#include "sg/domains/Being.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <sstream>
#include <string_view>

#include "sg/core/StateGraph.hpp"
#include "sg/spatial/Math.hpp"

namespace sg {

namespace {

constexpr double kDeg = 3.14159265358979323846 / 180.0;

// --- a turn as a unit quaternion -------------------------------------------------
struct Q {
    double w = 1, x = 0, y = 0, z = 0;
};
Q mul(const Q& a, const Q& b) {
    return {a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z, a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}
Q conj(const Q& a) { return {a.w, -a.x, -a.y, -a.z}; }
double dot(const Q& a, const Q& b) { return a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z; }
Q norm(Q a) {
    const double n = std::sqrt(dot(a, a));
    if (n < 1e-12) return {};
    return {a.w / n, a.x / n, a.y / n, a.z / n};
}
Q scaled(const Q& a, double k) { return {a.w * k, a.x * k, a.y * k, a.z * k}; }
Q plus(const Q& a, const Q& b) { return {a.w + b.w, a.x + b.x, a.y + b.y, a.z + b.z}; }
Q slerp(const Q& a, Q b, double t) {
    if (t <= 0) return a;
    if (t >= 1) return b;
    double c = dot(a, b);
    if (c < 0) b = scaled(b, -1), c = -c;
    if (c > 0.9995) return norm(plus(scaled(a, 1 - t), scaled(b, t)));
    const double th = std::acos(std::min(1.0, c)), s = std::sin(th);
    return plus(scaled(a, std::sin((1 - t) * th) / s), scaled(b, std::sin(t * th) / s));
}
Vec3d rotate(const Q& q, const Vec3d& v) {
    const Q p{0, v.x, v.y, v.z};
    const Q r = mul(mul(q, p), conj(q));
    return {r.x, r.y, r.z};
}
Q axis_angle(Vec3d axis, double a) {
    const double l = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    if (l < 1e-12) return {};
    const double s = std::sin(a * 0.5) / l;
    return {std::cos(a * 0.5), axis.x * s, axis.y * s, axis.z * s};
}
// The engine's one turn (yaw, then pitch, then roll - spatial::from_euler, so
// there is one convention, not two), and back.
Q from_euler(double yaw, double pitch, double roll) {
    Q q;
    spatial::to_quat(spatial::from_euler(yaw, pitch, roll), q.w, q.x, q.y, q.z);
    return q;
}
void to_euler(const Q& q, double& yaw, double& pitch, double& roll) {
    spatial::to_euler(spatial::from_quat(q.w, q.x, q.y, q.z), yaw, pitch, roll);
}
// A turn as the axis it turns about, as long as the angle (radians), and back.
Vec3d log_of(Q q) {
    if (q.w < 0) q = scaled(q, -1);
    const double s = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
    if (s < 1e-12) return {2 * q.x, 2 * q.y, 2 * q.z};
    const double a = 2 * std::atan2(s, q.w) / s;
    return {q.x * a, q.y * a, q.z * a};
}
Q exp_of(const Vec3d& v) { return axis_angle(v, std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z)); }
// A critically damped spring let go of `x` (going at `v`), `dt` on, its
// half-life `half`: exactly, as it would go in any number of steps.
void let_go(Vec3d& x, Vec3d& v, double half, double dt) {
    const double y = 0.6931471805599453 * 2.0 / std::max(1e-4, half);
    const Vec3d j = v + x * y;
    const double e = std::exp(-y * dt);
    x = (x + j * dt) * e;
    v = (v - j * (y * dt)) * e;
}

// The smallest turn taking direction `a` to direction `b`.
Q between(Vec3d a, Vec3d b) {
    const double la = length(a), lb = length(b);
    if (la < 1e-9 || lb < 1e-9) return {};
    a = a * (1 / la), b = b * (1 / lb);
    const double c = dot(a, b);
    const Vec3d k = cross(a, b);
    if (c < -0.99999) return axis_angle(std::fabs(a.x) < 0.9 ? Vec3d{0, -a.z, a.y} : Vec3d{-a.y, a.x, 0}, 3.14159265358979323846);
    return norm({1 + c, k.x, k.y, k.z});
}

// The four names a turn is kept under (`<p>w <p>x <p>y <p>z`), named once
// for each turn a being keeps: a step reads and writes them for every joint.
struct Turn4 {
    const char* p;
    Key w, x, y, z;
};
Turn4 turn4(const char* p) {
    const std::string s = p;
    return {p, Key{s + "w"}, Key{s + "x"}, Key{s + "y"}, Key{s + "z"}};
}
const Turn4& names_of(const char* p) {
    static const Turn4 known[] = {turn4("q"), turn4("rest_q"), turn4("h"), turn4("lead_q"), turn4("follow_q"), turn4("bq"), turn4("aq"), turn4("pq")};
    for (const Turn4& t : known)
        if (std::strcmp(t.p, p) == 0) return t;
    thread_local Turn4 other;
    other = turn4(p);
    return other;
}
Q q_of(const Element& e, const char* p = "q") {
    const Turn4& k = names_of(p);
    return {e.params.num(k.w, 1.0), e.params.num(k.x), e.params.num(k.y), e.params.num(k.z)};
}
void set_q(Element& e, const Q& q, const char* p = "q") {
    const Turn4& k = names_of(p);
    e.params.set(k.w, q.w).set(k.x, q.x).set(k.y, q.y).set(k.z, q.z);
}
Q rest_of(const Element& j) {
    // At rest: its own turn, as a model gave it (`rest_q*`), or as degrees said.
    static const Key rest_qw{"rest_qw"};
    if (j.params.has(rest_qw)) return norm(q_of(j, "rest_q"));
    return from_euler(j.params.num("rest_yaw") , j.params.num("rest_pitch"), j.params.num("rest_roll"));
}
// Where it stands on its parent: as a clip moves it now (`t*`), else as made.
Vec3d offset_of(const Element& e) {
    if (e.params.has(Key{"tx"})) return {e.params.num("tx"), e.params.num("ty"), e.params.num("tz")};
    return {e.params.num(keys::x), e.params.num(keys::y), e.params.num(keys::z)};
}
double ease(double dt, double rate) { return rate <= 0 ? 1.0 : 1.0 - std::exp(-dt * rate); }

// A foot set down (a goal that `plants`), one step on: `at` where its clips
// put it now and `going` how fast they move it, in the being's frame; the
// being's travel (`go_*`) and how far its body stands raised or lowered to
// its lowest foot's ground (`ground`) in `self`. Where the leg is to bring it
// is said as the goal's point. `may_step`: no other foot of it is off the
// ground - a foot left behind, or going faster than the ground, steps only
// while the others stand (its clip lifting it, it goes whenever).
bool plant_step(Element& g, const Params& self, Vec3d at, const Vec3d& going, double dt, bool may_step) {
    Params& p = g.params;
    const double vx = self.num("go_x"), vz = self.num("go_z"), turn = self.num("go_turn"), ground = self.num("ground");
    // (Its clip's height, as if its body stood on its own floor.)
    at.y -= ground;
    const double still = p.num("still", 0.27), slip = p.num("slip", 0.14), lift = p.num("lift", 0.06), leg = p.num("leg", 0.9);
    // How fast it goes over the ground as its clips have it: as they move it,
    // and as the body carries it along (its travel - not its turn, which no
    // clip turns it by: a body turning where it stands turns on its feet).
    // (A walk's foot on the ground goes back at no one speed through its
    // step: the faster it travels, the more its clip's foot is let differ.)
    const double over = std::hypot(going.x + vx, going.z + vz) - 0.4 * std::hypot(vx, vz);
    // How low the clips set it: the lowest it has been, forgotten slowly (a
    // step's height in a second or two) - and soon where it stays still: a
    // foot that does not go over the ground is on it (as a body stands up
    // from how it was made, or stands somewhere new).
    double low = p.num("low", 1e9);
    const bool first = low > 1e8;
    low = std::min(low, at.y);
    if (over < still) low += (at.y - low) * ease(dt, 8.0);
    else low = std::min(at.y, low + 0.02 * leg * dt);
    p.set("low", low);
    double lx = p.num("lock_x"), lz = p.num("lock_z"), yaw = p.num("lock_yaw");
    if (first) lx = at.x, lz = at.z;
    bool down = p.num("locked") > 0.5;
    const bool was_down = down;
    if (down) {
        // Where it was set down, as the body goes on over it: turned back by
        // the body's turn, and carried back by its travel - and the way it
        // points kept as it was on the ground (`lock_yaw`, from its clip's).
        const double a = turn * dt, c = std::cos(a), sn = std::sin(a);
        const double x = lx * c + lz * sn, z = -lx * sn + lz * c;
        lx = x - vx * dt, lz = z - vz * dt;
        yaw -= a;
    }
    const double off = std::hypot(at.x - lx, at.z - lz), high = at.y - low;
    // Let go of as its clip lifts it; as it goes over the ground faster than
    // the body's travel lets a foot on it go - only while it travels: a body
    // standing that changes what it does with its feet (a walk come to a
    // stop, standing again) steps to them, not slides; or left behind.
    // (Set down again only once its step has brought it back under its clip.)
    const bool travels = std::hypot(vx, vz) > 0.05;
    // (Standing still - not going, not turning - a foot left off where its
    // clip stands it, or turned, is stepped back under it: a body settles.)
    const bool settles = !travels && std::fabs(turn) < 0.05;
    const double slip_now = settles ? 0.35 * slip : slip, twist_now = settles ? 0.25 : 0.45;
    if (!down && over < still && high < 0.05 * leg && off < 0.25 * slip) down = true, lx = at.x, lz = at.z;
    else if (down && (high > 0.09 * leg || (may_step && ((travels && over > 2.0 * still) || off > slip_now || std::fabs(yaw) > twist_now)))) down = false;
    // Its ground: eased to what is under it (`floor_to`, from its own floor),
    // and the way it faces (`tilt_to_*`) as well.
    const double floor = p.num("floor") + (p.num("floor_to") - p.num("floor")) * ease(dt, 12.0);
    p.set("floor", floor);
    p.set("tilt_x", p.num("tilt_x") + (p.num("tilt_to_x") - p.num("tilt_x")) * ease(dt, 12.0));
    p.set("tilt_z", p.num("tilt_z") + (p.num("tilt_to_z") - p.num("tilt_z")) * ease(dt, 12.0));
    double y = at.y + floor;
    if (!down) {
        // Let go: brought back to where its clips have it in a short step,
        // lifted as far as it has to go.
        // (Carried as its clip carries it, and only how far it is off that let go of.)
        const double k = 1.0 - ease(dt, 16.0);
        y += std::min(lift, 0.5 * off + 0.1 * leg * std::fabs(yaw));
        lx = at.x + (lx - (at.x - going.x * dt)) * k, lz = at.z + (lz - (at.z - going.z * dt)) * k;
        yaw *= k;
    }
    if (down && !was_down) yaw = 0.0;  // (set down pointing as its clip has it)
    p.set("locked", down ? 1.0 : 0.0).set("lock_x", lx).set("lock_z", lz).set("lock_yaw", yaw);
    p.set(keys::x, lx).set(keys::y, y).set(keys::z, lz);
    // (Set down again: a footfall - not as it first stands, nor a foot
    // that never left the ground.)
    return down && !was_down && !first;
}

const Key kJoint{"joint"}, kPart{"part"}, kClip{"clip"}, kLayer{"layer"}, kGoal{"goal"}, kBlend{"blend"};
const Key kParentJoint{"parent_joint"}, kTip{"tip"};

// The text under `k`, or none (as `get_or` would give "" for it), read where
// it is.
std::string_view text_or_none(const Params& p, Key k) {
    const std::string* t = p.text(k);
    return t ? std::string_view(*t) : std::string_view{};
}

// A layer's name (`layer<i>`), named once.
Key layer_key(int i) {
    static const std::vector<Key> names = [] {
        std::vector<Key> n;
        for (int k = 0; k < Being::kLayers; ++k) n.push_back(Key{"layer" + std::to_string(k)});
        return n;
    }();
    return i >= 0 && i < int(names.size()) ? names[std::size_t(i)] : Key{"layer" + std::to_string(i)};
}

// Every joint in order, parents first, and where each is in the being's frame
// for a set of local turns.
struct Frame {
    Vec3d p;
    Q r;
};

// A blend's clips, each at its point (`clip x [y]` a line), and how much of
// each is played at (x, y): gradient bands - each clip's weight the least,
// over every other, of how far the point has still to go from it towards
// that other (Johansen's cartesian bands). Along a line it is linear between
// the two neighbours; on a plane it is smooth, and the clip on the point is
// all there is.
std::vector<std::pair<std::string, double>> blend_weights(const std::string& text, double x, double y) {
    struct P {
        std::string clip;
        double x, y;
    };
    std::vector<P> ps;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        std::istringstream ls(line.substr(0, line.find('#')));
        P p{{}, 0, 0};
        if (ls >> p.clip >> p.x) {
            ls >> p.y;
            ps.push_back(p);
        }
    }
    std::vector<std::pair<std::string, double>> out;
    double total = 0;
    for (std::size_t i = 0; i < ps.size(); ++i) {
        double w = 1;
        for (std::size_t j = 0; j < ps.size(); ++j) {
            if (j == i) continue;
            const double ax = ps[j].x - ps[i].x, ay = ps[j].y - ps[i].y, l2 = ax * ax + ay * ay;
            if (l2 < 1e-12) continue;
            w = std::min(w, 1.0 - ((x - ps[i].x) * ax + (y - ps[i].y) * ay) / l2);
        }
        w = std::max(0.0, w);
        out.emplace_back(ps[i].clip, w);
        total += w;
    }
    for (auto& [c, w] : out) w = total > 1e-12 ? w / total : 1.0 / double(out.size());
    return out;
}
}  // namespace

