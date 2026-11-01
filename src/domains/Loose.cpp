// The loose things of a room, given bodies (sg/domains/Loose.hpp): the
// physics' parts posed from its bodies, and the room's things made fixed
// boxes and bodies. (Moved from Memory Leak's src/loose and src/things.cpp.)
#include "sg/domains/Loose.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <sstream>

#include "sg/domains/Modeler.hpp"
#include "sg/domains/PhysicsSpace.hpp"
#include "sg/domains/Walk.hpp"
#include "sg/physics/Rigid.hpp"

namespace sg::physics {


namespace {

namespace rigid = sg::rigid;
const sg::Key kBody{"body"}, kPart{"part"}, kParts{"parts"}, kMode{"mode"}, kOf{"body"};

rigid::V3 v3(const Vec3d& v) { return {v.x, v.y, v.z}; }
rigid::M3 turn_of(const sg::Params& p) { return rigid::from_euler(p.num(sg::keys::yaw), p.num(sg::keys::pitch), p.num(sg::keys::roll)); }
rigid::M3 rel_of(const sg::Params& p) { return sg::spatial::from_quat(p.num("lqw", 1.0), p.num("lqx"), p.num("lqy"), p.num("lqz")); }
// A turn whose columns are `a`, `b` and their cross: x along a, y along b.
rigid::M3 frame(rigid::V3 a, rigid::V3 b) {
    a = rigid::normalize(a);
    b = rigid::normalize(b - a * rigid::dot(a, b));
    const rigid::V3 c = rigid::cross(a, b);
    rigid::M3 m;
    m.a = {a.x, b.x, c.x, a.y, b.y, c.y, a.z, b.z, c.z};
    return m;
}
// (A value written only where it is not already: asleep, nothing is.)
void put(sg::Params& p, sg::Key k, double v) {
    if (p.num(k) != v) p.set(k, v);
}

}  // namespace

Bodies::Bodies(sg::Key id, double room_w, double room_d, double room_h) : Physics(id, room_w, room_d, room_h) {
    // parts --step--> parts: after the bodies have stepped (declared after them).
    add_element(kParts, kParts);
    loop(sg::Key{"pose.parts"}, kParts, step_event(),
         [](sg::State& s, sg::Element&, sg::Element*, const sg::Event& ev) { static_cast<Bodies&>(s).pose_parts(ev.args.num(sg::keys::dt)); });
    // parts --step--> parts: and whoever walks among them, on the same step.
    loop(sg::Key{"walkers"}, kParts, step_event(),
         [](sg::State& s, sg::Element&, sg::Element*, const sg::Event& ev) { static_cast<Bodies&>(s).walk(ev.args.num(sg::keys::dt)); });
    loop(sg::Key{"hide"}, kParts, hide_event(), [](sg::State& s, sg::Element&, sg::Element*, const sg::Event& ev) {
        if (sg::Element* b = static_cast<Bodies&>(s).find(sg::Key{ev.args.get_or<std::string>("id", "")})) b->params.set("hidden", ev.args.num("on") > 0.5 ? 1.0 : 0.0);
    });
}

void Bodies::fixed(const std::string& name, const Vec3d& centre, const Vec3d& half, double yaw) {
    sg::Element& s = add_support(sg::Key{"#" + name});
    s.params.set(sg::keys::x, centre.x).set(sg::keys::z, centre.z).set(sg::keys::yaw, yaw);
    s.params.set(sg::keys::sx, half.x * 2).set(sg::keys::sz, half.z * 2).set("base", centre.y - half.y).set("top", centre.y + half.y);
}

sg::Element& Bodies::solid(sg::Key id, const Vec3d& centre, const Vec3d& half, double yaw, double pitch, double roll, double mass) {
    // (A rigid body's w runs along its x, thick up its y, h along its z.)
    sg::Element& b = add_body(id, half.x * 2, half.z * 2, half.y * 2, 0.0, "rigid");
    add_hull(b, false, {0, 0, 0}, half);
    b.params.set("mass", mass);
    place(b, centre, yaw, pitch, roll, mode::rest);
    return b;
}

sg::Element& Bodies::sheet(sg::Key id, const Vec3d& centre, double w, double h, double thick, const Vec3d& face, const Vec3d& up, bool pinned) {
    // Paper: it floats as it falls.
    sg::Element& b = add_body(id, w, h, std::max(thick, 0.001), 1.0, "sheet");
    // A sheet's frame is a panel's: x its face, y its up, z across.
    double yaw = 0, pitch = 0, roll = 0;
    rigid::to_euler(frame(v3(face), v3(up)), yaw, pitch, roll);
    place(b, centre, yaw, pitch, roll, pinned ? mode::pinned : mode::rest);
    return b;
}

sg::Element& Bodies::part(sg::Key part, sg::Key body, const Vec3d& centre, double lift, double yaw, double pitch, double roll, bool unseen, double cast) {
    const sg::Element& b = element(body);
    const rigid::M3 r = turn_of(b.params), rt = rigid::transpose(r);
    const rigid::V3 l = rt * (v3(centre) - rigid::V3{b.params.num(sg::keys::x), b.params.num(sg::keys::y), b.params.num(sg::keys::z)});
    double w = 1, x = 0, y = 0, z = 0;
    sg::spatial::to_quat(rigid::orthonormal(rt * rigid::from_euler(yaw, pitch, roll)), w, x, y, z);
    sg::Element& e = add_element(part, kPart);
    e.params.set("body", body.str()).set("lx", l.x).set("ly", l.y).set("lz", l.z).set("lift", lift);
    e.params.set("lqw", w).set("lqx", x).set("lqy", y).set("lqz", z);
    e.params.set(sg::keys::x, centre.x).set(sg::keys::y, centre.y - lift).set(sg::keys::z, centre.z);
    e.params.set(sg::keys::yaw, yaw).set(sg::keys::pitch, pitch).set(sg::keys::roll, roll);
    e.params.set("seen_as", unseen ? 1.0 : 0.0).set("unseen", unseen ? 1.0 : 0.0).set("cast_as", cast).set("cast", cast);
    return e;
}

int Bodies::awake() const {
    int n = 0;
    for (const sg::Element& e : elements()) {
        if (e.kind != kBody) continue;
        const std::string& m = text_of(e, kMode);
        n += m != mode::rest && m != mode::pinned ? 1 : 0;
    }
    return n;
}

void Bodies::pose_parts(double dt) {
    static const sg::Key bent{"bend"};
    for (sg::Element& p : elements()) {
        if (p.kind != kPart) continue;
        const sg::Element* b = find(sg::Key{text_of(p, kOf)});
        if (!b) continue;
        // A sheet in the hand sags: held by its middle, its edges curl back
        // from the face it shows (its body's x, the part's thinnest way) -
        // some 13% of it at its corners; let go, it lies flat again. Eased.
        const bool sheet = b->params.get_or<std::string>("shape", "") == "sheet";
        const double sag = sheet && b->params.get_or<std::string>("mode", "") == mode::held
                               ? -0.13 * std::max(b->params.num(sg::keys::w), b->params.num(sg::keys::h))
                               : 0.0;
        const double was = p.params.num(bent);
        if (sheet && was != sag) {
            const double now = was + (sag - was) * std::min(1.0, dt * 8.0);
            p.params.set(bent, std::fabs(now - sag) < 1e-4 ? sag : now);
        }
        // (Its body as it was when it was last posed: nothing to do.)
        uint64_t& seen = posed_[p.id];
        if (seen == b->params.stamp() && seen != 0) continue;
        seen = b->params.stamp();
        const rigid::V3 x{b->params.num(sg::keys::x), b->params.num(sg::keys::y), b->params.num(sg::keys::z)};
        const rigid::M3 r = turn_of(b->params);
        // (A thing of the place stands on its foot and turns about its middle.)
        const rigid::V3 base = x + r * rigid::V3{p.params.num("lx"), p.params.num("ly"), p.params.num("lz")} - rigid::V3{0, p.params.num("lift"), 0};
        double yaw = 0, pitch = 0, roll = 0;
        rigid::to_euler(r * rel_of(p.params), yaw, pitch, roll);
        // (Standing upright again, it keeps no tilt too small to see.)
        if (std::fabs(pitch) < 1e-5) pitch = 0.0;
        if (std::fabs(roll) < 1e-5) roll = 0.0;
        put(p.params, sg::keys::x, base.x), put(p.params, sg::keys::y, base.y), put(p.params, sg::keys::z, base.z);
        put(p.params, sg::keys::yaw, yaw), put(p.params, sg::keys::pitch, pitch), put(p.params, sg::keys::roll, roll);
        // (Hidden, not even its shadow is seen.)
        const bool hidden = b->params.num("hidden") > 0.5;
        put(p.params, sg::Key{"unseen"}, hidden ? 1.0 : p.params.num("seen_as"));
        put(p.params, sg::Key{"cast"}, hidden ? 0.0 : p.params.num("cast_as", 1.0));
    }
}

sg::Key Bodies::walker(sg::Key place) {
    const sg::Key id{this->id().str() + ".walker." + place.str()};
    if (!find(id)) add_element(id, sg::Key{"walker"}).params.set("walks", 0.0).set("push_x", 0.0).set("push_z", 0.0).set("push_n", 0.0).set("taken", 0.0);
    return id;
}

void Bodies::walk(double dt) {
    if (dt <= 0) return;
    constexpr double kRadius = 0.25;
    std::vector<sg::Event> knocks;
    for (sg::Element& w : elements()) {
        if (w.kind != sg::Key{"walker"}) continue;
        sg::Params& p = w.params;
        // (Not walking - leaned in to a screen, holding one - or the last way
        // out not yet taken by the place: nothing worked out now.)
        if (p.num("walks") < 0.5) {
            p.erase(sg::Key{"was_x"});
            continue;
        }
        if (p.num("taken") != p.num("push_n")) continue;
        // Her eye in these bodies' frame: the place's, where it stands here.
        const double yaw = p.num("in_yaw"), c = std::cos(yaw), sn = std::sin(yaw);
        const double lx = p.num(sg::keys::x), lz = p.num(sg::keys::z);
        const sg::Vec3d eye{p.num("in_x") + c * lx - sn * lz, p.num("in_y") + p.num(sg::keys::y), p.num("in_z") + sn * lx + c * lz};
        const sg::rigid::V3 moved = p.has(sg::Key{"was_x"}) ? sg::rigid::V3{eye.x - p.num("was_x"), 0.0, eye.z - p.num("was_z")} : sg::rigid::V3{};
        std::vector<Physics::Shove> limbs;
        const sg::rigid::V3 q = walk_into({eye.x, 0, eye.z}, kRadius, p.num("in_y"), eye.y, moved, dt, &limbs);
        for (const Physics::Shove& sh : limbs)
            knocks.push_back(sg::Event{knock_event(), sg::Params{}
                                                          .set("id", sh.id)
                                                          .set(sg::keys::x, sh.carried.x)
                                                          .set(sg::keys::y, sh.carried.y)
                                                          .set(sg::keys::z, sh.carried.z)
                                                          .set("lx", sh.lean.x)
                                                          .set("ly", sh.lean.y)
                                                          .set("lz", sh.lean.z)});
        p.set("was_x", q.x).set("was_z", q.z);
        if (q.x == eye.x && q.z == eye.z) continue;
        // Kept out: the way, turned back into the place's frame.
        const double dx = q.x - eye.x, dz = q.z - eye.z;
        p.set("push_x", c * dx + sn * dz).set("push_z", -sn * dx + c * dz).set("push_n", p.num("push_n") + 1.0);
    }
    for (sg::Event& k : knocks) hear(std::move(k));
}




namespace {

bool hung(const sg::Element& e) { return !e.params.get_or<std::string>(sg::keys::parent.str(), "").empty(); }

// A thing's model, if it was made from one (realms::Realm::build).
const sg::sculpt::Model* model_of(sg::StateGraph& g, const sg::Element& e) {
    const std::string by = e.params.get_or<std::string>("made_by", "");
    auto* m = by.empty() ? nullptr : dynamic_cast<sg::Modeler*>(g.find(sg::Key{by}));
    return m ? &m->model() : nullptr;
}

// The top of a model over a grid in its own frame (x, z from its foot's
// middle), a box under each run of cells of one height: what lies on it
// lies on what it is, not on the box round it. A box goes down to the
// lowest of the model over its cell - to the floor where the model comes
// down to it, but under an overhang (a lamp's arm, a shelf, a seat) only
// as low as the overhang: what stands under it stands in the air there,
// not in the model.
void tops(Bodies& ph, const std::string& name, const sg::sculpt::Model& m, const Vec3d& base, double yaw) {
    const Vec3d lo = m.lo, hi = m.hi, foot = m.foot();
    const double wx = hi.x - lo.x, wz = hi.z - lo.z;
    const double cell = std::clamp(std::max(wx, wz) / 16.0, 0.03, 0.12);
    const int nx = std::max(1, static_cast<int>(std::ceil(wx / cell))), nz = std::max(1, static_cast<int>(std::ceil(wz / cell)));
    std::vector<double> top(static_cast<std::size_t>(nx * nz), -1.0), low(static_cast<std::size_t>(nx * nz), 1e9);
    const auto put = [&](int i, int j, double y) {
        if (i < 0 || j < 0 || i >= nx || j >= nz) return;
        double& t = top[static_cast<std::size_t>(j * nx + i)];
        t = std::max(t, y);
        double& l = low[static_cast<std::size_t>(j * nx + i)];
        l = std::min(l, y);
    };
    for (const sg::sculpt::Part& p : m.parts) {
        const auto& c = p.corners;
        for (std::size_t k = 0; k + 23 < c.size(); k += 24) {
            const double ax = c[k], ay = c[k + 1], az = c[k + 2], bx = c[k + 8], by = c[k + 9], bz = c[k + 10], cx = c[k + 16], cy = c[k + 17],
                         cz = c[k + 18];
            for (const double* v : {&ax, &bx, &cx}) {
                const double* yv = v == &ax ? &ay : v == &bx ? &by : &cy;
                const double* zv = v == &ax ? &az : v == &bx ? &bz : &cz;
                put(static_cast<int>((*v - lo.x) / cell), static_cast<int>((*zv - lo.z) / cell), *yv);
            }
            // The cells whose middles the face covers, at its height there.
            const double den = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz);
            if (std::fabs(den) < 1e-12) continue;
            const int i0 = static_cast<int>((std::min({ax, bx, cx}) - lo.x) / cell), i1 = static_cast<int>((std::max({ax, bx, cx}) - lo.x) / cell);
            const int j0 = static_cast<int>((std::min({az, bz, cz}) - lo.z) / cell), j1 = static_cast<int>((std::max({az, bz, cz}) - lo.z) / cell);
            for (int j = j0; j <= j1; ++j)
                for (int i = i0; i <= i1; ++i) {
                    const double px = lo.x + (i + 0.5) * cell, pz = lo.z + (j + 0.5) * cell;
                    const double l1 = ((bz - cz) * (px - cx) + (cx - bx) * (pz - cz)) / den;
                    const double l2 = ((cz - az) * (px - cx) + (ax - cx) * (pz - cz)) / den;
                    const double l3 = 1 - l1 - l2;
                    if (l1 < -1e-6 || l2 < -1e-6 || l3 < -1e-6) continue;
                    put(i, j, l1 * ay + l2 * by + l3 * cy);
                }
        }
    }
    const rigid::M3 turn = rigid::from_euler(yaw, 0, 0);
    int n = 0;
    for (int j = 0; j < nz; ++j)
        for (int i = 0; i < nx;) {
            const double h = top[static_cast<std::size_t>(j * nx + i)];
            if (h <= lo.y + 0.004) {
                ++i;
                continue;
            }
            // (Within a few centimetres of its foot, it comes down to it.)
            const auto bottom = [&](int k) {
                const double l = low[static_cast<std::size_t>(j * nx + k)];
                return l < lo.y + 0.06 ? lo.y : l;
            };
            const double b = bottom(i);
            int e = i + 1;
            double hmax = h, bmin = b;
            while (e < nx && std::fabs(top[static_cast<std::size_t>(j * nx + e)] - h) < 0.02 && std::fabs(bottom(e) - b) < 0.02)
                hmax = std::max(hmax, top[static_cast<std::size_t>(j * nx + e)]), bmin = std::min(bmin, bottom(e)), ++e;
            const double h0 = std::max(hmax - bmin, 0.01);
            const rigid::V3 mid{lo.x + (i + e) * 0.5 * cell - foot.x, bmin - lo.y + h0 * 0.5, lo.z + (j + 0.5) * cell - foot.z};
            const rigid::V3 w = turn * mid;
            ph.fixed(name + "." + std::to_string(n++), {base.x + w.x, base.y + w.y, base.z + w.z}, {(e - i) * cell * 0.5, h0 * 0.5, cell * 0.5}, yaw);
            i = e;
        }
}

}  // namespace

