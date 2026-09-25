// A rope: a cord, a cable, a string - anything long, thin and limp that hangs
// between two ends, lies over the edges of things and on the floor, and never
// passes through anything.
//
// A rope is joints joined by links of one length. Each step, in substeps when
// an end has moved far (so it is led, not yanked):
//
//   move      every joint keeps its motion (verlet: where it is less where it
//             was), damped as a cord is - the air slows all of it a little,
//             and the rope slows itself: each joint's motion is drawn towards
//             its neighbours', so a pull runs along it once and dies instead
//             of rippling back and forth - and no faster than `fastest`;
//             gravity.
//   hold      over a few passes: each joint eased towards the line through
//             its neighbours (a rope resists bending, evenly: it lies in
//             curves, it does not kink); each link held to its length, swept
//             one way then back, so a pull is felt the same from either end;
//             no joint further from either end than the rope between them
//             (so however many links, it never stretches);
//             then out of the ground - on the floor, on the top of any block
//             it came down on, out of the side of one it is in, over any lump.
//             A link cannot pass through a block either: going over an edge
//             it leaves the top at the edge, and one end over a block and the
//             other under it puts the lower out past the nearest edge.
//   lie       a joint lying on something loses most of its slide (friction).
//
// Everything is in the caller's frame (y up, metres, seconds). The rope is
// plain data: where its joints are, and were. What it hangs from and what it
// lies on are handed to each step, so it can live in any state - as elements'
// params, stepped by that state's arrow - and be run again from any moment.
//
//   rope::Ground g{floor, blocks, lumps};
//   auto r = rope::Rope::laid(a, b, 1.2, 36, g);
//   r.step(dt, a_was, a, b_was, b, g);
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "sg/core/Core.hpp"