Being::Being(Key id, double scale) : State(std::move(id)) {
    add_element(self_id(), Key{"being"})
        .params.set("scale", scale)
        .set("tempo_exp", 1.0)
        .set("lead", 0.0)
        .set("lead_to", 0.0)
        .set("lead_fade", 0.3)
        .set("follow", 0.0)
        .set("follow_to", 0.0)
        .set("follow_fade", 0.3)
        .set("go_x", 0.0)
        .set("go_z", 0.0)
        .set("go_turn", 0.0)
        .set("ground", 0.0)
        .set("footfalls", 0.0)
        .set("footfall_on", std::string{})
        .set("slope_ahead", 0.0)
        .set("slope_side", 0.0)
        .set("slope_ahead_to", 0.0)
        .set("slope_side_to", 0.0)
        .set("hip_lean", 0.35)
        .set("balance", 1.0)
        .set("sway_x", 0.0)
        .set("sway_z", 0.0)
        .set("load", 0.0)
        .set("load_x", 0.0)
        .set("load_z", 0.0)
        .set("steady_x", 0.0)
        .set("steady_z", 0.0)
        .set("stumbles", 0.0)
        .set("inert", 0.0)
        .set("inert_from", 0.0)
        .set("inert_half", 0.1)
        .set("lived", 0.0);
    for (int i = 0; i < kLayers; ++i)
        add_element(layer_key(i), kLayer)
            .params.set("clip", std::string{})
            .set("phase", 0.0)
            .set("weight", 0.0)
            .set("to", 0.0)
            .set("fade", 0.3)
            .set("speed", 1.0)
            .set("loop", 1.0)
            .set("mask", std::string{})
            .set("additive", std::string{});
    //   self --live--> self: the spirit's step, on its line of time.
    loop(Key{"live"}, self_id(), live_event(), [](State& s, Element&, Element*, const Event& ev) { static_cast<Being&>(s).live(ev.args.num(keys::dt)); });
    //   self --play--> self: a clip, faded in (and the others out, unless not alone).
    loop(Key{"play"}, self_id(), play_event(), [](State& s, Element&, Element*, const Event& ev) {
        const std::string clip = ev.args.get_or<std::string>("clip", "");
        if (!s.find(Key{"clip." + clip}) && !s.find(Key{"blend." + clip})) return;
        const double fade = ev.args.num("fade", 0.3), weight = ev.args.num("weight", 1.0), inert = ev.args.num("inert", 0.0);
        const bool alone = ev.args.num("alone", 1.0) > 0.5;
        Element* slot = nullptr;
        for (int i = 0; i < kLayers; ++i) {
            Element& l = s.element(layer_key(i));
            if (l.params.get_or<std::string>("clip", "") == clip) slot = &l;
        }
        // Inertialized: only if what it plays changes (a clip it plays
        // already, alone, played again is no change of motion).
        bool changes = !slot || std::fabs(slot->params.num("weight") - weight) > 1e-3;
        if (alone)
            for (int i = 0; i < kLayers; ++i) {
                const Element& l = s.element(layer_key(i));
                if (&l != slot && !l.params.get_or<std::string>("clip", "").empty() && l.params.num("weight") > 1e-3) changes = true;
            }
        if (!slot) {
            double least = 1e9;
            for (int i = 0; i < kLayers; ++i) {
                Element& l = s.element(layer_key(i));
                const double w = l.params.get_or<std::string>("clip", "").empty() ? -1.0 : l.params.num("weight");
                if (w < least) least = w, slot = &l;
            }
            slot->params.set("clip", clip).set("phase", 0.0).set("weight", 0.0);
        }
        slot->params.set("to", weight).set("fade", fade).set("speed", ev.args.num("speed", 1.0)).set("loop", ev.args.num("loop", 1.0));
        slot->params.set("mask", ev.args.get_or<std::string>("mask", "")).set("additive", ev.args.get_or<std::string>("additive", ""));
        if (alone)
            for (int i = 0; i < kLayers; ++i) {
                Element& l = s.element(layer_key(i));
                if (&l != slot) l.params.set("to", 0.0).set("fade", fade);
            }
        if (inert > 0 && changes) {
            // At once: the clip wholly, the others gone; the step after
            // takes the difference up as each joint's offset, let go of.
            slot->params.set("weight", weight);
            if (alone)
                for (int i = 0; i < kLayers; ++i) {
                    Element& l = s.element(layer_key(i));
                    if (&l != slot) l.params.set("clip", std::string{}).set("weight", 0.0).set("to", 0.0);
                }
            s.element(self_id()).params.set("inert_from", 1.0).set("inert_half", inert);
        }
    });
    loop(Key{"stop"}, self_id(), stop_event(), [](State& s, Element&, Element*, const Event& ev) {
        const std::string clip = ev.args.get_or<std::string>("clip", "");
        for (int i = 0; i < kLayers; ++i) {
            Element& l = s.element(layer_key(i));
            if (clip.empty() || l.params.get_or<std::string>("clip", "") == clip) l.params.set("to", 0.0).set("fade", ev.args.num("fade", 0.3));
        }
    });
    //   self --reach--> self, self --look--> self: a goal's point, and how
    //   its chain is to meet it - its tip brought there, or turned to it.
    const auto aim_goal = [](State& s, const Event& ev, double look) {
        Element* g = s.find(Key{"goal." + ev.args.get_or<std::string>("goal", "")});
        if (!g) return;
        g->params.set(keys::x, ev.args.num(keys::x)).set(keys::y, ev.args.num(keys::y)).set(keys::z, ev.args.num(keys::z));
        g->params.set("to", 1.0).set("fade", ev.args.num("fade", 0.25)).set("aim", look);
        g->params.set("soften", std::clamp(ev.args.num("soften", 0.0), 0.0, 1.0)).set("twist", ev.args.num("twist", 0.0));
        const bool pole = ev.args.has(Key{"pole_x"}) || ev.args.has(Key{"pole_y"}) || ev.args.has(Key{"pole_z"});
        g->params.set("pole", pole ? 1.0 : 0.0);
        if (pole) g->params.set("pole_x", ev.args.num("pole_x")).set("pole_y", ev.args.num("pole_y")).set("pole_z", ev.args.num("pole_z"));
    };
    loop(Key{"reach"}, self_id(), reach_event(), [aim_goal](State& s, Element&, Element*, const Event& ev) { aim_goal(s, ev, 0.0); });
    loop(Key{"look"}, self_id(), look_event(), [aim_goal](State& s, Element&, Element*, const Event& ev) { aim_goal(s, ev, 1.0); });
    loop(Key{"release"}, self_id(), release_event(), [](State& s, Element&, Element*, const Event& ev) {
        if (Element* g = s.find(Key{"goal." + ev.args.get_or<std::string>("goal", "")})) g->params.set("to", 0.0).set("fade", ev.args.num("fade", 0.25));
    });
    loop(Key{"turn"}, self_id(), turn_event(), [](State& s, Element&, Element*, const Event& ev) {
        Element* j = s.find(Key{ev.args.get_or<std::string>("joint", "")});
        if (!j || j->kind != kJoint) return;
        set_q(*j, from_euler(ev.args.num("yaw") * kDeg, ev.args.num("pitch") * kDeg, ev.args.num("roll") * kDeg), "h");
        j->params.set("hold_to", ev.args.num("weight", 1.0));
    });
    loop(Key{"free"}, self_id(), free_event(), [](State& s, Element&, Element*, const Event& ev) {
        const std::string name = ev.args.get_or<std::string>("joint", "");
        for (Element& e : s.elements())
            if (e.kind == kJoint && (name.empty() || e.id.str() == name)) e.params.set("hold_to", 0.0);
    });
    //   self --steer--> self: where in a blend it is to be (eased there).
    loop(Key{"steer"}, self_id(), steer_event(), [](State& s, Element&, Element*, const Event& ev) {
        Element* b = s.find(Key{"blend." + ev.args.get_or<std::string>("blend", "")});
        if (!b) return;
        if (ev.args.has(keys::x)) b->params.set("to_x", ev.args.num(keys::x));
        if (ev.args.has(keys::y)) b->params.set("to_y", ev.args.num(keys::y));
    });
    //   self --lead--> self: how much it gives itself to what leads it.
    loop(Key{"lead"}, self_id(), lead_event(), [](State&, Element& self, Element*, const Event& ev) {
        self.params.set("lead_to", std::clamp(ev.args.num("weight", 1.0), 0.0, 1.0)).set("lead_fade", ev.args.num("fade", 0.3));
    });
    //   self --follow--> self: how much it takes another's motion for its own.
    loop(Key{"follow"}, self_id(), follow_event(), [](State&, Element& self, Element*, const Event& ev) {
        self.params.set("follow_to", std::clamp(ev.args.num("weight", 1.0), 0.0, 1.0)).set("follow_fade", ev.args.num("fade", 0.3));
    });
    //   self --travel--> self: how it goes across its ground, in its own frame
    //   (and the ground under each foot, if it is not its own floor).
    loop(Key{"travel"}, self_id(), travel_event(), [](State& s, Element& self, Element*, const Event& ev) {
        self.params.set("go_x", ev.args.num(keys::x)).set("go_z", ev.args.num(keys::z)).set("go_turn", ev.args.num("turn"));
        if (ev.args.has(Key{"slope_ahead"})) self.params.set("slope_ahead_to", ev.args.num("slope_ahead"));
        if (ev.args.has(Key{"slope_side"})) self.params.set("slope_side_to", ev.args.num("slope_side"));
        for (Element& g : s.elements())
            if (g.kind == kGoal && g.params.num("plants") > 0.5) {
                const std::string tip = g.params.get_or<std::string>("tip", "");
                if (const Key f{"floor_" + tip}; ev.args.has(f)) g.params.set("floor_to", ev.args.num(f));
                if (const Key f{"tilt_" + tip + "_x"}; ev.args.has(f))
                    g.params.set("tilt_to_x", ev.args.num(f)).set("tilt_to_z", ev.args.num(Key{"tilt_" + tip + "_z"}));
                if (const Key f{"on_" + tip}; ev.args.has(f)) g.params.set("on", ev.args.get_or<std::string>(f, ""));
                if (const Key f{"asked_" + tip + "_x"}; ev.args.has(f))
                    g.params.set("asked", 1.0).set("asked_x", ev.args.num(f)).set("asked_z", ev.args.num(Key{"asked_" + tip + "_z"}));
            }
    });
    //   self --balance--> self: what bears on its balance (its load, how it is steadied).
    loop(Key{"balance"}, self_id(), balance_event(), [](State&, Element& self, Element*, const Event& ev) {
        for (const char* k : {"load", "load_x", "load_z", "steady_x", "steady_z"})
            if (ev.args.has(Key{k})) self.params.set(k, ev.args.num(Key{k}));
    });
    loop(Key{"scale"}, self_id(), scale_event(), [](State&, Element& self, Element*, const Event& ev) {
        self.params.set("scale", std::max(1e-3, ev.args.num("scale", 1.0)));
    });
}

