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

// The smallest turn taking direction a to direction b.
M3 turn_between(V3 a, V3 b) {
    a = spatial::normalize(a), b = spatial::normalize(b);
    const V3 k = spatial::cross(a, b);
    const double s = spatial::length(k), c = spatial::dot(a, b);
    if (s < 1e-9) {
        if (c > 0) return M3{};
        const V3 o = std::fabs(a.x) < 0.9 ? V3{1, 0, 0} : V3{0, 0, 1};
        return spatial::axis_angle(spatial::normalize(spatial::cross(a, o)), 3.14159265358979);
    }
    return spatial::axis_angle(k * (1.0 / s), std::atan2(s, c));
}

// --- the ground plan: points across the floor (x, z) --------------------------------
struct P2 {
    double x, z;
};
P2 operator-(P2 a, P2 b) { return {a.x - b.x, a.z - b.z}; }
P2 operator+(P2 a, P2 b) { return {a.x + b.x, a.z + b.z}; }
P2 operator*(P2 a, double k) { return {a.x * k, a.z * k}; }
double len(P2 a) { return std::sqrt(a.x * a.x + a.z * a.z); }
double cross2(P2 o, P2 a, P2 b) { return (a.x - o.x) * (b.z - o.z) - (a.z - o.z) * (b.x - o.x); }
// What the feet stand on: the outline round their soles.
std::vector<P2> hull2(std::vector<P2> pts) {
    std::sort(pts.begin(), pts.end(), [](P2 a, P2 b) { return a.x < b.x || (a.x == b.x && a.z < b.z); });
    if (pts.size() < 3) return pts;
    std::vector<P2> h(pts.size() * 2);
    std::size_t k = 0;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        while (k >= 2 && cross2(h[k - 2], h[k - 1], pts[i]) <= 0) --k;
        h[k++] = pts[i];
    }
    for (std::size_t i = pts.size() - 1, t = k + 1; i > 0; --i) {
        while (k >= t && cross2(h[k - 2], h[k - 1], pts[i - 1]) <= 0) --k;
        h[k++] = pts[i - 1];
    }
    h.resize(k - 1);
    return h;
}
// The point of the outline (or within it) nearest p, and how far outside p is.
P2 within(const std::vector<P2>& h, P2 p, double* outside = nullptr) {
    if (outside) *outside = 0;
    if (h.empty()) return p;
    bool in = h.size() >= 3;
    for (std::size_t i = 0; i < h.size() && in; ++i)
        if (cross2(h[i], h[(i + 1) % h.size()], p) < 0) in = false;
    if (in) return p;
    P2 best = h[0];
    double least = 1e18;
    for (std::size_t i = 0; i < h.size(); ++i) {
        const P2 a = h[i], b = h[(i + 1) % h.size()], ab = b - a;
        const double l2 = ab.x * ab.x + ab.z * ab.z;
        const double t = l2 > 1e-12 ? std::clamp(((p.x - a.x) * ab.x + (p.z - a.z) * ab.z) / l2, 0.0, 1.0) : 0.0;
        const P2 q = a + ab * t;
        if (len(p - q) < least) least = len(p - q), best = q;
    }
    if (outside) *outside = least;
    return best;
}

