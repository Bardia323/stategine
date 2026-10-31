// Guardrails: the mistakes built places were found with, named (sg/domains/Guard.hpp).
#include "sg/domains/Guard.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

#include "sg/core/StateGraph.hpp"
#include "sg/domains/Spatial.hpp"
#include "sg/domains/Walk.hpp"
#include "sg/domains/Atlas.hpp"

namespace sg {
namespace {

const Key k_hole{"hole"}, k_unseen{"unseen"}, k_solid{"solid"}, k_shape{"shape"}, k_mount{"mount"};

// A thing's box in the world, as an axis-aligned box round its turned one.
struct Box {
    const Element* e = nullptr;
    Vec3d lo, hi;
    double sx = 0, sy = 0, sz = 0;
    bool drawn = false, solid = false, wall = false;
};

bool boxy(const Element& e) { return e.alive && (e.kind == kinds::mesh || e.kind == kinds::wall); }
bool drawn(const Element& e) { return boxy(e) && e.params.num(k_unseen, 0.0) < 0.5 && e.params.num(Key{"taken"}, 0.0) < 0.5; }
bool solid(const Element& e) {
    if (!e.alive) return false;
    if (e.kind == kinds::wall) return !(e.params.has(k_solid) && e.params.num(k_solid, 1.0) < 0.5);
    return e.kind == kinds::mesh && e.params.num(k_solid, 0.0) > 0.5;
}
// A wall: laid as one (kind wall), or a thing that says it is one.
// (A room's laid shell - `laid` - is its walls, floor and ceiling.)
bool is_wall(const Element& e) { return e.alive && (e.kind == kinds::wall || e.params.num(Key{"wall"}, 0.0) > 0.5 || e.params.has(Key{"laid"})); }

Box box_of(const State& s, const Element& e) {
    Box b;
    b.e = &e;
    b.sx = e.params.num(keys::sx, 1.0), b.sy = e.params.num(keys::sy, 1.0), b.sz = e.params.num(keys::sz, 1.0);
    const Pose p = world_pose(s, e);
    const Vec3d c = place_in(p, {0.0, b.sy * 0.5, 0.0});
    b.lo = {1e300, 1e300, 1e300}, b.hi = {-1e300, -1e300, -1e300};
    for (int k = 0; k < 8; ++k) {
        const Vec3d v = c + turn(p, Vec3d{(k & 1 ? 0.5 : -0.5) * b.sx, (k & 2 ? 0.5 : -0.5) * b.sy, (k & 4 ? 0.5 : -0.5) * b.sz});
        b.lo = {std::min(b.lo.x, v.x), std::min(b.lo.y, v.y), std::min(b.lo.z, v.z)};
        b.hi = {std::max(b.hi.x, v.x), std::max(b.hi.y, v.y), std::max(b.hi.z, v.z)};
    }
    b.drawn = drawn(e), b.solid = solid(e), b.wall = is_wall(e);
    return b;
}

std::vector<Box> boxes_of(const State& s) {
    std::vector<Box> out;
    for (const Element& e : s.elements())
        if (boxy(e) && e.params.num(keys::sx, 0.0) > 0.0 && e.params.num(keys::sy, 0.0) > 0.0 && e.params.num(keys::sz, 0.0) > 0.0) out.push_back(box_of(s, e));
    return out;
}

// How far apart two boxes are along each axis (negative: overlapping by so much).
Vec3d gaps(const Box& a, const Box& b) {
    return {std::max(b.lo.x - a.hi.x, a.lo.x - b.hi.x), std::max(b.lo.y - a.hi.y, a.lo.y - b.hi.y), std::max(b.lo.z - a.hi.z, a.lo.z - b.hi.z)};
}
double gap_of(const Vec3d& g) {
    const double x = std::max(g.x, 0.0), y = std::max(g.y, 0.0), z = std::max(g.z, 0.0);
    return std::sqrt(x * x + y * y + z * z);
}

std::string at_of(const Box& b) {
    char t[96];
    std::snprintf(t, sizeof t, "(at %.2f, %.2f, %.2f)", (b.lo.x + b.hi.x) * 0.5, b.lo.y, (b.lo.z + b.hi.z) * 0.5);
    return t;
}
std::string cm(double m) {
    char t[32];
    std::snprintf(t, sizeof t, "%.1f cm", m * 100.0);
    return t;
}
std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool named(const Element& e, std::initializer_list<const char*> words) {
    const std::string id = lower(e.id.str());
    for (const char* w : words)
        if (id.find(w) != std::string::npos) return true;
    return false;
}
// Whether one is part of the other: its parent (any way up), or of one group.
bool kin(const State& s, const Element& a, const Element& b) {
    const std::string ga = a.params.get_or<std::string>("group", ""), gb = b.params.get_or<std::string>("group", "");
    if (!ga.empty() && ga == gb) return true;
    for (const Element* x : {&a, &b}) {
        const Element* other = x == &a ? &b : &a;
        const Element* p = x;
        for (int i = 0; i < 16 && p && p->params.has(keys::parent); ++i) {
            const Key up{p->params.get_or<std::string>(keys::parent, "")};
            if (up == other->id) return true;
            p = s.find(up);
        }
    }
    // (Parts of one recipe - `n.part` and `n.other` - are one thing.)
    const std::string ia = a.id.str(), ib = b.id.str();
    const std::size_t da = ia.rfind('.'), db = ib.rfind('.');
    return da != std::string::npos && db != std::string::npos && da == db && ia.compare(0, da, ib, 0, db) == 0;
}

std::string mount_of(const Element& e) {
    const std::string m = e.params.get_or<std::string>(k_mount, "");
    if (!m.empty()) return m;
    // (By its own name - `room.switch_hall` - not a part's of another thing: `room.torch.switch` is the torch's.)
    if (e.params.has(keys::parent)) return "";
    std::string id = lower(e.id.str());
    const std::size_t d1 = id.find('.'), d2 = d1 == std::string::npos ? std::string::npos : id.find('.', d1 + 1);
    if (d2 != std::string::npos) id = id.substr(0, d2);
    for (const char* w : {"sconce", "switch", "socket", "outlet", "thermostat", "wall_lamp"})
        if (id.find(w) != std::string::npos) return "wall?";
    return "";
}

// The ground of a space under its things: its floor at 0 when it lays none and
// is not open land.
bool floor_at_zero(const State& s) {
    for (const Element& e : s.elements())
        if (e.alive && e.kind == Key{"terrain"}) return false;
    return true;
}

struct Opening {
    const Element* e;
    Pose at;
    double hw, hh;
};
std::vector<Opening> openings(const State& s, bool any_doorway = false) {
    std::vector<Opening> out;
    for (const Element& e : s.elements())
        if (e.alive && ((e.kind == kinds::portal && (e.params.num("walk", 0.0) > 0.5 || (any_doorway && e.params.num(keys::h, 2.0) >= 1.5))) || e.kind == k_hole))
            out.push_back({&e, world_pose(s, e), e.params.num(keys::w, 1.0) * 0.5, e.params.num(keys::h, 2.0) * 0.5});
    return out;
}

}  // namespace

std::vector<std::string> mount_faults(const State& space) {
    std::vector<std::string> out;
    const std::vector<Box> all = boxes_of(space);
    std::vector<const Box*> walls;
    for (const Box& b : all)
        if (b.wall) walls.push_back(&b);
    const bool ground0 = floor_at_zero(space);
    int said = 0;
    for (const Box& b : all) {
        if (!b.drawn || b.wall || said > 24) continue;
        const Element& e = *b.e;
        const std::string m = mount_of(e);
        if (m == "free" || m == "ceiling" || e.params.num(Key{"in_wall"}, 0.0) > 0.5 || e.params.num(Key{"in_opening"}, 0.0) > 0.5) continue;
        if (m == "wall" || m == "wall?") {
            if (walls.empty()) continue;
            // The wall it is nearest: off it, or into it, along the way it is thinnest to the wall.
            const Box* best = nullptr;
            double off = 1e300, into = 0.0;
            for (const Box* w : walls) {
                const Vec3d g = gaps(b, *w);
                const double d = gap_of(g);
                // (Into it: the least it would have to move to be out.)
                const double pen = d > 0.0 ? 0.0 : std::min({-g.x, -g.y, -g.z});
                if (d < off || (d == 0.0 && off == 0.0 && pen > into)) best = w, off = d, into = pen;
            }
            if (!best) continue;
            // (How deep it is the way it stands into the wall: its least size.)
            const double depth = std::min({b.hi.x - b.lo.x, b.hi.z - b.lo.z});
            // (Unsaid, named for a wall but far from any - a torch's switch on a table - is not on one.)
            if (m == "wall?" && off > 0.3) continue;
            if (off > 0.015) {
                out.push_back(e.id.str() + " " + at_of(b) + " hangs on a wall (`mount` " + (m == "wall" ? "wall" : "unsaid, named for one") + ") but stands " + cm(off) + " off the nearest, " +
                              best->e->id.str() + ": it floats - set its back on the wall's face");
                ++said;
            } else if (into > std::max(0.015, depth * 0.5)) {
                out.push_back(e.id.str() + " " + at_of(b) + " hangs on a wall but is sunk " + cm(into) + " into " + best->e->id.str() + " (it is " + cm(depth) +
                              " deep): set its back on the wall's face, not in it");
                ++said;
            }
            continue;
        }
        // On the floor, or on what stands on it. (Furniture's size, unsaid: a
        // knob, a shelf's book is held by what it is part of.)
        const bool said_floor = m == "floor";
        if (!said_floor && (!b.solid || e.params.has(keys::parent) || b.sy < 0.3 || (b.hi.x - b.lo.x) * (b.hi.z - b.lo.z) < 0.09)) continue;
        if (e.params.num(Key{"walk"}, 0.0) > 0.5 || e.params.num(Key{"hangs"}, 0.0) > 0.5 || e.params.has(Key{"hangs_on"})) continue;
        const double foot = b.lo.y;
        // (A thing a room's height or more, or a floor's size: the room's own.)
        if ((b.hi.x - b.lo.x) * (b.hi.z - b.lo.z) > 6.0) continue;
        // Held: something it touches is lower than its foot, or tops out at it - its legs, the floor, the table.
        bool held = ground0 && std::fabs(foot) < 0.01;
        double sunk = ground0 && foot < -0.02 && b.hi.y > 0.02 ? -foot : 0.0;
        // (Only what could be said is looked for: a thing high up, unsaid, is not a floor's.)
        if (!(said_floor || foot < 0.4) && sunk <= 0.0) continue;
        if (sunk <= 0.0)
            for (const Box& o : all) {
                if (&o == &b || !o.drawn) continue;
                const Vec3d g = gaps(b, o);
                if (g.x > 0.01 || g.z > 0.01 || g.y > 0.01) continue;
                if (o.lo.y < foot - 0.005 || std::fabs(o.hi.y - foot) < 0.02) {
                    held = true;
                    break;
                }
            }
        if (sunk > 0.0 && !said_floor && foot > -0.6) {
            out.push_back(e.id.str() + " " + at_of(b) + " stands " + cm(sunk) + " into the floor: set its foot on it");
            ++said;
        } else if (!held && (said_floor || foot < 0.4) && foot > 0.0) {
            out.push_back(e.id.str() + " " + at_of(b) + " hovers: nothing under it or beside it reaches its foot, " + cm(foot) +
                          " up - set it on the floor (or on what holds it), or say `mount` wall / ceiling / free");
            ++said;
        }
    }
    return out;
}

std::vector<std::string> opening_faults(const State& space) {
    std::vector<std::string> out;
    const std::vector<Opening> open = openings(space);
    if (open.empty()) return out;
    std::set<Key> said;
    for (const Element& e : space.elements()) {
        if (!drawn(e) || is_wall(e) || said.size() > 24) continue;
        if (e.params.num(Key{"in_opening"}, 0.0) > 0.5 || e.params.has(Key{"hangs_on"})) continue;
        const double sx = e.params.num(keys::sx, 0.0), sy = e.params.num(keys::sy, 0.0), sz = e.params.num(keys::sz, 0.0);
        if (sx <= 0.0 || sy <= 0.0 || sz <= 0.0) continue;
        // (A door's own: its leaf, its frame, its knob - named so.)
        if (named(e, {"door", "leaf", "jamb", "casing", "architrave", "threshold", "sill", "curtain", "frame", "lever", "knob", "handle", "latch", "bolt", "lock",
                      "hinge", "escutcheon"}))
            continue;
        const Pose p = world_pose(space, e);
        const Vec3d c = place_in(p, {0.0, sy * 0.5, 0.0});
        for (const Opening& o : open) {
            // Its corners in the opening's own frame: x through it, z across, y up from its middle.
            Vec3d lo{1e300, 1e300, 1e300}, hi{-1e300, -1e300, -1e300};
            for (int k = 0; k < 8; ++k) {
                const Vec3d v = local_of(o.at, c + turn(p, Vec3d{(k & 1 ? 0.5 : -0.5) * sx, (k & 2 ? 0.5 : -0.5) * sy, (k & 4 ? 0.5 : -0.5) * sz}));
                lo = {std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z)}, hi = {std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z)};
            }
            // The opening: its plane, a hand's depth either side (the wall it is
            // cut in), inside its jambs by 3 cm, over a sill of 3 cm, under its head.
            const double across = std::min(hi.z, o.hw - 0.03) - std::max(lo.z, -o.hw + 0.03);
            const double up = std::min(hi.y, o.hh - 0.03) - std::max(lo.y, -o.hh + 0.03);
            const bool in_plane = lo.x < 0.06 && hi.x > -0.06;
            if (!in_plane || across <= 0.0 || up <= 0.0) continue;
            // (Across most of it, or into it by more than a hand: a moulding
            // run on, a thing stood in the way. A bolt at its edge is not.)
            if (across < 0.1) continue;
            char t[160];
            std::snprintf(t, sizeof t, " runs across doorway %s (%.2f m of its %.2f m width, %.2f to %.2f m up)", o.e->id.str().c_str(), across, 2.0 * o.hw,
                          std::max(lo.y, -o.hh) + o.hh, std::min(hi.y, o.hh) + o.hh);
            out.push_back(e.id.str() + t + ": a skirting, rail or panelling stops at the jambs; what belongs in a doorway says `in_opening` 1");
            said.insert(e.id);
            break;
        }
    }
    return out;
}