Element& Being::joint(const std::string& name, const std::string& parent, const Vec3d& offset, const Vec3d& rest_deg, double stiffness) {
    Element& j = add_element(Key{name}, kJoint);
    j.params.set("parent_joint", parent).set(keys::x, offset.x).set(keys::y, offset.y).set(keys::z, offset.z);
    j.params.set("rest_yaw", rest_deg.x * kDeg).set("rest_pitch", rest_deg.y * kDeg).set("rest_roll", rest_deg.z * kDeg);
    j.params.set("stiffness", stiffness).set("hold", 0.0).set("hold_to", 0.0).set("lead_at", 0.0).set("lead_moves", 0.0);
    j.params.set("follow_at", 0.0).set("follow_moves", 0.0);
    set_q(j, rest_of(j));
    set_q(j, Q{}, "h");
    resolve();
    return j;
}

Element& Being::part(const std::string& name, const std::string& joint_name, const std::string& shape, const Vec3d& size, const Vec3d& offset,
                     const Vec3d& rgb) {
    Element& p = add_element(Key{name}, kPart);
    p.params.set("joint", joint_name).set("shape", shape).set(keys::sx, size.x).set(keys::sy, size.y).set(keys::sz, size.z);
    p.params.set("ox", offset.x).set("oy", offset.y).set("oz", offset.z).set(keys::r, rgb.x).set(keys::g, rgb.y).set(keys::b, rgb.z);
    resolve();
    return p;
}

Element& Being::clip(const std::string& name, const std::string& keys_text, bool loop_it) {
    Element* c = find(Key{"clip." + name});
    if (!c) c = &add_element(Key{"clip." + name}, kClip);
    c->params.set("keys", keys_text).set("loop", loop_it ? 1.0 : 0.0);
    return *c;
}

Element& Being::blend(const std::string& name, const std::string& points, double x, double y, double rate) {
    Element* b = find(Key{"blend." + name});
    if (!b) b = &add_element(Key{"blend." + name}, kBlend);
    b->params.set("clips", points).set(keys::x, x).set(keys::y, y).set("to_x", x).set("to_y", y).set("rate", rate);
    return *b;
}

Element& Being::goal(const std::string& name, const std::string& tip, int links) {
    Element& g = add_element(Key{"goal." + name}, kGoal);
    g.params.set("tip", tip).set("links", double(links)).set("weight", 0.0).set("to", 0.0).set("fade", 0.25);
    g.params.set(keys::x, 0.0).set(keys::y, 0.0).set(keys::z, 0.0);
    g.params.set("aim", 0.0).set("pole", 0.0).set("pole_x", 0.0).set("pole_y", 0.0).set("pole_z", 0.0).set("soften", 0.0).set("twist", 0.0);
    g.params.set("fwd_x", 1.0).set("fwd_y", 0.0).set("fwd_z", 0.0).set("up_x", 0.0).set("up_y", 1.0).set("up_z", 0.0).set("shares", std::string{});
    return g;
}

Element& Being::plant(const std::string& ankle) {
    const std::string name = "plant." + ankle;
    Element* had = find(Key{"goal." + name});
    Element& g = had ? *had : goal(name, ankle, 2);
    // How long its leg is, as it is made: what it lets slip, and how high it steps, are by it.
    double leg = 0;
    const Element* j = find(Key{ankle});
    for (int n = 0; j && n < 2; ++n) {
        const Element* up = find(Key{j->params.get_or<std::string>("parent_joint", "")});
        if (!up) break;
        const Vec3d a{j->params.num("px"), j->params.num("py"), j->params.num("pz")}, b{up->params.num("px"), up->params.num("py"), up->params.num("pz")};
        leg += length(a - b);
        j = up;
    }
    if (leg < 1e-6) leg = 0.9;
    g.params.set("plants", 1.0).set("to", 1.0).set("weight", 1.0).set("fade", 0.1).set("soften", 0.97);
    g.params.set("low", 1e9).set("floor", 0.0).set("floor_to", 0.0).set("lock_x", 0.0).set("lock_z", 0.0).set("lock_yaw", 0.0).set("locked", 0.0);
    const Vec3d ahead = facing();
    g.params.set("ahead_x", ahead.x).set("ahead_z", ahead.z);
    g.params.set("still", 0.3 * leg).set("slip", 0.16 * leg).set("lift", 0.07 * leg).set("leg", leg);
    return g;
}

const Being::Parsed& Being::parsed(const Element& c) const {
    Parsed& p = clips_[c.id];
    if (p.stamp == c.params.stamp()) return p;
    p = Parsed{};
    p.stamp = c.params.stamp();
    std::istringstream in(c.params.get_or<std::string>("keys", ""));
    for (std::string line; std::getline(in, line);) {
        std::istringstream ls(line.substr(0, line.find('#')));
        double t, y = 0, pi = 0, r = 0;
        std::string j, word;
        if (!(ls >> t >> j >> word)) continue;
        Q q;
        if (word == "pc" || word == "qc") {
            // `t joint pc x y z  ix iy iz  ox oy oz`, `t joint qc w x y z  iw ix iy iz  ow ox oy oz`:
            // a cubic key - its value, and its tangents in and out (glTF's
            // CUBICSPLINE, kept as it was made).
            const int n = word == "pc" ? 3 : 4;
            Key3 k{t, {0, 0, 0, 0}};
            k.cubic = true;
            for (int i = 0; i < n; ++i) ls >> k.q[i];
            for (int i = 0; i < n; ++i) ls >> k.in[i];
            for (int i = 0; i < n; ++i) ls >> k.out[i];
            if (n == 3) p.tracks[Key{j}].moves.push_back(k);
            else p.tracks[Key{j}].keys.push_back(k);
            p.length = std::max(p.length, t);
            continue;
        }
        if (word == "p") {
            // `t joint p x y z`: where it stands on its parent then (a hip's travel).
            double x = 0, yy = 0, z = 0;
            ls >> x >> yy >> z;
            p.tracks[Key{j}].moves.push_back({t, {x, yy, z, 0}});
            p.length = std::max(p.length, t);
            continue;
        }
        if (word == "q") {
            // `t joint q w x y z`: the turn itself (as BVH and glTF are read in).
            ls >> q.w >> q.x >> q.y >> q.z;
            q = norm(q);
        } else {
            y = std::atof(word.c_str());
            ls >> pi >> r;
            q = from_euler(y * kDeg, pi * kDeg, r * kDeg);
        }
        p.tracks[Key{j}].keys.push_back({t, {q.w, q.x, q.y, q.z}});
        p.length = std::max(p.length, t);
    }
    for (auto& [k, tr] : p.tracks) {
        std::stable_sort(tr.keys.begin(), tr.keys.end(), [](const Key3& a, const Key3& b) { return a.t < b.t; });
        std::stable_sort(tr.moves.begin(), tr.moves.end(), [](const Key3& a, const Key3& b) { return a.t < b.t; });
    }
    return p;
}

