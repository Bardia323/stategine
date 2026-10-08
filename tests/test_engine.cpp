#include <memory>
// Stategine - assertions over states, morphisms, functors, portals.
#include <cmath>
#include <cstdlib>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <cstdio>
#include <sstream>
#include <string>

#include "sg/core/Sheaf.hpp"
#include "sg/gl/Math.hpp"
#include "sg/gl/Shaders.hpp"
#include "sg/domains/Atlas.hpp"
#include "sg/render/Ascii.hpp"
#include "sg/sg.hpp"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "[ok]  " : "[FAIL]", what.c_str());
    if (!ok) ++failures;
}

bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

// Geometry goes through trig, so it is compared with a tolerance.
bool roughly(double a, double b) { return std::fabs(a - b) < 1e-5; }

// --- core -------------------------------------------------------------------
void test_keys_and_params() {
    const sg::Key a{"velocity"};
    const sg::Key b{std::string("velocity")};
    check(a == b && a.handle() == b.handle(), "equal names intern to one key");
    check(sg::Key{"velocity"} != sg::Key{"position"}, "different names stay distinct");

    sg::Params p;
    p.set("hp", int64_t{7}).set("name", std::string("hero")).set("speed", 2.5);
    check(p.size() == 3, "params hold three entries");
    p.set("hp", int64_t{9});
    check(p.size() == 3 && p.get_or<int64_t>("hp", 0) == 9, "setting twice overwrites in place");
    check(near(p.num("hp"), 9.0), "num() reads an int as a double");
    check(near(p.num("missing", -1.0), -1.0), "num() falls back");
    p.erase("name");
    check(!p.has("name") && p.size() == 2, "erase removes one entry");
}

void test_elements_and_morphisms() {
    sg::State s("t");
    s.add_element("a", "counter").params.set("n", int64_t{0});
    s.add_element("b", "counter").params.set("n", int64_t{0});

    s.arrow("bump", "a", "b", "tick",
            [](sg::State&, sg::Element& a, sg::Element* b, const sg::Event&) {
                a.params.set("n", a.params.get_or<int64_t>("n", 0) + 1);
                b->params.set("n", a.params.get_or<int64_t>("n", 0) * 10);
            });

    s.emit("tick");
    s.dispatch_pending();
    check(s.element("a").params.get_or<int64_t>("n", -1) == 1, "morphism ran on the domain");
    check(s.element("b").params.get_or<int64_t>("n", -1) == 10, "morphism wrote the codomain");
    check(s.validate().empty(), "no dangling arrows");

    s.emit("unrelated");
    s.dispatch_pending();
    check(s.element("a").params.get_or<int64_t>("n", -1) == 1, "unrelated events fire nothing");

    s.remove_element("b");
    s.emit("tick");
    s.dispatch_pending();
    check(s.element("a").params.get_or<int64_t>("n", -1) == 1, "dangling arrow is skipped");
    check(s.validate().size() == 1, "validate reports the missing codomain");
}

void test_composition() {
    sg::State s("c");
    s.add_element("x", "n").params.set("v", 1.0);
    s.add_element("y", "n").params.set("v", 0.0);
    s.add_element("z", "n").params.set("v", 0.0);
    s.arrow("f", "x", "y", "never",
            [](sg::State&, sg::Element& a, sg::Element* b, const sg::Event&) {
                b->params.set("v", a.params.num("v") + 1.0);
            });
    s.arrow("g", "y", "z", "never",
            [](sg::State&, sg::Element& a, sg::Element* b, const sg::Event&) {
                b->params.set("v", a.params.num("v") * 3.0);
            });
    s.compose("gf", "f", "g", "run");
    s.emit("run");
    s.dispatch_pending();
    check(near(s.element("z").params.num("v"), 6.0), "g . f composes (1 -> 2 -> 6)");

    bool threw = false;
    try {
        s.compose("bad", "g", "f", "run");  // cod(g)=z != dom(f)=x
    } catch (const std::exception&) {
        threw = true;
    }
    check(threw, "mismatched composition is rejected");
}

// --- graph ------------------------------------------------------------------
void test_transitions_and_stack() {
    sg::StateGraph g;
    g.add<sg::Spatial2D>("a");
    g.add<sg::Spatial2D>("b");
    g.add<sg::ConsoleState>("menu");
    g.connect("a", "go", "b");
    g.push("b", "menu", "menu");
    g.pop("menu", "back");
    g.set_initial("a");

    sg::Engine e(g);
    e.start();
    check(e.current()->id() == sg::Key{"a"}, "engine starts in the initial state");
    e.fire("go");
    e.tick(0.0);
    check(e.current()->id() == sg::Key{"b"}, "switch transition");
    e.fire("menu");
    e.tick(0.0);
    check(e.stack().size() == 2 && e.current()->id() == sg::Key{"menu"},
          "push keeps the stack below");
    e.fire("back");
    e.tick(0.0);
    check(e.stack().size() == 1 && e.current()->id() == sg::Key{"b"},
          "pop returns to the paused state");
}

void test_guards_and_wildcards() {
    sg::StateGraph g;
    g.add<sg::Spatial2D>("play");
    g.add<sg::Spatial2D>("dead");
    g.state("play").params().set("hp", int64_t{2});
    sg::Transition t;
    t.from = "*";
    t.trigger = "hit";
    t.to = "dead";
    t.guard = [](const sg::State& from, const sg::Event&) {
        return from.params().get_or<int64_t>("hp", 1) <= 0;
    };
    g.connect(std::move(t));
    g.set_initial("play");

    sg::Engine e(g);
    e.start();
    e.fire("hit");
    e.tick(0.0);
    check(e.current()->id() == sg::Key{"play"}, "guard blocks the transition");
    g.state("play").params().set("hp", int64_t{0});
    e.fire("hit");
    e.tick(0.0);
    check(e.current()->id() == sg::Key{"dead"}, "wildcard fires once the guard passes");
}

void test_integration() {
    sg::StateGraph g;
    auto& s = g.add<sg::Spatial2D>("world");
    s.sprite("p", 0, 0).params.set(sg::keys::vx, 2.0);
    g.set_initial("world");
    sg::Engine e(g);
    e.start();
    e.run_fixed(0.5, 4);
    check(near(s.element("p").params.num(sg::keys::x), 0.0), "a world nobody drives stands still");

    auto& clock = g.add<sg::Temporal>("clock");
    const sg::Key drive = sg::drive(g, clock, s.id(), s.step_event()).name;
    e.run_fixed(0.5, 4);
    check(near(s.element("p").params.num(sg::keys::x), 4.0), "driven, the integrator morphism ran");

    g.drop_drive(drive);
    e.run_fixed(0.5, 4);
    check(near(s.element("p").params.num(sg::keys::x), 4.0), "integration can be switched off");
}

// A fixed run is the same every run: a tick's time is the steps taken, not the
// wall clock.
struct Clocked : sg::State {
    using sg::State::State;
    std::vector<double> times;
    void on_update(const sg::Tick& t) override { times.push_back(t.time); }
};

void test_time_is_simulated() {
    sg::StateGraph g;
    auto& c = g.add<Clocked>("clocked");
    g.set_initial("clocked");
    sg::Engine e(g);
    e.run_fixed(0.25, 4);
    check(c.times.size() == 4 && c.times[0] == 0.25 && c.times[3] == 1.0 && e.simulated_time() == 1.0,
          "a tick's time is the sum of the steps, whatever the wall clock says");
}

// --- functors ---------------------------------------------------------------
void test_transports() {
    sg::Element src("a", "thing");
    src.params.set(sg::keys::x, 3.0).set(sg::keys::z, 7.0).set("hp", int64_t{5});

    sg::Element all("b", "thing");
    sg::transport::copy_all(src, all);
    check(near(all.params.num(sg::keys::z), 7.0) && all.params.has("hp"), "copy_all copies all");

    sg::Element some("c", "thing");
    sg::transport::only({sg::keys::x})(src, some);
    check(some.params.size() == 1 && near(some.params.num(sg::keys::x), 3.0), "only() filters");

    sg::Element swapped("d", "thing");
    sg::transport::swizzle({{sg::keys::y, sg::keys::z}})(src, swapped);
    check(near(swapped.params.num(sg::keys::y), 7.0), "swizzle renames z onto y");

    sg::Element scaled("e", "thing");
    sg::transport::swizzle_scaled({{sg::keys::x, sg::keys::x}},
                                  [](double v) { return v * 2.0 + 1.0; })(src, scaled);
    check(near(scaled.params.num(sg::keys::x), 7.0), "swizzle_scaled maps the value");
}

void test_functor_roundtrip() {
    sg::StateGraph g;
    auto& flat = g.add<sg::Spatial2D>("f2");
    auto& deep = g.add<sg::Spatial3D>("f3");
    flat.sprite("p", 3, 4).params.set(sg::keys::z, 7.0);
    deep.mesh("p", 0, 0, 0);

    sg::Functor& lift = g.add_functor("lift", "f2", "f3");
    lift.on_object("p", "p", sg::transport::copy_all)
        .on_object("camera", "camera")
        .on_morphism("move.p", "move.p");
    sg::Functor& drop = g.add_functor("drop", "f3", "f2");
    drop.on_object("p", "p", sg::transport::copy_all)
        .on_object("camera", "camera")
        .on_morphism("move.p", "move.p");

    check(lift.check_laws(flat, deep).empty(), "lift satisfies the functor laws");
    check(drop.check_laws(deep, flat).empty(), "drop satisfies the functor laws");

    sg::Adjunction adj("lift -| drop", &lift, &drop);
    check(adj.is_isomorphism(flat, deep), "object maps are mutually inverse");

    sg::Spatial3D s3("s3");
    sg::Spatial2D s2("s2");
    check(adj.data_defects(flat, s3, s2).empty(), "parameters survive the round trip");

    sg::Functor lossy("lossy", "f3", "f2");
    lossy.on_object("p", "p", sg::transport::only({sg::keys::x, sg::keys::y}));
    sg::Adjunction lossy_adj("lift -| lossy", &lift, &lossy);
    sg::Spatial3D s3b("s3b");
    sg::Spatial2D s2b("s2b");
    check(!lossy_adj.data_defects(flat, s3b, s2b).empty(), "a lossy transport is reported");

    g.connect("f2", "toggle", "f3", "lift");
    g.set_initial("f2");
    sg::Engine e(g);
    e.start();
    e.fire("toggle");
    e.tick(0.0);
    check(e.current()->id() == sg::Key{"f3"}, "transition taken");
    check(near(deep.element("p").params.num(sg::keys::z), 7.0),
          "the functor carried z across the transition");
}

void test_functor_composition() {
    sg::State a("a"), b("b"), c("c");
    a.add_element("x", "n").params.set("v", 2.0);
    b.add_element("y", "n");
    c.add_element("z", "n");

    sg::Functor f("f", "a", "b");
    f.on_object("x", "y", [](const sg::Element& s, sg::Element& d) {
        d.params.set("v", s.params.num("v") + 1.0);
    });
    sg::Functor g2("g", "b", "c");
    g2.on_object("y", "z", [](const sg::Element& s, sg::Element& d) {
        d.params.set("v", s.params.num("v") * 10.0);
    });

    const sg::Functor gf = g2 * f;  // g after f
    check(gf.from() == sg::Key{"a"} && gf.to() == sg::Key{"c"}, "composite endpoints");
    gf.apply(a, c);
    check(near(c.element("z").params.num("v"), 30.0), "composite runs f then g");

    bool threw = false;
    try {
        (void)(f * g2);
    } catch (const std::exception&) {
        threw = true;
    }
    check(threw, "ill-typed functor composition is rejected");

    sg::Functor id = sg::Functor::identity(a);
    sg::State a2("a2");
    a2.add_element("x", "n");
    id.apply(a, a2);
    check(near(a2.element("x").params.num("v"), 2.0), "identity functor copies through");
}

void test_lens() {
    sg::StateGraph g;
    auto& room = g.add<sg::Spatial3D>("room");
    auto& map = g.add<sg::Spatial2D>("map");
    room.mesh("crate", 4, 0, 6);
    map.sprite("crate_tok", 0, 0);

    // One declaration for both directions of an editable view.
    g.add_lens("collapse", "stamp", "room", "map", {{"crate", "crate_tok"}},
               sg::transport::swizzle({{sg::keys::x, sg::keys::x}, {sg::keys::y, sg::keys::z}}),
               sg::transport::swizzle({{sg::keys::x, sg::keys::x}, {sg::keys::z, sg::keys::y}}));

    g.functor("collapse")->apply(room, map);
    check(near(map.element("crate_tok").params.num(sg::keys::y), 6.0),
          "the lens collapsed z onto the map's y");
    map.element("crate_tok").params.set(sg::keys::y, 2.0);
    g.functor("stamp")->apply(map, room);
    check(near(room.element("crate").params.num(sg::keys::z), 2.0), "and stamped it back");
    check(near(room.element("crate").params.num(sg::keys::y), 0.0), "height untouched");
}

// --- embeddings ---------------------------------------------------------------
void test_embedding() {
    sg::StateGraph g;
    auto& world = g.add<sg::Spatial3D>("world");
    auto& map = g.add<sg::Spatial2D>("map");
    world.mesh("rock", 1, 5, 2);
    world.portal("table", {0, 2, 0}, 2.0, 1.5);
    map.sprite("rock_token", 0, 0);

    g.add_lens("collapse", "stamp", "world", "map", {{"rock", "rock_token"}},
               sg::transport::swizzle({{sg::keys::x, sg::keys::x}, {sg::keys::y, sg::keys::z}}),
               sg::transport::swizzle({{sg::keys::x, sg::keys::x}, {sg::keys::z, sg::keys::y}}));
    g.embed("map_portal", "world", "table", "map", "collapse", "stamp", sg::EmbedSync::Commit);
    g.set_initial("world");
    check(g.validate().empty(), "graph with an embedding validates");

    sg::Engine e(g);
    e.start();
    e.open_embed("map_portal");
    check(e.embed_open("map_portal"), "portal is open");
    check(world.element("table").params.get_or<bool>(sg::keys::open, false),
          "portal element is flagged");
    check(near(map.element("rock_token").params.num(sg::keys::y), 2.0),
          "the view collapsed world z onto map y");
    check(e.focused() != nullptr && e.focused()->id() == sg::Key{"map"}, "the guest holds focus");

    map.element("rock_token").params.set(sg::keys::x, 9.0).set(sg::keys::y, 4.0);
    e.tick(0.0);
    check(near(world.element("rock").params.num(sg::keys::x), 1.0),
          "Commit sync does not write back mid-frame");

    e.close_embed("map_portal");
    const sg::Element& rock = world.element("rock");
    check(near(rock.params.num(sg::keys::x), 9.0), "close committed x");
    check(near(rock.params.num(sg::keys::z), 4.0), "close committed z");
    check(near(rock.params.num(sg::keys::y), 5.0), "height untouched");
    check(e.focused() == nullptr, "focus returns to the host");
    check(e.current()->id() == sg::Key{"world"}, "the host was never left");

    e.open_embed("map_portal");
    map.element("rock_token").params.set(sg::keys::x, 0.0);
    e.close_embed("map_portal", /*commit=*/false);
    check(near(world.element("rock").params.num(sg::keys::x), 9.0),
          "discarded edits stay in the guest");

    g.set_sync("map_portal", sg::EmbedSync::Live);
    e.open_embed("map_portal");
    map.element("rock_token").params.set(sg::keys::x, 3.0);
    e.tick(0.0);
    check(near(world.element("rock").params.num(sg::keys::x), 3.0),
          "Live sync writes back each frame");

    // Input reaches the focused guest, not the host.
    int seen = 0;
    map.bus().subscribe("ping", [&seen](const sg::Event&) { ++seen; });
    e.fire("ping");
    e.tick(0.0);
    check(seen == 1, "input is routed to the focused guest");
}

