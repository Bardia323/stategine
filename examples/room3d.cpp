// Stategine example: a lit 3D room, crates on the floor, a map on the wall.
//
// Walk up to the map, press E, and the map takes focus: move a token on it and
// the crate it stands for slides across the real room in the same frame. The
// engine side of that is one lens (two functors) and one embed() call; the rest
// of this file is the room's contents and the controls.
//
//   move      W A S D      (in map mode: move the selected token)
//   look      mouse        (Esc releases the mouse, Esc again quits)
//   use map   E            while standing in front of it
//   select    Tab          cycle tokens while the map is open
//   cancel    C            close the map, discarding this session's edits
//   lamp      Q / R        slide the lamp along the room
//   light     F            dim / brighten
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

constexpr int kCols = 7;       // map cells across  -> room x
constexpr int kRows = 6;       // map cells down    -> room z
constexpr double kCell = 2.0;  // metres per map cell

double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

double cell_to_world(double cell) { return cell * kCell + kCell * 0.5; }
double world_to_cell(double world) { return std::floor(world / kCell); }

struct Crate {
    sg::Key id;
    double cell_x, cell_z;
    double r, g, b;
    double height;
};

}  // namespace

int main(int argc, char** argv) {
    // room3d [frames] [out.ppm] [shot]
    // A frame budget and a dump path make the example smoke-testable; `shot`
    // additionally poses the scene, so the README images are reproducible.
    const long max_frames = argc > 1 ? std::strtol(argv[1], nullptr, 10) : 0;
    const std::string shot = argc > 3 ? argv[3] : std::string{};

    sg::StateGraph graph;
    auto& room = graph.add<sg::Spatial3D>("room");
    room.set_integrating(false);
    room.params().set("room_w", kCols * kCell).set("room_d", kRows * kCell).set("room_h", 4.0);

    const std::vector<Crate> crates{
        {"crate_a", 1, 1, 0.82, 0.34, 0.24, 1.1},
        {"crate_b", 5, 1, 0.26, 0.62, 0.86, 0.8},
        {"crate_c", 2, 4, 0.88, 0.74, 0.28, 1.4},
        {"crate_d", 5, 4, 0.42, 0.80, 0.45, 1.0},
        {"crate_e", 3, 2, 0.72, 0.42, 0.86, 0.7},
    };
    for (const auto& c : crates) {
        sg::Element& e =
            room.mesh(c.id, cell_to_world(c.cell_x), 0.0, cell_to_world(c.cell_z));
        e.params.set(sg::keys::sx, c.height * 1.05).set(sg::keys::sy, c.height)
            .set(sg::keys::sz, c.height * 1.05);
        e.params.set(sg::keys::r, c.r).set(sg::keys::g, c.g).set(sg::keys::b, c.b);
        e.params.set("roughness", 0.62).set(sg::keys::yaw, (c.cell_x + c.cell_z) * 0.21);
    }

    // The lamp: an element like any other, so a morphism can drive it.
    sg::Element& lamp = room.light("lamp", {kCols * kCell * 0.5, 3.05, kRows * kCell * 0.5});
    lamp.params.set(sg::keys::intensity, 1.0).set("inner", 0.5).set("outer", 1.25);

    // The map hangs on the north wall (z = 0), facing into the room.
    room.portal("wall_map", {kCols * kCell * 0.5, 1.85, 0.14}, 3.2, 2.4, 0.0);

    // Camera starts at the far end, looking back at the wall with the map.
    room.camera().params.set(sg::keys::x, kCols * kCell * 0.5).set(sg::keys::y, 1.7)
        .set(sg::keys::z, kRows * kCell - 1.5);
    room.camera().params.set(sg::keys::yaw, -1.5708).set(sg::keys::pitch, -0.03);

    // --- the map, as a 2D surface state ---------------------------------------
    auto& map = graph.add<sg::Surface2D>("wallmap", kCols, kRows, 48);
    map.set_background(16, 20, 30);
    for (const auto& c : crates) {
        sg::Element& tok = map.sprite(sg::Key{c.id.str() + "_tok"}, c.cell_x, c.cell_z);
        tok.params.set(sg::keys::r, c.r).set(sg::keys::g, c.g).set(sg::keys::b, c.b);
    }
    map.set_selection(sg::Key{crates.front().id.str() + "_tok"});

    // --- the lens: room <-> map ------------------------------------------------
    // collapse drops the height axis and converts metres to cells; stamp does
    // the reverse and leaves the crate's height exactly as it was.
    std::vector<std::pair<sg::Key, sg::Key>> objects;
    for (const auto& c : crates) objects.emplace_back(c.id, sg::Key{c.id.str() + "_tok"});

    graph.add_lens(
        "collapse", "stamp", "room", "wallmap", objects,
        sg::transport::swizzle_scaled({{sg::keys::x, sg::keys::x}, {sg::keys::y, sg::keys::z}},
                                      world_to_cell),
        sg::transport::swizzle_scaled({{sg::keys::x, sg::keys::x}, {sg::keys::z, sg::keys::y}},
                                      cell_to_world));

    // One call hangs the interface inside the world. Live: edits land in the
    // room the frame they happen, with no confirmation step.
    graph.embed("wall_map", "room", "wall_map", "wallmap", "collapse", "stamp",
                sg::EmbedSync::Live);
    graph.set_initial("room");

    for (const auto& problem : graph.validate()) std::cout << "! " << problem << "\n";

    // --- window, renderer, loop --------------------------------------------------
    sg::gl::Window window(1440, 810, "stategine - room with a map on the wall");
    window.capture_mouse(true);

    sg::render::GLWorldView view;
    view.bind_surface("wall_map", &map);

    sg::Engine engine(graph);
    engine.start();

    std::size_t sel = 0;
    double last = glfwGetTime();
    long frames = 0;

    // --- scripted poses, for the screenshots in the README --------------------
    if (!shot.empty()) {
        window.capture_mouse(false);  // a posed shot must not drift with the mouse
        sg::Element& cam = room.camera();
        auto pose = [&cam](double x, double z, double yaw, double pitch) {
            cam.params.set(sg::keys::x, x).set(sg::keys::z, z);
            cam.params.set(sg::keys::yaw, yaw).set(sg::keys::pitch, pitch);
        };
        if (shot == "room") {
            pose(kCols * kCell - 1.2, kRows * kCell - 1.2, -2.25, -0.16);
        } else if (shot == "approach") {
            pose(kCols * kCell * 0.5 + 1.4, 4.6, -1.85, -0.05);
        } else if (shot == "open") {
            pose(kCols * kCell * 0.5, 2.6, -1.5708, -0.02);
            engine.open_embed("wall_map");
            map.set_selection(sg::Key{crates[2].id.str() + "_tok"});
        } else if (shot == "before" || shot == "after") {
            // The same camera either side of one edit on the map: the yellow
            // crate is at cell (2,4) in "before" and (5,4) in "after".
            pose(0.9, 11.2, -0.52, -0.04);
            engine.open_embed("wall_map");
            map.set_selection(sg::Key{crates[2].id.str() + "_tok"});
            if (shot == "after")
                map.element(sg::Key{crates[2].id.str() + "_tok"}).params.set(sg::keys::x, 5.0);
        } else if (shot == "dim") {
            pose(kCols * kCell - 1.2, kRows * kCell - 1.2, -2.25, -0.16);
            lamp.params.set(sg::keys::intensity, 0.35);
        }
    }

    std::cout << "WASD move, mouse look, E to use the map, Tab to select, C to cancel, "
                 "Q/R lamp, F dim, Esc to release the mouse\n";

    while (!window.should_close() && engine.running()) {
        window.poll();
        const double now = glfwGetTime();
        const double dt = now - last;
        last = now;

        const bool map_open = engine.embed_open("wall_map");
        sg::Element& cam = room.camera();

        // --- look -------------------------------------------------------------
        if (window.mouse_captured() && !map_open) {
            const double sens = 0.0022;
            cam.params.set(sg::keys::yaw, cam.params.num(sg::keys::yaw) + window.mouse_dx() * sens);
            cam.params.set(sg::keys::pitch,
                           clampd(cam.params.num(sg::keys::pitch) - window.mouse_dy() * sens, -1.4,
                                  1.4));
        }

        // --- move, or drive the map -------------------------------------------
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
            const double margin = 0.45;
            cam.params.set(sg::keys::x,
                           clampd(cam.params.num(sg::keys::x) + mx, margin, kCols * kCell - margin));
            cam.params.set(sg::keys::z,
                           clampd(cam.params.num(sg::keys::z) + mz, margin, kRows * kCell - margin));
        } else {
            // Map mode: the same keys push a token around the interface, and the
            // room follows because the portal syncs Live.
            sg::Element& tok = map.element(sg::Key{crates[sel].id.str() + "_tok"});
            double dx = 0, dy = 0;
            if (window.pressed(GLFW_KEY_D)) dx += 1;
            if (window.pressed(GLFW_KEY_A)) dx -= 1;
            if (window.pressed(GLFW_KEY_S)) dy += 1;
            if (window.pressed(GLFW_KEY_W)) dy -= 1;
            if (dx != 0 || dy != 0) {
                tok.params.set(sg::keys::x,
                               clampd(tok.params.num(sg::keys::x) + dx, 0, kCols - 1));
                tok.params.set(sg::keys::y,
                               clampd(tok.params.num(sg::keys::y) + dy, 0, kRows - 1));
            }
            if (window.pressed(GLFW_KEY_TAB)) {
                sel = (sel + 1) % crates.size();
                map.set_selection(sg::Key{crates[sel].id.str() + "_tok"});
            }
        }

        // --- use the map --------------------------------------------------------
        const bool in_reach = sg::looking_at(room, "wall_map", 3.4, 0.5);
        if (!map_open && in_reach) view.highlight("wall_map");

        if (window.pressed(GLFW_KEY_E)) {
            if (map_open) {
                engine.close_embed("wall_map");  // commit (Live already wrote)
                window.capture_mouse(true);
            } else if (in_reach) {
                engine.open_embed("wall_map");   // collapse: room -> map
                map.set_selection(sg::Key{crates[sel].id.str() + "_tok"});
                window.capture_mouse(false);
            }
        }
        if (map_open && window.pressed(GLFW_KEY_C)) {
            engine.close_embed("wall_map", /*commit=*/false);
            window.capture_mouse(true);
        }

        // --- the lamp ------------------------------------------------------------
        if (window.down(GLFW_KEY_Q) || window.down(GLFW_KEY_R)) {
            const double step = (window.down(GLFW_KEY_R) ? 3.5 : -3.5) * dt;
            lamp.params.set(sg::keys::x,
                            clampd(lamp.params.num(sg::keys::x) + step, 0.6, kCols * kCell - 0.6));
        }
        if (window.pressed(GLFW_KEY_F)) {
            const double i = lamp.params.num(sg::keys::intensity, 1.0);
            lamp.params.set(sg::keys::intensity, i > 0.6 ? 0.35 : 1.0);
        }

        if (window.pressed(GLFW_KEY_ESCAPE)) {
            if (window.mouse_captured()) {
                window.capture_mouse(false);
            } else {
                window.close();
            }
        }

        engine.tick(dt);                                  // states and portals
        view.render(room, window.width(), window.height());  // pixels
        window.swap();

        if (max_frames > 0 && ++frames >= max_frames) break;
    }

    // Optional frame dump: room3d <frames> <out.ppm>, for smoke tests and for
    // looking at what the renderer produced without a window.
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