std::vector<std::string> door_faults(const State& space, const StateGraph* graph) {
    std::vector<std::string> out;
    // (A doorway a door stands in: walked through, or one shut - a portal of a door's height.)
    const std::vector<Opening> open = openings(space, true);
    for (const Element& e : space.elements()) {
        if (!drawn(e) || e.params.num(Key{"blind"}, 0.0) > 0.5) continue;
        const bool leaf = named(e, {"door_leaf", ".leaf", "_leaf"}), frame = named(e, {"door_frame", "doorframe", "door_casing", "door.frame"});
        if (!leaf && !frame && e.params.get_or<std::string>("part", "") != "door") continue;
        const Box b = box_of(space, e);
        const Vec3d mid{(b.lo.x + b.hi.x) * 0.5, (b.lo.y + b.hi.y) * 0.5, (b.lo.z + b.hi.z) * 0.5};
        bool at_one = false;
        for (const Opening& o : open) {
            const Vec3d l = local_of(o.at, mid);
            if (std::fabs(l.x) < 1.2 && std::fabs(l.z) < o.hw + 0.6 && std::fabs(l.y) < o.hh + 0.6) at_one = true;
        }
        if (!at_one)
            out.push_back(e.id.str() + " " + at_of(b) + " is a door's " + (frame ? "frame" : "leaf") +
                          " with no doorway at it (no portal walked through, no hole): it leads nowhere - open the wall behind it to a room, or say `blind` 1");
    }
    if (graph)
        for (const Opening& o : openings(space)) {
            if (o.e->kind != kinds::portal || o.e->params.num(Key{"blind"}, 0.0) > 0.5) continue;
            // (A hole of rooms made one, or the hole kept beside a doorway - `x_hole` by `x` - is inside the place, not a way out of it.)
            const std::string id = o.e->id.str();
            if (id.size() > 5 && id.compare(id.size() - 5, 5, "_hole") == 0) continue;
            bool joined = false;
            for (std::size_t si : graph->seams_of(space.id())) {
                const Seam& sm = graph->seams()[si];
                for (const auto* bd : {&sm.boundary_a, &sm.boundary_b})
                    joined = joined || std::find(bd->begin(), bd->end(), o.e->id) != bd->end();
            }
            if (!joined)
                out.push_back("doorway " + o.e->id.str() + " is walked through (`walk` 1) but no seam joins it to a room: behind it is nothing - glue it, or say `walk` 0");
        }
    return out;
}