// Whether a guest is open, and whether it takes input, is its host's: the
// portal says it, so a host put back puts back what is open in it.
void test_the_host_says_what_is_open() {
    sg::StateGraph g;
    auto& room = g.add<sg::Spatial3D>("room");
    auto& map = g.add<sg::Spatial2D>("map");
    auto& note = g.add<sg::Spatial2D>("note");
    room.portal("table", {0, 1, 0}, 1.0, 1.0);
    g.embed("map_portal", "room", "table", "map", sg::Key{}, sg::Key{}, sg::EmbedSync::Commit);
    // Two at one portal - a thing that wears one picture and holds another -
    // open apart.
    g.set_focus(g.embed("note_portal", "room", "table", "note", sg::Key{}, sg::Key{}, sg::EmbedSync::Commit).name, false);
    g.set_initial("room");
    sg::Engine e(g);
    e.start();

    const sg::State::Snapshot shut = room.snapshot();
    e.open_embed("map_portal");
    check(e.embed_open("map_portal") && !e.embed_open("note_portal"), "two at one portal open apart");
    check(e.focused() == &map, "the opened guest holds focus");
    const sg::State::Snapshot open = room.snapshot();

    e.close_embed("map_portal", false);
    room.restore(open);
    e.tick(0.0);
    check(e.embed_open("map_portal"), "the host put back as it was open, it is open again");
    check(e.focused() == &map, "and focused again, as its portal says");
    check(!e.embed_open("note_portal"), "what the portal says nothing of is left as it is");

    e.focus_embed("map_portal", false);
    check(e.focused() == nullptr, "focus taken away");
    check(g.embedding("map_portal")->focus, "what was declared stays as declared");
    room.restore(open);
    e.tick(0.0);
    check(e.focused() == &map, "the portal said focused, and is put back so");

    room.restore(shut);
    e.tick(0.0);
    check(e.embed_open("map_portal"), "a portal that never said leaves the guest as it is");
    e.close_embed("map_portal", false);
    const sg::State::Snapshot closed = room.snapshot();
    e.open_embed("map_portal");
    room.restore(closed);
    e.tick(0.0);
    check(!e.embed_open("map_portal"), "the host put back as it was shut, it is shut");

    e.open_embed("note_portal");
    check(e.embed_open("note_portal") && e.focused() != &note, "a guest declared without focus takes none");
    e.close_embed("note_portal", false);  // the portal has said it shut
    g.keep_default(sg::Key{"room"});
    e.open_embed("note_portal");
    g.restore_default(sg::Key{"room"});
    e.tick(0.0);
    check(!e.embed_open("note_portal"), "back to its start, the room shows what it showed then");
}

// --- anchored groups ---------------------------------------------------------------
void test_anchors() {
    sg::Spatial3D w("w");
    w.anchor("annex", {10.0, 0.0, 4.0});
    sg::Element& wall = w.wall("a_west", {0.0, 0.0, 2.0}, 0.3, 3.0, 4.0);
    sg::attach_to(wall, "annex");

    sg::Pose p = sg::world_pose(w, wall);
    check(roughly(p.position.x, 10.0) && roughly(p.position.z, 6.0),
          "an anchored element is placed in its anchor's frame");

    // Move the anchor: the whole group moves, with no edit to its parts.
    w.element("annex").params.set(sg::keys::z, 9.0);
    p = sg::world_pose(w, wall);
    check(roughly(p.position.z, 11.0), "moving the anchor moves the group");

    // Rotate the anchor: local +x swings round to world +z.
    w.element("annex").params.set(sg::keys::z, 4.0).set(sg::keys::yaw, 1.5707963);
    sg::Element& probe = w.wall("probe", {2.0, 0.0, 0.0}, 1, 1, 1);
    sg::attach_to(probe, "annex");
    p = sg::world_pose(w, probe);
    check(roughly(p.position.x, 10.0) && roughly(p.position.z, 6.0),
          "an anchor's yaw rotates the group around it");
    check(roughly(p.yaw, 1.5707963), "and carries the heading");

    // Unparented elements are untouched, and a missing parent is not fatal.
    sg::Element& loose = w.mesh("loose", 3.0, 0.0, 3.0);
    check(roughly(sg::world_pose(w, loose).position.x, 3.0), "unanchored elements stay put");
    sg::attach_to(loose, "nowhere");
    check(roughly(sg::world_pose(w, loose).position.x, 3.0), "a missing anchor is ignored");
}

void test_camera_queries_follow_anchors() {
    sg::Spatial3D w("w");
    w.anchor("annex", {14.0, 0.0, 2.5});
    // A panel hung inside the group: its stored pose is local to the anchor.
    sg::attach_to(w.portal("panel", {7.75, 1.8, 4.5}, 2.6, 2.0, -1.5707963), "annex");

    sg::Element& cam = w.camera();
    sg::set_position(cam, {19.6, 1.7, 7.0});  // in front of where it really is
    cam.params.set(sg::keys::yaw, 0.0);
    check(sg::distance_to(w, "panel") < 3.0, "distance_to uses the anchored world pose");
    check(sg::looking_at(w, "panel", 3.2, 0.5), "and so an anchored panel can be used");

    // Standing where its *local* coordinates would put it reaches nothing.
    sg::set_position(cam, {5.6, 1.7, 4.5});
    check(!sg::looking_at(w, "panel", 3.2, 0.5), "its local pose is not a place in the world");

    // Move the group: the panel comes with it, and so does its reachability.
    w.element("annex").params.set(sg::keys::x, 4.0);
    sg::set_position(cam, {9.6, 1.7, 7.0});
    cam.params.set(sg::keys::yaw, 0.0);
    check(sg::looking_at(w, "panel", 3.2, 0.5), "moving the group moves what you can reach");
}

void test_wall_collisions() {
    sg::Spatial3D w("w");
    // A wall across x = 10, with a gap at z 4..6 and a lintel over it.
    w.wall("left", {10.0, 0.0, 2.0}, 0.4, 3.0, 4.0);
    w.wall("right", {10.0, 0.0, 8.0}, 0.4, 3.0, 4.0);
    w.wall("lintel", {10.0, 2.2, 5.0}, 0.4, 0.8, 2.0);  // base above head height

    sg::Element& cam = w.camera();
    cam.params.set(sg::keys::y, 1.7);

    // Into a solid part from the near side: pushed back out of it.
    sg::set_position(cam, {9.9, 1.7, 2.0});
    sg::resolve_wall_collisions(w, cam, 0.3);
    check(cam.params.num(sg::keys::x) <= 9.5 + 1e-9, "a solid wall pushes the walker back out");

    // From either side, the walker ends up clear of the wall's slab.
    sg::set_position(cam, {10.12, 1.7, 2.0});
    sg::resolve_wall_collisions(w, cam, 0.3);
    check(std::fabs(cam.params.num(sg::keys::x) - 10.0) >= 0.5 - 1e-9,
          "and is never left standing inside one");

    // Through the gap: nothing touches them, lintel included.
    sg::set_position(cam, {10.0, 1.7, 5.0});
    sg::resolve_wall_collisions(w, cam, 0.3);
    check(roughly(cam.params.num(sg::keys::x), 10.0) && roughly(cam.params.num(sg::keys::z), 5.0),
          "a doorway with a lintel over it can be walked through");

    // Walk the whole way through, a step at a time.
    sg::set_position(cam, {8.5, 1.7, 5.0});
    for (int i = 0; i < 20; ++i) {
        cam.params.set(sg::keys::x, cam.params.num(sg::keys::x) + 0.2);
        sg::resolve_wall_collisions(w, cam, 0.3);
    }
    check(cam.params.num(sg::keys::x) > 11.0, "and crossed to the far side");

    // Anchored walls move with their group, and still collide.
    sg::Spatial3D a("a");
    a.anchor("grp", {5.0, 0.0, 0.0});
    sg::attach_to(a.wall("slab", {0.0, 0.0, 0.0}, 1.0, 3.0, 6.0), "grp");
    sg::Element& probe = a.camera();
    probe.params.set(sg::keys::y, 1.7);
    sg::set_position(probe, {5.0, 1.7, 0.0});
    sg::resolve_wall_collisions(a, probe, 0.3);
    check(std::fabs(probe.params.num(sg::keys::x) - 5.0) > 0.7, "an anchored wall collides");

    a.element("grp").params.set(sg::keys::x, 20.0);
    sg::set_position(probe, {5.0, 1.7, 0.0});
    sg::resolve_wall_collisions(a, probe, 0.3);
    check(roughly(probe.params.num(sg::keys::x), 5.0),
          "and stops colliding once its group has moved away");
}

// --- portals between 3D states --------------------------------------------------
// --- the one convention -------------------------------------------------------
// The domain and the renderer sit on opposite sides of a layer boundary and
// cannot share a rotation, so this pins them together. Every sign error this
// engine has shipped would have failed here.
void test_one_rotation() {
    const double angles[] = {0.0, 0.7, 1.5707963, 3.14159265, -2.2, 5.9};
    for (double a : angles) {
        // A heading is what something with that yaw faces, and turning a unit
        // x by that yaw must produce it.
        const sg::Vec3d h = sg::heading(a);
        const sg::Vec3d turned = sg::rotate_xz({1, 0, 0}, a);
        check(roughly(h.x, turned.x) && roughly(h.z, turned.z),
              "rotating +x by a yaw gives that yaw's heading");

        // `across` is a quarter turn to the left of the heading, and the two
        // are a right-handed pair.
        const sg::Vec3d s = sg::across(a);
        check(roughly(h.x * s.x + h.z * s.z, 0.0), "across is square to the heading");
        check(roughly(s.x, sg::rotate_xz(h, 1.5707963265).x), "and is heading turned a quarter");

        // The renderer's matrix has to be that same rotation. It is written in
        // floats, on the other side of the engine, and nothing but this test
        // stops it drifting.
        const sg::gl::Mat4 m = sg::gl::Mat4::rotate_y(static_cast<float>(a));
        const sg::gl::Vec3 mx = m.transform_point({1, 0, 0});
        const sg::gl::Vec3 mz = m.transform_point({0, 0, 1});
        check(std::fabs(mx.x - h.x) < 1e-5 && std::fabs(mx.z - h.z) < 1e-5,
              "the render matrix turns +x to the same heading");
        check(std::fabs(mz.x - s.x) < 1e-5 && std::fabs(mz.z - s.z) < 1e-5,
              "and turns +z to the same side vector");

        // And the vector form the renderer uses for portal offsets agrees too.
        const sg::gl::Vec3 rotated = sg::gl::rotate_y({1, 0, 0}, static_cast<float>(a));
        check(std::fabs(rotated.x - h.x) < 1e-5 && std::fabs(rotated.z - h.z) < 1e-5,
              "as does rotating a vector directly");

        // A panel's pitch raises its face the way a camera's pitch raises its
        // gaze, and its roll turns it about that face: the renderer's panel
        // turn and the domain's forward_of have to agree on both.
        for (double p : {0.0, 0.4, 1.5707963, -0.9}) {
            sg::Element cam;
            cam.params.set(sg::keys::yaw, a).set(sg::keys::pitch, p);
            const sg::Vec3d f = sg::forward_of(cam);
            const sg::gl::Mat4 turn = sg::gl::Mat4::rotate_y(static_cast<float>(a)) *
                                      sg::gl::Mat4::rotate_z(static_cast<float>(p)) *
                                      sg::gl::Mat4::rotate_x(0.8f);
            const sg::gl::Vec3 face = turn.transform_point({1, 0, 0});
            check(std::fabs(face.x - f.x) < 1e-5 && std::fabs(face.y - f.y) < 1e-5 &&
                      std::fabs(face.z - f.z) < 1e-5,
                  "a pitched, rolled panel faces where a camera with that yaw and pitch looks");
        }
    }

    // Composing poses is that rotation too, not a second copy of it.
    const sg::Pose parent{{3.0, 0.0, 1.0}, 1.1};
    const sg::Pose local{{2.0, 0.0, -0.5}, 0.3};
    const sg::Pose composed = sg::compose_pose(parent, local);
    const sg::Vec3d by_hand = sg::rotate_xz(local.position, parent.yaw);
    check(roughly(composed.position.x, parent.position.x + by_hand.x) &&
              roughly(composed.position.z, parent.position.z + by_hand.z),
          "composing poses turns the child by the parent's yaw");
    check(roughly(composed.yaw, parent.yaw + local.yaw), "and adds the headings");
}

// Yaw is a heading: a doorway faces the way its yaw points, exactly as a
// camera does. One convention, used by the domain and the renderer alike.
sg::Vec3d facing(double yaw) { return sg::heading(yaw); }

void test_portal_transform() {
    sg::Spatial3D a("a"), b("b");
    // Two doorways facing different ways, in rooms with unrelated coordinates.
    a.portal("door_a", {14.0, 1.5, 6.0}, 2.4, 3.0, 3.14159265);  // faces -x
    b.portal("door_b", {4.5, 1.5, 0.0}, 2.4, 3.0, 1.5707963);    // faces +z

    // Standing 4m in front of door A - on the side it faces - looking at it.
    const sg::Vec3d here{10.0, 1.7, 6.0};
    const sg::Pose there = sg::through_portal(a.element("door_a"), b.element("door_b"), here, 0.0);

    // The claim is relational, not a pair of magic coordinates: you come out
    // behind the far doorway, measured against the way it faces, square in its
    // opening, pointed the way it points.
    const sg::Vec3d bp = sg::position_of(b.element("door_b"));
    const sg::Vec3d bn = facing(b.element("door_b").params.num(sg::keys::yaw));
    const double along = (there.position.x - bp.x) * bn.x + (there.position.z - bp.z) * bn.z;
    const double across = (there.position.x - bp.x) * -bn.z + (there.position.z - bp.z) * bn.x;
    check(roughly(along, -4.0), "you come out 4m behind the far doorway");
    check(roughly(across, 0.0), "square in the opening, not off to one side");
    const sg::Vec3d out = facing(there.yaw);
    check(roughly(out.x, bn.x) && roughly(out.z, bn.z), "facing the way that doorway faces");

    // The two directions are inverse: there and back is the identity.
    const sg::Pose back =
        sg::through_portal(b.element("door_b"), a.element("door_a"), there.position, there.yaw);
    check(roughly(back.position.x, here.x) && roughly(back.position.z, here.z),
          "carrying back through returns the original position");

    // Crossing is front-to-back through the opening only.
    const sg::Element& door = a.element("door_a");
    check(sg::crossed_portal(door, {13.0, 1.7, 6.0}, {14.5, 1.7, 6.0}), "walking in crosses");
    check(!sg::crossed_portal(door, {14.5, 1.7, 6.0}, {13.0, 1.7, 6.0}),
          "walking the other way does not");
    check(!sg::crossed_portal(door, {13.0, 1.7, 9.5}, {14.5, 1.7, 9.5}),
          "missing the opening does not");
}