double Being::tempo() const {
    const Element& s = element(self_id());
    return std::pow(std::max(1e-3, s.params.num("scale", 1.0)), -s.params.num("tempo_exp", 1.0));
}

// The last key at or before `ph` (the first, before them all): found by
// halving, as keys are in order of time.
namespace {
template <class Keys>
std::size_t key_at(const Keys& ks, double ph) {
    auto it = std::upper_bound(ks.begin(), ks.end(), ph, [](double v, const auto& k) { return v < k.t; });
    return it == ks.begin() ? 0 : std::size_t(it - ks.begin()) - 1;
}
// Hermite's cubic between two keys `span` apart, `s` of the way (0..1):
// the values and the tangents, each `n` numbers (glTF's CUBICSPLINE).
void hermite(const double* v0, const double* out0, const double* v1, const double* in1, double span, double s, int n, double* r) {
    const double s2 = s * s, s3 = s2 * s;
    const double h00 = 2 * s3 - 3 * s2 + 1, h10 = s3 - 2 * s2 + s, h01 = -2 * s3 + 3 * s2, h11 = s3 - s2;
    for (int i = 0; i < n; ++i) r[i] = h00 * v0[i] + h10 * span * out0[i] + h01 * v1[i] + h11 * span * in1[i];
}
}  // namespace

void Being::turn_at(const Track& tr, double ph, bool loop, double length, double out[4]) {
    const auto& ks = tr.keys;
    const std::size_t k = key_at(ks, ph);
    const Q a{ks[k].q[0], ks[k].q[1], ks[k].q[2], ks[k].q[3]};
    Q q = a;
    if (k + 1 < ks.size()) {
        const Key3& n = ks[k + 1];
        const Q b{n.q[0], n.q[1], n.q[2], n.q[3]};
        const double span = n.t - ks[k].t;
        const double s = span > 0 ? std::clamp((ph - ks[k].t) / span, 0.0, 1.0) : 0.0;
        if (ks[k].cubic && n.cubic) {
            double r[4];
            hermite(ks[k].q, ks[k].out, n.q, n.in, span, s, 4, r);
            q = norm({r[0], r[1], r[2], r[3]});
        } else {
            q = slerp(a, b, s);
        }
    } else if (loop && ks.size() > 1 && length > ks[k].t) {
        // Round the loop, back to its first key.
        const Q b{ks[0].q[0], ks[0].q[1], ks[0].q[2], ks[0].q[3]};
        q = slerp(a, b, (ph - ks[k].t) / (length - ks[k].t + ks[0].t + 1e-9));
    }
    out[0] = q.w, out[1] = q.x, out[2] = q.y, out[3] = q.z;
}

void Being::move_at(const Track& tr, double ph, double out[3]) {
    const auto& ms = tr.moves;
    const std::size_t k = key_at(ms, ph);
    for (int i = 0; i < 3; ++i) out[i] = ms[k].q[i];
    if (k + 1 < ms.size() && ms[k + 1].t > ms[k].t) {
        const Key3& n = ms[k + 1];
        const double span = n.t - ms[k].t, s = std::clamp((ph - ms[k].t) / span, 0.0, 1.0);
        if (ms[k].cubic && n.cubic) hermite(ms[k].q, ms[k].out, n.q, n.in, span, s, 3, out);
        else
            for (int i = 0; i < 3; ++i) out[i] = ms[k].q[i] + (n.q[i] - ms[k].q[i]) * s;
    }
}

bool Being::sample(const std::string& clip, Key joint, double t, double out[4]) const {
    const Element* c = find(Key{"clip." + clip});
    if (!c) return false;
    const Parsed& p = parsed(*c);
    auto tr = p.tracks.find(joint);
    if (tr == p.tracks.end() || tr->second.keys.empty()) return false;
    turn_at(tr->second, t, c->params.num("loop", 1.0) > 0.5, p.length, out);
    return true;
}

double Being::ground_speed(const std::string& clip) const {
    const Element* c = find(Key{"clip." + clip});
    if (!c) return 0;
    const Parsed& p = parsed(*c);
    const std::vector<Key> feet = ankles();
    if (p.length <= 0 || feet.empty()) return 0;
    // The joints in order, parents first, as the clip alone poses them.
    std::vector<const Element*> js;
    for (const Element& e : elements())
        if (e.kind == kJoint) js.push_back(&e);
    std::unordered_map<std::string_view, std::size_t> at;
    for (std::size_t i = 0; i < js.size(); ++i) at[js[i]->id.str()] = i;
    std::vector<int> parent(js.size(), -1);
    for (std::size_t i = 0; i < js.size(); ++i)
        if (auto it = at.find(text_or_none(js[i]->params, kParentJoint)); it != at.end() && it->second < i) parent[i] = int(it->second);
    const bool loops = c->params.num("loop", 1.0) > 0.5;
    const auto pose_at = [&](double ph, std::vector<Vec3d>& out) {
        std::vector<Frame> f(js.size());
        for (std::size_t i = 0; i < js.size(); ++i) {
            const Element& j = *js[i];
            Q q = rest_of(j);
            Vec3d off{j.params.num(keys::x), j.params.num(keys::y), j.params.num(keys::z)};
            if (auto tr = p.tracks.find(j.id); tr != p.tracks.end()) {
                double a[4], v[3];
                if (!tr->second.keys.empty()) turn_at(tr->second, ph, loops, p.length, a), q = norm(Q{a[0], a[1], a[2], a[3]});
                if (!tr->second.moves.empty()) move_at(tr->second, ph, v), off = {v[0], v[1], v[2]};
            }
            f[i] = parent[i] < 0 ? Frame{off, q} : Frame{f[std::size_t(parent[i])].p + rotate(f[std::size_t(parent[i])].r, off), mul(f[std::size_t(parent[i])].r, q)};
        }
        out.clear();
        for (Key k : feet) out.push_back(f[at[k.str()]].p);
    };
    const int n = std::max(48, int(p.length * 120));
    const double step = p.length / n;
    std::vector<std::vector<Vec3d>> track(std::size_t(n) + 1);
    for (int k = 0; k <= n; ++k) pose_at(k * step, track[std::size_t(k)]);
    const Vec3d ahead = facing();
    // Each foot while it is at its lowest (within a fiftieth of its leg): how
    // fast it goes back along the facing then, the middle of those speeds.
    std::vector<double> speeds;
    for (std::size_t foot = 0; foot < feet.size(); ++foot) {
        double low = 1e18, high = -1e18;
        for (const auto& t : track) low = std::min(low, t[foot].y), high = std::max(high, t[foot].y);
        const Element* g = find(Key{"goal.plant." + feet[foot].str()});
        const double leg = g ? g->params.num("leg", 0.9) : std::max(0.2, high - low) * 3.0;
        for (int k = 1; k < n; ++k) {
            const Vec3d& here = track[std::size_t(k)][foot];
            if (here.y > low + 0.02 * leg) continue;
            const Vec3d d = track[std::size_t(k + 1)][foot] - track[std::size_t(k - 1)][foot];
            speeds.push_back(-(d.x * ahead.x + d.z * ahead.z) / (2 * step));
        }
    }
    if (speeds.empty()) return 0;
    std::nth_element(speeds.begin(), speeds.begin() + std::ptrdiff_t(speeds.size() / 2), speeds.end());
    return std::max(0.0, speeds[speeds.size() / 2]);
}

