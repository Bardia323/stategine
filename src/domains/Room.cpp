#include "sg/domains/Room.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <set>

namespace sg::plan {

// --- a floor plan ------------------------------------------------------------

double WallLine::length() const {
    double s = 0;
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) s += len2(pts[i + 1] - pts[i]);
    return s;
}

P2 WallLine::at(double along, P2* dir) const {
    double s = 0;
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
        const double l = len2(pts[i + 1] - pts[i]);
        if (along <= s + l || i + 2 == pts.size()) {
            const double t = l > 1e-12 ? std::clamp((along - s) / l, 0.0, 1.0) : 0.0;
            if (dir) *dir = (pts[i + 1] - pts[i]) * (1.0 / std::max(l, 1e-12));
            return pts[i] + (pts[i + 1] - pts[i]) * t;
        }
        s += l;
    }
    if (dir) *dir = {1, 0};
    return pts.empty() ? P2{} : pts.back();
}

double WallLine::nearest(P2 p, double* off) const {
    double best = 1e18, best_s = 0, s = 0;
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
        const P2 a = pts[i], b = pts[i + 1];
        const double l = len2(b - a);
        const double t = l > 1e-12 ? std::clamp(dot2(p - a, b - a) / (l * l), 0.0, 1.0) : 0.0;
        const double d = len2(p - (a + (b - a) * t));
        if (d < best) best = d, best_s = s + t * l;
        s += l;
    }
    if (off) *off = best;
    return best_s;
}

std::vector<P2> WallLine::between(double s0, double s1) const {
    std::vector<P2> out{at(s0)};
    double s = 0;
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
        s += len2(pts[i + 1] - pts[i]);
        if (s > s0 + 1e-9 && s < s1 - 1e-9) out.push_back(pts[i + 1]);
    }
    out.push_back(at(s1));
    return out;
}

bool Outline::inside(P2 p) const {
    bool in = false;
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const P2 a = poly[i], b = poly[j];
        if ((a.z > p.z) != (b.z > p.z) && p.x < (b.x - a.x) * (p.z - a.z) / (b.z - a.z) + a.x) in = !in;
    }
    return in;
}

double Outline::clearance(P2 p) const {
    double best = 1e18;
    for (std::size_t i = 0; i < poly.size(); ++i) {
        const P2 a = poly[i], b = poly[(i + 1) % poly.size()];
        const double l = len2(b - a);
        const double t = l > 1e-12 ? std::clamp(dot2(p - a, b - a) / (l * l), 0.0, 1.0) : 0.0;
        best = std::min(best, len2(p - (a + (b - a) * t)));
    }
    return best;
}

P2 Outline::inward(P2 p, P2 dir) const {
    const P2 n{-dir.z, dir.x};
    return inside(p + n * 0.05) ? n : n * -1.0;
}