// A doorway between two worlds at different heights carries height as height
// above the doorway, and the screen's glass keeps every corner of its picture.
void test_open_world() {
    sg::StateGraph g;
    auto& room = g.add<sg::Spatial3D>("room");
    auto& dunes = g.add<sg::Spatial3D>("dunes");
    room.portal("door", {5.0, 1.0, 6.0}, 0.9, 2.0, -1.5707963);
    dunes.portal("home", {100.0, 13.0, -40.0}, 0.9, 2.0, 0.6);
    sg::Element& cam = room.camera();
    cam.params.set(sg::keys::x, 5.2).set(sg::keys::y, 1.65).set(sg::keys::z, 5.0).set(sg::keys::yaw, 1.2);
    sg::Element out = dunes.camera();
    sg::portal_carry(room.element("door"), dunes.element("home"))(cam, out);
    check(roughly(out.params.num(sg::keys::y), 13.65), "a doorway carries height above it, not height");
    sg::Element back = room.camera();
    sg::portal_carry(dunes.element("home"), room.element("door"))(out, back);
    check(roughly(back.params.num(sg::keys::x), 5.2) && roughly(back.params.num(sg::keys::y), 1.65) &&
              roughly(back.params.num(sg::keys::z), 5.0),
          "and there and back again is where you started");

    // Walk the picture's edge: every point of it is seen through the glass,
    // inside its rounded corners.
    using G = sg::gl::CrtGlass;
    bool inside = true;
    for (int i = 0; i <= 200 && inside; ++i) {
        const double a = i / 200.0;
        // The panel point that shows picture point (a, 0), (a, 1), (0, a) or
        // (1, a), found by bisection along the ray from the middle.
        const double targets[4][2] = {{a, 0.0}, {a, 1.0}, {0.0, a}, {1.0, a}};
        for (const auto& t : targets) {
            double lo = 0, hi = 2;
            for (int k = 0; k < 60; ++k) {
                const double m = (lo + hi) * 0.5;
                double pu, pv;
                sg::gl::crt_picture(0.5 + (t[0] - 0.5) * m, 0.5 + (t[1] - 0.5) * m, pu, pv);
                const bool past = std::fabs(pu - 0.5) > std::fabs(t[0] - 0.5) + 1e-12 ||
                                  std::fabs(pv - 0.5) > std::fabs(t[1] - 0.5) + 1e-12;
                (past ? hi : lo) = m;
            }
            const double u = 0.5 + (t[0] - 0.5) * lo, v = 0.5 + (t[1] - 0.5) * lo;
            const double gx = std::fabs((u * 2 - 1) / G::half_w), gy = std::fabs((v * 2 - 1) / G::half_h);
            const double dx = std::max(gx - (1 - G::corner), 0.0), dy = std::max(gy - (1 - G::corner), 0.0);
            const double sd = std::sqrt(dx * dx + dy * dy) + std::min(std::max(gx, gy) - (1 - G::corner), 0.0) - G::corner;
            inside = inside && sd < 0 && u > 0 && u < 1 && v > 0 && v < 1;
        }
    }
    check(inside, "the whole picture on a screen shows inside its glass, corners and all");
    double pu, pv;
    check(sg::gl::crt_picture(0.5, 0.5, pu, pv) && roughly(pu, 0.5) && roughly(pv, 0.5),
          "and its middle is the picture's middle");
}

// Two rooms of the same kind joined by a doorway: the joint is a seam, and a
// seam is two-way, its round trips are the identity, and both rooms agree on
// the doorway and on anything hanging in it.
bool has_seam_violation(sg::StateGraph& g, const std::string& text) {
    for (const sg::Violation& v : sg::laws::seams(g))
        if (v.detail.find(text) != std::string::npos) return true;
    return false;
}

void test_seams() {
    sg::StateGraph g;
    auto& room = g.add<sg::Spatial3D>("room");
    auto& yard = g.add<sg::Spatial3D>("yard");
    room.portal("door", {5.0, 1.0, 6.0}, 0.9, 2.0, -1.5707963);
    yard.portal("gate", {40.0, 7.0, -3.0}, 0.9, 2.0, 0.4);
    room.camera().params.set(sg::keys::x, 5.0).set(sg::keys::y, 1.6).set(sg::keys::z, 4.0);

    // A window one way only: the kind of interface that cannot stand.
    g.add_functor("peek", "room", "yard")
        .on_object(sg::SpatialState::camera_id(), sg::SpatialState::camera_id(),
                   sg::portal_carry(room.element("door"), yard.element("gate")));
    g.embed("window", "room", "door", "yard", "peek", sg::Key{}, sg::EmbedSync::View);
    check(has_seam_violation(g, "one way"), "a one-way window between two rooms is a violation");

    // Glued as a seam, with a door hanging in the doorway on both sides.
    auto hang = [](sg::Spatial3D& s, sg::Vec3d at, double yaw) {
        s.anchor("leaf", at, yaw);
    };
    hang(room, {4.55, 0.0, 6.0}, -0.3);
    const sg::Element& door = room.element("door");
    const sg::Element& gate = yard.element("gate");
    sg::Element leaf_there = room.element("leaf");
    sg::pose_carry(door, gate)(room.element("leaf"), leaf_there);
    hang(yard, sg::position_of(leaf_there), leaf_there.params.num(sg::keys::yaw));
    sg::glue_doorway(g, "doorway", "room", "door", "yard", "gate", {{"leaf", "leaf"}});
    g.drop_embedding("window");  // the window, now the seam's way through
    g.embed("window", "room", "door", "yard", "doorway.ab", sg::Key{}, sg::EmbedSync::View);
    check(sg::laws::seams(g).empty(), "glued as a seam, the doorway is sound");
    for (const auto& v : sg::laws::seams(g)) std::printf("        %s\n", v.str().c_str());
    g.connect("room", "step_out", "yard", "doorway.ab");
    g.connect("yard", "step_in", "room", "doorway.ba");
    check(sg::laws::seams(g).empty(), "and walking through it both ways travels along it");

    // The door swings on one side only: the rooms disagree about the seam.
    room.element("leaf").params.set(sg::keys::yaw, -1.2);
    check(has_seam_violation(g, "yard.leaf.yaw"), "a door that swings on one side only is caught");
    room.element("leaf").params.set(sg::keys::yaw, -0.3);
    check(sg::laws::seams(g).empty(), "and swung back into agreement, it is sound again");

    // One doorway moved: its glue is made from where its two sides are when
    // it is used, so the doorway is glued where it now is - nothing is left
    // that could disagree with it.
    yard.element("gate").params.set(sg::keys::x, 41.0);
    {
        bool gate = false, leaf = false;
        for (const auto& v : sg::laws::seams(g))
            gate = gate || v.detail.find(".gate.") != std::string::npos, leaf = leaf || v.detail.find(".leaf.") != std::string::npos;
        check(!gate, "a doorway moved is glued where it now is: its glue cannot drift from it");
        check(leaf, "but a door left hanging where the doorway was is caught, on both sides");
    }
    yard.element("gate").params.set(sg::keys::x, 40.0);
    {
        // What must agree is the whole: three rooms in a ring, each doorway
        // back to back, close - go round and you are where you started. Move
        // one doorway on one side and every doorway is still sound, but the
        // ring no longer closes: round it you are moved on. That is a space
        // of its own (it winds), and the cover says which.
        sg::StateGraph r;
        sg::Atlas atlas;
        const char* names[3] = {"ring_a", "ring_b", "ring_c"};
        for (const char* n : names) {
            auto& s = r.add<sg::Spatial3D>(n);
            s.portal("next", {0, 1, 0}, 1, 2, 0.0);
            s.portal("prev", {0, 1, 0}, 1, 2, 3.14159265358979);
        }
        for (int i = 0; i < 3; ++i) atlas.glue(sg::Key{std::string("d") + std::to_string(i)}, names[i], "next", names[(i + 1) % 3], "prev");
        check(sg::descent_defects(atlas, r).empty(), "a ring of rooms that closes is one space");
        auto ring = sg::as_cover(atlas, r).monodromy(r);
        check(ring.size() == 1 && ring[0].trivial(), "and its one ring is trivial");
        r.state("ring_b").element("next").params.set(sg::keys::x, 2.0);
        check(sg::descent_defects(atlas, r).empty(), "a doorway moved on one side only is no defect: the ring is still an automorphism");
        ring = sg::as_cover(atlas, r).monodromy(r);
        for (const auto& m : ring) std::printf("        %s\n", m.str().c_str());
        check(ring.size() == 1 && ring[0].order == 0 && !ring[0].permutes && !ring[0].moves.empty(),
              "but round it you are moved on, and never come home: the cover names it");
        std::vector<std::string> why;
        check(sg::as_cover(atlas, r).sections(r, "ring_a", &why).empty() && !why.empty(), "and one chart cannot hold it");
        // Turned a quarter instead: four times round, home.
        r.state("ring_b").element("next").params.set(sg::keys::x, 0.0).set(sg::keys::yaw, 1.5707963267948966);
        ring = sg::as_cover(atlas, r).monodromy(r);
        for (const auto& m : ring) std::printf("        %s\n", m.str().c_str());
        check(ring.size() == 1 && ring[0].order == 4 && sg::descent_defects(atlas, r).empty(),
              "a doorway turned a quarter: round the ring four times, and you are home");
    }
    {
        // A torus: one square room, its east wall glued to its west and its
        // north to its south. Each seam joins the room to itself, and going
        // round either way brings you back moved by the room's width - that is
        // the space's shape, and the seams say so: they wrap.
        sg::StateGraph r;
        auto& room = r.add<sg::Spatial3D>("torus");
        room.portal("east", {8, 1, 4}, 8, 2, 3.14159265358979);
        room.portal("west", {0, 1, 4}, 8, 2, 0.0);
        room.portal("south", {4, 1, 8}, 8, 2, -1.5707963267948966);
        room.portal("north", {4, 1, 0}, 8, 2, 1.5707963267948966);
        sg::Atlas atlas;
        atlas.glue("ew", "torus", "east", "torus", "west").wraps = true;
        atlas.glue("ns", "torus", "south", "torus", "north").wraps = true;
        const auto defects = sg::descent_defects(atlas, r);
        for (const auto& d : defects) std::printf("        %s\n", d.c_str());
        check(defects.empty() && r.validate().empty(), "a torus: one room glued to itself both ways, its seams wrapping, is one space");
        {
            const auto ms = sg::as_cover(atlas, r).monodromy(r);
            bool shape = ms.size() == 2;
            for (const auto& m : ms) shape = shape && m.wraps && m.order == 0 && !m.permutes;
            check(shape, "and its two rings are its shape: each moves you on, for ever");
        }
        sg::Element cam = room.camera();
        cam.params.set(sg::keys::x, 7.9).set(sg::keys::y, 1.6).set(sg::keys::z, 4.0).set(sg::keys::yaw, 0.0);
        sg::Element out = cam;
        sg::portal_carry(room.element("east"), room.element("west"))(cam, out);
        check(std::fabs(out.params.num(sg::keys::x) - (-0.1)) < 1e-9 || std::fabs(out.params.num(sg::keys::x) - 0.1) < 1e-9,
              "walk out of its east wall and you come in at its west");

        sg::StateGraph bad;
        auto& one = bad.add<sg::Spatial3D>("one");
        one.portal("a", {8, 1, 4}, 8, 2, 3.14159265358979);
        one.portal("b", {0, 1, 4}, 8, 2, 0.0);
        sg::glue_doorway(bad, "self", "one", "a", "one", "b");
        bool refused = false;
        for (const auto& e : bad.validate()) refused = refused || e.find("wraps") != std::string::npos;
        check(refused, "but a room glued to itself without saying so is refused, and told why");
    }

    // A glue that reaches past the boundary, or leaves part of it unglued.
    {
        sg::Seam narrow = *g.seam("doorway");
        narrow.boundary_b = {"gate"};
        narrow.boundary_a = {"door"};
        sg::StateGraph& gg = g;
        const sg::Seam keep = *g.seam("doorway");
        gg.add_seam(narrow);
        check(has_seam_violation(g, "reaches past the boundary"), "a glue reaching past its boundary is caught");
        gg.add_seam(keep);
        room.anchor("stray", {1, 0, 1}, 0.0);
        sg::Seam wide = keep;
        wide.boundary_a.push_back("stray");
        wide.boundary_b.push_back("leaf");
        gg.add_seam(wide);
        check(has_seam_violation(g, "unglued"), "and so is a boundary the glue leaves out");
        gg.add_seam(keep);
    }

    // Travel that drags the doorway along with the traveller.
    sg::Functor* ab = g.functor("doorway.ab");
    ab->on_object("door", "gate", sg::seam_carry(door, gate));
    check(has_seam_violation(g, "part of the boundary"), "travel that writes the doorway is caught");
}

void test_view_portal() {
    sg::StateGraph g;
    auto& room = g.add<sg::Spatial3D>("room");
    auto& lab = g.add<sg::Spatial3D>("lab");
    room.portal("door_out", {14.0, 1.5, 6.0}, 2.4, 3.0, 3.14159265);  // faces -x
    lab.portal("door_in", {4.5, 1.5, 0.0}, 2.4, 3.0, 1.5707963);      // faces +z
    room.camera().params.set(sg::keys::x, 10.0).set(sg::keys::y, 1.7).set(sg::keys::z, 6.0);

    g.add_functor("peek", "room", "lab")
        .on_object(sg::SpatialState::camera_id(), sg::SpatialState::camera_id(),
                   sg::portal_carry(room.element("door_out"), lab.element("door_in")));
    g.embed("window", "room", "door_out", "lab", "peek", sg::Key{}, sg::EmbedSync::View);
    g.connect("room", "step_through", "lab", "peek");
    g.set_initial("room");
    check(g.validate().empty(), "a View portal validates");

    sg::Engine e(g);
    e.start();
    e.open_embed("window");
    e.tick(0.016);
    check(roughly(lab.camera().params.num(sg::keys::x), 4.5) &&
              roughly(lab.camera().params.num(sg::keys::z), -4.0),
          "View refreshes the guest camera every frame");

    // Move in the host: the guest's camera follows, with no edit coming back.
    room.camera().params.set(sg::keys::x, 12.0);
    lab.add_element("scribble", "mesh");
    e.tick(0.016);
    check(roughly(lab.camera().params.num(sg::keys::x), 4.5) &&
              roughly(lab.camera().params.num(sg::keys::z), -2.0),
          "the window tracks the viewer");
    check(room.find("scribble") == nullptr, "a View portal never writes back");

    // The same functor, taken as a transition, is walking through the doorway.
    e.fire("step_through");
    e.tick(0.016);
    check(e.current()->id() == sg::Key{"lab"}, "stepping through changes state");
    check(roughly(lab.camera().params.num(sg::keys::z), -2.0),
          "and carries the camera with it");
}

