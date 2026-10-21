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
    std::printf("%s\n", failures ? "FAILED" : "all ok");
    return failures ? 1 : 0;
}
