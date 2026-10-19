// The GL view's shadows: each view's maps, the casters of a room kept by
// where they are, and the keys that say when a map must be laid again.
#include "sg/gl/World.hpp"

#include "WorldHash.hpp"

namespace sg::render {

// One layer of a depth array copied to the same layer of another (or the part of it in `rect`): a
// map's still casters kept, or laid again under what moves.
void GLWorldView::copy_depth(const gl::ShadowArray& from, const gl::ShadowArray& to, int layer, const int* rect) {
    constexpr gl::GLenum kFramebufferBinding = 0x8CA6;
    gl::GLint read = 0, draw = 0;
    from.bind_layer(layer);
    gl::glGetIntegerv(kFramebufferBinding, &read);
    to.bind_layer(layer);
    gl::glGetIntegerv(kFramebufferBinding, &draw);
    const int n = to.size();
    const int whole[4] = {0, 0, n, n};
    if (!rect) rect = whole;
    if (rect[0] < rect[2] && rect[1] < rect[3]) {
        gl::glBindFramebuffer(gl::GL_READ_FRAMEBUFFER, static_cast<gl::GLuint>(read));
        gl::glBindFramebuffer(gl::GL_DRAW_FRAMEBUFFER, static_cast<gl::GLuint>(draw));
        gl::glBlitFramebuffer(rect[0], rect[1], rect[2], rect[3], rect[0], rect[1], rect[2], rect[3], gl::GL_DEPTH_BUFFER_BIT, gl::GL_NEAREST);
    }
    gl::glBindFramebuffer(gl::GL_FRAMEBUFFER, static_cast<gl::GLuint>(draw));
    gl::glViewport(0, 0, n, n);
}

// A view's maps bound again where the scene reads their depths as they are
// (uShadowDepths): through a sampler that compares nothing and blends no
// texels - a depth between two is no depth anything has.
void GLWorldView::bind_shadow_depths(const ShadowSet& maps) {
    if (!shadow_depths_) {
        gl::GLint units = 0;
        gl::glGetIntegerv(gl::GL_MAX_TEXTURE_IMAGE_UNITS, &units);
        if (units <= kShadowDepthsUnit)
            std::fprintf(stderr, "render: %d texture units; a sun's penumbra wants %d\n", units, kShadowDepthsUnit + 1);
        gl::glGenSamplers(1, &shadow_depths_);
        gl::glSamplerParameteri(shadow_depths_, gl::GL_TEXTURE_COMPARE_MODE, static_cast<gl::GLint>(gl::GL_NONE));
        gl::glSamplerParameteri(shadow_depths_, gl::GL_TEXTURE_MIN_FILTER, static_cast<gl::GLint>(gl::GL_NEAREST));
        gl::glSamplerParameteri(shadow_depths_, gl::GL_TEXTURE_MAG_FILTER, static_cast<gl::GLint>(gl::GL_NEAREST));
        gl::glSamplerParameteri(shadow_depths_, gl::GL_TEXTURE_WRAP_S, static_cast<gl::GLint>(gl::GL_CLAMP_TO_EDGE));
        gl::glSamplerParameteri(shadow_depths_, gl::GL_TEXTURE_WRAP_T, static_cast<gl::GLint>(gl::GL_CLAMP_TO_EDGE));
    }
    maps.array.bind_depth(kShadowDepthsUnit);
    gl::glBindSampler(static_cast<gl::GLuint>(kShadowDepthsUnit), shadow_depths_);
    gl::glActiveTexture(gl::GL_TEXTURE0);
}

void GLWorldView::ShadowSet::forget() {
    for (std::size_t i = 0; i < kShadowMaps; ++i) {
        sig[i] = ident[i] = still_at[i] = layout[i] = movers[i] = 0, waits[i] = 0, base[i] = false;
        mrect[i][0] = mrect[i][1] = mrect[i][2] = mrect[i][3] = 0;
    }
}

GLWorldView& GLWorldView::shadow_pool() {
    GLWorldView* p = this;
    while (p->root_ && p->root_ != p) p = p->root_;
    return *p;
}

long long GLWorldView::shadow_room() {
    if (&shadow_pool() != this) return shadow_pool().shadow_room();
    if (shadow_room_ != 0) return shadow_room_;
    if (q_.shadow_budget_mb < 0) return shadow_room_ = -1;
    // As the card says it is (in kilobytes): how much it has, and how much
    // of it is free now (the first map made: the rest of the frame's targets
    // still to come, and whatever else shares the card).
    constexpr gl::GLenum kDedicatedNvx = 0x9047, kFreeNvx = 0x9049, kFreeAti = 0x87FC;
    const auto ask = [](gl::GLenum what) -> long long {
        gl::GLint kb[4] = {0, 0, 0, 0};
        gl::glGetIntegerv(what, kb);
        return gl::glGetError() == gl::GL_NO_ERROR && kb[0] > 0 ? static_cast<long long>(kb[0]) << 10 : 0;
    };
    const long long card = ask(kDedicatedNvx);
    long long free = ask(kFreeNvx);
    if (!free) free = ask(kFreeAti);
    // What it is told (render.conf), else a sixteenth of the card within
    // reason - and never more than a quarter of what is free: maps the card
    // has no room for are paged out and back every frame, every frame slow.
    long long room = q_.shadow_budget_mb > 0 ? static_cast<long long>(q_.shadow_budget_mb) << 20
                     : card > 0              ? std::clamp(card / 16, 512LL << 20, 2048LL << 20)
                     : free > 0              ? std::clamp(free / 16, 512LL << 20, 2048LL << 20)
                                             : 1024LL << 20;
    if (free > 0) room = std::min(room, std::max(free / 4, 256LL << 20));
    return shadow_room_ = room;
}

bool GLWorldView::let_go_of_oldest_shadows() {
    // (Never one asked for this frame or the last: what is being drawn. Of
    // every view's, in the pool: the one asked for longest ago, whoever's.)
    GLWorldView& pool = shadow_pool();
    auto& sets = pool.shadow_sets_;
    auto oldest = sets.end();
    for (auto it = sets.begin(); it != sets.end(); ++it)
        if (it->second->used + 1 < pool.frame_count_ && (oldest == sets.end() || it->second->used < oldest->second->used)) oldest = it;
    if (oldest == sets.end()) return false;
    for (gl::ShadowArray* a : {&oldest->second->array, &oldest->second->still})
        if (a->valid()) pool.spare_shadows_.push_back(std::move(*a));
    sets.erase(oldest);
    return true;
}

bool GLWorldView::fit_shadows(gl::ShadowArray& maps, int size, int layers, bool keep) {
    const long long room = shadow_room();
    auto& pool = shadow_pool();
    layers = std::max(1, layers);
    if (maps.valid() && maps.size() == size && maps.layers() >= layers) return false;
    // (Grown, and asked to keep what it holds: the old maps copied, layer by
    // layer, into the new - nothing laid again.)
    gl::ShadowArray old;
    const bool grow = keep && maps.valid() && maps.size() == size;
    if (grow) old = std::move(maps);
    const auto kept = [&] {
        if (!grow) return true;
        for (int l = 0; l < old.layers() && l < maps.layers(); ++l) copy_depth(old, maps, l);
        gl::glBindFramebuffer(gl::GL_FRAMEBUFFER, 0);
        pool.spare_shadows_.push_back(std::move(old));
        return false;
    };
    if (room < 0) {
        maps.ensure(size, layers);
        return kept();
    }
    if (maps.valid()) pool.spare_shadows_.push_back(std::move(maps));
    // A spare that holds them and little more (the smallest), else room made
    // for them: a spare much bigger would hold the card for nothing.
    const auto take_spare = [&] {
        auto best = pool.spare_shadows_.end();
        for (auto it = pool.spare_shadows_.begin(); it != pool.spare_shadows_.end(); ++it)
            if (it->size() == size && it->layers() >= layers && it->layers() <= layers + layers / 4 + 1 &&
                (best == pool.spare_shadows_.end() || it->layers() < best->layers()))
                best = it;
        if (best == pool.spare_shadows_.end()) return false;
        maps = std::move(*best);
        pool.spare_shadows_.erase(best);
        return true;
    };
    if (take_spare()) return kept();
    const long long need = static_cast<long long>(size) * size * layers * 4;
    while (gl::ShadowArray::made_bytes() + need > room) {
        if (!pool.spare_shadows_.empty()) {
            pool.spare_shadows_.pop_back();  // (none of them would do)
            continue;
        }
        if (!let_go_of_oldest_shadows()) break;  // (what is being drawn needs more than there is room for)
        if (take_spare()) return kept();
    }
    maps.ensure(size, layers);
    return kept();
}

void GLWorldView::keep_shadows_within_room() {
    const long long room = shadow_room();
    auto& pool = shadow_pool();
    if (room < 0) return;
    while (gl::ShadowArray::made_bytes() > room) {
        if (!pool.spare_shadows_.empty()) {
            pool.spare_shadows_.pop_back();
            continue;
        }
        if (!let_go_of_oldest_shadows()) break;
    }
}

auto GLWorldView::shadows_for(const void* world, const void* view) -> ShadowSet& {
    // Never more than the views there can be: past that, the set asked for
    // longest ago goes - one, never all of them at once (every view would
    // draw its maps again in the same frame). (In the pool, keyed by whose:
    // a view's sets are laid from its own casters.)
    GLWorldView& pool = shadow_pool();
    auto& sets = pool.shadow_sets_;
    const void* whose = reinterpret_cast<const void*>(reinterpret_cast<std::uintptr_t>(view) ^ (reinterpret_cast<std::uintptr_t>(this) * 1099511628211ULL));
    if (sets.size() >= 64 && !sets.count({world, whose})) {
        auto oldest = sets.begin();
        for (auto it = sets.begin(); it != sets.end(); ++it)
            if (it->second->used < oldest->second->used) oldest = it;
        sets.erase(oldest);
    }
    auto& set = sets[{world, whose}];
    if (!set) set = std::make_unique<ShadowSet>();
    set->used = pool.frame_count_;
    return *set;
}

uint64_t GLWorldView::lights_key(const std::vector<PlacedRoom>& rooms, const std::vector<RoomCasters*>& casters) const {
    // What a room's lights are: its lamps (where each hangs, what it gives),
    // where its doorways are and what they let through - and so what stands
    // in them. Not the room's data as a whole, and never the camera: a
    // creature moving about, or the viewer walking, lights nothing again.
    uint64_t h = 1469598103934665603ULL;
    for (std::size_t r = 0; r < rooms.size(); ++r) {
        const PlacedRoom& p = rooms[r];
        if (!p.room || p.image) continue;
        const State& room = *p.room;
        h = fnv(h, reinterpret_cast<std::uintptr_t>(p.room));
        h = fnv(h, room.structure());
        for (double v : {p.pose.position.x, p.pose.position.y, p.pose.position.z, p.pose.yaw, p.pose.pitch, p.pose.roll}) h = fnv(h, bits_of(v));
        for (Key d : p.doorways) h = fnv(h, std::hash<Key>{}(d));
        const KindIndex& ix = index_of(room);
        // (A lamp lit by a picture is a lamp like any other: its room eases
        // it, and its stamp says when it moved.)
        for (const Element* e : ix.lights) h = fnv(h, (chain_stamp(room, *e, slot_of(*e).chain) << 1) | (e->alive ? 1u : 0u));
        const RoomCasters& rc = *casters[r];
        h = fnv(h, rc.flip_count);
        for (const Element* e : ix.portals) {
            h = fnv(h, (chain_stamp(room, *e, slot_of(*e).chain) << 1) | (e->alive ? 1u : 0u));
            // What stands in a doorway lets less through (covered): what
            // moves near one is part of its light.
            if (!e->alive || is_screen(*e) || e->params.num(Key{"light"}, 1.0) <= 0.0) continue;
            auto it = worlds_.find(e->id);
            if (it == worlds_.end() || !it->second.world || rc.movers.empty()) continue;
            const Pose door = pose_of(room, *e);
            const double reach = e->params.num(keys::w, 3.0) * 0.5 + 1.6;
            for (uint32_t k : rc.movers) {
                const Caster& c = rc.list[k];
                const double dx = c.lx - door.position.x, dz = c.lz - door.position.z;
                if (dx * dx + dz * dz <= reach * reach) h = fnv(fnv(h, reinterpret_cast<std::uintptr_t>(c.element)), c.where);
            }
        }
    }
    return h;
}

uint64_t GLWorldView::worlds_stamp() const {
    // Every world a doorway opens onto - its lights - and how it is seen:
    // what comes in through a doorway is theirs. (Asked by every view in a
    // frame, and the same for all: worked out once.)
    auto& memo = worlds_memo_;
    if (memo.first == frame_count_ + 1) return memo.second;
    uint64_t h = 1469598103934665603ULL;
    for (const auto& [id, wp] : worlds_) {
        if (!wp.world) continue;
        h = (h ^ std::hash<Key>{}(id)) * 1099511628211ULL;
        // Its lights, not all it holds: snow falling in it lets in no more
        // light than it did.
        for (const Element* e : index_of(*wp.world).lights) h = (h ^ chain_stamp(*wp.world, *e, slot_of(*e).chain) ^ (e->alive ? 1u : 0u)) * 1099511628211ULL;
        // And the sky it lets in (through_doorways), not the look as a whole:
        // grain and clouds move every frame, and light nothing again.
        static const Key sky[] = {Key{"uAmbient"}, Key{"uSky.x"}, Key{"uSky.y"}, Key{"uSky.z"}};
        const LookState& look = look_of(*wp.world);
        for (const Key& k : sky) h = fnv(h, bits_of(value(look, passes::scene, k, -1.0)));
    }
    memo = {frame_count_ + 1, h};
    return h;
}

void GLWorldView::flipped(RoomCasters& rc, gl::Vec3 centre, float radius, bool all) {
    ++rc.flip_count;
    rc.flips.push_back({++flip_clock_, centre, radius, all});
    if (rc.flips.size() > 48) {
        // (A map laid before these asks again, whatever it sees.)
        const std::size_t drop = rc.flips.size() / 2;
        rc.trimmed = rc.flips[drop - 1].at;
        rc.flips.erase(rc.flips.begin(), rc.flips.begin() + static_cast<std::ptrdiff_t>(drop));
    }
}

auto GLWorldView::casters_in(const PlacedRoom& placed, std::size_t r) -> RoomCasters& {
    // A room's casters as one placement of it sees them: kept by where the
    // room stands (its pose is part of where things are) and which of the
    // rooms drawn it is.
    uint64_t key = fnv(1469598103934665603ULL, reinterpret_cast<std::uintptr_t>(placed.room));
    for (double v : {placed.pose.position.x, placed.pose.position.y, placed.pose.position.z, placed.pose.yaw, placed.pose.pitch, placed.pose.roll}) key = fnv(key, bits_of(v));
    auto& slot = room_casters_[key];
    if (!slot) {
        slot = std::make_unique<RoomCasters>();
        slot->room = r;
        slot->state = placed.room;
        if (room_casters_.size() > 48)
            for (auto it = room_casters_.begin(); it != room_casters_.end();)
                it = (it->second && it->second != slot && it->second->used + 600 < frame_count_) ? room_casters_.erase(it) : std::next(it);
    }
    slot->used = frame_count_;
    // (Which of this frame's rooms it is: the order may change, the room does not.)
    if (slot->room != r) {
        slot->room = r;
        for (Caster& c : slot->list) c.room = r;
        for (Caster& c : slot->extras) c.room = r;
    }
    refresh_casters(*slot, placed);
    return *slot;
}

void GLWorldView::refresh_casters(RoomCasters& rc, const PlacedRoom& placed) {
    if (rc.refreshed == frame_count_) return;
    rc.refreshed = frame_count_;
    const Spatial3D& room = *placed.room;
    set_frame(placed.pose);
    uint64_t frame_hash = 1469598103934665603ULL;
    for (float f : frame_matrix_.m) frame_hash = mix_bits(frame_hash, f);
    const bool fresh = rc.structure != room.structure();
    if (fresh) {
        rc.structure = room.structure();
        rc.list.clear();
        rc.flips.clear();
        rc.trimmed = 0;
        for (const Element& e : room.elements()) {
            if (e.kind != kinds::mesh && e.kind != kinds::wall) continue;
            Caster c;
            c.room = rc.room, c.element = &e, c.hold = kMoverFrames, c.slot = &slot_of(e);
            c.stamp = ~uint64_t{0};  // (never read: read below)
            rc.list.push_back(c);
        }
        flipped(rc, {}, 0.0f, true);
    }
    rc.movers.clear();
    rc.movers_key = 0;
    for (std::size_t k = 0; k < rc.list.size(); ++k) {
        Caster& c = rc.list[k];
        const Element& e = *c.element;
        // (What hangs off nothing is its own stamp: asked every frame of every
        // thing, so cheap.)
        const uint64_t stamp = c.chained ? chain_stamp(room, e, c.slot->chain) : e.params.stamp();
        if (stamp != c.stamp || e.alive != c.alive) {
            const bool was_on = c.on, was_at_rest = !c.mover;
            const gl::Vec3 was_at = c.centre;
            const float was_r = c.radius;
            const uint64_t was_where = c.where;
            c.chained = e.params.has(keys::parent);
            c.stamp = c.chained ? chain_stamp(room, e, c.slot->chain) : e.params.stamp();
            c.alive = e.alive, c.on = false;
            // A lamp's own shade does not shadow its lamp; a sprite, a
            // picture turned to the eye, has no shape to cast.
            if (e.alive && !is_sprite(e) && e.params.num(Key{"cast"}, 1.0) >= 0.5) {
                // Where it is and what shape: worked out once each time
                // its parameters (or its anchor's) change - a change of
                // colour or glow is no change to a shadow.
                Placed& p = placed_in(*c.slot, room, e);
                if (!p.hashed) {
                    // To the tenth of a millimetre: a cord settling by less
                    // than that draws no shadow again.
                    uint64_t w = 1469598103934665603ULL;
                    for (float f : box_matrix(room, e).m.m) w = mix_bits(w, std::round(f * 1e4f));
                    w = (w ^ reinterpret_cast<std::uintptr_t>(&shape_of(room, e))) * 1099511628211ULL;
                    p.where = w, p.hashed = true;
                }
                if (p.held_in != frame_hash) {
                    const DrawBound b = query_bounds(box_matrix(room, e));
                    p.held_at = b.centre, p.held_r = b.radius, p.held_in = frame_hash;
                }
                c.centre = p.held_at, c.radius = p.held_r, c.bounded = true, c.where = p.where, c.on = true;
                c.lx = static_cast<float>(pose_of(room, e).position.x), c.lz = static_cast<float>(pose_of(room, e).position.z);
            }
            if (!fresh && (c.on != was_on || (c.on && c.where != was_where))) {
                // What the maps that laid it at rest have of it is where it was.
                if (was_on && was_at_rest) flipped(rc, was_at, was_r, false);
                if (c.moved) c.hold = std::min<uint32_t>(c.hold * 2, 4096);  // it started again: it stays a mover longer
                c.moved = frame_count_;
            }
        }
        const bool mover = c.on && c.moved && frame_count_ - c.moved < c.hold;
        if (mover != c.mover) {
            // Come to rest: laid with the rest from where it is now.
            if (!mover && c.on) flipped(rc, c.centre, c.radius, false);
            c.mover = mover;
        }
        if (mover) {
            rc.movers.push_back(static_cast<uint32_t>(k));
            rc.movers_key = fnv(fnv(rc.movers_key, reinterpret_cast<std::uintptr_t>(c.element)), c.where);
        }
    }
    // Terrain and panels (a screen's frame, a ball's glass) depend on more
    // than the room's own data - the terrain streamed in, a doorway declared
    // - and there are few: asked again each frame.
    const KindIndex& ix = index_of(room);
    rc.extras.clear();
    rc.extras_key = 1469598103934665603ULL;
    for (const Element* ep : ix.terrains) {
        const Element& e = *ep;
        if (!e.alive || e.params.num(Key{"cast"}, 1.0) < 0.5) continue;
        auto t = terrains_.find(e.id);
        if (t == terrains_.end()) continue;
        Caster c;
        c.room = rc.room, c.element = &e;
        uint64_t w = 1469598103934665603ULL;
        w = mix_bits(w, static_cast<float>(t->second.cx));
        w = mix_bits(w, static_cast<float>(t->second.cz));
        w = mix_bits(w, static_cast<float>(t->second.rev));
        c.where = w;
        rc.extras_key = fnv(fnv(rc.extras_key, reinterpret_cast<std::uintptr_t>(c.element)), c.where);
        rc.extras.push_back(c);
    }
    // Things strewn by the many: those near the eye cast (strewn_into) -
    // which they are moves with it, a cell at a time.
    for (const Element* ep : ix.strewn) {
        const Element& e = *ep;
        if (!e.alive || e.params.num(Key{"cast"}, 1.0) < 0.5) continue;
        Caster c;
        c.room = rc.room, c.element = &e;
        uint64_t w = strewn_cells(room, e).made;
        w = mix_bits(w, std::floor(cam_eye_.x / 16.0f));
        w = mix_bits(w, std::floor(cam_eye_.z / 16.0f));
        c.where = w;
        rc.extras_key = fnv(fnv(rc.extras_key, reinterpret_cast<std::uintptr_t>(c.element)), c.where);
        rc.extras.push_back(c);
    }
    for (const Element* ep : ix.portals) {
        const Element& e = *ep;
        // (`cast` 0: it throws no shadow - as a thing that says so throws none.)
        if (!e.alive || is_doorway(room, e) || !has_surface(e) || e.params.num(Key{"cast"}, 1.0) < 0.5) continue;
        Caster c;
        c.room = rc.room, c.element = &e;
        const RoomMatrix frame = portal_frame_model(room, e);
        uint64_t w = 1469598103934665603ULL;
        for (float f : frame.m.m) w = mix_bits(w, f);
        const DrawBound b = query_bounds(frame);
        c.centre = b.centre, c.radius = b.radius, c.bounded = true, c.where = w;
        rc.extras_key = fnv(fnv(rc.extras_key, reinterpret_cast<std::uintptr_t>(c.element)), c.where);
        rc.extras.push_back(c);
    }
    set_frame(Pose{});
}

bool GLWorldView::shades(const Caster& c, const Frustum& sees, const Light& light) {
    if (!c.bounded) return true;
    if (!sees.intersects_sphere({c.centre.x, c.centre.y, c.centre.z}, c.radius)) return false;
    if (light.gated) {
        // (The same plane the caster pass cuts at: a hand's breadth of slack.)
        const float side = gl::dot(light.gate_in, {c.centre.x, c.centre.y, c.centre.z}) + 0.08f - gl::dot(light.gate_in, light.gate_at);
        if (side < -c.radius) return false;
    }
    return true;
}

}  // namespace sg::render