void test_view_portal_validation() {
    sg::StateGraph g;
    g.add<sg::Spatial3D>("a");
    g.add<sg::Spatial3D>("b");
    g.state("a").add_element("door", sg::kinds::portal);
    g.set_initial("a");
    g.embed("no_in", "a", "door", "b", sg::Key{}, sg::Key{}, sg::EmbedSync::View);
    const auto errors = g.validate();
    bool found = false;
    for (const auto& e : errors)
        if (e.find("needs an `in` functor") != std::string::npos) found = true;
    check(found, "a View portal without `in` is rejected");
}

// --- covers, descent, gluing -------------------------------------------------------
// A shift transport: the only data each state holds is one number, and the
// transitions add to it. Composites are then easy to reason about, which makes
// this the clearest way to test descent without any geometry in the way.
sg::Transport shift_by(double d) {
    return [d](const sg::Element& src, sg::Element& dst) {
        dst.params.set("v", src.params.num("v") + d);
    };
}

void link_shift(sg::StateGraph& g, sg::Cover& cover, sg::Key a, sg::Key b, double d) {
    const sg::Key fwd{a.str() + "->" + b.str()};
    const sg::Key back{b.str() + "->" + a.str()};
    g.set_functor(sg::Functor{fwd, a, b}).on_object("x", "x", shift_by(d));
    g.set_functor(sg::Functor{back, b, a}).on_object("x", "x", shift_by(-d));
    cover.add(sg::Key{a.str() + "^" + b.str()}, a, b, fwd, back);
}

void test_descent() {
    sg::StateGraph g;
    for (const char* id : {"u", "v", "w"}) {
        auto& s = g.add<sg::State>(sg::Key{id});
        s.add_element("x", "thing").params.set("v", 0.0);
    }
    g.set_initial("u");

    // A chain that agrees on its overlaps: across and back is the identity.
    sg::Cover chain;
    link_shift(g, chain, "u", "v", 3.0);
    link_shift(g, chain, "v", "w", 5.0);
    check(chain.descent_defects(g).empty(), "a cover whose overlaps agree has no descent defects");

    // Gluing: the composite transition from u expresses w's data in u's terms.
    const auto sections = chain.sections(g, "u");
    check(sections.size() == 3, "every piece is reached from the root");
    const sg::Functor* to_w = nullptr;
    for (const auto& sec : sections)
        if (sec.first == sg::Key{"w"}) to_w = &sec.second;
    sg::State probe("probe");
    if (to_w) to_w->apply(g.state("u"), probe);
    check(to_w && near(probe.element("x").params.num("v"), 8.0),
          "and the glued section is the composite of the steps");

    // Close the ring another way: u -> v -> w -> u, and 3 + 5 - 7 is not
    // zero. Round it, v is one more: that is monodromy - a space that winds,
    // not a fault - and the cover names it.
    sg::Cover bad_ring;
    link_shift(g, bad_ring, "u", "v", 3.0);
    link_shift(g, bad_ring, "v", "w", 5.0);
    link_shift(g, bad_ring, "w", "u", 7.0);
    check(bad_ring.descent_defects(g).empty(), "a ring that does not close is still a cover: each overlap agrees");
    const auto winds = bad_ring.monodromy(g);
    check(winds.size() == 1 && winds[0].order == 0 && !winds[0].moves.empty() && winds[0].moves[0].find("x.v") != std::string::npos,
          "and its ring is named, with what it moves");
    // But one chart cannot hold it: gluing asks that every ring close.
    std::vector<std::string> why;
    check(bad_ring.sections(g, "u", &why).empty() && !why.empty(),
          "a cover whose ring does not close glues to nothing, and says why");

    // Close it correctly and the seam goes away.
    sg::Cover good_ring;
    link_shift(g, good_ring, "u", "v", 3.0);
    link_shift(g, good_ring, "v", "w", 5.0);
    link_shift(g, good_ring, "w", "u", -8.0);
    check(good_ring.descent_defects(g).empty(), "a ring that closes up glues cleanly");
    check(good_ring.sections(g, "u").size() == 3, "and glues to a section over every piece");

    // A ring of six whose holonomy only shows all the way round: no budget on
    // loop length hides it.
    sg::StateGraph h;
    const char* six[] = {"p0", "p1", "p2", "p3", "p4", "p5"};
    for (const char* id : six) {
        h.add<sg::State>(sg::Key{id}).add_element("x", "thing").params.set("v", 0.0);
    }
    h.set_initial("p0");
    sg::Cover long_ring, long_chain;
    for (int i = 0; i < 6; ++i) link_shift(h, long_ring, six[i], six[(i + 1) % 6], 1.0);
    for (int i = 0; i < 5; ++i) link_shift(h, long_chain, six[i], six[i + 1], 1.0);
    const auto six_round = long_ring.monodromy(h);
    check(six_round.size() == 1 && !six_round[0].trivial() && long_ring.descent_defects(h).empty(),
          "a ring of six that does not close is named, however long");
    const auto far = long_chain.sections(h, "p0");
    const sg::Functor* to_p5 = nullptr;
    for (const auto& sec : far)
        if (sec.first == sg::Key{"p5"}) to_p5 = &sec.second;
    sg::State probe5("probe5");
    if (to_p5) to_p5->apply(h.state("p0"), probe5);
    check(far.size() == 6 && to_p5 && near(probe5.element("x").params.num("v"), 5.0),
          "and a section reaches every piece, however far from the root");
}

// A ring that is not the identity is a space of its own shape, and the cover
// names it: two pieces joined twice, the second join carrying things by some
// map and back by its inverse, so each overlap agrees and only the ring
// does not close.
using Carry = std::function<void(const sg::Element&, sg::Element&)>;
std::vector<sg::Monodromy> twice_joined(std::vector<std::pair<sg::Key, sg::Key>> objects, Carry there, Carry back) {
    sg::StateGraph g;
    for (const char* id : {"ha", "hb"}) {
        auto& s = g.add<sg::State>(sg::Key{id});
        s.add_element("p", "thing").params.set("v", 1.0).set("w", 0.0);
        s.add_element("q", "thing").params.set("v", 2.0).set("w", 0.0);
    }
    auto& same = g.set_functor(sg::Functor{"ha->hb", "ha", "hb"});
    auto& home = g.set_functor(sg::Functor{"hb->ha", "hb", "ha"});
    auto& out = g.set_functor(sg::Functor{"ha=>hb", "ha", "hb"});
    auto& in = g.set_functor(sg::Functor{"hb=>ha", "hb", "ha"});
    for (sg::Key k : {sg::Key{"p"}, sg::Key{"q"}}) same.on_object(k, k), home.on_object(k, k);
    for (const auto& [a, b] : objects) out.on_object(a, b, there), in.on_object(b, a, back);
    sg::Cover cover;
    cover.add("first", "ha", "hb", "ha->hb", "hb->ha");
    cover.add("second", "ha", "hb", "ha=>hb", "hb=>ha");
    for (const auto& d : cover.descent_defects(g)) std::printf("        %s\n", d.c_str());
    check(cover.descent_defects(g).empty(), "each overlap agrees, so the ring is an automorphism");
    const auto m = cover.monodromy(g);
    for (const auto& r : m) std::printf("        %s\n", r.str().c_str());
    return m;
}

void test_monodromy() {
    const std::vector<std::pair<sg::Key, sg::Key>> each{{"p", "p"}, {"q", "q"}}, swapped{{"p", "q"}, {"q", "p"}};
    const Carry as_is = [](const sg::Element& s, sg::Element& d) { d.params = s.params; };
    const auto turned = [](double by) {
        return Carry([by](const sg::Element& s, sg::Element& d) {
            d.params = s.params;
            d.params.set("w", std::remainder(s.params.num("w") + by, 6.283185307179586));
        });
    };
    auto m = twice_joined(each, as_is, as_is);
    check(m.size() == 1 && m[0].trivial(), "joined twice the same way: the ring closes");

    m = twice_joined(each, turned(1.5707963267948966), turned(-1.5707963267948966));
    check(m[0].order == 4 && !m[0].permutes, "a heading turned a quarter round it: four times round, home");

    m = twice_joined(each, [](const sg::Element& s, sg::Element& d) { d.params = s.params; d.params.set("v", -s.params.num("v")); },
                     [](const sg::Element& s, sg::Element& d) { d.params = s.params; d.params.set("v", -s.params.num("v")); });
    check(m[0].order == 2, "reflected round it: twice round, home");

    m = twice_joined(each, [](const sg::Element& s, sg::Element& d) { d.params = s.params; d.params.set("v", s.params.num("v") + 0.25); },
                     [](const sg::Element& s, sg::Element& d) { d.params = s.params; d.params.set("v", s.params.num("v") - 0.25); });
    check(m[0].order == 0 && !m[0].moves.empty(), "moved on round it: never home, and what moves is named");

    m = twice_joined(swapped, as_is, as_is);
    check(m[0].permutes && m[0].order == 2, "p and q change places round it: the space itself goes round, and twice is home");
}

// A round trip that drops a parameter outright has lost it, as surely as one
// that changes it.
void test_lossless_counts_what_is_dropped() {
    sg::StateGraph g;
    auto& a = g.add<sg::State>("a");
    g.add<sg::State>("b");
    a.add_element("x", "n").params.set("v", 2.0).set("w", 3.0);
    g.set_initial("a");
    g.add_functor("there", "a", "b").on_object("x", "y");
    g.add_functor("back", "b", "a").on_object("y", "x");
    g.add_functor("there_v", "a", "b").on_object("x", "y", sg::transport::only({"v"}));
    check(sg::is_lossless(g, sg::Functor::compose(*g.functor("there"), *g.functor("back"))),
          "a round trip that carries everything is lossless");
    check(!sg::is_lossless(g, sg::Functor::compose(*g.functor("there_v"), *g.functor("back"))),
          "one that drops a parameter is not");
    g.add_functor("back_z", "b", "a").on_object("y", "z");
    check(!sg::is_lossless(g, sg::Functor::compose(*g.functor("there"), *g.functor("back_z"))),
          "nor one that brings an object back as another");
}

void test_atlas_is_a_cover() {
    sg::StateGraph g;
    auto& hall = g.add<sg::Spatial3D>("hall");
    auto& annex = g.add<sg::Spatial3D>("annex");
    hall.portal("door", {14.0, 1.5, 7.0}, 2.8, 3.0, 3.14159265);  // faces -x
    annex.portal("door", {4.5, 1.5, 0.0}, 2.8, 3.0, 1.5707963);   // faces +z
    g.set_initial("hall");

    sg::Atlas atlas;
    atlas.glue("doorway", "hall", "door", "annex", "door");
    check(sg::descent_defects(atlas, g).empty(), "a doorway glues two rooms without a seam");

    // The doorway's transition is derived from the portals, so moving one and
    // re-asking gives a consistent answer rather than a stale one.
    sg::Pose before, after;
    atlas.placement(g, "hall", "annex", before);
    hall.element("door").params.set(sg::keys::z, 3.0);
    atlas.placement(g, "hall", "annex", after);
    check(roughly(after.position.z - before.position.z, -4.0),
          "moving a doorway moves the room it joins");
    check(sg::descent_defects(atlas, g).empty(), "and the gluing is still seamless afterwards");

    // Seen from the annex, it is the hall that has moved: the same fact, told
    // from the other side.
    sg::Pose from_annex;
    check(atlas.placement(g, "annex", "hall", from_annex), "the relation reads both ways");
    const sg::Pose round_trip = sg::compose_pose(after, from_annex);
    check(roughly(round_trip.position.x, 0.0) && roughly(round_trip.position.z, 0.0),
          "and the two readings are inverse");
}

// --- the guards themselves ----------------------------------------------------------
// Each of these is a mistake this engine actually shipped, turned into
// something that fails before it can be shipped again.
void test_guards_against_past_mistakes() {
    // A doorway whose transition writes the far side's portal moves the room
    // you are walking into. It used to; now it is named.
    sg::StateGraph g;
    auto& hall = g.add<sg::Spatial3D>("hall");
    auto& annex = g.add<sg::Spatial3D>("annex");
    hall.portal("door", {14.0, 1.5, 7.0}, 2.8, 3.0, 3.14159265);
    annex.portal("door", {4.5, 1.5, 0.0}, 2.8, 3.0, 1.5707963);
    g.set_initial("hall");

    sg::Atlas atlas;
    atlas.glue("doorway", "hall", "door", "annex", "door");
    check(sg::descent_defects(atlas, g).empty(), "a plain doorway is clean");

    // Registering the doorway's transitions by hand, with the portal in the
    // object map, as we once did. Deriving them makes this unrepresentable;
    // a hand-written cover still has to be told.
    sg::Cover by_hand;
    g.set_functor(sg::Functor{"hand.ab", "hall", "annex"})
        .on_object(sg::SpatialState::camera_id(), sg::SpatialState::camera_id(),
                   sg::portal_carry(hall.element("door"), annex.element("door")))
        .on_object("door", "door", sg::portal_carry(hall.element("door"), annex.element("door")));
    g.set_functor(sg::Functor{"hand.ba", "annex", "hall"})
        .on_object(sg::SpatialState::camera_id(), sg::SpatialState::camera_id(),
                   sg::portal_carry(annex.element("door"), hall.element("door")));
    by_hand.add("hand", "hall", "annex", "hand.ab", "hand.ba");
    bool named = false;
    for (const auto& d : sg::travel_defects(by_hand, g))
        if (d.find("not a traveller") != std::string::npos) named = true;
    check(named, "a transition that writes the far doorway is refused");
    check(sg::travel_defects(sg::as_cover(atlas, g), g).empty(),
          "and a derived one cannot contain the mistake at all");

    // A portal element that is not a portal at all.
    sg::StateGraph g2;
    auto& a2 = g2.add<sg::Spatial3D>("a");
    auto& b2 = g2.add<sg::Spatial3D>("b");
    a2.add_element("door", sg::kinds::mesh);
    b2.portal("door", {0, 1.5, 0}, 2.0, 3.0, 0.0);
    g2.set_initial("a");
    sg::Atlas atlas2;
    atlas2.glue("doorway", "a", "door", "b", "door");
    bool kind_named = false;
    for (const auto& d : sg::descent_defects(atlas2, g2))
        if (d.find("not a portal element") != std::string::npos) kind_named = true;
    check(kind_named, "a doorway hung on something that is not a portal is refused");

    // A doorway naming a room that does not exist.
    sg::StateGraph g3;
    g3.add<sg::Spatial3D>("only");
    g3.set_initial("only");
    sg::Atlas atlas3;
    atlas3.glue("doorway", "only", "door", "nowhere", "door");
    bool room_named = false;
    for (const auto& d : sg::descent_defects(atlas3, g3))
        if (d.find("unknown room") != std::string::npos) room_named = true;
    check(room_named, "a doorway onto a room that does not exist is refused");
}

