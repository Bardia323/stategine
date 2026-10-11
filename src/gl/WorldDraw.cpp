#include <map>
#include <cstdio>
#include <cstdlib>
// The GL view drawing a room: its shadow maps, its air, and its scene
// (GLWorldView::draw_world).
#include "sg/gl/World.hpp"

#include "WorldHash.hpp"

namespace sg::render {

namespace {
// The pixels of a light's depth map (n square) that a sphere can fall on, a
// pixel to spare; the whole map if it could fall anywhere (it reaches behind
// the light).
void depth_rect(const gl::Mat4& vp, const gl::Vec3& c, float r, int n, int rect[4]) {
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int k = 0; k < 8; ++k) {
        const float x = c.x + ((k & 1) ? r : -r), y = c.y + ((k & 2) ? r : -r), z = c.z + ((k & 4) ? r : -r);
        const float w = vp.m[3] * x + vp.m[7] * y + vp.m[11] * z + vp.m[15];
        if (w <= 1e-3f) {
            rect[0] = rect[1] = 0, rect[2] = rect[3] = n;
            return;
        }
        const float u = (vp.m[0] * x + vp.m[4] * y + vp.m[8] * z + vp.m[12]) / w;
        const float v = (vp.m[1] * x + vp.m[5] * y + vp.m[9] * z + vp.m[13]) / w;
        x0 = std::min(x0, u), x1 = std::max(x1, u), y0 = std::min(y0, v), y1 = std::max(y1, v);
    }
    const float h = static_cast<float>(n) * 0.5f;
    rect[0] = std::clamp(static_cast<int>(std::floor((x0 + 1.0f) * h)) - 1, 0, n);
    rect[1] = std::clamp(static_cast<int>(std::floor((y0 + 1.0f) * h)) - 1, 0, n);
    rect[2] = std::clamp(static_cast<int>(std::ceil((x1 + 1.0f) * h)) + 1, 0, n);
    rect[3] = std::clamp(static_cast<int>(std::ceil((y1 + 1.0f) * h)) + 1, 0, n);
}
}  // namespace