std::vector<std::string> level_faults(const State& space) {
    std::vector<std::string> out;
    struct Face {
        const Element* e;
        double y, x0, x1, z0, z1, rank;
        bool top;
    };
    std::vector<Face> faces;
    for (const Element& e : space.elements()) {
        if (!drawn(e)) continue;
        const std::string shape = e.params.get_or<std::string>(k_shape, "");
        if (!shape.empty() && shape != "box") continue;
        const double sx = e.params.num(keys::sx, 0.0), sy = e.params.num(keys::sy, 0.0), sz = e.params.num(keys::sz, 0.0);
        if (sx <= 0.0 || sy <= 0.0 || sz <= 0.0) continue;
        const Pose p = world_pose(space, e);
        if (std::fabs(p.pitch) > 1e-6 || std::fabs(p.roll) > 1e-6 || std::fabs(std::remainder(p.yaw, 1.5707963267948966)) > 1e-4) continue;
        const bool turned = std::fabs(std::sin(p.yaw)) > 0.5;
        const double hx = (turned ? sz : sx) * 0.5, hz = (turned ? sx : sz) * 0.5;
        const double rank = 3.0 * (4.0 - std::log2(std::max(std::cbrt(sx * sy * sz), 1e-4)));
        faces.push_back({&e, p.position.y + sy, p.position.x - hx, p.position.x + hx, p.position.z - hz, p.position.z + hz, rank, true});
        faces.push_back({&e, p.position.y, p.position.x - hx, p.position.x + hx, p.position.z - hz, p.position.z + hz, rank, false});
    }
    std::sort(faces.begin(), faces.end(), [](const Face& a, const Face& b) { return a.y < b.y; });
    std::set<std::pair<Key, Key>> said;
    for (std::size_t i = 0; i < faces.size(); ++i)
        for (std::size_t j = i + 1; j < faces.size() && faces[j].y - faces[i].y < 0.002; ++j) {
            const Face& a = faces[i];
            const Face& b = faces[j];
            // (One facing up, one facing down: a box on a box, touching - unseen.)
            if (a.e == b.e || a.top != b.top) continue;
            const double ox = std::min(a.x1, b.x1) - std::max(a.x0, b.x0), oz = std::min(a.z1, b.z1) - std::max(a.z0, b.z0);
            if (ox <= 0.05 || oz <= 0.05) continue;
            if (a.e->kind == kinds::wall && b.e->kind == kinds::wall) continue;
            // (The renderer sees the smaller where two of a size differ: Shaders.hpp's rule, as coinciding reads it.)
            if (std::fabs(a.rank - b.rank) >= 1.0 || a.e->params.num(Key{"depth_layer"}, 0.0) != b.e->params.num(Key{"depth_layer"}, 0.0)) continue;
            const auto key = a.e->id.str() < b.e->id.str() ? std::pair{a.e->id, b.e->id} : std::pair{b.e->id, a.e->id};
            if (!said.insert(key).second || said.size() > 12) continue;
            char t[160];
            std::snprintf(t, sizeof t, " lie level, their %s at %.3f m, overlapping %.2f by %.2f m (at %.2f, %.2f)", a.top ? "tops" : "bottoms", a.y, ox, oz,
                          (std::max(a.x0, b.x0) + std::min(a.x1, b.x1)) * 0.5, (std::max(a.z0, b.z0) + std::min(a.z1, b.z1)) * 0.5);
            out.push_back(key.first.str() + " and " + key.second.str() + t + ": they fight, flickering - end one where the other begins, or lift one a few millimetres");
        }
    if (said.size() > 12) out.push_back("and " + std::to_string(said.size() - 12) + " more pairs of level faces");
    return out;
}

