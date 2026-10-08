#include "sg/render/ViewPlan.hpp"
#include "sg/core/Temporal.hpp"
#include "sg/domains/Atlas.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace sg::render {
ViewCamera view_camera(const Element &c) {
    ViewCamera out;
    out.eye = position_of(c);
    out.forward = forward_of(c);
    // The view may stand back from the eye along the way it looks (`back`):
    // seen square on (`ortho`), so what is beside the eye is seen, not cut;
    // in depth, so a lens narrowing towards square on keeps what it frames.
    const double back = c.params.num(Key{"back"}, c.params.num(Key{"ortho"}, 0.0) > 0.0 ? 40.0 : 0.0);
    if (back != 0.0) out.eye = {out.eye.x - out.forward.x * back, out.eye.y - out.forward.y * back, out.eye.z - out.forward.z * back};
    out.fov = c.params.num(keys::fov, 70);
    const float roll = static_cast<float>(c.params.num(keys::roll));
    if (c.params.has(Key{"stand_w"})) {
        // Standing on ground of its own (a wall, a planet's far side): its up is that ground's, turned by the look.
        out.up = up_of(c);
    } else if (roll != 0) {
        using namespace spatial::projection;
        const Vec3 f{static_cast<float>(out.forward.x), static_cast<float>(out.forward.y),
                     static_cast<float>(out.forward.z)};
        const auto side = normalize(cross(f, Vec3{0, 1, 0}));
        const auto up = cross(side, f) * std::cos(roll) + side * std::sin(roll);
        out.up = {up.x, up.y, up.z};
    }
    return out;
}
HalfSpace portal_clip(const State &host, const Element &portal, const Element &hc, const Element &gc) {
    const Pose p = world_pose(host, portal);
    if (!upright(p) || hc.params.has(Key{"stand_w"}) || gc.params.has(Key{"stand_w"})) {
        // The portal's plane, carried by the turn and shift that carried the
        // eye: whatever took the host's eye to the guest's.
        const Pose by = compose_pose(eye_pose(gc), inverse(eye_pose(hc)));
        const Vec3d n = facing(p);
        const double inset = portal_inset(portal);
        const Vec3d q = place_in(by, {p.position.x + n.x * inset, p.position.y + n.y * inset, p.position.z + n.z * inset});
        const Vec3d m = turn(by, n);
        return {{-m.x, -m.y, -m.z}, m.x * q.x + m.y * q.y + m.z * q.z};
    }
    const Vec3d n = heading(p.yaw);
    const double inset = portal_inset(portal);
    const Vec3d at{p.position.x + n.x * inset, p.position.y, p.position.z + n.z * inset};
    const double turn = gc.params.num(keys::yaw) - hc.params.num(keys::yaw);
    const auto he = position_of(hc), ge = position_of(gc);
    const auto off = rotate_xz({at.x - he.x, at.y - he.y, at.z - he.z}, turn);
    const Vec3d q{ge.x + off.x, ge.y + off.y, ge.z + off.z};
    const auto m = rotate_xz(n, turn);
    return {{-m.x, -m.y, -m.z}, m.x * q.x + m.y * q.y + m.z * q.z};
}
DrawLists draw_lists(const Spatial3D &room) {
    DrawLists out;
    for (std::size_t i = 0; i < room.elements().size(); ++i) {
        const auto &e = room.elements()[i];
        if (!e.alive)
            continue;
        if (e.kind == kinds::mesh || e.kind == kinds::wall)
            out.solids.push_back(i);
        else if (e.kind == kinds::portal)
            out.portals.push_back(i);
        else if (e.kind == kinds::light || e.kind == Key{"terrain"})
            out.unbounded.push_back(i);
    }
    return out;
}
spatial::projection::Mat4 box_transform(const State &st, const Element &e) {
    using namespace spatial::projection;
    const Vec3 s{static_cast<float>(e.params.num(keys::sx, 1)), static_cast<float>(e.params.num(keys::sy, 1)),
                 static_cast<float>(e.params.num(keys::sz, 1))};
    // Its whole turn, its anchors' with it; turned about its middle.
    const Pose w = world_pose(st, e);
    const Vec3d c = place_in(w, {0.0, s.y * 0.5, 0.0});
    const Vec3 p{static_cast<float>(c.x), static_cast<float>(c.y), static_cast<float>(c.z)};
    auto turn = Mat4::rotate_y(static_cast<float>(w.yaw));
    if (!upright(w))
        turn = turn * Mat4::rotate_z(static_cast<float>(w.pitch)) * Mat4::rotate_x(static_cast<float>(w.roll));
    return Mat4::translate(p) * turn * Mat4::scale(s);
}
std::vector<DrawInstance> enclosure(const Spatial3D &world) {
    using spatial::projection::Mat4;
    using spatial::projection::Vec3;
    std::vector<DrawInstance> out;
    const auto add = [&](const Mat4 &m, const Vec3 &c, float rough, float surface) {
        DrawInstance d;
        d.model = m;
        d.colour = {c.x, c.y, c.z};
        d.roughness = rough;
        d.surface = surface;
        out.push_back(d);
    };
    // A space that is not a room - the void, a sky round planets - says so:
    // nothing encloses it but what it holds.
    if (world.params().num(Key{"enclosed"}, 1.0) < 0.5) return out;
    const float w = static_cast<float>(world.params().num(Key{"room_w"}, 14.0));
    const float d = static_cast<float>(world.params().num(Key{"room_d"}, 12.0));
    const float h = static_cast<float>(world.params().num(Key{"room_h"}, 4.0));
    const float t = 0.25f;
    const Vec3 floor_c{static_cast<float>(world.params().num(Key{"floor_r"}, 0.42)),
                       static_cast<float>(world.params().num(Key{"floor_g"}, 0.39)),
                       static_cast<float>(world.params().num(Key{"floor_b"}, 0.36))};
    const Vec3 wall_c{static_cast<float>(world.params().num(Key{"wall_r"}, 0.52)),
                      static_cast<float>(world.params().num(Key{"wall_g"}, 0.50)),
                      static_cast<float>(world.params().num(Key{"wall_b"}, 0.48))};

    // `floor_surface` and `ceiling_surface` pick their materials (tiles
    // and plaster if not said); `ceiling_r/g/b` its colour.
    const float floor_s = static_cast<float>(world.params().num(Key{"floor_surface"}, 1.0));
    const float ceil_s = static_cast<float>(world.params().num(Key{"ceiling_surface"}, 2.0));
    const Vec3 ceil_c = world.params().has(Key{"ceiling_r"})
                            ? Vec3{static_cast<float>(world.params().num(Key{"ceiling_r"})),
                                   static_cast<float>(world.params().num(Key{"ceiling_g"})),
                                   static_cast<float>(world.params().num(Key{"ceiling_b"}))}
                            : wall_c * 0.5f;
    add(Mat4::translate({w / 2, -t / 2, d / 2}) * Mat4::scale({w, t, d}), floor_c, 0.55f, floor_s);
    add(Mat4::translate({w / 2, h + t / 2, d / 2}) * Mat4::scale({w, t, d}), ceil_c, 0.95f, ceil_s);

    if (std::any_of(world.elements().begin(), world.elements().end(),
                    [](const Element &e) { return e.alive && e.kind == kinds::wall; }))
        return out; // the state places its own walls

    add(Mat4::translate({-t / 2, h / 2, d / 2}) * Mat4::scale({t, h, d}), wall_c, 0.9f, 2.0f);
    add(Mat4::translate({w + t / 2, h / 2, d / 2}) * Mat4::scale({t, h, d}), wall_c, 0.9f, 2.0f);
    add(Mat4::translate({w / 2, h / 2, -t / 2}) * Mat4::scale({w, h, t}), wall_c, 0.9f, 2.0f);
    add(Mat4::translate({w / 2, h / 2, d + t / 2}) * Mat4::scale({w, h, t}), wall_c, 0.9f, 2.0f);

    // Skirting board: cheap, and it sells the scale.
    const float sh = 0.16f;
    const Vec3 trim{0.20f, 0.18f, 0.17f};
    add(Mat4::translate({w / 2, sh / 2, 0.06f}) * Mat4::scale({w, sh, 0.12f}), trim, 0.7f, 0.0f);
    add(Mat4::translate({w / 2, sh / 2, d - 0.06f}) * Mat4::scale({w, sh, 0.12f}), trim, 0.7f, 0.0f);
    add(Mat4::translate({0.06f, sh / 2, d / 2}) * Mat4::scale({0.12f, sh, d}), trim, 0.7f, 0.0f);
    add(Mat4::translate({w - 0.06f, sh / 2, d / 2}) * Mat4::scale({0.12f, sh, d}), trim, 0.7f, 0.0f);
    return out;
}