namespace sg::rope {

// Something the rope lies on and cannot pass through: a box, turned `yaw`
// about y, its footprint `sx` x `sz` about (`x`, `z`), from `base` up to `top`.
struct Block {
    double x = 0, z = 0, yaw = 0, sx = 0, sz = 0, base = 0, top = 0;
};
// Something small lying about, as its box (along the axes): the rope goes over it.
struct Lump {
    Vec3d lo, hi;
};
struct Ground {
    double floor = 0;
    std::vector<Block> blocks;
    std::vector<Lump> lumps;
};

struct Settings {
    double gravity = 9.81;
    double air = 5.0;         // how much the air slows it, per second
    double inner = 0.7;       // how much each joint's motion is drawn to its neighbours' (0..1)
    double fastest = 4.0;     // no joint faster than this, m/s
    double bend = 0.08;       // how far each pass eases a joint towards straight (0..1)
    double friction = 0.85;   // how much of a lying joint's slide it loses a step (0..1)
    double lift = 0.003;      // how far above what it lies on a joint's middle is
    int passes = 16;
    int max_substeps = 4;
};

inline double length_of(const Vec3d& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

struct Rope {
    std::vector<Vec3d> p, o;  // the joints now, and a step ago
    double length = 1.0;

    int links() const { return static_cast<int>(p.size()) - 1; }
    double link() const { return length / std::max(1, links()); }

    // Laid from `a` to `b` at its whole length - a curve bowed out to the side
    // just enough to be that long, cut into equal links - lifted out of
    // anything it starts in, and let fall for `settle` steps.
    static Rope laid(const Vec3d& a, const Vec3d& b, double length, int links, const Ground& g, const Settings& s = {},
                     int settle = 200) {
        Rope r;
        r.length = length;
        links = std::max(2, links);
        Vec3d side{-(b.z - a.z), 0, b.x - a.x};
        const double sl = std::hypot(side.x, side.z);
        side = sl > 1e-9 ? side * (1.0 / sl) : Vec3d{1, 0, 0};
        const auto curve = [&](double bow, double t) { return a + (b - a) * t + side * (4 * bow * t * (1 - t)); };
        const auto run = [&](double bow) {
            double l = 0;
            Vec3d prev = a;
            for (int k = 1; k <= 200; ++k) {
                const Vec3d q = curve(bow, k / 200.0);
                l += length_of(q - prev), prev = q;
            }
            return l;
        };
        double lo = 0, hi = length;
        for (int k = 0; k < 40; ++k) (run((lo + hi) * 0.5) < length ? lo : hi) = (lo + hi) * 0.5;
        std::vector<double> along{0.0};
        std::vector<Vec3d> fine{a};
        for (int k = 1; k <= 400; ++k) {
            fine.push_back(curve(lo, k / 400.0));
            along.push_back(along.back() + length_of(fine.back() - fine[fine.size() - 2]));
        }
        std::size_t at = 0;
        for (int i = 0; i <= links; ++i) {
            const double want = along.back() * i / links;
            while (at + 1 < along.size() && along[at + 1] < want) ++at;
            const double f = at + 1 < along.size() ? (want - along[at]) / std::max(1e-12, along[at + 1] - along[at]) : 0.0;
            Vec3d q = at + 1 < fine.size() ? fine[at] + (fine[at + 1] - fine[at]) * f : fine.back();
            q.y = std::max(g.floor + s.lift, q.y);
            for (const Block& k : g.blocks)
                if (inside(k, q) && q.y < k.top + s.lift && q.y > k.top - 0.25) q.y = k.top + s.lift;
            r.p.push_back(q);
        }
        r.o = r.p;
        for (int k = 0; k < settle; ++k) r.step(1.0 / 60.0, a, a, b, b, g, s);
        return r;
    }

    // Where a thing on the end may be pulled to: no further from where the
    // rope is fixed than `share` of its length.
    static Vec3d within(const Vec3d& fixed, const Vec3d& t, double length, double share = 0.93) {
        const Vec3d d = t - fixed;
        const double far = length_of(d), most = length * share;
        return far > most ? fixed + d * (most / far) : t;
    }

    // Is (x, z) over a block's footprint?
    static bool inside(const Block& k, const Vec3d& q, double margin = 0.0) {
        const Vec3d l = rotate_xz({q.x - k.x, 0, q.z - k.z}, -k.yaw);
        return std::fabs(l.x) < k.sx * 0.5 + margin && std::fabs(l.z) < k.sz * 0.5 + margin;
    }

    // Does any link pass through a block? (For checking.)
    bool through(const Ground& g, double margin = 0.005) const {
        for (int i = 0; i < links(); ++i) {
            const Vec3d& u = p[static_cast<std::size_t>(i)];
            const Vec3d& w = p[static_cast<std::size_t>(i + 1)];
            const Vec3d& hi = u.y > w.y ? u : w;
            const Vec3d& lo = u.y > w.y ? w : u;
            for (const Block& k : g.blocks) {
                if (hi.y < k.top || lo.y > k.base) continue;
                const double t = (hi.y - k.top) / std::max(1e-9, hi.y - lo.y);
                if (inside(k, hi + (lo - hi) * t, -margin)) return true;
            }
        }
        return false;
    }

    // A step of `dt`, its ends going from `a0`, `b0` to `a`, `b`. How far
    // its most-moved joint went.
    double step(double dt, const Vec3d& a0, const Vec3d& a, const Vec3d& b0, const Vec3d& b, const Ground& g,
                const Settings& s = {}) {
        const int n = links();
        if (n < 1 || dt <= 0) return 0.0;
        const double lk = link();
        const std::vector<Vec3d> start = p;
        const double lead = std::max(length_of(b - b0), length_of(a - a0));
        const int subs = std::clamp(static_cast<int>(std::ceil(lead / (lk * 0.75))), 1, std::max(1, s.max_substeps));
        std::vector<Vec3d> v(p.size()), was;
        for (int sub = 1; sub <= subs; ++sub) {
            const double h = dt / subs, f = static_cast<double>(sub) / subs;
            const double keep = std::exp(-s.air * h);
            for (std::size_t i = 0; i < p.size(); ++i) v[i] = p[i] - o[i];
            for (int i = 1; i < n; ++i) {
                const std::size_t k = static_cast<std::size_t>(i);
                Vec3d w = (v[k] * (1.0 - s.inner) + (v[k - 1] + v[k] * 2.0 + v[k + 1]) * (0.25 * s.inner)) * keep;
                const double speed = length_of(w), most = s.fastest * h;
                if (speed > most) w = w * (most / speed);
                o[k] = p[k];
                p[k] = p[k] + w + Vec3d{0, -s.gravity * h * h, 0};
            }
            was = o;
            o.front() = p.front(), o.back() = p.back();
            p.front() = a0 + (a - a0) * f, p.back() = b0 + (b - b0) * f;
            for (int it = 0; it < s.passes; ++it) {
                for (int i = 1; i < n; ++i) {
                    const Vec3d mid = (p[static_cast<std::size_t>(i - 1)] + p[static_cast<std::size_t>(i + 1)]) * 0.5;
                    Vec3d& q = p[static_cast<std::size_t>(i)];
                    q = q + (mid - q) * s.bend;
                }
                // No joint further from either end than the rope between them
                // is long: however many links, it never stretches.
                for (int i = 1; i < n; ++i) {
                    Vec3d& q = p[static_cast<std::size_t>(i)];
                    for (int e = 0; e < 2; ++e) {
                        const Vec3d& end = e ? p.back() : p.front();
                        const double most = (e ? n - i : i) * lk;
                        const Vec3d d = q - end;
                        const double far = length_of(d);
                        if (far > most) q = end + d * (most / far);
                    }
                }
                for (int j = 0; j < n; ++j) {
                    const int i = it % 2 ? n - 1 - j : j;
                    Vec3d& u = p[static_cast<std::size_t>(i)];
                    Vec3d& w = p[static_cast<std::size_t>(i + 1)];
                    const Vec3d d = w - u;
                    const double l = length_of(d);
                    if (l < 1e-9) continue;
                    const Vec3d fix = d * ((l - lk) / l);
                    const bool fu = i == 0, fw = i + 1 == n;
                    if (fu && fw) continue;
                    if (fu) w = w - fix;
                    else if (fw) u = u + fix;
                    else u = u + fix * 0.5, w = w - fix * 0.5;
                }
                for (int i = 1; i < n; ++i) settle_joint(p[static_cast<std::size_t>(i)], was[static_cast<std::size_t>(i)], g, s);
                for (int i = 0; i < n; ++i) keep_link(i, g);
            }
            // Lying on something, a joint is held by it: it slides only a little.
            for (int i = 1; i < n; ++i) {
                const Vec3d& q = p[static_cast<std::size_t>(i)];
                bool lying = q.y <= g.floor + s.lift + 1e-6;
                for (const Block& k : g.blocks)
                    if (!lying && std::fabs(q.y - (k.top + s.lift)) < 1e-6 && inside(k, q)) lying = true;
                if (lying) o[static_cast<std::size_t>(i)] = q - (q - o[static_cast<std::size_t>(i)]) * (1.0 - s.friction);
            }
        }
        double moved = 0;
        for (std::size_t i = 0; i < p.size(); ++i) moved = std::max(moved, length_of(p[i] - start[i]));
        return moved;
    }

private:
    // A joint on the floor and on the tops of things, not in them.
    static void settle_joint(Vec3d& q, const Vec3d& was, const Ground& g, const Settings& s) {
        if (q.y < g.floor + s.lift) q.y = g.floor + s.lift;
        for (const Block& k : g.blocks) {
            const Vec3d l = rotate_xz({q.x - k.x, 0, q.z - k.z}, -k.yaw);
            const double hx = k.sx * 0.5, hz = k.sz * 0.5, top = k.top + s.lift;
            if (std::fabs(l.x) >= hx || std::fabs(l.z) >= hz || q.y >= top) continue;
            // Came down onto it from above (however far it fell this step):
            // it lies on it. Otherwise only what is in it is put out.
            const bool from_above = was.y >= top - 0.01;
            if (!from_above && q.y <= k.base) continue;
            if (from_above || q.y > top - 0.05) {
                q.y = top;
            } else {
                Vec3d out = l;
                if (hx - std::fabs(l.x) < hz - std::fabs(l.z)) out.x = (l.x < 0 ? -hx : hx) * 1.001;
                else out.z = (l.z < 0 ? -hz : hz) * 1.001;
                const Vec3d back = rotate_xz(out, k.yaw);
                q.x = k.x + back.x, q.z = k.z + back.z;
            }
        }
        for (const Lump& m : g.lumps) {
            if (q.x <= m.lo.x || q.x >= m.hi.x || q.z <= m.lo.z || q.z >= m.hi.z || q.y >= m.hi.y + s.lift || q.y <= m.lo.y) continue;
            // Come down onto it (or nearly on top): over it. From the side:
            // out of the side it came in by, not popped up on top.
            if (was.y >= m.hi.y + s.lift - 0.01 || q.y > m.hi.y - 0.004) {
                q.y = m.hi.y + s.lift;
            } else {
                const double gaps[4] = {q.x - m.lo.x, m.hi.x - q.x, q.z - m.lo.z, m.hi.z - q.z};
                const int k = static_cast<int>(std::min_element(gaps, gaps + 4) - gaps);
                if (k == 0) q.x = m.lo.x - 1e-4;
                else if (k == 1) q.x = m.hi.x + 1e-4;
                else if (k == 2) q.z = m.lo.z - 1e-4;
                else q.z = m.hi.z + 1e-4;
            }
        }
    }

    // Link `i` not through a block: over an edge it leaves the top at the
    // edge (the joint on it slides out to it); one end over a block and the
    // other under it, the lower is put out past its nearest edge.
    void keep_link(int i, const Ground& g) {
        const int n = links();
        Vec3d& u = p[static_cast<std::size_t>(i)];
        Vec3d& w = p[static_cast<std::size_t>(i + 1)];
        for (const Block& k : g.blocks) {
            const bool u_over = u.y >= k.top, w_over = w.y >= k.top;
            if (u_over == w_over) continue;
            Vec3d& low = u_over ? w : u;
            Vec3d& high = u_over ? u : w;
            const bool low_fixed = u_over ? i + 1 == n : i == 0, high_fixed = u_over ? i == 0 : i + 1 == n;
            const Vec3d c{k.x, 0, k.z};
            const Vec3d l = rotate_xz(low - c, -k.yaw), hi = rotate_xz(high - c, -k.yaw);
            const double hx = k.sx * 0.5, hz = k.sz * 0.5;
            const bool hi_in = std::fabs(hi.x) < hx && std::fabs(hi.z) < hz, lo_in = std::fabs(l.x) < hx && std::fabs(l.z) < hz;
            if (hi_in && !lo_in) {
                if (high_fixed) continue;
                double t = 1.0;
                for (int a = 0; a < 2; ++a) {
                    const double a0 = a ? hi.z : hi.x, a1 = a ? l.z : l.x, h = a ? hz : hx;
                    if (std::fabs(a1) > h && std::fabs(a1 - a0) > 1e-9) t = std::min(t, ((a1 > 0 ? h : -h) - a0) / (a1 - a0));
                }
                if (hi.y + (l.y - hi.y) * t < k.top) {
                    Vec3d edge = hi + (l - hi) * t;
                    edge.y = hi.y;
                    const Vec3d back = rotate_xz(edge, k.yaw);
                    high.x = c.x + back.x, high.z = c.z + back.z;
                }
                continue;
            }
            if (low_fixed || low.y > k.base) continue;
            const double t = (hi.y - k.top) / std::max(1e-9, hi.y - l.y);
            const double cx = hi.x + (l.x - hi.x) * t, cz = hi.z + (l.z - hi.z) * t;
            if (std::fabs(cx) >= hx || std::fabs(cz) >= hz) continue;
            Vec3d out{cx, l.y, cz};
            if (hx - std::fabs(cx) < hz - std::fabs(cz)) out.x = (cx < 0 ? -hx : hx) + (cx < 0 ? -0.01 : 0.01);
            else out.z = (cz < 0 ? -hz : hz) + (cz < 0 ? -0.01 : 0.01);
            const Vec3d back = rotate_xz(out, k.yaw);
            low.x = c.x + back.x, low.z = c.z + back.z;
        }
    }
};

}  // namespace sg::rope
