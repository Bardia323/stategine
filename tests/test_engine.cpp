// Stategine - assertions over states, morphisms, functors, portals.
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>

#include "sg/core/Sheaf.hpp"
#include "sg/gl/Math.hpp"
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
    auto& t = g.connect("*", "hit", "dead");
    t.guard = [](const sg::State& from, const sg::Event&) {
        return from.params().get_or<int64_t>("hp", 1) <= 0;
    };
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
    check(near(s.element("p").params.num(sg::keys::x), 4.0), "the integrator morphism ran");

    s.set_integrating(false);
    e.run_fixed(0.5, 4);
    check(near(s.element("p").params.num(sg::keys::x), 4.0), "integration can be switched off");
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

    g.connect("f2", "toggle", "f3").functor = "lift";
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

    g.embedding("map_portal")->sync = sg::EmbedSync::Live;
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
    g.connect("room", "step_through", "lab").functor = "peek";
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

    // Close the ring the wrong way: u -> v -> w -> u must be the identity, and
    // 3 + 5 - 7 is not zero. That is holonomy, and there is nothing to glue.
    sg::Cover bad_ring;
    link_shift(g, bad_ring, "u", "v", 3.0);
    link_shift(g, bad_ring, "v", "w", 5.0);
    link_shift(g, bad_ring, "w", "u", 7.0);
    const auto seams = bad_ring.descent_defects(g);
    bool named_cycle = false;
    for (const auto& d : seams)
        if (d.find("cycle") != std::string::npos) named_cycle = true;
    check(!seams.empty() && named_cycle, "a ring that does not close is reported as a seam");

    // Close it correctly and the seam goes away.
    sg::Cover good_ring;
    link_shift(g, good_ring, "u", "v", 3.0);
    link_shift(g, good_ring, "v", "w", 5.0);
    link_shift(g, good_ring, "w", "u", -8.0);
    check(good_ring.descent_defects(g).empty(), "a ring that closes up glues cleanly");
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
}

}  // namespace

int main() {
    test_one_rotation();
    test_keys_and_params();
    test_elements_and_morphisms();
    test_composition();
    test_transitions_and_stack();
    test_guards_and_wildcards();
    test_integration();
    test_transports();
    test_functor_roundtrip();
    test_functor_composition();
    test_lens();
    test_embedding();
    test_anchors();
    test_camera_queries_follow_anchors();
    test_wall_collisions();
    test_portal_transform();
    test_view_portal();
    test_view_portal_validation();
    test_descent();
    test_atlas_is_a_cover();
    test_guards_against_past_mistakes();
    test_graph_analysis();
    test_surface_and_views();
    std::printf("\n%s\n", failures == 0 ? "all tests passed" : "FAILURES PRESENT");
    return failures == 0 ? 0 : 1;
}
