#include "sg/domains/Being.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <sstream>

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
    const spatial::M3 m = spatial::from_euler(yaw, pitch, roll);
    Q q;
    const double t = m(0, 0) + m(1, 1) + m(2, 2);
    if (t > 0) {
        const double s = std::sqrt(t + 1.0) * 2;
        q = {0.25 * s, (m(2, 1) - m(1, 2)) / s, (m(0, 2) - m(2, 0)) / s, (m(1, 0) - m(0, 1)) / s};
    } else if (m(0, 0) > m(1, 1) && m(0, 0) > m(2, 2)) {
        const double s = std::sqrt(1.0 + m(0, 0) - m(1, 1) - m(2, 2)) * 2;
        q = {(m(2, 1) - m(1, 2)) / s, 0.25 * s, (m(0, 1) + m(1, 0)) / s, (m(0, 2) + m(2, 0)) / s};
    } else if (m(1, 1) > m(2, 2)) {
        const double s = std::sqrt(1.0 + m(1, 1) - m(0, 0) - m(2, 2)) * 2;
        q = {(m(0, 2) - m(2, 0)) / s, (m(0, 1) + m(1, 0)) / s, 0.25 * s, (m(1, 2) + m(2, 1)) / s};
    } else {
        const double s = std::sqrt(1.0 + m(2, 2) - m(0, 0) - m(1, 1)) * 2;
        q = {(m(1, 0) - m(0, 1)) / s, (m(0, 2) + m(2, 0)) / s, (m(1, 2) + m(2, 1)) / s, 0.25 * s};
    }
    return norm(q);
}
void to_euler(const Q& q, double& yaw, double& pitch, double& roll) {
    const Q n = norm(q);
    const double w = n.w, x = n.x, y = n.y, z = n.z;
    spatial::M3 m;
    m.a = {1 - 2 * (y * y + z * z), 2 * (x * y - w * z),     2 * (x * z + w * y),
           2 * (x * y + w * z),     1 - 2 * (x * x + z * z), 2 * (y * z - w * x),
           2 * (x * z - w * y),     2 * (y * z + w * x),     1 - 2 * (x * x + y * y)};
    spatial::to_euler(m, yaw, pitch, roll);
}
// The smallest turn taking direction `a` to direction `b`.
Q between(Vec3d a, Vec3d b) {
    const auto len = [](const Vec3d& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); };
    const double la = len(a), lb = len(b);
    if (la < 1e-9 || lb < 1e-9) return {};
    a = a * (1 / la), b = b * (1 / lb);
    const double c = a.x * b.x + a.y * b.y + a.z * b.z;
    const Vec3d k{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    if (c < -0.99999) return axis_angle(std::fabs(a.x) < 0.9 ? Vec3d{0, -a.z, a.y} : Vec3d{-a.y, a.x, 0}, 3.14159265358979323846);
    return norm({1 + c, k.x, k.y, k.z});
}

Q q_of(const Element& e, const char* p = "q") {
    const std::string s = p;
    return {e.params.num(Key{s + "w"}, 1.0), e.params.num(Key{s + "x"}), e.params.num(Key{s + "y"}), e.params.num(Key{s + "z"})};
}
void set_q(Element& e, const Q& q, const char* p = "q") {
    const std::string s = p;
    e.params.set(Key{s + "w"}, q.w).set(Key{s + "x"}, q.x).set(Key{s + "y"}, q.y).set(Key{s + "z"}, q.z);
}
Q rest_of(const Element& j) {
    // At rest: its own turn, as a model gave it (`rest_q*`), or as degrees said.
    if (j.params.has(Key{"rest_qw"})) return norm(q_of(j, "rest_q"));
    return from_euler(j.params.num("rest_yaw") , j.params.num("rest_pitch"), j.params.num("rest_roll"));
}
// Where it stands on its parent: as a clip moves it now (`t*`), else as made.
Vec3d offset_of(const Element& e) {
    if (e.params.has(Key{"tx"})) return {e.params.num("tx"), e.params.num("ty"), e.params.num("tz")};
    return {e.params.num(keys::x), e.params.num(keys::y), e.params.num(keys::z)};
}
double ease(double dt, double rate) { return rate <= 0 ? 1.0 : 1.0 - std::exp(-dt * rate); }

const Key kJoint{"joint"}, kPart{"part"}, kClip{"clip"}, kLayer{"layer"}, kGoal{"goal"};