void Being::live(double dt) {
    if (dt <= 0) return;  // no time, no change
    Element& self = element(self_id());
    // (Its first step: it stands as it means to at once - no easing out of
    // how it was made, a rest pose it never stands in.)
    const bool first = self.params.num("lived") < 0.5;
    self.params.set("lived", 1.0);
    const double t = dt * tempo();

    // The joints in order, parents first.
    std::vector<Element*> js;
    for (Element& e : elements())
        if (e.kind == kJoint) js.push_back(&e);
    // (By the names as written: a joint's parent is text, found here
    // without being named again.)
    std::unordered_map<std::string_view, std::size_t> at;
    for (std::size_t i = 0; i < js.size(); ++i) at[js[i]->id.str()] = i;
    std::vector<int> parent(js.size(), -1);
    for (std::size_t i = 0; i < js.size(); ++i) {
        auto it = at.find(text_or_none(js[i]->params, kParentJoint));
        if (it != at.end() && it->second < i) parent[i] = int(it->second);
    }

    // A layer's mask, joint by joint: as it names a joint (`spine 1, hips 0`),
    // else as its parent is, a root wholly. None: every joint wholly.
    const auto mask_of = [&](const std::string& text) {
        std::vector<double> m;
        if (text.find_first_not_of(" \t\r\n,") == std::string::npos) return m;
        std::string words = text;
        std::replace(words.begin(), words.end(), ',', ' ');
        std::unordered_map<std::string, double> named;
        std::istringstream in(words);
        std::string name;
        double w;
        while (in >> name >> w) named[name] = std::clamp(w, 0.0, 1.0);
        m.assign(js.size(), 1.0);
        for (std::size_t i = 0; i < js.size(); ++i) {
            auto it = named.find(js[i]->id.str());
            m[i] = it != named.end() ? it->second : parent[i] >= 0 ? m[std::size_t(parent[i])] : 1.0;
        }
        return m;
    };

    // What it intends, clip by clip: each layer fades toward its weight, and
    // plays on. A layer that plays a blend plays its clips together, each by
    // its weight at the blend's point, in step: one phase (0..1) for them
    // all, gone round in the time their lengths, so weighted, take - a walk
    // and a run blended put their feet down together. Each sample is also
    // where it was at the step's start (`back`), so how fast the clips turn
    // the body is read from the clips themselves.
    struct Sample {
        const Parsed* clip;
        double phase, back, weight;
        bool loop;
        int additive;  // 0: blended; 1: added, against rest; 2: added, against its first key
        std::vector<double> mask;
    };
    std::vector<Sample> samples;
    for (int i = 0; i < kLayers; ++i) {
        Element& l = element(layer_key(i));
        const std::string c = l.params.get_or<std::string>("clip", "");
        if (c.empty()) continue;
        const double w = l.params.num("weight"), to = l.params.num("to");
        const double nw = w + (to - w) * ease(t, 1.0 / std::max(1e-3, l.params.num("fade", 0.3)));
        l.params.set("weight", nw);
        const bool loops = l.params.num("loop", 1.0) > 0.5;
        const std::string add = l.params.get_or<std::string>("additive", "");
        const int additive = add == "rest" ? 1 : add == "first" ? 2 : 0;
        const std::vector<double> mask = mask_of(l.params.get_or<std::string>("mask", ""));
        const double before = l.params.num("phase");
        if (const Element* ce = find(Key{"clip." + c})) {
            const double len = parsed(*ce).length;
            double ph = before + t * l.params.num("speed", 1.0);
            // (Played backwards, a speed below nought: round the other way.)
            if (len > 0) ph = loops ? ph - std::floor(ph / len) * len : std::clamp(ph, 0.0, len);
            l.params.set("phase", ph);
            samples.push_back({&parsed(*ce), ph, before, nw, loops, additive, mask});
        } else if (Element* be = find(Key{"blend." + c})) {
            // Where in it: eased toward where it was steered, at its rate.
            const double k = ease(t, be->params.num("rate", 6.0));
            const double bx = be->params.num(keys::x) + (be->params.num("to_x") - be->params.num(keys::x)) * k;
            const double by = be->params.num(keys::y) + (be->params.num("to_y") - be->params.num(keys::y)) * k;
            be->params.set(keys::x, bx).set(keys::y, by);
            std::vector<std::pair<const Parsed*, double>> parts;
            double length = 0;
            for (const auto& [name, bw] : blend_weights(be->params.get_or<std::string>("clips", ""), bx, by)) {
                const Element* ce = find(Key{"clip." + name});
                if (!ce || bw <= 1e-6) continue;
                parts.emplace_back(&parsed(*ce), bw);
                length += bw * parts.back().first->length;
            }
            double ph = before + (length > 1e-6 ? t * l.params.num("speed", 1.0) / length : 0.0);
            ph = loops ? ph - std::floor(ph) : std::clamp(ph, 0.0, 1.0);
            l.params.set("phase", ph);
            for (const auto& [p, bw] : parts) samples.push_back({p, ph * p->length, before * p->length, nw * bw, true, additive, mask});
        }
        if (to <= 0 && nw < 1e-3) l.params.set("clip", std::string{}).set("weight", 0.0);
    }

    // (Switched just now, inertialized: where each joint stood on its parent before.)
    const bool switched = self.params.num("inert_from") > 0.5;
    std::vector<Vec3d> stood;
    if (switched)
        for (Element* j : js) {
            // (A root as it stood on its own floor: the ground it is raised to is no motion of its clips.)
            const bool root = j->params.get_or<std::string>("parent_joint", "").empty();
            stood.push_back(offset_of(*j) - Vec3d{0, root ? self.params.num("ground") : 0.0, 0});
        }

    // Each joint's target: rest, the clips blended over it, what is added to
    // that, what is held - now, and as the clips had it at the step's start.
    std::vector<Q> target(js.size()), back(js.size());
    for (std::size_t i = 0; i < js.size(); ++i) {
        Element& j = *js[i];
        const Q rest = rest_of(j);
        Q sum = scaled(rest, 0.0), sum_b = sum, add{}, add_b{};
        double total = 0;
        // Where it stands on its parent, as the clips that move it say.
        const Vec3d made{j.params.num(keys::x), j.params.num(keys::y), j.params.num(keys::z)};
        Vec3d moved{}, added{};
        double moved_w = 0;
        bool adds_move = false;
        for (const Sample& sm : samples) {
            const double w = sm.weight * (sm.mask.empty() ? 1.0 : sm.mask[i]);
            if (w <= 1e-6) continue;
            const Parsed& p = *sm.clip;
            auto tr = p.tracks.find(j.id);
            if (tr == p.tracks.end()) continue;
            if (const auto& ms = tr->second.moves; !ms.empty()) {
                double v[3];
                move_at(tr->second, sm.phase, v);
                if (sm.additive) {
                    const Vec3d ref = sm.additive == 1 ? made : Vec3d{ms[0].q[0], ms[0].q[1], ms[0].q[2]};
                    added = added + (Vec3d{v[0], v[1], v[2]} - ref) * w, adds_move = true;
                } else {
                    moved = moved + Vec3d{v[0], v[1], v[2]} * w, moved_w += w;
                }
            }
            if (tr->second.keys.empty()) continue;
            double a[4], b[4];
            turn_at(tr->second, sm.phase, sm.loop, p.length, a);
            turn_at(tr->second, sm.back, sm.loop, p.length, b);
            Q q{a[0], a[1], a[2], a[3]}, qb{b[0], b[1], b[2], b[3]};
            if (sm.additive) {
                // Added: its turn away from the pose it is measured against,
                // as far as its weight, after what the others play.
                const auto& k0 = tr->second.keys[0];
                const Q ref = sm.additive == 1 ? rest : norm(Q{k0.q[0], k0.q[1], k0.q[2], k0.q[3]});
                add = mul(add, slerp(Q{}, norm(mul(conj(ref), q)), w));
                add_b = mul(add_b, slerp(Q{}, norm(mul(conj(ref), qb)), w));
                continue;
            }
            if (dot(q, rest) < 0) q = scaled(q, -1);
            if (dot(qb, rest) < 0) qb = scaled(qb, -1);
            sum = plus(sum, scaled(q, w)), sum_b = plus(sum_b, scaled(qb, w));
            total += w;
        }
        target[i] = total > 1e-6 ? norm(plus(sum, scaled(rest, std::max(0.0, 1.0 - total)))) : rest;
        back[i] = total > 1e-6 ? norm(plus(sum_b, scaled(rest, std::max(0.0, 1.0 - total)))) : rest;
        target[i] = norm(mul(target[i], add)), back[i] = norm(mul(back[i], add_b));
        if (moved_w > 1e-6 || adds_move) {
            const Vec3d to = (moved_w > 1e-6 ? moved + made * std::max(0.0, 1.0 - moved_w) : made) + added;
            j.params.set("tx", to.x).set("ty", to.y).set("tz", to.z);
        } else if (j.params.has(Key{"tx"})) {
            j.params.erase(Key{"tx"}), j.params.erase(Key{"ty"}), j.params.erase(Key{"tz"});
        }
        const double hold = j.params.num("hold") + (j.params.num("hold_to") - j.params.num("hold")) * ease(t, 8.0);
        j.params.set("hold", hold);
        if (hold > 1e-4) target[i] = slerp(target[i], q_of(j, "h"), hold), back[i] = slerp(back[i], q_of(j, "h"), hold);
    }

    // Inertialized: what it plays changed at once (`play` with `inert`), so
    // each joint's turn from where it was to where the clips have it now -
    // and how much faster it was turning than they turn it - is taken up as
    // an offset (`i*`; a joint that travels, its place too: `ip*`), let go of
    // by a critically damped spring of the half-life said: one motion, no two
    // averaged, its speed kept across the change. Gone once it is let go.
    {
        static const Key ix{"ix"}, iy{"iy"}, iz{"iz"}, ivx{"ivx"}, ivy{"ivy"}, ivz{"ivz"}, ipx{"ipx"}, ipy{"ipy"}, ipz{"ipz"}, iux{"iux"}, iuy{"iuy"}, iuz{"iuz"};
        static const Key awx{"awx"}, awy{"awy"}, awz{"awz"};
        if (switched) {
            self.params.set("inert_from", 0.0).set("inert", 1.0);
            for (std::size_t i = 0; i < js.size(); ++i) {
                Element& j = *js[i];
                // From what it meant last step (its clips' pose, `aq` - not
                // the body, whose feet were set on the ground over it: taken
                // for motion, that would be set on the ground again).
                const Q meant_was = parent[i] >= 0 ? norm(mul(conj(q_of(*js[std::size_t(parent[i])], "aq")), q_of(j, "aq"))) : norm(q_of(j, "aq"));
                const Vec3d x = log_of(mul(meant_was, conj(target[i])));
                // How fast it was turning on its parent (from the meant turns'
                // speeds in the being's frame), and how fast the clips turn it now.
                const Vec3d wi{j.params.num(awx), j.params.num(awy), j.params.num(awz)};
                Vec3d wp{};
                Q pq{};
                if (parent[i] >= 0) {
                    const Element& up = *js[std::size_t(parent[i])];
                    wp = {up.params.num(awx), up.params.num(awy), up.params.num(awz)}, pq = norm(q_of(up, "aq"));
                }
                const Vec3d v = rotate(conj(pq), wi - wp) - log_of(mul(target[i], conj(back[i]))) * (1.0 / dt);
                j.params.set(ix, x.x).set(iy, x.y).set(iz, x.z).set(ivx, v.x).set(ivy, v.y).set(ivz, v.z);
                const Vec3d d = stood[i] - offset_of(j);
                if (length(d) > 1e-6) j.params.set(ipx, d.x).set(ipy, d.y).set(ipz, d.z).set(iux, 0.0).set(iuy, 0.0).set(iuz, 0.0);
            }
        }
        if (self.params.num("inert") > 0.5) {
            const double half = self.params.num("inert_half", 0.1);
            double most = 0;
            for (std::size_t i = 0; i < js.size(); ++i) {
                Element& j = *js[i];
                if (!j.params.has(ix)) continue;
                Vec3d x{j.params.num(ix), j.params.num(iy), j.params.num(iz)}, v{j.params.num(ivx), j.params.num(ivy), j.params.num(ivz)};
                let_go(x, v, half, dt);
                j.params.set(ix, x.x).set(iy, x.y).set(iz, x.z).set(ivx, v.x).set(ivy, v.y).set(ivz, v.z);
                const Q off = exp_of(x);
                target[i] = norm(mul(off, target[i])), back[i] = norm(mul(off, back[i]));
                most = std::max({most, length(x), length(v) * half});
                if (j.params.has(ipx)) {
                    Vec3d p{j.params.num(ipx), j.params.num(ipy), j.params.num(ipz)}, u{j.params.num(iux), j.params.num(iuy), j.params.num(iuz)};
                    let_go(p, u, half, dt);
                    j.params.set(ipx, p.x).set(ipy, p.y).set(ipz, p.z).set(iux, u.x).set(iuy, u.y).set(iuz, u.z);
                    const Vec3d at = offset_of(j) + p;
                    j.params.set("tx", at.x).set("ty", at.y).set("tz", at.z);
                    most = std::max(most, length(p) * 4.0);
                }
            }
            if (most < 1e-4) {
                self.params.set("inert", 0.0);
                for (Element* j : js)
                    for (Key k : {ix, iy, iz, ivx, ivy, ivz, ipx, ipy, ipz, iux, iuy, iuz}) j->params.erase(k);
            }
        }
    }

    // Followed: each joint whose motion is another being's taken for its own
    // (`follow_q`, a turn in the being's frame; for one that travels
    // `follow_t`, a place - carried by retarget(..., own)) is turned that way
    // by how much it follows (`follow`), as a clip would turn it - before
    // anything leads it: it is what it means to do, so a ragdoll's muscles
    // aim for it and a knock (`lead`) is on top of it. The turn on its parent
    // is found from its parent's as followed, now and at the step's start.
    {
        const double fol = self.params.num("follow") +
                           (self.params.num("follow_to") - self.params.num("follow")) * ease(t, 1.0 / std::max(1e-3, self.params.num("follow_fade", 0.3)));
        self.params.set("follow", fol);
        if (fol > 1e-4) {
            std::vector<Q> wt(js.size()), wb(js.size());
            for (std::size_t i = 0; i < js.size(); ++i) {
                Element& j = *js[i];
                const Q pt = parent[i] >= 0 ? wt[std::size_t(parent[i])] : Q{}, pb = parent[i] >= 0 ? wb[std::size_t(parent[i])] : Q{};
                Q w = mul(pt, target[i]), w_b = mul(pb, back[i]);
                if (j.params.num("follow_at") > 0.5) {
                    Q l = norm(q_of(j, "follow_q"));
                    if (dot(l, w) < 0) l = scaled(l, -1);
                    w = slerp(w, l, fol);
                    Q lb = l;
                    if (dot(lb, w_b) < 0) lb = scaled(lb, -1);
                    w_b = slerp(w_b, lb, fol);
                    target[i] = norm(mul(conj(pt), w)), back[i] = norm(mul(conj(pb), w_b));
                }
                wt[i] = w, wb[i] = w_b;
                if (j.params.num("follow_moves") > 0.5) {
                    const Vec3d from = offset_of(j), to{j.params.num("follow_tx"), j.params.num("follow_ty"), j.params.num("follow_tz")};
                    const Vec3d p = from + (to - from) * fol;
                    j.params.set("tx", p.x).set("ty", p.y).set("tz", p.z);
                }
            }
        }
    }

    // Where every joint is, for a set of turns.
    const auto fk = [&](const std::vector<Q>& qs, std::vector<Frame>& out) {
        out.resize(js.size());
        for (std::size_t i = 0; i < js.size(); ++i) {
            const Vec3d off = offset_of(*js[i]);
            if (parent[i] < 0) out[i] = {off, qs[i]};
            else {
                const Frame& pf = out[std::size_t(parent[i])];
                out[i] = {pf.p + rotate(pf.r, off), mul(pf.r, qs[i])};
            }
        }
    };

    // What it means to do, in its own frame - each joint's turn and place as
    // its clips and holds say, before anything leads it: what whoever moves
    // it from outside (a ragdoll's muscles) aims for. And how fast that turn
    // goes (`aw`, radians a second of its line, in its own frame): the turn
    // from the clips at the step's start to its end - both read from the
    // clips, so a muscle follows the motion and not only the pose.
    std::vector<Frame> meant, before;
    {
        fk(target, meant);
        fk(back, before);
        for (std::size_t i = 0; i < js.size(); ++i) {
            set_q(*js[i], meant[i].r, "aq");
            js[i]->params.set("ax", meant[i].p.x).set("ay", meant[i].p.y).set("az", meant[i].p.z);
            Q d = norm(mul(meant[i].r, conj(before[i].r)));
            if (d.w < 0) d = scaled(d, -1);
            const double s = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            // (No faster than a body turns: a clip that jumps is a jump in
            // the pose, not a spin to chase.)
            const double spin = s > 1e-12 ? std::min(2.0 * std::atan2(s, d.w) / dt, 20.0) : 0.0;
            const Vec3d aw = s > 1e-12 ? Vec3d{d.x, d.y, d.z} * (spin / s) : Vec3d{};
            js[i]->params.set("awx", aw.x).set("awy", aw.y).set("awz", aw.z);
        }
    }

    // Led: each joint that something leads (`lead_q`, a turn in its own frame,
    // and for one that travels `lead_t`, a place) is turned that way in the
    // being's frame by how much it gives itself (`lead`), the turn on its
    // parent found from its parent's as led - so a led arm on an unled body
    // still points where it is led. What leads it is another being's motion
    // carried across (`retarget`), or a ragdoll's bones.
    const double lead = self.params.num("lead") +
                        (self.params.num("lead_to") - self.params.num("lead")) * ease(t, 1.0 / std::max(1e-3, self.params.num("lead_fade", 0.3)));
    self.params.set("lead", lead);
    std::vector<double> led(js.size(), 0.0);
    if (lead > 1e-4) {
        std::vector<Q> world(js.size());
        for (std::size_t i = 0; i < js.size(); ++i) {
            Element& j = *js[i];
            const Q pw = parent[i] >= 0 ? world[std::size_t(parent[i])] : Q{};
            Q w = mul(pw, target[i]);
            if (j.params.num("lead_at") > 0.5) {
                Q l = norm(q_of(j, "lead_q"));
                if (dot(l, w) < 0) l = scaled(l, -1);
                w = slerp(w, l, lead);
                target[i] = norm(mul(conj(pw), w));
                led[i] = lead;
            }
            world[i] = w;
            if (j.params.num("lead_moves") > 0.5) {
                const Vec3d from = offset_of(j), to{j.params.num("lead_tx"), j.params.num("lead_ty"), j.params.num("lead_tz")};
                const Vec3d p = from + (to - from) * lead;
                j.params.set("tx", p.x).set("ty", p.y).set("tz", p.z);
            }
        }
    }

    // Its feet's ground: the body raised or lowered to the lowest ground any
    // foot of it is on (`ground`, eased; its roots carry it), each foot then
    // brought to its own (Being::plant) - a body over a step stands on the
    // lower side, its other leg bent up onto it, not hung from the top.
    std::vector<const Element*> feet;
    {
        double lowest = 1e18;
        for (const Element& g : elements())
            if (g.kind == kGoal && g.params.num("plants") > 0.5) feet.push_back(&g), lowest = std::min(lowest, g.params.num("floor_to"));
        if (feet.empty()) lowest = 0;
        const double ground = self.params.num("ground") + (lowest - self.params.num("ground")) * ease(dt, 10.0);
        self.params.set("ground", ground);
        if (std::fabs(ground) > 1e-6)
            for (std::size_t i = 0; i < js.size(); ++i)
                if (parent[i] < 0) {
                    const Vec3d at = offset_of(*js[i]);
                    js[i]->params.set("tx", at.x).set("ty", at.y + ground).set("tz", at.z);
                }
    }

    // The slope round it, eased to what it was told; and its balance, a step
    // on: pushed by its load where its centre is off the body's, by ground
    // steeper than it stands on easily (a quarter radian ahead, a seventh to
    // its side) and by turning as it goes (outward); held upright as it is
    // steadied; righting itself the while. Past 1 of sway, it stumbles.
    {
        Params& p = self.params;
        const double ahead = p.num("slope_ahead") + (p.num("slope_ahead_to") - p.num("slope_ahead")) * ease(dt, 6.0);
        const double side = p.num("slope_side") + (p.num("slope_side_to") - p.num("slope_side")) * ease(dt, 6.0);
        p.set("slope_ahead", ahead).set("slope_side", side);
        const auto beyond = [](double v, double easy) { return v > easy ? v - easy : v < -easy ? v + easy : 0.0; };
        const double load = std::max(p.num("load"), 0.0), speed = std::hypot(p.num("go_x"), p.num("go_z"));
        const double push_x = 0.02 * load * p.num("load_x") - beyond(ahead, 0.25);
        const double push_z = 0.02 * load * p.num("load_z") - beyond(side, 0.15) - 0.15 * speed * p.num("go_turn");
        double sx = p.num("sway_x"), sz = p.num("sway_z");
        sx += (push_x - 1.5 * std::clamp(p.num("steady_x"), -1.0, 1.0)) * dt;
        sz += (push_z - 1.5 * std::clamp(p.num("steady_z"), -1.0, 1.0)) * dt;
        const double right = 1.0 - ease(dt, 0.8);
        sx *= right, sz *= right;
        if (std::hypot(sx, sz) >= 1.0) {
            p.set("stumbles", p.num("stumbles") + 1.0);
            sx = sz = 0.0;
        }
        p.set("sway_x", sx).set("sway_z", sz).set("balance", 1.0 - std::min(std::hypot(sx, sz), 1.0));
        // Its hips leaned with the slope and its sway: forward up a slope, a
        // little toward a slope to its side, as far as it sways.
        const double forward = p.num("hip_lean", 0.35) * ahead + 0.15 * sx, toward = 0.07 * side + 0.15 * sz;
        if (std::fabs(forward) > 1e-6 || std::fabs(toward) > 1e-6) {
            const Q lean = norm(mul(axis_angle(Vec3d{0, 0, 1}, -forward), axis_angle(Vec3d{1, 0, 0}, toward)));
            for (std::size_t i = 0; i < js.size(); ++i)
                if (parent[i] < 0) target[i] = norm(mul(lean, target[i]));
        }
    }

    // Reaching and looking: each goal's chain turned toward its point, and
    // the target turned that far toward it by the goal's weight. A chain of
    // two is solved exactly (two bones and the line to the point are a
    // triangle); a longer one by cyclic coordinate descent; a look turns the
    // tip's forward to the point, shared down its chain.
    // (Its feet last, on the body as it now is - `on_body`: a leg on the
    // ground solved where its body is, not where its body is going, so that
    // a joint eased after its target at its stiffness never leaves a foot
    // late, off where it was set down.)
    const auto solve_goals = [&](bool on_body, std::vector<Q>& base) {
    for (Element& g : elements()) {
        if (g.kind != kGoal || (g.params.num("plants") > 0.5) != on_body) continue;
        const double w = g.params.num("weight") + (g.params.num("to") - g.params.num("weight")) * ease(t, 1.0 / std::max(1e-3, g.params.num("fade", 0.25)));
        g.params.set("weight", w);
        if (w < 1e-4) continue;
        auto tip_at = at.find(text_or_none(g.params, kTip));
        if (tip_at == at.end()) continue;
        const std::size_t tip = tip_at->second;
        std::vector<std::size_t> chain;  // from the tip's parent up
        for (int c = parent[tip], n = int(g.params.num("links", 2)); c >= 0 && n > 0; c = parent[std::size_t(c)], --n) chain.push_back(std::size_t(c));
        const bool has_pole = g.params.num("pole") > 0.5;
        const Vec3d pole{g.params.num("pole_x"), g.params.num("pole_y"), g.params.num("pole_z")};
        std::vector<Q> work = base;
        const bool plants = g.params.num("plants") > 0.5;
        // (A leg on the ground solved from how its clip turns it, on the body
        // as it is - never from how it was solved last step: a solve settles
        // where the knee and the foot go, not how the leg is rolled about its
        // length, and begun from its own last answer that roll would wander.)
        if (on_body) {
            work[tip] = target[tip];
            for (std::size_t c : chain) work[c] = target[c];
        }
        std::vector<Frame> f;
        fk(work, f);
        if (plants) {
            bool others_down = true;
            for (const Element* o : feet)
                if (o != &g && o->params.num("locked") < 0.5) others_down = false;
            // (Where its clip has it - not the body's foot, which is where it
            // was solved to be: on the ground, as its body is raised or lowered.)
            const Vec3d clip_at = meant[tip].p + Vec3d{0, self.params.num("ground"), 0};
            if (plant_step(g, self.params, clip_at, (meant[tip].p - before[tip].p) * (1.0 / dt), dt, others_down))
                self.params.set("footfalls", self.params.num("footfalls") + 1.0).set("footfall_on", g.params.get_or<std::string>("on", ""));
        }
        const Vec3d aim{g.params.num(keys::x), g.params.num(keys::y), g.params.num(keys::z)};
        // (A foot set down keeps its own turn, however its leg bends - turned
        // as it was on the ground, as the body turns over it; its knee half as far.)
        const double foot_yaw = plants ? g.params.num("lock_yaw") : 0.0;
        // (Its clip's turn, not the body's: what the body had was turned last step.)
        // (And its sole turned to the ground under it: the way its ground
        // faces, no more than `tilt_most`, none on ground past `tilt_steep`.)
        Q foot_tilt{};
        if (plants) {
            const double tx = g.params.num("tilt_x"), tz = g.params.num("tilt_z"), sideways = std::sqrt(tx * tx + tz * tz);
            const double steep = std::asin(std::min(sideways, 1.0));
            if (sideways > 1e-6 && steep < g.params.num("tilt_steep", 0.8))
                foot_tilt = axis_angle(Vec3d{tz, 0.0, -tx} * (1.0 / sideways), std::min(steep, g.params.num("tilt_most", 0.45)));
        }
        const Q foot_turn = plants ? norm(mul(foot_tilt, mul(from_euler(foot_yaw, 0, 0), meant[tip].r))) : f[tip].r;
        // Joint `c` turned by `d`, a turn in the being's frame (and all it carries).
        const auto turn_by = [&](std::size_t c, const Q& d) {
            const Q pr = parent[c] >= 0 ? f[std::size_t(parent[c])].r : Q{};
            work[c] = norm(mul(mul(conj(pr), mul(d, pr)), work[c]));
        };
        std::vector<std::size_t> moved = chain;
        if (g.params.num("aim") > 0.5) {
            // Looking: from the top of the chain down to the tip, each joint
            // turns the tip's forward its share of the way to the point (the
            // tip all the rest), and then about that forward to bring its up
            // toward the pole.
            moved.insert(moved.begin(), tip);
            std::reverse(moved.begin(), moved.end());
            std::vector<double> shares;
            {
                std::istringstream in(g.params.get_or<std::string>("shares", ""));
                for (double v; in >> v;) shares.push_back(v);
            }
            const Q unbind = conj(norm(q_of(*js[tip], "bq")));
            const Vec3d fwd{g.params.num("fwd_x", 1.0), g.params.num("fwd_y"), g.params.num("fwd_z")};
            const Vec3d up{g.params.num("up_x"), g.params.num("up_y", 1.0), g.params.num("up_z")};
            for (std::size_t k = 0; k < moved.size(); ++k) {
                const double share = k + 1 == moved.size() ? 1.0 : k < shares.size() ? std::clamp(shares[k], 0.0, 1.0) : std::min(0.6, 0.2 + 0.1 * double(k));
                if (k > 0) fk(work, f);
                const Q held = mul(f[tip].r, unbind);  // the tip's turn from how it is bound
                const Vec3d F = rotate(held, fwd), U = rotate(held, up);
                Q d = slerp(Q{}, between(F, aim - f[tip].p), share);
                if (has_pole) {
                    const Vec3d F2 = rotate(d, F), U2 = rotate(d, U);
                    const double lf = length(F2);
                    if (lf > 1e-9) {
                        const Vec3d n = F2 * (1.0 / lf), want = pole - f[tip].p;
                        const Vec3d u = U2 - n * dot(U2, n), p = want - n * dot(want, n);
                        if (length(u) > 1e-9 && length(p) > 1e-9) d = mul(slerp(Q{}, between(u, p), share), d);
                    }
                }
                turn_by(moved[k], d);
            }
        } else if (chain.size() == 2) {
            // Two bones, exactly: the middle where both bones' lengths put it
            // on the side the pole is (or the side it bends now), the root
            // turned to put it there, then the middle to put the tip on the point.
            const std::size_t mid = chain[0], root = chain[1];
            const Vec3d R = f[root].p, M = f[mid].p, T = f[tip].p;
            const double a = length(M - R), b = length(T - M), dist = length(aim - R);
            if (a > 1e-9 && b > 1e-9 && dist > 1e-9) {
                const Vec3d dir = (aim - R) * (1.0 / dist);
                const double full = a + b, soft = g.params.num("soften");
                double c = dist;
                // Softened: past `soften` of its whole length it straightens
                // ever more slowly toward it, never quite (no snap at full stretch).
                if (soft > 0 && soft < 1 && c > soft * full) {
                    const double s0 = soft * full, room = full - s0;
                    c = s0 + room * (1.0 - std::exp(-(c - s0) / room));
                }
                c = std::clamp(c, std::max(std::fabs(a - b), 1e-9), full);
                const double cr = std::clamp((a * a + c * c - b * b) / (2 * a * c), -1.0, 1.0);
                Vec3d side = (has_pole ? pole : M) - R;
                // (A foot's knee bends the way its clip bends it - not the
                // way it was solved to bend last step.)
                if (plants && !has_pole) side = meant[mid].p - meant[root].p;
                if (plants && !has_pole) {
                    // (A leg nearly straight says little of which way its knee
                    // goes: a foot's knee goes ahead, as the being faces - and,
                    // set down and turned on the ground, half as far round as its foot.)
                    side = side - dir * dot(side, dir);
                    side = side + Vec3d{g.params.num("ahead_x", 1.0), 0, g.params.num("ahead_z")} * std::max(0.0, 0.08 * a - length(side));
                    if (foot_yaw != 0.0) side = rotate(from_euler(0.5 * foot_yaw, 0, 0), side);
                }
                side = side - dir * dot(side, dir);
                if (length(side) < 1e-6 * a) {
                    // (Straight, with no pole: toward where it faces (+x, as
                    // a knee bends), else across the way it points.)
                    const Vec3d ahead{1, 0, 0};
                    side = ahead - dir * dot(ahead, dir);
                    if (length(side) < 1e-3) side = cross(dir, Vec3d{0, 0, 1});
                }
                side = side * (1.0 / length(side));
                if (const double tw = g.params.num("twist"); tw != 0.0) side = rotate(axis_angle(dir, tw * kDeg), side);
                const Vec3d M2 = R + dir * (a * cr) + side * (a * std::sqrt(std::max(0.0, 1.0 - cr * cr)));
                const Vec3d T2 = R + dir * c;
                turn_by(root, between(M - R, M2 - R));
                fk(work, f);
                turn_by(mid, between(f[tip].p - f[mid].p, T2 - f[mid].p));
            }
        } else {
            for (int it = 0; it < 12; ++it) {
                for (std::size_t c : chain) {
                    fk(work, f);
                    turn_by(c, between(f[tip].p - f[c].p, aim - f[c].p));
                }
            }
        }
        if (plants && parent[tip] >= 0) {
            fk(work, f);
            work[tip] = norm(mul(conj(f[std::size_t(parent[tip])].r), foot_turn));
            moved.push_back(tip);
        }
        for (std::size_t c : moved) base[c] = slerp(base[c], work[c], w);
    }
    };
    solve_goals(false, target);

    // The body goes where it is meant to, each joint at its own stiffness -
    // and wholly as far as it is led: what leads it has dynamics of its own.
    for (std::size_t i = 0; i < js.size(); ++i)
        set_q(*js[i], norm(slerp(q_of(*js[i]), target[i], first ? 1.0 : std::max(led[i], ease(t, js[i]->params.num("stiffness", 24.0))))));
    // And its feet on the ground, on the body as it is now.
    if (!feet.empty()) {
        std::vector<Q> body(js.size());
        for (std::size_t i = 0; i < js.size(); ++i) body[i] = q_of(*js[i]);
        solve_goals(true, body);
        for (std::size_t i = 0; i < js.size(); ++i) set_q(*js[i], body[i]);
    }
    resolve();
}

