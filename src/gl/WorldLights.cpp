// The GL view's lights: a room's own lamps, what comes in through its
// doorways, and how much of a doorway what stands in it covers.
#include "sg/gl/World.hpp"

#include "WorldHash.hpp"

namespace sg::render {

void GLWorldView::aim_rays(const Spatial3D& first) {
    rays_on_ = rays_room_ == &first;
    if (!rays_on_) return;
    const Element& cam = first.camera();
    const Vec3d to = rays_at_ - position_of(cam);
    const Vec3d f = forward_of(cam);
    const double half = cam.params.num(keys::fov, 70.0) * 3.14159265358979323846 / 360.0;
    rays_dir_ = to;
    rays_fwd_ = f;
    rays_tan_ = std::tan(half);
}

void GLWorldView::lamp_to(const gl::Program& p, std::size_t i, const Light& l) {
    p.set(light_uniform(i, 0), l.pos);
    p.set(light_uniform(i, 1), l.dir);
    p.set(light_uniform(i, 2), l.color);
    p.set(light_uniform(i, 3), l.power);
    p.set(light_uniform(i, 4), std::cos(l.inner));
    p.set(light_uniform(i, 5), std::cos(l.outer));
    p.set(light_uniform(i, 6), l.sun ? 1.0f : 0.0f);
    p.set(light_uniform(i, 7), l.floor);
    // (1: lit as bounce and standing in for it; 0.6: lit as bounce only.)
    p.set(light_uniform(i, 8), l.indirect ? (l.bounce ? 1.0f : 0.6f) : 0.0f);
    p.set(light_uniform(i, 9), l.falloff);
    p.set(light_uniform(i, 10), l.gate_at.x, l.gate_at.y, l.gate_at.z, l.gate_w);
    p.set(light_uniform(i, 11), l.gate_across.x, l.gate_across.z, l.gate_h, l.gated ? (l.hung_only ? 2.0f : 1.0f) : 0.0f);
    p.set(light_uniform(i, 13), l.open);
    p.set(light_uniform(i, 14), l.scatter);
    p.set(light_uniform(i, 15), l.frame_w, l.frame_h, l.frame_soft, l.frame_w > 0.0f && l.frame_h > 0.0f ? 1.0f : 0.0f);
    p.set(light_uniform(i, 16), l.range);
}

namespace {
bool same_pose(const Pose& a, const Pose& b) {
    return a.position.x == b.position.x && a.position.y == b.position.y && a.position.z == b.position.z && a.yaw == b.yaw &&
           a.pitch == b.pitch && a.roll == b.roll;
}
}  // namespace

auto GLWorldView::own_lights(const Spatial3D& room, const Pose& pose) const -> std::vector<Light> {
    std::vector<Light> out;
    for (const Element* ep : index_of(room).lights) {
        const Element& e = *ep;
        if (!e.alive) continue;
        // (Read again only when the lamp, what it hangs from, or where its
        // room stands has changed.)
        Placed& placed = placed_of(room, e);
        if (!placed.lit || !same_pose(placed.lit_at, pose)) placed.light = light_of(room, e, pose), placed.lit_at = pose, placed.lit = true;
        Light l = placed.light;
        if (baking_) {
            // Baking a probe: the one lamp, at 1, in white - whatever it is now.
            if (e.id != solo_ || l.sun || l.indirect) continue;
            l.color = {1.0f, 1.0f, 1.0f};
            l.power = 26.0f;
            out.push_back(l);
            continue;
        }
        if (l.power <= 0.0f) continue;  // switched off
        out.push_back(l);
    }
    return out;
}

auto GLWorldView::through_doorways(const PlacedRoom& placed) -> std::vector<Light> {
    std::vector<Light> out;
    if (!placed.room || baking_) return out;
    const Spatial3D& room = *placed.room;
    for (const Element* ep : index_of(room).portals) {
        const Element& e = *ep;
        // (`light`: how much of what is beyond it an opening lets in, 0 to 1 -
        // a curtain half drawn, a window's day turned down; 0, none.)
        if (!e.alive) continue;
        static const Key light_key{"light"};
        const float pass = static_cast<float>(std::clamp(e.params.num(light_key, 1.0), 0.0, 1.0));
        if (is_screen(e) || pass <= 0.0f) continue;
        const auto it = worlds_.find(e.id);
        if (it == worlds_.end() || !it->second.world || !it->second.carry || !declared_world(room,e,*it->second.world)) continue;
        const Spatial3D& far = *it->second.world;
        const Pose door = pose_of(room, e);
        const float half_w = static_cast<float>(e.params.num(keys::w, 3.0)) * 0.5f;
        const float half_h = static_cast<float>(e.params.num(keys::h, 2.0)) * 0.5f;
        if (half_w <= 0.0f || half_h <= 0.0f) continue;
        Element probe = room.camera(), there = far.camera();
        probe.params.set(keys::x, door.position.x).set(keys::y, door.position.y).set(keys::z, door.position.z)
            .set(keys::yaw, 0.0).set(keys::pitch, 0.0);
        it->second.carry(probe, there);
        const double turn = there.params.num(keys::yaw);
        const Vec3d from = position_of(there);
        // A point of the far world, where this room has it.
        const auto here = [&](const gl::Vec3& q) {
            const Vec3d local = rotate_xz({q.x - from.x, q.y - from.y, q.z - from.z}, -turn);
            const Pose p{{door.position.x + local.x, door.position.y + local.y, door.position.z + local.z}, 0.0};
            return to_vec3(compose_pose(placed.pose, p).position);
        };
        const auto turned = [&](const gl::Vec3& d) {
            return to_vec3(rotate_xz({d.x, d.y, d.z}, placed.pose.yaw - turn));
        };
        const gl::Vec3 at = to_vec3(compose_pose(placed.pose, door).position);
        const gl::Vec3 across_door = to_vec3(across(door.yaw + placed.pose.yaw));
        const gl::Vec3 into = to_vec3(heading(door.yaw + placed.pose.yaw));  // a portal faces into its own room
        // A door shut in the opening lets nothing through. Ajar, what
        // comes through throws the leaf's shadow (its own shadow map);
        // `open` is only for a light past the maps there are.
        bool shut = false;
        const float open = 1.0f - covered(room, e, door, half_w, half_h, shut);
        const auto gate = [&](Light& l) {
            l.gated = true;
            l.gate = std::hash<Key>{}(e.id);
            l.gate_at = at, l.gate_across = across_door, l.gate_in = into, l.gate_w = half_w, l.gate_h = half_h;
            // In the shadow of what stands in the way, none of it gets through;
            // of the rest, as much as the opening lets in.
            l.open = open * pass, l.floor = 0.0f;
            // Shut, nothing gets through - but onto the leaf itself, which is
            // half in that room.
            if (shut) l.hung_only = true;
        };
        // Its lamps, the strongest few. A bounce standing in for light from
        // all round belongs to its own room, and stays there - but for the
        // door's leaf, half in it.
        // Each where this room has it, let in by the opening - and only
        // those whose light reaches the opening at all (light_meets_gate):
        // one that ends before it lets nothing through, and takes no place
        // from one that does.
        std::vector<Light> lamps = own_lights(far, Pose{});
        for (Light& l : lamps) {
            l.pos = here(l.pos);
            l.dir = gl::normalize(turned(l.dir));
            gate(l);
        }
        lamps.erase(std::remove_if(lamps.begin(), lamps.end(), [](const Light& l) { return !light_meets_gate(l); }),
                    lamps.end());
        std::sort(lamps.begin(), lamps.end(), [](const Light& a, const Light& b) {
            if (a.indirect != b.indirect) return b.indirect;
            if (a.sun != b.sun) return a.sun;
            return a.power > b.power;
        });
        std::size_t real = 0;
        for (Light& l : lamps)
            if (l.indirect) l.hung_only = true;
            else ++real;
        if (real > 6) lamps.erase(lamps.begin() + 6, lamps.begin() + static_cast<std::ptrdiff_t>(real));
        for (Light l : lamps) {
            if (l.sun) {
                // Its shadow lies in this room, near the opening.
                constexpr float kReach = 8.0f;
                l.extent = std::min(l.extent, kReach);
                l.pinned = true;
                l.focus = at + into * (kReach * 0.4f);
            }
            out.push_back(l);
        }
        // Its sky, or whatever light fills it from all round, seen
        // through the opening: a soft lamp just beyond it, as wide as it.
        const LookState& look = look_of(far);
        const double amb = value(look, passes::scene, Key{"uAmbient"}, 0.55);
        const gl::Vec3 sky{static_cast<float>(value(look, passes::scene, Key{"uSky.x"}, 0.10) * amb),
                           static_cast<float>(value(look, passes::scene, Key{"uSky.y"}, 0.13) * amb),
                           static_cast<float>(value(look, passes::scene, Key{"uSky.z"}, 0.20) * amb)};
        const float bright = std::max({sky.x, sky.y, sky.z});
        if (bright > 1e-4f) {
            // A portal faces into its own room; beyond is behind it.
            const gl::Vec3 beyond = to_vec3(heading(door.yaw + placed.pose.yaw)) * -1.0f;
            Light glow;
            glow.pos = at + beyond * 0.35f;
            glow.dir = beyond * -1.0f;
            glow.color = sky * (1.0f / bright);
            glow.power = bright * half_w * half_h * 24.0f;
            glow.inner = 0.9f;
            glow.outer = 1.55f;
            glow.falloff = 1.0f;
            glow.indirect = true;
            gate(glow);
            out.push_back(glow);
        }
    }
    // (What comes in before what only falls on a shut door's leaf: a window's
    // sky is not left out for a closed door's lamps.)
    std::stable_partition(out.begin(), out.end(), [](const Light& l) { return !l.hung_only; });
    if (out.size() > 8) out.resize(8);
    return out;
}

void GLWorldView::doors_to_program(const PlacedRoom& placed) {
    static const auto name = [](const char* base, int i) {
        static std::array<std::array<std::string, 4>, 5> names = [] {
            std::array<std::array<std::string, 4>, 5> n;
            const char* bases[] = {"uDoorAt", "uDoorAxis", "uDoorIn", "uDoorSky", "uDoorGround"};
            for (int b = 0; b < 5; ++b)
                for (int k = 0; k < 4; ++k) n[static_cast<std::size_t>(b)][static_cast<std::size_t>(k)] = std::string(bases[b]) + "[" + std::to_string(k) + "]";
            return n;
        }();
        const std::string s = base;
        const int b = s == "uDoorAt" ? 0 : s == "uDoorAxis" ? 1 : s == "uDoorIn" ? 2 : s == "uDoorSky" ? 3 : 4;
        return names[static_cast<std::size_t>(b)][static_cast<std::size_t>(i)].c_str();
    };
    int count = 0;
    if (placed.room)
        for (const auto& e : placed.room->elements()) {
            if (count >= 4) break;
            if (e.kind != kinds::portal || !e.alive || is_screen(e) || e.params.num(Key{"light"}, 1.0) <= 0.0) continue;
            const auto it = worlds_.find(e.id);
            if (it == worlds_.end() || !it->second.world || !declared_world(*placed.room,e,*it->second.world)) continue;
            const Pose door = pose_of(*placed.room, e);
            // A door shut in it: nothing of the other side here.
            bool shut = false;
            covered(*placed.room, e, door, static_cast<float>(e.params.num(keys::w, 3.0) * 0.5), static_cast<float>(e.params.num(keys::h, 2.0) * 0.5), shut);
            // (Shut, it is still there for the leaf in it: uDoorIn.z.)
            const gl::Vec3 at = to_vec3(compose_pose(placed.pose, door).position);
            const Vec3d a = across(door.yaw + placed.pose.yaw), in = heading(door.yaw + placed.pose.yaw);
            const LookState& look = look_of(*it->second.world);
            const double amb = value(look, passes::scene, Key{"uAmbient"}, 0.55);
            const auto v3 = [&](Key kx, Key ky, Key kz, double fx, double fy, double fz) {
                return gl::Vec3{static_cast<float>(value(look, passes::scene, kx, fx) * amb),
                                static_cast<float>(value(look, passes::scene, ky, fy) * amb),
                                static_cast<float>(value(look, passes::scene, kz, fz) * amb)};
            };
            scene_->set(name("uDoorAt", count), at.x, at.y, at.z, static_cast<float>(e.params.num(keys::w, 3.0) * 0.5));
            scene_->set(name("uDoorAxis", count), static_cast<float>(a.x), static_cast<float>(a.z),
                        static_cast<float>(e.params.num(keys::h, 2.0) * 0.5), 0.0f);
            scene_->set(name("uDoorIn", count), static_cast<float>(in.x), static_cast<float>(in.z), shut ? 1.0f : 0.0f, 0.0f);
            gl::Vec3 sky = v3(Key{"uSky.x"}, Key{"uSky.y"}, Key{"uSky.z"}, 0.10, 0.13, 0.20),
                     ground = v3(Key{"uGround.x"}, Key{"uGround.y"}, Key{"uGround.z"}, 0.14, 0.10, 0.07);
            // And what the far room's probe nearest the doorway holds of its
            // lamps' light come back, from above and from below.
            const Spatial3D& far = *it->second.world;
            if (!index_of(far).probes.empty()) {
                const Element* back = !it->second.back.empty() ? far.find(it->second.back) : back_portal(far, *placed.room);
                const Vec3d there = back ? world_pose(far, *back).position : Vec3d{};
                const Element* nearest = nullptr;
                double best = 1e300;
                for (const Element* p : index_of(far).probes) {
                    if (!p->alive) continue;
                    const Vec3d d = world_pose(far, *p).position - there;
                    if (dot(d, d) < best) best = dot(d, d), nearest = p;
                }
                if (nearest) {
                    const Sh9 sh = probe_light(far, sets_of(*nearest));
                    const Rgb up = sh.irradiance({0, 1, 0}), down = sh.irradiance({0, -1, 0});
                    sky = sky + gl::Vec3{static_cast<float>(up.r), static_cast<float>(up.g), static_cast<float>(up.b)};
                    ground = ground + gl::Vec3{static_cast<float>(down.r), static_cast<float>(down.g), static_cast<float>(down.b)};
                }
            }
            scene_->set(name("uDoorSky", count), sky);
            scene_->set(name("uDoorGround", count), ground);
            ++count;
        }
    scene_->set("uDoorCount", count);
}

float GLWorldView::covered(const Spatial3D& room,const Element& portal,const Pose& door,float half_w,float half_h,bool& shut) const {
    // What stands in a doorway is what stands near it: found by walking every
    // thing in the room, so kept while the doorway is as it was, nothing at
    // rest has changed or come to rest (the room's flips), and nothing that
    // moves is near it.
    const RoomCasters* rc = nullptr;
    for (const auto& entry : room_casters_)
        if (entry.second->state == &room && entry.second->refreshed == frame_count_) rc = entry.second.get();
    if (!rc) return portal_occlusion(room,portal,door,half_w,half_h,shut);
    uint64_t key = fnv(fnv(chain_stamp(room, portal, slot_of(portal).chain), rc->flip_count), reinterpret_cast<std::uintptr_t>(rc));
    key = fnv(key, rc->structure);
    // (Only what stands within reach of the doorway is looked at by
    // portal_occlusion, in the room's own frame.)
    const double reach = half_w + 1.6;
    for (uint32_t k : rc->movers) {
        const Caster& c = rc->list[k];
        const double dx = c.lx - door.position.x, dz = c.lz - door.position.z;
        if (dx * dx + dz * dz <= reach * reach) key = fnv(fnv(key, reinterpret_cast<std::uintptr_t>(c.element)), c.where);
    }
    OccMemo& memo = occlusion_[&portal];
    if (memo.key != key || !memo.known) {
        bool is_shut = false;
        memo.value = portal_occlusion(room,portal,door,half_w,half_h,is_shut);
        memo.shut = is_shut, memo.key = key, memo.known = true;
    }
    shut = memo.shut;
    return memo.value;
}

auto GLWorldView::read_lights(const std::vector<PlacedRoom>& rooms, std::size_t& shadowed) -> std::vector<Light> {
    std::vector<Light> out;
    for (const PlacedRoom& placed : rooms)
        if (placed.room && !placed.image)
            for (const Light& l : own_lights(*placed.room, placed.pose)) out.push_back(l);
    // A lamp whose light ends (its `range`) before it reaches any room drawn
    // lights nothing here: it is left out before the strongest are chosen,
    // so it takes no place from one that does. Which rooms are drawn, not
    // where the eye is in them, says so: nothing pops as one walks. A room
    // whose bounds are not known (no `room_w`, `room_d`, `room_h`; a space
    // not enclosed; a copy of one that wraps) may be reached by anything.
    {
        struct Box { Pose pose; spatial::projection::Vec3 hi; };
        std::vector<Box> boxes;
        bool bounded = true;
        for (const PlacedRoom& placed : rooms) {
            if (!placed.room) continue;
            const auto& p = placed.room->params();
            if (placed.image || p.num(Key{"enclosed"}, 1.0) < 0.5 || !p.has(Key{"room_w"}) || !p.has(Key{"room_d"}) ||
                !p.has(Key{"room_h"})) {
                bounded = false;
                break;
            }
            boxes.push_back({placed.pose, {static_cast<float>(p.num(Key{"room_w"})), static_cast<float>(p.num(Key{"room_h"})),
                                           static_cast<float>(p.num(Key{"room_d"}))}});
        }
        if (bounded && !boxes.empty()) {
            // (Half a metre to spare: a lamp in the wall's thickness, a
            // fitting proud of the ceiling.)
            constexpr float kSpare = 0.5f;
            const auto reaches = [&](const Light& l) {
                if (l.sun || l.range <= 0.0f) return true;
                for (const Box& b : boxes) {
                    Light local = l;
                    local.pos = to_vec3(local_of(b.pose, Vec3d{l.pos.x, l.pos.y, l.pos.z}));
                    if (light_meets_box(local, {-kSpare, -kSpare, -kSpare}, {b.hi.x + kSpare, b.hi.y + kSpare, b.hi.z + kSpare}))
                        return true;
                }
                return false;
            };
            out.erase(std::remove_if(out.begin(), out.end(), [&](const Light& l) { return !reaches(l); }), out.end());
        }
    }
    // A sun first - it lights everything, so it has the first shadow -
    // then lamps, brightest first. Not nearest: which lamps cast shadows
    // must not change as the viewer walks about, or shadows pop in and out.
    // Equal lamps are ordered by where they hang, never by the viewer:
    // a tie broken by distance hands the shadow maps from lamp to lamp as
    // the viewer walks, and shadows vanish a step further off.
    // Bounce light stands in for light from all round and casts no
    // shadow worth a map: it comes after every real lamp.
    std::sort(out.begin(), out.end(), [](const Light& a, const Light& b) {
        if (a.indirect != b.indirect) return b.indirect;
        if (a.sun != b.sun) return a.sun;
        if (a.power != b.power) return a.power > b.power;
        if (a.pos.x != b.pos.x) return a.pos.x < b.pos.x;
        if (a.pos.z != b.pos.z) return a.pos.z < b.pos.z;
        return a.pos.y < b.pos.y;
    });
    // (Lamps, not layers: a lamp with no cone takes six layers of the maps,
    // one a face of a cube round it - while the array holds them.)
    std::size_t own = 0, faces = 0;
    while (own < std::min(out.size(), kOwnShadows) && faces + shadow_faces(out[own]) <= kShadowMaps)
        faces += static_cast<std::size_t>(shadow_faces(out[own])), ++own;
    // What comes through a doorway, shut or not - the same lights in the
    // same places, only what they reach told by the door (hung_only) - gets
    // a shadow map of its own while there are maps: then what stands in the
    // opening - a door ajar, the frame - throws its own shadow. Past that,
    // it is let in by how much of the opening is clear.
    std::vector<Light> in;
    for (const PlacedRoom& placed : rooms)
        if (!placed.image)
            for (const Light& l : through_doorways(placed)) in.push_back(l);
    shadowed = std::min({own + in.size(), std::max(kShadowLights, own), own + (kShadowMaps - faces)});
    for (std::size_t k = 0; k < shadowed - own && k < in.size(); ++k) in[k].open = 1.0f;
    out.insert(out.begin() + static_cast<std::ptrdiff_t>(own), in.begin(), in.end());
    shadowed = std::min(shadowed, out.size());
    if (out.size() > kMaxLights) out.resize(kMaxLights);
    if (out.empty()) {
        Light none;
        none.power = 0.0f;  // everything off: ambient only, and the shadow map unused
        out.push_back(none);
    }
    return out;
}

bool GLWorldView::same_eye(const Element& a, const Element& b) {
    for (Key k : {keys::x, keys::y, keys::z, keys::yaw, keys::pitch, keys::fov})
        if (a.params.num(k, 0.0) != b.params.num(k, 0.0)) return false;
    return true;
}

const Element* GLWorldView::back_portal(const Spatial3D& guest, const Spatial3D& host) const {
    for (const Element* ep : index_of(guest).portals) {
        const Element& e = *ep;
        if (!e.alive) continue;
        auto it = worlds_.find(e.id);
        if (it != worlds_.end() && it->second.world == &host && declared_world(guest,e,host)) return &e;
    }
    return nullptr;
}

}  // namespace sg::render