std::vector<std::string> seam_level_faults(const State& space, const StateGraph& graph) {
    std::vector<std::string> out;
    struct Top {
        Key id;
        double y, x0, x1, z0, z1;
        bool top;
    };
    // Its drawn boxes square to the world near a point, as tops and bottoms - turned by `to` when given.
    const auto faces_near = [](const State& s, const Vec3d& at, const Pose* from, const Pose* to) {
        std::vector<Top> f;
        for (const Element& e : s.elements()) {
            if (!drawn(e)) continue;
            const std::string shape = e.params.get_or<std::string>(k_shape, "");
            if (!shape.empty() && shape != "box") continue;
            const double sx = e.params.num(keys::sx, 0.0), sy = e.params.num(keys::sy, 0.0), sz = e.params.num(keys::sz, 0.0);
            if (sx <= 0.0 || sy <= 0.0 || sz <= 0.0) continue;
            Pose p = world_pose(s, e);
            if (from && to) p = through_portal(*from, *to, p);
            if (std::fabs(p.pitch) > 1e-6 || std::fabs(p.roll) > 1e-6 || std::fabs(std::remainder(p.yaw, 1.5707963267948966)) > 1e-4) continue;
            const bool turned = std::fabs(std::sin(p.yaw)) > 0.5;
            const double hx = (turned ? sz : sx) * 0.5, hz = (turned ? sx : sz) * 0.5;
            if (std::max({0.0, std::fabs(p.position.x - at.x) - hx, std::fabs(p.position.z - at.z) - hz}) > 1.0) continue;
            f.push_back({e.id, p.position.y + sy, p.position.x - hx, p.position.x + hx, p.position.z - hz, p.position.z + hz, true});
            f.push_back({e.id, p.position.y, p.position.x - hx, p.position.x + hx, p.position.z - hz, p.position.z + hz, false});
        }
        return f;
    };
    bool glued = false;
    std::vector<PlacedRoom> glued_here;
    for (std::size_t si : graph.seams_of(space.id())) {
        const Seam& sm = graph.seams()[si];
        if (sm.a != space.id() || sm.b == sm.a || !graph.contains(sm.b)) continue;  // (said once, by its first side)
        const State& there = graph.state(sm.b);
        for (std::size_t i = 0; i < sm.boundary_a.size() && i < sm.boundary_b.size(); ++i) {
            const Element* e = space.find(sm.boundary_a[i]);
            const Element* f = there.find(sm.boundary_b[i]);
            if (!e || !f || e->kind != kinds::portal || e->params.num("walk", 0.0) < 0.5) continue;
            // Only what is drawn in one depth buffer fights. A room seen through a walked doorway is
            // clipped at its plane (portal_inset 0): there the two sides never meet. Only rooms the
            // renderer glues into one (sheafify, as it grows a space: both doorways saying `sheaf`, of
            // one kind, standing in each other by no more than 0.6 m) are drawn whole, together.
            if (!glued) glued_here = sheafify(graph, space.id(), 6, static_cast<std::size_t>(-1), false), glued = true;
            if (std::none_of(glued_here.begin(), glued_here.end(), [&](const PlacedRoom& p) { return p.room == &there; }) || glued_here.size() < 2) continue;
            const Pose pe = world_pose(space, *e), pf = world_pose(there, *f);
            const std::vector<Top> mine = faces_near(space, pe.position, nullptr, nullptr);
            // (Theirs carried here as a walker is carried back: through their doorway to ours.)
            const std::vector<Top> theirs = faces_near(there, pe.position, &pf, &pe);
            int told = 0;
            for (const Top& a : mine)
                for (const Top& b : theirs) {
                    if (told >= 4 || a.top != b.top || std::fabs(a.y - b.y) > 0.002) continue;
                    const double ox = std::min(a.x1, b.x1) - std::max(a.x0, b.x0), oz = std::min(a.z1, b.z1) - std::max(a.z0, b.z0);
                    if (ox <= 0.02 || oz <= 0.02) continue;
                    char t[200];
                    std::snprintf(t, sizeof t, " lie level (%s at %.3f m) and overlap %.2f by %.2f m across the seam at doorway %s", a.top ? "tops" : "bottoms", a.y, ox, oz,
                                  e->id.str().c_str());
                    out.push_back(a.id.str() + " and " + there.id().str() + "'s " + b.id.str() + t + ": both drawn there, they fight - end each at the doorway's plane");
                    ++told;
                }
        }
    }
    return out;
}