void Being::resolve() {
    const bool lived = element(self_id()).params.num("lived") > 0.5;
    // Every joint and part posed in the being's frame from the joints' turns;
    // and each joint as it is bound, at rest (`bq`): the frame a motion is
    // carried in from one body to another.
    // (Found by the names as written, as live() finds them.)
    std::unordered_map<std::string_view, Frame> f;
    std::unordered_map<std::string_view, Q> bind;
    for (Element& e : elements()) {
        if (e.kind != kJoint) continue;
        const Vec3d off = offset_of(e);
        const std::string_view up = text_or_none(e.params, kParentJoint);
        auto pit = f.find(up);
        Frame mine = pit == f.end() ? Frame{off, q_of(e)} : Frame{pit->second.p + rotate(pit->second.r, off), mul(pit->second.r, q_of(e))};
        f[e.id.str()] = mine;
        auto bit = bind.find(up);
        const Q b = norm(bit == bind.end() ? rest_of(e) : mul(bit->second, rest_of(e)));
        bind[e.id.str()] = b;
        set_q(e, b, "bq");
        double y, p, r;
        to_euler(mine.r, y, p, r);
        e.params.set("px", mine.p.x).set("py", mine.p.y).set("pz", mine.p.z).set("pyaw", y).set("ppitch", p).set("proll", r);
        set_q(e, norm(mine.r), "pq");
        if (!lived) {  // (before it has lived, it means what it is - however it has been made since)
            set_q(e, norm(mine.r), "aq");
            e.params.set("ax", mine.p.x).set("ay", mine.p.y).set("az", mine.p.z);
        }
    }
    for (Element& e : elements()) {
        if (e.kind != kPart) continue;
        auto jt = f.find(text_or_none(e.params, kJoint));
        const Frame jf = jt == f.end() ? Frame{} : jt->second;
        const Vec3d at = jf.p + rotate(jf.r, {e.params.num("ox"), e.params.num("oy"), e.params.num("oz")});
        double y, p, r;
        to_euler(jf.r, y, p, r);
        // (A mesh stands on its base and turns about its middle, half its height
        // above that base, straight up - so its base is set that far below
        // its middle, whatever its turn.)
        const Vec3d base = at - Vec3d{0.0, e.params.num(keys::sy) * 0.5, 0.0};
        e.params.set("px", base.x).set("py", base.y).set("pz", base.z).set("pyaw", y).set("ppitch", p).set("proll", r);
    }
}