int Outline::wall_index(const std::string& name) const {
    for (std::size_t i = 0; i < walls.size(); ++i)
        if (walls[i].name == name) return static_cast<int>(i);
    if (!name.empty() && std::all_of(name.begin(), name.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        const int i = std::stoi(name);
        return i >= 0 && i < static_cast<int>(walls.size()) ? i : -1;
    }
    return -1;
}

P2 Outline::middle() const {
    const P2 c{w * 0.5, d * 0.5};
    if (inside(c) && clearance(c) > 0.3) return c;
    P2 best = c;
    double most = -1;
    for (int i = 1; i < 16; ++i)
        for (int j = 1; j < 16; ++j) {
            const P2 p{w * i / 16.0, d * j / 16.0};
            if (!inside(p)) continue;
            const double cl = clearance(p);
            if (cl > most) most = cl, best = p;
        }
    return best;
}

std::string Outline::tag(std::size_t wall) const {
    if (shape == "rect" && wall < 4) return std::string(1, "nesw"[wall]);
    return std::to_string(wall);
}

const std::vector<std::string>& shape_names() {
    static const std::vector<std::string> n{"rect", "round", "semi", "hex", "oct", "L", "T", "cross", "ngon"};
    return n;
}

bool known_shape(const std::string& s) {
    const auto& n = shape_names();
    return std::find(n.begin(), n.end(), s) != n.end();
}

Outline outline_of(std::string shape, double w, double d, int sides) {
    Outline o;
    if (shape == "hex") shape = "ngon", sides = 6;
    else if (shape == "oct") shape = "ngon", sides = 8;
    if (!known_shape(shape)) shape = "rect";
    o.shape = shape, o.w = w, o.d = d, o.sides = std::clamp(sides, 3, 24);
    // A closed run of corners, as straight walls named wall1, wall2...
    const auto polygon = [&](std::vector<P2> corners) {
        o.poly = corners;
        for (std::size_t i = 0; i < corners.size(); ++i)
            o.walls.push_back({"wall" + std::to_string(i + 1), {corners[i], corners[(i + 1) % corners.size()]}, false});
    };
    if (shape == "rect") {
        o.poly = {{0, 0}, {w, 0}, {w, d}, {0, d}};
        o.walls = {{"north", {{0, 0}, {w, 0}}, false},
                   {"east", {{w, 0}, {w, d}}, false},
                   {"south", {{0, d}, {w, d}}, false},
                   {"west", {{0, 0}, {0, d}}, false}};
    } else if (shape == "round") {
        const int n = 96;
        WallLine c{"round", {}, true};
        for (int i = 0; i <= n; ++i) {
            const double a = -1.5707963267948966 + 6.283185307179586 * i / n;
            c.pts.push_back({w * 0.5 + std::cos(a) * w * 0.5, d * 0.5 + std::sin(a) * d * 0.5});
        }
        o.poly.assign(c.pts.begin(), c.pts.end() - 1);
        o.walls.push_back(c);
    } else if (shape == "semi") {
        const int n = 64;
        WallLine c{"curve", {}, true};
        for (int i = 0; i <= n; ++i) {
            const double a = 3.141592653589793 * i / n;
            c.pts.push_back({w * 0.5 + std::cos(a) * w * 0.5, std::sin(a) * d});
        }
        o.walls.push_back({"north", {{0, 0}, {w, 0}}, false});
        o.walls.push_back(c);
        // Round from the north-west corner: along the north wall, then back
        // along the curve.
        o.poly.clear();
        o.poly.push_back({0, 0});
        for (auto it = c.pts.begin(); it != c.pts.end() - 1; ++it) o.poly.push_back(*it);
    } else if (shape == "ngon") {
        // Flat along the north, stretched to fill the box.
        std::vector<P2> c;
        const int n = o.sides;
        const double a0 = -1.5707963267948966 - 3.141592653589793 / n;
        double x0 = 1e9, x1 = -1e9, z0 = 1e9, z1 = -1e9;
        for (int i = 0; i < n; ++i) {
            const double a = a0 + 6.283185307179586 * i / n;
            c.push_back({std::cos(a), std::sin(a)});
            x0 = std::min(x0, c.back().x), x1 = std::max(x1, c.back().x), z0 = std::min(z0, c.back().z), z1 = std::max(z1, c.back().z);
        }
        for (P2& p : c) p = {(p.x - x0) / (x1 - x0) * w, (p.z - z0) / (z1 - z0) * d};
        polygon(c);
    } else if (shape == "L") {
        polygon({{0, 0}, {w * 0.5, 0}, {w * 0.5, d * 0.5}, {w, d * 0.5}, {w, d}, {0, d}});
    } else if (shape == "T") {
        polygon({{0, 0}, {w, 0}, {w, d * 0.4}, {w * 0.7, d * 0.4}, {w * 0.7, d}, {w * 0.3, d}, {w * 0.3, d * 0.4}, {0, d * 0.4}});
    } else if (shape == "cross") {
        const double a = w / 3, b = d / 3;
        polygon({{a, 0}, {2 * a, 0}, {2 * a, b}, {w, b}, {w, 2 * b}, {2 * a, 2 * b}, {2 * a, d}, {a, d}, {a, 2 * b}, {0, 2 * b},
                 {0, b}, {a, b}});
    }
    return o;
}

std::string walls_text(const Outline& o) {
    std::string s;
    char buf[64];
    for (const WallLine& w : o.walls) {
        if (!s.empty()) s += "|";
        s += w.name + ":";
        for (std::size_t i = 0; i < w.pts.size(); ++i) {
            std::snprintf(buf, sizeof buf, "%s%.3f,%.3f", i ? " " : "", w.pts[i].x, w.pts[i].z);
            s += buf;
        }
    }
    return s;
}

std::vector<WallLine> walls_from_text(const std::string& s) {
    std::vector<WallLine> out;
    std::size_t at = 0;
    while (at < s.size()) {
        const std::size_t bar = std::min(s.find('|', at), s.size());
        const std::string part = s.substr(at, bar - at);
        const std::size_t colon = part.find(':');
        if (colon != std::string::npos) {
            WallLine w;
            w.name = part.substr(0, colon);
            std::size_t i = colon + 1;
            while (i < part.size()) {
                const std::size_t sp = std::min(part.find(' ', i), part.size());
                const std::string xy = part.substr(i, sp - i);
                const std::size_t comma = xy.find(',');
                if (comma != std::string::npos) w.pts.push_back({std::atof(xy.substr(0, comma).c_str()), std::atof(xy.substr(comma + 1).c_str())});
                i = sp + 1;
            }
            w.curved = w.pts.size() > 2;
            out.push_back(w);
        }
        at = bar + 1;
    }
    return out;
}

// --- openings ----------------------------------------------------------------

Opening opening_of(const Element& e) {
    Opening o;
    o.id = e.id;
    o.side = static_cast<int>(e.params.num("side", 0.0));
    o.along = e.params.num("along");
    o.sill = e.params.num("sill");
    o.w = e.params.num(keys::w, kDoorW);
    o.h = e.params.num(keys::h, kDoorH);
    o.door = e.params.num("walk", 0.0) > 0.5;
    return o;
}

std::vector<WallPiece> wall_pieces(double length, double height, std::vector<Opening> holes) {
    std::sort(holes.begin(), holes.end(), [](const Opening& a, const Opening& b) { return a.along < b.along; });
    std::vector<WallPiece> out;
    double at = 0.0;
    for (const Opening& o : holes) {
        const double lo = std::clamp(o.lo(), 0.0, length), hi = std::clamp(o.hi(), 0.0, length);
        if (lo > at + 1e-6) out.push_back({at, lo, 0.0, height});
        if (hi <= lo) continue;
        if (o.sill > 1e-6) out.push_back({lo, hi, 0.0, std::min(o.sill, height)});
        if (o.sill + o.h < height - 1e-6) out.push_back({lo, hi, o.sill + o.h, height});
        at = std::max(at, hi);
    }
    if (length > at + 1e-6) out.push_back({at, length, 0.0, height});
    return out;
}

bool opening_fits(const Opening& o, const Outline& ol, double height, const std::vector<Opening>& others, std::string* why) {
    if (o.side < 0 || o.side >= static_cast<int>(ol.walls.size())) {
        if (why) *why = "the room has no such wall";
        return false;
    }
    const WallLine& wl = ol.walls[static_cast<std::size_t>(o.side)];
    const double len = wl.length();
    if (o.lo() < 0.15 - 1e-6 || o.hi() > len - 0.15 + 1e-6) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.2f", len);
        if (why) *why = "it does not fit on the " + wl.name + " wall there (it is " + buf + " m long)";
        return false;
    }
    if (o.sill < 0 || o.sill + o.h > height - 0.05) {
        if (why) *why = "it is taller than the wall";
        return false;
    }
    for (const Opening& x : others) {
        if (x.id == o.id || x.side != o.side) continue;
        if (o.lo() < x.hi() + 0.2 && x.lo() < o.hi() + 0.2) {
            if (why) *why = "it would run into " + x.id.str();
            return false;
        }
    }
    return true;
}