// --- the engine without a domain ------------------------------------------------
// Nothing above this point in the engine knows what a room is. This is the
// check that says so: the same states, functors, lenses, covers and laws,
// applied to a library's stock. If any of the machinery had quietly grown a
// dependency on geometry, none of this would compile, let alone pass.
void test_any_domain() {
    sg::StateGraph g;

    auto& library = g.add<sg::State>("library");
    library.add_element("dune", "book")
        .params.set("title", std::string("Dune"))
        .set("copies", int64_t{30});
    library.add_element("ubik", "book")
        .params.set("title", std::string("Ubik"))
        .set("copies", int64_t{7});
    library.add_element("desk", sg::kinds::portal);

    // A desk view that counts in dozens: lossy on purpose, like every summary.
    auto& desk = g.add<sg::State>("desk");
    desk.add_element("dune_row", "row");
    desk.add_element("ubik_row", "row");

    g.add_lens("to_desk", "to_shelf", "library", "desk",
               {{"dune", "dune_row"}, {"ubik", "ubik_row"}},
               sg::transport::then(sg::transport::only({"title"}),
                                   sg::transport::swizzle_scaled({{"dozens", "copies"}},
                                                                 [](double c) {
                                                                     return std::floor(c / 12.0);
                                                                 })),
               sg::transport::swizzle_scaled({{"copies", "dozens"}},
                                             [](double d) { return d * 12.0; }));
    g.embed("desk", "library", "desk", "desk", "to_desk", "to_shelf", sg::EmbedSync::Commit);
    g.set_initial("library");

    check(g.validate().empty(), "a graph about books validates like any other");
    check(sg::interface_defects(g).empty(),
          "a summary that rounds to dozens settles, so it is a lawful view");
    // Lawful is not the same as lossless, and the engine keeps the two apart.
    check(!sg::is_lossless(g, sg::Functor::compose(*g.functor("to_desk"), *g.functor("to_shelf"))),
          "and it is honest about being a projection, not an isomorphism");

    sg::Engine e(g);
    e.start();
    e.open_embed("desk");
    check(desk.element("dune_row").params.get_or<std::string>("title", "") == "Dune",
          "the view carries what it was asked to carry");
    check(near(desk.element("dune_row").params.num("dozens"), 2.0), "and counts in dozens");

    // Edit the summary, commit, and the shelf follows - in units again.
    desk.element("dune_row").params.set("dozens", 4.0);
    e.close_embed("desk");
    check(near(library.element("dune").params.num("copies"), 48.0),
          "an edit written back arrives in the subject's own terms");

    // The same lens, made to drift: every open-and-close would add stock.
    sg::StateGraph bad;
    auto& shelf = bad.add<sg::State>("shelf");
    shelf.add_element("dune", "book").params.set("copies", int64_t{30});
    shelf.add_element("desk", sg::kinds::portal);
    auto& sheet = bad.add<sg::State>("sheet");
    sheet.add_element("row", "row");
    bad.add_lens("show", "put", "shelf", "sheet", {{"dune", "row"}},
                 sg::transport::swizzle_scaled({{"n", "copies"}},
                                               [](double c) { return c / 12.0; }),
                 sg::transport::swizzle_scaled({{"copies", "n"}},
                                               [](double n) { return n * 12.0 + 1.0; }));
    bad.embed("sheet", "shelf", "desk", "sheet", "show", "put", sg::EmbedSync::Commit);
    bad.set_initial("shelf");
    bool drift_named = false;
    for (const auto& d : sg::interface_defects(bad))
        if (d.find("keeps moving") != std::string::npos) drift_named = true;
    check(drift_named, "a view that never settles is named, in any domain");

    // And gluing: two catalogues that agree about the books they share.
    sg::StateGraph cat;
    for (const char* id : {"west", "east"}) {
        auto& wing = cat.add<sg::State>(sg::Key{id});
        wing.add_element("shared", "book").params.set("copies", int64_t{12});
    }
    cat.set_initial("west");
    cat.set_functor(sg::Functor{"w2e", "west", "east"})
        .on_object("shared", "shared", sg::transport::copy_all);
    cat.set_functor(sg::Functor{"e2w", "east", "west"})
        .on_object("shared", "shared", sg::transport::copy_all);
    sg::Cover shelves;
    shelves.add("shared_stock", "west", "east", "w2e", "e2w");
    check(shelves.descent_defects(cat).empty(),
          "two catalogues that agree on their overlap glue");

    // Make one side disagree and the same check names the seam.
    cat.set_functor(sg::Functor{"e2w", "east", "west"})
        .on_object("shared", "shared",
                   sg::transport::swizzle_scaled({{"copies", "copies"}},
                                                 [](double c) { return c + 1.0; }));
    check(!shelves.descent_defects(cat).empty(), "and disagreement is a seam, not a silent merge");
}

void test_graph_analysis() {
    sg::StateGraph g;
    g.add<sg::Spatial2D>("start");
    g.add<sg::Spatial2D>("island");
    g.set_initial("start");
    check(g.validate().size() == 1, "unreachable state is reported");
    g.connect("start", "go", "island");
    check(g.validate().empty(), "reachable once connected");
    g.connect("start", "nowhere", "ghost");
    check(!g.validate().empty(), "unknown transition target is reported");
    check(g.to_dot(false).find("digraph") == 0, "dot export produced");
}

// --- glued rooms are adjacent, never overlapping -------------------------------------
// A wall centred on the glue plane puts half of itself in the neighbour; each
// room then draws the same surface in its own look, and one bleeds through the
// other. The gluing law names it; standing each wall on its own side clears it.
void test_rooms_are_adjacent() {
    const auto build = [](double hall_wall_x, double annex_wall_z, sg::StateGraph& g,
                          sg::Atlas& atlas) {
        auto& hall = g.add<sg::Spatial3D>("hall");
        auto& annex = g.add<sg::Spatial3D>("annex");
        hall.portal("door", {14.0, 1.5, 7.0}, 2.8, 3.0, 3.14159265358979);  // faces -x, into the hall
        annex.portal("door", {4.5, 1.5, 0.0}, 2.8, 3.0, 1.5707963267949);   // faces +z, into the annex
        hall.wall("east", {hall_wall_x, 0.0, 3.0}, 0.3, 4.0, 5.6);
        annex.wall("south", {2.0, 0.0, annex_wall_z}, 3.0, 3.6, 0.3);
        g.set_initial("hall");
        atlas.glue("doorway", "hall", "door", "annex", "door");
    };

    sg::StateGraph centred;
    sg::Atlas a1;
    build(14.0, 0.0, centred, a1);
    const auto named = sg::adjacency_defects(a1, centred);
    for (const auto& d : named) std::printf("        %s\n", d.c_str());
    check(named.size() == 2, "walls centred on the glue plane are both named");
    check(!named.empty() && named[0].find("0.150000 m past doorway") != std::string::npos,
          "with how far each reaches into the other room");

    sg::StateGraph adjacent;
    sg::Atlas a2;
    build(13.85, 0.15, adjacent, a2);
    check(sg::adjacency_defects(a2, adjacent).empty() && sg::descent_defects(a2, adjacent).empty(),
          "walls standing on their own side meet at the plane, and glue");

    // The same boundary, as the renderer's clip: a room's own side of its doorway.
    const sg::HalfSpace side = sg::room_side(adjacent.state("hall"),
                                             adjacent.state("hall").element("door"), sg::Pose{});
    check(side.at({13.0, 0.0, 7.0}) > 0.0 && side.at({15.0, 0.0, 7.0}) < 0.0 &&
              near(side.at({14.0, 0.0, 3.0}), 0.0),
          "a room owns the half-space its doorway faces into, bounded by the plane");
}

// --- looks: how a state is shown, as states ------------------------------------------
void test_text_and_store() {
    // A state written out and read back is the same state.
    sg::Spatial3D a("room a");
    sg::Element& m = a.mesh("desk top", 1.5, 0.75, -2.0);
    m.params.set("note", std::string("line one\nline two\twith a tab \ and a slash"))
        .set("n", int64_t{42})
        .set("on", true)
        .set(sg::keys::sx, 1.0 / 3.0);
    a.params().set("far", 99.5);
    a.element(a.camera_id()).alive = false;
    const std::string src = sg::to_text(a);
    sg::Spatial3D b("room b");
    std::string why;
    check(sg::from_text(b, src, &why), "a state's text reads back" + (why.empty() ? "" : ": " + why));
    const sg::Element* bm = b.find(sg::Key{"desk top"});
    check(bm && bm->params.get_or<std::string>("note", "") == m.params.get_or<std::string>("note", "x") &&
              bm->params.num("n") == 42 && bm->params.get_or<bool>("on", false) &&
              bm->params.num(sg::keys::sx) == 1.0 / 3.0 && b.params().num("far") == 99.5,
          "every value comes back exactly: text, whole numbers, flags, numbers to the last bit");
    check(!b.element(b.camera_id()).alive, "and whether each element is there");
    check(sg::to_text(b).substr(sg::to_text(b).find('\n')) == src.substr(src.find('\n')),
          "and written out again, it is the same text");
    check(!sg::from_text(b, "element x mesh\n  k q:1\n", &why) && !why.empty(), "a line it cannot read is refused");
    {
        // Written again and again, element by element as they change: the
        // very text to_text writes, whatever changed in between.
        sg::StateText text;
        bool same = text(a) == sg::to_text(a);
        m.params.set(sg::keys::x, 2.5);
        same = same && text(a) == sg::to_text(a);
        a.element(a.camera_id()).alive = true;
        a.mesh("lamp", 0, 0, 0);
        same = same && text(a) == sg::to_text(a);
        a.remove_element(sg::Key{"lamp"});
        a.params().set("far", 12.0);
        same = same && text(a) == sg::to_text(a);
        const auto skip = [](const sg::Element& e) { return e.id != sg::Key{"desk top"}; };
        same = same && text(a, skip) == sg::to_text(a, skip);
        check(same, "a state's text kept element by element is the text written whole");
    }

    // Texts in files: read once, written only when changed, re-read when
    // changed from outside.
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("sg_store_" + std::to_string(std::rand()));
    sg::TextStore store;
    store.bind("p", dir / "p.txt", "first");
    check(*store.get("p") == "first" && fs::exists(dir / "p.txt"), "a text bound to no file yet is written there");
    const auto stamp = fs::last_write_time(dir / "p.txt");
    store.put("p", "first");
    check(fs::last_write_time(dir / "p.txt") == stamp, "put the same again, the file is not touched");
    check(store.poll(0.0).empty(), "nothing changed outside: nothing to read");
    {
        std::ofstream(dir / "p.txt", std::ios::trunc) << "edited by hand";
    }
    fs::last_write_time(dir / "p.txt", stamp + std::chrono::seconds(2));
    const auto changed = store.poll(1.0);
    check(changed.size() == 1 && *store.get("p") == "edited by hand", "edited outside, it is read again");
    sg::TextStore again;
    check(again.bind("p", dir / "p.txt", "first") == "edited by hand", "bound again, the file wins");
    fs::remove_all(dir);
}

void test_assets_a_folder_per_state() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / ("sg_assets_" + std::to_string(std::rand()));
    const sg::Assets assets(root);
    check(assets.folder("hall") == root / "hall" && fs::is_directory(root / "hall"), "a state's folder is named by the state, and made");
    check(assets.folder("hall") != assets.folder("vale"), "two states never share a folder");
    auto refused = [](auto&& f) {
        try {
            f();
        } catch (const std::invalid_argument&) {
            return true;
        }
        return false;
    };
    check(refused([&] { assets.folder("../hall"); }) && refused([&] { assets.folder("a/b"); }) && refused([&] { assets.folder(""); }),
          "an owner that is not one plain name is refused");
    check(refused([&] { assets.file("hall", "../vale/x"); }) && refused([&] { assets.file("hall", "/etc/x"); }),
          "a name that leaves the folder is refused: a state cannot reach another's files");
    sg::TextStore store;
    store.bind("p", assets, "hall", "papers/p.txt", "words");
    check(fs::exists(root / "hall" / "papers" / "p.txt"), "a text bound to an owner is kept in the owner's folder");
    std::ofstream(root / "loose.txt") << "old";
    std::ofstream(root / "keep.txt") << "old";
    std::ofstream(root / "hall" / "keep.txt") << "new";
    check(assets.adopt(root, "hall") == 1 && fs::exists(root / "hall" / "loose.txt") && fs::exists(root / "keep.txt"),
          "an older flat folder is moved in, and never over what is there");
    check(fs::exists(root), "and the root stays");
    fs::remove_all(root);
}

void test_the_graph_is_watched() {
    // States meet only through what the graph declares; the engine keeps
    // checking, and says when one floats free.
    sg::StateGraph g;
    auto& room = g.add<sg::Spatial3D>("room");
    room.portal("screen", {0, 1, 0}, 1.0, 0.75);
    g.set_initial("room");
    sg::Engine engine(g);
    std::vector<std::string> said;
    engine.on_problem = [&](const std::string& p) { said.push_back(p); };
    engine.set_watch_interval(0.0);
    engine.start();
    engine.tick(0.01);
    check(said.empty(), "a graph of one room has nothing wrong");
    auto& realm = g.add<sg::Spatial3D>("realm");
    engine.tick(0.01);
    check(!said.empty() && said.back().find("realm") != std::string::npos,
          "a state added with no interface to it is reported as it appears");
    g.set_focus(g.embed(sg::Key{"screen.realm"}, room.id(), sg::Key{"screen"}, realm.id(), sg::Key{}, sg::Key{}).name, false);
    engine.tick(0.01);
    check(engine.check_graph().empty(), "embedded in a portal, it is reached through it, and all is well");
    engine.open_embed(sg::Key{"screen.realm"});
    check(engine.focused() == nullptr, "an embedding without focus takes no input");
    engine.focus_embed(sg::Key{"screen.realm"}, true);
    check(engine.focused() == &realm, "given focus, its guest does");
    engine.focus_embed(sg::Key{"screen.realm"}, false);
    engine.close_embed(sg::Key{"screen.realm"});
    check(g.drop_embedding(sg::Key{"screen.realm"}) && !g.embedding(sg::Key{"screen.realm"}), "an embedding can be taken away");
    engine.set_strict(true);
    bool threw = false;
    try {
        engine.check_graph();
    } catch (const std::exception&) {
        threw = true;
    }
    check(threw, "strict, a graph that breaks throws");

    // A room glued by a seam is reached through it.
    sg::StateGraph h;
    h.add<sg::Spatial3D>("a");
    h.add<sg::Spatial3D>("b");
    h.set_initial("a");
    sg::Seam seam;
    seam.name = sg::Key{"a|b"};
    seam.a = sg::Key{"a"}, seam.b = sg::Key{"b"};
    h.add_seam(seam);
    check(h.reachable().count(sg::Key{"b"}) == 1, "a room glued by a seam is reachable through it");

    // Every state starts somewhere, and can go back there.
    sg::StateGraph d;
    auto& s = d.add<sg::Spatial3D>("s");
    s.mesh("crate", 1, 0, 1);
    d.keep_defaults();
    s.element(sg::Key{"crate"}).params.set(sg::keys::x, 5.0);
    s.mesh("extra", 0, 0, 0);
    s.params().set("mood", 3.0);
    check(d.restore_default(sg::Key{"s"}) && s.element(sg::Key{"crate"}).params.num(sg::keys::x) == 1.0 &&
              !s.find(sg::Key{"extra"}) && !s.params().has(sg::Key{"mood"}),
          "restored, a state is exactly as it started");
    // Its arrows too: one taken away with what it stood on, and another
    // added since, as many as there were - the start brings back the one,
    // and not the other.
    sg::StateGraph k;
    auto& t = k.add<sg::State>("t");
    const auto nothing = [](sg::State&, sg::Element&, sg::Element*, const sg::Event&) {};
    t.add_element("stand1", "stand");
    t.loop("summon.stand1", "stand1", "summon", nothing);
    k.keep_defaults();
    t.remove_with_arrows(sg::Key{"stand1"});
    t.add_element("stand2", "stand");
    t.loop("summon.stand2", "stand2", "summon", nothing);
    check(k.restore_default(sg::Key{"t"}) && t.morphism(sg::Key{"summon.stand1"}) && !t.morphism(sg::Key{"summon.stand2"}) &&
              t.find(sg::Key{"stand1"}) && !t.find(sg::Key{"stand2"}) && t.validate().empty(),
          "restored, its arrows are the ones it started with, not as many");
}

