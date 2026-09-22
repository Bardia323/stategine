// Stategine demo, headless: a console state, a 2D state and a 3D state that
// are isomorphic to each other, transitions that carry data along functors, and
// a portal that nests one state inside another.
#include <iostream>

#include "sg/render/Ascii.hpp"
#include "sg/sg.hpp"

namespace {

void report(const char* title, const std::vector<std::string>& msgs) {
    std::cout << title << ": ";
    if (msgs.empty()) {
        std::cout << "clean\n";
        return;
    }
    std::cout << msgs.size() << "\n";
    for (const auto& m : msgs) std::cout << "  - " << m << "\n";
}

}  // namespace

int main() {
    sg::StateGraph graph;

    // --- states --------------------------------------------------------------
    auto& console = graph.add<sg::ConsoleState>("boot");
    auto& flat = graph.add<sg::Spatial2D>("world2d", 40, 16);
    auto& deep = graph.add<sg::Spatial3D>("world3d");

    flat.sprite("player", 5, 8, '@').params.set(sg::keys::vx, 2.0).set(sg::keys::z, 0.0);
    flat.sprite("rock", 20, 4, 'o').params.set(sg::keys::z, 3.0);
    deep.mesh("player", 5, 8, 0, '@').params.set(sg::keys::vx, 2.0);
    deep.mesh("rock", 20, 4, 3, 'o');
    deep.light("lamp", {10, 3, 5});
    deep.portal("map_table", {2, 2, 0}, 3.0, 2.0, 1.5707963);  // faces +z
    // The portal and the lamp exist on both sides, at the same place, so nothing
    // in either state falls outside the functors and the pair stays a true
    // isomorphism. (Declaring them here with only a z once passed every check
    // that used a scratch world, and failed the one that used the real one.)
    flat.add_element("map_table", sg::kinds::portal)
        .params.set(sg::keys::x, 2.0).set(sg::keys::y, 2.0).set(sg::keys::z, 0.0)
        .set(sg::keys::yaw, 1.5707963);
    flat.add_element("lamp", sg::kinds::light)
        .params.set(sg::keys::x, 10.0).set(sg::keys::y, 3.0).set(sg::keys::z, 5.0);

    // --- functors -------------------------------------------------------------
    // The 2D state parks a z it never draws; that spare slot is what turns the
    // free/forgetful pair into an isomorphism.
    const std::vector<sg::Key> shared{"player", "rock", "camera", "lamp", "map_table"};
    sg::Functor& lift = graph.add_functor("lift", "world2d", "world3d");
    sg::Functor& flatten = graph.add_functor("flatten", "world3d", "world2d");
    // Flattening carries only what a 2D state has room for. Copying everything
    // would drag a lamp's colour and a portal's size into the map on every
    // round trip - which `roundtrip`, composed on scratch data, would never see.
    const sg::Transport to_2d = sg::transport::only(
        {sg::keys::x, sg::keys::y, sg::keys::z, sg::keys::vx, sg::keys::vy, sg::keys::vz,
         sg::keys::glyph, sg::keys::yaw, sg::keys::pitch, sg::keys::fov});
    for (sg::Key id : shared) {
        lift.on_object(id, id, sg::transport::copy_all);
        flatten.on_object(id, id, to_2d);
    }
    lift.on_morphism("move.player", "move.player")
        .on_morphism("move.rock", "move.rock")
        .on_event(flat.step_event(), deep.step_event());
    flatten.on_morphism("move.player", "move.player")
        .on_morphism("move.rock", "move.rock")
        .on_event(deep.step_event(), flat.step_event());
    graph.compose_functors("roundtrip", {"lift", "flatten"});

    // --- an interface nested in the 3D world ----------------------------------
    // A top-down map on a table: (x, z) of the world become (x, y) on the map.
    auto& maproom = graph.add<sg::Spatial2D>("maproom", 30, 10);
    maproom.set_integrating(false);
    maproom.sprite("player_token", 5, 0, '@');
    maproom.sprite("rock_token", 20, 3, 'o');

    graph.add_lens("collapse", "stamp", "world3d", "maproom",
                   {{"player", "player_token"}, {"rock", "rock_token"}},
                   sg::transport::swizzle({{sg::keys::x, sg::keys::x},
                                           {sg::keys::y, sg::keys::z}}),
                   sg::transport::swizzle({{sg::keys::x, sg::keys::x},
                                           {sg::keys::z, sg::keys::y}}));
    graph.embed("map", "world3d", "map_table", "maproom", "collapse", "stamp",
                sg::EmbedSync::Commit);

    // --- transitions ------------------------------------------------------------
    graph.connect("boot", "start", "world2d");
    graph.connect("world2d", "toggle", "world3d").functor = "lift";
    graph.connect("world3d", "toggle", "world2d").functor = "flatten";
    graph.push("world2d", "pause", "boot");
    graph.pop("boot", "resume");
    graph.set_initial("boot");

    // --- what the engine can prove about all that -------------------------------
    report("graph validate", graph.validate());
    report("lift laws", lift.check_laws(flat, deep));
    report("flatten laws", flatten.check_laws(deep, flat));

    sg::Adjunction adj("lift -| flatten", &lift, &flatten);
    report("unit   (2d -> 3d -> 2d, objects)", adj.unit_defects(flat));
    report("counit (3d -> 2d -> 3d, objects)", adj.counit_defects(deep));
    sg::Spatial3D scratch3d("scratch3d");
    sg::Spatial2D scratch2d("scratch2d");
    report("round trip data", adj.data_defects(flat, scratch3d, scratch2d));
    std::cout << "isomorphic: " << (adj.is_isomorphism(flat, deep) ? "yes" : "no") << "\n";

    // Every law, run on the data as it stands and then undone. The integrators
    // do nothing at dt = 0, so they are probed with a real step.
    sg::LawOptions probe;
    probe.args.set(sg::keys::dt, 0.5);
    const sg::LawReport laws = sg::verify(graph, {}, probe);
    std::vector<std::string> broken = laws.structure;
    for (const auto& v : laws.violations) broken.push_back(v.str());
    report("laws on live data", broken);
    std::cout << "\n";

    // --- run ---------------------------------------------------------------------
    sg::Engine engine(graph);
    engine.set_trace(true);
    engine.start("boot", sg::Params{}.set("banner", std::string("stategine ready")));
    engine.fire("start");
    engine.run_fixed(0.5, 2);
    sg::render::draw(flat);

    std::cout << "\n[switch to 3D, carrying the world along lift]\n";
    engine.fire("toggle");
    engine.run_fixed(0.5, 2);
    sg::render::draw_top_down(deep, 24, 10);

    std::cout << "\n[open the map that hangs inside the 3D world]\n";
    engine.fire(sg::Event{"embed.open", sg::Params{}.set(sg::keys::name, std::string("map"))});
    engine.run_fixed(0.5, 1);
    sg::render::draw(maproom);

    std::cout << "\n[drag the rock across the map, then close the portal]\n";
    maproom.element("rock_token").params.set(sg::keys::x, 8.0).set(sg::keys::y, 6.0);
    engine.fire(sg::Event{"embed.close", sg::Params{}.set(sg::keys::name, std::string("map"))});
    engine.run_fixed(0.5, 1);
    const sg::Vec3d rock = sg::position_of(deep.element("rock"));
    std::cout << "rock in the world: x=" << rock.x << " y=" << rock.y << " z=" << rock.z
              << "  (height untouched)\n";

    std::cout << "\n[push the console on top, then pop back]\n";
    engine.fire("pause");
    console.submit("hello from the paused stack");
    engine.run_fixed(0.5, 1);
    sg::render::draw(console);
    engine.fire("resume");
    engine.run_fixed(0.5, 1);

    std::cout << "\n--- graph.dot ---\n" << graph.to_dot(false);
    return 0;
}
