// Stategine example: one space, two rooms, and two interfaces that edit it.
//
// Everything here is a single 3D state. The walls are elements, so they are
// data like anything else - and the second room's walls, lamp, plinths and
// doorway all carry `parent`, naming an anchor element. Move that anchor and
// the whole annex slides through the shared space, doorway included.
//
//   the crate map   in room one: a 2D state on a wall panel, Live-embedded, so
//                   moving a token moves the crate it stands for.
//   the annex map   in room two: the same construction with one object - the
//                   anchor. Its token drags the entire second room. Line the
//                   two doorways up and you can walk between the rooms; slide
//                   it away and the opening closes against a blank wall.
//
//   move      W A S D      (in map mode: move the selected token)
//   look      mouse        (Esc releases the mouse, Esc again quits)
//   use map   E            while standing in front of one
//   select    Tab          cycle tokens while the crate map is open
//   cancel    C            close the map, discarding this session's edits
//   lamp      Q / R        slide the nearer lamp     F  dim / brighten
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "sg/gl/Window.hpp"
#include "sg/render/GLWorld.hpp"
#include "sg/sg.hpp"

namespace {

// Room one occupies x 0..14; the annex lives east of it, wherever its anchor
// happens to be. One floor slab runs under both.
constexpr double kRoomW = 14.0;
constexpr double kRoomD = 14.0;
constexpr double kRoomH = 4.0;
constexpr double kFloorW = 30.0;
constexpr double kFloorD = 16.0;

constexpr double kGapZ = 7.0;     // centre of room one's opening
constexpr double kGapHalf = 1.4;  // half its width

constexpr int kCols = 6;      // crate map cells
constexpr int kRows = 6;
constexpr double kCell = 2.0;  // metres per crate-map cell

constexpr int kAnnexCols = 7;  // annex map cells
constexpr int kAnnexRows = 7;

double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

double cell_to_world(double cell) { return cell * kCell + kCell * 0.5; }
double world_to_cell(double world) { return std::floor(world / kCell); }

// The annex anchor's reachable range, in metres per map cell. Cell (0, 3) is
// the aligned position, where the two doorways meet.
double cell_to_annex_x(double c) { return kRoomW + 0.01 + c * 0.55; }
double annex_x_to_cell(double x) { return std::floor((x - kRoomW - 0.01) / 0.55 + 0.5); }
double cell_to_annex_z(double c) { return kGapZ - 4.5 + (c - 3.0) * 1.3; }
double annex_z_to_cell(double z) { return std::floor((z - kGapZ + 4.5) / 1.3 + 3.5); }

struct Crate {
    sg::Key id;
    double cell_x, cell_z;
    double r, g, b;
    double height;
};

}  // namespace