// A state that gives an account of its doorway the other side does not agree
// with, slowly enough that checking many of them is a cost.
struct Doorway : sg::State {
    bool walked;
    Doorway(const char* name, bool w) : sg::State(sg::Key{name}), walked(w) { add_element("d", "portal"); }
    sg::Params overlap(sg::Key) const override {
        const auto until = std::chrono::steady_clock::now() + std::chrono::microseconds(150);
        while (std::chrono::steady_clock::now() < until) {}
        return sg::Params{}.set("walk", walked ? 1.0 : 0.0);
    }
};

sg::Seam disagreeing_seams(sg::StateGraph& g, int n) {
    sg::Seam last;
    g.add<sg::State>("hub");
    for (int i = 0; i < n; ++i) {
        const std::string a = "a" + std::to_string(i), b = "b" + std::to_string(i);
        g.add<Doorway>(a.c_str(), true);
        g.add<Doorway>(b.c_str(), false);
        sg::Seam seam;
        seam.name = sg::Key{a + "|" + b};
        seam.a = sg::Key{a}, seam.b = sg::Key{b};
        seam.boundary_a = {sg::Key{"d"}}, seam.boundary_b = {sg::Key{"d"}};
        last = g.add_seam(seam);
    }
    g.set_initial("a0");
    return last;
}

void test_the_watch_takes_a_slice_of_a_frame() {
    // Checking the graph again is a seam at a time, a few at a frame, never
    // all in the frame the graph moved - and a strict engine, or one with no
    // interval, still finds it the frame the graph moved.
    const int kSeams = 24;
    sg::StateGraph g;
    disagreeing_seams(g, kSeams);
    sg::Engine engine(g);
    std::vector<std::string> said;
    engine.on_problem = [&](const std::string& p) { said.push_back(p); };
    engine.set_watch_interval(0.001);
    engine.set_watch_slice(0.25);
    engine.start();
    int ticks = 0;
    while (said.size() < static_cast<std::size_t>(kSeams) && ticks < 400) {
        engine.tick(0.01);
        ++ticks;
    }
    const std::vector<std::string> all = sg::Engine(g).check_graph();
    check(ticks > 3, "a check of the graph is spread over frames, not made in one");
    int walked = 0;
    for (const std::string& p : said) walked += p.find("walked through from one side only") != std::string::npos;
    check(walked == kSeams, "and finds every seam that does not agree, as the whole check does");
    check(said.size() == all.size(), "no more and no less than the check made at once");

    // The watch is told to be whole: the frame the graph moved.
    sg::StateGraph h;
    disagreeing_seams(h, kSeams);
    sg::Engine whole(h);
    std::vector<std::string> heard;
    whole.on_problem = [&](const std::string& p) { heard.push_back(p); };
    whole.set_watch_interval(0.0);
    whole.start();
    whole.tick(0.01);
    check(heard.size() == all.size(), "an engine with no interval finds all of it in the frame the graph moved");

    sg::StateGraph k;
    disagreeing_seams(k, kSeams);
    sg::Engine strict(k);
    strict.set_strict(true);
    bool threw = false;
    try {
        strict.start();
        strict.tick(0.01);
    } catch (const std::exception&) {
        threw = true;
    }
    check(threw, "a strict engine throws in the first frame, with the graph broken");
}

void test_light() {
    const sg::Daylight noon = sg::daylight(12.0), night = sg::daylight(0.0), dusk = sg::daylight(18.0);
    check(noon.sun.y > 0.8 && noon.day > 0.99 && noon.stars < 0.01, "at noon the sun is overhead and it is day");
    check(night.sun.y > 0.5 && night.day < 0.01 && night.stars > 0.99, "at midnight the moon is up, and the stars");
    check(night.intensity < noon.intensity && night.light.b > night.light.r, "moonlight is dimmer than the sun, and blue");
    check(dusk.light.r > dusk.light.b * 2.0, "the setting sun is red");
    check(sg::daylight(36.0).sun.y == noon.sun.y, "the hour wraps");

    sg::LookState look("sky");
    sg::show_daylight(look, noon);
    check(std::fabs(look.element(sg::passes::scene).params.num(sg::Key{"uAmbient"}) - noon.ambient) < 1e-12,
          "the day goes into a look");

    sg::Surface2D red(sg::Key{"red"}, 4, 3, 8);
    red.set_background(255, 0, 0);
    red.raster();
    sg::Element lamp;
    lamp.params.set(sg::keys::r, 1.0).set(sg::keys::g, 1.0).set(sg::keys::b, 1.0).set(sg::keys::intensity, 0.0);
    for (int i = 0; i < 60; ++i) sg::spill(lamp, red, 0.05);
    check(lamp.params.num(sg::keys::r) > 0.99 && lamp.params.num(sg::keys::g) < 0.4,
          "a screen's lamp takes the colour of its picture, paler");
    sg::Surface2D white(sg::Key{"white"}, 4, 3, 8);
    white.set_background(255, 255, 255);
    white.raster();
    sg::Element bright = lamp;
    for (int i = 0; i < 60; ++i) sg::spill(bright, white, 0.05);
    check(std::fabs(bright.params.num(sg::keys::intensity) - 0.05) < 1e-3 &&
              lamp.params.num(sg::keys::intensity) < 0.03,
          "as strong as the picture is bright");
}

void test_looks() {
    namespace p = sg::passes;
    sg::StateGraph g;
    auto& hall = g.add<sg::Spatial3D>("hall");
    auto& calm = g.add<sg::LookState>("calm");
    auto& alert = g.add<sg::LookState>("alert");
    calm.uniform(p::scene, "uFogDensity", 0.02);
    alert.uniform(p::scene, "uFogDensity", 0.06)
        .uniform(p::composite, "uTint", 1.0, 0.5, 0.5)
        .fade(0.5);
    g.set_initial("hall");
    sg::wear(g, "hall", "calm");
    sg::wear(g, "hall", "alert");

    check(sg::active_look(hall) == sg::Key{"calm"}, "the first look a room wears is the one shown");
    check(sg::worn_looks(g, "hall").size() == 2, "and it may wear more than one");
    check(sg::look_defects(g).empty(), "a room showing a look it wears has nothing wrong");
    check(sg::verify(g).ok(), "looks are reachable through the rooms that wear them, and a graph with looks in it keeps every law");

    sg::LookState standard("<standard>");
    standard.uniform(p::scene, "uFogDensity", 0.018).uniform(p::composite, "uTint", 1.0, 1.0, 1.0);
    sg::LookFader fader(standard);

    const auto step = [&](double dt) {
        fader.advance(dt);
        return fader.mix("hall", fader.look_of(&g, hall));
    };
    const auto fog = [&](const sg::LookMix& mx) {
        return fader.value(mx, p::scene, "uFogDensity", 0.0);
    };

    sg::LookMix m = step(0.0);
    check(m.settled() && m.weight(&calm) == 1.0f,
          "a first sighting starts in its look, with nothing to fade from");

    sg::set_look(hall, "alert");
    m = step(0.25);
    check(roughly(m.weight(&alert), 0.5) && roughly(m.weight(&calm), 0.5),
          "a change of look moves weight over the incoming look's own time");
    check(roughly(fog(m), 0.04), "numbers are the weighted blend of the looks");
    check(roughly(fader.value(m, p::composite, "uTint.y", 0.0), 0.75),
          "and one a look never set comes from the standard look");
    m = fader.mix("hall", fader.look_of(&g, hall));
    check(roughly(m.weight(&alert), 0.5), "asked twice in one frame, a fade moves once");

    // Turned back half way: it walks back down the same path, no jump.
    sg::set_look(hall, "calm");
    double last = fog(m), worst = 0.0;
    for (int i = 0; i < 20; ++i) {
        m = step(0.8 / 20.0);  // calm's fade is the default 0.6 s
        worst = std::max(worst, std::fabs(fog(m) - last));
        last = fog(m);
    }
    check(worst < 0.004,
          "undone half way, a change flows back continuously - no frame jumps");
    check(m.settled() && m.weight(&calm) == 1.0f && roughly(fog(m), 0.02),
          "and ends exactly where it started");

    // Forward then back by the same weight is the identity: the reverse is
    // the same path, walked the other way.
    sg::set_look(hall, "alert");
    m = step(0.1);  // alert gains 0.2
    sg::set_look(hall, "calm");
    m = step(0.12);  // calm regains 0.2 at its own rate (0.12 / 0.6)
    check(m.settled() && roughly(fog(m), 0.02), "forward then back by the same amount is no change");

    // A third look mid-change leaves from the blend on screen, not from an end.
    sg::set_look(hall, "alert");
    m = step(0.25);
    const double before = fog(m);  // half calm, half alert
    auto& storm = g.add<sg::LookState>("storm");
    storm.uniform(p::scene, "uFogDensity", 0.10).fade(1.0);
    sg::wear(g, "hall", "storm");
    sg::set_look(hall, "storm");
    m = step(0.01);
    check(std::fabs(fog(m) - before) < 0.001 && m.parts.size() == 3,
          "a change to a third look mid-fade starts from the blend it was in");
    for (int i = 0; i < 100; ++i) m = step(0.01);
    check(m.settled() && &m.shown() == &storm && roughly(fog(m), 0.10),
          "and arrives in the third look");
    sg::set_look(hall, "alert");
    for (int i = 0; i < 10; ++i) m = step(0.1);

    sg::set_look(hall, "nowhere");
    check(&fader.look_of(&g, hall) == &standard, "a look that is not there shows the standard one");
    bool named = false;
    for (const auto& d : sg::look_defects(g)) named = named || d.find("does not wear") != std::string::npos;
    check(named, "and showing a look a room does not wear is named");
    sg::set_look(hall, "calm");

    g.add<sg::State>("notes");
    sg::wear(g, "hall", "notes");
    alert.add_element("sepia", sg::kinds::pass);
    bool not_a_look = false, no_pass = false;
    for (const auto& d : sg::look_defects(g)) {
        not_a_look = not_a_look || d.find("notes, which is not a look") != std::string::npos;
        no_pass = no_pass || d.find("no pass named sepia") != std::string::npos;
    }
    check(not_a_look, "wearing a state that is not a look is named");
    check(no_pass, "and so is a look with a pass no renderer has");
}

void test_surface_and_views() {
    sg::Surface2D surf("panel", 4, 3, 8);
    surf.sprite("tok", 1, 1);
    surf.raster();
    const uint64_t rev = surf.revision();
    surf.raster();
    check(surf.revision() == rev, "an unchanged surface is not redrawn");
    surf.element("tok").params.set(sg::keys::x, 2.0);
    surf.raster();
    check(surf.revision() > rev, "moving a token redraws");
    check(surf.px_w() == 32 && surf.px_h() == 24, "raster size follows the cell grid");

    sg::Spatial3D world("w");
    world.mesh("crate", 2, 0, 3);
    world.light("lamp", {2, 3, 2});
    std::ostringstream os;
    sg::render::draw_top_down(world, 8, 6, os);
    check(os.str().find('#') != std::string::npos, "the terminal view draws the crate");

    // A surface that paints itself: repainted when it says so, and only then.
    struct Sheet : sg::Surface2D {
        Sheet() : sg::Surface2D("sheet", 6, 4, 1) {}
        int shade = 40;
        int paints = 0;
        void ink(int v) {
            shade = v;
            invalidate();
        }
        void paint() override {
            ++paints;
            fill(shade, shade, shade);
            put(0, 0, 255, 0, 0);
        }
    };
    Sheet sheet;
    sheet.raster();
    sheet.raster();
    check(sheet.paints == 1, "a painted surface is painted once until it changes");
    check(sheet.pixel(0, 0)[0] == 255 && sheet.pixel(1, 0)[0] == 40, "with its own pixels");
    sheet.ink(90);
    sheet.raster();
    check(sheet.paints == 2 && sheet.pixel(3, 2)[1] == 90, "and again once it is invalidated");
}

// A camera is a state of its own: it stands alone, keeps the laws alone, and
// sees a world only by filming it - the world embedded in its lens, the rig
// it stands on carried onto the lens. A screen in the same room can show it.
void test_a_camera_is_a_state() {
    {
        sg::StateGraph g;
        auto& cam = g.add<sg::Camera>("cam");
        g.set_initial("cam");
        check(sg::verify(g).ok(), "a camera alone is a whole state, and keeps every law");
        cam.emit(sg::Event{sg::Camera::aim_event(), sg::Params{}.set(sg::keys::x, 1.0).set(sg::keys::yaw, 0.5)});
        cam.emit(sg::Event{sg::Camera::zoom_event(), sg::Params{}.set(sg::keys::fov, 500.0)});
        cam.dispatch_pending();
        check(near(cam.lens().params.num(sg::keys::x), 1.0) && near(cam.lens().params.num(sg::keys::yaw), 0.5) &&
                  near(cam.lens().params.num(sg::keys::fov), 150.0),
              "it is aimed and zoomed by its own arrows, and no lens is wider than a lens can be");
    }
    sg::StateGraph g;
    auto& room = g.add<sg::Spatial3D>("room");
    sg::Element& shelf = room.add_element("shelf", sg::kinds::anchor);
    shelf.params.set(sg::keys::x, 2.0).set(sg::keys::y, 1.0).set(sg::keys::z, 0.0).set(sg::keys::yaw, 0.0);
    sg::Element& rig = room.add_element("rig", sg::kinds::anchor);
    rig.params.set("parent", std::string("shelf")).set(sg::keys::x, 0.5).set(sg::keys::y, 0.2).set(sg::keys::z, 0.0).set(sg::keys::pitch, -0.1);
    sg::Element& screen = room.portal("screen", {0, 1, 3}, 0.4, 0.3, 0.0);
    auto& cam = g.add<sg::Camera>("cam");
    const sg::Key film = sg::film(g, cam.id(), room.id(), "rig");
    screen.params.set("feed", 1.0).set("shows", film.str());
    g.set_initial("room");
    const sg::LawReport r = sg::verify(g);
    if (!r.ok()) std::printf("%s", r.str().c_str());
    check(r.ok(), "filming reaches the camera (a camera that films a room is part of the graph), and the room, the camera and the filming keep every law");
    const sg::Embedding* em = g.embedding(film);
    check(em && em->host == cam.id() && em->guest == room.id() && !em->focus,
          "the room is embedded in the lens, and takes no input through it");
    sg::Engine e(g);
    e.set_strict(true);
    e.start();
    e.open_embed(film);
    e.tick(1.0 / 60.0);
    const sg::Pose at = sg::world_pose(room, room.element("rig"));
    check(near(cam.lens().params.num(sg::keys::x), at.position.x) && near(cam.lens().params.num(sg::keys::y), 1.2) &&
              near(cam.lens().params.num(sg::keys::pitch), -0.1),
          "rolling, the camera stands where its rig is, through what the rig stands on");
    shelf.params.set(sg::keys::x, 3.0);
    e.tick(1.0 / 60.0);
    check(near(cam.lens().params.num(sg::keys::x), 3.5), "and moves with it");
    check(room.find("rig") && !cam.find("rig") && !room.find(sg::Camera::lens_id()),
          "neither knows the other's things: the room has no lens, the camera no rig");
}