void lend_bodies(Room& p) {
    // Where it is embedded in the place: open, for as long as the place is.
    p.portal(p.part("loose"), {0, 0, 0}, 0.0, 0.0).params.set("frame", 0.0).set(sg::keys::open, true).set("unseen", 1.0);
}

Bodies& make_bodies(sg::StateGraph& g, Room& p, const std::string& name) {
    const double W = p.params().num("room_w"), D = p.params().num("room_d"), H = p.params().num("room_h");
    auto& ph = g.add<Bodies>(sg::Key{name + "_loose"}, W, D, H);
    lend_bodies(p);
    return ph;
}

Bodies& give_bodies(sg::StateGraph& g, Room& p, const std::string& name) {
    auto& ph = make_bodies(g, p, name);
    // (One room: the physics' own shell is its floor, ceiling and four
    // walls; its walls and plugs are said as for any space.)
    lay_space(ph, {sg::PlacedRoom{&p, sg::Pose{}, {}}});
    fill_bodies(g, ph, p, sg::Pose{});
    return ph;
}

sg::Pose physics_frame(const Room& p) {
    const sg::Element* e = p.find(p.part("loose"));
    if (!e) return sg::Pose{};
    sg::Pose at;
    at.position = {e->params.num("in_x"), e->params.num("in_y"), e->params.num("in_z")};
    at.yaw = e->params.num("in_yaw");
    return at;
}