std::vector<std::string> Being::faults() const {
    std::vector<std::string> out;
    for (const Element& j : elements()) {
        if (j.kind != kJoint) continue;
        const Element* up = find(Key{j.params.get_or<std::string>("parent_joint", "")});
        if (!up || up->kind != kJoint) continue;
        const Vec3d bone = offset_of(j);
        const Vec3d at{j.params.num("ax"), j.params.num("ay"), j.params.num("az")};
        const Vec3d want = Vec3d{up->params.num("ax"), up->params.num("ay"), up->params.num("az")} + rotate(norm(q_of(*up, "aq")), bone);
        const Vec3d d = at - want;
        const double off = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z), l = std::sqrt(bone.x * bone.x + bone.y * bone.y + bone.z * bone.z);
        if (off > 0.005 + 0.05 * l) {
            char b[200];
            std::snprintf(b, sizeof b, "joint %s means to be %.3f m from where its own bone puts it - its meant pose is of a body it no longer is", j.id.str().c_str(), off);
            out.push_back(b);
            break;  // (one says it: the rest follow from it)
        }
    }
    return out;
}

Pose Being::pose_of(Key id) const {
    const Element& e = element(id);
    return Pose{{e.params.num("px"), e.params.num("py"), e.params.num("pz")}, e.params.num("pyaw"), e.params.num("ppitch"), e.params.num("proll")};
}