// Every joint in order, parents first, and where each is in the being's frame
// for a set of local turns.
struct Frame {
    Vec3d p;
    Q r;
};
}  // namespace

Being::Being(Key id, double scale) : State(std::move(id)) {
    add_element(self_id(), Key{"being"}).params.set("scale", scale).set("tempo_exp", 1.0).set("age", 0.0);
    for (int i = 0; i < kLayers; ++i)
        add_element(Key{"layer" + std::to_string(i)}, kLayer)
            .params.set("clip", std::string{})
            .set("phase", 0.0)
            .set("weight", 0.0)
            .set("to", 0.0)
            .set("fade", 0.3)
            .set("speed", 1.0)
            .set("loop", 1.0);
    //   self --live--> self: the spirit's step, on its line of time.
    loop(Key{"live"}, self_id(), live_event(), [](State& s, Element&, Element*, const Event& ev) { static_cast<Being&>(s).live(ev.args.num(keys::dt)); });
    //   self --play--> self: a clip, faded in (and the others out, unless not alone).
    loop(Key{"play"}, self_id(), play_event(), [](State& s, Element&, Element*, const Event& ev) {
        const std::string clip = ev.args.get_or<std::string>("clip", "");
        if (!s.find(Key{"clip." + clip})) return;
        const double fade = ev.args.num("fade", 0.3), weight = ev.args.num("weight", 1.0);
        Element* slot = nullptr;
        for (int i = 0; i < kLayers; ++i) {
            Element& l = s.element(Key{"layer" + std::to_string(i)});
            if (l.params.get_or<std::string>("clip", "") == clip) slot = &l;
        }
        if (!slot) {
            double least = 1e9;
            for (int i = 0; i < kLayers; ++i) {
                Element& l = s.element(Key{"layer" + std::to_string(i)});
                const double w = l.params.get_or<std::string>("clip", "").empty() ? -1.0 : l.params.num("weight");
                if (w < least) least = w, slot = &l;
            }
            slot->params.set("clip", clip).set("phase", 0.0).set("weight", 0.0);
        }
        slot->params.set("to", weight).set("fade", fade).set("speed", ev.args.num("speed", 1.0)).set("loop", ev.args.num("loop", 1.0));
        if (ev.args.num("alone", 1.0) > 0.5)
            for (int i = 0; i < kLayers; ++i) {
                Element& l = s.element(Key{"layer" + std::to_string(i)});
                if (&l != slot) l.params.set("to", 0.0).set("fade", fade);
            }
    });
    loop(Key{"stop"}, self_id(), stop_event(), [](State& s, Element&, Element*, const Event& ev) {
        const std::string clip = ev.args.get_or<std::string>("clip", "");
        for (int i = 0; i < kLayers; ++i) {
            Element& l = s.element(Key{"layer" + std::to_string(i)});
            if (clip.empty() || l.params.get_or<std::string>("clip", "") == clip) l.params.set("to", 0.0).set("fade", ev.args.num("fade", 0.3));
        }
    });
    loop(Key{"reach"}, self_id(), reach_event(), [](State& s, Element&, Element*, const Event& ev) {
        Element* g = s.find(Key{"goal." + ev.args.get_or<std::string>("goal", "")});
        if (!g) return;
        g->params.set(keys::x, ev.args.num(keys::x)).set(keys::y, ev.args.num(keys::y)).set(keys::z, ev.args.num(keys::z));
        g->params.set("to", 1.0).set("fade", ev.args.num("fade", 0.25));
    });
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
    loop(Key{"scale"}, self_id(), scale_event(), [](State&, Element& self, Element*, const Event& ev) {
        self.params.set("scale", std::max(1e-3, ev.args.num("scale", 1.0)));
    });
}