DrawLight light_of(const State &room, const Element &e, const Pose &pose) {
    using spatial::projection::normalize;
    using spatial::projection::Vec3;
    const auto vec = [](Vec3d v) {
        return Vec3{static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
    };
    DrawLight l;
    l.pos = vec(compose_pose(pose, world_pose(room, e)).position);
    l.color = {static_cast<float>(e.params.num("r", 1)), static_cast<float>(e.params.num("g", .93)),
               static_cast<float>(e.params.num("b", .82))};
    l.power = static_cast<float>(e.params.num(keys::intensity, 1)) * 26.0f;
    l.dir = normalize(vec(turn(pose, {e.params.num("dx"), e.params.num("dy", -1), e.params.num("dz")})));
    l.inner = static_cast<float>(e.params.num("inner", .55));
    l.outer = static_cast<float>(e.params.num("outer", 1.15));
    l.sun = e.params.num("sun") > .5;
    l.extent = static_cast<float>(e.params.num("extent", 40));
    // A sun that lights one room (a window's) keeps its shadow box on the room,
    // not on whoever walks about in it: `pin_x/y/z`, where its box is centred.
    if (e.params.has(Key{"pin_x"})) {
        l.pinned = true;
        l.focus = vec(Vec3d{e.params.num("pin_x"), e.params.num("pin_y"), e.params.num("pin_z")});
    }
    l.floor = static_cast<float>(e.params.num("shadow_floor", -1));
    l.indirect = e.params.num("indirect") > .5;
    l.falloff = static_cast<float>(std::clamp(e.params.num("falloff"), 0.0, 1.0));
    l.scatter = static_cast<float>(std::max(e.params.num("scatter", 1), 0.0));
    l.frame_w = static_cast<float>(std::max(e.params.num("frame_w", 0), 0.0));
    l.frame_h = static_cast<float>(std::max(e.params.num("frame_h", 0), 0.0));
    l.frame_soft = static_cast<float>(std::clamp(e.params.num("frame_soft", 0.08), 0.001, 1.0));
    l.range = l.sun ? 0.0f : static_cast<float>(std::max(e.params.num("range", 0), 0.0));
    return l;
}
float range_window(float distance, float range) {
    if (range <= 0.0f) return 1.0f;
    const float q = distance / range, q2 = q * q;
    const float w = std::clamp(1.0f - q2 * q2, 0.0f, 1.0f);
    return w * w;
}
bool light_meets_box(const DrawLight &l, const spatial::projection::Vec3 &lo, const spatial::projection::Vec3 &hi) {
    if (l.sun || l.range <= 0.0f) return true;
    // The nearest point of the box to the lamp, and whether it is within reach.
    const float dx = std::max({lo.x - l.pos.x, 0.0f, l.pos.x - hi.x});
    const float dy = std::max({lo.y - l.pos.y, 0.0f, l.pos.y - hi.y});
    const float dz = std::max({lo.z - l.pos.z, 0.0f, l.pos.z - hi.z});
    return dx * dx + dy * dy + dz * dz < l.range * l.range;
}
bool light_meets_gate(const DrawLight &l) {
    if (!l.gated || l.sun || l.range <= 0.0f) return true;
    // The opening is a rectangle standing upright in the doorway: its middle,
    // half its width across, half its height up. The nearest point of it to
    // the lamp, and whether that is within reach.
    using spatial::projection::Vec3;
    const Vec3 a = spatial::projection::normalize(Vec3{l.gate_across.x, 0.0f, l.gate_across.z});
    const Vec3 to{l.pos.x - l.gate_at.x, l.pos.y - l.gate_at.y, l.pos.z - l.gate_at.z};
    const float u = to.x * a.x + to.z * a.z;
    const float cu = std::clamp(u, -l.gate_w, l.gate_w), cv = std::clamp(to.y, -l.gate_h, l.gate_h);
    const Vec3 q{a.x * cu - to.x, cv - to.y, a.z * cu - to.z};
    return q.x * q.x + q.y * q.y + q.z * q.z < l.range * l.range;
}
std::vector<DrawInstance> portal_body(const State &host, const Element &e, bool window) {
    using namespace spatial::projection;
    const auto p = world_pose(host, e);
    const Vec3 at{static_cast<float>(p.position.x), static_cast<float>(p.position.y), static_cast<float>(p.position.z)};
    const float yaw = static_cast<float>(p.yaw), w = static_cast<float>(e.params.num(keys::w, 3)),
                h = static_cast<float>(e.params.num(keys::h, 2));
    const auto rotation = Mat4::rotate_y(yaw) * Mat4::rotate_z(static_cast<float>(p.pitch)) *
                          Mat4::rotate_x(static_cast<float>(p.roll));
    std::vector<DrawInstance> out;
    // Every piece in the portal's own frame, turned as it is turned (x out of
    // its face, y up it, z across it): a frame stands as its doorway does.
    const auto add = [&](const Vec3 &offset, const Vec3 &size, Rgb colour, double roughness) {
        DrawInstance d;
        d.model = Mat4::translate(at) * rotation * Mat4::translate(offset) * Mat4::scale(size);
        d.colour = colour;
        d.roughness = roughness;
        out.push_back(d);
    };
    if (window) {
        const float t = static_cast<float>(e.params.num("casing", .22)),
                    depth = static_cast<float>(e.params.num("depth", .34));
        const Rgb c{e.params.num("r", .24), e.params.num("g", .22), e.params.num("b", .20)};
        if (t > 0) {
            add({0, 0, w * .5f + t * .5f}, {depth, h + 2 * t, t}, c, .6);
            add({0, 0, -(w * .5f + t * .5f)}, {depth, h + 2 * t, t}, c, .6);
            add({0, h * .5f + t * .5f, 0}, {depth, t, w + 2 * t}, c, .6);
            // Under the opening a sill - but not under a doorway one walks
            // through: its floor is the room's (a piece there lies in the floor).
            if (e.params.num("walk", 0) < .5) add({0, -(h * .5f + t * .5f), 0}, {depth, t, w + 2 * t}, c, .6);
        }
    } else if (e.params.num("frame", 1) > .5) {
        const float border = static_cast<float>(e.params.num("border", .15));
        add({}, {.12f, h + 2 * border, w + 2 * border}, {.14f, .11f, .08f}, .6);
    } else
        add({}, {static_cast<float>(e.params.num("thick", .004)), h, w},
            {e.params.num("r", .92), e.params.num("g", .90), e.params.num("b", .86)}, .85);
    return out;
}
spatial::projection::Mat4 portal_face(const State &host, const Element &e, bool window) {
    using namespace spatial::projection;
    // In the portal's own frame, turned as it is turned (as its body is).
    const auto p = world_pose(host, e);
    const float lift = window                          ? static_cast<float>(e.params.num("inset", .06))
                       : e.params.num("frame", 1) > .5 ? .08f
                                                       : static_cast<float>(e.params.num("thick", .004)) * .5f + .0015f;
    const Vec3 at{static_cast<float>(p.position.x), static_cast<float>(p.position.y), static_cast<float>(p.position.z)};
    const float w = static_cast<float>(e.params.num(keys::w, 3)), h = static_cast<float>(e.params.num(keys::h, 2));
    const float a0 = window ? static_cast<float>(e.params.num("crop_a0")) : 0,
                a1 = window ? static_cast<float>(e.params.num("crop_a1", 1)) : 1,
                b0 = window ? static_cast<float>(e.params.num("crop_b0")) : 0,
                b1 = window ? static_cast<float>(e.params.num("crop_b1", 1)) : 1;
    const auto v = [](const Vec3d &d) { return Vec3{static_cast<float>(d.x), static_cast<float>(d.y), static_cast<float>(d.z)}; };
    return Mat4::translate(at + v(facing(p)) * lift + v(across_of(p)) * (((a0 + a1) * .5f - .5f) * w) +
                           v(up_of(p)) * (((b0 + b1) * .5f - .5f) * h)) *
           Mat4::rotate_y(static_cast<float>(p.yaw)) * Mat4::rotate_z(static_cast<float>(p.pitch)) *
           Mat4::rotate_x(static_cast<float>(p.roll)) * Mat4::scale({1, h * (b1 - b0), w * (a1 - a0)});
}
spatial::projection::Mat4 shadow_projection(const DrawLight &l, const ViewCamera &camera, int size, float &bias) {
    if (size <= 0)
        throw std::invalid_argument("render: invalid shadow size");
    const spatial::projection::Vec3 eye{static_cast<float>(camera.eye.x), static_cast<float>(camera.eye.y),
                                        static_cast<float>(camera.eye.z)},
        forward{static_cast<float>(camera.forward.x), static_cast<float>(camera.forward.y),
                static_cast<float>(camera.forward.z)};
    spatial::projection::Mat4 result;
    if (l.sun) {
        // A box of shadow round the viewer, a little ahead of them,
        // moved in whole texels so the edges do not crawl. The sun's
        // way is taken in steps of about a fifth of a degree: it
        // creeps across the sky, and its map is drawn again when it
        // has moved that far, not every frame of the day.
        spatial::projection::Vec3 d = spatial::projection::normalize(l.dir);
        d = spatial::projection::normalize(
            {std::round(d.x * 300.0f) / 300.0f, std::round(d.y * 300.0f) / 300.0f, std::round(d.z * 300.0f) / 300.0f});
        const float e = l.extent, reach = e * 4.0f;
        const float texel = 2.0f * e / static_cast<float>(size);
        spatial::projection::Vec3 c =
            l.pinned ? l.focus : eye + spatial::projection::normalize({forward.x, 0.0f, forward.z}) * (e * 0.4f);
        const spatial::projection::Vec3 up =
            std::fabs(d.y) > 0.99f ? spatial::projection::Vec3{0, 0, 1} : spatial::projection::Vec3{0, 1, 0};
        // Whole texels of the map itself - across it and up it, as the sun
        // sees - not of the world's axes, which run slantwise over a map the
        // sun looks down on at a slant: snapped on those, the box moved by
        // parts of a texel and the edges crawled. Along the sun's way the box
        // may go where it likes: depth moves no edge.
        const spatial::projection::Vec3 right = spatial::projection::normalize(spatial::projection::cross(d, up));
        const spatial::projection::Vec3 upward = spatial::projection::cross(right, d);
        const float cr = spatial::projection::dot(c, right), cu = spatial::projection::dot(c, upward);
        c = c + right * (std::floor(cr / texel) * texel - cr) + upward * (std::floor(cu / texel) * texel - cu);
        result = spatial::projection::Mat4::ortho(-e, e, -e, e, 1.0f, reach * 2.0f) *
                 spatial::projection::Mat4::look_at(c - d * reach, c, up);
        bias = 45.0f / (reach * 2.0f);
    } else {
        // A wide lamp spreads its map thin, so each texel covers more
        // of a wall at a slant: its bias grows with the width, or the
        // walls stripe with acne.
        const float fov = std::min(l.outer * 2.05f, 2.7f);
        // Its map goes as far as its light does; with no range, 40 m.
        const float depth_end = l.range > 0.0f ? std::max(l.range, 0.5f) : 40.0f;
        result = spatial::projection::Mat4::perspective(fov, 1.0f, 0.1f, depth_end) *
                 spatial::projection::Mat4::look_at(l.pos, l.pos + l.dir,
                                                    std::fabs(l.dir.y) > 0.99f ? spatial::projection::Vec3{0, 0, 1}
                                                                               : spatial::projection::Vec3{0, 1, 0});
        bias = std::max(1.0f, std::tan(fov * 0.5f) / std::tan(0.6f)) * 1.5f;
    }
    return result;
}

float portal_occlusion(const Spatial3D &room, const Element &portal, const Pose &door, float half_w, float half_h,
                       bool &shut) {
    const Vec3d a = across(door.yaw), n = heading(door.yaw);
    float most = 0.0f;
    shut = false;
    for (const auto &e : room.elements()) {
        if (!e.alive || (e.kind != kinds::mesh && e.kind != kinds::wall) || e.id == portal.id)
            continue;
        if (e.params.num(Key{"cast"}, 1.0) < 0.5)
            continue;
        const Vec3d c = world_pose(room, e).position; // in the room, not off its anchor
        const double dx = c.x - door.position.x, dz = c.z - door.position.z;
        if (dx * dx + dz * dz > (half_w + 1.5) * (half_w + 1.5))
            continue;
        const auto m = box_transform(room, e);
        float u0 = 1e9f, u1 = -1e9f, v0 = 1e9f, v1 = -1e9f, d0 = 1e9f, d1 = -1e9f;
        for (int k = 0; k < 8; ++k) {
            const spatial::projection::Vec3 q =
                m.transform_point({k & 1 ? 0.5f : -0.5f, k & 2 ? 0.5f : -0.5f, k & 4 ? 0.5f : -0.5f});
            const double rx = q.x - door.position.x, ry = q.y - door.position.y, rz = q.z - door.position.z;
            const float u = static_cast<float>(rx * a.x + rz * a.z), v = static_cast<float>(ry);
            const float d = static_cast<float>(rx * n.x + rz * n.z);
            u0 = std::min(u0, u), u1 = std::max(u1, u), v0 = std::min(v0, v), v1 = std::max(v1, v);
            d0 = std::min(d0, d), d1 = std::max(d1, d);
        }
        // Only what is in the opening, not a thing across the room.
        if (d1 < -0.25f || d0 > 0.25f)
            continue;
        const float w = std::max(0.0f, std::min(u1, half_w) - std::max(u0, -half_w));
        const float h = std::max(0.0f, std::min(v1, half_h) - std::max(v0, -half_h));
        const float part = w * h / (4.0f * half_w * half_h);
        most = std::max(most, part);
        if (part > 0.95f && d1 - d0 < 0.1f)
            shut = true;
    }
    return std::clamp(most, 0.0f, 1.0f);
}

namespace {
bool registered(const StateGraph &graph, const State *state) {
    for (Key id : graph.ids())
        if (graph.find(id) == state)
            return true;
    return false;
}
const State *owner_of(const StateGraph &graph, const Element &element) {
    for (Key id : graph.ids())
        if (const auto *state = graph.find(id); state && state->find(element.id) == &element)
            return state;
    return nullptr;
}
} // namespace
spatial::projection::Mat4 sprite_transform(const State &host, const Element &e, const ViewCamera &camera) {
    using namespace spatial::projection;
    const auto vec = [](Vec3d p) {
        return Vec3{static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)};
    };
    const auto center = box_transform(host, e).transform_point({0, 0, 0});
    Vec3 n, up;
    if (e.params.num("face") > .5) {
        n = normalize(vec(camera.forward) * -1);
        up = normalize(vec(camera.up) - n * dot(vec(camera.up), n));
    } else {
        n = vec(camera.eye) - center;
        n.y = 0;
        n = dot(n, n) > 1e-8f ? normalize(n) : Vec3{1, 0, 0};
        up = {0, 1, 0};
    }
    const auto side = cross(n, up);
    const float w = static_cast<float>(e.params.num(keys::sx, 1)), h = static_cast<float>(e.params.num(keys::sy, 1));
    Mat4 out;
    out.m[0] = n.x;
    out.m[1] = n.y;
    out.m[2] = n.z;
    out.m[4] = up.x * h;
    out.m[5] = up.y * h;
    out.m[6] = up.z * h;
    out.m[8] = side.x * w;
    out.m[9] = side.y * w;
    out.m[10] = side.z * w;
    out.m[12] = center.x;
    out.m[13] = center.y;
    out.m[14] = center.z;
    return out;
}
Key signal_of(const StateGraph &g, const Element &panel) {
    if (!panel.params.has("shows"))
        return panel.id;
    const Key name{panel.params.get_or<std::string>("shows", {})};
    if (name.empty())
        return panel.id;
    const auto *owner = owner_of(g, panel);
    for (const auto &em : g.embeddings())
        if (owner && em.name == name) {
            const auto *source = g.find(em.host);
            const auto *p = source ? source->find(em.portal) : nullptr;
            return p && p->params.num("eye") > .5 ? em.name : em.portal;
        }
    return {};
}
bool declared_world(const StateGraph &g, const State &host, const Element &p, const State &guest) {
    if (!registered(g, &host) || !registered(g, &guest) || !p.alive || host.find(p.id) != &p)
        return false;
    for (const auto &em : g.embeddings())
        if (em.host == host.id() && em.portal == p.id && em.guest == guest.id() && em.open)
            return true;
    for (const auto &s : g.seams()) {
        if (s.a == host.id() && s.b == guest.id() &&
            std::find(s.boundary_a.begin(), s.boundary_a.end(), p.id) != s.boundary_a.end())
            return true;
        if (s.b == host.id() && s.a == guest.id() &&
            std::find(s.boundary_b.begin(), s.boundary_b.end(), p.id) != s.boundary_b.end())
            return true;
    }
    return false;
}
bool declared_feed(const StateGraph &g, Key p, const State &guest) {
    if (!registered(g, &guest))
        return false;
    for (const auto &em : g.embeddings())
        if ((em.portal == p || em.name == p) && em.guest == guest.id() && em.open) {
            const auto *host = g.find(em.host);
            const auto *e = host ? host->find(em.portal) : nullptr;
            if (e && e->alive && e->params.num("feed") > 0.5)
                return true;
        }
    return false;
}
namespace {
// What declares a surface to a portal, gathered once for each revision of a
// graph: who owns each element, which guests each portal and embedding name
// opens on, what each functor carries each object to, and what each state
// leads on to. Asked of every thing that wears a picture, each frame; walked
// through the whole graph for each, a frame in which the graph moved paid for
// a room's hundred books a hundred times.
struct SurfaceIndex {
    const StateGraph *graph = nullptr;
    uint64_t revision = ~uint64_t{0};
    std::unordered_map<const Element *, const State *> owner;
    std::unordered_set<const State *> states;
    std::unordered_map<uint64_t, std::vector<Key>> by_portal, by_object;
    std::unordered_map<Key, std::vector<Key>> by_name, next;
};
uint64_t pair_key(Key a, Key b) { return std::hash<Key>{}(a) * 1099511628211ULL ^ std::hash<Key>{}(b); }
const SurfaceIndex &surface_index(const StateGraph &g) {
    thread_local SurfaceIndex ix;
    if (ix.graph == &g && ix.revision == g.revision())
        return ix;
    ix = SurfaceIndex{};
    ix.graph = &g, ix.revision = g.revision();
    for (Key id : g.ids())
        if (const State *s = g.find(id)) {
            ix.states.insert(s);
            for (const Element &e : s->elements())
                ix.owner[&e] = s;
        }
    for (const auto &em : g.embeddings()) {
        ix.by_portal[pair_key(em.host, em.portal)].push_back(em.guest);
        ix.by_name[em.name].push_back(em.guest);
        ix.next[em.host].push_back(em.guest);
    }
    for (const auto &named : g.functors()) {
        const Functor &f = named.second;
        ix.next[f.from()].push_back(f.to());
        f.for_each_object([&](Key from, Key to) {
            ix.by_object[pair_key(f.from(), from)].push_back(f.to());
            if (f.to() != f.from())  // (one way only, within a state)
                ix.by_object[pair_key(f.to(), to)].push_back(f.from());
        });
    }
    return ix;
}
} // namespace