std::vector<Key> Being::parts() const {
    std::vector<Key> out;
    for (const Element& e : elements())
        if (e.kind == kPart) out.push_back(e.id);
    return out;
}

std::vector<Key> Being::joints() const {
    std::vector<Key> out;
    for (const Element& e : elements())
        if (e.kind == kJoint) out.push_back(e.id);
    return out;
}

Key show(StateGraph& g, const Being& b, Key host, Key anchor) {
    State& h = g.state(host);
    const Key name{b.id().str() + ".body"};
    Functor f(name, b.id(), host);
    for (Key part : b.parts()) {
        const Element& p = b.element(part);
        const Key there{anchor.str() + "." + part.str()};
        Element* e = h.find(there);
        if (!e) e = &h.add_element(there, kinds::mesh);
        e->params.set(keys::parent, anchor.str()).set("shape", p.params.get_or<std::string>("shape", "box"));
        for (Key k : {keys::sx, keys::sy, keys::sz, keys::r, keys::g, keys::b}) e->params.set(k, p.params.num(k));
        // What it is carried by: the part's pose in the being's frame, as its
        // pose on the anchor it rides.
        f.on_object(part, there, [](const Element& src, Element& dst) {
            dst.params.set(keys::x, src.params.num("px")).set(keys::y, src.params.num("py")).set(keys::z, src.params.num("pz"));
            dst.params.set(keys::yaw, src.params.num("pyaw")).set(keys::pitch, src.params.num("ppitch")).set(keys::roll, src.params.num("proll"));
        });
    }
    g.set_functor(std::move(f));
    g.functor(name)->apply(b, h);
    return name;
}

namespace {
// A joint's name without the namespace a rig gave it (`mixamorig:Hips`,
// `mixamorig1:Hips`, `Armature|Hips` are all `Hips`).
std::string bare(const std::string& n) {
    const std::size_t k = n.find_last_of(":|");
    return k == std::string::npos ? n : n.substr(k + 1);
}
}  // namespace

std::vector<std::pair<Key, Key>> same_joints(const Being& from, const Being& to) {
    std::unordered_map<std::string, Key> theirs, by_bare;
    for (Key j : to.joints()) theirs.emplace(j.str(), j), by_bare.emplace(bare(j.str()), j);
    std::vector<std::pair<Key, Key>> out;
    for (Key j : from.joints()) {
        if (auto it = theirs.find(j.str()); it != theirs.end()) out.emplace_back(j, it->second);
        else if (auto bt = by_bare.find(bare(j.str())); bt != by_bare.end()) out.emplace_back(j, bt->second);
    }
    return out;
}

namespace {
double length_of(const Element& e) {
    const double x = e.params.num(keys::x), y = e.params.num(keys::y), z = e.params.num(keys::z);
    return std::sqrt(x * x + y * y + z * z);
}
// How far a joint stands from its parent in `b`: as it was made - unless
// its first clip that carries it first puts it nowhere near there (a rig
// whose root holds the rest, its hips made beside it; Mixamo's FBX), and
// then as that clip stands it. (A crouch in a clip is a crouch carried: only
// a made length that says nothing of the body is passed over.)
double standing(const Being& b, Key joint) {
    const Element* j = b.find(joint);
    const double made = j ? length_of(*j) : 0.0;
    for (const Element& c : b.elements()) {
        if (c.kind != kClip) continue;
        std::istringstream in(c.params.get_or<std::string>("keys", ""));
        double first = 1e18, len = -1;
        for (std::string line; std::getline(in, line);) {
            std::istringstream ls(line);
            double t, x, y, z;
            std::string jn, word;
            if (ls >> t >> jn >> word && jn == joint.str() && (word == "p" || word == "pc") && (ls >> x >> y >> z) && t < first)
                first = t, len = std::sqrt(x * x + y * y + z * z);
        }
        if (len >= 0) return made < 0.5 * len || made > 2.0 * len ? len : made;
    }
    return made;
}
}  // namespace

Key retarget(StateGraph& g, const Being& from, const Being& to, Key name, bool own) {
    // (Led: lead_*; taken for its own: follow_*.)
    const std::string ch = own ? "follow" : "lead";
    const Key k_q{ch + "_q"}, k_at{ch + "_at"}, k_moves{ch + "_moves"}, k_tx{ch + "_tx"}, k_ty{ch + "_ty"}, k_tz{ch + "_tz"};
    if (name.empty()) name = Key{from.id().str() + ".to." + to.id().str()};
    Functor f(name, from.id(), to.id());
    for (const auto& [a, b] : same_joints(from, to)) {
        // A joint that travels (the hips) travels as far for its size: by how
        // far each stands it from its parent - the other as it stands (its
        // clip), this one as it was made.
        // Only a root travels: a rig that keys a place on every bone (Mixamo's)
        // keys its own proportions there, not motion - carried, they would
        // make this body the other's shape.
        const Element& mine = to.element(b);
        const Element* up = to.find(Key{mine.params.get_or<std::string>("parent_joint", "")});
        const bool root = !up || up->kind != kJoint;
        const double ls = standing(from, a), k = ls > 1e-6 ? length_of(mine) / ls : 1.0;
        f.on_object(a, b, [=](const Element& src, Element& dst) {
            // The source's turn away from its own bind, put on the target's
            // bind: lead = pq_src * bq_src^-1 * bq_dst. Each body keeps its
            // own bones' axes; only the motion crosses.
            const Q w = mul(mul(q_of(src, "pq"), conj(q_of(src, "bq"))), q_of(dst, "bq"));
            set_q(dst, norm(w), own ? "follow_q" : "lead_q");
            dst.params.set(k_at, 1.0);
            // A joint that travels (the hips) travels as far for its size:
            // by how far each stands from its parent as made.
            if (root && src.params.has(Key{"tx"})) {
                dst.params.set(k_tx, src.params.num("tx") * k).set(k_ty, src.params.num("ty") * k).set(k_tz, src.params.num("tz") * k);
                dst.params.set(k_moves, 1.0);
            } else {
                dst.params.set(k_moves, 0.0);
            }
        });
    }
    g.set_functor(std::move(f));
    g.keep(name);
    return name;
}

Key carry_body(StateGraph& g, const Being& from, const Being& to, Key name) {
    if (name.empty()) name = Key{from.id().str() + ".as." + to.id().str()};
    if (g.functor(name)) return name;
    Functor f(name, from.id(), to.id());
    for (const Element& e : from.elements()) {
        if (e.kind == kClip || e.kind == Key{"skin"}) continue;
        const Element* there = to.find(e.id);
        if (there && there->kind == e.kind) f.on_object(e.id, e.id, transport::copy_all);
    }
    f.from_living();
    g.set_functor(std::move(f));
    g.keep(name);
    return name;
}

}  // namespace sg