Element& Being::joint(const std::string& name, const std::string& parent, const Vec3d& offset, const Vec3d& rest_deg, double stiffness) {
    Element& j = add_element(Key{name}, kJoint);
    j.params.set("parent_joint", parent).set(keys::x, offset.x).set(keys::y, offset.y).set(keys::z, offset.z);
    j.params.set("rest_yaw", rest_deg.x * kDeg).set("rest_pitch", rest_deg.y * kDeg).set("rest_roll", rest_deg.z * kDeg);
    j.params.set("stiffness", stiffness).set("hold", 0.0).set("hold_to", 0.0);
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

Element& Being::goal(const std::string& name, const std::string& tip, int links) {
    Element& g = add_element(Key{"goal." + name}, kGoal);
    g.params.set("tip", tip).set("links", double(links)).set("weight", 0.0).set("to", 0.0).set("fade", 0.25);
    g.params.set(keys::x, 0.0).set(keys::y, 0.0).set(keys::z, 0.0);
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

void Being::live(double dt) {
    if (dt <= 0) return;  // no time, no change
    Element& self = element(self_id());
    const double t = dt * tempo();
    self.params.set("age", self.params.num("age") + t);

    // What it intends, clip by clip: each layer fades toward its weight, and
    // plays on.
    for (int i = 0; i < kLayers; ++i) {
        Element& l = element(Key{"layer" + std::to_string(i)});
        const std::string c = l.params.get_or<std::string>("clip", "");
        if (c.empty()) continue;
        const double w = l.params.num("weight"), to = l.params.num("to");
        const double nw = w + (to - w) * ease(t, 1.0 / std::max(1e-3, l.params.num("fade", 0.3)));
        l.params.set("weight", nw);
        const Element* ce = find(Key{"clip." + c});
        if (!ce) continue;
        const double len = parsed(*ce).length;
        double ph = l.params.num("phase") + t * l.params.num("speed", 1.0);
        if (len > 0) ph = l.params.num("loop", 1.0) > 0.5 ? std::fmod(ph, len) : std::min(ph, len);
        l.params.set("phase", ph);
        if (to <= 0 && nw < 1e-3) l.params.set("clip", std::string{}).set("weight", 0.0);
    }

    // The joints in order, parents first.
    std::vector<Element*> js;
    for (Element& e : elements())
        if (e.kind == kJoint) js.push_back(&e);
    std::unordered_map<Key, std::size_t> at;
    for (std::size_t i = 0; i < js.size(); ++i) at[js[i]->id] = i;
    std::vector<int> parent(js.size(), -1);
    for (std::size_t i = 0; i < js.size(); ++i) {
        auto it = at.find(Key{js[i]->params.get_or<std::string>("parent_joint", "")});
        if (it != at.end() && it->second < i) parent[i] = int(it->second);
    }

    // Each joint's target: rest, the clips blended over it, what is held.
    std::vector<Q> target(js.size());
    for (std::size_t i = 0; i < js.size(); ++i) {
        Element& j = *js[i];
        const Q rest = rest_of(j);
        Q sum = scaled(rest, 0.0);
        double total = 0;
        // Where it stands on its parent, as the clips that move it say.
        const Vec3d made{j.params.num(keys::x), j.params.num(keys::y), j.params.num(keys::z)};
        Vec3d moved{};
        double moved_w = 0;
        for (int li = 0; li < kLayers; ++li) {
            const Element& l = element(Key{"layer" + std::to_string(li)});
            const std::string c = l.params.get_or<std::string>("clip", "");
            const double w = l.params.num("weight");
            const Element* ce = c.empty() ? nullptr : find(Key{"clip." + c});
            if (!ce || w <= 1e-6) continue;
            const Parsed& p = parsed(*ce);
            auto tr = p.tracks.find(j.id);
            if (tr == p.tracks.end()) continue;
            if (const auto& ms = tr->second.moves; !ms.empty()) {
                const double ph = l.params.num("phase");
                std::size_t k = 0;
                while (k + 1 < ms.size() && ms[k + 1].t <= ph) ++k;
                Vec3d v{ms[k].q[0], ms[k].q[1], ms[k].q[2]};
                if (k + 1 < ms.size() && ms[k + 1].t > ms[k].t) {
                    const double f = (ph - ms[k].t) / (ms[k + 1].t - ms[k].t);
                    v = v + (Vec3d{ms[k + 1].q[0], ms[k + 1].q[1], ms[k + 1].q[2]} - v) * f;
                }
                moved = moved + v * w, moved_w += w;
            }
            if (tr->second.keys.empty()) continue;
            const auto& ks = tr->second.keys;
            const double ph = l.params.num("phase");
            std::size_t k = 0;
            while (k + 1 < ks.size() && ks[k + 1].t <= ph) ++k;
            Q a{ks[k].q[0], ks[k].q[1], ks[k].q[2], ks[k].q[3]}, q = a;
            if (k + 1 < ks.size()) {
                const Q b{ks[k + 1].q[0], ks[k + 1].q[1], ks[k + 1].q[2], ks[k + 1].q[3]};
                const double span = ks[k + 1].t - ks[k].t;
                q = slerp(a, b, span > 0 ? (ph - ks[k].t) / span : 0.0);
            } else if (l.params.num("loop", 1.0) > 0.5 && ks.size() > 1 && p.length > ks[k].t) {
                // Round the loop, back to its first key.
                const Q b{ks[0].q[0], ks[0].q[1], ks[0].q[2], ks[0].q[3]};
                q = slerp(a, b, (ph - ks[k].t) / (p.length - ks[k].t + ks[0].t + 1e-9));
            }
            if (dot(q, rest) < 0) q = scaled(q, -1);
            sum = plus(sum, scaled(q, w));
            total += w;
        }
        target[i] = total > 1e-6 ? norm(plus(sum, scaled(rest, std::max(0.0, 1.0 - total)))) : rest;
        if (moved_w > 1e-6) {
            const Vec3d to = moved + made * std::max(0.0, 1.0 - moved_w);
            j.params.set("tx", to.x).set("ty", to.y).set("tz", to.z);
        } else if (j.params.has(Key{"tx"})) {
            j.params.erase(Key{"tx"}), j.params.erase(Key{"ty"}), j.params.erase(Key{"tz"});
        }
        const double hold = j.params.num("hold") + (j.params.num("hold_to") - j.params.num("hold")) * ease(t, 8.0);
        j.params.set("hold", hold);
        if (hold > 1e-4) target[i] = slerp(target[i], q_of(j, "h"), hold);
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

    // Reaching: each goal's chain turned, joint by joint from the tip up, to
    // bring the tip to the point (cyclic coordinate descent), and the target
    // turned that far toward it by the goal's weight.
    for (Element& g : elements()) {
        if (g.kind != kGoal) continue;
        const double w = g.params.num("weight") + (g.params.num("to") - g.params.num("weight")) * ease(t, 1.0 / std::max(1e-3, g.params.num("fade", 0.25)));
        g.params.set("weight", w);
        if (w < 1e-4) continue;
        auto tip = at.find(Key{g.params.get_or<std::string>("tip", "")});
        if (tip == at.end()) continue;
        std::vector<std::size_t> chain;
        for (int c = parent[tip->second], n = int(g.params.num("links", 2)); c >= 0 && n > 0; c = parent[std::size_t(c)], --n) chain.push_back(std::size_t(c));
        const Vec3d aim{g.params.num(keys::x), g.params.num(keys::y), g.params.num(keys::z)};
        std::vector<Q> work = target;
        std::vector<Frame> f;
        for (int it = 0; it < 12; ++it) {
            for (std::size_t c : chain) {
                fk(work, f);
                const Vec3d to_tip = f[tip->second].p - f[c].p, to_aim = aim - f[c].p;
                const Q d = between(to_tip, to_aim);
                const Q pr = parent[c] >= 0 ? f[std::size_t(parent[c])].r : Q{};
                work[c] = norm(mul(mul(conj(pr), mul(d, pr)), work[c]));
            }
        }
        for (std::size_t c : chain) target[c] = slerp(target[c], work[c], w);
    }

    // The body goes where it is meant to, each joint at its own stiffness.
    for (std::size_t i = 0; i < js.size(); ++i) set_q(*js[i], norm(slerp(q_of(*js[i]), target[i], ease(t, js[i]->params.num("stiffness", 24.0)))));
    resolve();
}

void Being::resolve() {
    // Every joint and part posed in the being's frame from the joints' turns.
    std::unordered_map<Key, Frame> f;
    for (Element& e : elements()) {
        if (e.kind != kJoint) continue;
        const Vec3d off = offset_of(e);
        auto pit = f.find(Key{e.params.get_or<std::string>("parent_joint", "")});
        Frame mine = pit == f.end() ? Frame{off, q_of(e)} : Frame{pit->second.p + rotate(pit->second.r, off), mul(pit->second.r, q_of(e))};
        f[e.id] = mine;
        double y, p, r;
        to_euler(mine.r, y, p, r);
        e.params.set("px", mine.p.x).set("py", mine.p.y).set("pz", mine.p.z).set("pyaw", y).set("ppitch", p).set("proll", r);
        set_q(e, norm(mine.r), "pq");
    }
    for (Element& e : elements()) {
        if (e.kind != kPart) continue;
        auto jt = f.find(Key{e.params.get_or<std::string>("joint", "")});
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

}  // namespace sg
