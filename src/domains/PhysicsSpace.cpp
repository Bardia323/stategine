#include "sg/domains/PhysicsSpace.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace sg::physics {

namespace {

// A support, a box round `centre` (in the physics' frame), `half` its size,
// turned `yaw` about up.
void support(Physics& ph, const std::string& id, const sg::Vec3d& centre, const sg::Vec3d& half, double yaw) {
    sg::Element& s = ph.add_support(sg::Key{id});
    s.params.set(sg::keys::x, centre.x).set(sg::keys::z, centre.z).set(sg::keys::yaw, yaw);
    s.params.set(sg::keys::sx, half.x * 2).set(sg::keys::sz, half.z * 2).set("base", centre.y - half.y).set("top", centre.y + half.y);
}

}  // namespace

void room_reach(const sg::Spatial3D& room, sg::Vec3d& lo, sg::Vec3d& hi) {
    lo = {1e30, 1e30, 1e30}, hi = {-1e30, -1e30, -1e30};
    const auto add = [&](const sg::Vec3d& p) {
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    };
    const sg::Params& p = room.params();
    if (p.has(sg::Key{"room_w"}) && p.has(sg::Key{"room_d"})) {
        add({0, 0, 0});
        add({p.num(sg::Key{"room_w"}), p.num(sg::Key{"room_h"}, 3.0), p.num(sg::Key{"room_d"})});
        return;
    }
    for (const sg::Element& e : room.elements()) {
        if (!e.alive || e.kind != sg::kinds::wall) continue;
        const sg::Pose w = sg::world_pose(room, e);
        const double hx = e.params.num(sg::keys::sx, 0.0) * 0.5, hz = e.params.num(sg::keys::sz, 0.0) * 0.5;
        for (double x : {-hx, hx})
            for (double z : {-hz, hz}) {
                const sg::Vec3d c = w.position + sg::rotate_xz({x, 0, z}, w.yaw);
                add(c), add(c + sg::Vec3d{0, e.params.num(sg::keys::sy, 0.0), 0});
            }
    }
}

void lay_space(Physics& ph, const std::vector<sg::PlacedRoom>& rooms) {
    // (Rooms made one - sg::make_space - are one room whose floor plan is no
    // box: its floors and ceilings are the floor each covered, `regions`.)
    const auto regions = [](const sg::Spatial3D& r) { return static_cast<int>(r.params().num("regions", 0.0)); };
    if (rooms.size() > 1 || (rooms.size() == 1 && rooms[0].room && regions(*rooms[0].room) > 0)) ph.params().set("shell", 0.0);
    // (Its size the room's as it is now - a room made of rooms is as big as
    // they are together, not as it was made, empty, before they joined it.)
    if (rooms.size() == 1 && rooms[0].room)
        for (const char* k : {"room_w", "room_d", "room_h"})
            if (rooms[0].room->params().has(sg::Key{k})) ph.params().set(sg::Key{k}, rooms[0].room->params().num(sg::Key{k}));
    const double floor = ph.params().num("floor");
    for (const sg::PlacedRoom& placed : rooms) {
        if (!placed.room) continue;
        const sg::Spatial3D& room = *placed.room;
        const sg::Pose& at = placed.pose;
        const std::string tag = room.id().str() + ".";
        // Where in the physics' frame a point of the room is.
        const auto there = [&](const sg::Vec3d& p) { return at.position + sg::rotate_xz(p, at.yaw); };
        for (int k = 0; k < regions(room); ++k) {
            const std::string r = "region" + std::to_string(k) + "_";
            const double x0 = room.params().num(r + "x0"), z0 = room.params().num(r + "z0"), x1 = room.params().num(r + "x1"), z1 = room.params().num(r + "z1");
            const sg::Vec3d mid{(x0 + x1) * 0.5, 0.0, (z0 + z1) * 0.5}, half{(x1 - x0) * 0.5, 0.5, (z1 - z0) * 0.5};
            support(ph, tag + "floor" + std::to_string(k), there(mid) + sg::Vec3d{0, floor - 0.5, 0}, half, at.yaw);
            support(ph, tag + "ceiling" + std::to_string(k), there(mid) + sg::Vec3d{0, room.params().num(r + "h", 3.0) + 0.5, 0}, half, at.yaw);
        }
        if (rooms.size() > 1 && regions(room) == 0) {
            // Its floor and its ceiling, a slab each as wide as it is.
            sg::Vec3d lo, hi;
            room_reach(room, lo, hi);
            if (lo.x <= hi.x) {
                const sg::Vec3d mid{(lo.x + hi.x) * 0.5, 0.0, (lo.z + hi.z) * 0.5};
                const sg::Vec3d half{(hi.x - lo.x) * 0.5, 0.5, (hi.z - lo.z) * 0.5};
                support(ph, tag + "floor", there(mid) + sg::Vec3d{0, floor - 0.5, 0}, half, at.yaw);
                support(ph, tag + "ceiling", there(mid) + sg::Vec3d{0, hi.y + 0.5, 0}, half, at.yaw);
            }
        }
        // Where its doorways made one stand: the opening each is in (a
        // room's own, `<name>_hole`) is open with it.
        std::vector<std::pair<sg::Vec3d, double>> open;
        for (sg::Key d : placed.doorways)
            if (const sg::Element* pe = room.find(d)) open.push_back({sg::world_pose(room, *pe).position, 0.5 * pe->params.num(sg::keys::w, 1.0)});
        const auto opened = [&](const sg::Element& e) {
            const sg::Vec3d c = sg::world_pose(room, e).position;
            for (const auto& [o, half] : open)
                if (std::hypot(c.x - o.x, c.z - o.z) <= half) return true;
            return false;
        };
        int n = 0;
        for (const sg::Element& e : room.elements()) {
            if (!e.alive) continue;
            const std::string k = tag + std::to_string(n++);
            if (e.kind == sg::kinds::wall) {
                // (What fills an opening onto nothing is no wall where the
                // opening is made one with a room.)
                const std::string& id = e.id.str();
                if (id.size() > 6 && id.compare(id.size() - 6, 6, ".blank") == 0 && !placed.doorways.empty()) continue;
                const sg::Pose w = sg::world_pose(room, e);
                const double sx = e.params.num(sg::keys::sx, 1.0), sy = e.params.num(sg::keys::sy, 1.0), sz = e.params.num(sg::keys::sz, 1.0);
                support(ph, "w" + k, there({w.position.x, w.position.y + sy * 0.5, w.position.z}), {sx * 0.5, sy * 0.5, sz * 0.5}, w.yaw + at.yaw);
            } else if (e.kind == sg::kinds::portal && e.params.num("walk", 0.0) > 0.5 && !e.params.has(sg::Key{"ball"}) &&
                       std::find(placed.doorways.begin(), placed.doorways.end(), e.id.key()) == placed.doorways.end() && !opened(e)) {
                // A doorway not made one: stopped, a plug just beyond it.
                const sg::Pose w = sg::world_pose(room, e);
                const double wd = e.params.num(sg::keys::w, 1.0), h = e.params.num(sg::keys::h, 2.0);
                const sg::Vec3d out{-std::cos(w.yaw) * 0.08, 0.0, -std::sin(w.yaw) * 0.08};
                support(ph, "door" + k, there(w.position + out), {0.05, h * 0.5 + 0.1, wd * 0.5 + 0.1}, w.yaw + at.yaw);
            }
        }
    }
}

}  // namespace sg::physics