std::vector<std::string> inside_faults(const State& space) {
    std::vector<std::string> out;
    std::vector<Box> all = boxes_of(space);
    all.erase(std::remove_if(all.begin(), all.end(), [](const Box& b) { return !b.solid || !b.drawn || b.e->params.num(Key{"walk"}, 0.0) > 0.5; }), all.end());
    std::sort(all.begin(), all.end(), [](const Box& a, const Box& b) { return a.lo.x < b.lo.x; });
    int said = 0;
    for (std::size_t i = 0; i < all.size() && said < 24; ++i)
        for (std::size_t j = i + 1; j < all.size() && all[j].lo.x < all[i].hi.x && said < 24; ++j) {
            const Box& a = all[i];
            const Box& b = all[j];
            if (a.wall && b.wall) continue;
            const Vec3d g = gaps(a, b);
            if (g.x > -0.03 || g.y > -0.03 || g.z > -0.03) continue;
            if (kin(space, *a.e, *b.e)) continue;
            bool held = false;
            for (const Box* x : {&a, &b}) {
                const Box* y = x == &a ? &b : &a;
                const std::string in = x->e->params.get_or<std::string>("in", "");
                if (!in.empty() && y->e->id.str().find(in) != std::string::npos) held = true;
                if (x->e->params.num(Key{"in_wall"}, 0.0) > 0.5 && y->wall) held = true;
            }
            // (Something wholly the size of a room's floor under it: the room's own slab, laid under what stands.)
            if (held) continue;
            char t[128];
            std::snprintf(t, sizeof t, " stand inside each other (%.2f x %.2f x %.2f m)", -g.x, -g.y, -g.z);
            out.push_back(a.e->id.str() + " " + at_of(a) + " and " + b.e->id.str() + t + ": move one out, or say the one in the other (`in`)");
            ++said;
        }
    return out;
}