void set_physics_frame(Room& p, const sg::Pose& at) {
    p.element(p.part("loose")).params.set("in_x", at.position.x).set("in_y", at.position.y).set("in_z", at.position.z).set("in_yaw", at.yaw);
}

void fill_bodies(sg::StateGraph& g, Bodies& ph, Room& p, const sg::Pose& at) {
    // A point of the place, in the physics' frame.
    const auto there = [&](const Vec3d& v) { return at.position + sg::rotate_xz(v, at.yaw); };
    const std::string room = p.id().str() + ".";
    std::map<std::string, std::vector<const sg::Element*>> loose;
    int n = 0;
    for (const sg::Element& e : p.elements()) {
        if (!e.alive) continue;
        const std::string tag = room + std::to_string(n++);
        const Vec3d at_e = sg::position_of(e);
        const double sx = e.params.num(sg::keys::sx, 1.0), sy = e.params.num(sg::keys::sy, 1.0), sz = e.params.num(sg::keys::sz, 1.0);
        if (e.kind != sg::kinds::mesh) continue;
        if (!e.params.get_or<std::string>("loose", "").empty()) {
            loose[e.params.get_or<std::string>("loose", "")].push_back(&e);
            continue;
        }
        // What stands in the place, fixed: not what hangs on something
        // that moves (a door's leaf, a person), not what is not there.
        if (hung(e) || (e.params.num("unseen") > 0.5 && e.params.num("cast", 1.0) < 0.5)) continue;
        // (Leaves: air to what is thrown through them.)
        if (e.params.num("airy") > 0.5) continue;
        if (e.params.num("pitch") != 0.0 || e.params.num("roll") != 0.0) continue;
        const double yaw = e.params.num(sg::keys::yaw);
        if (const std::string hulls = e.params.get_or<std::string>("hulls", ""); !hulls.empty()) {
            const rigid::M3 turn = rigid::from_euler(yaw, 0, 0);
            std::istringstream in(hulls);
            std::string one;
            int k = 0;
            while (std::getline(in, one, ';')) {
                std::istringstream ls(one);
                double cx = 0, cy = 0, cz = 0, hx = 0, hy = 0, hz = 0;
                if (!(ls >> cx >> cy >> cz >> hx >> hy >> hz)) continue;
                const rigid::V3 w = turn * rigid::V3{cx, cy, cz};
                ph.fixed("h" + tag + "." + std::to_string(k++), there({at_e.x + w.x, at_e.y + w.y, at_e.z + w.z}), {hx, hy, hz}, yaw + at.yaw);
            }
            continue;
        }
        // (Only the first part of a model: the others are the same box.)
        const sg::sculpt::Model* m = model_of(g, e);
        if (m && e.id.str() + ".model" != e.params.get_or<std::string>("made_by", "")) continue;
        if (m && std::max(sx, sz) > 0.15 && sy > 0.03) tops(ph, "m" + tag, *m, there(at_e), yaw + at.yaw);
        else if (sx > 1e-4 && sy > 1e-4 && sz > 1e-4) ph.fixed("b" + tag, there({at_e.x, at_e.y + sy * 0.5, at_e.z}), {sx * 0.5, sy * 0.5, sz * 0.5}, yaw + at.yaw);
    }
    // The loose things: a body each, in the box of its first part, and every
    // part of it posed from that body.
    for (const auto& [thing, parts] : loose) {
        const sg::Element* first = parts.front();
        for (const sg::Element* e : parts)
            if (e->id.str() == thing) first = e;
        const sg::Params& q = first->params;
        const double sx = q.num(sg::keys::sx, 0.1), sy = q.num(sg::keys::sy, 0.1), sz = q.num(sg::keys::sz, 0.1);
        const double yaw = q.num(sg::keys::yaw), pitch = q.num(sg::keys::pitch), roll = q.num(sg::keys::roll);
        // (A thing of the place stands on its foot and turns about its middle.)
        const Vec3d base = there(sg::position_of(*first));
        const sg::Key body{"@" + thing};
        if (q.num("loose_sheet") > 0.5) {
            // Paper: its face the way it is thinnest, its writing's up as it says.
            const rigid::M3 r = rigid::from_euler(yaw + at.yaw, pitch, roll);
            const int thin = sx <= sy && sx <= sz ? 0 : sy <= sz ? 1 : 2;
            const rigid::V3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
            const rigid::V3 face = r * axes[thin], up = r * rigid::V3{q.num("loose_ux"), q.num("loose_uy", 1.0), q.num("loose_uz")};
            const double size[3] = {sx, sy, sz};
            const rigid::V3 u{q.num("loose_ux"), q.num("loose_uy", 1.0), q.num("loose_uz")};
            const double high = std::fabs(u.x) * sx + std::fabs(u.y) * sy + std::fabs(u.z) * sz;
            const double wide = sx + sy + sz - size[thin] - high;
            ph.sheet(body, {base.x, base.y + sy * 0.5, base.z}, wide, high, size[thin], {face.x, face.y, face.z}, {up.x, up.y, up.z},
                     q.num("loose_pinned") > 0.5);
            ph.element(body).params.set("sound", q.get_or<std::string>("loose_sound", "paper"));
            // (A sheet of one of the place's papers: which, to whoever reads it.)
            if (const std::string d = q.get_or<std::string>("loose_document", ""); !d.empty()) ph.element(body).params.set("document", d);
        } else {
            sg::Element& b = ph.solid(body, {base.x, base.y + sy * 0.5, base.z}, {sx * 0.5, sy * 0.5, sz * 0.5}, yaw + at.yaw, pitch, roll, q.num("loose_mass", 0.5));
            b.params.set("sound", q.get_or<std::string>("loose_sound", "wood"));  // (what it is made of: how it sounds)
            // (A book: what it is, to whoever opens it.)
            // (A paper: which of the house's it is, to whoever reads it.)
            if (const std::string d = q.get_or<std::string>("loose_document", ""); !d.empty()) b.params.set("document", d);
            if (const std::string t = q.get_or<std::string>("loose_title", ""); !t.empty())
                b.params.set("title", t).set("author", q.get_or<std::string>("loose_author", ""))
                    .set("cover_r", q.num("loose_cover_r", 0.3)).set("cover_g", q.num("loose_cover_g", 0.06)).set("cover_b", q.num("loose_cover_b", 0.04));
            // (Let fall where it was put, to rest on what is under it: put a
            // little over a top, it lies on it, not over it.)
            if (q.num("loose_settle") > 0.5) Physics::place(b, {base.x, base.y + sy * 0.5, base.z}, yaw + at.yaw, pitch, roll, mode::fall);
        }
        for (const sg::Element* e : parts) {
            const double lift = e->params.num(sg::keys::sy, 0.1) * 0.5;
            const Vec3d foot = there(sg::position_of(*e));
            ph.part(e->id, body, {foot.x, foot.y + lift, foot.z}, lift, e->params.num(sg::keys::yaw) + at.yaw, e->params.num(sg::keys::pitch),
                    e->params.num(sg::keys::roll), e->params.num("unseen") > 0.5, e->params.num("cast", 1.0));
        }
    }
}