bool declared_surface(const StateGraph &g, const Element &p, const Surface2D &surface) {
    const SurfaceIndex &ix = surface_index(g);
    const auto own = ix.owner.find(&p);
    const State *owner = own == ix.owner.end() ? nullptr : own->second;
    if (!owner || !p.alive || !ix.states.count(&surface))
        return false;
    std::vector<Key> pending, seen;
    const auto add = [&](const auto &map, const auto &key) {
        if (const auto it = map.find(key); it != map.end())
            pending.insert(pending.end(), it->second.begin(), it->second.end());
    };
    add(ix.by_portal, pair_key(owner->id(), signal_of(g, p)));
    if (p.params.has("shows"))
        add(ix.by_name, Key{p.params.get_or<std::string>("shows", {})});
    add(ix.by_object, pair_key(owner->id(), p.id));
    while (!pending.empty()) {
        const Key id = pending.back();
        pending.pop_back();
        if (id == surface.id())
            return true;
        if (std::find(seen.begin(), seen.end(), id) != seen.end())
            continue;
        seen.push_back(id);
        add(ix.next, id);
    }
    return false;
}
double semantic_time(const StateGraph *g, const State &state) {
    if (state.params().has("own_time"))
        return state.params().num("own_time");
    double value = 0;
    bool found = false;
    if (g)
        for (const auto &d : g->drives())
            if (d.state == state.id()) {
                const auto *clock = dynamic_cast<const Temporal *>(g->find(d.clock));
                if (!clock || !clock->has_timeline(timeline_of(d)))
                    continue;
                const double t = clock->time(timeline_of(d));
                if (found && value != t)
                    throw std::invalid_argument("render: ambiguous declared time; carry own_time explicitly");
                found = true;
                value = t;
            }
    return value;
}
namespace {
DrawInstance instance(const State &s, const Element &e) {
    DrawInstance d;
    d.element = &e;
    d.pose = world_pose(s, e);
    const auto &p = e.params;
    d.size = {p.num(keys::sx, 1), p.num(keys::sy, 1), p.num(keys::sz, 1)};
    if (e.kind == kinds::wall || e.kind == kinds::mesh)
        d.pose.position.y += d.size.y * 0.5;
    d.pitch = p.num(keys::pitch);
    d.roll = p.num(keys::roll);
    const bool wall = e.kind == kinds::wall, mesh = e.kind == kinds::mesh;
    d.colour = {p.num("r", wall   ? .52
                           : mesh ? .8
                                  : .9),
                p.num("g", wall ? .50 : .5),
                p.num("b", wall   ? .48
                           : mesh ? .25
                                  : .2)};
    d.roughness = p.num("roughness", wall ? .9 : .6);
    d.surface = p.num("surface", wall ? 2 : mesh ? 3 : 0);
    d.emissive = p.num("emissive");
    d.model = box_transform(s, e);
    return d;
}
} // namespace
ViewPlan view_plan(const StateGraph &g, const State &root, const std::vector<PlacedRoom> &placements) {
    if (g.find(root.id()) != &root)
        throw std::invalid_argument("render: root is absent from graph");
    ViewPlan out;
    out.time = semantic_time(&g, root);
    out.surface = dynamic_cast<const Surface2D *>(&root);
    if (const auto *space = dynamic_cast<const Spatial3D *>(&root)) {
        auto rooms = placements;
        if (rooms.empty())
            rooms.push_back({space, {}, {}});
        // A requested placement is resource data, so its reach and transform
        // must follow a currently declared spatial seam. It cannot connect rooms.
        Atlas atlas;
        for (const auto &seam : g.seams())
            if (!seam.boundary_a.empty() && !seam.boundary_b.empty()) {
                const auto *a = dynamic_cast<const Spatial3D *>(g.find(seam.a));
                const auto *b = dynamic_cast<const Spatial3D *>(g.find(seam.b));
                const auto *pa = a ? a->find(seam.boundary_a[0]) : nullptr;
                const auto *pb = b ? b->find(seam.boundary_b[0]) : nullptr;
                if (pa && pb && pa->alive && pb->alive && pa->kind == kinds::portal && pb->kind == kinds::portal)
                    atlas.glue(seam.name, seam.a, pa->id, seam.b, pb->id);
            }
        const auto charts = atlas.charts(g, root.id(), static_cast<int>(g.ids().size()));
        for (const auto &placed : rooms) {
            if (!placed.room || g.find(placed.room->id()) != placed.room)
                continue;
            const auto chart =
                std::find_if(charts.begin(), charts.end(), [&](const Chart &c) { return c.room == placed.room->id(); });
            if (chart == charts.end())
                continue;
            const Pose expected = compose_pose(rooms.front().pose, chart->pose);
            if (!same_number(keys::x, expected.position.x, placed.pose.position.x, 1e-6) ||
                !same_number(keys::y, expected.position.y, placed.pose.position.y, 1e-6) ||
                !same_number(keys::z, expected.position.z, placed.pose.position.z, 1e-6) ||
                !same_number(keys::yaw, expected.yaw, placed.pose.yaw, 1e-6))
                continue;
            RoomDraw room;
            room.room = placed.room;
            room.placement = placed.pose;
            room.camera = view_camera(placed.room->camera());
            room.camera.far_plane = placed.room->params().num("far", 120);
            room.time = semantic_time(&g, *placed.room);
            const auto id = active_look(*placed.room);
            for (const auto &em : g.embeddings())
                if (em.host == placed.room->id() && em.guest == id && em.portal == look_slot_id())
                    room.look = dynamic_cast<const LookState *>(g.find(id));
            for (Key door : placed.doorways) {
                const bool declared = std::any_of(g.seams().begin(), g.seams().end(), [&](const Seam &seam) {
                    const auto &boundary = seam.a == placed.room->id() ? seam.boundary_a : seam.boundary_b;
                    return (seam.a == placed.room->id() || seam.b == placed.room->id()) &&
                           std::find(boundary.begin(), boundary.end(), door) != boundary.end();
                });
                if (declared)
                    if (const auto *p = placed.room->find(door); p && p->alive)
                        room.clips.push_back(room_side(*placed.room, *p, placed.pose));
            }
            const auto lists = draw_lists(*placed.room);
            for (auto i : lists.solids)
                room.instances.push_back(instance(*placed.room, placed.room->elements()[i]));
            for (auto i : lists.unbounded)
                if (placed.room->elements()[i].kind == kinds::light)
                    room.lights.push_back(&placed.room->elements()[i]);
            for (auto i : lists.portals) {
                const auto &p = placed.room->elements()[i];
                for (const auto &em : g.embeddings())
                    if (((em.host == placed.room->id() && em.portal == p.id) ||
                         (p.params.has("shows") && em.name == Key{p.params.get_or<std::string>("shows", {})})) &&
                        em.open) {
                        const auto *guest = g.find(em.guest);
                        if (guest) {
                            const auto *source = g.find(em.host);
                            const auto *eye = source ? source->find(em.portal) : nullptr;
                            room.portals.push_back({placed.room, &p, guest, &em, nullptr,
                                                    p.params.num("feed") > 0.5 || p.params.has("shows"),
                                                    eye && eye->params.num("eye") > .5 ? eye : nullptr});
                        }
                    }
                for (const auto &s : g.seams()) {
                    if (p.params.num("feed") > .5 || p.params.has("shows"))
                        continue;
                    const bool a = s.a == placed.room->id(), b = s.b == placed.room->id();
                    const auto &boundary = a ? s.boundary_a : s.boundary_b;
                    if ((a || b) && std::find(boundary.begin(), boundary.end(), p.id) != boundary.end())
                        if (const auto *guest = g.find(a ? s.b : s.a))
                            room.portals.push_back({placed.room, &p, guest, nullptr, &s, false});
                }
            }
            out.rooms.push_back(std::move(room));
        }
    } else if (dynamic_cast<const Spatial2D *>(&root)) {
        for (const auto &e : root.elements())
            if (e.alive && e.kind == kinds::sprite)
                out.sprites.push_back(instance(root, e));
    }
    return out;
}
std::vector<DrawLight> boundary_lights(const StateGraph &graph, const RoomDraw &room, const LookFader &look) {
    using namespace spatial::projection;
    std::vector<DrawLight> out;
    if (!room.room)
        return out;
    const auto vec = [](Vec3d p) {
        return Vec3{static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)};
    };
    for (const auto &overlap : room.portals) {
        const auto &e = *overlap.portal;
        const auto *far = dynamic_cast<const Spatial3D *>(overlap.guest);
        if (!far || overlap.feed || e.params.num("screen") > .5 || e.params.num("light", 1) < .5)
            continue;
        const auto *functor = graph.functor(
            overlap.seam ? (overlap.seam->a == room.room->id() ? overlap.seam->a_to_b : overlap.seam->b_to_a)
                         : overlap.embedding->in);
        const auto *carry = functor ? functor->transport_of(room.room->camera().id) : nullptr;
        if (!carry)
            continue;
        const Pose door = world_pose(*room.room, e);
        const float w = static_cast<float>(e.params.num(keys::w, 3)) * .5f,
                    h = static_cast<float>(e.params.num(keys::h, 2)) * .5f;
        if (w <= 0 || h <= 0)
            continue;
        bool shut = false;
        const float open = 1 - portal_occlusion(*room.room, e, door, w, h, shut);
        if (shut)
            continue;
        Element probe = room.room->camera(), there = far->camera();
        probe.params.set(keys::x, door.position.x)
            .set(keys::y, door.position.y)
            .set(keys::z, door.position.z)
            .set(keys::yaw, 0.0)
            .set(keys::pitch, 0.0);
        (*carry)(probe, there);
        const double turn = there.params.num(keys::yaw);
        const auto from = position_of(there);
        const auto here = [&](Vec3 p) {
            const auto q = rotate_xz({p.x - from.x, p.y - from.y, p.z - from.z}, -turn);
            return vec(
                compose_pose(room.placement, {{door.position.x + q.x, door.position.y + q.y, door.position.z + q.z}, 0})
                    .position);
        };
        const auto gate = [&](DrawLight &l) {
            l.gated = true;
            l.gate_at = vec(compose_pose(room.placement, door).position);
            l.gate_across = vec(across(door.yaw + room.placement.yaw));
            l.gate_in = vec(heading(door.yaw + room.placement.yaw));
            l.gate_w = w;
            l.gate_h = h;
            l.open = open;
            l.floor = 0;
        };
        std::vector<DrawLight> lamps;
        for (const auto &light : far->elements())
            if (light.alive && light.kind == kinds::light) {
                auto l = light_of(*far, light);
                if (l.power > 0 && !l.indirect)
                    lamps.push_back(l);
            }
        std::sort(lamps.begin(), lamps.end(), [](const auto &a, const auto &b) {
            if (a.sun != b.sun)
                return a.sun;
            return a.power > b.power;
        });
        if (lamps.size() > 3)
            lamps.resize(3);
        for (auto l : lamps) {
            l.pos = here(l.pos);
            l.dir = normalize(vec(rotate_xz({l.dir.x, l.dir.y, l.dir.z}, room.placement.yaw - turn)));
            gate(l);
            out.push_back(l);
        }
        const auto &shown = look.look_of(&graph, *far);
        const double ambient = look.value(shown, passes::scene, "uAmbient", .55);
        const Vec3 sky{static_cast<float>(look.value(shown, passes::scene, "uSky.x", .10) * ambient),
                       static_cast<float>(look.value(shown, passes::scene, "uSky.y", .13) * ambient),
                       static_cast<float>(look.value(shown, passes::scene, "uSky.z", .20) * ambient)};
        const float bright = std::max({sky.x, sky.y, sky.z});
        if (bright > .0001f) {
            DrawLight l;
            const auto beyond = vec(heading(door.yaw + room.placement.yaw)) * -1;
            l.pos = vec(compose_pose(room.placement, door).position) + beyond * .35f;
            l.dir = beyond * -1;
            l.color = sky * (1 / bright);
            l.power = bright * w * h * 24;
            l.inner = .9f;
            l.outer = 1.55f;
            l.falloff = 1;
            l.indirect = true;
            gate(l);
            out.push_back(l);
        }
    }
    if (out.size() > 8)
        out.resize(8);
    return out;
}
std::vector<DoorLight> boundary_ambient(const StateGraph &graph, const RoomDraw &room, const LookFader &look) {
    std::vector<DoorLight> out;
    const auto vec = [](Vec3d p) {
        return spatial::projection::Vec3{static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)};
    };
    if (!room.room)
        return out;
    for (const auto &overlap : room.portals) {
        const auto &e = *overlap.portal;
        const auto *far = dynamic_cast<const Spatial3D *>(overlap.guest);
        if (!far || overlap.feed || e.params.num("screen") > .5 || e.params.num("light", 1) < .5)
            continue;
        const auto door = world_pose(*room.room, e);
        bool shut = false;
        portal_occlusion(*room.room, e, door, static_cast<float>(e.params.num(keys::w, 3) * .5),
                         static_cast<float>(e.params.num(keys::h, 2) * .5), shut);
        if (shut)
            continue;
        const auto &shown = look.look_of(&graph, *far);
        const auto ambient = look.value(shown, passes::scene, "uAmbient", .55);
        const auto colour = [&](const char *name, Vec3d fallback) {
            return spatial::projection::Vec3{
                static_cast<float>(look.value(shown, passes::scene, Key{std::string(name) + ".x"}, fallback.x) *
                                   ambient),
                static_cast<float>(look.value(shown, passes::scene, Key{std::string(name) + ".y"}, fallback.y) *
                                   ambient),
                static_cast<float>(look.value(shown, passes::scene, Key{std::string(name) + ".z"}, fallback.z) *
                                   ambient)};
        };
        out.push_back({vec(compose_pose(room.placement, door).position), vec(across(door.yaw + room.placement.yaw)),
                       vec(heading(door.yaw + room.placement.yaw)), colour("uSky", {.10, .13, .20}),
                       colour("uGround", {.14, .10, .07}), static_cast<float>(e.params.num(keys::w, 3) * .5),
                       static_cast<float>(e.params.num(keys::h, 2) * .5)});
        if (out.size() == 4)
            break;
    }
    return out;
}
} // namespace sg::render