std::vector<std::string> pad_faults(const State& space) {
    std::vector<std::string> out;
    std::vector<Box> all = boxes_of(space);
    for (const Element& p : space.elements()) {
        if (!p.alive || p.params.num(Key{"bare"}, 0.0) > 0.5 || p.params.num(Key{"pad_given"}, 0.0) > 0.5) continue;
        const bool pad = !p.params.get_or<std::string>("use", "").empty() || !p.params.get_or<std::string>("pt_use", "").empty() || p.params.num(Key{"pad"}, 0.0) > 0.5;
        if (!pad || (boxy(p) && drawn(p))) continue;
        Box b = box_of(space, p);
        if (!boxy(p) || !p.params.has(keys::sx)) {
            const Vec3d at = world_position(space, p);
            b.lo = at - Vec3d{0.02, 0.02, 0.02}, b.hi = at + Vec3d{0.02, 0.02, 0.02};
        }
        bool near = false;
        for (const Box& o : all) {
            if (o.e == &p || !o.drawn || o.wall || named(*o.e, {"shell", ".floor", "ceiling"})) continue;
            if (gap_of(gaps(b, o)) <= 0.05) {
                near = true;
                break;
            }
        }
        if (!near)
            out.push_back(p.id.str() + " " + at_of(b) + " is used (`" + (p.params.has(Key{"pt_use"}) ? "pt_use" : "use") +
                          "`) but is not drawn and has nothing drawn within 5 cm: a player is told to use nothing - put the thing there, or say `bare` 1");
    }
    return out;
}

