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
    Q q;
    spatial::to_quat(spatial::from_euler(yaw, pitch, roll), q.w, q.x, q.y, q.z);
    return q;
}
void to_euler(const Q& q, double& yaw, double& pitch, double& roll) {
    spatial::to_euler(spatial::from_quat(q.w, q.x, q.y, q.z), yaw, pitch, roll);
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

const Key kJoint{"joint"}, kPart{"part"}, kClip{"clip"}, kLayer{"layer"}, kGoal{"goal"}, kBlend{"blend"};

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
        .set("age", 0.0)
        .set("lead", 0.0)
        .set("lead_to", 0.0)
        .set("lead_fade", 0.3)
        .set("lived", 0.0);
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
        if (!s.find(Key{"clip." + clip}) && !s.find(Key{"blend." + clip})) return;
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
    loop(Key{"scale"}, self_id(), scale_event(), [](State&, Element& self, Element*, const Event& ev) {
        self.params.set("scale", std::max(1e-3, ev.args.num("scale", 1.0)));
    });
}

Element& Being::joint(const std::string& name, const std::string& parent, const Vec3d& offset, const Vec3d& rest_deg, double stiffness) {
    Element& j = add_element(Key{name}, kJoint);
    j.params.set("parent_joint", parent).set(keys::x, offset.x).set(keys::y, offset.y).set(keys::z, offset.z);
    j.params.set("rest_yaw", rest_deg.x * kDeg).set("rest_pitch", rest_deg.y * kDeg).set("rest_roll", rest_deg.z * kDeg);
    j.params.set("stiffness", stiffness).set("hold", 0.0).set("hold_to", 0.0).set("lead_at", 0.0).set("lead_moves", 0.0);
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
    self.params.set("lived", 1.0);
    const double t = dt * tempo();
    self.params.set("age", self.params.num("age") + t);

    // What it intends, clip by clip: each layer fades toward its weight, and
    // plays on. A layer that plays a blend plays its clips together, each by
    // its weight at the blend's point, in step: one phase (0..1) for them
    // all, gone round in the time their lengths, so weighted, take - a walk
    // and a run blended put their feet down together.
    struct Sample {
        const Parsed* clip;
        double phase, weight;
        bool loop;
    };
    std::vector<Sample> samples;
    for (int i = 0; i < kLayers; ++i) {
        Element& l = element(Key{"layer" + std::to_string(i)});
        const std::string c = l.params.get_or<std::string>("clip", "");
        if (c.empty()) continue;
        const double w = l.params.num("weight"), to = l.params.num("to");
        const double nw = w + (to - w) * ease(t, 1.0 / std::max(1e-3, l.params.num("fade", 0.3)));
        l.params.set("weight", nw);
        const bool loops = l.params.num("loop", 1.0) > 0.5;
        if (const Element* ce = find(Key{"clip." + c})) {
            const double len = parsed(*ce).length;
            double ph = l.params.num("phase") + t * l.params.num("speed", 1.0);
            if (len > 0) ph = loops ? std::fmod(ph, len) : std::min(ph, len);
            l.params.set("phase", ph);
            samples.push_back({&parsed(*ce), ph, nw, loops});
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
            double ph = l.params.num("phase") + (length > 1e-6 ? t * l.params.num("speed", 1.0) / length : 0.0);
            ph = loops ? ph - std::floor(ph) : std::min(ph, 1.0);
            l.params.set("phase", ph);
            for (const auto& [p, bw] : parts) samples.push_back({p, ph * p->length, nw * bw, true});
        }
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
        for (const Sample& sm : samples) {
            if (sm.weight <= 1e-6) continue;
            const Parsed& p = *sm.clip;
            auto tr = p.tracks.find(j.id);
            if (tr == p.tracks.end()) continue;
            const double ph = sm.phase, w = sm.weight;
            if (const auto& ms = tr->second.moves; !ms.empty()) {
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
            std::size_t k = 0;
            while (k + 1 < ks.size() && ks[k + 1].t <= ph) ++k;
            Q a{ks[k].q[0], ks[k].q[1], ks[k].q[2], ks[k].q[3]}, q = a;
            if (k + 1 < ks.size()) {
                const Q b{ks[k + 1].q[0], ks[k + 1].q[1], ks[k + 1].q[2], ks[k + 1].q[3]};
                const double span = ks[k + 1].t - ks[k].t;
                q = slerp(a, b, span > 0 ? (ph - ks[k].t) / span : 0.0);
            } else if (sm.loop && ks.size() > 1 && p.length > ks[k].t) {
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

    // What it means to do, in its own frame - each joint's turn and place as
    // its clips and holds say, before anything leads it: what whoever moves
    // it from outside (a ragdoll's muscles) aims for.
    {
        std::vector<Frame> meant;
        fk(target, meant);
        for (std::size_t i = 0; i < js.size(); ++i) {
            set_q(*js[i], meant[i].r, "aq");
            js[i]->params.set("ax", meant[i].p.x).set("ay", meant[i].p.y).set("az", meant[i].p.z);
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

    // The body goes where it is meant to, each joint at its own stiffness -
    // and wholly as far as it is led: what leads it has dynamics of its own.
    for (std::size_t i = 0; i < js.size(); ++i)
        set_q(*js[i], norm(slerp(q_of(*js[i]), target[i], std::max(led[i], ease(t, js[i]->params.num("stiffness", 24.0))))));
    resolve();
}

void Being::resolve() {
    const bool lived = element(self_id()).params.num("lived") > 0.5;
    // Every joint and part posed in the being's frame from the joints' turns;
    // and each joint as it is bound, at rest (`bq`): the frame a motion is
    // carried in from one body to another.
    std::unordered_map<Key, Frame> f;
    std::unordered_map<Key, Q> bind;
    for (Element& e : elements()) {
        if (e.kind != kJoint) continue;
        const Vec3d off = offset_of(e);
        const Key up{e.params.get_or<std::string>("parent_joint", "")};
        auto pit = f.find(up);
        Frame mine = pit == f.end() ? Frame{off, q_of(e)} : Frame{pit->second.p + rotate(pit->second.r, off), mul(pit->second.r, q_of(e))};
        f[e.id] = mine;
        auto bit = bind.find(up);
        const Q b = norm(bit == bind.end() ? rest_of(e) : mul(bit->second, rest_of(e)));
        bind[e.id] = b;
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

Key retarget(StateGraph& g, const Being& from, const Being& to, Key name) {
    if (name.empty()) name = Key{from.id().str() + ".to." + to.id().str()};
    Functor f(name, from.id(), to.id());
    for (const auto& [a, b] : same_joints(from, to))
        f.on_object(a, b, [](const Element& src, Element& dst) {
            // The source's turn away from its own bind, put on the target's
            // bind: lead = pq_src * bq_src^-1 * bq_dst. Each body keeps its
            // own bones' axes; only the motion crosses.
            const Q w = mul(mul(q_of(src, "pq"), conj(q_of(src, "bq"))), q_of(dst, "bq"));
            set_q(dst, norm(w), "lead_q");
            dst.params.set("lead_at", 1.0);
            // A joint that travels (the hips) travels as far for its size:
            // by how far each stands from its parent as made.
            if (src.params.has(Key{"tx"})) {
                const auto len = [](const Element& e) {
                    const double x = e.params.num(keys::x), y = e.params.num(keys::y), z = e.params.num(keys::z);
                    return std::sqrt(x * x + y * y + z * z);
                };
                const double ls = len(src), k = ls > 1e-6 ? len(dst) / ls : 1.0;
                dst.params.set("lead_tx", src.params.num("tx") * k).set("lead_ty", src.params.num("ty") * k).set("lead_tz", src.params.num("tz") * k);
                dst.params.set("lead_moves", 1.0);
            } else {
                dst.params.set("lead_moves", 0.0);
            }
        });
    g.set_functor(std::move(f));
    g.keep(name);
    return name;
}

}  // namespace sg