void chord(const Outline& ol, const Opening& o, P2& mid, P2& in, double& yaw) {
    const WallLine& wl = ol.walls[static_cast<std::size_t>(o.side)];
    const P2 a = wl.at(o.lo()), b = wl.at(o.hi());
    mid = (a + b) * 0.5;
    P2 dir = b - a;
    dir = dir * (1.0 / std::max(1e-9, len2(dir)));
    in = ol.inward(mid, dir);
    yaw = std::atan2(in.z, in.x);
}

}  // namespace sg::plan

namespace sg {

using plan::Opening;
using plan::Outline;
using plan::P2;

Room::Room(Key id, double w, double d, double h, const std::string& shape, int sides, const std::string& names)
    : Spatial3D(id) {
    // Its architecture stands; what moves in it is moved by what moves it.
    set_integrating(false);
    params().set("room_w", w).set("room_d", d).set("room_h", h);
    params().set("shape", plan::known_shape(shape) ? shape : std::string("rect")).set("sides", static_cast<double>(sides));
    params().set("names", names);
}

std::vector<Opening> Room::openings() const {
    std::vector<Opening> out;
    for (const auto& e : elements())
        if (e.alive && plan::is_opening(e)) out.push_back(plan::opening_of(e));
    return out;
}

bool Room::fits(const Opening& o, std::string* why) const { return plan::opening_fits(o, outline(), h(), openings(), why); }

Element* Room::add_opening(const std::string& name, int side, double along, double ow, double oh, double sill, bool door,
                           std::string* why) {
    const Key id = part(name);
    if (find(id)) {
        if (why) *why = "there is already something called " + name + " in " + this->id().str();
        return nullptr;
    }
    Opening o;
    o.id = id, o.side = side, o.along = along, o.w = ow, o.h = oh, o.sill = sill, o.door = door;
    if (!fits(o, why)) return nullptr;
    Element& e = portal(id, {0, 0, 0}, ow, oh, 0.0);
    e.params.set("side", static_cast<double>(side)).set("along", along).set("sill", sill).set("walk", door ? 1.0 : 0.0);
    e.params.set("inset", 0.0).set("casing", door ? 0.08 : 0.06).set("depth", 0.22).set("glow", 0.0);
    e.params.set("tunnel", 0.4).set("tunnel_margin", 1.2);
    e.params.set(keys::r, 0.86).set(keys::g, 0.84).set(keys::b, 0.8);
    seat(e);
    lay_walls();
    return &e;
}

bool Room::move_opening(Key id, int side, double along, std::string* why) {
    Element* e = find(id);
    if (!e || !plan::is_opening(*e)) {
        if (why) *why = "there is no opening " + id.str() + " in " + this->id().str();
        return false;
    }
    Opening o = plan::opening_of(*e);
    o.side = side, o.along = along;
    if (!fits(o, why)) return false;
    e->params.set("side", static_cast<double>(side)).set("along", along);
    seat(*e);
    lay_walls();
    return true;
}

bool Room::size_opening(Key id, double ow, double oh, double sill, std::string* why) {
    Element* e = find(id);
    if (!e || !plan::is_opening(*e)) {
        if (why) *why = "there is no opening " + id.str() + " in " + this->id().str();
        return false;
    }
    Opening o = plan::opening_of(*e);
    o.w = ow, o.h = oh, o.sill = o.door ? 0.0 : sill;
    if (ow < 0.3 || oh < 0.3) {
        if (why) *why = "an opening is at least 0.3 by 0.3 m";
        return false;
    }
    if (o.door && oh < 1.8) {
        if (why) *why = "a door is at least 1.8 m high, to walk through";
        return false;
    }
    if (!fits(o, why)) return false;
    e->params.set(keys::w, ow).set(keys::h, oh).set("sill", o.sill);
    seat(*e);
    lay_walls();
    return true;
}

bool Room::remove_opening(Key id, std::string* why) {
    const Element* e = find(id);
    if (!e || !plan::is_opening(*e)) {
        if (why) *why = "there is no opening " + id.str() + " in " + this->id().str();
        return false;
    }
    remove_with_arrows(id);
    lay_walls();
    return true;
}

void Room::opens(Key id, bool onto_something) {
    Element* e = find(id);
    if (!e || !plan::is_opening(*e)) return;
    e->params.set("onto", onto_something ? 1.0 : 0.0);
    if (Element* b = find(Key{id.str() + ".blank"}))
        b->alive = !onto_something && params().num("fill_openings", 1.0) > 0.5;
}

void Room::seat(Element& e) {
    const Outline ol = outline();
    const Opening o = plan::opening_of(e);
    if (o.side < 0 || o.side >= static_cast<int>(ol.walls.size())) return;
    P2 mid, in;
    double yaw;
    plan::chord(ol, o, mid, in, yaw);
    const P2 at = mid + in * params().num("opening_gap", 0.0);
    set_position(e, {at.x, o.sill + o.h * 0.5, at.z});
    e.params.set(keys::yaw, yaw);
    // What hangs on it, at its foot, turned as it has turned.
    const std::string id = e.id.str();
    for (auto& a : elements())
        if (a.params.get_or<std::string>("hangs_on", "") == id) {
            set_position(a, {mid.x, 0.0, mid.z});
            a.params.set(keys::yaw, yaw - a.params.num("hang_yaw"));
        }
}

void Room::seat_openings() {
    for (auto& e : elements())
        if (plan::is_opening(e)) seat(e);
}

int Room::nearest_wall(double x, double z, double& along) const {
    const Outline o = outline();
    int best = 0;
    double best_off = 1e18;
    for (std::size_t i = 0; i < o.walls.size(); ++i) {
        double off = 0;
        const double s = o.walls[i].nearest({x, z}, &off);
        if (off < best_off) best_off = off, best = static_cast<int>(i), along = s;
    }
    return best;
}

std::vector<const Element*> Room::laid(const std::string& what) const {
    std::vector<const Element*> out;
    for (const auto& e : elements())
        if (e.params.get_or<std::string>("laid", "") == what) out.push_back(&e);
    return out;
}

double Room::corner_run(const Outline& ol, P2 p, P2 q, double t) {
    P2 dir = p - q;
    dir = dir * (1.0 / std::max(1e-9, plan::len2(dir)));
    const P2 in = ol.inward((p + q) * 0.5, dir * -1.0);
    const P2 probe = p + dir * (t * 0.5) - in * (t * 0.5) + in * (-0.001);
    return ol.inside(probe) ? 0.0 : t;
}

double Room::bend(const std::vector<P2>& pts, std::size_t i, double t) {
    if (i == 0 || i + 1 >= pts.size()) return 0.0;
    const P2 a = pts[i] - pts[i - 1], b = pts[i + 1] - pts[i];
    const double turn = std::fabs(std::atan2(plan::cross2(a, b), plan::dot2(a, b)));
    return std::min(t, t * std::tan(turn * 0.5)) + 0.002;
}

void Room::lay_walls() {
    const Outline ol = outline();
    const std::vector<Opening> all = openings();
    const double t = params().num("wall_t", 0.2), rh = h(), skin = params().num("skin", 0.0);
    const bool faces = params().num("wall_faces", 0.0) > 0.5;
    const bool fill = params().num("fill_openings", 1.0) > 0.5;
    const double skirting = params().num("skirting", 0.0);
    std::set<Key> used;

    // The same element by the same name, made if it is not there yet.
    const auto made = [&](Key k, const std::string& what, Key kind) -> Element& {
        used.insert(k);
        Element* e = find(k);
        if (!e || e->kind != kind) {
            if (e) remove_with_arrows(k);
            e = &add_element(k, kind);
        }
        e->alive = true;
        e->params.set("laid", what);
        return *e;
    };
    const auto dress = [&](Element& e, const char* part, double r, double g, double b, double surface, double rough) {
        const std::string p(part);
        e.params.set(keys::r, params().num(Key{p + "_r"}, r)).set(keys::g, params().num(Key{p + "_g"}, g));
        e.params.set(keys::b, params().num(Key{p + "_b"}, b)).set("surface", params().num(Key{p + "_surface"}, surface));
        e.params.set("roughness", params().num(Key{p + "_roughness"}, rough));
    };
    // A block of wall from `a` to `b` (run on by `ea` before a and `eb` past
    // b), y0 to y1 up, `thick` thick, its inner face `inner` in front of the
    // floor's edge.
    const auto block = [&](Element& e, P2 a, P2 b, double ea, double eb, double y0, double y1, double thick, double inner) {
        const double l = plan::len2(b - a);
        const P2 dir = (b - a) * (1.0 / std::max(1e-9, l));
        const P2 in = ol.inward((a + b) * 0.5, dir);
        const P2 c = (a + b) * 0.5 + dir * ((eb - ea) * 0.5) - in * (thick * 0.5 - inner);
        set_position(e, {c.x, y0, c.z});
        e.params.set(keys::sx, l + ea + eb).set(keys::sy, y1 - y0).set(keys::sz, thick).set(keys::yaw, std::atan2(dir.z, dir.x));
    };

    for (std::size_t side = 0; side < ol.walls.size(); ++side) {
        const plan::WallLine& wl = ol.walls[side];
        const std::string tag = ol.tag(side);
        const double len = wl.length();
        std::vector<Opening> holes;
        for (const Opening& o : all)
            if (o.side == static_cast<int>(side)) holes.push_back(o);
        const auto around_hole = [&](const plan::WallPiece& pc) {
            for (const Opening& o : holes)
                if (std::fabs(std::clamp(o.lo(), 0.0, len) - pc.s0) < 1e-9 && std::fabs(std::clamp(o.hi(), 0.0, len) - pc.s1) < 1e-9) return true;
            return false;
        };

        int n = 0;
        for (const plan::WallPiece& pc : plan::wall_pieces(len, rh, holes)) {
            // Under or over an opening the wall spans its chord, as the
            // opening does; elsewhere it follows the wall, piece by piece.
            std::vector<P2> pts;
            std::vector<double> at;  // how far along each point is
            if (around_hole(pc)) {
                pts = {wl.at(pc.s0), wl.at(pc.s1)}, at = {pc.s0, pc.s1};
            } else {
                pts = wl.between(pc.s0, pc.s1);
                at.push_back(pc.s0);
                for (std::size_t i = 1; i < pts.size(); ++i) at.push_back(at.back() + plan::len2(pts[i] - pts[i - 1]));
                at.back() = pc.s1;
            }
            const double y0 = pc.y0 < 1e-6 ? pc.y0 - t : pc.y0, y1 = pc.y1 > rh - 1e-6 ? pc.y1 + t : pc.y1;
            for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
                const P2 a = pts[i], b = pts[i + 1];
                if (plan::len2(b - a) < 1e-6 || pc.y1 - pc.y0 < 1e-6) continue;
                // At a corner of the room the wall runs on past it, to meet
                // the next; at an opening it stops.
                const bool first = i == 0, last = i + 2 == pts.size();
                const double ea = first ? (at[i] < 1e-6 ? corner_run(ol, a, b, t) : 0.0) : bend(pts, i, t);
                const double eb = last ? (at[i + 1] > len - 1e-6 ? corner_run(ol, b, a, t) : 0.0) : bend(pts, i + 1, t);
                const std::string name = tag + "_" + std::to_string(n++);
                Element& shell = made(part("shell_wall_" + name), "shell", kinds::wall);
                block(shell, a, b, ea, eb, y0, y1, t, skin);
                dress(shell, "wall", 0.74, 0.72, 0.68, 2.0, 0.9);
                if (!faces) continue;
                // Its face: the piece as it is seen, `skin` in front of it, for
                // a finish to be shown on - measured from the end of the wall
                // its pattern starts at, so the pieces of one wall are one.
                const double l = plan::len2(b - a);
                const P2 dir = (b - a) * (1.0 / l), in = ol.inward((a + b) * 0.5, dir);
                const double yaw = std::atan2(in.z, in.x);
                const Vec3d u = across(yaw);
                const double ox = dir.x * u.x + dir.z * u.z > 0 ? len - at[i + 1] : at[i];
                const P2 mid = (a + b) * 0.5 + in * skin;
                Key k = part("wall_" + name);
                const bool fresh = !find(k);
                Element& face = made(k, "face", kinds::portal);
                set_position(face, {mid.x, (pc.y0 + pc.y1) * 0.5, mid.z});
                face.params.set(keys::w, l).set(keys::h, pc.y1 - pc.y0).set(keys::yaw, yaw);
                if (fresh) {
                    face.params.set(keys::open, false).set(keys::pitch, 0.0).set("frame", 0.0).set("thick", 0.0004).set("glow", 0.0);
                    face.params.set("roughness", params().num("face_roughness", 0.92));
                }
                face.params.set("face_ox", ox).set("face_oy", pc.y0).set("face_h", rh);
                face.params.set("seed", params().num("face_seed", 23.0) + static_cast<double>(side));
                face.params.set("finish", params().get_or<std::string>("face_finish", "plaster"));
            }
        }

        // What fills an opening onto nothing.
        for (const Opening& o : holes) {
            if (!fill) break;
            Element& bk = made(Key{o.id.str() + ".blank"}, "blank", kinds::wall);
            block(bk, wl.at(o.lo()), wl.at(o.hi()), 0, 0, o.sill, o.sill + o.h, 0.05, 0.0);
            bk.params.set(keys::r, 0.5).set(keys::g, 0.47).set(keys::b, 0.43);
            bk.alive = find(o.id)->params.num("onto", 0.0) < 0.5;
        }

        // Skirting, where the wall meets the floor - but across a doorway,
        // and stopping short of its casing.
        if (skirting <= 0) continue;
        std::vector<Opening> ways;
        for (const Opening& o : holes)
            if (o.sill < 0.05) ways.push_back(o);
        const auto casing = [&](double s, bool lo) {
            for (const Opening& o : ways)
                if (std::fabs((lo ? o.hi() : o.lo()) - s) < 1e-6) return find(o.id)->params.num("casing", 0.1);
            return 0.0;
        };
        int k = 0;
        for (const plan::WallPiece& pc : plan::wall_pieces(len, skirting, ways)) {
            if (pc.y0 > 1e-6) continue;
            const double lo = pc.s0 + (pc.s0 > 1e-6 ? casing(pc.s0, true) : 0.0);
            const double hi = pc.s1 - (pc.s1 < len - 1e-6 ? casing(pc.s1, false) : 0.0);
            if (hi - lo < 1e-3) continue;
            const std::vector<P2> pts = wl.between(lo, hi);
            for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
                const P2 a = pts[i], b = pts[i + 1];
                const double l = plan::len2(b - a);
                if (l < 1e-6) continue;
                const P2 dir = (b - a) * (1.0 / l), in = ol.inward((a + b) * 0.5, dir);
                const P2 c = (a + b) * 0.5 + in * 0.01;
                Element& sk = made(part("skirt_" + tag + std::to_string(k++)), "skirt", kinds::mesh);
                set_position(sk, {c.x, 0.0, c.z});
                sk.params.set(keys::sx, std::max(0.01, l)).set(keys::sy, skirting).set(keys::sz, 0.02);
                sk.params.set(keys::yaw, std::atan2(in.z, in.x) + 1.5707963267948966).set("bevel", 0.004);
                dress(sk, "skirt", 0.22, 0.143, 0.0825, 4.0, 0.5);
            }
        }
    }

    // What is no longer wanted is put out of the way (not alive), not taken
    // away: something may be shown on it, or carried to it, and it is the
    // same element when it is wanted again.
    for (auto& e : elements())
        if (!used.count(e.id) && e.params.has(Key{"laid"})) e.alive = false;
}

}  // namespace sg