void join_bodies(sg::StateGraph& g, Bodies& ph, const std::vector<Room*>& places) {
    // (Its parts are named by the things of the place they were made from:
    // the same thing on another sheet of that room is named by that sheet's
    // place. A room is its places' name without the sheet's number.)
    const auto room_of = [](std::string s) {
        while (!s.empty() && std::isdigit(static_cast<unsigned char>(s.back()))) s.pop_back();
        return s;
    };
    std::set<std::string> carried;
    for (Room* p : places) {
        sg::Functor* out = g.functor(sg::Key{ph.id().str() + ".out." + p->id().str()});
        if (!out) continue;
        // From the physics' frame into the place's: where the place stands
        // in it, undone - a turn about up and a move, affine in the pose.
        const sg::Pose at = physics_frame(*p);
        const double c = std::cos(at.yaw), s = std::sin(at.yaw);
        const double qx = at.position.x, qy = at.position.y, qz = at.position.z;
        sg::Affine undo;
        undo.set(sg::keys::x, {{sg::keys::x, c}, {sg::keys::z, s}}, -c * qx - s * qz);
        undo.set(sg::keys::y, {{sg::keys::y, 1.0}}, -qy);
        undo.set(sg::keys::z, {{sg::keys::x, -s}, {sg::keys::z, c}}, s * qx - c * qz);
        undo.set(sg::keys::yaw, {{sg::keys::yaw, 1.0}}, -at.yaw);
        undo.copy(sg::keys::pitch, sg::keys::pitch).copy(sg::keys::roll, sg::keys::roll).copy(sg::Key{"bend"}, sg::Key{"bend"}).copy(sg::Key{"unseen"}, sg::Key{"unseen"}).copy(sg::Key{"cast"}, sg::Key{"cast"});
        const bool same = at.yaw == 0.0 && qx == 0.0 && qy == 0.0 && qz == 0.0;
        const auto pose = same ? sg::transport::only({sg::keys::x, sg::keys::y, sg::keys::z, sg::keys::yaw, sg::keys::pitch, sg::keys::roll, sg::Key{"bend"}, sg::Key{"unseen"}, sg::Key{"cast"}})
                               : sg::transport::affine(undo);
        const std::string room = room_of(p->id().str());
        for (const sg::Element& e : ph.elements()) {
            if (e.kind != sg::Key{"part"}) continue;
            const std::string& id = e.id.str();
            // (A place made of rooms keeps their things' names: the part goes
            // to the thing of its own name - `lounge.tape_room2` in the hotel.)
            if (p->find(e.id)) {
                out->on_object(e.id, e.id, pose);
                carried.insert(id);
                continue;
            }
            const std::size_t dot = id.find('.');
            if (dot == std::string::npos || room_of(id.substr(0, dot)) != room) continue;
            if (!p->find(sg::Key{p->id().str() + id.substr(dot)})) continue;
            out->on_object(e.id, sg::Key{p->id().str() + id.substr(dot)}, pose);
            carried.insert(id);
        }
    }
    // (A part carried to no thing moves unseen: whatever it is, it stays where
    // it was made, however it is taken up or knocked - said, never met in play.)
    for (const sg::Element& e : ph.elements())
        if (e.kind == sg::Key{"part"} && !carried.count(e.id.str())) std::fprintf(stderr, "! %s: a part of %s that no place's thing is carried to\n", e.id.str().c_str(), ph.id().str().c_str());
}

