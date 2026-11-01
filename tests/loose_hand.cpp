// The loose things of a room given bodies (sg/domains/Loose.hpp), and the hand
// (sg/domains/Hand.hpp) taking one up, carrying it, setting it down - every
// step through the graph: the hand sends, the physics' arrows move the body,
// a functor carries it back to the room's thing.
#include <cmath>
#include <cstdio>
#include <string>

#include "sg/core/Engine.hpp"
#include "sg/core/Temporal.hpp"
#include "sg/domains/Hand.hpp"
#include "sg/domains/Loose.hpp"

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "[ok]  " : "[FAIL]", what.c_str());
    if (!ok) ++failures;
}
}  // namespace

int main() {
    using sg::Key;
    using Phys = sg::physics::Physics;
    sg::StateGraph g;
    auto& clock = g.add<sg::Temporal>(Key{"time"});
    auto& room = g.add<sg::Room>(Key{"room"}, 4.0, 4.0, 3.0, "rect", 6, "room.");
    g.set_initial("room");
    // A block on the floor, and a cup-sized box on it that is loose.
    room.mesh(room.part("block"), 2.0, 0.0, 2.0).params.set(sg::keys::sx, 0.5).set(sg::keys::sy, 0.8).set(sg::keys::sz, 0.5).set("solid", 1.0);
    room.mesh(room.part("cup"), 2.0, 0.8, 2.0).params.set(sg::keys::sx, 0.08).set(sg::keys::sy, 0.1).set(sg::keys::sz, 0.08);
    sg::physics::loose_thing(room, "cup", 0.1, "plastic");
    check(room.element(room.part("cup")).params.get_or<std::string>("loose", "") == "room.cup", "a thing said loose says so on its parts");
    sg::physics::Bodies& bodies = sg::physics::give_bodies(g, room, "room");
    const sg::Element* body = bodies.find(Key{"@room.cup"});
    check(body != nullptr, "its physics has a body for it");
    check(body && std::fabs(body->params.num(sg::keys::y) - 0.85) < 0.02, "standing where the thing stands, on the block");
    // Joined as a program's .sg joins it: the physics open in the room's portal, its parts carried back.
    g.set_functor(sg::Functor(Key{"room_loose.out.room"}, bodies.id(), room.id()));
    g.set_focus(g.set_follows(g.embed(Key{"room.things"}, room.id(), room.part("loose"), bodies.id(), Key{}, Key{"room_loose.out.room"}, sg::EmbedSync::Live).name, true), false);
    sg::physics::join_bodies(g, bodies, {&room});
    sg::drive(g, clock, bodies.id(), Phys::step_event(), false, sg::Keeps::Always);
    for (Key k : {Phys::grab_event(), Phys::hold_event(), Phys::drop_event(), Phys::pin_event(), Phys::lay_event()}) g.port(bodies.id(), k);
    g.port(room.id(), sg::Hand::work_event(room));
    check(sg::Hand::missing_ports(g, room).empty(), "the hand finds every port it sends to declared");
    check(sg::physics::bodies_of(g, room.id()) == &bodies, "the room's physics is the one its portal is open to");
    const auto problems = g.validate();
    for (const auto& p : problems) std::printf("  %s\n", p.c_str());
    check(problems.empty(), "the graph validates");

    // The eye a pace off, looking at the cup.
    sg::Element& eye = room.camera();
    sg::set_position(eye, {1.3, 1.5, 2.0});
    eye.params.set(sg::keys::yaw, 0.0).set(sg::keys::pitch, std::atan2(0.85 - 1.5, 0.7));
    sg::Engine engine(g);
    engine.start();
    sg::Hand hand;
    const auto frames = [&](int n, sg::HandButtons b) {
        for (int i = 0; i < n; ++i) {
            hand.frame(engine, g, room, b, 1.0 / 60.0);
            engine.tick(1.0 / 60.0);
            b.left = false;
        }
    };
    sg::HandButtons click;
    click.left = true;
    frames(1, click);
    frames(20, {});
    check(hand.held() == Key{"@room.cup"}, "a click takes up what the eye is on");
    check(body && sg::physics::text_of(*body, Key{"mode"}) == sg::physics::mode::held, "its body is in the hand");
    const sg::Element& part = room.element(room.part("cup"));
    check(part.params.num(sg::keys::y) > 1.0, "and the room's thing goes where its body goes - up, before the eye (" + std::to_string(part.params.num(sg::keys::y)) + ")");
    // A click: to be put down - it hangs over where the eye is; a click again: down.
    frames(1, click);
    frames(10, {});
    frames(1, click);
    frames(120, {});
    check(hand.held().empty(), "two clicks set it down");
    check(body && sg::physics::text_of(*body, Key{"mode"}) == sg::physics::mode::rest, "and it comes to rest");
    check(body && body->params.num(sg::keys::y) > 0.79, "on the block it was taken from (" + std::to_string(body ? body->params.num(sg::keys::y) : 0.0) + ")");
    std::printf("%s\n", failures ? "FAILED" : "all ok");
    return failures ? 1 : 0;
}
