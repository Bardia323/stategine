// Stategine example: two rooms, no fixed point, and two maps built the same way.
//
// Neither room has a position. Each is its own state with its own coordinates,
// and the only thing relating them is a doorway: a portal element in each,
// glued in the Atlas. Where a room "is" is always an answer to "seen from
// where?" - the renderer roots the atlas at whichever room the viewer stands
// in and composes the doorway to place the other.
//
// The two interfaces are structurally identical:
//
//   crate map   token_i <-> crate_i        moving a token moves a crate
//                                          inside the hall.
//   door map    token   <-> the doorway    moving the token moves the doorway
//                                          around the hall's perimeter.
//
// Both are a Surface2D embedded Live through a lens onto elements of a room.
// The second one only *looks* bigger, because the annex hangs off the doorway
// it moves: from inside the annex nothing shifts at all, and it is the hall
// that swings round to meet a different edge. Stand in the hall instead and
// the same edit reads as the annex moving. Neither reading is privileged;
// that is the point.
//
// How each room is shown is a state as well: a LookState it wears. The hall
// has two and switches between them; the annex has one of its own, with its
// own composite shader. Walking through the doorway fades from one to the
// other, and every look is compiled before the first frame.
//
//   move      W A S D      (in map mode: move the selected token)
//   look      mouse        (Esc releases the mouse, Esc again quits)
//   use map   E            while standing in front of one
//   select    Tab          cycle tokens while the crate map is open
//   cancel    C            close the map, discarding this session's edits
//   lamp      Q / R        slide the lamp of the room you are in   F  dim
//   alarm     L            switch the hall between its calm and alert looks
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "sg/core/Sheaf.hpp"
#include "sg/domains/Atlas.hpp"
#include "sg/gl/Window.hpp"
#include "sg/render/GLWorld.hpp"
#include "sg/sg.hpp"

namespace {

constexpr double kHallW = 14.0;   // the hall, in its own coordinates
constexpr double kHallD = 14.0;
constexpr double kHallH = 4.0;
constexpr double kAnnexW = 9.0;   // the annex, in its own
constexpr double kAnnexD = 11.0;
constexpr double kAnnexH = 3.6;

constexpr double kWallT = 0.3;
constexpr double kGapHalf = 1.4;  // half the doorway's width

// The annex's own composite pass: a slight colour fringe towards the edges and
// faint moving scanlines, over the same tone curve as the hall's. A look only
// has to declare the uniforms it reads; the renderer checks, before the first
// frame, that it still has the ones the renderer sets.
const char* kAnnexComposite = R"(#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform float uBloomStrength;
uniform float uExposure;
uniform vec2  uTexel;
uniform float uTime;

vec3 aces(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec2 d = vUV - 0.5;
    vec2 shift = d * dot(d, d) * 0.02;
    vec3 scene = vec3(texture(uScene, vUV + shift).r, texture(uScene, vUV).g,
                      texture(uScene, vUV - shift).b);
    scene += texture(uBloom, vUV).rgb * uBloomStrength;
    vec3 color = aces(scene * uExposure);
    color *= 0.975 + 0.025 * sin(vUV.y / uTexel.y * 1.6 + uTime * 3.0);
    color *= 1.0 - dot(d, d) * 0.75;
    FragColor = vec4(pow(max(color, vec3(0.0)), vec3(1.0 / 2.2)), 1.0);
})";

constexpr int kCols = 6;       // crate map cells
constexpr int kRows = 6;
constexpr double kCell = 2.0;  // metres per crate-map cell

// The door map is a ring of cells: three stations per side of the hall, read
// clockwise from the north-west corner.
constexpr int kStations = 12;

double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

double cell_to_world(double cell) { return cell * kCell + kCell * 0.5; }
double world_to_cell(double world) { return std::floor(world / kCell); }

struct Crate {
    sg::Key id;
    double cell_x, cell_z;
    double r, g, b;
    double height;
};

// --- the hall's perimeter, as one parameter ------------------------------------
// Station -> the doorway's pose on the hall's wall, facing into the hall. This
// is the only place the hall's shape enters: everything else just moves a
// number around a ring.
sg::Pose station_pose(double station) {
    const int s = static_cast<int>(std::lround(station)) % kStations;
    const int side = s / 3;           // 0 north, 1 east, 2 south, 3 west
    const double t = (s % 3 + 1) / 4.0;  // quarter, half, three-quarters along
    switch (side) {
        case 0: return {{kHallW * t, 0.0, 0.0}, 1.5707963};              // faces +z
        case 1: return {{kHallW, 0.0, kHallD * t}, 3.14159265};          // faces -x
        case 2: return {{kHallW * (1.0 - t), 0.0, kHallD}, -1.5707963};  // faces -z
        default: return {{0.0, 0.0, kHallD * (1.0 - t)}, 0.0};           // faces +x
    }
}