namespace {
// The physics a room's things are in: the one its `loose` portal is open to
// (a house's bedroom and corridor: one for every sheet of both).
sg::Key bodies_key(const sg::StateGraph& g, sg::Key place) {
    const auto* r = dynamic_cast<const Room*>(g.find(place));
    const sg::Key portal = r ? r->part("loose") : sg::Key{place.str() + ".loose"};
    for (const sg::Embedding& em : g.embeddings())
        if (em.host == place && em.portal == portal) return em.guest;
    return sg::Key{};
}
}  // namespace

Bodies* bodies_of(sg::StateGraph& g, sg::Key room) {
    const sg::Key k = bodies_key(g, room);
    return k.empty() ? nullptr : dynamic_cast<Bodies*>(g.find(k));
}

const Bodies* bodies_of(const sg::StateGraph& g, sg::Key room) {
    const sg::Key k = bodies_key(g, room);
    return k.empty() ? nullptr : dynamic_cast<const Bodies*>(g.find(k));
}

bool walk_among(sg::StateGraph& g, Room& p) {
    Bodies* ph = bodies_of(g, p.id());
    if (!ph) return false;
    if (!p.find(p.part("pushed"))) p.add_element(p.part("pushed"), sg::Key{"marker"}).params.set("push_x", 0.0).set("push_z", 0.0).set("push_n", 0.0);
    const sg::Key walker = ph->walker(p.id());
    // Her eye, whether she walks and what she has taken, and where the place
    // stands in the bodies' frame: to their walker.
    sg::Functor in(sg::Key{p.id().str() + ".walks_among"}, p.id(), ph->id());
    in.on_object(p.camera_id(), walker, [](const sg::Element& cam, sg::Element& w) {
        for (const char* k : {"x", "y", "z", "walks", "taken"})
            if (w.params.num(sg::Key{k}) != cam.params.num(sg::Key{k})) w.params.set(sg::Key{k}, cam.params.num(sg::Key{k}));
    });
    in.on_object(p.part("loose"), walker, [](const sg::Element& at, sg::Element& w) {
        for (const char* k : {"in_x", "in_y", "in_z", "in_yaw"})
            if (w.params.num(sg::Key{k}) != at.params.num(sg::Key{k})) w.params.set(sg::Key{k}, at.params.num(sg::Key{k}));
    });
    // And the way the bodies keep her out of them, back to the place.
    sg::Functor out(sg::Key{p.id().str() + ".kept_out"}, ph->id(), p.id());
    out.on_object(walker, p.part("pushed"), [](const sg::Element& w, sg::Element& m) {
        for (const char* k : {"push_x", "push_z", "push_n"})
            if (m.params.num(sg::Key{k}) != w.params.num(sg::Key{k})) m.params.set(sg::Key{k}, w.params.num(sg::Key{k}));
    });
    const sg::Key in_name = in.name(), out_name = out.name();
    g.set_functor(std::move(in));
    g.set_functor(std::move(out));
    g.keep(in_name);
    g.keep(out_name);
    return true;
}