// A leg bent to put its ankle at `to`: its hip at `hip`, its knee and ankle
// at rest at `knee0`, `ankle0` (the knee bends the way it does at rest), its
// thigh and shin turned `T0`, `S0` at rest. Its thigh's and shin's turns.
void two_bone(V3 hip, V3 knee0, V3 ankle0, V3 to, const M3& T0, const M3& S0, M3& T, M3& S) {
    const double l1 = spatial::length(knee0 - hip), l2 = spatial::length(ankle0 - knee0);
    V3 d = to - hip;
    const double dl = std::clamp(spatial::length(d), std::fabs(l1 - l2) + 1e-4, l1 + l2 - 1e-4);
    const V3 dh = spatial::normalize(d);
    V3 pole = (knee0 - hip) - dh * spatial::dot(knee0 - hip, dh);
    if (spatial::length(pole) < 1e-6) pole = spatial::cross(dh, std::fabs(dh.y) < 0.9 ? V3{0, 1, 0} : V3{1, 0, 0});
    const double a = (l1 * l1 - l2 * l2 + dl * dl) / (2 * dl), hh = std::sqrt(std::max(0.0, l1 * l1 - a * a));
    const V3 knee = hip + dh * a + spatial::normalize(pole) * hh;
    const M3 rt = turn_between(knee0 - hip, knee - hip);
    T = rt * T0;
    const V3 ankle_now = knee + rt * (ankle0 - knee0);
    S = turn_between(ankle_now - knee, (hip + dh * dl) - knee) * rt * S0;
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
        .set("stepping", 0.0)
        .set("full", 0.0)
        .set("floor", 0.0)
        .set("damping", 1.0)
        .set("hold", 600.0)
        .set("lead", 0.0)
        .set("ox", 0.0).set("oy", 0.0).set("oz", 0.0).set("oyaw", 0.0)
        .set("grab", std::string{})
        .set("grab_x", 0.0).set("grab_y", 0.0).set("grab_z", 0.0)
        .set("grab_force", 500.0)
        .set("anchor", -1.0)
        .set("grab_at", 0.0).set("grab_lx", 0.0).set("grab_ly", 0.0).set("grab_lz", 0.0);

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
    // The spine: up from the root, bone by bone, as far as it goes up (the
    // neck and head at its top); everything else not a leg is an arm.
    std::vector<bool> spine(js.size(), false), neck(js.size(), false);
    for (std::size_t i = 0; i < js.size(); ++i) {
        if (parent[i] < 0 || leg[i]) continue;
        const std::size_t up = std::size_t(parent[i]);
        if ((parent[up] < 0 || spine[up]) && spatial::normalize(p[i] - p[up]).y > 0.5) spine[i] = true;
    }
    for (std::size_t i = 0; i < js.size(); ++i)
        if (spine[i] && parent[i] >= 0 && spine[std::size_t(parent[i])] && kids[i].size() <= 1) neck[i] = true;
    // Two legs: a thigh from the root, its shin, its foot - what it balances on.
    std::vector<std::size_t> thighs;
    for (std::size_t i = 0; i < js.size(); ++i)
        if (parent[i] >= 0 && parent[std::size_t(parent[i])] < 0 && leg[i] && !kids[i].empty() && !kids[kids[i][0]].empty()) thighs.push_back(i);
    // What the being shows on each joint, if it shows parts: a bone is as big
    // as they are.
    std::unordered_map<Key, std::pair<V3, V3>> shown;
    for (Key part : body.parts()) {
        const Element& e = body.element(part);
        const Key on{e.params.get_or<std::string>("joint", "")};
        const V3 mid{e.params.num("ox"), e.params.num("oy"), e.params.num("oz")};
        const V3 half{e.params.num(keys::sx) * 0.5, e.params.num(keys::sy) * 0.5, e.params.num(keys::sz) * 0.5};
        auto [it, fresh] = shown.try_emplace(on, mid - half, mid + half);
        if (!fresh)
            it->second = {{std::min(it->second.first.x, mid.x - half.x), std::min(it->second.first.y, mid.y - half.y), std::min(it->second.first.z, mid.z - half.z)},
                          {std::max(it->second.second.x, mid.x + half.x), std::max(it->second.second.y, mid.y + half.y), std::max(it->second.second.z, mid.z + half.z)}};
    }
    // A box given in the being's frame about joint i, as a block in the
    // bone's own frame (its corners turned in, boxed again).
    const auto block_from = [&](std::size_t i, V3 lo, V3 hi, V3& out_lo, V3& out_hi) {
        out_lo = {1e9, 1e9, 1e9}, out_hi = {-1e9, -1e9, -1e9};
        for (int c = 0; c < 8; ++c) {
            const V3 w{c & 1 ? hi.x : lo.x, c & 2 ? hi.y : lo.y, c & 4 ? hi.z : lo.z};
            const V3 l = spatial::transpose(bind[i]) * w;
            out_lo = {std::min(out_lo.x, l.x), std::min(out_lo.y, l.y), std::min(out_lo.z, l.z)};
            out_hi = {std::max(out_hi.x, l.x), std::max(out_hi.y, l.y), std::max(out_hi.z, l.z)};
        }
    };
    std::vector<double> raw(js.size());
    double total = 0;
    for (std::size_t i = 0; i < js.size(); ++i) {
        Element& b = add_element(js[i], kBone);
        const Element& j = body.element(js[i]);
        b.params.set("parent", parent[i] < 0 ? std::string{} : js[std::size_t(parent[i])].str());
        // How strong its muscles are (rad/s): the legs that carry it most,
        // the spine less, the neck and head less, the arms least.
        b.params.set("leg", leg[i] ? 1.0 : 0.0).set("omega", leg[i] ? 28.0 : spine[i] ? (kids[i].empty() || neck[i] ? 16.0 : 24.0) : 16.0);
        b.params.set("cone", 1.9).set("weak", 1.0).set("weak_rate", 0.8);
        set_vec(b, made[i], "jx", "jy", "jz");
        double vol;
        const V3 from = parent[i] < 0 ? V3{0, 1, 0} : spatial::normalize(p[i] - p[std::size_t(parent[i])]);
        V3 lo, hi;
        bool block = true;
        if (auto it = shown.find(js[i]); it != shown.end()) {
            lo = it->second.first, hi = it->second.second;  // (already in the joint's frame)
            if (kids[i].empty() && leg[i]) {
                // A foot shown a little into the floor stands on it: its
                // solid stops there.
                V3 wlo{1e9, 1e9, 1e9}, whi{-1e9, -1e9, -1e9};
                for (int c = 0; c < 8; ++c) {
                    const V3 w = bind[i] * V3{c & 1 ? hi.x : lo.x, c & 2 ? hi.y : lo.y, c & 4 ? hi.z : lo.z};
                    wlo = {std::min(wlo.x, w.x), std::min(wlo.y, w.y), std::min(wlo.z, w.z)};
                    whi = {std::max(whi.x, w.x), std::max(whi.y, w.y), std::max(whi.z, w.z)};
                }
                wlo.y = std::max(wlo.y, 0.003 - p[i].y);
                block_from(i, wlo, whi, lo, hi);
            }
        } else if (kids[i].empty() && leg[i] && from.y < -0.5) {
            // A foot: on along the ground (the way a pose faces, x), down to
            // the floor from the ankle.
            const double l = std::clamp(0.6 * spatial::length(made[i]), 0.12, 0.26), h = std::max(0.03, p[i].y - 0.005);
            block_from(i, {-0.25 * l, -h, -0.045}, {l, 0.03, 0.045}, lo, hi);
        } else if (kids[i].empty() && from.y > 0.5) {
            // A head: round, on top of where it is borne.
            const double r = std::clamp(0.9 * spatial::length(made[i]), 0.08, 0.12);
            block_from(i, {-r, 0.0, -r}, {r, 2 * r, r}, lo, hi);
        } else {
            block = false;
        }
        if (block) {
            b.params.set("block", 1.0);
            set_vec(b, lo, "lox", "loy", "loz");
            set_vec(b, hi, "hix", "hiy", "hiz");
            const V3 d = hi - lo;
            vol = d.x * d.y * d.z;
        } else if (kids[i].size() >= 2) {
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
            // (A neck bearing a head: the head's own bone is round.)
            const bool crown = kids[i].size() == 1 && kids[kids[i][0]].empty() && spatial::normalize(p[kids[i][0]] - p[i]).y > 0.5 && !leg[i];
            const double l = spatial::length(end), r = crown ? std::clamp(0.45 * l, 0.03, 0.11) : std::clamp(0.2 * l, 0.025, 0.09);
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
    // A bone too small to matter (a finger's joints, the end of a toe or a
    // crown: under 4.5% of the body's height, and showing nothing) has no body
    // of its own: it rides its parent as the being moves it, and so does all
    // on it. However many fingers a skeleton has, they cost nothing and
    // cannot shake.
    double top = -1e9, bottom = 1e9;
    for (const V3& q : p) top = std::max(top, q.y), bottom = std::min(bottom, q.y);
    const double tall = std::max(0.1, top - bottom);
    // (Small, and all on it small - a finger, not a neck that bears a head;
    // or an end that only marks where its parent's bone ends, showing nothing
    // of its own - `HeadTop_End`, `Toe_End`.)
    std::vector<bool> tiny(js.size(), false), rides(js.size(), false);
    for (std::size_t i = 0; i < js.size(); ++i) {
        const Element& b = element(js[i]);
        tiny[i] = parent[i] >= 0 && b.params.num("block") < 0.5 && spatial::length(vec(b, "ex", "ey", "ez")) < 0.045 * tall;
    }
    for (std::size_t i = js.size(); i-- > 0;) {
        if (parent[i] < 0) continue;
        bool all = tiny[i];
        for (std::size_t k : kids[i]) all = all && rides[k];
        // (An end its skin binds nothing to only marks a place.)
        const Element& j = body.element(js[i]);
        const bool end = kids[i].empty() && j.params.has(Key{"bound"}) && j.params.num("bound") < 0.5;
        rides[i] = all || end;
    }
    for (std::size_t i = 0; i < js.size(); ++i)
        if (rides[i]) raw[i] = 0;
    total = 0;
    for (double r : raw) total += r;
    // A person's weight, where the body is one (two legs, a spine): shared as
    // a person's is - pelvis 14.2%, thigh 10, shin 4.65, foot 1.45, a toe
    // 0.3, shoulder 1, upper arm 2.8, forearm 1.6, hand 0.6, neck 1, head
    // 7.1, the trunk the rest by size - however big its parts are drawn (a
    // zebra's head is not its body's weight). Any other body by size.
    std::vector<double> share(js.size(), -1.0);
    if (thighs.size() == 2) {
        const auto chain = [&](std::size_t from, std::initializer_list<double> parts) {
            std::size_t at = from;
            for (double part : parts) {
                if (rides[at]) break;
                share[at] = part;
                std::size_t next = js.size();
                for (std::size_t k : kids[at])
                    if (!rides[k]) next = k;
                if (next == js.size()) break;
                at = next;
            }
        };
        for (std::size_t t : thighs) chain(t, {10.0, 4.65, 1.45, 0.3});
        for (std::size_t i = 0; i < js.size(); ++i) {
            if (parent[i] < 0) share[i] = 14.2;
            // An arm: what leaves the spine and is not the spine.
            if (parent[i] >= 0 && spine[std::size_t(parent[i])] && !spine[i] && !leg[i] && !rides[i]) {
                std::size_t n = 0;  // (how many bones the arm is: with a shoulder, or not)
                for (std::size_t at = i; !rides[at]; ++n) {
                    std::size_t next = js.size();
                    for (std::size_t k : kids[at])
                        if (!rides[k]) next = k;
                    if (next == js.size()) {
                        ++n;
                        break;
                    }
                    at = next;
                }
                if (n >= 4) chain(i, {1.0, 2.8, 1.6, 0.6});
                else chain(i, {2.8, 1.6, 0.6});
            }
            if (spine[i] && neck[i] && !rides[i]) {
                // The top of the spine: the last bone a head, the one under it a neck.
                bool top = true;
                for (std::size_t k : kids[i])
                    if (!rides[k] && spine[k]) top = false;
                share[i] = top ? 7.1 : 1.0;
            }
        }
        double given = 0, trunk = 0;
        for (std::size_t i = 0; i < js.size(); ++i)
            if (share[i] >= 0) given += share[i];
            else if (!rides[i]) trunk += raw[i];
        for (std::size_t i = 0; i < js.size(); ++i)
            if (share[i] < 0 && !rides[i]) share[i] = std::max(0.0, 100.0 - given) * raw[i] / std::max(trunk, 1e-9);
        double sum = 0;
        for (double v : share) sum += std::max(0.0, v);
        for (std::size_t i = 0; i < js.size(); ++i) raw[i] = std::max(0.0, share[i]) / std::max(sum, 1e-9);
        total = 1.0;
    }
    // Neighbours no more than ten times each other: a light hand on a heavy
    // arm shakes.
    for (std::size_t i = 0; i < js.size(); ++i) {
        double m = mass * raw[i] / std::max(total, 1e-9);
        if (parent[i] >= 0 && !rides[i]) m = std::max(m, element(js[std::size_t(parent[i])]).params.num("mass") * 0.1);
        element(js[i]).params.set("mass", rides[i] ? 0.0 : std::max(m, 0.05)).set("rides", rides[i] ? 1.0 : 0.0);
    }
    for (std::size_t i = js.size(); i-- > 0;) {
        // (And a light bone between heavy ones is no lighter than a tenth of
        // what it bears: a thin spine under a chest.)
        if (rides[i]) continue;
        double most = 0;
        for (std::size_t k : kids[i])
            if (!rides[k]) most = std::max(most, element(js[k]).params.num("mass"));
        Element& b = element(js[i]);
        b.params.set("mass", std::max(b.params.num("mass"), most * 0.1));
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
        own[i] = rides[i] ? 0.0 : 3.0 / std::max(1e-9, inv(0, 0) + inv(1, 1) + inv(2, 2));  // (what rides weighs nothing)
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
            if (rides[i]) continue;
            double most = 0;
            if (parent[i] >= 0) most = std::max(most, stiff[std::size_t(parent[i])]);
            for (std::size_t k : kids[i])
                if (!rides[k]) most = std::max(most, stiff[k]);
            stiff[i] = std::max(stiff[i], most * 0.1);
        }
    for (std::size_t i = 0; i < js.size(); ++i) {
        // What the solver's spring turns: the bone against its parent (a
        // root stands fixed), as against all that hangs from it.
        if (rides[i]) {
            element(js[i]).params.set("armature", 0.0).set("gain", 1.0).set("heavy", 1.0);
            continue;
        }
        const double pair = parent[i] < 0 ? stiff[i] : 1.0 / (1.0 / stiff[i] + 1.0 / stiff[std::size_t(parent[i])]);
        // (No stiffer than its steps can carry: a spring of more than a
        // fifth of a turn a substep shivers instead of holding.)
        // (`heavy`: how much heavier to turn all it moves is than the solver
        // sees - the muscle's damping is sized by it, whole, so it settles
        // without rocking even where its stiffness is capped.)
        const double heavy = std::sqrt(std::max(1.0, sub[i] / pair));
        element(js[i]).params.set("armature", stiff[i] - own[i]).set("gain", std::clamp(heavy, 1.0, 8.0)).set("heavy", heavy);
    }

    // Its balance: what it stands on (two feet), the sway of its hips over
    // them and how fast, where each foot is planted, the step it is taking.
    {
        Element& self = element(self_id());
        std::size_t root = 0;
        while (root < js.size() && parent[root] >= 0) ++root;
        self.params.set("root", root < js.size() ? js[root].str() : std::string{});
        self.params.set("balance", thighs.size() == 2 ? 1.0 : 0.0);
        for (std::size_t f = 0; f < thighs.size() && f < 2; ++f) {
            const std::size_t t = thighs[f], sh = kids[t][0], ft = kids[sh][0];
            const std::string n = std::to_string(f);
            self.params.set("thigh" + n, js[t].str()).set("shin" + n, js[sh].str()).set("foot" + n, js[ft].str());
            self.params.set("plant" + n + "x", p[ft].x).set("plant" + n + "z", p[ft].z);
        }
        self.params.set("sway_x", 0.0).set("sway_z", 0.0).set("sway_vx", 0.0).set("sway_vz", 0.0).set("push_x", 0.0).set("push_z", 0.0);
        self.params.set("swing", -1.0).set("swing_t", 0.0).set("steps", 0.0).set("stumble", 0.0).set("fallen", 0.0).set("lie", 0.0).set("rising", 0.0);
        self.params.set("from_x", 0.0).set("from_z", 0.0).set("to_x", 0.0).set("to_z", 0.0).set("catching", 0.0).set("going", -1.0);
    }

    //   self --step--> self: the body moved for `dt` - asleep, where it is
    //   meant to be; awake, as it is pushed, pulled and moved by its muscles.
    loop(Key{"step"}, self_id(), step_event(), [](State& s, Element&, Element*, const Event& ev) { static_cast<Ragdoll&>(s).step(ev.args.num(keys::dt)); });
    //   self --hit--> self: an impulse on a bone; it and its neighbours weakened.
    loop(Key{"hit"}, self_id(), hit_event(), [](State& s, Element& self, Element*, const Event& ev) {
        Element* b = s.find(Key{ev.args.get_or<std::string>("bone", "")});
        if (!b || b->kind != kBone) return;
        const V3 j{ev.args.num(keys::x), ev.args.num(keys::y), ev.args.num(keys::z)};
        // The whole body takes what it was hit with (its sway, below, and
        // every bone a share); the bone hit a kick of its own besides, no
        // faster than a limb is thrown - it goes down where it stands, it
        // does not fly.
        double mass = 0;
        for (const Element& n : s.elements())
            if (n.kind == kBone) mass += n.params.num("mass", 1.0);
        // (Standing firm, its feet hold its hips and legs: the body above
        // them takes it, bends, and comes back up.)
        const bool firm = static_cast<const Ragdoll&>(s).firm();
        for (Element& n : s.elements())
            if (n.kind == kBone && !(firm && (n.params.num("leg") > 0.5 || n.params.get_or<std::string>("parent", "").empty()))) set_vec(n, vec(n, "vx", "vy", "vz") + j * (1.0 / std::max(1.0, mass)), "vx", "vy", "vz");
        V3 kick = j * (0.35 / b->params.num("mass", 1.0));
        if (spatial::length(kick) > 3.0) kick = kick * (3.0 / spatial::length(kick));
        set_vec(*b, vec(*b, "vx", "vy", "vz") + kick, "vx", "vy", "vz");
        // (A firm body is jolted, not unstrung: the bone hit loses at most
        // four tenths of its strength, its neighbours half that, back in half
        // a second.)
        const double weaken = std::clamp(ev.args.num("weaken", 0.6), 0.0, 1.0), rate = 1.0 / std::max(0.05, ev.args.num("for", 0.5));
        for (Element& n : s.elements()) {
            if (n.kind != kBone) continue;
            const bool near = &n == b || n.id.str() == b->params.get_or<std::string>("parent", "") || n.params.get_or<std::string>("parent", "") == b->id.str();
            if (near) n.params.set("weak", std::max(0.02, std::min(n.params.num("weak", 1.0), &n == b ? weaken : 0.5 + 0.5 * weaken))).set("weak_rate", rate);
        }
        // What it was hit with moves the whole of it: its sway takes it.
        self.params.set("push_x", self.params.num("push_x") + j.x).set("push_z", self.params.num("push_z") + j.z);
        // The whole body staggers as hard as it was hit - no more for being
        // hit again (blows close together do not add up to limp); past
        // `fall_at`, unless it stands firm, all its strength goes.
        const double how = spatial::length(j) / std::max(1.0, self.params.num("fall_at", 150.0));
        self.params.set("strength", std::min(self.params.num("strength", 1.0), how >= 1.0 && !firm ? 0.0 : std::max(0.6, 1.0 - 0.8 * how)));
        self.params.set("awake", 1.0).set("still", 0.0);
    });
    //   self --grab--> self: a hand holding a bone, pulling it toward a point.
    loop(Key{"grab"}, self_id(), grab_event(), [](State& s, Element& self, Element*, const Event& ev) {
        const std::string bone = ev.args.get_or<std::string>("bone", "");
        const Element* b = s.find(Key{bone});
        if (!b || b->kind != kBone) return;
        self.params.set("grab", bone).set("grab_x", ev.args.num(keys::x)).set("grab_y", ev.args.num(keys::y)).set("grab_z", ev.args.num(keys::z));
        // Where on it (the being's frame), if said: kept in the bone's own
        // frame, so it is held there however it turns. Else by its middle.
        if (ev.args.has(Key{"ax"})) {
            const V3 at{ev.args.num("ax"), ev.args.num("ay"), ev.args.num("az")};
            set_vec(self, spatial::transpose(turn_of(*b, "q")) * (at - vec(*b, "x", "y", "z")), "grab_lx", "grab_ly", "grab_lz");
            self.params.set("grab_at", 1.0);
        } else if (self.params.get_or<std::string>("grab", "") != bone || ev.args.num("fresh") > 0.5) {
            self.params.set("grab_at", 0.0);
        }
        self.params.set("grab_force", ev.args.num("force", 500.0)).set("awake", 1.0).set("still", 0.0);
    });
    //   self --anchor--> self: how far it may be moved from its place.
    loop(Key{"anchor"}, self_id(), anchor_event(), [](State&, Element& self, Element*, const Event& ev) {
        self.params.set("anchor", ev.args.num("reach", -1.0));
    });
    loop(Key{"let_go"}, self_id(), let_go_event(), [](State&, Element& self, Element*, const Event&) { self.params.set("grab", std::string{}).set("grab_at", 0.0); });
}

rigid::Hull Ragdoll::hull(const Element& bone) { return shape_of(bone); }

rigid::Body Ragdoll::body(const Element& bone, const std::string& id, int group) {
    rigid::Body b;
    b.id = id;
    b.hulls.push_back(shape_of(bone));
    b.friction = 0.6, b.restitution = 0.1;
    b.group = group;
    b.set_mass(std::max(0.5, bone.params.num("mass", 1.0)));
    return b;
}

void Ragdoll::pose_in(const Element& bone, const V3& origin, double yaw, V3& x, M3& r) {
    const M3 o = spatial::from_euler(yaw, 0, 0);
    x = origin + o * vec(bone, "x", "y", "z");
    r = o * turn_of(bone, "q");
}

Key Ragdoll::nearest(const V3& p) const {
    // (By the middle of its solid as it is turned now, not its joint: a hand
    // on the shin is on the shin, not the knee.)
    Key best;
    double least = 1e18;
    for (const Element& b : elements()) {
        if (b.kind != kBone || b.params.num("rides") > 0.5) continue;
        const rigid::Hull h = shape_of(b);
        const V3 mid = vec(b, "x", "y", "z") + turn_of(b, "q") * h.centre;
        const double d = spatial::length(mid - p);
        if (d < least) least = d, best = b.id;
    }
    return best;
}

std::vector<std::string> Ragdoll::faults() const {
    std::vector<std::string> out;
    for (const Element& b : elements()) {
        if (b.kind != kBone || b.params.num("rides") > 0.5) continue;
        const Element* up = find(Key{b.params.get_or<std::string>("parent", "")});
        if (!up) continue;
        const V3 bone = vec(b, "jx", "jy", "jz");
        const V3 want = vec(*up, "tx", "ty", "tz") + turn_of(*up, "aim") * bone;
        const double off = spatial::length(vec(b, "tx", "ty", "tz") - want);
        if (off > 0.02 + 0.1 * spatial::length(bone)) {
            char t[200];
            std::snprintf(t, sizeof t, "bone %s is meant %.3f m from where its own bone reaches - its targets are of a body it is not", b.id.str().c_str(), off);
            out.push_back(t);
            break;
        }
    }
    return out;
}

std::vector<Key> Ragdoll::bones() const {
    std::vector<Key> out;
    for (const Element& e : elements())
        if (e.kind == kBone) out.push_back(e.id);
    return out;
}

bool Ragdoll::disturbed() const {
    // Held, or a bone that has weight where it is meant to be meets a solid.
    // (Not the floor: a bone the being means at or under the floor is its own
    // pose - a foot planted, a bind pose before its clip - and waking cannot
    // mend it.)
    const Element& self = element(self_id());
    if (!self.params.get_or<std::string>("grab", "").empty()) return true;
    const bool full = self.params.num("full") > 0.5;
    std::vector<Box> solids;
    for (const Element& c : elements())
        if (c.kind == kCollider && c.alive) solids.push_back(box_in(c, self));
    for (const Element& b : elements()) {
        if (b.kind != kBone || b.params.num("rides") > 0.5 || (!full && b.params.get_or<std::string>("parent", "").empty())) continue;
        const V3 at = vec(b, "tx", "ty", "tz");
        const M3 t = turn_of(b, "aim");
        const bool block = b.params.num("block") > 0.5;
        const double r = block ? 0.5 * spatial::length(vec(b, "hix", "hiy", "hiz") - vec(b, "lox", "loy", "loz")) : b.params.num("radius");
        const V3 end = at + t * (block ? (vec(b, "hix", "hiy", "hiz") + vec(b, "lox", "loy", "loz")) * 0.5 : vec(b, "ex", "ey", "ez"));
        for (V3 q : {at, end}) {
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
    // (No higher than the pose the being means: an animation that sinks a foot
    // a little into the floor is its own; a floor above it would push the
    // foot off where it is meant to be, and the body would never come home.)
    double ground = self.params.num("floor");
    for (const Element& b : elements()) {
        if (b.kind != kBone || b.params.num("rides") > 0.5) continue;
        const V3 at = vec(b, "tx", "ty", "tz");
        const M3 t = turn_of(b, "aim");
        for (const V3& c : shape_of(b).v) ground = std::min(ground, (at + t * c).y - 0.003);
    }
    floor.hulls.push_back(rigid::Hull::box({0, ground - 0.5, 0}, {200, 0.5, 200}));
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
        if (b.kind != kBone || b.params.num("rides") > 0.5) continue;
        rigid::Body body;
        body.id = b.id.str();
        body.hulls.push_back(shape_of(b));
        body.group = 1;
        body.friction = 0.8, body.restitution = 0.05;
        body.set_mass(b.params.num("mass", 1.0));
        {
            // Its armature, and its turning rounded out: a thin bone is a
            // hundred times easier to twist about its length than to swing,
            // and a muscle's push would spin it there - so every way it turns
            // is given a quarter of how hard it turns on the whole.
            M3 i = spatial::inverse(body.inv_inertia_local);
            const double round = 0.25 * (i(0, 0) + i(1, 1) + i(2, 2)) / 3.0 + std::max(0.0, b.params.num("armature"));
            i(0, 0) += round, i(1, 1) += round, i(2, 2) += round;
            body.inv_inertia_local = spatial::inverse(i);
        }
        w.add(body);
    }
    for (const Element& b : elements()) {
        const std::string up = b.params.get_or<std::string>("parent", "");
        if (b.kind != kBone || up.empty() || b.params.num("rides") > 0.5) continue;
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
    // Fallen, its strength ebbs (half a second); else it comes back.
    if (self.params.num("fallen") > 0.5) self.params.set("strength", strength * std::exp(-0.69 * dt));
    else self.params.set("strength", strength + (1.0 - strength) * ease(dt, self.params.num("recover", 0.6)));
    for (Element& b : elements())
        if (b.kind == kBone) b.params.set("weak", b.params.num("weak", 1.0) + (1.0 - b.params.num("weak", 1.0)) * ease(dt, b.params.num("weak_rate", 1.25)));

    if (self.params.num("awake") < 0.5) {
        // Asleep: each bone where the being means it, at the speed it got
        // there - so, woken, it goes on as it was going.
        for (Element& b : elements()) {
            if (b.kind != kBone) continue;
            const V3 x = vec(b, "x", "y", "z"), t = vec(b, "tx", "ty", "tz");
            const M3 q = turn_of(b, "q"), tq = turn_of(b, "aim");
            // (Faster than a body moves - a pose put, not moved into: a clip
            // begun, a bind pose left - it was put there, and is still.)
            const V3 v = (t - x) * (1.0 / dt), w = spatial::log_map(tq * spatial::transpose(q)) * (1.0 / dt);
            const bool put = spatial::length(v) > 6.0 || spatial::length(w) > 20.0;
            set_vec(b, put ? V3{} : v, "vx", "vy", "vz");
            set_vec(b, put ? V3{} : w, "wx", "wy", "wz");
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
        // As a hit is taken: the whole body by what it carries, the bone a
        // kick of its own, no faster than a limb is thrown.
        // (Standing firm, its feet hold its hips and legs: the body above
        // them takes it.)
        double mass = 0;
        for (const Element& n : elements())
            if (n.kind == kBone) mass += n.params.num("mass", 1.0);
        for (Element& n : elements())
            if (n.kind == kBone && !(firm() && (n.params.num("leg") > 0.5 || n.params.get_or<std::string>("parent", "").empty()))) set_vec(n, vec(n, "vx", "vy", "vz") + k * (1.0 / std::max(1.0, mass)), "vx", "vy", "vz");
        V3 kick = k * (0.35 / b.params.num("mass", 1.0));
        if (spatial::length(kick) > 3.0) kick = kick * (3.0 / spatial::length(kick));
        set_vec(b, vec(b, "vx", "vy", "vz") + kick, "vx", "vy", "vz");
        const double how = spatial::length(k) / std::max(1.0, self.params.num("fall_at", 150.0));
        // (As hard as it was struck, and no more for being struck again: a
        // body leant on is knocked every step, and must not go limp.)
        self.params.set("strength", std::min(self.params.num("strength", 1.0), how >= 1.0 && !firm() ? 0.0 : std::max(0.6, 1.0 - 0.8 * how)));
        b.params.set("knock_seen", b.params.num("knock_n")).set("weak", std::min(b.params.num("weak", 1.0), std::max(0.6, 1.0 - spatial::length(k) / 25.0)));
        self.params.set("push_x", self.params.num("push_x") + k.x).set("push_z", self.params.num("push_z") + k.z);
        knocked = true;
    }
    if (self.params.num("awake") < 0.5) {
        if (!knocked && !disturbed()) {
            self.params.set("lead", 0.0);
            return;
        }
        self.params.set("awake", 1.0).set("still", 0.0);
        // Woken standing: its feet planted where the being has them, its
        // hips over them.
        for (int f = 0; f < 2 && self.params.num("balance") > 0.5; ++f) {
            const Element& foot = element(Key{self.params.get_or<std::string>("foot" + std::to_string(f), "")});
            self.params.set("plant" + std::to_string(f) + "x", foot.params.num("tx")).set("plant" + std::to_string(f) + "z", foot.params.num("tz"));
        }
        self.params.set("sway_x", 0.0).set("sway_z", 0.0).set("swing", -1.0).set("going", -1.0);
    }
    self.params.set("lead", 1.0);

    // The world, as it is now; begun from all the solver carried over from
    // the last step - its contacts and its joints' pushes, one cache
    // (resting contacts hold steady, they do not buzz) - remembered by
    // everything this step starts from: the same start, tried again in any
    // order, begins from the same cache, joints and all.
    rigid::World w;
    build(w);
    uint64_t in = 1469598103934665603ull;
    const auto mix = [&](double v) {
        uint64_t q;
        std::memcpy(&q, &v, sizeof q);
        in = (in ^ q) * 1099511628211ull;
    };
    mix(dt);
    for (const Element& e : elements())
        for (const auto& [k, v] : e.params)
            if (const double* d = std::get_if<double>(&v)) mix(*d);
    {
        auto it = std::find_if(starts_.begin(), starts_.end(), [&](const auto& m) { return m.first == in; });
        if (it != starts_.end()) w.set_contacts(it->second);
        else {
            starts_.emplace_back(in, last_);
            if (starts_.size() > 16) starts_.erase(starts_.begin());
            w.set_contacts(last_);
        }
    }

    balance(dt);
    const bool full = self.params.num("full") > 0.5, stands = firm() || self.params.num("rising") > 0.5;
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
        if (!body) continue;  // (it rides)
        body->x = vec(b, "x", "y", "z");
        body->r = turn_of(b, "q");
        body->v = vec(b, "vx", "vy", "vz");
        body->w = vec(b, "wx", "wy", "wz");
        body->place();
        // Its muscles hold it up as strongly as they are: limp, it has all
        // its weight; strong, they carry it (gravity met where it falls).
        // (Held up against gravity as strongly as the body is - dazed, or
        // out - not as a bone is jolted for a moment: a jolted neck still
        // holds its head.)
        body->receives = {{"gravity", field::Response::Acceleration, 1.0 - strong * 0.95}};
        const bool hips = b.params.get_or<std::string>("parent", "").empty();
        if (!full && (hips || (stands && b.params.num("leg") > 0.5))) {
            // The hips: where the being means them, swayed by balance; and,
            // standing firm, the legs where the being means them too - and
            // no faster than a body can move (getting up is not a jump).
            V3 to = hips && self.params.num("balance") > 0.5 ? vec(self, "hip_x", "hip_y", "hip_z") : vec(b, "tx", "ty", "tz");
            const V3 go = to - body->x;
            const double most = 1.6 * dt;
            if (spatial::length(go) > most) to = body->x + go * (most / spatial::length(go));
            M3 r = turn_of(b, "aim");
            const V3 turn = spatial::log_map(r * spatial::transpose(body->r));
            if (spatial::length(turn) > 3.0 * dt) r = spatial::axis_angle(turn * (1.0 / spatial::length(turn)), 3.0 * dt) * body->r;
            driven.push_back({body, body->x, to, body->r, r});
        }
    }
    for (rigid::Joint& j : w.joints) {
        const Element& a = element(Key{j.a});
        const Element& b = element(Key{j.b});
        const double s = strong * b.params.num("weak", 1.0);
        // (Where balance has it put a foot, its leg is aimed there instead.)
        const auto aimed = [&](const Element& e) { return e.params.num("planned") > 0.5 ? turn_of(e, "plan") : turn_of(e, "aim"); };
        j.aim = spatial::transpose(aimed(a)) * aimed(b);
        j.rest_turn = turn_of(b, "r");
        j.cone = b.params.num("cone", 1.9);
        const double gain = b.params.num("gain", 1.0);
        j.aim_hertz = std::max(0.2, b.params.num("omega", 14.0) * s) * gain / (2 * 3.14159265358979);
        // Damped for all it moves, though no stiffer than its steps carry:
        // a capped spring damped as if uncapped would rock like a bag.
        j.aim_damping = zeta * b.params.num("heavy", gain);
        j.aim_torque = 2.0 + 600.0 * s;  // what is left of a limp joint: its friction
    }
    const std::string held = self.params.get_or<std::string>("grab", "");
    V3 push{};
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
            if (rigid::Body* g = w.find(held)) {
                const V3 at = self.params.num("grab_at") > 0.5 ? vec(self, "grab_lx", "grab_ly", "grab_lz") : g->com_local;
                const V3 to = vec(self, "grab_x", "grab_y", "grab_z");
                w.grab(held, at, to, self.params.num("grab_force", 500.0)).turns = false;
                // What sways the body is what pulls it from outside, as hard
                // as the hand pulls (its spring, no stronger than the hand):
                // not the joints' own pushing as the limb is yanked.
                const V3 pull = (to - (g->x + g->r * at)) * 600.0;
                const double most = self.params.num("grab_force", 500.0);
                push = push + (spatial::length(pull) > most ? pull * (most / spatial::length(pull)) : pull) * h;
            }
        w.step(h);
        if (!held.empty()) w.release(held);
    }
    self.params.set("push_x", self.params.num("push_x") + push.x).set("push_z", self.params.num("push_z") + push.z);

    last_ = w.contacts();

    // Back into the params; and asleep again once it is where it is meant
    // to be, still, whole and let go a moment.
    bool settled = held.empty() && self.params.num("strength") > 0.95;
    for (Element& b : elements()) {
        if (b.kind != kBone) continue;
        const rigid::Body* body = w.find(b.id.str());
        if (!body) {
            // Riding its parent, turned on it as the being means it.
            const Element& up = element(Key{b.params.get_or<std::string>("parent", "")});
            const M3 pq = turn_of(up, "q");
            set_vec(b, vec(up, "x", "y", "z") + pq * vec(b, "jx", "jy", "jz"), "x", "y", "z");
            set_turn(b, pq * spatial::transpose(turn_of(up, "aim")) * turn_of(b, "aim"), "q");
            set_vec(b, vec(up, "vx", "vy", "vz"), "vx", "vy", "vz");
            set_vec(b, vec(up, "wx", "wy", "wz"), "wx", "wy", "wz");
            continue;
        }
        set_vec(b, body->x, "x", "y", "z");
        set_turn(b, spatial::orthonormal(body->r), "q");
        set_vec(b, body->v, "vx", "vy", "vz");
        set_vec(b, body->w, "wx", "wy", "wz");
        if (b.params.num("weak", 1.0) < 0.95 || spatial::length(body->x - vec(b, "tx", "ty", "tz")) > 0.03 ||
            spatial::length(spatial::log_map(turn_of(b, "aim") * spatial::transpose(body->r))) > 0.06 || spatial::length(body->w) > 0.6)
            settled = false;
    }
    if (self.params.num("fallen") > 0.5 || self.params.num("swing") >= 0 || std::hypot(self.params.num("sway_x"), self.params.num("sway_z")) > 0.02) settled = false;
    for (Element& b : elements()) {
        if (b.kind != kBone) continue;
    }
    const double still = settled ? self.params.num("still") + dt : 0.0;
    self.params.set("still", still);
    if (still > 0.4 && !disturbed()) self.params.set("awake", 0.0).set("lead", 0.0);
}

bool Ragdoll::firm() const {
    const Params& p = element(self_id()).params;
    return p.num("balance") > 0.5 && p.num("stepping") < 0.5 && p.num("fallen") < 0.5 && p.num("full") < 0.5 && p.num("rising") < 0.5;
}

void Ragdoll::balance(double dt) {
    // Standing over two feet: the hips sway as a body on its feet does (an
    // inverted pendulum, w0 = sqrt(g / h)), pushed by what moves above them;
    // the feet push back no further than their soles reach - toward where the
    // sway would stop (the capture point) and a little toward home. When
    // that is off the soles, a foot steps to it. Steady again, the feet step
    // home. Too many steps, too long stumbling or too far leaning, and it
    // falls; lain still a while, it gets up.
    Element& self = element(self_id());
    if (self.params.num("balance") < 0.5) return;
    const std::string root_name = self.params.get_or<std::string>("root", "");
    const Element& root = element(Key{root_name});
    const V3 home = vec(root, "tx", "ty", "tz");
    const double floor = self.params.num("floor");
    const auto set_hips = [&](V3 at) { set_vec(self, at, "hip_x", "hip_y", "hip_z"); };
    const auto unplan = [&] {
        for (Element& b : elements())
            if (b.kind == kBone) b.params.set("planned", 0.0);
    };

    if (self.params.num("fallen") > 0.5) {
        // Lying: once still a while, it gets up - its hips eased home.
        const double speed = spatial::length(vec(root, "vx", "vy", "vz"));
        const double lie = speed < 0.15 ? self.params.num("lie") + dt : 0.0;
        self.params.set("lie", lie);
        set_hips(home);
        unplan();
        if (lie > 2.0) {
            // Up: its strength back, rising (no stepping, no giving up) until
            // it stands.
            self.params.set("fallen", 0.0).set("full", 0.0).set("lie", 0.0).set("steps", 0.0).set("stumble", 0.0).set("rising", 1.0);
            self.params.set("strength", std::max(self.params.num("strength"), 0.8));
            self.params.set("sway_x", 0.0).set("sway_z", 0.0).set("sway_vx", 0.0).set("sway_vz", 0.0).set("swing", -1.0);
            for (int f = 0; f < 2; ++f) {
                const Element& foot = element(Key{self.params.get_or<std::string>("foot" + std::to_string(f), "")});
                self.params.set("plant" + std::to_string(f) + "x", foot.params.num("tx")).set("plant" + std::to_string(f) + "z", foot.params.num("tz"));
            }
        }
        return;
    }
    if (self.params.num("full") > 0.5) return;  // let fall by whoever said so

    if (self.params.num("rising") > 0.5) {
        // Getting up: the hips eased home over the feet where they are meant
        // to be; done once the hips are nearly there.
        set_hips(home);
        unplan();
        self.params.set("push_x", 0.0).set("push_z", 0.0);
        if (spatial::length(vec(root, "x", "y", "z") - home) < 0.05) self.params.set("rising", 0.0);
        return;
    }
    if (firm()) {
        // Firm: it stands as its being stands it - no sway, no step - and
        // only its upper body gives. What pushes it past what its feet hold
        // builds up (and ebbs in a second); past `fall_at`, it goes down: a
        // body run into and kept pushing, not one leant on or struck.
        P2 push{self.params.num("push_x"), self.params.num("push_z")};
        const double braced = std::max(0.0, len(push) - self.params.num("hold", 300.0) * dt);
        push = len(push) > 1e-12 ? push * (braced / len(push)) : P2{0, 0};
        P2 load = P2{self.params.num("load_x"), self.params.num("load_z")} * std::exp(-dt) + push;
        self.params.set("push_x", 0.0).set("push_z", 0.0);
        if (self.params.num("anchor", -1.0) < 0 && len(load) > self.params.num("fall_at", 150.0)) {
            double mass = 0;
            for (Element& b : elements())
                if (b.kind == kBone) mass += b.params.num("mass", 1.0);
            // (Down the way it was pushed, with what got past its feet.)
            for (Element& b : elements())
                if (b.kind == kBone) set_vec(b, vec(b, "vx", "vy", "vz") + V3{load.x, 0.0, load.z} * (1.0 / std::max(1.0, mass)), "vx", "vy", "vz");
            self.params.set("fallen", 1.0).set("full", 1.0).set("lie", 0.0).set("load_x", 0.0).set("load_z", 0.0);
            unplan();
            return;
        }
        self.params.set("load_x", load.x).set("load_z", load.z).set("sway_x", 0.0).set("sway_z", 0.0).set("sway_vx", 0.0).set("sway_vz", 0.0);
        set_hips(home);
        unplan();
        return;
    }
    double mass = 0;
    for (const Element& b : elements())
        if (b.kind == kBone) mass += b.params.num("mass", 1.0);
    const double height = std::max(0.3, home.y - floor), w0 = std::sqrt(9.81 / height);
    P2 sway{self.params.num("sway_x"), self.params.num("sway_z")}, v{self.params.num("sway_vx"), self.params.num("sway_vz")};
    // Its feet brace against what pushes it, as hard as they hold (`hold`,
    // newtons): only what is more than that moves it.
    P2 push{self.params.num("push_x"), self.params.num("push_z")};
    const double braced = std::max(0.0, len(push) - self.params.num("hold", 300.0) * dt);
    push = len(push) > 1e-12 ? push * (braced / len(push)) : P2{0, 0};
    v = v + push * (1.0 / std::max(1.0, mass));
    self.params.set("push_x", 0.0).set("push_z", 0.0);
    const P2 rest{home.x, home.z}, com = rest + sway, capture = com + v * (1.0 / w0);

    // The feet: planted, or the one swinging; and their soles, as planted.
    int swing = int(self.params.num("swing", -1));
    double t = self.params.num("swing_t");
    const auto foot_of = [&](int f) -> const Element& { return element(Key{self.params.get_or<std::string>("foot" + std::to_string(f), "")}); };
    const auto plant = [&](int f) { return P2{self.params.num("plant" + std::to_string(f) + "x"), self.params.num("plant" + std::to_string(f) + "z")}; };
    const auto soles = [&](int f, std::vector<P2>& out, P2 at) {
        const Element& foot = foot_of(f);
        const M3 r = turn_of(foot, "aim");
        for (const V3& c : shape_of(foot).v) {
            const V3 q = r * c;
            out.push_back({at.x + q.x, at.z + q.z});
        }
    };
    // (A foot going home, not catching, is a short light step: its weight is
    // not all off it, so it still bears where it is on its way.)
    std::vector<P2> under;
    for (int f = 0; f < 2; ++f)
        if (f != swing) soles(f, under, plant(f));
        else if (self.params.num("catching") < 0.5) {
            const P2 from{self.params.num("from_x"), self.params.num("from_z")}, to{self.params.num("to_x"), self.params.num("to_z")};
            soles(f, under, from + (to - from) * std::min(1.0, t));
        }
    const std::vector<P2> support = hull2(under);

    // Where the feet push the body from: the capture point, and a little
    // toward the middle of the feet it stands on (not toward where it stood:
    // its feet go home first, and the body follows them), kept to the soles.
    double off = 0;
    within(support, capture, &off);
    // (Where the hips stand over these feet: where they stood, moved as far
    // as the feet have moved.)
    // (A foot waiting to go home: the hips move over the other first.)
    const int going = int(self.params.num("going", -1));
    P2 moved{0, 0};
    int planted = 0;
    for (int f = 0; f < 2; ++f)
        if (f != swing && f != going) moved = moved + (plant(f) - P2{foot_of(f).params.num("tx"), foot_of(f).params.num("tz")}), ++planted;
    // (Where the animation stands it, unless a step really moved its feet:
    // an idle shifting its weight under planted feet must not draw the hips
    // after it, to and fro.)
    // (A foot going home is light, not lifted off a body over the other
    // foot: the hips lean a third of the way toward the foot that stays, and
    // no further - a whole shift onto it and back, foot after foot, rocks.)
    const P2 shift = moved * (1.0 / std::max(1, planted));
    const P2 stood = len(shift) > 0.05 ? rest + shift : rest;
    const P2 centre = going >= 0 && swing < 0 ? stood + (plant(1 - going) - stood) * (1.0 / 3) : stood;
    // (Pulled home at a quarter: critically - straight back, never past.)
    const P2 cop = within(support, capture + (com - centre) * 0.25);
    const P2 a = (com - cop) * (w0 * w0);
    v = v + a * dt;
    sway = sway + v * dt;
    // Anchored: no further from its place than its reach; moving away, it
    // is held there.
    const double anchor = self.params.num("anchor", -1.0);
    if (anchor >= 0 && len(sway) > anchor) {
        const P2 out = sway * (1.0 / len(sway));
        sway = out * anchor;
        const double away = v.x * out.x + v.z * out.z;
        if (away > 0) v = v - out * away;
    }

    // A planted foot within a few centimetres of where its clip has it simply
    // follows the clip (an idle shifting its weight is not a stumble): only a
    // foot a step has really moved is stepped home - else the hips would go
    // from foot to foot after every shuffle, rocking.
    if (swing < 0 && self.params.num("catching") < 0.5)
        for (int f = 0; f < 2; ++f) {
            const P2 home_foot{foot_of(f).params.num("tx"), foot_of(f).params.num("tz")};
            if (f != going && len(plant(f) - home_foot) < 0.05)
                self.params.set("plant" + std::to_string(f) + "x", home_foot.x).set("plant" + std::to_string(f) + "z", home_foot.z);
        }
    // A step: to catch it, or, steady, home.
    const P2 mid = (plant(0) + plant(1)) * 0.5;
    const double reach = 0.6 * height;
    if (anchor >= 0) {
        // (Anchored, it does not step: its feet stay where they are planted.)
    } else if (swing < 0 && off > 0.03) {
        int f = len(plant(0) - capture) > len(plant(1) - capture) ? 0 : 1;
        P2 to = capture + (plant(f) - mid) + (capture - mid) * (0.05 / std::max(1e-6, len(capture - mid)));
        if (len(to - com) > reach) to = com + (to - com) * (reach / len(to - com));
        swing = f, t = 0;
        self.params.set("from_x", plant(f).x).set("from_z", plant(f).z).set("to_x", to.x).set("to_z", to.z);
        self.params.set("steps", self.params.num("steps") + 1).set("catching", 1.0).set("going", -1.0);
    } else if (swing < 0 && off <= 0 && len(v) < 0.15) {
        // Steady: the foot furthest from home goes home, if one is - once the
        // body has leaned toward the other one (it leans first).
        int f = going;
        if (f < 0) {
            double most = 0.05;
            for (int g = 0; g < 2; ++g) {
                const Element& foot = foot_of(g);
                const double d = len(plant(g) - P2{foot.params.num("tx"), foot.params.num("tz")});
                if (d > most) most = d, f = g;
            }
            self.params.set("going", double(f));
        }
        if (f >= 0 && len(capture - centre) < 0.02) {
            {
                const Element& foot = foot_of(f);
                swing = f, t = 0;
                self.params.set("from_x", plant(f).x).set("from_z", plant(f).z).set("to_x", foot.params.num("tx")).set("to_z", foot.params.num("tz"));
                self.params.set("catching", 0.0).set("going", -1.0);
            }
        }
    }
    if (swing >= 0 && self.params.num("catching") > 0.5) {
        // Catching: the foot goes on toward where the sway would now come to
        // rest, not where it would when the step began.
        const P2 other = plant(1 - swing);
        P2 to = capture + (capture - other) * (0.05 / std::max(1e-6, len(capture - other)));
        if (len(to - com) > reach) to = com + (to - com) * (reach / len(to - com));
        self.params.set("to_x", to.x).set("to_z", to.z);
    }
    if (swing >= 0) {
        t += dt / 0.25;
        if (t >= 1.0) {
            self.params.set("plant" + std::to_string(swing) + "x", self.params.num("to_x")).set("plant" + std::to_string(swing) + "z", self.params.num("to_z"));
            swing = -1, t = 0;
        }
    }
    // Stumbling: while where it would come to rest is off its soles. Back
    // over them, it has caught itself - whatever its feet do next is only
    // going home.
    double stumble = off > 0 ? self.params.num("stumble") + dt : self.params.num("stumble");
    if (off <= 0 && swing < 0 && len(v) < 0.1) stumble = 0, self.params.set("steps", 0.0);
    // Leaning: how far its chest is from upright.
    double lean = 0;
    for (const Element& b : elements())
        if (b.kind == kBone && b.params.get_or<std::string>("parent", "") == root_name && b.params.num("leg") < 0.5)
            lean = std::max(lean, std::acos(std::clamp((turn_of(b, "q") * spatial::transpose(turn_of(b, "aim")) * V3{0, 1, 0}).y, -1.0, 1.0)));
    // (Going where no step could catch it - its sway would come to rest
    // further than a stride and a half beyond its feet - it falls at once.)
    if (anchor < 0 && (self.params.num("steps") > 4 || stumble > 2.0 || lean > 0.87 || off > 1.5 * reach)) {
        // It gives up: its legs and hips have weight now, and its strength goes.
        self.params.set("fallen", 1.0).set("full", 1.0).set("lie", 0.0).set("swing", -1.0);
        unplan();
        return;
    }
    self.params.set("sway_x", sway.x).set("sway_z", sway.z).set("sway_vx", v.x).set("sway_vz", v.z);
    self.params.set("swing", double(swing)).set("swing_t", t).set("stumble", stumble);

    // The hips where the sway has them, and
    // each leg aimed to put its foot where it is planted or swinging to.
    const V3 hips = home + V3{sway.x, 0.0, sway.z};  // (level as a foot moves: a dip each step reads as a bob)
    set_hips(hips);
    const bool home_again = std::hypot(sway.x, sway.z) < 0.005 && swing < 0;
    const M3 rr = turn_of(root, "aim");
    for (int f = 0; f < 2; ++f) {
        Element& thigh = element(Key{self.params.get_or<std::string>("thigh" + std::to_string(f), "")});
        Element& shin = element(Key{self.params.get_or<std::string>("shin" + std::to_string(f), "")});
        Element& foot = element(Key{self.params.get_or<std::string>("foot" + std::to_string(f), "")});
        P2 at = plant(f);
        double lift = 0;
        if (f == swing) {
            at = P2{self.params.num("from_x"), self.params.num("from_z")} * (1 - t) + P2{self.params.num("to_x"), self.params.num("to_z")} * t;
            lift = 0.07 * std::sin(3.14159265358979 * t);
        }
        const V3 shift{sway.x, hips.y - home.y, sway.z};
        const V3 ankle_home = vec(foot, "tx", "ty", "tz");
        if (home_again && len(at - P2{ankle_home.x, ankle_home.z}) < 0.01) {
            thigh.params.set("planned", 0.0), shin.params.set("planned", 0.0), foot.params.set("planned", 0.0);
            continue;
        }
        const V3 hip = hips + rr * vec(thigh, "jx", "jy", "jz");
        M3 T, S;
        two_bone(hip, vec(shin, "tx", "ty", "tz") + shift, ankle_home + shift, {at.x, ankle_home.y + lift, at.z}, turn_of(thigh, "aim"), turn_of(shin, "aim"), T, S);
        set_turn(thigh, T, "plan"), set_turn(shin, S, "plan"), set_turn(foot, turn_of(foot, "aim"), "plan");
        thigh.params.set("planned", 1.0), shin.params.set("planned", 1.0), foot.params.set("planned", 1.0);
    }
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