// The door map is a picture of the hall's outline: a 5x5 board whose twelve
// non-corner border cells are the twelve stations, three to a side. That is
// not decoration - the stations form a ring, and the border of a grid is a
// ring, so cells that are neighbours on the map are neighbours on the wall.
// A rectangle of slots would have had interior cells that are nowhere, and
// edges that run off one row onto the next.
constexpr int kMapSide = 5;

struct Cell {
    int x, y;
};

Cell station_cell(double station) {
    const int st = ((static_cast<int>(std::lround(station)) % kStations) + kStations) % kStations;
    const int side = st / 3, i = st % 3;
    switch (side) {
        case 0: return {i + 1, 0};                  // north, left to right
        case 1: return {kMapSide - 1, i + 1};       // east, top to bottom
        case 2: return {kMapSide - 2 - i, kMapSide - 1};  // south, right to left
        default: return {0, kMapSide - 2 - i};      // west, bottom to top
    }
}

// Only the twelve border cells are stations; corners are where walls meet and
// the middle is the room itself. Anything else is not a place a door can be.
bool cell_station(int cx, int cy, double& station) {
    const int last = kMapSide - 1;
    if (cy == 0 && cx >= 1 && cx <= last - 1) {
        station = cx - 1;
    } else if (cx == last && cy >= 1 && cy <= last - 1) {
        station = 3 + (cy - 1);
    } else if (cy == last && cx >= 1 && cx <= last - 1) {
        station = 6 + (last - 1 - cx);
    } else if (cx == 0 && cy >= 1 && cy <= last - 1) {
        station = 9 + (last - 1 - cy);
    } else {
        return false;
    }
    return true;
}

// The hall's perimeter walls are rebuilt around wherever the doorway is: on
// the side that holds it, two segments and a lintel; on the others, one solid
// run. Called from a morphism, so the wall segments really are downstream of
// the door element.
void fit_walls_to_door(sg::Spatial3D& hall, double station) {
    const int s = static_cast<int>(std::lround(station)) % kStations;
    const int door_side = s / 3;
    const sg::Pose d = station_pose(s);

    struct Side {
        sg::Key seg_a, seg_b, lintel;
        bool horizontal;  // runs along x
        double fixed;     // z for horizontal sides, x for vertical ones
        double length;
    };
    const Side sides[4] = {
        {"n_a", "n_b", "n_top", true, 0.0, kHallW},
        {"e_a", "e_b", "e_top", false, kHallW, kHallD},
        {"s_a", "s_b", "s_top", true, kHallD, kHallW},
        {"w_a", "w_b", "w_top", false, 0.0, kHallD},
    };

    for (int i = 0; i < 4; ++i) {
        const Side& side = sides[i];
        // Where along this side the doorway sits, if it is on this side at all.
        const double along = side.horizontal ? d.position.x : d.position.z;
        const bool has_door = (i == door_side);
        const double lo = has_door ? along - kGapHalf : side.length;
        const double hi = has_door ? along + kGapHalf : side.length;

        // A wall stands inside the hall, its outer face on the perimeter. Any
        // side may hold the doorway, and a doorway's plane is where the hall
        // ends and the next room begins: the two are adjacent there, never
        // overlapping, or each room's look shows through on the other's wall.
        const double inward = side.fixed > 0.0 ? -kWallT * 0.5 : kWallT * 0.5;
        auto place = [&](sg::Key id, double from, double to, double base, double height) {
            sg::Element& e = hall.element(id);
            const double len = std::max(0.0, to - from);
            const double mid = (from + to) * 0.5;
            if (side.horizontal) {
                sg::set_position(e, {mid, base, side.fixed + inward});
                e.params.set(sg::keys::sx, len).set(sg::keys::sz, kWallT);
            } else {
                sg::set_position(e, {side.fixed + inward, base, mid});
                e.params.set(sg::keys::sx, kWallT).set(sg::keys::sz, len);
            }
            e.params.set(sg::keys::sy, height);
            e.alive = len > 1e-6;
        };

        place(side.seg_a, 0.0, lo, 0.0, kHallH);
        place(side.seg_b, hi, side.length, 0.0, kHallH);
        place(side.lintel, lo, hi, 3.0, kHallH - 3.0);
        hall.element(side.lintel).alive = has_door;
    }
}

}  // namespace