std::vector<sg::Element*> parts_of(Room& r, const std::string& thing) {
    const std::string first = r.part(thing).str(), more = first + ".";
    std::vector<sg::Element*> out;
    for (sg::Element& e : r.elements())
        if (e.alive && e.kind == sg::kinds::mesh && (e.id.str() == first || e.id.str().rfind(more, 0) == 0)) out.push_back(&e);
    return out;
}

void loose_thing(Room& r, const std::string& thing, double mass, const std::string& sound) {
    const std::vector<sg::Element*> parts = parts_of(r, thing);
    if (parts.empty()) return;  // (not made here)
    // (Paper is a sheet: it floats down, slides and settles - a block as thin
    // falls like a board.)
    if (parts.size() == 1) {
        double d[3] = {parts[0]->params.num(sg::keys::sx), parts[0]->params.num(sg::keys::sy), parts[0]->params.num(sg::keys::sz)};
        std::sort(d, d + 3);
        if (d[0] > 0.0 && d[0] < 0.005 && d[1] > 40.0 * d[0]) {
            std::fprintf(stderr, "(%s is as thin as paper: made a sheet, not a block - loose_note says so)\n", r.part(thing).str().c_str());
            loose_note(r, thing, {0, 0, 1}, false);
            return;
        }
    }
    for (sg::Element* e : parts) e->params.set("loose", r.part(thing).str()).set("loose_mass", mass).set("loose_sound", sound).set("solid", 0.0);
}

void loose_note(Room& r, const std::string& thing, const Vec3d& up, bool pinned) {
    for (sg::Element* e : parts_of(r, thing)) {
        if (e->params.has(sg::keys::parent) && !e->params.get_or<std::string>(sg::keys::parent.str(), "").empty()) return;  // (hung on something that moves)
        e->params.set("loose", r.part(thing).str()).set("loose_mass", 0.006).set("loose_sheet", 1.0).set("loose_pinned", pinned ? 1.0 : 0.0).set("loose_sound", "paper");
        e->params.set("loose_ux", up.x).set("loose_uy", up.y).set("loose_uz", up.z).set("solid", 0.0);
    }
}

}  // namespace sg::physics
