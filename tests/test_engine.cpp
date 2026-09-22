// Stategine - assertions over states, morphisms, functors, portals.
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>

#include "sg/render/Ascii.hpp"
#include "sg/sg.hpp"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "[ok]  " : "[FAIL]", what.c_str());
    if (!ok) ++failures;
}

bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

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
    test_graph_analysis();
    test_surface_and_views();
    std::printf("\n%s\n", failures == 0 ? "all tests passed" : "FAILURES PRESENT");
    return failures == 0 ? 0 : 1;
}