int main(int argc, char** argv) {
    // room3d [frames] [out.ppm] [shot]
    const long max_frames = argc > 1 ? std::strtol(argv[1], nullptr, 10) : 0;
    const std::string shot = argc > 3 ? argv[3] : std::string{};

    sg::StateGraph graph;

    // --- the hall ----------------------------------------------------------------
    auto& hall = graph.add<sg::Spatial3D>("hall");
    hall.set_integrating(false);
    hall.params().set("room_w", kHallW).set("room_d", kHallD).set("room_h", kHallH);

    for (const char* id : {"n_a", "n_b", "n_top", "e_a", "e_b", "e_top", "s_a", "s_b", "s_top",
                           "w_a", "w_b", "w_top"})
        hall.wall(sg::Key{id}, {0, 0, 0}, 1, kHallH, 1);

    const std::vector<Crate> crates{
        {"crate_a", 1, 1, 0.82, 0.34, 0.24, 1.1},
        {"crate_b", 4, 1, 0.26, 0.62, 0.86, 0.8},
        {"crate_c", 1, 4, 0.88, 0.74, 0.28, 1.4},
        {"crate_d", 4, 4, 0.42, 0.80, 0.45, 1.0},
        {"crate_e", 2, 2, 0.72, 0.42, 0.86, 0.7},
    };
    for (const auto& c : crates) {
        sg::Element& e = hall.mesh(c.id, cell_to_world(c.cell_x), 0.0, cell_to_world(c.cell_z));
        e.params.set(sg::keys::sx, c.height * 1.05).set(sg::keys::sy, c.height)
            .set(sg::keys::sz, c.height * 1.05);
        e.params.set(sg::keys::r, c.r).set(sg::keys::g, c.g).set(sg::keys::b, c.b);
        e.params.set("roughness", 0.62).set(sg::keys::yaw, (c.cell_x + c.cell_z) * 0.21);
    }

    sg::Element& hall_lamp = hall.light("lamp", {kHallW * 0.5, 3.05, kHallD * 0.5});
    hall_lamp.params.set(sg::keys::intensity, 1.0).set("inner", 0.5).set("outer", 1.25);

    hall.portal("crate_map", {kHallW * 0.5, 1.85, 0.3}, 3.0, 2.2, 1.5707963);  // faces +z

    // The doorway, as an element of the hall like any other. It starts at
    // station 4: the middle of the east wall.
    sg::Element& door = hall.portal("door", station_pose(4).position, kGapHalf * 2, 3.0,
                                    station_pose(4).yaw);
    door.params.set("station", 4.0);
    // door --moved--> the wall segments. The walls are downstream of the door,
    // so the opening is wherever the door is, by construction.
    for (const char* seg : {"n_a", "n_b", "n_top", "e_a", "e_b", "e_top", "s_a", "s_b", "s_top",
                            "w_a", "w_b", "w_top"}) {
        hall.arrow(sg::Key{std::string("fit.") + seg}, "door", sg::Key{seg}, "door.moved",
                   [](sg::State& s, sg::Element& d, sg::Element*, const sg::Event&) {
                       fit_walls_to_door(static_cast<sg::Spatial3D&>(s), d.params.num("station"));
                   });
    }

    // --- the annex ----------------------------------------------------------------
    auto& annex = graph.add<sg::Spatial3D>("annex");
    annex.set_integrating(false);
    annex.params().set("room_w", kAnnexW).set("room_d", kAnnexD).set("room_h", kAnnexH);
    annex.params().set("floor_r", 0.22).set("floor_g", 0.28).set("floor_b", 0.32);
    annex.params().set("wall_r", 0.26).set("wall_g", 0.36).set("wall_b", 0.42);

    // Its own doorway sits in the middle of its south wall, and never moves.
    // That wall stands inside the annex, its outer face on the doorway plane:
    // the hall's wall meets it there from the other side.
    const double a_gap = kAnnexW * 0.5;
    const double a_south = kWallT * 0.5;
    annex.wall("a_south_a", {(a_gap - kGapHalf) * 0.5, 0.0, a_south}, a_gap - kGapHalf, kAnnexH,
               kWallT);
    annex.wall("a_south_b", {(a_gap + kGapHalf + kAnnexW) * 0.5, 0.0, a_south},
               kAnnexW - a_gap - kGapHalf, kAnnexH, kWallT);
    annex.wall("a_south_top", {a_gap, 3.0, a_south}, kGapHalf * 2, kAnnexH - 3.0, kWallT);
    annex.wall("a_north", {kAnnexW * 0.5, 0.0, kAnnexD}, kAnnexW + kWallT, kAnnexH, kWallT);
    annex.wall("a_west", {0.0, 0.0, kAnnexD * 0.5}, kWallT, kAnnexH, kAnnexD);
    annex.wall("a_east", {kAnnexW, 0.0, kAnnexD * 0.5}, kWallT, kAnnexH, kAnnexD);
    annex.portal("door", {a_gap, 1.5, 0.0}, kGapHalf * 2, 3.0, 1.5707963);  // faces +z, inward

    for (int i = 0; i < 4; ++i) {
        sg::Element& p = annex.mesh(sg::Key{"plinth_" + std::to_string(i)},
                                    i % 2 == 0 ? 2.0 : kAnnexW - 2.0, 0.0, 3.0 + (i / 2) * 3.4);
        p.params.set(sg::keys::sx, 0.9).set(sg::keys::sy, 1.5 + (i % 3) * 0.4)
            .set(sg::keys::sz, 0.9);
        p.params.set(sg::keys::r, 0.30).set(sg::keys::g, 0.55).set(sg::keys::b, 0.62);
        p.params.set("roughness", 0.4);
    }
    sg::Element& monolith = annex.mesh("monolith", 2.0, 0.0, kAnnexD - 2.4);
    monolith.params.set(sg::keys::sx, 1.4).set(sg::keys::sy, 2.4).set(sg::keys::sz, 0.5);
    monolith.params.set(sg::keys::r, 0.25).set(sg::keys::g, 0.85).set(sg::keys::b, 0.75);
    monolith.params.set("roughness", 0.2);

    sg::Element& annex_lamp =
        annex.light("lamp", {kAnnexW * 0.5, 2.9, kAnnexD * 0.5}, 0.72, 0.86, 1.0);
    annex_lamp.params.set(sg::keys::intensity, 1.1).set("inner", 0.6).set("outer", 1.3);

    // The door map hangs on the annex's north wall, facing back down the room.
    annex.portal("door_map", {kAnnexW * 0.5, 1.8, kAnnexD - 0.3}, 2.8, 2.1, -1.5707963);

    // --- the looks -------------------------------------------------------------------
    // A look states only how it differs from the standard one. The hall wears
    // two and starts calm; the annex wears one, with a shader of its own.
    auto& calm = graph.add<sg::LookState>("hall.calm");
    calm.uniform(sg::passes::scene, "uFogColor", 0.07, 0.06, 0.05).fade(0.8);

    auto& alert = graph.add<sg::LookState>("hall.alert");
    alert.uniform(sg::passes::scene, "uFogColor", 0.16, 0.03, 0.02)
        .uniform(sg::passes::scene, "uFogDensity", 0.035)
        .uniform(sg::passes::composite, "uTint", 1.3, 0.62, 0.55)
        .uniform(sg::passes::composite, "uSaturation", 0.55)
        .uniform(sg::passes::composite, "uExposure", 1.3)
        .fade(0.35);

    auto& cool = graph.add<sg::LookState>("annex.cool");
    cool.uniform(sg::passes::scene, "uFogColor", 0.03, 0.06, 0.10)
        .uniform(sg::passes::scene, "uFogDensity", 0.03)
        .uniform(sg::passes::scene, "uSky", 0.08, 0.14, 0.26)
        .uniform(sg::passes::composite, "uExposure", 1.05)
        .uniform(sg::passes::composite, "uBloomStrength", 0.8)
        .shader(sg::passes::composite, kAnnexComposite)
        .fade(0.7);

    sg::wear(graph, "hall", "hall.calm");
    sg::wear(graph, "hall", "hall.alert");
    sg::wear(graph, "annex", "annex.cool");

    // --- the glue ------------------------------------------------------------------
    // One doorway, named from both sides. This is the whole relation between
    // the two rooms; neither owns the other, and neither has a position.
    sg::Atlas atlas;
    atlas.glue("doorway", "hall", "door", "annex", "door");

    // The atlas is an instance of a cover, and `as_cover` derives the doorway's
    // transition functors from the two portal elements. Walking through is that
    // same relation taken as a transition: the camera is re-expressed in the
    // other room's coordinates and nothing else changes. Because the functors
    // are derived rather than declared, moving the doorway cannot leave them
    // saying something the geometry no longer does.
    sg::as_cover(atlas, graph);
    graph.connect("hall", "step_through", "annex", "doorway.ab");
    graph.connect("annex", "step_through", "hall", "doorway.ba");

    // --- interface one: the crate map -------------------------------------------------
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
        "crates_to_map", "map_to_crates", "hall", "cratemap", crate_objects,
        sg::transport::swizzle_scaled({{sg::keys::x, sg::keys::x}, {sg::keys::y, sg::keys::z}},
                                      world_to_cell),
        sg::transport::swizzle_scaled({{sg::keys::x, sg::keys::x}, {sg::keys::z, sg::keys::y}},
                                      cell_to_world));
    graph.embed("crate_map", "hall", "crate_map", "cratemap", "crates_to_map", "map_to_crates",
                sg::EmbedSync::Live);

    // --- interface two: the door map ---------------------------------------------------
    // The same shape exactly: a Surface2D, a lens onto one element of a room,
    // embedded Live. The element happens to be the doorway, and the annex is
    // related to the hall through it, so moving this token rearranges which
    // edge of the hall the annex meets - without the annex's own coordinates
    // changing by a millimetre.
    auto& door_map = graph.add<sg::Surface2D>("doormap", kMapSide, kMapSide, 40);
    door_map.set_background(18, 15, 13);

    // The board: the hall's floor in the middle, wall joints at the corners,
    // and a slot for every place the doorway can sit.
    for (int cy = 0; cy < kMapSide; ++cy) {
        for (int cx = 0; cx < kMapSide; ++cx) {
            double st = 0;
            const bool slot = cell_station(cx, cy, st);
            const bool border = (cx == 0 || cy == 0 || cx == kMapSide - 1 || cy == kMapSide - 1);
            sg::Element& t = door_map.add_element(
                sg::Key{"tile_" + std::to_string(cx) + "_" + std::to_string(cy)}, sg::kinds::tile);
            t.params.set(sg::keys::x, static_cast<double>(cx));
            t.params.set(sg::keys::y, static_cast<double>(cy));
            if (slot) {
                t.params.set(sg::keys::r, 0.36).set(sg::keys::g, 0.30).set(sg::keys::b, 0.22);
            } else if (border) {
                t.params.set(sg::keys::r, 0.13).set(sg::keys::g, 0.12).set(sg::keys::b, 0.11);
            } else {
                t.params.set(sg::keys::r, 0.19).set(sg::keys::g, 0.20).set(sg::keys::b, 0.23);
            }
        }
    }

    const Cell start_cell = station_cell(4);
    sg::Element& door_tok = door_map.sprite("door_tok", start_cell.x, start_cell.y);
    door_tok.params.set(sg::keys::r, 0.95).set(sg::keys::g, 0.72).set(sg::keys::b, 0.3);
    door_map.set_selection("door_tok");

    graph.add_lens(
        "door_to_map", "map_to_door", "hall", "doormap", {{"door", "door_tok"}},
        [](const sg::Element& d, sg::Element& tok) {
            const Cell c = station_cell(d.params.num("station"));
            tok.params.set(sg::keys::x, static_cast<double>(c.x));
            tok.params.set(sg::keys::y, static_cast<double>(c.y));
        },
        [](const sg::Element& tok, sg::Element& d) {
            double st = 0;
            if (!cell_station(static_cast<int>(std::lround(tok.params.num(sg::keys::x))),
                              static_cast<int>(std::lround(tok.params.num(sg::keys::y))), st))
                return;  // not a place a doorway can be
            if (std::fabs(st - d.params.num("station")) < 1e-9) return;
            d.params.set("station", st);
            const sg::Pose p = station_pose(st);
            sg::set_position(d, p.position);
            d.params.set(sg::keys::yaw, p.yaw);
        });
    // Mounted in the annex, acting on the hall: the panel hangs where you use
    // it, and the element it edits lives where it lives.
    graph.embed("door_map", "annex", "door_map", "doormap", "door_to_map", "map_to_door",
                sg::EmbedSync::Live, /*subject=*/"hall");
    graph.set_initial("hall");

    // What the player does is fired as events and done by arrows, so the
    // loop below only says what was pressed. Walking and looking are the
    // camera's own arrow in each room: it turns, steps, and keeps out of the
    // room's walls.
    for (sg::Spatial3D* room : {&hall, &annex})
        room->loop("walk", sg::SpatialState::camera_id(), "walk",
                   [](sg::State& s, sg::Element& cam, sg::Element*, const sg::Event& ev) {
                       cam.params.set(sg::keys::yaw, cam.params.num(sg::keys::yaw) + ev.args.num("turn"));
                       cam.params.set(sg::keys::pitch,
                                      clampd(cam.params.num(sg::keys::pitch) - ev.args.num("tilt"), -1.4, 1.4));
                       const sg::Vec3d at = sg::position_of(cam);
                       sg::set_position(cam, {at.x + ev.args.num("dx"), at.y, at.z + ev.args.num("dz")});
                       sg::resolve_wall_collisions(s, cam, 0.35, 1.9);
                   });
    // Each room's lamp slides along the room and dims by its own arrows, and
    // the hall's alarm is its look changing - one parameter, by an arrow.
    for (sg::Spatial3D* room : {&hall, &annex}) {
        room->loop("lamp.slide", "lamp", "lamp.slide",
                   [](sg::State& s, sg::Element& lamp, sg::Element*, const sg::Event& ev) {
                       const double w = s.params().num("room_w", 14.0);
                       lamp.params.set(sg::keys::x, clampd(lamp.params.num(sg::keys::x) + ev.args.num("dx"), 0.8, w - 0.8));
                   });
        room->loop("lamp.dim", "lamp", "lamp.dim", [](sg::State&, sg::Element& lamp, sg::Element*, const sg::Event&) {
            const double i = lamp.params.num(sg::keys::intensity, 1.0);
            lamp.params.set(sg::keys::intensity, i > 0.6 ? 0.3 : 1.1);
        });
    }
    hall.loop("alarm", sg::look_slot_id(), "alarm", [](sg::State& s, sg::Element&, sg::Element*, const sg::Event&) {
        sg::set_look(s, sg::active_look(s) == sg::Key{"hall.calm"} ? "hall.alert" : "hall.calm");
    });

    // A map's token moves by the map's arrow: the crate map's on its grid,
    // the door map's round its ring.
    for (const Crate& c : crates) {
        const sg::Key tok{c.id.str() + "_tok"};
        crate_map.loop(sg::Key{"nudge." + tok.str()}, tok, sg::Key{"nudge." + tok.str()},
                       [](sg::State&, sg::Element& t, sg::Element*, const sg::Event& ev) {
                           t.params.set(sg::keys::x, clampd(t.params.num(sg::keys::x) + ev.args.num("dx"), 0, kCols - 1));
                           t.params.set(sg::keys::y, clampd(t.params.num(sg::keys::y) + ev.args.num("dy"), 0, kRows - 1));
                       });
    }
    door_map.loop("ring_step", "door_tok", "ring_step",
                  [](sg::State&, sg::Element& t, sg::Element*, const sg::Event& ev) {
                      double st = 0;
                      if (!cell_station(static_cast<int>(std::lround(t.params.num(sg::keys::x))),
                                        static_cast<int>(std::lround(t.params.num(sg::keys::y))), st))
                          return;
                      const Cell c = station_cell(st + ev.args.num("step"));
                      t.params.set(sg::keys::x, static_cast<double>(c.x));
                      t.params.set(sg::keys::y, static_cast<double>(c.y));
                  });

    // Structure, then every law on the data as it stands - checked and undone.
    const sg::LawReport laws = sg::verify(graph);
    if (!laws.ok()) std::cout << laws.str();

    // Fit the hall's walls to wherever the door starts out.
    hall.emit("door.moved");
    hall.dispatch_pending();

    // The rooms glue into one space: the doorway closes up, and the two rooms
    // meet along it without either reaching into the other.
    for (const auto& seam : sg::descent_defects(atlas, graph)) std::cout << "seam: " << seam << "\n";

    hall.camera().params.set(sg::keys::x, kHallW * 0.5).set(sg::keys::y, 1.7)
        .set(sg::keys::z, kHallD * 0.5);
    hall.camera().params.set(sg::keys::yaw, 0.0).set(sg::keys::pitch, -0.02);
    annex.camera().params.set(sg::keys::x, kAnnexW * 0.5).set(sg::keys::y, 1.7)
        .set(sg::keys::z, 2.5);
    annex.camera().params.set(sg::keys::yaw, 1.5707963).set(sg::keys::pitch, -0.02);

    // --- window, renderer, loop ----------------------------------------------------------
    sg::gl::Window window(1440, 810, "stategine - two rooms, no fixed point");
    window.capture_mouse(true);

    sg::render::GLWorldView view;
    view.bind_surface("crate_map", &crate_map);
    view.bind_surface("door_map", &door_map);

    // Every look the graph can reach, compiled and checked now rather than
    // the first time someone walks into a room that wears it.
    for (const auto& problem : view.prepare(graph)) std::cout << "! " << problem << "\n";
    std::cout << "looks: " << view.stats().programs << " programs compiled in "
              << view.stats().compile_ms << " ms\n";

    sg::Engine engine(graph);
    engine.start();

    std::size_t sel = 0;
    double last = glfwGetTime();
    long frames = 0;

    if (!shot.empty()) {
        window.capture_mouse(false);
        view.set_fixed_step(1.0 / 60.0);  // a shot shows the same moment every time
        auto pose_in = [&](sg::Spatial3D& room, double x, double z, double yaw, double pitch) {
            room.camera().params.set(sg::keys::x, x).set(sg::keys::z, z);
            room.camera().params.set(sg::keys::yaw, yaw).set(sg::keys::pitch, pitch);
        };
        auto go_to_annex = [&] {
            engine.fire("step_through");
            engine.tick(0.016);
        };
        // Open first, then set the token: opening runs `in`, which would
        // otherwise read the token straight back off the door.
        auto set_station = [&](double st) {
            engine.open_embed("door_map");
            door_map.element("door_tok").params.set(sg::keys::x, std::fmod(st, 4.0));
            door_map.element("door_tok").params.set(sg::keys::y, std::floor(st / 4.0));
        };

        if (shot == "room") {
            pose_in(hall, kHallW - 1.4, kHallD - 1.4, -2.3, -0.14);
        } else if (shot == "approach") {
            pose_in(hall, kHallW * 0.5 + 1.3, 4.0, -1.9, -0.05);
        } else if (shot == "open") {
            pose_in(hall, kHallW * 0.5, 2.6, -1.5708, -0.02);
            engine.open_embed("crate_map");
            crate_map.set_selection(sg::Key{crates[2].id.str() + "_tok"});
        } else if (shot == "before" || shot == "after") {
            pose_in(hall, 1.2, 12.4, -0.6, -0.04);
            engine.open_embed("crate_map");
            crate_map.set_selection(sg::Key{crates[2].id.str() + "_tok"});
            if (shot == "after")
                crate_map.element(sg::Key{crates[2].id.str() + "_tok"})
                    .params.set(sg::keys::x, 4.0);
        } else if (shot == "crossing") {
            // Just short of the doorway; the loop steps through part way into
            // the run, so the last frame is caught half way between the looks.
            pose_in(hall, kHallW - 1.2, kHallD * 0.5, 0.0, -0.02);
        } else if (shot == "alert") {
            pose_in(hall, kHallW - 1.4, kHallD - 1.4, -2.3, -0.14);
            sg::set_look(hall, "hall.alert");
        } else if (shot == "dim") {
            pose_in(hall, kHallW - 1.4, kHallD - 1.4, -2.3, -0.14);
            hall_lamp.params.set(sg::keys::intensity, 0.35);
        } else if (shot == "doorway") {
            pose_in(hall, kHallW - 5.5, kHallD * 0.5, 0.0, -0.02);
        } else if (shot == "annex") {
            go_to_annex();
            pose_in(annex, kAnnexW * 0.5, kAnnexD - 2.8, 1.5707963, -0.02);
        } else if (shot == "east" || shot == "south" || shot == "north") {
            // The same view from inside the annex, looking back through its
            // doorway, with the hall met along a different edge each time.
            go_to_annex();
            pose_in(annex, kAnnexW * 0.5, 3.4, -1.5707963, -0.03);
            set_station(shot == "east" ? 4 : (shot == "south" ? 7 : 1));
        }
    }

    std::cout << "WASD move, mouse look, E to use a map, Tab to select, C to cancel, "
                 "Q/R lamp, F dim, L alarm, Esc for the mouse\n";

    while (!window.should_close() && engine.running()) {
        window.poll();
        const double now = glfwGetTime();
        const double dt = now - last;
        last = now;

        // Watched, not touched: what the player does goes in as events.
        const auto* here = static_cast<const sg::Spatial3D*>(engine.current());
        const bool in_hall = here->id() == sg::Key{"hall"};
        const bool crate_open = engine.embed_open("crate_map");
        const bool door_open = engine.embed_open("door_map");
        const bool map_open = crate_open || door_open;
        const sg::Element& cam = here->camera();

        // Everything is placed from where the viewer stands. Nothing else has
        // a position to be placed from.
        std::vector<sg::PlacedRoom> rooms = sg::place_rooms(graph, atlas, here->id());

        double turn = 0, tilt = 0;
        if (window.mouse_captured() && !map_open) {
            const double sens = 0.0022;
            turn = window.mouse_dx() * sens;
            tilt = window.mouse_dy() * sens;
        }

        if (!map_open) {
            const sg::Vec3d f = sg::forward_of(cam);
            const double len = std::sqrt(f.x * f.x + f.z * f.z);
            const double fx = len > 1e-6 ? f.x / len : 0.0;
            const double fz = len > 1e-6 ? f.z / len : 1.0;
            const double rx = -fz, rz = fx;  // right = forward x up
            const double speed = (window.down(GLFW_KEY_LEFT_SHIFT) ? 6.5 : 3.2) * dt;
            double mx = 0, mz = 0;
            if (window.down(GLFW_KEY_W)) { mx += fx * speed; mz += fz * speed; }
            if (window.down(GLFW_KEY_S)) { mx -= fx * speed; mz -= fz * speed; }
            if (window.down(GLFW_KEY_D)) { mx += rx * speed; mz += rz * speed; }
            if (window.down(GLFW_KEY_A)) { mx -= rx * speed; mz -= rz * speed; }

            const sg::Vec3d from = sg::position_of(cam);
            const sg::Vec3d to{from.x + mx, from.y, from.z + mz};
            if (mx != 0 || mz != 0 || turn != 0 || tilt != 0)
                engine.fire(sg::Event{"walk", sg::Params{}.set("dx", mx).set("dz", mz).set("turn", turn).set("tilt", tilt)});

            // Crossing the doorway swaps which room is the root. The camera is
            // carried by the same arrow that places the rooms, so the view does
            // not jump - only the coordinates it is written in change.
            if (const sg::Element* d = here->find("door")) {
                if (sg::crossed_portal(*d, from, to)) engine.fire("step_through");
            }
        } else if (crate_open) {
            // The crate map is a floor plan, so its token moves on a grid.
            double dx = 0, dy = 0;
            if (window.pressed(GLFW_KEY_D)) dx += 1;
            if (window.pressed(GLFW_KEY_A)) dx -= 1;
            if (window.pressed(GLFW_KEY_S)) dy += 1;
            if (window.pressed(GLFW_KEY_W)) dy -= 1;
            if (dx != 0 || dy != 0)
                engine.fire(sg::Event{sg::Key{"nudge." + crates[sel].id.str() + "_tok"},
                                      sg::Params{}.set("dx", dx).set("dy", dy)});
            if (window.pressed(GLFW_KEY_TAB)) {
                sel = (sel + 1) % crates.size();
                crate_map.set_selection(sg::Key{crates[sel].id.str() + "_tok"});
            }
        } else {
            // The door map is a ring, so its token steps around it. There is
            // nowhere else for a doorway to be, and an interface should not
            // offer moves its subject cannot make.
            double step = 0;
            if (window.pressed(GLFW_KEY_D) || window.pressed(GLFW_KEY_S)) step += 1;
            if (window.pressed(GLFW_KEY_A) || window.pressed(GLFW_KEY_W)) step -= 1;
            if (step != 0) engine.fire(sg::Event{"ring_step", sg::Params{}.set("step", step)});
        }

        // The crossing shot walks through at frame 30. At 60 frames a second
        // and the annex look's 0.7 s fade, a 51-frame run ends half way
        // between the looks; a longer one ends in the annex's own.
        if (shot == "crossing" && frames == 30) engine.fire("step_through");

        engine.tick(dt);
        // The door may have been moved by the map. The hall's walls follow it,
        // and so does the doorway's transition, which is rebuilt from the two
        // portals rather than remembered.
        hall.emit("door.moved");
        hall.dispatch_pending();
        sg::as_cover(atlas, graph);

        // Re-root: the active room may have changed, and the doorway may have.
        here = static_cast<const sg::Spatial3D*>(engine.current());
        rooms = sg::place_rooms(graph, atlas, here->id());

        const bool at_crate_map = in_hall && sg::looking_at(*here, "crate_map", 3.2, 0.5);
        const bool at_door_map = !in_hall && sg::looking_at(*here, "door_map", 3.2, 0.5);
        if (!map_open && at_crate_map) view.highlight("crate_map");
        if (!map_open && at_door_map) view.highlight("door_map");

        if (window.pressed(GLFW_KEY_E)) {
            if (crate_open) {
                engine.close_embed("crate_map");
                window.capture_mouse(true);
            } else if (door_open) {
                engine.close_embed("door_map");
                window.capture_mouse(true);
            } else if (at_crate_map) {
                engine.open_embed("crate_map");
                crate_map.set_selection(sg::Key{crates[sel].id.str() + "_tok"});
                window.capture_mouse(false);
            } else if (at_door_map) {
                engine.open_embed("door_map");
                window.capture_mouse(false);
            }
        }
        if (map_open && window.pressed(GLFW_KEY_C)) {
            engine.close_embed(crate_open ? sg::Key{"crate_map"} : sg::Key{"door_map"},
                               /*commit=*/false);
            window.capture_mouse(true);
        }

        if (here->find("lamp") && (window.down(GLFW_KEY_Q) || window.down(GLFW_KEY_R)))
            engine.fire(sg::Event{"lamp.slide", sg::Params{}.set("dx", (window.down(GLFW_KEY_R) ? 3.5 : -3.5) * dt)});
        if (here->find("lamp") && window.pressed(GLFW_KEY_F)) engine.fire("lamp.dim");

        // The alarm is a change of look: one parameter, faded by the renderer.
        if (in_hall && window.pressed(GLFW_KEY_L)) engine.fire("alarm");

        if (window.pressed(GLFW_KEY_ESCAPE)) {
            if (window.mouse_captured()) {
                window.capture_mouse(false);
            } else {
                window.close();
            }
        }

        view.render(rooms, window.width(), window.height());
        window.swap();

        if (max_frames > 0 && ++frames >= max_frames) break;
    }

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

    std::cout << "closed after " << frames << " frames; " << view.stats().late
              << " shaders compiled mid-game\n";
    return 0;
}