// A room is seen into from where it is seen: the engine carries the viewer's
// eye across each seam of the room it is in, by the seam's own travel.
void test_the_view_crosses_seams() {
    sg::StateGraph g;
    auto& hall = g.add<sg::Spatial3D>("hall");
    auto& annex = g.add<sg::Spatial3D>("annex");
    hall.portal("door", {14.0, 1.5, 7.0}, 2.8, 3.0, 3.14159265358979);
    annex.portal("door", {4.5, 1.5, 0.0}, 2.8, 3.0, 1.5707963267949);
    g.set_initial("hall");
    sg::glue_doorway(g, "doorway", "hall", "door", "annex", "door");
    sg::Engine e(g);
    e.set_strict(true);
    e.start();
    hall.camera().params.set(sg::keys::x, 10.0).set(sg::keys::z, 7.5).set(sg::keys::yaw, 0.3);
    e.tick(1.0 / 60.0);
    sg::Element want = annex.camera();
    sg::portal_carry(hall.element("door"), annex.element("door"))(hall.camera(), want);
    check(roughly(annex.camera().params.num(sg::keys::x), want.params.num(sg::keys::x)) &&
              roughly(annex.camera().params.num(sg::keys::z), want.params.num(sg::keys::z)) &&
              roughly(annex.camera().params.num(sg::keys::yaw), want.params.num(sg::keys::yaw)),
          "the room beyond the door is seen from the eye carried through it");
    const double was = hall.camera().params.num(sg::keys::x);
    annex.camera().params.set(sg::keys::x, 99.0);
    e.tick(1.0 / 60.0);
    check(near(hall.camera().params.num(sg::keys::x), was), "and only from the room the engine is in: the far room's eye moves no one");
}

// A step through a doorway is a step into the room beyond, in the frame it is
// taken: the engine takes the seam's own transition after the step, so no
// frame is seen from past the doorway in the room it leads out of.
void test_a_step_crosses_in_its_frame() {
    sg::StateGraph g;
    auto& hall = g.add<sg::Spatial3D>("hall");
    auto& annex = g.add<sg::Spatial3D>("annex");
    hall.portal("door", {14.0, 1.5, 7.0}, 2.8, 3.0, 3.14159265358979).params.set("walk", 1.0);
    annex.portal("door", {4.5, 1.5, 0.0}, 2.8, 3.0, 1.5707963267949).params.set("walk", 1.0);
    hall.arrow("stride", sg::SpatialState::camera_id(), sg::SpatialState::camera_id(), "step",
               [](sg::State&, sg::Element& eye, sg::Element*, const sg::Event& ev) {
                   eye.params.set(sg::keys::x, eye.params.num(sg::keys::x) + ev.args.num("dx"));
               });
    g.set_initial("hall");
    sg::walkway(g, "doorway", "hall", "door", "annex", "door");
    sg::Engine e(g);
    e.set_strict(true);
    e.start();
    hall.camera().params.set(sg::keys::x, 13.95).set(sg::keys::y, 1.6).set(sg::keys::z, 7.0);
    e.tick(1.0 / 60.0);
    hall.camera().params.set(sg::keys::x, 14.2);  // put past it, not walked
    e.tick(1.0 / 60.0);
    check(e.current() == &hall, "a walker put past a doorway is not walked through it");
    hall.camera().params.set(sg::keys::x, 13.95);
    sg::Element want = annex.camera();
    sg::Element after = hall.camera();
    after.params.set(sg::keys::x, 14.05);
    sg::portal_carry(hall.element("door"), annex.element("door"))(after, want);
    e.fire(sg::Event{"step", sg::Params{}.set("dx", 0.1)});
    e.tick(1.0 / 60.0);
    check(e.current() == &annex, "a step through the doorway lands in the room beyond, in the frame it is taken");
    check(roughly(annex.camera().params.num(sg::keys::x), want.params.num(sg::keys::x)) &&
              roughly(annex.camera().params.num(sg::keys::z), want.params.num(sg::keys::z)),
          "carried there by the seam's own travel, where the step took it");
}

// A seam glues like to like: no doorway between a space walked in three
// dimensions and one walked in two (that one is a surface, gone into as a
// screen is). And what a doorway carries of an eye is the whole of it - the
// lens too: an eye from a room of perspective is not a far camera's ortho.
void test_seams_glue_like_to_like() {
    sg::StateGraph bad;
    bad.add<sg::Spatial3D>("hall").portal("frame", {14.0, 1.5, 7.0}, 2.8, 3.0, 3.14159265358979);
    auto& flat = bad.add<sg::Spatial3D>("flat");
    flat.params().set("walk_dims", 2.0);
    flat.portal("door", {0.0, 1.5, 0.0}, 1.0, 2.0, 0.0);
    bad.set_initial("hall");
    sg::glue_doorway(bad, "doorway", "hall", "frame", "flat", "door");
    bool refused = false;
    for (const auto& e : bad.validate()) refused = refused || e.find("like to like") != std::string::npos;
    check(refused, "a doorway between a space of three and one of two is refused, and told why");

    sg::Element from = bad.state("hall").find("frame") ? static_cast<sg::Spatial3D&>(bad.state("hall")).camera() : sg::Element{};
    from.params.set(sg::keys::x, 13.0).set(sg::keys::y, 1.6).set(sg::keys::z, 7.0).set(sg::keys::fov, 70.0);
    sg::Element to = flat.camera();
    to.params.set("ortho", 5.0);
    sg::portal_carry(static_cast<sg::Spatial3D&>(bad.state("hall")).element("frame"), flat.element("door"))(from, to);
    check(!to.params.has("ortho") && near(to.params.num(sg::keys::fov), 70.0), "a doorway carries the eye's lens with it, not the far camera's");
}

// A seam is seamless: two rooms glued at a doorway, each with its floor and
// its pull, and what the overlap law says when one side is not like the
// other - and that a seam saying what differs is let be.
void test_seams_are_seamless() {
    // `floor` how far down the far room's ground is; `pull` its gravity,
    // sideways if it says so; `walkway` the standard doorway, or bare glue.
    const auto room = [](double floor, bool sideways, bool standard, const std::string& differs = "") {
        auto g = std::make_unique<sg::StateGraph>();
        auto& hall = g->add<sg::Spatial3D>("hall");
        auto& annex = g->add<sg::Spatial3D>("annex");
        hall.params().set("g", 9.8);
        annex.params().set("g", sideways ? 0.0 : 9.8);
        if (sideways) annex.add_element("pull", "field").params.set("channel", std::string("gravity")).set("field_shape", std::string("directional")).set("strength", 9.8)
                          .set("dx", 1.0).set("dy", 0.0).set("dz", 0.0);
        hall.portal("door", {14.0, 1.5, 7.0}, 2.8, 3.0, 3.14159265358979).params.set("walk", 1.0);
        annex.portal("door", {4.5, 1.5 - floor, 0.0}, 2.8, 3.0, 1.5707963267949).params.set("walk", 1.0).set("differs", differs);
        for (auto* s : {&hall, &annex}) {
            sg::Element& f = s->mesh("floor", 7.0, -0.2, 3.0);
            f.params.set(sg::keys::sx, 30.0).set(sg::keys::sy, 0.2).set(sg::keys::sz, 30.0).set("solid", 1.0);
        }
        g->set_initial("hall");
        if (standard) sg::walkway(*g, "doorway", "hall", "door", "annex", "door");
        else sg::glue_doorway(*g, "doorway", "hall", "door", "annex", "door");
        return g;
    };
    const auto says = [](const sg::StateGraph& g, const std::string& what) {
        const auto v = sg::laws::overlaps(g);
        if (what.empty()) return v.empty();
        return std::any_of(v.begin(), v.end(), [&](const sg::Violation& x) { return x.detail.find(what) != std::string::npos; });
    };
    check(says(*room(0.0, false, true), ""), "the standard doorway between like rooms is seamless as made");
    check(says(*room(0.0, false, false), "leads nowhere"), "a doorway walked into with no way across is named");
    check(says(*room(0.3, false, true), "the ground is"), "a doorway whose far side stands higher off its ground is named: whoever crosses would drop");
    check(says(*room(0.0, true, true), "down is"), "a doorway into a room whose pull is another way is named: whoever crosses would be turned");
    check(says(*room(0.3, true, true, "floor down"), ""), "a seam that says what differs is let be");
}

}  // namespace

void test_adjunction_is_not_an_isomorphism() {
    // A = the order 0 < 1, B = one object. F collapses A; G picks out 1.
    // F -| G with unit eta_0 = u : 0 -> 1 and eta_1 = id: an adjunction
    // whose round trip does not come home, so not an isomorphism.
    sg::StateGraph g;
    auto& A = g.add<sg::State>("A");
    auto& B = g.add<sg::State>("B");
    A.add_element("0", "n");
    A.add_element("1", "n");
    B.add_element("*", "n");
    A.arrow("u", "0", "1", "never",
            [](sg::State&, sg::Element&, sg::Element* to, const sg::Event&) {
                if (to) to->params.set("reached", 1.0);
            });
    A.loop("id_1", "1", "never", nullptr);
    B.loop("id_*", "*", "never", nullptr);

    sg::Functor& F = g.add_functor("F", "A", "B");
    F.on_object("0", "*").on_object("1", "*").on_morphism("u", "id_*").on_morphism("id_1", "id_*");
    sg::Functor& G = g.add_functor("G", "B", "A");
    G.on_object("*", "1").on_morphism("id_*", "id_1");

    sg::Adjunction adj("F -| G", &F, &G);
    adj.identity("id_1").identity("id_*").unit("0", "u").unit("1").counit("*");
    const auto defects = adj.check(A, B);
    for (const auto& d : defects) std::printf("        %s\n", d.c_str());
    check(defects.empty(), "unit, counit, naturality and both triangles hold");
    check(!adj.unit_defects(A).empty(), "yet G(F(0)) is 1, not 0");
    check(!adj.is_isomorphism(A, B), "so it is an adjunction and not an isomorphism");
    check(sg::laws::adjunction(g, adj).empty(), "and the equations hold on live data");

    // Without a unit arrow where G(F(x)) is not x, there is nothing to check.
    sg::Adjunction bare("F -| G, no unit", &F, &G);
    bare.identity("id_1").identity("id_*");
    check(!bare.holds(A, B), "a missing unit component is refused");

    // A second arrow 0 -> 1 makes Hom(0, G*) two arrows against Hom(F0, *)'s
    // one: no bijection, and naturality at the new arrow says so.
    A.arrow("v", "0", "1", "never", nullptr);
    F.on_morphism("v", "id_*");
    check(!adj.holds(A, B), "two parallel arrows collapsed by F break unit naturality");

    // G sending * to 0 needs a unit 1 -> 0, which A does not have.
    sg::Functor G0("G0", "B", "A");
    G0.on_object("*", "0");
    sg::Adjunction wrong("F -| G0", &F, &G0);
    wrong.identity("id_*").unit("1", "u");
    check(!wrong.holds(A, B), "a unit of the wrong type is refused");
}

// What a Kan result may claim: a functor only on a finished search with
// nothing left open; a defect only where a search finished; nothing either
// way on one that did not.
bool epistemic(const sg::kan::Result& r) {
    if (r.ok() && (!r.complete || !r.holes.empty() || !r.defects.empty())) return false;
    if (!r.defects.empty() && !r.complete) return false;
    if (!r.holes.empty() && r.ok()) return false;
    if (!r.complete && (r.ok() || !r.defects.empty())) return false;
    return true;
}