void GLWorldView::draw_world(const std::vector<PlacedRoom>& given, const Camera& cam, float aspect, gl::RenderTarget& target, int depth, float znear, Key skip_portal, const std::vector<HalfSpace>& clips) {
    // A space that wraps is seen in every copy of it the eye can reach -
    // from inside it or through any doorway onto it: every view is drawn
    // here, so every view sees the same space.
    const auto part_from = std::chrono::steady_clock::now();
    const auto lap = [&](int k, std::chrono::steady_clock::time_point& at) {
        const auto now = std::chrono::steady_clock::now();
        times_.part[k] += std::chrono::duration<double, std::milli>(now - at).count();
        at = now;
    };
    auto part_at = part_from;
    std::vector<PlacedRoom> rooms = given;
    for (std::size_t r = 0, own = given.size(); r < own; ++r) {
        if (!given[r].room) continue;
        const Spatial3D& room = *given[r].room;
        const Vec3d p = period_of(room);
        if (p.x <= 0 && p.y <= 0 && p.z <= 0) continue;
        const Vec3d eye = local_of(given[r].pose, {cam.eye.x, cam.eye.y, cam.eye.z});
        for (const Vec3d& o : images(room, eye, room.params().num(Key{"far"}, 120.0))) {
            if (o.x == 0 && o.y == 0 && o.z == 0) continue;
            PlacedRoom copy = given[r];
            copy.pose = compose_pose(given[r].pose, Pose{o});
            copy.image = true;
            rooms.push_back(copy);
        }
    }
    std::size_t shadowed = 0;
    // What casts, in each room seen: kept from frame to frame, read again
    // only where its own stamps moved (refresh_casters).
    const auto sig_from = std::chrono::steady_clock::now();
    std::vector<RoomCasters*> entries(rooms.size(), nullptr);
    for (std::size_t r = 0; r < rooms.size(); ++r)
        if (rooms[r].room && !rooms[r].image) entries[r] = &casters_in(rooms[r], r);
    times_.signature += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - sig_from).count();
    const auto lights_from = std::chrono::steady_clock::now();
    const uint64_t lit_key = (lights_key(rooms, entries) ^ worlds_stamp()) * 1099511628211ULL;
    if (lights_memo_.size() > 64) lights_memo_.clear();
    auto lit_memo = lights_memo_.find(lit_key);
    if (lit_memo == lights_memo_.end()) {
        LightsMemo m;
        m.lights = read_lights(rooms, m.shadowed);
        ++times_.lights_read;
        lit_memo = lights_memo_.emplace(lit_key, std::move(m)).first;
    }
    const std::vector<Light>& lights = lit_memo->second.lights;
    shadowed = lit_memo->second.shadowed;
    times_.lights_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - lights_from).count();
    cam_eye_ = cam.eye, cam_forward_ = cam.forward, cam_up_ = cam.up;
    guest_pass_ = depth > 0;
    lod_px_ = 0.5f * static_cast<float>(target.height()) / std::tan(cam.fov * 0.5f);
    lod_least_ = static_cast<float>(rooms.front().room->params().num(Key{"lod_px"}, 0.75));
    const float zfar = static_cast<float>(rooms.front().room->params().num(Key{"far"}, 120.0));
    for (const PlacedRoom& placed : rooms)
        if (placed.room)
            for (const auto& e : placed.room->elements())
                if (e.alive && e.kind == terrain_kind()) {
                    // The state's own ground, unless the program bound one.
                    TerrainMesh& t = terrains_[e.id];
                    if (!t.height)
                        if (const auto* space = dynamic_cast<const Spatial3D*>(placed.room))
                            if (const Spatial3D::Height* h = space->ground(e.id)) t.height = *h;
                    ensure_terrain(e, cam);
                }
    // The strongest lamps get a shadow map each. What the scene shader is told
    // of each map is how wide a texel of it is in the world, at a unit's
    // distance from a lamp (or anywhere, for a sun's box): it keeps its
    // lookups that far off the surface (shadow_factor).
    const int shadow_px = q_.shadow_size;
    const auto texel_of = [&](const Light& l) {
        const float size = static_cast<float>(std::max(shadow_px, 1));
        if (l.sun) return 2.0f * l.extent / size;
        return 2.0f * std::tan(std::min(l.outer * 2.05f, 2.7f) * 0.5f) / size;
    };
    gl::Mat4 light_vp[kShadowMaps];
    float bias[kShadowMaps];
    std::fill(bias, bias + kShadowMaps, 1.0f);
    // Each light with maps takes its layers in turn: one, or - a lamp with no
    // cone - six, a face of a cube round it each (shadow_faces). Which light
    // a layer is of, and which face of its cube (-1: the light's one map).
    std::array<std::size_t, kShadowMaps> layer_light{};
    std::array<int, kShadowMaps> layer_face{};
    std::vector<float> first_layer(lights.size(), -1.0f), cube_of(lights.size(), 0.0f);
    std::size_t layers = 0, cube_extra = 0;
    {
        const ViewCamera eye{{cam.eye.x,cam.eye.y,cam.eye.z},{cam.forward.x,cam.forward.y,cam.forward.z},{cam.up.x,cam.up.y,cam.up.z}};
        const float face_texel = 2.0f * kCubeFaceSpread / static_cast<float>(std::max(shadow_px, 1));
        for (std::size_t i = 0; i < shadowed && layers < kShadowMaps; ++i) {
            const Light& l = lights[i];
            const int faces = shadow_faces(l);
            first_layer[i] = static_cast<float>(layers);
            cube_of[i] = faces > 1 ? 1.0f : 0.0f;
            for (int f = 0; f < faces && layers < kShadowMaps; ++f, ++layers) {
                layer_light[layers] = i;
                layer_face[layers] = faces > 1 ? f : -1;
                if (faces > 1) {
                    light_vp[layers] = shadow_face(l, f);
                    bias[layers] = face_texel;
                } else {
                    light_vp[layers] = shadow_projection(l, eye, shadow_px, bias[layers]);
                    bias[layers] = texel_of(l);
                }
            }
            cube_extra += static_cast<std::size_t>(faces - 1);
        }
    }
    if (std::getenv("SG_SHADOW_PICK")) {
        std::string pick;
        char buf[96];
        for (std::size_t i = 0; i < shadowed; ++i) {
            std::snprintf(buf, sizeof buf, " [%.1f %.1f %.1f%s%s]", lights[i].pos.x, lights[i].pos.y, lights[i].pos.z, lights[i].gated ? " g" : "", shadow_faces(lights[i]) > 1 ? " o" : "");
            pick += buf;
        }
        std::fprintf(stderr, "pick f%llu d%d %s n%zu sh%zu L%zu:%s\n", (unsigned long long)frame_count_, depth, rooms.front().room->id().str().c_str(), lights.size(), shadowed, layers, pick.c_str());
    }
    // A sun whose map reaches far gets a second, small one round the viewer
    // (in the layers after the lamps'): close up, a texel is a centimetre,
    // not a hand's breadth - a door's edge, a chair's legs, a leaf half in
    // each room would otherwise show the steps of the wide one. (As many as
    // there would be with no cube: a cube's six count as its lamp's one.)
    constexpr float kNearReach = 6.0f;
    std::vector<float> near_of(lights.size(), -1.0f);
    for (std::size_t i = 0; i < shadowed && layers < kShadowMaps && layers - cube_extra < kShadowLights; ++i) {
        const Light& l = lights[i];
        if (!l.sun || l.indirect || l.extent <= 2.0f * kNearReach) continue;
        Light close = l;
        close.extent = kNearReach;
        close.pinned = false;
        const ViewCamera eye{{cam.eye.x,cam.eye.y,cam.eye.z},{cam.forward.x,cam.forward.y,cam.forward.z},{cam.up.x,cam.up.y,cam.up.z}};
        light_vp[layers] = shadow_projection(close, eye, shadow_px, bias[layers]);
        bias[layers] = texel_of(close);
        near_of[i] = static_cast<float>(layers);
        layer_light[layers] = i;
        layer_face[layers] = -1;
        ++layers;
    }
    // Narrowed to the part of the screen drawn (sub_): that part fills the
    // viewport, at the same pixels it has on the screen.
    const bool part = sub_.x0 > -1 || sub_.y0 > -1 || sub_.x1 < 1 || sub_.y1 < 1;
    const float hx = (sub_.x1 - sub_.x0) * 0.5f, hy = (sub_.y1 - sub_.y0) * 0.5f;
    const gl::Mat4 narrow = part ? gl::Mat4::scale({1.0f / hx, 1.0f / hy, 1.0f}) * gl::Mat4::translate({-(sub_.x0 + sub_.x1) * 0.5f, -(sub_.y0 + sub_.y1) * 0.5f, 0.0f})
                                 : gl::Mat4::identity();
    // (The screen's pixels: a part's picture need only be as big as the part.)
    vp_w_ = part ? std::round(static_cast<float>(target_w_) * hx) : static_cast<float>(target.width());
    vp_h_ = part ? std::round(static_cast<float>(target_h_) * hy) : static_cast<float>(target.height());
    const gl::Mat4 lens = projection_of(cam, aspect, znear, zfar), looking = gl::Mat4::look_at(cam.eye, cam.eye + cam.forward, cam.up);
    const gl::Mat4 view_proj = narrow * lens * looking;
    // As drawn: with its depth reversed, where it may be (near 1, far 0, in
    // float: gl::reversed_depth) - the lens's own depth row turned, before the
    // view is put to it, so nothing of a float's precision is lost - and moved
    // by this frame's part of a pixel, for the views drawn at the screen's
    // pixels (temporal antialiasing). Nothing else sees either: the air, what
    // is culled and what a view is kept by go by the view as it is.
    const bool reversed = gl::reversed_depth();
    gl::Mat4 drawn_lens = lens;
    if (reversed)
        for (int c = 0; c < 4; ++c) drawn_lens.m[c * 4 + 2] = 0.5f * lens.m[c * 4 + 3] - 0.5f * lens.m[c * 4 + 2];
    const bool jittered = depth <= 1 && (jitter_x_ != 0.0f || jitter_y_ != 0.0f);
    const gl::Mat4 drawn_proj = (jittered ? gl::Mat4::translate({2.0f * jitter_x_ / vp_w_, 2.0f * jitter_y_ / vp_h_, 0.0f}) : gl::Mat4::identity()) *
                                narrow * drawn_lens * looking;
    if (depth == 0 && taa_on()) {
        taa_vp_ = view_proj;
        taa_world_ = rooms.front().room;
        taa_eye_ = cam.eye;
    }

    if (timing_) gl::glFinish();
    const auto shadow_start = std::chrono::steady_clock::now();
    // --- shadow depth, one pass per shadowed lamp ------------------------
    // Each world seen (the room, and whatever a portal shows) keeps its own
    // maps, and a map is drawn again only when its lamp has moved or
    // turned, or anything that casts has: most frames, nothing has, and
    // the shadows cost nothing.
    // Each view keeps its own maps: two windows onto one world see it
    // lit differently (each lets in its own room's light), and would
    // otherwise draw over each other's maps every frame.
    // A lamp's map is the lamp's and the casters', whoever looks: views of a
    // world lit the same way share its maps (a room glued to itself, seen
    // through doorway after doorway, draws them once). Only a sun whose box
    // goes where the eye goes keeps a set for each view.
    // (Which lights, not where they are this moment - a sun goes round with
    // the day; whether each map still holds is its own signature's to say.)
    // (One set for a world, whichever of its lamps and of the lamps let in
    // by its doorways cast now: each light's maps are found in it where they
    // were laid - below - so a lamp come into sight through a doorway, or
    // gone, costs its own map, never the room's every map again.)
    uint64_t lit = 1469598103934665603ULL;
    bool follows_eye = false;
    for (std::size_t i = 0; i < layers; ++i) {
        const Light& l = lights[layer_light[i]];
        follows_eye = follows_eye || (l.sun && !l.pinned) || near_of[layer_light[i]] >= 0.0f;
    }
    // (A view is the way the eye came to it - its path, the same from frame
    // to frame whatever picture it is drawn into.)
    if (follows_eye) {
        lit = (lit ^ std::hash<std::string>{}(path_)) * 1099511628211ULL;
        lit = (lit ^ reinterpret_cast<std::uintptr_t>(root_)) * 1099511628211ULL;
    }
    // A view asked only what its surfaces are (see_into: where, which way,
    // what of the light) lights nothing, and so lays no maps: one set of a
    // texel stands for them, never a set of full maps a room for nothing.
    const bool lightless = surface_only_ != 0;
    // (A space glued into one keeps its maps by the room its frame is - the
    // one placed where the frame is - whichever of its rooms the eye is in.)
    const void* frame_room = rooms.front().room;
    for (const PlacedRoom& pr : rooms)
        if (pr.room && !pr.image && pr.pose.position.x == 0.0 && pr.pose.position.y == 0.0 && pr.pose.position.z == 0.0 && pr.pose.yaw == 0.0) {
            frame_room = pr.room;
            break;
        }
    ShadowSet& maps = lightless ? lightless_maps_ : shadows_for(frame_room, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(lit)));
    // Each light's maps in the layers they were last laid in, where they
    // still hold (the same light, the same box): lights that come in another
    // order - one more let in by a doorway, one gone - keep theirs, and only
    // a light not laid before takes a layer of its own, the one used longest
    // ago. (A lamp with no cone keeps its six together.)
    std::array<bool, kShadowMaps> in_use{};
    if (!lightless && layers > 0) {
        std::array<uint64_t, kShadowMaps> want{};
        std::array<bool, kShadowMaps> near_layer{};
        for (std::size_t j = 0; j < layers; ++j) {
            const Light& l = lights[layer_light[j]];
            near_layer[j] = near_of[layer_light[j]] == static_cast<float>(j);
            uint64_t id = fnv(fnv(fnv(l.sun ? 1 : 2, l.gate), near_layer[j] ? 1 : 0), l.sun ? 0 : 1);
            if (layer_face[j] >= 0) id = fnv(id, static_cast<uint64_t>(layer_face[j]) + 7);
            want[j] = id;
        }
        struct Block {
            std::size_t from, size, to;
        };
        std::vector<Block> blocks;
        for (std::size_t j = 0; j < layers;) {
            std::size_t k = j + 1;
            if (layer_face[j] >= 0)
                while (k < layers && layer_light[k] == layer_light[j] && layer_face[k] >= 0) ++k;
            blocks.push_back({j, k - j, kShadowMaps});
            j = k;
        }
        std::array<bool, kShadowMaps> taken{};
        const auto holds = [&](std::size_t s, const Block& b) {
            if (s + b.size > kShadowMaps) return false;
            for (std::size_t f = 0; f < b.size; ++f)
                if (taken[s + f] || maps.sig[s + f] == 0 || maps.ident[s + f] != want[b.from + f] ||
                    std::memcmp(maps.vp[s + f].m, light_vp[b.from + f].m, sizeof light_vp[0].m) != 0)
                    return false;
            return true;
        };
        for (Block& b : blocks)
            for (std::size_t s = 0; s + b.size <= kShadowMaps; ++s)
                if (holds(s, b)) {
                    b.to = s;
                    for (std::size_t f = 0; f < b.size; ++f) taken[s + f] = true;
                    break;
                }
        // The rest where nothing wanted now was laid, used longest ago -
        // within the layers there are, before any more are made.
        const std::size_t have = maps.array.valid() && maps.array.size() == shadow_px ? static_cast<std::size_t>(maps.array.layers()) : 0;
        bool fits = true;
        for (Block& b : blocks) {
            if (b.to != kShadowMaps) continue;
            std::size_t best = kShadowMaps;
            uint64_t best_age = ~uint64_t{0};
            bool best_inside = false;
            for (std::size_t s = 0; s + b.size <= kShadowMaps; ++s) {
                bool free = true;
                uint64_t age = 0;
                for (std::size_t f = 0; f < b.size && free; ++f) free = !taken[s + f], age = std::max(age, maps.held[s + f]);
                if (!free) continue;
                const bool inside = s + b.size <= have;
                if (best == kShadowMaps || (inside && !best_inside) || (inside == best_inside && age < best_age))
                    best = s, best_age = age, best_inside = inside;
            }
            if (best == kShadowMaps) {
                fits = false;
                break;
            }
            b.to = best;
            for (std::size_t f = 0; f < b.size; ++f) taken[best + f] = true;
        }
        if (fits) {
            gl::Mat4 vp_at[kShadowMaps];
            float bias_at[kShadowMaps];
            std::fill(bias_at, bias_at + kShadowMaps, 1.0f);
            std::array<std::size_t, kShadowMaps> light_at{};
            std::array<int, kShadowMaps> face_at{};
            face_at.fill(-1);
            std::size_t top = 0;
            for (const Block& b : blocks) {
                for (std::size_t f = 0; f < b.size; ++f) {
                    const std::size_t s = b.to + f, j = b.from + f;
                    vp_at[s] = light_vp[j], bias_at[s] = bias[j], light_at[s] = layer_light[j], face_at[s] = layer_face[j];
                    in_use[s] = true;
                    top = std::max(top, s + 1);
                }
                const std::size_t li = layer_light[b.from];
                if (near_layer[b.from]) near_of[li] = static_cast<float>(b.to);
                else first_layer[li] = static_cast<float>(b.to);
            }
            std::copy(vp_at, vp_at + kShadowMaps, light_vp);
            std::copy(bias_at, bias_at + kShadowMaps, bias);
            layer_light = light_at, layer_face = face_at;
            layers = top;
        } else {
            for (std::size_t j = 0; j < layers; ++j) in_use[j] = true;
        }
        for (std::size_t s = 0; s < layers; ++s)
            if (in_use[s]) maps.held[s] = frame_count_;
    }
    if (lightless ? maps.array.ensure(1, 1) : fit_shadows(maps.array, shadow_px, static_cast<int>(std::max<std::size_t>(layers, 1)))) maps.forget();
    // Which rooms' casters these maps are laid from (their lists, and where
    // each room stands, which is part of the key of each).
    // (Whatever order they are drawn in: the same rooms are the same layout.)
    std::vector<std::uintptr_t> laid;
    for (const RoomCasters* rc : entries)
        if (rc) laid.push_back(reinterpret_cast<std::uintptr_t>(rc));
    std::sort(laid.begin(), laid.end());
    uint64_t layout_now = 1469598103934665603ULL;
    for (std::uintptr_t a : laid) layout_now = fnv(layout_now, a);
    gl::glEnable(gl::GL_DEPTH_TEST);
    gl::glEnable(gl::GL_CULL_FACE);
    // Faces towards the lamp: the shadow starts where the thing does (its
    // lookups keep off the surface by the normal, shadow_factor), not at its
    // far side - which leaves light under a thing standing on the floor.
    gl::glCullFace(gl::GL_BACK);
    const gl::Program& caster = *program_for(post_.shown(), passes::shadow);
    bool caster_ready = false;
    // The light's view and side the casters are drawn with now, for a thing
    // cut out of its picture, drawn with a program of its own.
    gl::Mat4 caster_vp;
    float caster_side[4] = {0, 0, 0, 1};
    // A thin thing (`two_sided`) casts from either face, and one cut out of
    // its picture (`cutout`) casts only where it is not cut away - a leaf's
    // shadow, not its card's: each drawn on its own.
    const auto cast_thin = [&](const Spatial3D& room, const Element& e) {
        static const Key two{"two_sided"}, cutout{"cutout"};
        const bool cut = cuts_out(e) && cut_caster_;
        if (!cut && e.params.num(two, 0.0) <= 0.5) return false;
        gl::glDisable(gl::GL_CULL_FACE);
        const gl::Mat4 model = frame_matrix_ * box_matrix(room, e).m;
        if (cut && bind_skin(room, e)) {
            cut_caster_->use();
            cut_caster_->set("uLightViewProj", caster_vp);
            cut_caster_->set("uCasterSide", caster_side[0], caster_side[1], caster_side[2], caster_side[3]);
            cut_caster_->set("uModel", model);
            cut_caster_->set("uTex", 0);
            cut_caster_->set("uCutout", static_cast<float>(std::clamp(e.params.num(cutout, 0.0), 0.0, 1.0)));
            shape_of(room, e).draw();
            caster.use();
        } else {
            caster.set("uModel", model);
            shape_of(room, e).draw();
        }
        gl::glEnable(gl::GL_CULL_FACE);
        return true;
    };
    const auto draw_casters = [&](const std::vector<const Caster*>& list) {
        std::size_t room_now = rooms.size();
        for (const Caster* c : list) {
            if (c->room != room_now) {
                if (room_now < rooms.size()) flush_batches(caster, false);
                room_now = c->room;
                set_frame(rooms[room_now].pose);
            }
            const Spatial3D& room = *rooms[room_now].room;
            const Element& e = *c->element;
            if (e.kind == kinds::mesh || e.kind == kinds::wall) {
                if (e.kind == kinds::mesh && cast_thin(room, e)) continue;
                if (q_.instancing) {
                    batch(shape_of(room, e), box_matrix(room, e).m, {}, 0, 0, 0, 0, 0);
                    continue;
                }
                caster.set("uModel", frame_matrix_ * box_matrix(room, e).m);
                shape_of(room, e).draw();
            } else if (e.kind == terrain_kind()) {
                auto t = terrains_.find(e.id);
                if (t == terrains_.end() || !t->second.mesh.valid()) continue;
                caster.set("uModel", frame_matrix_);
                t->second.mesh.draw();
            } else if (e.kind == kinds::portal) {
                caster.set("uModel", frame_matrix_ * portal_frame_model(room, e).m);
                cube_.draw();
            }
        }
        if (room_now < rooms.size()) flush_batches(caster, false);
    };
    const auto layers_from = std::chrono::steady_clock::now();
    std::vector<const Caster*> sees, movers_in, extras_in;
    bool unshadowed = lightless;
    // A map holds what stands still in the volume its light sees, and what
    // moves in it, drawn over. What stands still is laid once, and kept as it
    // was laid (ShadowSet::still) while nothing at rest in that volume has
    // changed or come to rest (the rooms' flips): a creature moving every
    // frame costs a copy and its own few casters, never the room's again.
    // A map is laid again only when what stands still has changed in the
    // volume - or its lamp, or its box, has - and then: the view of the eye's
    // own room lays every map that must, the frame it must: a shadow is where
    // its thing is, never a frame or two behind it - and, of several lamps,
    // never some shadows behind the others, out of step. The views one doorway
    // on lay a few a frame, taking turns from frame to frame; past that a map
    // stands as last drawn, with its own box, and the frame does not wait on
    // every lamp at once. (One never drawn is drawn now.) Only a map whose box
    // is fixed where it is may wait: one whose box goes with the eye (a sun's)
    // stood as it was covers where the eye was, not where it is, and is laid
    // every frame it changes. What moves is drawn the frame it moves.
    // The views seen through doorways share the frame's maps (shadow_budget_):
    // the ones that have waited longest go first, then the nearest the eye,
    // so that a view that has only just come into sight takes a few frames to
    // be lit by every lamp, never a frame to be lit by them all.
    std::array<std::size_t, kShadowMaps> order{};
    for (std::size_t n = 0; n < layers; ++n) order[n] = (n + frame_count_) % layers;
    if (layers > 1) {
        const auto far_of = [&](std::size_t k) {
            if (!in_use[k]) return 1e30f;
            const Light& l = lights[layer_light[k]];
            if (l.sun) return 0.0f;
            const gl::Vec3 d = l.pos - cam.eye;
            return gl::dot(d, d);
        };
        std::stable_sort(order.begin(), order.begin() + static_cast<std::ptrdiff_t>(layers), [&](std::size_t a, std::size_t b) {
            if (maps.waits[a] != maps.waits[b]) return maps.waits[a] > maps.waits[b];
            return far_of(a) < far_of(b);
        });
    }
    const int layer_count = static_cast<int>(std::max<std::size_t>(layers, 1));
    // The still layers are made when something first moves in a map, and
    // grown with the maps: grown, they are empty, and the maps that had
    // something laid in them lay it again when they next change.
    const auto keep_still = [&](std::size_t i) {
        if (fit_shadows(maps.still, shadow_px, layer_count))
            for (std::size_t j = 0; j < kShadowMaps; ++j)
                if (j != i) maps.base[j] = false, maps.still_at[j] = 0;
    };
    for (std::size_t n = 0; n < (lightless ? 0 : layers); ++n) {
        const std::size_t i = order[n];
        if (!in_use[i]) continue;  // (a layer kept for a light not lit now)
        const Light& li = lights[layer_light[i]];
        // Which light this layer holds now, as far as a depth map goes: a map
        // is that light's, and one that was another's (the lights came in
        // another order) is laid now, never left standing as some other
        // lamp's shadow. (Colour and power are not in a depth map: a sun's go
        // with the hour.)
        // (And which face of a lamp's cube: a face's map is never another's.)
        uint64_t ident = fnv(fnv(fnv(li.sun ? 1 : 2, li.gate), near_of[layer_light[i]] == static_cast<float>(i) ? 1 : 0), li.sun ? 0 : 1);
        if (layer_face[i] >= 0) ident = fnv(ident, static_cast<uint64_t>(layer_face[i]) + 7);
        const Frustum volume = frustum_of(light_vp[i]);
        // What moves in the volume, and the terrain and panels in it.
        movers_in.clear(), extras_in.clear();
        uint64_t movers_sig = 0, layout = layout_now;
        for (const RoomCasters* rc : entries) {
            if (!rc) continue;
            for (uint32_t k : rc->movers) {
                const Caster& c = rc->list[k];
                if (!shades(c, volume, li)) continue;
                movers_in.push_back(&c);
                movers_sig = fnv(fnv(movers_sig, reinterpret_cast<std::uintptr_t>(c.element)), c.where);
            }
            for (const Caster& c : rc->extras) {
                if (!shades(c, volume, li)) continue;
                extras_in.push_back(&c);
                layout = fnv(fnv(layout, reinterpret_cast<std::uintptr_t>(c.element)), c.where);
            }
        }
        // Whether what stands still, as laid, still holds: the same light, box
        // and rooms - and nothing at rest that this light sees has changed.
        bool at_rest = maps.still_at[i] != 0 && maps.ident[i] == ident && maps.layout[i] == layout &&
                       std::memcmp(maps.vp[i].m, light_vp[i].m, sizeof light_vp[i].m) == 0;
        for (std::size_t r = 0; at_rest && r < entries.size(); ++r) {
            const RoomCasters* rc = entries[r];
            if (!rc) continue;
            if (rc->trimmed > maps.still_at[i]) at_rest = false;
            for (auto f = rc->flips.rbegin(); at_rest && f != rc->flips.rend() && f->at > maps.still_at[i]; ++f) {
                Caster there;
                there.centre = f->centre, there.radius = f->radius, there.bounded = !f->all;
                if (f->all || shades(there, volume, li)) at_rest = false;
            }
        }
        if (at_rest && maps.sig[i] != 0 && maps.movers[i] == movers_sig) continue;
        // Only what moves in it has moved (a creature breathing): drawn again
        // as the frame's maps allow, the ones that waited longest first - a
        // shadow a frame or two behind, never every lamp's map every frame.
        if (at_rest && maps.sig[i] != 0) {
            int& budget = depth == 0 && !mirroring_ ? eye_shadow_budget_ : shadow_budget_;
            if (budget <= 0 && maps.waits[i] < kShadowWaits) {
                ++maps.waits[i];
                light_vp[i] = maps.vp[i];
                continue;
            }
            --budget;
            maps.waits[i] = 0;
        }
        if (!at_rest) {
            if (depth == 0 && !mirroring_) {
                // The eye's own room lays as many as a frame can carry
                // (eye_shadow_budget_); the rest stand as last drawn, with
                // their own boxes, a frame or two - a door swung, a lamp
                // switched, never the frame stopping for every lamp at once.
                const bool first = maps.sig[i] == 0;
                const bool may_wait = !first && maps.ident[i] == ident && maps.waits[i] < kShadowWaits;
                if (may_wait && eye_shadow_budget_ <= 0) {
                    ++maps.waits[i];
                    light_vp[i] = maps.vp[i];
                    continue;
                }
                --eye_shadow_budget_;
            } else if (depth >= 1) {
                // A map that is out of date waits its turn, standing as last
                // drawn, with its own box: the box of a sun that goes with
                // the eye is moved in whole texels, so what stands in its
                // overlap is the same - it may wait a few frames, not for ever.
                const bool first = maps.sig[i] == 0;  // never drawn: it is drawn now, whatever the frame has left
                const bool may_wait = !first && maps.ident[i] == ident && maps.waits[i] < kShadowWaits;
                if (!first && shadow_budget_ <= (may_wait ? 0 : -kNestedShadowMaps)) {
                    ++maps.waits[i];
                    light_vp[i] = maps.vp[i];  // as last drawn, with its own box
                    continue;
                }
                --shadow_budget_;
            }
            maps.waits[i] = 0;
        }
        ++times_.shadow_maps;
        if (std::getenv("SG_SHADOW_WHY")) {
            const bool vp_same = std::memcmp(maps.vp[i].m, light_vp[i].m, sizeof light_vp[i].m) == 0;
            std::fprintf(stderr, "map f%llu d%d set%p L%zu/%zu pos %.2f %.2f %.2f gated%d sun%d | rest%d still%llu ident%d layout%d vp%d sig%d movers%d n%zu\n",
                (unsigned long long)frame_count_, depth, (void*)&maps, i, layers, li.pos.x, li.pos.y, li.pos.z, li.gated ? 1 : 0, li.sun ? 1 : 0,
                at_rest ? 1 : 0, (unsigned long long)maps.still_at[i], maps.ident[i] == ident, maps.layout[i] == layout, vp_same, maps.sig[i] != 0, maps.movers[i] == movers_sig, movers_in.size());
        }
        if (!caster_ready) {
            caster.use();
            apply_uniforms(caster, post_, passes::shadow);
            caster.set("uInstanced", 0);
            caster_ready = true;
        }
        const int layer = static_cast<int>(i);
        // Where on the map what moves falls: that much is laid again from what
        // stands still, never the whole map.
        int rect[4] = {0, 0, 0, 0};
        for (const Caster* c : movers_in) {
            int r[4];
            depth_rect(light_vp[i], c->centre, c->radius, shadow_px, r);
            if (rect[0] >= rect[2]) std::copy(r, r + 4, rect);
            else rect[0] = std::min(rect[0], r[0]), rect[1] = std::min(rect[1], r[1]), rect[2] = std::max(rect[2], r[2]), rect[3] = std::max(rect[3], r[3]);
        }
        if (!at_rest) {
            maps.vp[i] = light_vp[i];
            maps.ident[i] = ident;
            maps.array.bind_layer(layer);
            gl::glClear(gl::GL_DEPTH_BUFFER_BIT);
        } else if (maps.base[i]) {
            copy_depth(maps.still, maps.array, layer, maps.mrect[i]);
        } else if (!movers_in.empty()) {
            // Laid with nothing moving in it, and something now does: what
            // stands in it is what stands still, kept.
            keep_still(i);
            copy_depth(maps.array, maps.still, layer);
            maps.base[i] = true;
            maps.array.bind_layer(layer);  // what moves is drawn on the map, never on what stands still
        } else {
            maps.array.bind_layer(layer);
        }
        caster.set("uLightViewProj", light_vp[i]);
        caster_vp = light_vp[i];
        // Light from beyond a doorway is kept out only by what stands on
        // this side of it - the room behind the opening's plane is the
        // other world's, and the light comes from there. (A hand's
        // breadth of slack keeps a leaf shut in the plane casting.)
        if (li.gated) {
            gl::glEnable(gl::GL_CLIP_DISTANCE0);
            caster.set("uCasterSide", li.gate_in.x, li.gate_in.y, li.gate_in.z, 0.08f - gl::dot(li.gate_in, li.gate_at));
            caster_side[0] = li.gate_in.x, caster_side[1] = li.gate_in.y, caster_side[2] = li.gate_in.z;
            caster_side[3] = 0.08f - gl::dot(li.gate_in, li.gate_at);
        } else {
            gl::glDisable(gl::GL_CLIP_DISTANCE0);
            caster.set("uCasterSide", 0.0f, 0.0f, 0.0f, 1.0f);
            caster_side[0] = caster_side[1] = caster_side[2] = 0.0f, caster_side[3] = 1.0f;
        }
        if (!at_rest) {
            sees.clear();
            for (const RoomCasters* rc : entries) {
                if (!rc) continue;
                for (const Caster& c : rc->list)
                    if (c.on && !c.mover && shades(c, volume, li)) sees.push_back(&c);
            }
            sees.insert(sees.end(), extras_in.begin(), extras_in.end());
            times_.shadow_casters += static_cast<int>(sees.size());
            draw_casters(sees);
            maps.still_at[i] = flip_clock_;
            maps.layout[i] = layout;
            maps.base[i] = false;
            // Something moves in it: what stands still is kept, to be laid under
            // it each frame it moves.
            if (!movers_in.empty()) {
                keep_still(i);
                copy_depth(maps.array, maps.still, layer);
                maps.base[i] = true;
                maps.array.bind_layer(layer);
            }
        }
        if (!movers_in.empty()) {
            times_.shadow_casters += static_cast<int>(movers_in.size());
            draw_casters(movers_in);
        }
        maps.movers[i] = movers_sig;
        std::copy(rect, rect + 4, maps.mrect[i]);
        maps.sig[i] = fnv(fnv(maps.layout[i], maps.still_at[i]), movers_sig) | 1;
    }
    times_.layers_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - layers_from).count();
    gl::glDisable(gl::GL_CLIP_DISTANCE0);
    gl::glCullFace(gl::GL_BACK);
    if (timing_) {
        gl::glFinish();
        times_.shadows += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - shadow_start).count();
    }

    lap(0, part_at);
    // --- the scene -------------------------------------------------------
    // The first room is the one this view is taken from; its look clears.
    const Mix first = mix(rooms.front().room->id(), look_of(*rooms.front().room));
    // The lights, as every pass that lights reads them (lights_glsl).
    // Each room drawn is lit by the lamps that reach it (more than one room
    // drawn: glued into one space), as many as a draw takes - its own and its
    // neighbours', strongest first; one room, by all of them.
    std::vector<std::vector<std::size_t>> lit_by;
    if (std::count_if(rooms.begin(), rooms.end(), [](const PlacedRoom& r) { return r.room && !r.image; }) > 1) {
        lit_by.resize(rooms.size());
        for (std::size_t r = 0; r < rooms.size(); ++r) {
            if (!rooms[r].room) continue;
            // Its own, and its neighbours' let in by its doorways (read as
            // seen through them: gated, its shadow where there are maps) -
            // never a neighbour's lamp through the wall between them.
            for (std::size_t i = 0; i < lights.size() && lit_by[r].size() < kMaxLights; ++i)
                if (lights[i].room == static_cast<int>(r) || lights[i].sun) lit_by[r].push_back(i);
        }
    }
    std::vector<std::size_t> air_lit;
    if (!lit_by.empty())
        for (std::size_t i = 0; i < lights.size() && air_lit.size() < kMaxLights; ++i)
            if (!lights[i].gated) air_lit.push_back(i);
    const auto light_uniforms = [&](const gl::Program& p, const std::vector<std::size_t>* only = nullptr) {
        if (!only && !lit_by.empty()) only = &lit_by.front();
        for (std::size_t i = 0; i < kShadowMaps; ++i) {
            p.set(shadow_uniform(i, 0), light_vp[i]);
            p.set(shadow_uniform(i, 1), bias[i]);
        }
        const std::size_t n = only ? only->size() : std::min(lights.size(), kMaxLights);
        p.set("uLightCount", static_cast<int>(n));
        p.set("uShadowCount", unshadowed ? 0 : static_cast<int>(only ? n : std::min(shadowed, n)));
        for (std::size_t k = 0; k < n; ++k) {
            const std::size_t i = only ? (*only)[k] : k;
            lamp_to(p, k, lights[i]);
            p.set(light_uniform(k, 12), unshadowed ? -1.0f : near_of[i]);
            p.set(light_uniform(k, 17), unshadowed || i >= shadowed ? -1.0f : first_layer[i]);
            p.set(light_uniform(k, 18), cube_of[i]);
        }
        p.set("uShadowMaps", 1);
    };
    // The air of this view, lit by its lamps (air_fs) - when its look says
    // its air scatters: in the eye's own view and in every view through a
    // doorway, however deep (each gathered at its own pixels, so a small one
    // costs little, and kept while nothing it is made from moves) - a room's
    // haze is what is seen of it from any way. (A mirror's picture has none.)
    const Air* air = nullptr;
    const float scatter = baking_ || mirroring_ ? 0.0f : static_cast<float>(setting(first, passes::scene, "scatter", 0.0));
    if (scatter > 0.0f) {
        Air& a = air_for(rooms.front().room);
        a.near = 0.3f;
        // As far out as the look says (`scatter.far`), within what is drawn.
        // As far out as the look says (`scatter.far`); unless it says, as far
        // as the air goes: across a room with walls (its box's diagonal, a
        // little more), not past them, and up to 90 m under a sky.
        const PlacedRoom& here = rooms.front();
        float reach = std::min(zfar, 90.0f);
        if (here.room->params().num(Key{"sky"}, 0.0) < 0.5 && here.room->params().has(Key{"room_w"})) {
            const double w = here.room->params().num(Key{"room_w"}), d = here.room->params().num(Key{"room_d"}),
                         h = here.room->params().num(Key{"room_h"}, 3.0);
            reach = std::min(reach, std::ceil(static_cast<float>(1.1 * std::sqrt(w * w + d * d + h * h))));
        }
        a.far = std::clamp(static_cast<float>(setting(first, passes::scene, "scatter.far", reach)), 4.0f, std::max(zfar, 4.0f));
        // A cell is kAirTile pixels of a large view; a small one (a world drawn
        // at a few hundred pixels, as a game of its day would) keeps about a
        // hundred and twenty cells across, so a beam of light in its air is
        // a beam and not a row of blocks.
        // A look may ask for finer cells, or coarser (`scatter.cell`, pixels).
        const int fit = std::clamp(static_cast<int>(vp_w_) / 120, 4, kAirTile);
        const int tile = std::clamp(static_cast<int>(setting(first, passes::scene, "scatter.cell", fit)), 2, 64);
        const int gx = std::max(1, (static_cast<int>(vp_w_) + tile - 1) / tile);
        const int gy = std::max(1, (static_cast<int>(vp_h_) + tile - 1) / tile);
        const float ahead = static_cast<float>(setting(first, passes::scene, "scatter.ahead", 0.5));
        // How many points of each cell are lit, through its depth (`scatter.steps`).
        const int steps = std::clamp(static_cast<int>(setting(first, passes::scene, "scatter.steps", 1.0)), 1, 16);
        const float density = static_cast<float>(setting(first, passes::scene, "uFogDensity", 0.0));
        const float start = static_cast<float>(setting(first, passes::scene, "uFogStart", 0.0));
        // Only the air beyond the doorway it is seen through is this view's:
        // this side of it is the other world's, lit by that one's lamps.
        const HalfSpace clip = clips.empty() ? HalfSpace{{0, 0, 0}, 1.0} : clips.front();
        // What it is gathered from: the view, the lights and their maps, the air.
        uint64_t of = 1469598103934665603ULL;
        for (float f : view_proj.m) of = mix_bits(of, f);
        for (const Light& l : lights)
            for (float f : {l.pos.x, l.pos.y, l.pos.z, l.dir.x, l.dir.y, l.dir.z, l.color.x, l.color.y, l.color.z, l.power, l.inner, l.outer,
                            l.falloff, l.scatter, l.frame_w, l.frame_h, l.frame_soft, l.gate_at.x, l.gate_at.y, l.gate_at.z, l.gate_w, l.gate_h, l.open,
                            l.sun ? 1.0f : 0.0f, l.indirect ? 1.0f : 0.0f, l.gated ? (l.hung_only ? 2.0f : 1.0f) : 0.0f})
                of = mix_bits(of, f);
        for (std::size_t i = 0; i < layers; ++i) {
            // What stands still in each map, not what moves over it: a thing
            // moving through a lamp's cone throws no shadow in the air anyone
            // could see, and the air gathered again in every view, every frame
            // something moves, is what it would cost.
            of = fnv(fnv(of, maps.layout[i]), maps.still_at[i] != 0 ? maps.still_at[i] : maps.sig[i]);
            for (float f : light_vp[i].m) of = mix_bits(of, f);
        }
        for (float f : {scatter, ahead, density, start, static_cast<float>(clip.normal.x), static_cast<float>(clip.normal.y),
                        static_cast<float>(clip.normal.z), static_cast<float>(clip.offset), a.near, a.far,
                        static_cast<float>(gx), static_cast<float>(gy), static_cast<float>(steps), unshadowed ? 1.0f : 0.0f, static_cast<float>(lights.size())})
            of = mix_bits(of, f);
        if (std::getenv("SG_AIR_WHY")) {
            static std::map<const void*, std::array<uint64_t, 4>> was;
            std::array<uint64_t, 4> h{1, 1, 1, 1};
            for (float f : view_proj.m) h[0] = mix_bits(h[0], f);
            for (const Light& l : lights)
                for (float f : {l.pos.x, l.pos.y, l.pos.z, l.dir.x, l.dir.y, l.dir.z, l.color.x, l.color.y, l.color.z, l.power, l.open}) h[1] = mix_bits(h[1], f);
            for (std::size_t i = 0; i < layers; ++i) {
                h[2] = fnv(fnv(h[2], maps.layout[i]), maps.still_at[i] != 0 ? maps.still_at[i] : maps.sig[i]);
                for (float f : light_vp[i].m) h[2] = mix_bits(h[2], f);
            }
            auto& w = was[&a];
            std::string power;
            char buf[48];
            for (const Light& l : lights) { std::snprintf(buf, sizeof buf, " %.2f/%.2f", l.power, l.open); power += buf; }
            std::fprintf(stderr, "air f%llu d%d %s view%d lights%d maps%d |%s\n", (unsigned long long)frame_count_, depth, rooms.front().room->id().str().c_str(), w[0] != h[0], w[1] != h[1], w[2] != h[2], w[1] != h[1] ? power.c_str() : "");
            w = h;
        }
        // (Each slice's own light laid eight to a row, a pixel a cell.)
        constexpr int group = gl::LayerArray::kGroup;
        bool made = a.light.ensure(gx, gy, kAirSlices, /*volume=*/true);
        if (!a.local.valid() || a.local.width() != gx * group || a.local.height() != gy * (kAirSlices / group)) {
            a.local.create(gx * group, gy * (kAirSlices / group), gl::GL_RGBA16F, 0, false);
            made = true;
        }
        // Gathered again when what it is made from moves - and, while nothing
        // does, again and again, each time at other points of each cell,
        // averaged, until there are enough to stand for the whole of each
        // cell: then it rests. (One gathering alone, each cell sampled at one
        // point, is noise that stands still on the screen near a lamp.)
        if (made || a.of != of) a.gathered = 0;
        if (a.gathered < kAirGatherings) {
            a.of = of;
            if (!air_prog_) {
                air_prog_ = std::make_unique<gl::Program>(gl::post_vs(), gl::air_fs(), "air");
                air_sum_prog_ = std::make_unique<gl::Program>(gl::post_vs(), gl::air_sum_fs(), "air sum");
            }
            const gl::Program& p = *air_prog_;
            gl::glDisable(gl::GL_DEPTH_TEST);
            gl::glDisable(gl::GL_CULL_FACE);
            gl::glDisable(gl::GL_BLEND);
            gl::glDisable(gl::GL_SCISSOR_TEST);
            p.use();
            // (A space glued into one has one air: lit by the lamps of every
            // room of it - gathered along each ray only as far as what it
            // meets, so no lamp glows through a wall - not the eye's room's
            // alone, which would light the air beyond a doorway only once
            // the eye is through it.)
            if (!air_lit.empty()) light_uniforms(p, &air_lit);
            else light_uniforms(p);
            maps.array.bind_depth(1);
            p.set("uAir", a.near, a.far, static_cast<float>(kAirSlices), 1.0f);
            p.set("uAirUnproject", view_proj.inverse());
            p.set("uViewPos", cam.eye);
            p.set("uFogDensity", density);
            p.set("uFogStart", start);
            p.set("uScatter", scatter);
            p.set("uScatterAhead", ahead);
            p.set("uAirClip", static_cast<float>(clip.normal.x), static_cast<float>(clip.normal.y), static_cast<float>(clip.normal.z),
                  static_cast<float>(clip.offset));
            // Every slice's own light in one pass; then the slices added up
            // from the eye, eight at a time, no pass waiting on another.
            p.set("uAirCells", static_cast<float>(gx), static_cast<float>(gy));
            p.set("uAirSpin", static_cast<float>(a.spin++ % 4096u));
            p.set("uAirSteps", steps);
            a.local.bind();
            if (a.gathered > 0) {
                // Into the average: this gathering one part in as many as there are now.
                gl::glEnable(gl::GL_BLEND);
                gl::glBlendColor(0.0f, 0.0f, 0.0f, 1.0f / static_cast<float>(a.gathered + 1));
                gl::glBlendFunc(gl::GL_CONSTANT_ALPHA, gl::GL_ONE_MINUS_CONSTANT_ALPHA);
            }
            screen_.draw();
            gl::glDisable(gl::GL_BLEND);
            ++a.gathered;
            const gl::Program& sum = *air_sum_prog_;
            sum.use();
            sum.set("uAirLocal", 5);
            sum.set("uAirCells", static_cast<float>(gx), static_cast<float>(gy));
            a.local.bind_color(5);
            for (int g = 0; g < kAirSlices / group; ++g) {
                a.light.bind_group(g);
                sum.set("uAirGroup", g * group);
                screen_.draw();
            }
            ++times_.air_built;
        }
        a.light.bind_color(5);
        gl::glActiveTexture(gl::GL_TEXTURE0);
        air = &a;
    }
    target.bind();
    bool cut = false;
    if (part) {
        gl::glViewport(0, 0, static_cast<int>(vp_w_), static_cast<int>(vp_h_));
        gl::glScissor(0, 0, static_cast<int>(vp_w_), static_cast<int>(vp_h_));
        gl::glEnable(gl::GL_SCISSOR_TEST);
    } else {
        cut = scissor_to(cut_, target.width(), target.height());
    }
    gl::glClearColor(static_cast<float>(setting(first, passes::scene, "clear.x", 0.012)),
                     static_cast<float>(setting(first, passes::scene, "clear.y", 0.014)),
                     static_cast<float>(setting(first, passes::scene, "clear.z", 0.022)),
                     1.0f);
    // (Asked what surfaces are, where there are none is nothing: 0.)
    if (surface_only_) gl::glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    // Its depth reversed, where it may be: taken 0 to 1, cleared to the far
    // (0), the nearer the greater - and put back as GL keeps it once the scene
    // is drawn, for shadow maps and all else.
    struct Reversed {
        bool on;
        ~Reversed() {
            if (!on) return;
            gl::glClipControl(gl::GL_LOWER_LEFT, gl::GL_NEGATIVE_ONE_TO_ONE);
            gl::glClearDepth(1.0);
            gl::glDepthFunc(gl::GL_LESS);
        }
    } reversed_scene{reversed};
    if (reversed) {
        gl::glClipControl(gl::GL_LOWER_LEFT, gl::GL_ZERO_TO_ONE);
        gl::glClearDepth(0.0);
    }
    gl::glClear(gl::GL_COLOR_BUFFER_BIT | gl::GL_DEPTH_BUFFER_BIT);
    gl::glEnable(gl::GL_DEPTH_TEST);
    gl::glDepthFunc(reversed ? gl::GL_GREATER : gl::GL_LESS);
    gl::glEnable(gl::GL_MULTISAMPLE);
    gl::glDisable(gl::GL_CULL_FACE);
    for (int i = 0; i < kMaxBounds; ++i)
        gl::glEnable(gl::GL_CLIP_DISTANCE0 + static_cast<gl::GLenum>(i));
    maps.array.bind_depth(1);

    // Everything a scene shader is fed that is not the look's. Set again
    // whenever a room's look brings a different program.
    const auto frame_uniforms = [&](const gl::Program& p) {
        p.set("uViewProj", drawn_proj);
        p.set("uDepthNudge", reversed ? 1.0f : 0.0f);
        p.set("uMirrorScreen", vp_w_, vp_h_);
        mirror_uniforms(p, !mirroring_);
        p.set("uInstanced", 0);
        p.set("uDim", 0.0f);
        p.set("uSurfaceOnly", surface_only_);
        light_uniforms(p);
        gl::Vec3 sun_dir{0, 1, 0}, sun_color{0, 0, 0};
        for (std::size_t i = 0; i < lights.size(); ++i) {
            // The sky's sun is this room's own, not one seen through a door.
            if (lights[i].sun && !lights[i].gated && sun_color.x == 0.0f && sun_color.y == 0.0f && sun_color.z == 0.0f) {
                sun_dir = gl::normalize(lights[i].dir) * -1.0f;
                sun_color = lights[i].color;
            }
        }
        p.set("uSunDir", sun_dir);
        p.set("uSunColor", sun_color);
        p.set("uViewPos", cam.eye);
        p.set("uTime", static_cast<float>(world_time_));
        p.set("uShadowTexel", 1.0f / static_cast<float>(maps.array.size()),
              1.0f / static_cast<float>(maps.array.size()));
        p.set("uShadowMaps", 1);
        p.set("uStraddle", 0.0f);
        p.set("uScreenRect", 0.0f, 0.0f, 1.0f, 1.0f);
        p.set("uTex", 0);
        p.set("uSurfaceMap", 9);  // a worn texture's occlusion, roughness, metal (bind_surface_map)
        p.set("uSurfaceMapOn", 0.0f);
        p.set("uNormalMap", 10);  // and its normal map (bind_normal_map)
        p.set("uNormalMapOn", 0.0f);
        p.set("uEnv", 7);
        p.set("uEnvMix", 0.0f);
        p.set("uCRT", 0.0f);
        p.set("uScreenUV", 0.0f);
        p.set("uViewport", vp_w_, vp_h_);
        p.set("uUVRect", 0.0f, 0.0f, 0.0f, 0.0f);
        p.set("uAirLight", 5);
        if (air) p.set("uAir", air->near, air->far, static_cast<float>(kAirSlices), 1.0f);
        else p.set("uAir", 0.0f, 0.0f, 0.0f, 0.0f);
        if (host_air_.on && !clips.empty()) {
            const HalfSpace& door = clips.front();
            p.set("uHostFog", host_air_.density, host_air_.start, host_air_.full, 1.0f);
            p.set("uHostFogColor", host_air_.color);
            p.set("uDoorPlane", static_cast<float>(door.normal.x), static_cast<float>(door.normal.y), static_cast<float>(door.normal.z),
                  static_cast<float>(door.offset));
        } else {
            p.set("uHostFog", 0.0f, 0.0f, 0.0f, 0.0f);
        }
    };

    // A room's own settings on the program drawing it (in use as scene_):
    // its look, time, where its copy is lit, its sides of its doorways.
    const auto prepare = [&](const Mix& look, const gl::Vec3& shift, const PlacedRoom& placed, const Spatial3D& room) {
        apply_uniforms(*scene_, look, passes::scene);
        apply_attended(*scene_, passes::scene);
        scene_->set("uLatticeShift", shift);
        scene_->set("uTime",static_cast<float>(semantic_time(graph_?graph_:(root_?root_->graph_:nullptr),room)));
        // This room's side of each doorway, from the portals as they are now,
        // and whatever the caller cuts away besides.
        int n = 0;
        for (const HalfSpace& h : clips) {
            if (n >= kMaxBounds) break;
            scene_->set(clip_uniform(n++), static_cast<float>(h.normal.x),
                        static_cast<float>(h.normal.y), static_cast<float>(h.normal.z),
                        static_cast<float>(h.offset));
        }
        for (Key d : placed.doorways) {
            const Element* portal = room.find(d);
            if (!portal || n >= kMaxBounds) continue;
            const HalfSpace h = room_side(room, *portal, placed.pose);
            scene_->set(clip_uniform(n++), static_cast<float>(h.normal.x),
                        static_cast<float>(h.normal.y), static_cast<float>(h.normal.z),
                        static_cast<float>(h.offset));
        }
        scene_->set("uClipCount", n);
        clip_count_ = n;
        doors_to_program(placed);
        probes_to_program(placed);
        if (!lit_by.empty()) {
            const std::size_t r = static_cast<std::size_t>(&placed - rooms.data());
            if (r < lit_by.size()) light_uniforms(*scene_, &lit_by[r]);
        }
        if (baking_) {
            // A probe's bake: only its lamp's light, nothing of its own.
            scene_->set("uAmbient", 0.0f);
            scene_->set("uDark", 1.0f);
            scene_->set("uFogDensity", 0.0f);
            scene_->set("uDoorCount", 0);
        }
        return n;
    };
    std::vector<const Element*> sprites;
    // Glass (`glass`, how clear): drawn after everything else of its room,
    // furthest first, over what is behind it.
    std::vector<const Element*> glass;

    lap(1, part_at);
    scene_ = nullptr;
    const Frustum view = frustum_of(view_proj);
    const PlacedRoom* last = nullptr;
    for (const PlacedRoom& placed : rooms) {
        if (!placed.room) continue;
        // A copy of a space that wraps, out of sight: its cell, not in view.
        if (placed.image) {
            const Vec3d cell = period_of(*placed.room);
            const double r = 0.5 * std::sqrt(cell.x * cell.x + cell.y * cell.y + cell.z * cell.z);
            const Vec3d c = placed.pose.position;
            if (!view.intersects_sphere({static_cast<float>(c.x), static_cast<float>(c.y), static_cast<float>(c.z)}, static_cast<float>(r))) continue;
        }
        set_frame(placed.pose);
        const Spatial3D& room = *placed.room;
        // A copy of a space that wraps is lit where its original is.
        gl::Vec3 shift{0, 0, 0};
        if (placed.image)
            for (const PlacedRoom& own : rooms)
                if (own.room == placed.room && !own.image) shift = to_vec3(own.pose.position) - to_vec3(placed.pose.position);
        // A copy straight after its own room is that room again: the same
        // look, the same program and everything set on it, only moved.
        const bool again = placed.image && last && last->room == placed.room && placed.doorways.empty();
        last = &placed;
        int bounds = clip_count_;
        if (again) {
            scene_->set("uLatticeShift", shift);
        } else {
        // Each room in its own look: the annex seen through the doorway
        // keeps its own fog, whichever side you stand on.
        const Mix look = mix(room.id(), look_of(room));
        const gl::Program* program = program_for(look.shown(), passes::scene);
        if (program != scene_) {
            scene_ = program;
            scene_->use();
            frame_uniforms(*scene_);
        }
        bounds = prepare(look, shift, placed, room);
        }

        lap(2, part_at);
        if (room.params().num(Key{"sky"}, 0.0) > 0.5 && !baking_) {
            // The sky is at no distance a plane can cut: it is the room's
            // ceiling, whatever bounds its ground.
            scene_->set("uClipCount", 0);
            draw_sky(cam, zfar);
            scene_->set("uClipCount", bounds);
        }
        const gl::Vec3 moved = shift * -1.0f;
        static const Key unseen_key{"unseen"}, glass_key{"glass"}, surface_key{"surface"};
        // What fills an opening onto nothing (`<opening>.blank`) where the
        // opening is a glued doorway: there is something beyond it now.
        std::vector<std::pair<Vec3d, double>> openings;
        for (Key d : placed.doorways)
            if (const Element* pe = room.find(d)) openings.push_back({world_position(room, *pe), 0.5 * pe->params.num(keys::w, 1.0) + 0.3});
        const auto fills_opening = [&](const Element& e) {
            if (openings.empty()) return false;
            const std::string& n = e.id.str();
            if (n.size() < 6 || n.compare(n.size() - 6, 6, ".blank") != 0) return false;
            const Vec3d at = world_position(room, e) + Vec3d{0, e.params.num(keys::sy, 0.0) * 0.5, 0};
            for (const auto& [o, reach] : openings)
                if (std::hypot(at.x - o.x, at.z - o.z) < reach) return true;
            return false;
        };
        for (const auto i : plan_draws(room, view, placed.image ? &moved : nullptr)) {
            const auto& e=room.elements()[i];
            if (e.kind == kinds::wall && fills_opening(e)) continue;
            // `unseen`: no eye sees it - it still casts its shadow (a walker's
            // own body, seen from inside it).
            if (e.params.num(unseen_key, 0.0) > 0.5) continue;
            if (hidden_ && hidden_->count(e.id.key())) continue;  // (moving: not what the probes see)
            if (e.kind == terrain_kind()) {
                draw_terrain(room, e);
            } else if (e.kind == kinds::mesh) {
                if (is_sprite(e) || cuts_out(e)) sprites.push_back(&e);
                else if (e.params.num(glass_key, 0.0) > 0.0) glass.push_back(&e);
                else if (instanceable(e)) batch_crate(room, e);
                else if (!batch_skinned(room, e)) draw_crate(room, e);
            } else if (e.kind == kinds::wall) {
                if (q_.instancing && e.id != highlight_)
                    batch(cube_, box_matrix(room, e).m, color_of(e, {0.52f, 0.50f, 0.48f}), 0.9f,
                          static_cast<float>(e.params.num(surface_key, 2.0)), 0, 0, 0);
                else
                    draw_wall_element(room, e);
            } else if (e.kind == kinds::light) {
                draw_lamp(room, e);
            }
        }
        lap(3, part_at);
        flush_batches(*scene_, true);
        draw_straddlers(placed, room);
        // Pictures cut out of their cards - and things cut out of their
        // pictures (`cutout`: leaves) - together, with the program that may
        // cut (cutout_of), set up for this room as the other is.
        if (!sprites.empty()) {
            const gl::Program* solid = scene_;
            const gl::Program* cut = cutout_of(*solid);
            if (cut) {
                scene_ = cut;
                cut->use();
                frame_uniforms(*cut);
                prepare(mix(room.id(), look_of(room)), shift, placed, room);
            }
            for (const Element* e : sprites) is_sprite(*e) ? draw_sprite(room, *e) : draw_crate(room, *e);
            sprites.clear();
            if (cut) {
                scene_ = solid;
                solid->use();
            }
        }
        lap(4, part_at);
        // Doorways' frames drawn together (what each shows, one by one).
        batch_frames_ = q_.instancing;
        for (const auto i : draw_plans_.at(&room).portals) {
            const auto& e=room.elements()[i];
            // (Glued: its frame, but no view - what is beyond it is drawn where it stands.)
            const bool opening = std::find(placed.doorways.begin(), placed.doorways.end(), e.id.key()) != placed.doorways.end();
            // The doorway being looked through keeps its frame; only its
            // view is left out - seen from its own far side it would fill
            // the whole picture.
            draw_portal(room, e, depth, target, placed.image || opening || (!skip_portal.empty() && e.id == skip_portal));
        }
        if (batch_frames_) flush_batches(*scene_, true);
        batch_frames_ = false;
        // The room's own floor, walls and ceiling after all that stands in
        // it and all that hangs on them (its pictures, its doorways' frames
        // and views): they are behind everything, and where something hides
        // them they are refused by depth, not shaded.
        // (Glued into one space, a room that does not say how big it is is
        // bounded by its own walls: a box of the default size round it would
        // stand in the rooms beside it.)
        if (room.params().num(Key{"sky"}, 0.0) <= 0.5 && !(placed.glued && !room.params().has(Key{"room_w"}))) draw_room(room);
        // (Asked what surfaces are, glass is none: it is seen through.)
        if (surface_only_) glass.clear();
        if (!glass.empty()) {
            const Vec3d eye{cam.eye.x, cam.eye.y, cam.eye.z};
            const auto far = [&](const Element* e) { return distance(world_position(room, *e), eye); };
            std::sort(glass.begin(), glass.end(), [&](const Element* a, const Element* b) { return far(a) > far(b); });
            gl::glEnable(gl::GL_BLEND);
            gl::glBlendFuncSeparate(gl::GL_SRC_ALPHA, gl::GL_ONE_MINUS_SRC_ALPHA, gl::GL_ZERO, gl::GL_ONE);
            gl::glDepthMask(0);
            for (const Element* e : glass) {
                scene_->set("uGlass", static_cast<float>(std::clamp(e->params.num(Key{"glass"}, 0.0), 0.0, 1.0)));
                draw_crate(room, *e);
            }
            scene_->set("uGlass", 0.0f);
            gl::glDepthMask(1);
            gl::glDisable(gl::GL_BLEND);
            glass.clear();
        }
        lap(5, part_at);
    }
    for (int i = 0; i < kMaxBounds; ++i)
        gl::glDisable(gl::GL_CLIP_DISTANCE0 + static_cast<gl::GLenum>(i));
    if (part || cut) gl::glDisable(gl::GL_SCISSOR_TEST);
    set_frame(Pose{});
}

}  // namespace sg::render
