// Stategine example: two rooms, a map on the wall, and a doorway between them.
//
// Two interfaces, both built the same way - a state embedded in an element of
// another state, joined by functors:
//
//   the map    a 2D state on a wall panel. EmbedSync::Live, so moving a token
//              moves the crate it stands for, in the room, in the same frame.
//   the door   a 3D state seen through a doorway. EmbedSync::View, so every
//              frame the functor carries the viewer's pose into the other
//              room's camera - that is what makes the doorway a window. Walk
//              into it and the same functor runs as a transition, and you are
//              standing in the other room.
//
// The two rooms have their own coordinates; nothing lines them up but the
// portal transform.
//
//   move      W A S D      (in map mode: move the selected token)
//   look      mouse        (Esc releases the mouse, Esc again quits)
//   use map   E            while standing in front of it
//   select    Tab          cycle tokens while the map is open
//   cancel    C            close the map, discarding this session's edits
//   lamp      Q / R        slide the lamp        F  dim / brighten
//   doorway   just walk through it
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
constexpr double kRoomW = kCols * kCell;
constexpr double kRoomD = kRows * kCell;

constexpr double kLabW = 9.0;  // the second room is a different shape entirely
constexpr double kLabD = 16.0;

double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

double cell_to_world(double cell) { return cell * kCell + kCell * 0.5; }
double world_to_cell(double world) { return std::floor(world / kCell); }

struct Crate {
    sg::Key id;
    double cell_x, cell_z;
    double r, g, b;
    double height;
};

// Which doorway leads out of which room.
struct Door {
    sg::Key room;
    sg::Key element;
    sg::Key trigger;
};

}  // namespace