void test_kan_extensions() {
    // A known piece of a world, and a picture of the piece. A = {p -f-> q};
    // B = A and one more thing past it, {p -f-> q -g-> r}; K the inclusion,
    // carrying each object whole; F draws A in C = {P -Ff-> Q}. Lan_K F asks
    // what F must do on all of B: r lies past q, so it goes where q goes.
    const auto add = [](sg::State&, sg::Element& from, sg::Element* to, const sg::Event&) {
        if (to) to->params.set("v", to->params.num("v") + from.params.num("v"));
    };
    {
        sg::StateGraph g;
        auto& A = g.add<sg::State>("piece");
        auto& B = g.add<sg::State>("world");
        auto& C = g.add<sg::State>("picture");
        A.add_element("p", "n").params.set("v", 1.0);
        A.add_element("q", "n").params.set("v", 5.0);
        A.arrow("f", "p", "q", "never", add);
        B.add_element("p", "n").params.set("v", 1.0);
        B.add_element("q", "n").params.set("v", 5.0);
        B.add_element("r", "n");
        B.arrow("f", "p", "q", "never", add);
        B.arrow("g", "q", "r", "never", nullptr);
        C.add_element("P", "n").params.set("v", 1.0);
        C.add_element("Q", "n").params.set("v", 5.0);
        C.arrow("Ff", "P", "Q", "never", add);
        g.add_functor("K", "piece", "world").on_object("p", "p").on_object("q", "q").on_morphism("f", "f");
        g.add_functor("F", "piece", "picture").on_object("p", "P").on_object("q", "Q").on_morphism("f", "Ff");

        // First compiled: the extension exists, but g goes to the identity
        // on Q, which the picture does not name, and r's data derives from
        // nothing declared. Found, not executable: no functor.
        sg::kan::Result r = sg::kan::left(g, "K", "F", "Lan");
        check(epistemic(r), "and it claims only what it proved");
        std::printf("        %s\n", r.str().c_str());
        check(r.defects.empty() && r.complete && !r.ok(), "nothing makes it impossible, but it has holes: no functor");
        check(r.objects.at("p") == sg::Key{"P"} && r.objects.at("q") == sg::Key{"Q"} &&
                  r.objects.at("r") == sg::Key{"Q"},
              "r, forced past q, goes where q goes: the colimit is Q, found among the picture's own elements");
        check(r.arrows.at("f") == sg::Key{"Ff"} && !r.arrows.count("g"), "f goes to Ff; g has nowhere named to go");
        check(r.cells.at("p").empty() && r.cells.at("q").empty(), "on the piece itself the 2-cell is the identity");
        check(r.holes.size() == 2 && r.holes_of(sg::kan::Hole::Kind::Arrow) == 1 &&
                  r.holes_of(sg::kan::Hole::Kind::Transport) == 1,
              "two holes: an identity at Q (an arrow), and r's data (a transport)");
        check(r.current(g), "and what it was worked out from has not moved");

        // Data moving under the same structure leaves the answer standing.
        C.element("Q").params.set("v", 9.0);
        check(r.current(g), "data moved: nothing to compile again");
        C.element("Q").params.set("v", 5.0);

        // Fill the holes as any world is changed: an identity loop at Q, and
        // a transport said for r (it carries nothing).
        C.loop("id_Q", "Q", "never", nullptr);
        check(!r.current(g), "structure moved: the answer is stale");
        sg::kan::Options o;
        o.supply("r", sg::transport::only({}));
        r = sg::kan::left(g, "K", "F", "Lan", o);
        check(epistemic(r), "and it claims only what it proved");
        std::printf("        %s\n", r.str().c_str());
        check(r.ok() && r.complete && r.holes.empty() && r.defects.empty(), "compiled: an ordinary functor");
        check(r.functor->image_object("r") == sg::Key{"Q"} && r.functor->image_morphism("g") == sg::Key{"id_Q"},
              "r to Q, g to Q's identity");
        check(r.functor->declared_of("p") != nullptr, "p's transport is F's own, derived, declared as F's is");

        // The graph takes it as it takes any functor; the laws hold it to its arrows.
        g.add_functor(std::move(*r.functor));
        g.connect("piece", "grow", "world", "K");
        g.connect("world", "draw", "picture", "Lan");
        g.set_initial("piece");
        const sg::LawReport report = sg::verify(g);
        if (!report.ok()) std::printf("%s", report.str().c_str());
        check(report.ok(), "and in the graph its laws hold like any functor's");
    }

    // Where no element of C has the universal property, there is no
    // extension. B = {p -a-> r <-b- q}, A = {p, q}: r's colimit is a
    // coproduct of P and Q, and the picture has two candidates, T and T2,
    // neither through the other.
    {
        sg::StateGraph g;
        auto& A = g.add<sg::State>("piece");
        auto& B = g.add<sg::State>("world");
        auto& C = g.add<sg::State>("picture");
        A.add_element("p", "n");
        A.add_element("q", "n");
        B.add_element("p", "n");
        B.add_element("q", "n");
        B.add_element("r", "n");
        B.arrow("a", "p", "r", "never", nullptr);
        B.arrow("b", "q", "r", "never", nullptr);
        for (const char* x : {"P", "Q", "T", "T2"}) C.add_element(x, "n");
        C.arrow("tp", "P", "T", "never", nullptr);
        C.arrow("tq", "Q", "T", "never", nullptr);
        C.arrow("sp", "P", "T2", "never", nullptr);
        C.arrow("sq", "Q", "T2", "never", nullptr);
        g.add_functor("K", "piece", "world").on_object("p", "p").on_object("q", "q");
        g.add_functor("F", "piece", "picture").on_object("p", "P").on_object("q", "Q");
        sg::kan::Options o;
        o.supply("r", sg::transport::only({}));
        sg::kan::Result r = sg::kan::left(g, "K", "F", "Lan", o);
        check(epistemic(r), "and it claims only what it proved");
        std::printf("        %s\n", r.str().c_str());
        check(!r.ok() && r.complete && r.defects.size() == 1 && r.holes.empty(),
              "two cocones, neither universal: no colimit at r - it cannot exist, and that is proven");
        C.remove_with_arrows("T2");
        check(!r.current(g), "the picture changed");
        r = sg::kan::left(g, "K", "F", "Lan", o);
        check(epistemic(r), "and it claims only what it proved");
        check(r.ok() && r.objects.at("r") == sg::Key{"T"} && r.arrows.at("a") == sg::Key{"tp"} &&
                  r.arrows.at("b") == sg::Key{"tq"},
              "with T alone, it is the coproduct, and a, b go to its legs");

        // An inclusion only: two objects of A onto one of B is refused.
        g.add_functor("K2", "piece", "world").on_object("p", "p").on_object("q", "p");
        const sg::kan::Result k2 = sg::kan::left(g, "K2", "F", "Lan2");
        check(epistemic(k2), "and it claims only what it proved");
        check(!k2.ok() && k2.defects.empty() && !k2.complete && k2.holes_of(sg::kan::Hole::Kind::Unsupported) == 1,
              "along a functor that is not an inclusion: not resolved, and not called impossible");
    }

    // Right: B = {p <-x- r -y-> q}, A = {p, q}. Ran_K F at r is the limit of
    // P and Q - a product, which the picture has as Pr with its projections.
    {
        sg::StateGraph g;
        auto& A = g.add<sg::State>("piece");
        auto& B = g.add<sg::State>("world");
        auto& C = g.add<sg::State>("picture");
        A.add_element("p", "n");
        A.add_element("q", "n");
        B.add_element("p", "n");
        B.add_element("q", "n");
        B.add_element("r", "n");
        B.add_element("far", "n");  // nothing of A reaches it or is reached: outside
        B.arrow("x", "r", "p", "never", nullptr);
        B.arrow("y", "r", "q", "never", nullptr);
        for (const char* e : {"P", "Q", "Pr"}) C.add_element(e, "n");
        C.arrow("pi1", "Pr", "P", "never", nullptr);
        C.arrow("pi2", "Pr", "Q", "never", nullptr);
        g.add_functor("K", "piece", "world").on_object("p", "p").on_object("q", "q");
        g.add_functor("F", "piece", "picture").on_object("p", "P").on_object("q", "Q");
        sg::kan::Options o;
        o.supply("r", sg::transport::only({}));
        const sg::kan::Result r = sg::kan::right(g, "K", "F", "Ran", o);
        check(epistemic(r), "and it claims only what it proved");
        std::printf("        %s\n", r.str().c_str());
        check(r.ok() && r.objects.at("r") == sg::Key{"Pr"} && r.arrows.at("x") == sg::Key{"pi1"} &&
                  r.arrows.at("y") == sg::Key{"pi2"},
              "Ran at r is the product Pr; x and y go to its projections");
        check(!r.objects.count("far"), "and what the piece does not force is left out");
    }

    // A transport is composed from declared ones, or it is a hole. A = {p},
    // B = {p}, C = {P}; F carries p's `v` to P's `w`.
    {
        const auto world = [](sg::StateGraph& g) {
            g.add<sg::State>("piece").add_element("p", "n").params.set("v", 2.0);
            g.add<sg::State>("world").add_element("p", "n").params.set("u", 2.0);
            g.add<sg::State>("picture").add_element("P", "n");
            g.add_functor("F", "piece", "picture").on_object("p", "P", sg::transport::swizzle({{"w", "v"}}));
        };
        using Kind = sg::kan::Hole::Kind;
        {
            // K renames v to u: undone, then F's - one declared transport.
            sg::StateGraph g;
            world(g);
            g.add_functor("K", "piece", "world").on_object("p", "p", sg::transport::swizzle({{"u", "v"}}));
            sg::kan::Result r = sg::kan::left(g, "K", "F", "Lan");
            check(epistemic(r), "and it claims only what it proved");
            check(r.ok() && r.functor->declared_of("p") != nullptr, "K a renaming: undone, then F's, declared");
            sg::Element P("P", "n");
            (*r.functor->transport_of("p"))(g.state("world").element("p"), P);
            check(P.params.num("w") == 2.0 && !P.params.has("u") && !P.params.has("v"),
                  "and it carries the world's u to the picture's w, nothing else");
        }
        {
            // K opaque: nothing says how to undo it.
            sg::StateGraph g;
            world(g);
            g.add_functor("K", "piece", "world").on_object("p", "p", [](const sg::Element& s, sg::Element& d) {
                d.params.set("u", s.params.num("v"));
            });
            const sg::kan::Result r = sg::kan::left(g, "K", "F", "Lan");
            check(epistemic(r), "and it claims only what it proved");
            check(!r.ok() && r.complete && r.defects.empty() && r.holes.size() == 1 &&
                      r.holes_of(Kind::Unsupported) == 1,
                  "K's transport opaque: searched through, but nothing to invert - unsupported, no copy made up");
        }
        {
            // F reads what K does not carry.
            sg::StateGraph g;
            world(g);
            g.add_functor("K", "piece", "world").on_object("p", "p", sg::transport::swizzle({{"u", "x"}}));
            const sg::kan::Result r = sg::kan::left(g, "K", "F", "Lan");
            check(epistemic(r), "and it claims only what it proved");
            check(!r.ok() && r.holes_of(Kind::Transport) == 1, "F reads v, which K drops: a transport hole");
        }
        {
            // Looks like a rename, but two of a's parameters land on one: no inverse.
            sg::StateGraph g;
            world(g);
            g.add_functor("K", "piece", "world")
                .on_object("p", "p", sg::transport::swizzle({{"u", "v"}, {"u", "x"}}));
            const sg::kan::Result r = sg::kan::left(g, "K", "F", "Lan");
            check(epistemic(r), "and it claims only what it proved");
            check(!r.ok() && r.complete && r.holes.size() == 1 && r.holes_of(Kind::Transport) == 1,
                  "a rename with a collision is not inverted: a transport hole");
        }
        {
            // One of a's parameters to two of b's: not one to one either.
            sg::StateGraph g;
            world(g);
            g.add_functor("K", "piece", "world")
                .on_object("p", "p", sg::transport::swizzle({{"u", "v"}, {"u2", "v"}}));
            const sg::kan::Result r = sg::kan::left(g, "K", "F", "Lan");
            check(!r.ok() && r.holes_of(Kind::Transport) == 1, "a rename that duplicates is not inverted either");
        }
        {
            // A copy of everything with a rename on top: the rename may overwrite.
            sg::StateGraph g;
            world(g);
            g.add_functor("K", "piece", "world")
                .on_object("p", "p", sg::transport::swizzle({{"u", "v"}}, true));
            const sg::kan::Result r = sg::kan::left(g, "K", "F", "Lan");
            check(!r.ok() && r.holes_of(Kind::Transport) == 1, "a copy with renames on top is not inverted");
        }
        {
            // F opaque: nothing declared to compose, even after a whole copy.
            sg::StateGraph g;
            g.add<sg::State>("piece").add_element("p", "n");
            g.add<sg::State>("world").add_element("p", "n");
            g.add<sg::State>("picture").add_element("P", "n");
            g.add_functor("K", "piece", "world").on_object("p", "p");
            g.add_functor("F", "piece", "picture").on_object("p", "P", [](const sg::Element& s, sg::Element& d) {
                d.params.set("w", s.params.num("v"));
            });
            const sg::kan::Result r = sg::kan::left(g, "K", "F", "Lan");
            check(!r.ok() && r.complete && r.holes_of(Kind::Unsupported) == 1,
                  "F's transport opaque: unsupported, not reused as if composed");
        }
        {
            // F the identity: only a whole copy makes b's data literally a's.
            sg::StateGraph g;
            g.add<sg::State>("piece").add_element("p", "n").params.set("v", 2.0);
            g.add<sg::State>("world").add_element("p", "n");
            g.add_functor(sg::Functor::identity("piece", "F"));
            g.add_functor("K", "piece", "world").on_object("p", "p");
            sg::kan::Result r = sg::kan::left(g, "K", "F", "Lan");
            check(epistemic(r), "and it claims only what it proved");
            check(r.ok() && r.functor->image_object("p") == sg::Key{"p"},
                  "F the identity, K a whole copy: the identity transport, the same representation");
            g.set_functor(sg::Functor("K", "piece", "world").on_object("p", "p", sg::transport::swizzle({{"u", "v"}})));
            r = sg::kan::left(g, "K", "F", "Lan");
            check(!r.ok() && r.holes_of(Kind::Transport) == 1,
                  "F the identity after a rename: a's other parameters are not there - a hole, not a copy");
        }
    }

    // A search cut short proves nothing: no colimit found within the budget
    // is a hole, not a defect. B = {p -a-> m -b-> r}, A = {p}; C = {P -c-> X
    // -d-> Y}: whether P is universal turns on P -> Y, two arrows long.
    {
        sg::StateGraph g;
        auto& A = g.add<sg::State>("piece");
        auto& B = g.add<sg::State>("world");
        auto& C = g.add<sg::State>("picture");
        A.add_element("p", "n");
        for (const char* e : {"p", "m", "r"}) B.add_element(e, "n");
        B.arrow("a", "p", "m", "never", nullptr);
        B.arrow("b", "m", "r", "never", nullptr);
        for (const char* e : {"P", "X", "Y"}) C.add_element(e, "n");
        C.arrow("c", "P", "X", "never", nullptr);
        C.arrow("d", "X", "Y", "never", nullptr);
        g.add_functor("K", "piece", "world").on_object("p", "p");
        g.add_functor("F", "piece", "picture").on_object("p", "P");
        sg::kan::Options o;
        o.max_path = 1;
        const sg::kan::Result cut = sg::kan::left(g, "K", "F", "Lan", o);
        check(epistemic(cut), "and it claims only what it proved");
        std::printf("        %s\n", cut.str().c_str());
        check(!cut.ok() && !cut.complete && cut.defects.empty() && cut.holes_of(sg::kan::Hole::Kind::Budget) > 0,
              "cut at one arrow: not resolved, never called impossible");
    }
}

int main() {
    test_one_rotation();
    test_keys_and_params();
    test_elements_and_morphisms();
    test_composition();
    test_transitions_and_stack();
    test_guards_and_wildcards();
    test_integration();
    test_time_is_simulated();
    test_transports();
    test_functor_roundtrip();
    test_functor_composition();
    test_adjunction_is_not_an_isomorphism();
    test_kan_extensions();
    test_lens();
    test_embedding();
    test_the_host_says_what_is_open();
    test_anchors();
    test_camera_queries_follow_anchors();
    test_wall_collisions();
    test_portal_transform();
    test_view_portal();
    test_open_world();
    test_seams();
    test_view_portal_validation();
    test_descent();
    test_monodromy();
    test_lossless_counts_what_is_dropped();
    test_atlas_is_a_cover();
    test_any_domain();
    test_guards_against_past_mistakes();
    test_graph_analysis();
    test_surface_and_views();
    test_looks();
    test_light();
    test_the_graph_is_watched();
    test_the_watch_takes_a_slice_of_a_frame();
    test_text_and_store();
    test_assets_a_folder_per_state();
    test_rooms_are_adjacent();
    test_a_camera_is_a_state();
    test_the_view_crosses_seams();
    test_a_step_crosses_in_its_frame();
    test_seams_glue_like_to_like();
    test_seams_are_seamless();
    std::printf("\n%s\n", failures == 0 ? "all tests passed" : "FAILURES PRESENT");
    return failures == 0 ? 0 : 1;
}
