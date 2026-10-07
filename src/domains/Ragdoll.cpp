#include "sg/domains/Ragdoll.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

#include "sg/core/StateGraph.hpp"
#include "sg/core/Temporal.hpp"
#include "sg/domains/Being.hpp"

namespace sg {

namespace {

using rigid::M3;
using rigid::V3;

const Key kBone{"bone"}, kCollider{"collider"};

M3 turn_of(const Element& e, const char* p) {
    const std::string s = p;
    return spatial::from_quat(e.params.num(Key{s + "w"}, 1.0), e.params.num(Key{s + "x"}), e.params.num(Key{s + "y"}), e.params.num(Key{s + "z"}));
}
void set_turn(Element& e, const M3& m, const char* p) {
    const std::string s = p;
    double w, x, y, z;
    spatial::to_quat(m, w, x, y, z);
    e.params.set(Key{s + "w"}, w).set(Key{s + "x"}, x).set(Key{s + "y"}, y).set(Key{s + "z"}, z);
}
V3 vec(const Element& e, const char* x, const char* y, const char* z) { return {e.params.num(Key{x}), e.params.num(Key{y}), e.params.num(Key{z})}; }
void set_vec(Element& e, V3 v, const char* x, const char* y, const char* z) { e.params.set(Key{x}, v.x).set(Key{y}, v.y).set(Key{z}, v.z); }
double ease(double dt, double rate) { return rate <= 0 ? 1.0 : 1.0 - std::exp(-dt * rate); }

// The turn taking up (y) to `u`.
M3 up_to(V3 u) {
    u = spatial::normalize(u);
    const V3 k = spatial::cross({0, 1, 0}, u);
    const double s = spatial::length(k), c = u.y;
    if (s < 1e-9) return c > 0 ? M3{} : spatial::axis_angle({1, 0, 0}, 3.14159265358979);
    return spatial::axis_angle(k * (1.0 / s), std::atan2(s, c));
}

// A bone's solid, in its own frame (its joint at the origin): a rod to its
// end, or a block.
rigid::Hull shape_of(const Element& b) {
    if (b.params.num("block") > 0.5) {
        const V3 lo = vec(b, "lox", "loy", "loz"), hi = vec(b, "hix", "hiy", "hiz");
        return rigid::Hull::box((lo + hi) * 0.5, (hi - lo) * 0.5);
    }
    const V3 end = vec(b, "ex", "ey", "ez");
    const double r = b.params.num("radius", 0.04), l = std::max(spatial::length(end), 2 * r);
    return rigid::Hull::prism(end * 0.5, {r, l * 0.5, r}, 8, up_to(end));
}

// A solid of the room, seen from the being's frame.
struct Box {
    V3 centre, half;
    M3 turn;
};
Box box_in(const Element& c, const Element& self) {
    const M3 o = spatial::from_euler(self.params.num("oyaw"), 0, 0);
    const M3 t = spatial::from_euler(c.params.num(keys::yaw), c.params.num(keys::pitch), c.params.num(keys::roll));
    const V3 half{c.params.num(keys::sx, 1) * 0.5, c.params.num(keys::sy, 1) * 0.5, c.params.num(keys::sz, 1) * 0.5};
    const V3 host = vec(c, "x", "y", "z") + t * V3{0, half.y, 0};
    return {spatial::transpose(o) * (host - vec(self, "ox", "oy", "oz")), half, spatial::transpose(o) * t};
}
bool inside(const Box& b, V3 p, double r) {
    const V3 q = spatial::transpose(b.turn) * (p - b.centre);
    const V3 c{std::clamp(q.x, -b.half.x, b.half.x), std::clamp(q.y, -b.half.y, b.half.y), std::clamp(q.z, -b.half.z, b.half.z)};
    return spatial::length(q - c) < r;
}

uint64_t mix(uint64_t h, double v) {
    uint64_t b;
    static_assert(sizeof b == sizeof v);
    std::memcpy(&b, &v, sizeof b);
    return (h ^ b) * 1099511628211ull;
}
}  // namespace

Ragdoll::Ragdoll(Key id, const Being& body, double mass) : State(std::move(id)) {
    add_element(self_id(), Key{"ragdoll"})
        .params.set("mass", mass)
        .set("awake", 0.0)
        .set("still", 0.0)
        .set("strength", 1.0)
        .set("recover", 0.6)
        .set("fall_at", 150.0)
        .set("full", 0.0)
        .set("floor", 0.0)
        .set("damping", 0.7)
        .set("lead", 0.0)
        .set("ox", 0.0).set("oy", 0.0).set("oz", 0.0).set("oyaw", 0.0)
        .set("grab", std::string{})
        .set("grab_x", 0.0).set("grab_y", 0.0).set("grab_z", 0.0)
        .set("grab_force", 500.0);

    // The skeleton as it is bound: each joint's place and turn.
    const std::vector<Key> js = body.joints();
    std::unordered_map<Key, std::size_t> at;
    for (std::size_t i = 0; i < js.size(); ++i) at[js[i]] = i;
    std::vector<int> parent(js.size(), -1);
    std::vector<V3> made(js.size()), p(js.size());
    std::vector<M3> bind(js.size());
    std::vector<std::vector<std::size_t>> kids(js.size());
    for (std::size_t i = 0; i < js.size(); ++i) {
        const Element& j = body.element(js[i]);
        auto it = at.find(Key{j.params.get_or<std::string>("parent_joint", "")});
        if (it != at.end() && it->second < i) parent[i] = int(it->second), kids[it->second].push_back(i);
        made[i] = vec(j, "x", "y", "z");
        bind[i] = turn_of(j, "bq");
        p[i] = parent[i] < 0 ? made[i] : p[std::size_t(parent[i])] + bind[std::size_t(parent[i])] * made[i];
    }
    // The legs: what hangs down from a root, and the root; they keep it
    // standing unless it is let fall.
    // (A root's child is a leg if the bone it begins - to its own first
    // child - points down.)
    std::vector<bool> leg(js.size(), false);
    for (std::size_t i = 0; i < js.size(); ++i) {
        if (parent[i] < 0) {
            leg[i] = true;
            continue;
        }
        const std::size_t up = std::size_t(parent[i]);
        if (!leg[up]) continue;
        if (parent[up] >= 0) leg[i] = true;
        else if (!kids[i].empty() && spatial::normalize(p[kids[i][0]] - p[i]).y < -0.5) leg[i] = true;
    }

    // Each bone: a rod to its child, a block round its children, or a short
    // rod on from its parent; weighed by its size, the whole as heavy as said.
    std::vector<double> raw(js.size());
    double total = 0;
    for (std::size_t i = 0; i < js.size(); ++i) {
        Element& b = add_element(js[i], kBone);
        const Element& j = body.element(js[i]);
        b.params.set("parent", parent[i] < 0 ? std::string{} : js[std::size_t(parent[i])].str());
        b.params.set("leg", leg[i] ? 1.0 : 0.0).set("omega", leg[i] ? 22.0 : 14.0).set("cone", 1.9).set("weak", 1.0).set("weak_rate", 1.25);
        set_vec(b, made[i], "jx", "jy", "jz");
        double vol;
        if (kids[i].size() >= 2) {
            V3 lo{}, hi{};
            for (std::size_t k : kids[i])
                lo = {std::min(lo.x, made[k].x), std::min(lo.y, made[k].y), std::min(lo.z, made[k].z)},
                hi = {std::max(hi.x, made[k].x), std::max(hi.y, made[k].y), std::max(hi.z, made[k].z)};
            const double r = std::clamp(0.3 * spatial::length(hi - lo), 0.04, 0.12);
            lo = lo - V3{r, r, r} * 0.6, hi = hi + V3{r, r, r} * 0.6;
            b.params.set("block", 1.0);
            set_vec(b, lo, "lox", "loy", "loz");
            set_vec(b, hi, "hix", "hiy", "hiz");
            const V3 d = hi - lo;
            vol = d.x * d.y * d.z;
        } else {
            V3 end;
            if (kids[i].size() == 1) end = made[kids[i][0]];
            else {
                const V3 from = parent[i] < 0 ? V3{0, 1, 0} : p[i] - p[std::size_t(parent[i])];
                const double l = std::clamp(0.5 * spatial::length(made[i]), 0.06, 0.2);
                end = spatial::transpose(bind[i]) * spatial::normalize(from) * l;
            }
            const double l = spatial::length(end), r = std::clamp(0.2 * l, 0.025, 0.09);
            b.params.set("block", 0.0).set("radius", r);
            set_vec(b, end, "ex", "ey", "ez");
            vol = 3.14159265 * r * r * std::max(l, 2 * r);
        }
        raw[i] = vol, total += vol;
        // Bound: how it turns on its parent at rest (where the cone is).
        set_turn(b, parent[i] < 0 ? bind[i] : spatial::transpose(bind[std::size_t(parent[i])]) * bind[i], "r");
        // Where it is now, and where it is meant to be: the being as it stands.
        set_vec(b, vec(j, "px", "py", "pz"), "x", "y", "z");
        set_turn(b, turn_of(j, "pq"), "q");
        set_vec(b, vec(j, "px", "py", "pz"), "tx", "ty", "tz");
        set_turn(b, turn_of(j, "pq"), "aim");
        set_vec(b, {}, "vx", "vy", "vz");
        set_vec(b, {}, "wx", "wy", "wz");
        b.params.set("knock_n", 0.0).set("knock_seen", 0.0);
    }
    // Neighbours no more than ten times each other: a light hand on a heavy
    // arm shakes.
    for (std::size_t i = 0; i < js.size(); ++i) {
        double m = mass * raw[i] / std::max(total, 1e-9);
        if (parent[i] >= 0) m = std::max(m, element(js[std::size_t(parent[i])]).params.num("mass") * 0.1);
        element(js[i]).params.set("mass", std::max(m, 0.05));
    }
    // What each muscle moves: not its own bone alone but all that hangs from
    // it, turned about its joint (kp = I w^2 for that I). The solver's
    // spring sees only the bone, so its stiffness and damping are raised by
    // how much heavier to turn the whole is (`gain`).
    std::vector<double> own(js.size()), sub(js.size(), 0.0);
    std::vector<V3> com(js.size());
    for (std::size_t i = 0; i < js.size(); ++i) {
        rigid::Body b;
        b.hulls.push_back(shape_of(element(js[i])));
        b.set_mass(element(js[i]).params.num("mass"));
        const M3& inv = b.inv_inertia_local;
        own[i] = 3.0 / std::max(1e-9, inv(0, 0) + inv(1, 1) + inv(2, 2));
        com[i] = p[i] + bind[i] * b.com_local;
    }
    for (std::size_t i = 0; i < js.size(); ++i)
        for (std::size_t k = i, n = 0; n < js.size(); ++n) {  // i's bone adds to every joint above it
            const V3 d = com[i] - p[k];
            sub[k] += element(js[i]).params.num("mass") * spatial::dot(d, d) + own[i];
            if (parent[k] < 0) break;
            k = std::size_t(parent[k]);
        }
    // Neighbours no more than ten times each other to turn, either: a thin
    // bone between two heavy ones is given what it lacks (`armature`), or
    // a muscle between them would spin it and barely move them.
    std::vector<double> stiff = own;
    for (int pass = 0; pass < 2; ++pass)
        for (std::size_t i = 0; i < js.size(); ++i) {
            double most = 0;
            if (parent[i] >= 0) most = std::max(most, stiff[std::size_t(parent[i])]);
            for (std::size_t k : kids[i]) most = std::max(most, stiff[k]);
            stiff[i] = std::max(stiff[i], most * 0.1);
        }
    for (std::size_t i = 0; i < js.size(); ++i) {
        // What the solver's spring turns: the bone against its parent (a
        // root stands fixed), as against all that hangs from it.
        const double pair = parent[i] < 0 ? stiff[i] : 1.0 / (1.0 / stiff[i] + 1.0 / stiff[std::size_t(parent[i])]);
        element(js[i]).params.set("armature", stiff[i] - own[i]).set("gain", std::sqrt(std::max(1.0, sub[i] / pair)));
    }

    //   self --step--> self: the body moved for `dt` - asleep, where it is
    //   meant to be; awake, as it is pushed, pulled and moved by its muscles.
    loop(Key{"step"}, self_id(), step_event(), [](State& s, Element&, Element*, const Event& ev) { static_cast<Ragdoll&>(s).step(ev.args.num(keys::dt)); });
    //   self --hit--> self: an impulse on a bone; it and its neighbours weakened.
    loop(Key{"hit"}, self_id(), hit_event(), [](State& s, Element& self, Element*, const Event& ev) {
        Element* b = s.find(Key{ev.args.get_or<std::string>("bone", "")});
        if (!b || b->kind != kBone) return;
        const V3 j{ev.args.num(keys::x), ev.args.num(keys::y), ev.args.num(keys::z)};
        set_vec(*b, vec(*b, "vx", "vy", "vz") + j * (1.0 / b->params.num("mass", 1.0)), "vx", "vy", "vz");
        const double weaken = std::clamp(ev.args.num("weaken", 0.25), 0.0, 1.0), rate = 1.0 / std::max(0.05, ev.args.num("for", 0.8));
        for (Element& n : s.elements()) {
            if (n.kind != kBone) continue;
            const bool near = &n == b || n.id.str() == b->params.get_or<std::string>("parent", "") || n.params.get_or<std::string>("parent", "") == b->id.str();
            if (near) n.params.set("weak", std::max(0.02, n.params.num("weak", 1.0) * weaken)).set("weak_rate", rate);
        }
        if (spatial::length(j) >= self.params.num("fall_at", 150.0)) self.params.set("strength", 0.0);
        self.params.set("awake", 1.0).set("still", 0.0);
    });
    //   self --grab--> self: a hand holding a bone, pulling it toward a point.
    loop(Key{"grab"}, self_id(), grab_event(), [](State& s, Element& self, Element*, const Event& ev) {
        const std::string bone = ev.args.get_or<std::string>("bone", "");
        const Element* b = s.find(Key{bone});
        if (!b || b->kind != kBone) return;
        self.params.set("grab", bone).set("grab_x", ev.args.num(keys::x)).set("grab_y", ev.args.num(keys::y)).set("grab_z", ev.args.num(keys::z));
        self.params.set("grab_force", ev.args.num("force", 500.0)).set("awake", 1.0).set("still", 0.0);
    });
    loop(Key{"let_go"}, self_id(), let_go_event(), [](State&, Element& self, Element*, const Event&) { self.params.set("grab", std::string{}); });
}

rigid::Hull Ragdoll::hull(const Element& bone) { return shape_of(bone); }

std::vector<Key> Ragdoll::bones() const {
    std::vector<Key> out;
    for (const Element& e : elements())
        if (e.kind == kBone) out.push_back(e.id);
    return out;
}

bool Ragdoll::disturbed() const {
    // Held, or a bone that has weight where it is meant to be meets a solid.
    const Element& self = element(self_id());
    if (!self.params.get_or<std::string>("grab", "").empty()) return true;
    const bool full = self.params.num("full") > 0.5;
    std::vector<Box> solids;
    for (const Element& c : elements())
        if (c.kind == kCollider && c.alive) solids.push_back(box_in(c, self));
    const double floor = self.params.num("floor");
    for (const Element& b : elements()) {
        if (b.kind != kBone || (!full && b.params.num("leg") > 0.5)) continue;
        const V3 at = vec(b, "tx", "ty", "tz");
        const M3 t = turn_of(b, "aim");
        const bool block = b.params.num("block") > 0.5;
        const double r = block ? 0.5 * spatial::length(vec(b, "hix", "hiy", "hiz") - vec(b, "lox", "loy", "loz")) : b.params.num("radius");
        const V3 end = at + t * (block ? (vec(b, "hix", "hiy", "hiz") + vec(b, "lox", "loy", "loz")) * 0.5 : vec(b, "ex", "ey", "ez"));
        for (V3 q : {at, end}) {
            if (q.y - r < floor - 1e-3 && !block) return true;
            for (const Box& s : solids)
                if (inside(s, q, r)) return true;
        }
    }
    return false;
}

void Ragdoll::build(rigid::World& w) const {
    // The solver's world, as the params say: what is fixed (the floor, the
    // room's solids), and a body for each bone at its ball.
    w = rigid::World{};
    w.sleep_after = 1e9;  // it sleeps as a whole, by its own measure
    w.iterations = 8;     // stiff muscles on light bones want a converged solve
    const Element& self = element(self_id());
    rigid::Body floor;
    floor.id = "floor";
    floor.hulls.push_back(rigid::Hull::box({0, self.params.num("floor") - 0.5, 0}, {200, 0.5, 200}));
    floor.set_mass(0);
    w.add(floor);
    for (const Element& c : elements()) {
        if (c.kind != kCollider || !c.alive) continue;
        const Box b = box_in(c, self);
        rigid::Body s;
        s.id = "solid:" + c.id.str();
        s.hulls.push_back(rigid::Hull::box(b.centre, b.half, b.turn));
        s.set_mass(0);
        w.add(s);
    }
    for (const Element& b : elements()) {
        if (b.kind != kBone) continue;
        rigid::Body body;
        body.id = b.id.str();
        body.hulls.push_back(shape_of(b));
        body.group = 1;
        body.friction = 0.8, body.restitution = 0.05;
        body.set_mass(b.params.num("mass", 1.0));
        if (const double a = b.params.num("armature"); a > 0) {
            M3 i = spatial::inverse(body.inv_inertia_local);
            i(0, 0) += a, i(1, 1) += a, i(2, 2) += a;
            body.inv_inertia_local = spatial::inverse(i);
        }
        w.add(body);
    }
    for (const Element& b : elements()) {
        const std::string up = b.params.get_or<std::string>("parent", "");
        if (b.kind != kBone || up.empty()) continue;
        rigid::Joint j;
        j.kind = rigid::Joint::Ball;
        j.a = up, j.b = b.id.str();
        j.la = vec(b, "jx", "jy", "jz"), j.lb = {};
        j.muscle = true, j.limit_cone = true;
        w.joints.push_back(j);
    }
}

void Ragdoll::step(double dt) {
    if (dt <= 0) return;
    Element& self = element(self_id());
    const double strength = self.params.num("strength", 1.0);
    self.params.set("strength", strength + (1.0 - strength) * ease(dt, self.params.num("recover", 0.6)));
    for (Element& b : elements())
        if (b.kind == kBone) b.params.set("weak", b.params.num("weak", 1.0) + (1.0 - b.params.num("weak", 1.0)) * ease(dt, b.params.num("weak_rate", 1.25)));

    if (self.params.num("awake") < 0.5) {
        // Asleep: each bone where the being means it, at the speed it got
        // there - so, woken, it goes on as it was going.
        for (Element& b : elements()) {
            if (b.kind != kBone) continue;
            const V3 x = vec(b, "x", "y", "z"), t = vec(b, "tx", "ty", "tz");
            const M3 q = turn_of(b, "q"), tq = turn_of(b, "aim");
            set_vec(b, (t - x) * (1.0 / dt), "vx", "vy", "vz");
            set_vec(b, spatial::log_map(tq * spatial::transpose(q)) * (1.0 / dt), "wx", "wy", "wz");
            set_vec(b, t, "x", "y", "z");
            set_turn(b, tq, "q");
        }
    }
    // Knocks not yet taken: each an impulse on its bone, which it weakens a
    // little - and wakes it.
    bool knocked = false;
    for (Element& b : elements()) {
        if (b.kind != kBone || b.params.num("knock_n") == b.params.num("knock_seen")) continue;
        const V3 k = vec(b, "kx", "ky", "kz");
        set_vec(b, vec(b, "vx", "vy", "vz") + k * (1.0 / b.params.num("mass", 1.0)), "vx", "vy", "vz");
        b.params.set("knock_seen", b.params.num("knock_n")).set("weak", b.params.num("weak", 1.0) * 0.6);
        knocked = true;
    }
    if (self.params.num("awake") < 0.5) {
        if (!knocked && !disturbed()) {
            self.params.set("lead", 0.0);
            return;
        }
        self.params.set("awake", 1.0).set("still", 0.0);
    }
    self.params.set("lead", 1.0);

    // The world, as it is now; started from the same contacts when this very
    // step is tried again.
    rigid::World w;
    build(w);
    uint64_t in = 1469598103934665603ull;
    in = mix(in, dt);
    for (const Element& e : elements())
        for (const auto& [k, v] : e.params)
            if (const double* d = std::get_if<double>(&v)) in = mix(in, *d);
    if (in != memo_in_) memo_ = last_, memo_in_ = in;
    w.set_contacts(memo_);

    const bool full = self.params.num("full") > 0.5;
    const double strong = self.params.num("strength", 1.0), zeta = self.params.num("damping", 0.9);
    struct Driven {
        rigid::Body* body;
        V3 from, to;
        M3 from_r, to_r;
    };
    std::vector<Driven> driven;
    for (Element& b : elements()) {
        if (b.kind != kBone) continue;
        rigid::Body* body = w.find(b.id.str());
        body->x = vec(b, "x", "y", "z");
        body->r = turn_of(b, "q");
        body->v = vec(b, "vx", "vy", "vz");
        body->w = vec(b, "wx", "wy", "wz");
        body->place();
        // Its muscles hold it up as strongly as they are: limp, it has all
        // its weight; strong, they carry it (gravity met where it falls).
        const double s = strong * b.params.num("weak", 1.0);
        body->receives = {{"gravity", field::Response::Acceleration, 1.0 - s * 0.95}};
        if (!full && b.params.num("leg") > 0.5) driven.push_back({body, body->x, vec(b, "tx", "ty", "tz"), body->r, turn_of(b, "aim")});
    }
    for (rigid::Joint& j : w.joints) {
        const Element& a = element(Key{j.a});
        const Element& b = element(Key{j.b});
        const double s = strong * b.params.num("weak", 1.0);
        j.aim = spatial::transpose(turn_of(a, "aim")) * turn_of(b, "aim");
        j.rest_turn = turn_of(b, "r");
        j.cone = b.params.num("cone", 1.9);
        const double gain = b.params.num("gain", 1.0);
        j.aim_hertz = std::max(0.2, b.params.num("omega", 14.0) * s) * gain / (2 * 3.14159265358979);
        j.aim_damping = zeta * gain;
        j.aim_torque = 2.0 + 600.0 * s;  // what is left of a limp joint: its friction
    }
    const std::string held = self.params.get_or<std::string>("grab", "");
    const int n = std::max(1, int(std::ceil(dt * 120.0 - 1e-9)));
    const double h = dt / n;
    for (int k = 1; k <= n; ++k) {
        const double f = double(k) / n;
        for (const Driven& d : driven) {
            const V3 turn = spatial::log_map(d.to_r * spatial::transpose(d.from_r));
            const double a = spatial::length(turn);
            w.drive(*d.body, d.from + (d.to - d.from) * f, (a > 1e-12 ? spatial::axis_angle(turn * (1.0 / a), a * f) : M3{}) * d.from_r);
        }
        if (!held.empty())
            if (rigid::Body* g = w.find(held))
                w.grab(held, g->com_local, vec(self, "grab_x", "grab_y", "grab_z"), self.params.num("grab_force", 500.0)).turns = false;
        w.step(h);
        if (!held.empty()) w.release(held);
    }
    last_ = w.contacts();

    // Back into the params; and asleep again once it is where it is meant
    // to be, still, whole and let go a moment.
    bool settled = held.empty() && self.params.num("strength") > 0.95;
    for (Element& b : elements()) {
        if (b.kind != kBone) continue;
        const rigid::Body* body = w.find(b.id.str());
        set_vec(b, body->x, "x", "y", "z");
        set_turn(b, spatial::orthonormal(body->r), "q");
        set_vec(b, body->v, "vx", "vy", "vz");
        set_vec(b, body->w, "wx", "wy", "wz");
        if (b.params.num("weak", 1.0) < 0.95 || spatial::length(body->x - vec(b, "tx", "ty", "tz")) > 0.03 ||
            spatial::length(spatial::log_map(turn_of(b, "aim") * spatial::transpose(body->r))) > 0.06 || spatial::length(body->w) > 0.6)
            settled = false;
    }
    const double still = settled ? self.params.num("still") + dt : 0.0;
    self.params.set("still", still);
    if (still > 0.4 && !disturbed()) self.params.set("awake", 0.0).set("lead", 0.0);
}

Key ragdoll(StateGraph& g, Being& body, Temporal& clock, double mass) {
    auto& rag = g.add<Ragdoll>(Key{body.id().str() + ".rag"}, body, mass);
    // The being's meaning, carried to the ragdoll's bones as where they are
    // to be.
    Functor means(Key{body.id().str() + ".means"}, body.id(), rag.id());
    for (Key b : rag.bones())
        means.on_object(b, b, [](const Element& j, Element& bone) {
            set_vec(bone, vec(j, "ax", "ay", "az"), "tx", "ty", "tz");
            set_turn(bone, turn_of(j, "aq"), "aim");
        });
    g.set_functor(std::move(means));
    g.keep(Key{body.id().str() + ".means"});
    // Its bones, carried back as what leads the being, as far as it is awake.
    Functor leads(Key{rag.id().str() + ".leads"}, rag.id(), body.id());
    for (Key b : rag.bones())
        leads.on_object(b, b, [](const Element& bone, Element& j) {
            set_turn(j, turn_of(bone, "q"), "lead_q");
            j.params.set("lead_at", 1.0);
            const bool root = bone.params.get_or<std::string>("parent", "").empty();
            if (root) j.params.set("lead_tx", bone.params.num("x")).set("lead_ty", bone.params.num("y")).set("lead_tz", bone.params.num("z"));
            j.params.set("lead_moves", root ? 1.0 : 0.0);
        });
    leads.on_object(Ragdoll::self_id(), Being::self_id(), [](const Element& self, Element& being) {
        being.params.set("lead_to", self.params.num("lead")).set("lead_fade", 0.15);
    });
    g.set_functor(std::move(leads));
    g.keep(Key{rag.id().str() + ".leads"});
    sg::drive(g, clock, rag.id(), rag.step_event(), false, Keeps::Always);
    return rag.id();
}

Key collide_with(StateGraph& g, Ragdoll& rag, Key host, Key anchor) {
    State& h = g.state(host);
    const Key name{rag.id().str() + ".solids"};
    Functor f(name, host, rag.id());
    const auto carry = [](const Element& s, Element& c) {
        for (Key k : {keys::x, keys::y, keys::z, keys::sx, keys::sy, keys::sz, keys::yaw, keys::pitch, keys::roll}) c.params.set(k, s.params.num(k));
        c.alive = s.alive;
    };
    for (const Element& e : h.elements()) {
        const bool solid = e.kind == kinds::wall || (e.kind == kinds::mesh && e.params.num("solid") > 0.5);
        if (!solid || !e.params.get_or<std::string>(keys::parent.str(), "").empty()) continue;
        const Key mine{"solid." + e.id.str()};
        if (!rag.find(mine)) rag.add_element(mine, kCollider);
        f.on_object(e.id, mine, carry);
    }
    f.on_object(anchor, Ragdoll::self_id(), [](const Element& a, Element& self) {
        self.params.set("ox", a.params.num(keys::x)).set("oy", a.params.num(keys::y)).set("oz", a.params.num(keys::z)).set("oyaw", a.params.num(keys::yaw));
    });
    g.set_functor(std::move(f));
    g.keep(name);
    g.functor(name)->apply(h, rag);
    return name;
}

}  // namespace sg