int main(int argc, char** argv) {
    // room3d [frames] [out.ppm] [shot]
    const long max_frames = argc > 1 ? std::strtol(argv[1], nullptr, 10) : 0;
    const std::string shot = argc > 3 ? argv[3] : std::string{};

    sg::StateGraph graph;
    auto& world = graph.add<sg::Spatial3D>("world");
    world.set_integrating(false);
    world.params().set("room_w", kFloorW).set("room_d", kFloorD).set("room_h", kRoomH);

    // --- room one: fixed walls, with an opening in the east one ------------------
    const double t = 0.3;
    world.wall("w_north", {kRoomW * 0.5, 0.0, -t * 0.5}, kRoomW + t, kRoomH, t);
    world.wall("w_south", {kRoomW * 0.5, 0.0, kRoomD + t * 0.5}, kRoomW + t, kRoomH, t);
    world.wall("w_west", {-t * 0.5, 0.0, kRoomD * 0.5}, t, kRoomH, kRoomD);
    // The east wall is two segments and a lintel; the gap between them is the
    // doorway the annex has to line up with.
    const double lower = kGapZ - kGapHalf, upper = kGapZ + kGapHalf;
    world.wall("w_east_a", {kRoomW, 0.0, lower * 0.5}, t, kRoomH, lower);
    world.wall("w_east_b", {kRoomW, 0.0, (upper + kRoomD) * 0.5}, t, kRoomH, kRoomD - upper);
    world.wall("w_east_top", {kRoomW, 3.0, kGapZ}, t, kRoomH - 3.0, kGapHalf * 2);

    const std::vector<Crate> crates{
        {"crate_a", 1, 1, 0.82, 0.34, 0.24, 1.1},
        {"crate_b", 4, 1, 0.26, 0.62, 0.86, 0.8},
        {"crate_c", 1, 4, 0.88, 0.74, 0.28, 1.4},
        {"crate_d", 4, 5, 0.42, 0.80, 0.45, 1.0},
        {"crate_e", 2, 2, 0.72, 0.42, 0.86, 0.7},
    };
    for (const auto& c : crates) {
        sg::Element& e = world.mesh(c.id, cell_to_world(c.cell_x), 0.0, cell_to_world(c.cell_z));
        e.params.set(sg::keys::sx, c.height * 1.05).set(sg::keys::sy, c.height)
            .set(sg::keys::sz, c.height * 1.05);
        e.params.set(sg::keys::r, c.r).set(sg::keys::g, c.g).set(sg::keys::b, c.b);
        e.params.set("roughness", 0.62).set(sg::keys::yaw, (c.cell_x + c.cell_z) * 0.21);
    }

    sg::Element& lamp = world.light("lamp", {kRoomW * 0.5, 3.05, kRoomD * 0.5});
    lamp.params.set(sg::keys::intensity, 1.0).set("inner", 0.5).set("outer", 1.25);

    world.portal("crate_map", {kRoomW * 0.45, 1.85, 0.25}, 3.0, 2.2, 0.0);

    // --- room two: the same space, hung off an anchor ------------------------------
    // Everything below carries parent="annex", so all of it moves together.
    const double ax = 8.0, az = 9.0, ah = 3.6;  // annex size
    world.anchor("annex", {cell_to_annex_x(0), 0.0, cell_to_annex_z(3)});

    auto annex_part = [](sg::Element& e) -> sg::Element& { return sg::attach_to(e, "annex"); };

    // Local coordinates: x runs east from the shared wall, z across the annex.
    annex_part(world.wall("a_north", {ax * 0.5, 0.0, -t * 0.5}, ax + t, ah, t));
    annex_part(world.wall("a_south", {ax * 0.5, 0.0, az + t * 0.5}, ax + t, ah, t));
    annex_part(world.wall("a_east", {ax + t * 0.5, 0.0, az * 0.5}, t, ah, az));
    // Its west wall has a matching gap, in the middle of the annex.
    const double a_gap = az * 0.5;
    annex_part(
        world.wall("a_west_a", {0.0, 0.0, (a_gap - kGapHalf) * 0.5}, t, ah, a_gap - kGapHalf));
    annex_part(world.wall("a_west_b", {0.0, 0.0, (a_gap + kGapHalf + az) * 0.5}, t, ah,
                          az - a_gap - kGapHalf));
    annex_part(world.wall("a_west_top", {0.0, 3.0, a_gap}, t, ah - 3.0, kGapHalf * 2));

    sg::Element& annex_lamp =
        annex_part(world.light("annex_lamp", {ax * 0.5, 2.9, az * 0.5}, 0.72, 0.86, 1.0));
    annex_lamp.params.set(sg::keys::intensity, 1.1).set("inner", 0.6).set("outer", 1.3);

    for (int i = 0; i < 3; ++i) {
        sg::Element& p =
            annex_part(world.mesh(sg::Key{"plinth_" + std::to_string(i)}, 2.4 + i * 2.0, 0.0,
                                  i % 2 == 0 ? 2.0 : az - 2.0));
        p.params.set(sg::keys::sx, 0.9).set(sg::keys::sy, 1.6 + i * 0.35).set(sg::keys::sz, 0.9);
        p.params.set(sg::keys::r, 0.30).set(sg::keys::g, 0.55).set(sg::keys::b, 0.62);
        p.params.set("roughness", 0.4);
    }
    sg::Element& monolith = annex_part(world.mesh("monolith", ax - 2.0, 0.0, az * 0.5 - 2.6));
    monolith.params.set(sg::keys::sx, 0.6).set(sg::keys::sy, 2.4).set(sg::keys::sz, 1.2);
    monolith.params.set(sg::keys::r, 0.25).set(sg::keys::g, 0.85).set(sg::keys::b, 0.75);
    monolith.params.set("roughness", 0.2);

    // The annex map hangs on the annex's east wall, facing back in (-x).
    annex_part(world.portal("annex_map", {ax - 0.25, 1.8, az * 0.5}, 2.6, 2.0, -1.5707963));

    world.camera().params.set(sg::keys::x, 4.0).set(sg::keys::y, 1.7).set(sg::keys::z, kGapZ);
    world.camera().params.set(sg::keys::yaw, 0.0).set(sg::keys::pitch, -0.02);

    // --- interface one: the crate map -----------------------------------------------
    auto& crate_map = graph.add<sg::Surface2D>("cratemap", kCols, kRows, 48);
    crate_map.set_background(16, 20, 30);
    for (const auto& c : crates) {
        sg::Element& tok = crate_map.sprite(sg::Key{c.id.str() + "_tok"}, c.cell_x, c.cell_z);
        tok.params.set(sg::keys::r, c.r).set(sg::keys::g, c.g).set(sg::keys::b, c.b);
    }
    crate_map.set_selection(sg::Key{crates.front().id.str() + "_tok"});

    std::vector<std::pair<sg::Key, sg::Key>> crate_objects;
    for (const auto& c : crates) crate_objects.emplace_back(c.id, sg::Key{c.id.str() + "_tok"});
    graph.add_lens(
        "collapse", "stamp", "world", "cratemap", crate_objects,
        sg::transport::swizzle_scaled({{sg::keys::x, sg::keys::x}, {sg::keys::y, sg::keys::z}},
                                      world_to_cell),
        sg::transport::swizzle_scaled({{sg::keys::x, sg::keys::x}, {sg::keys::z, sg::keys::y}},
                                      cell_to_world));
    graph.embed("crate_map", "world", "crate_map", "cratemap", "collapse", "stamp",
                sg::EmbedSync::Live);

    // --- interface two: the annex map -------------------------------------------------
    // The same construction with a single object - the anchor. Dragging its
    // token slides the whole second room: walls, lamp, plinths, doorway, and
    // the very map being used.
    auto& annex_map = graph.add<sg::Surface2D>("annexmap", kAnnexCols, kAnnexRows, 44);
    annex_map.set_background(22, 18, 30);
    sg::Element& annex_tok = annex_map.sprite("annex_tok", 0, 3);
    annex_tok.params.set(sg::keys::r, 0.35).set(sg::keys::g, 0.85).set(sg::keys::b, 0.78);
    annex_map.set_selection("annex_tok");

    graph.add_lens(
        "annex_to_map", "map_to_annex", "world", "annexmap", {{"annex", "annex_tok"}},
        sg::transport::then(
            sg::transport::swizzle_scaled({{sg::keys::x, sg::keys::x}}, annex_x_to_cell),
            sg::transport::swizzle_scaled({{sg::keys::y, sg::keys::z}}, annex_z_to_cell)),
        sg::transport::then(
            sg::transport::swizzle_scaled({{sg::keys::x, sg::keys::x}}, cell_to_annex_x),
            sg::transport::swizzle_scaled({{sg::keys::z, sg::keys::y}}, cell_to_annex_z)));
    graph.embed("annex_map", "world", "annex_map", "annexmap", "annex_to_map", "map_to_annex",
                sg::EmbedSync::Live);
    graph.set_initial("world");

    for (const auto& problem : graph.validate()) std::cout << "! " << problem << "\n";

    // --- window, renderer, loop ----------------------------------------------------------
    sg::gl::Window window(1440, 810, "stategine - one space, two rooms, two maps");
    window.capture_mouse(true);

    sg::render::GLWorldView view;
    view.bind_surface("crate_map", &crate_map);
    view.bind_surface("annex_map", &annex_map);

    sg::Engine engine(graph);
    engine.start();

    std::size_t sel = 0;
    double last = glfwGetTime();
    long frames = 0;

    // --- scripted poses, for the README images ---------------------------------------
    if (!shot.empty()) {
        window.capture_mouse(false);
        sg::Element& cam = world.camera();
        auto pose = [&cam](double x, double z, double yaw, double pitch) {
            cam.params.set(sg::keys::x, x).set(sg::keys::z, z);
            cam.params.set(sg::keys::yaw, yaw).set(sg::keys::pitch, pitch);
        };
        if (shot == "room") {
            pose(kRoomW - 1.4, kRoomD - 1.4, -2.3, -0.14);
        } else if (shot == "approach") {
            pose(kRoomW * 0.45 + 1.3, 4.2, -1.85, -0.05);
        } else if (shot == "open") {
            pose(kRoomW * 0.45, 2.4, -1.5708, -0.02);
            engine.open_embed("crate_map");
            crate_map.set_selection(sg::Key{crates[2].id.str() + "_tok"});
        } else if (shot == "before" || shot == "after") {
            pose(1.0, 12.6, -0.55, -0.04);
            engine.open_embed("crate_map");
            crate_map.set_selection(sg::Key{crates[2].id.str() + "_tok"});
            if (shot == "after")
                crate_map.element(sg::Key{crates[2].id.str() + "_tok"})
                    .params.set(sg::keys::x, 4.0);
        } else if (shot == "dim") {
            pose(kRoomW - 1.4, kRoomD - 1.4, -2.3, -0.14);
            lamp.params.set(sg::keys::intensity, 0.35);
        } else if (shot == "doorway") {
            pose(kRoomW - 5.5, kGapZ, 0.0, -0.02);
        } else if (shot == "annex") {
            pose(kRoomW + 4.5, kGapZ, 0.0, -0.02);  // inside the annex, facing its map
        } else if (shot == "back") {
            // From inside the annex, looking back through the opening: both
            // rooms are lit and both are shadowed, from two different lamps.
            pose(kRoomW + 2.6, kGapZ, 3.14159, -0.02);
        } else if (shot == "shifted") {
            // The same view as `doorway`, with the annex dragged out of line.
            // The token has to be set after the portal opens: opening runs the
            // `in` functor, which would otherwise read it straight back.
            pose(kRoomW - 5.5, kGapZ, 0.0, -0.02);
            engine.open_embed("annex_map");
            annex_map.element("annex_tok").params.set(sg::keys::y, 6.0);
        }
    }

    std::cout << "WASD move, mouse look, E to use a map, Tab to select, C to cancel, "
                 "Q/R lamp, F dim, Esc for the mouse\n";

    while (!window.should_close() && engine.running()) {
        window.poll();
        const double now = glfwGetTime();
        const double dt = now - last;
        last = now;

        const bool crate_open = engine.embed_open("crate_map");
        const bool annex_open = engine.embed_open("annex_map");
        const bool map_open = crate_open || annex_open;
        sg::Element& cam = world.camera();

        // --- look ---------------------------------------------------------------------
        if (window.mouse_captured() && !map_open) {
            const double sens = 0.0022;
            cam.params.set(sg::keys::yaw, cam.params.num(sg::keys::yaw) + window.mouse_dx() * sens);
            cam.params.set(sg::keys::pitch,
                           clampd(cam.params.num(sg::keys::pitch) - window.mouse_dy() * sens, -1.4,
                                  1.4));
        }

        // --- move, or drive whichever map is open ---------------------------------------
        if (!map_open) {
            const sg::Vec3d f = sg::forward_of(cam);
            const double len = std::sqrt(f.x * f.x + f.z * f.z);
            const double fx = len > 1e-6 ? f.x / len : 0.0;
            const double fz = len > 1e-6 ? f.z / len : 1.0;
            // right = forward x up, the same basis the view matrix builds.
            const double rx = -fz, rz = fx;
            const double speed = (window.down(GLFW_KEY_LEFT_SHIFT) ? 6.5 : 3.2) * dt;
            double mx = 0, mz = 0;
            if (window.down(GLFW_KEY_W)) { mx += fx * speed; mz += fz * speed; }
            if (window.down(GLFW_KEY_S)) { mx -= fx * speed; mz -= fz * speed; }
            if (window.down(GLFW_KEY_D)) { mx += rx * speed; mz += rz * speed; }
            if (window.down(GLFW_KEY_A)) { mx -= rx * speed; mz -= rz * speed; }
            cam.params.set(sg::keys::x,
                           clampd(cam.params.num(sg::keys::x) + mx, 0.4, kFloorW - 0.4));
            cam.params.set(sg::keys::z,
                           clampd(cam.params.num(sg::keys::z) + mz, 0.4, kFloorD - 0.4));
        } else {
            // Both maps are driven the same way; only the token differs.
            sg::Surface2D& surf = crate_open ? crate_map : annex_map;
            const sg::Key tok_id =
                crate_open ? sg::Key{crates[sel].id.str() + "_tok"} : sg::Key{"annex_tok"};
            const int cols = crate_open ? kCols : kAnnexCols;
            const int rows = crate_open ? kRows : kAnnexRows;
            sg::Element& tok = surf.element(tok_id);
            double dx = 0, dy = 0;
            if (window.pressed(GLFW_KEY_D)) dx += 1;
            if (window.pressed(GLFW_KEY_A)) dx -= 1;
            if (window.pressed(GLFW_KEY_S)) dy += 1;
            if (window.pressed(GLFW_KEY_W)) dy -= 1;
            if (dx != 0 || dy != 0) {
                tok.params.set(sg::keys::x, clampd(tok.params.num(sg::keys::x) + dx, 0, cols - 1));
                tok.params.set(sg::keys::y, clampd(tok.params.num(sg::keys::y) + dy, 0, rows - 1));
            }
            if (crate_open && window.pressed(GLFW_KEY_TAB)) {
                sel = (sel + 1) % crates.size();
                crate_map.set_selection(sg::Key{crates[sel].id.str() + "_tok"});
            }
        }

        engine.tick(dt);  // states, portals, and the annex's anchor
        // The annex may have moved under the viewer; the walls push back either way.
        if (!map_open) sg::resolve_wall_collisions(world, cam, 0.35);

        // --- using a map ------------------------------------------------------------------
        const bool at_crate_map = sg::looking_at(world, "crate_map", 3.2, 0.5);
        const bool at_annex_map = sg::looking_at(world, "annex_map", 3.2, 0.5);
        if (!map_open && at_crate_map) view.highlight("crate_map");
        if (!map_open && at_annex_map) view.highlight("annex_map");

        if (window.pressed(GLFW_KEY_E)) {
            if (crate_open) {
                engine.close_embed("crate_map");
                window.capture_mouse(true);
            } else if (annex_open) {
                engine.close_embed("annex_map");
                window.capture_mouse(true);
            } else if (at_crate_map) {
                engine.open_embed("crate_map");
                crate_map.set_selection(sg::Key{crates[sel].id.str() + "_tok"});
                window.capture_mouse(false);
            } else if (at_annex_map) {
                engine.open_embed("annex_map");
                window.capture_mouse(false);
            }
        }
        if (map_open && window.pressed(GLFW_KEY_C)) {
            engine.close_embed(crate_open ? sg::Key{"crate_map"} : sg::Key{"annex_map"},
                               /*commit=*/false);
            window.capture_mouse(true);
        }

        // --- the nearer lamp -----------------------------------------------------------------
        const bool in_annex = cam.params.num(sg::keys::x) > kRoomW;
        sg::Element& near_lamp = in_annex ? annex_lamp : lamp;
        if (window.down(GLFW_KEY_Q) || window.down(GLFW_KEY_R)) {
            const double step = (window.down(GLFW_KEY_R) ? 3.5 : -3.5) * dt;
            near_lamp.params.set(sg::keys::x,
                                 clampd(near_lamp.params.num(sg::keys::x) + step, 0.6, 12.0));
        }
        if (window.pressed(GLFW_KEY_F)) {
            const double i = near_lamp.params.num(sg::keys::intensity, 1.0);
            near_lamp.params.set(sg::keys::intensity, i > 0.6 ? 0.3 : 1.1);
        }

        if (window.pressed(GLFW_KEY_ESCAPE)) {
            if (window.mouse_captured()) {
                window.capture_mouse(false);
            } else {
                window.close();
            }
        }

        view.render(world, window.width(), window.height());
        window.swap();

        if (max_frames > 0 && ++frames >= max_frames) break;
    }

    // Optional frame dump, for smoke tests and for the README images.
    if (argc > 2) {
        const int w = window.width(), h = window.height();
        std::vector<unsigned char> px(static_cast<std::size_t>(w) * h * 3);
        sg::gl::glReadPixels(0, 0, w, h, sg::gl::GL_RGB, sg::gl::GL_UNSIGNED_BYTE, px.data());
        if (FILE* f = std::fopen(argv[2], "wb")) {
            std::fprintf(f, "P6\n%d %d\n255\n", w, h);
            for (int y = h - 1; y >= 0; --y)  // GL reads bottom-up
                std::fwrite(px.data() + static_cast<std::size_t>(y) * w * 3, 1,
                            static_cast<std::size_t>(w) * 3, f);
            std::fclose(f);
            std::cout << "wrote " << argv[2] << " (" << w << "x" << h << ")\n";
        }
    }

    std::cout << "closed after " << frames << " frames\n";
    return 0;
}