int main(int argc, char** argv) {
    // room3d [frames] [out.ppm] [shot]
    const long max_frames = argc > 1 ? std::strtol(argv[1], nullptr, 10) : 0;
    const std::string shot = argc > 3 ? argv[3] : std::string{};

    sg::StateGraph graph;

    // --- room one: crates, a lamp, a map ---------------------------------------
    auto& room = graph.add<sg::Spatial3D>("room");
    room.set_integrating(false);
    room.params().set("room_w", kRoomW).set("room_d", kRoomD).set("room_h", 4.0);

    const std::vector<Crate> crates{
        {"crate_a", 1, 1, 0.82, 0.34, 0.24, 1.1},
        {"crate_b", 5, 1, 0.26, 0.62, 0.86, 0.8},
        {"crate_c", 2, 4, 0.88, 0.74, 0.28, 1.4},
        {"crate_d", 5, 4, 0.42, 0.80, 0.45, 1.0},
        {"crate_e", 3, 2, 0.72, 0.42, 0.86, 0.7},
    };
    for (const auto& c : crates) {
        sg::Element& e = room.mesh(c.id, cell_to_world(c.cell_x), 0.0, cell_to_world(c.cell_z));
        e.params.set(sg::keys::sx, c.height * 1.05).set(sg::keys::sy, c.height)
            .set(sg::keys::sz, c.height * 1.05);
        e.params.set(sg::keys::r, c.r).set(sg::keys::g, c.g).set(sg::keys::b, c.b);
        e.params.set("roughness", 0.62).set(sg::keys::yaw, (c.cell_x + c.cell_z) * 0.21);
    }

    sg::Element& lamp = room.light("lamp", {kRoomW * 0.5, 3.05, kRoomD * 0.5});
    lamp.params.set(sg::keys::intensity, 1.0).set("inner", 0.5).set("outer", 1.25);

    room.portal("wall_map", {kRoomW * 0.5, 1.85, 0.14}, 3.2, 2.4, 0.0);
    // The doorway sits in the east wall, facing back into the room (-x).
    room.portal("door_to_lab", {kRoomW - 0.08, 1.5, kRoomD * 0.5}, 2.4, 3.0, -1.5707963);

    room.camera().params.set(sg::keys::x, kRoomW * 0.5).set(sg::keys::y, 1.7)
        .set(sg::keys::z, kRoomD - 1.5);
    room.camera().params.set(sg::keys::yaw, -1.5708).set(sg::keys::pitch, -0.03);

    // --- room two: a different space, with its own light and its own colours ---
    auto& lab = graph.add<sg::Spatial3D>("lab");
    lab.set_integrating(false);
    lab.params().set("room_w", kLabW).set("room_d", kLabD).set("room_h", 5.0);
    lab.params().set("floor_r", 0.20).set("floor_g", 0.26).set("floor_b", 0.30);
    lab.params().set("wall_r", 0.24).set("wall_g", 0.34).set("wall_b", 0.40);

    // Pillars down the length of it, alternating sides.
    for (int i = 0; i < 6; ++i) {
        const bool left = (i % 2) == 0;
        sg::Element& p = lab.mesh(sg::Key{"pillar_" + std::to_string(i)},
                                  left ? 2.0 : kLabW - 2.0, 0.0, 2.5 + i * 2.2);
        p.params.set(sg::keys::sx, 0.7).set(sg::keys::sy, 3.2 + (i % 3) * 0.4)
            .set(sg::keys::sz, 0.7);
        p.params.set(sg::keys::r, 0.30).set(sg::keys::g, 0.33).set(sg::keys::b, 0.38);
        p.params.set("roughness", 0.45);
    }
    // A single bright object at the far end, so the room reads as somewhere.
    sg::Element& monolith = lab.mesh("monolith", kLabW * 0.5, 0.0, kLabD - 2.6);
    monolith.params.set(sg::keys::sx, 1.2).set(sg::keys::sy, 2.6).set(sg::keys::sz, 0.5);
    monolith.params.set(sg::keys::r, 0.25).set(sg::keys::g, 0.85).set(sg::keys::b, 0.75);
    monolith.params.set("roughness", 0.2);

    sg::Element& lab_lamp = lab.light("lamp", {kLabW * 0.5, 4.1, kLabD * 0.45}, 0.72, 0.86, 1.0);
    lab_lamp.params.set(sg::keys::intensity, 1.15).set("inner", 0.6).set("outer", 1.35);

    // The matching doorway, in the short south wall, so stepping through puts
    // the whole length of the lab in front of you.
    lab.portal("door_to_room", {kLabW * 0.5, 1.5, 0.08}, 2.4, 3.0, 0.0);

    // --- the doorway, as a pair of functors ------------------------------------
    // portal_carry is the whole transform: rotate by the difference between the
    // two doorway yaws, translate into the far doorway's frame. One direction
    // aims the window; the same functor is what moves you when you walk in.
    graph.add_functor("peek_lab", "room", "lab")
        .on_object(sg::SpatialState::camera_id(), sg::SpatialState::camera_id(),
                   sg::portal_carry(room.element("door_to_lab"), lab.element("door_to_room")));
    graph.add_functor("peek_room", "lab", "room")
        .on_object(sg::SpatialState::camera_id(), sg::SpatialState::camera_id(),
                   sg::portal_carry(lab.element("door_to_room"), room.element("door_to_lab")));

    // View: `in` runs every frame, nothing comes back. The guest's camera is
    // the window's camera, so the renderer needs no portal maths at all.
    graph.embed("window_to_lab", "room", "door_to_lab", "lab", "peek_lab", sg::Key{},
                sg::EmbedSync::View);
    graph.embed("window_to_room", "lab", "door_to_room", "room", "peek_room", sg::Key{},
                sg::EmbedSync::View);

    // Walking through is the same transport, taken as a transition.
    graph.connect("room", "step_through", "lab").functor = "peek_lab";
    graph.connect("lab", "step_through", "room").functor = "peek_room";

    // --- the map on the wall of room one ----------------------------------------
    auto& map = graph.add<sg::Surface2D>("wallmap", kCols, kRows, 48);
    map.set_background(16, 20, 30);
    for (const auto& c : crates) {
        sg::Element& tok = map.sprite(sg::Key{c.id.str() + "_tok"}, c.cell_x, c.cell_z);
        tok.params.set(sg::keys::r, c.r).set(sg::keys::g, c.g).set(sg::keys::b, c.b);
    }
    map.set_selection(sg::Key{crates.front().id.str() + "_tok"});

    std::vector<std::pair<sg::Key, sg::Key>> objects;
    for (const auto& c : crates) objects.emplace_back(c.id, sg::Key{c.id.str() + "_tok"});
    graph.add_lens(
        "collapse", "stamp", "room", "wallmap", objects,
        sg::transport::swizzle_scaled({{sg::keys::x, sg::keys::x}, {sg::keys::y, sg::keys::z}},
                                      world_to_cell),
        sg::transport::swizzle_scaled({{sg::keys::x, sg::keys::x}, {sg::keys::z, sg::keys::y}},
                                      cell_to_world));
    graph.embed("wall_map", "room", "wall_map", "wallmap", "collapse", "stamp",
                sg::EmbedSync::Live);
    graph.set_initial("room");

    for (const auto& problem : graph.validate()) std::cout << "! " << problem << "\n";

    // --- window, renderer, loop ---------------------------------------------------
    sg::gl::Window window(1440, 810, "stategine - two rooms, a map and a doorway");
    window.capture_mouse(true);

    sg::render::GLWorldView view;
    view.bind_surface("wall_map", &map);
    view.bind_world("door_to_lab", &lab);
    view.bind_world("door_to_room", &room);

    sg::Engine engine(graph);
    engine.start();
    // A doorway is always a window: both portals stay open for their whole life.
    engine.open_embed("window_to_lab");
    engine.open_embed("window_to_room");

    const std::vector<Door> doors{{"room", "door_to_lab", "step_through"},
                                  {"lab", "door_to_room", "step_through"}};

    std::size_t sel = 0;
    double last = glfwGetTime();
    long frames = 0;

    // --- scripted poses, for the screenshots in the README ------------------------
    if (!shot.empty()) {
        window.capture_mouse(false);
        sg::Element& cam = room.camera();
        auto pose = [&cam](double x, double z, double yaw, double pitch) {
            cam.params.set(sg::keys::x, x).set(sg::keys::z, z);
            cam.params.set(sg::keys::yaw, yaw).set(sg::keys::pitch, pitch);
        };
        if (shot == "room") {
            pose(kRoomW - 1.2, kRoomD - 1.2, -2.25, -0.16);
        } else if (shot == "approach") {
            pose(kRoomW * 0.5 + 1.4, 4.6, -1.85, -0.05);
        } else if (shot == "open") {
            pose(kRoomW * 0.5, 2.6, -1.5708, -0.02);
            engine.open_embed("wall_map");
            map.set_selection(sg::Key{crates[2].id.str() + "_tok"});
        } else if (shot == "before" || shot == "after") {
            pose(0.9, 11.2, -0.52, -0.04);
            engine.open_embed("wall_map");
            map.set_selection(sg::Key{crates[2].id.str() + "_tok"});
            if (shot == "after")
                map.element(sg::Key{crates[2].id.str() + "_tok"}).params.set(sg::keys::x, 5.0);
        } else if (shot == "dim") {
            pose(kRoomW - 1.2, kRoomD - 1.2, -2.25, -0.16);
            lamp.params.set(sg::keys::intensity, 0.35);
        } else if (shot == "doorway") {
            pose(kRoomW - 4.2, kRoomD * 0.5, 0.0, -0.02);
        } else if (shot == "lab") {
            // Walk through first, then look back at the room from the far side.
            room.camera().params.set(sg::keys::x, kRoomW + 0.2)
                .set(sg::keys::z, kRoomD * 0.5).set(sg::keys::yaw, 0.0);
            engine.fire("step_through");
            engine.tick(0.016);
            lab.camera().params.set(sg::keys::x, kLabW * 0.5).set(sg::keys::z, 4.5)
                .set(sg::keys::yaw, 1.5708).set(sg::keys::pitch, -0.02);
        }
    }

    std::cout << "WASD move, mouse look, E to use the map, Tab to select, C to cancel, "
                 "Q/R lamp, F dim, walk into the doorway to change rooms, Esc for the mouse\n";

    while (!window.should_close() && engine.running()) {
        window.poll();
        const double now = glfwGetTime();
        const double dt = now - last;
        last = now;

        // Whichever room is on top of the stack is the one being played.
        auto* here = static_cast<sg::Spatial3D*>(engine.current());
        const bool in_room = here->id() == sg::Key{"room"};
        const bool map_open = engine.embed_open("wall_map") && in_room;
        sg::Element& cam = here->camera();

        // --- look ---------------------------------------------------------------
        if (window.mouse_captured() && !map_open) {
            const double sens = 0.0022;
            cam.params.set(sg::keys::yaw, cam.params.num(sg::keys::yaw) + window.mouse_dx() * sens);
            cam.params.set(sg::keys::pitch,
                           clampd(cam.params.num(sg::keys::pitch) - window.mouse_dy() * sens, -1.4,
                                  1.4));
        }

        // --- move, or drive the map ---------------------------------------------
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

            const sg::Vec3d from = sg::position_of(cam);
            const sg::Vec3d to{from.x + mx, from.y, from.z + mz};

            // Did that step take us through a doorway? Check before clamping to
            // the walls, or the wall would win.
            bool stepped = false;
            for (const Door& d : doors) {
                if (d.room != here->id()) continue;
                const sg::Element* door = here->find(d.element);
                if (door && sg::crossed_portal(*door, from, to)) {
                    sg::set_position(cam, to);   // just past the threshold
                    engine.fire(d.trigger);      // the functor does the rest
                    stepped = true;
                }
            }

            if (!stepped) {
                const double margin = 0.45;
                const double w = here->params().num("room_w", 14.0);
                const double d = here->params().num("room_d", 12.0);
                cam.params.set(sg::keys::x, clampd(to.x, margin, w - margin));
                cam.params.set(sg::keys::z, clampd(to.z, margin, d - margin));
            }
        } else {
            sg::Element& tok = map.element(sg::Key{crates[sel].id.str() + "_tok"});
            double dx = 0, dy = 0;
            if (window.pressed(GLFW_KEY_D)) dx += 1;
            if (window.pressed(GLFW_KEY_A)) dx -= 1;
            if (window.pressed(GLFW_KEY_S)) dy += 1;
            if (window.pressed(GLFW_KEY_W)) dy -= 1;
            if (dx != 0 || dy != 0) {
                tok.params.set(sg::keys::x, clampd(tok.params.num(sg::keys::x) + dx, 0, kCols - 1));
                tok.params.set(sg::keys::y, clampd(tok.params.num(sg::keys::y) + dy, 0, kRows - 1));
            }
            if (window.pressed(GLFW_KEY_TAB)) {
                sel = (sel + 1) % crates.size();
                map.set_selection(sg::Key{crates[sel].id.str() + "_tok"});
            }
        }

        // --- the map ---------------------------------------------------------------
        const bool in_reach = in_room && sg::looking_at(room, "wall_map", 3.4, 0.5);
        if (!map_open && in_reach) view.highlight("wall_map");

        if (window.pressed(GLFW_KEY_E)) {
            if (map_open) {
                engine.close_embed("wall_map");
                window.capture_mouse(true);
            } else if (in_reach) {
                engine.open_embed("wall_map");
                map.set_selection(sg::Key{crates[sel].id.str() + "_tok"});
                window.capture_mouse(false);
            }
        }
        if (map_open && window.pressed(GLFW_KEY_C)) {
            engine.close_embed("wall_map", /*commit=*/false);
            window.capture_mouse(true);
        }

        // --- the lamp of whichever room you are in -----------------------------------
        sg::Element* here_lamp = here->find("lamp");
        if (here_lamp && (window.down(GLFW_KEY_Q) || window.down(GLFW_KEY_R))) {
            const double step = (window.down(GLFW_KEY_R) ? 3.5 : -3.5) * dt;
            const double w = here->params().num("room_w", 14.0);
            here_lamp->params.set(sg::keys::x,
                                  clampd(here_lamp->params.num(sg::keys::x) + step, 0.6, w - 0.6));
        }
        if (here_lamp && window.pressed(GLFW_KEY_F)) {
            const double i = here_lamp->params.num(sg::keys::intensity, 1.0);
            here_lamp->params.set(sg::keys::intensity, i > 0.6 ? 0.35 : 1.15);
        }

        if (window.pressed(GLFW_KEY_ESCAPE)) {
            if (window.mouse_captured()) {
                window.capture_mouse(false);
            } else {
                window.close();
            }
        }

        engine.tick(dt);  // states, portals, and any transition fired above
        view.render(*static_cast<sg::Spatial3D*>(engine.current()), window.width(),
                    window.height());
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
