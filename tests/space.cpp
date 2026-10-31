// Rooms made one room (sg::make_space, the DSL's `space`): two rooms glued
// at a doorway become one local state - their walls, floors and things in
// one frame, the doorway a hole a walker passes and a ray sees through, the
// rooms and their seam no more; and what cannot be one is refused, said.
#include <cstdio>
#include <string>
#include <vector>

#include "sg/core/StateGraph.hpp"
#include "sg/domains/Atlas.hpp"
#include "sg/domains/Room.hpp"
#include "sg/domains/Walk.hpp"

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("[%s] %s\n", ok ? "ok" : "FAIL", what.c_str());
    if (!ok) ++failures;
}
// A room 4 m square with a doorway in its north wall (`north`) or its south.
sg::Room& room(sg::StateGraph& g, const char* name, bool north) {
    auto& r = g.add<sg::Room>(sg::Key{name}, 4.0, 4.0, 3.0);
    double along = 0.0;
    const int wall = r.nearest_wall(2.0, north ? 0.0 : 4.0, along);
    r.params().set("names", std::string(name) + ".");
    r.add_opening("door", wall, along, 1.0, 2.2, 0.0, true);
    r.lay_walls();
    r.params().set("enclosed", 0.0);  // (as a room that lays its own says)
    r.fixture(sg::Key{std::string(name) + ".chair"}, 1.0, 0.0, 2.0).params.set(sg::keys::sx, 0.5).set(sg::keys::sy, 0.9).set(sg::keys::sz, 0.5);
    return r;
}
}  // namespace

int main() {
    using namespace sg;
    {
        StateGraph g;
        auto& a = room(g, "a", true);
        auto& b = room(g, "b", false);
        glue_doorway(g, Key{"ab"}, a.id(), Key{"a.door"}, b.id(), Key{"b.door"});
        auto& one = g.add<Room>(Key{"ab1"}, 1.0, 1.0, 3.0);
        std::vector<std::string> why;
        const bool made = make_space(g, one.id(), {a.id(), b.id()}, &why);
        check(made, "two rooms glued at a doorway made one room" + (why.empty() ? std::string() : ": " + why.front()));
        check(!g.find(Key{"a"}) && !g.find(Key{"b"}) && !g.seam(Key{"ab"}), "the rooms and their seam are no more");
        check(one.find(Key{"a.chair"}) && one.find(Key{"b.chair"}), "what each was made of is the one room's");
        check(one.params().num(Key{"regions"}) == 2.0, "the floor each covered kept: two regions");
        check(one.params().num(Key{"room_d"}) > 7.5 && one.params().num(Key{"room_w"}) > 3.5, "its floor plan theirs together, from its origin");
        const Element* hole = one.find(Key{"a.door"});
        check(hole && hole->kind == Key{"hole"}, "the doorway between them a hole, no doorway");
        check(space_faults(one).empty(), "a walker passes the hole");
        check(one.params().num(Key{"enclosed"}, 1.0) < 0.5, "closed by its rooms' own walls: no box of the renderer's round it, to fight their floors");
        // A ray from the middle of one room's floor plan through the hole runs on into the other.
        const double z0 = one.params().num(Key{"region0_z0"}), z1 = one.params().num(Key{"region1_z0"});
        const Element* chair_a = one.find(Key{"a.chair"});
        const double xa = chair_a->params.num(keys::x) + 1.0;
        Vec3d hit, normal;
        double dist = 0.0;
        const bool a_first = z0 > z1;
        const Vec3d from{xa, 1.0, (a_first ? z0 : z1) + 2.0};
        const bool hits = ray(one, from, {0.0, 0.0, a_first ? -1.0 : 1.0}, 20.0, hit, normal, &dist);
        check(hits && dist > 5.5, "a ray through the hole sees the far room's far wall (" + std::to_string(dist) + " m)");
    }
    {
        StateGraph g;
        auto& a = room(g, "a", true);
        auto& b = room(g, "b", false);
        auto& one = g.add<Room>(Key{"ab1"}, 1.0, 1.0, 3.0);
        std::vector<std::string> why;
        check(!make_space(g, one.id(), {a.id(), b.id()}, &why) && !why.empty() && why.front().find("not reached") != std::string::npos,
              "rooms no seam joins are not one, said: " + (why.empty() ? std::string() : why.front()));
        check(g.find(Key{"a"}) && g.find(Key{"b"}), "and nothing is made: the rooms stand");
    }
    {
        // Two rooms glued at a doorway, each its own: an eye's ray through the
        // doorway goes on into the other room (ray_seen) - a lens focused by
        // it is focused on what is past the door, not on the doorway's plane.
        StateGraph g;
        auto& a = room(g, "a", true);
        auto& b = room(g, "b", false);
        glue_doorway(g, Key{"ab"}, a.id(), Key{"a.door"}, b.id(), Key{"b.door"});
        const Element* door = a.find(Key{"a.door"});
        check(door && door->kind == kinds::portal && door->params.num("walk", 0.0) > 0.5, "the doorway is a walked portal");
        const Pose at = world_pose(a, *door);
        // From 1 m and 3 m inside a, straight out through the doorway's middle.
        for (double back : {1.0, 3.0}) {
            const Vec3d in = place_in(at, {back, 0.0, 0.0}), out = place_in(at, {back - 1.0, 0.0, 0.0});
            const Vec3d d = out - in;
            Vec3d hit, normal;
            double dist = 0.0;
            Key what;
            const State* where = nullptr;
            const bool plain = ray(a, in, d, 30.0, hit, normal, &dist);
            check(!plain || dist > back - 0.05, "the room's own ray does not stop short of the doorway");
            const bool seen = ray_seen(g, a, in, d, 30.0, hit, normal, &dist, &what, &where);
            check(seen && where == &b && dist > back + 3.0 && dist < back + 4.5,
                  "from " + std::to_string(back) + " m, seen through the doorway: b's far wall at " + std::to_string(dist) + " m (" + what.str() + ")");
        }
        // Beside the doorway, the wall it is in: met where it is.
        {
            const Vec3d in = place_in(at, {1.0, 0.0, -1.2}), out = place_in(at, {0.0, 0.0, -1.2});
            Vec3d hit, normal;
            double dist = 0.0;
            const State* where = nullptr;
            const bool met = ray_seen(g, a, in, out - in, 30.0, hit, normal, &dist, nullptr, &where);
            check(met && where == &a && dist > 0.8 && dist < 1.2, "beside the doorway, its wall is met: " + std::to_string(dist) + " m");
        }
    }
    std::printf("%s\n", failures ? "FAILED" : "all ok");
    return failures ? 1 : 0;
}