std::vector<std::string> taken_faults(const State& space) {
    std::vector<std::string> out;
    for (const Element& e : space.elements()) {
        if (!e.alive || (e.params.num(Key{"taken"}, 0.0) < 0.5 && e.params.num(Key{"hidden"}, 0.0) < 0.5)) continue;
        std::vector<std::string> still;
        if (boxy(e) && e.params.num(k_unseen, 0.0) < 0.5) still.push_back("drawn");
        if (boxy(e) && e.params.num(Key{"cast"}, 1.0) > 0.5) still.push_back("casting a shadow");
        if (e.params.num(Key{"mirrored"}, 0.0) > 0.5) still.push_back("mirrored");
        if (solid(e)) still.push_back("solid");
        if (e.kind == kinds::light && e.params.num(keys::intensity, 1.0) > 0.0) still.push_back("lighting");
        if (still.empty()) continue;
        std::string s;
        for (const std::string& w : still) s += (s.empty() ? "" : ", ") + w;
        out.push_back(e.id.str() + " is " + (e.params.num(Key{"taken"}, 0.0) > 0.5 ? "taken" : "hidden") + " yet still " + s +
                      ": what is gone is gone - take it away (`alive` false, or moved to who holds it), not only hide it");
    }
    return out;
}

std::vector<std::string> climb_faults(const State& space, double step) {
    std::vector<std::string> out;
    // (The walker's own step, as sg::walk reads it: 0.36 unless it says.)
    bool said = step >= 0.0;
    if (step < 0.0) {
        const Element* cam = space.find(SpatialState::camera_id());
        said = cam && cam->params.has(Key{"step"});
        step = cam ? cam->params.num(Key{"step"}, 0.36) : 0.36;
    }
    if (said && step > 0.36) {
        char t[160];
        std::snprintf(t, sizeof t, "its walker steps up %.2f m (`step`): more than a stair's riser and the engine's own 0.36 - it climbs chairs and beds; a stair is a ramp under its treads", step);
        out.push_back(t);
    }
    std::vector<Box> all = boxes_of(space);
    all.erase(std::remove_if(all.begin(), all.end(),
                             [](const Box& b) { return !b.solid || b.wall || (b.hi.x - b.lo.x) < 0.15 || (b.hi.z - b.lo.z) < 0.15 || b.hi.y - b.lo.y > 3.0; }),
              all.end());
    // From the floor, every top within a step of where a walker stands, and on.
    const bool ground0 = floor_at_zero(space);
    std::vector<int> reach(all.size(), 0);
    std::vector<std::size_t> frontier;
    for (std::size_t i = 0; i < all.size(); ++i)
        if (all[i].e->params.num(Key{"walk"}, 0.0) > 0.5 || all[i].e->params.num(Key{"climb"}, 0.0) > 0.5 ||
            (ground0 && all[i].hi.y > 0.03 && all[i].hi.y <= step && all[i].lo.y < 0.05))
            reach[i] = 1, frontier.push_back(i);
    while (!frontier.empty()) {
        const std::size_t i = frontier.back();
        frontier.pop_back();
        for (std::size_t j = 0; j < all.size(); ++j) {
            if (reach[j]) continue;
            const Vec3d g = gaps(all[i], all[j]);
            if (g.x > 0.3 || g.z > 0.3) continue;  // (a stride from its edge)
            const double rise = all[j].hi.y - all[i].hi.y;
            if (rise > 0.03 && rise <= step) reach[j] = 1, frontier.push_back(j);
        }
    }
    int told = 0;
    for (std::size_t i = 0; i < all.size() && told < 16; ++i) {
        const Element& e = *all[i].e;
        if (!reach[i] || e.params.num(Key{"walk"}, 0.0) > 0.5 || e.params.num(Key{"climb"}, 0.0) > 0.5 || all[i].hi.y < 0.05) continue;
        // (A stair's treads are climbed: named so.)
        if (named(e, {"stair", "tread", "step", "landing"})) continue;
        char t[160];
        std::snprintf(t, sizeof t, " %s: its top, %.2f m up, is a walker's step (%.2f) from where it stands - it climbs onto it", at_of(all[i]).c_str(), all[i].hi.y, step);
        out.push_back(e.id.str() + t + "; say `climb` 1 if it may (a tread), or make it not solid low down (its top higher than a step)");
        ++told;
    }
    return out;
}

std::vector<std::string> guard_faults(const State& space, const StateGraph* graph) {
    std::vector<std::string> out;
    const auto add = [&](const char* law, const std::vector<std::string>& fs) {
        for (const std::string& f : fs) out.push_back(std::string(law) + ": " + f);
    };
    add("mount", mount_faults(space));
    add("opening", opening_faults(space));
    add("door", door_faults(space, graph));
    add("level", level_faults(space));
    if (graph) add("seam", seam_level_faults(space, *graph));
    add("inside", inside_faults(space));
    add("walkway", doorway_faults(space));
    add("pad", pad_faults(space));
    add("taken", taken_faults(space));
    add("climb", climb_faults(space));
    return out;
}

}  // namespace sg
