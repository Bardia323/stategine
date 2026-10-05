// Where a cut is: a solid's exact faces kept everywhere but in a box round
// the cut, the field meshed only in that box, and the two made one surface
// along the box's face - the strip between their rims filled, so nothing is
// open and nothing is hidden inside.
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <map>
#include <unordered_map>

#include "ModelerKernel.hpp"

namespace sg::sculpt::kernel {

namespace {

double axis(V3 p, int a) { return a == 0 ? p.x : a == 1 ? p.y : p.z; }
void set_axis(V3& p, int a, double v) { (a == 0 ? p.x : a == 1 ? p.y : p.z) = v; }

// Where an edge crosses a plane, the same whichever way the edge is walked -
// so the two faces that share it cut it at one point.
V3 crossing(V3 p, V3 q, int a, double at) {
    const bool swap = axis(p, 0) > axis(q, 0) || (axis(p, 0) == axis(q, 0) && (axis(p, 1) > axis(q, 1) || (axis(p, 1) == axis(q, 1) && axis(p, 2) > axis(q, 2))));
    if (swap) std::swap(p, q);
    const double pa = axis(p, a), qa = axis(q, a);
    const double t = std::abs(qa - pa) > 1e-300 ? (at - pa) / (qa - pa) : 0.5;
    V3 r = p + (q - p) * std::clamp(t, 0.0, 1.0);
    set_axis(r, a, at);
    return r;
}

// A convex polygon in faces, from whichever corner makes the thinnest of
// them least thin (a corner lying on an edge then makes no face of no area).
void fan(const std::vector<V3>& poly, int material, Geom& out) {
    std::vector<V3> p;
    for (const V3& q : poly)
        if (p.empty() || len(q - p.back()) > 1e-12) p.push_back(q);
    while (p.size() > 1 && len(p.front() - p.back()) <= 1e-12) p.pop_back();
    const std::size_t n = p.size();
    if (n < 3) return;
    std::size_t best = 0;
    double best_min = -1;
    for (std::size_t s = 0; s < n; ++s) {
        double m = 1e300;
        for (std::size_t i = 1; i + 1 < n; ++i) m = std::min(m, len(cross(p[(s + i) % n] - p[s], p[(s + i + 1) % n] - p[s])));
        if (m > best_min) best_min = m, best = s;
    }
    for (std::size_t i = 1; i + 1 < n; ++i) {
        const int base = int(out.p.size());
        out.p.push_back(p[best]);
        out.p.push_back(p[(best + i) % n]);
        out.p.push_back(p[(best + i + 1) % n]);
        out.t.insert(out.t.end(), {base, base + 1, base + 2});
        out.mat.push_back(material);
    }
}

using Loop = std::vector<int>;

// The open rims of a surface: its edges met by one face only, chained into
// loops, each the way its faces run along it. Only edges `keep` says (both
// ends): the rim on the box, not a hole the surface had before. False if a
// rim branches.
bool rims(const Geom& g, const std::function<bool(V3)>& keep, std::vector<Loop>& loops) {
    std::map<std::pair<int, int>, int> uses;
    for (std::size_t f = 0; f + 2 < g.t.size(); f += 3)
        for (int k = 0; k < 3; ++k) {
            const int a = g.t[f + std::size_t(k)], b = g.t[f + std::size_t((k + 1) % 3)];
            ++uses[{a, b}];
        }
    std::unordered_map<int, int> next;
    for (const auto& [e, n] : uses) {
        if (uses.count({e.second, e.first})) continue;
        if (n != 1) return false;
        if (!keep(g.p[std::size_t(e.first)]) || !keep(g.p[std::size_t(e.second)])) continue;
        if (!next.emplace(e.first, e.second).second) return false;
    }
    std::unordered_map<int, char> seen;
    std::vector<int> starts;
    for (const auto& kv : next) starts.push_back(kv.first);
    std::sort(starts.begin(), starts.end());
    for (int s : starts) {
        if (seen[s]) continue;
        Loop l;
        int at = s;
        while (!seen[at]) {
            seen[at] = 1;
            l.push_back(at);
            auto it = next.find(at);
            if (it == next.end()) return false;
            at = it->second;
        }
        if (at != s || l.size() < 3) return false;
        loops.push_back(std::move(l));
    }
    return true;
}

V3 area_of(const Geom& g, const Loop& l) {
    V3 a;
    for (std::size_t i = 0; i < l.size(); ++i) a = a + cross(g.p[std::size_t(l[i])], g.p[std::size_t(l[(i + 1) % l.size()])]);
    return a * 0.5;
}

double seg_dist(V3 p, V3 a, V3 b) {
    const V3 ab = b - a;
    const double ll = dot(ab, ab);
    const double t = ll > 0 ? std::clamp(dot(p - a, ab) / ll, 0.0, 1.0) : 0.0;
    return len(p - (a + ab * t));
}

// How far apart two rims are: the mean distance of each one's points from the
// other's line, the larger way.
double apart(const Geom& ga, const Loop& a, const Geom& gb, const Loop& b) {
    const auto one = [](const Geom& gp, const Loop& p, const Geom& gq, const Loop& q) {
        double sum = 0;
        for (int i : p) {
            double best = 1e300;
            for (std::size_t j = 0; j < q.size(); ++j) best = std::min(best, seg_dist(gp.p[std::size_t(i)], gq.p[std::size_t(q[j])], gq.p[std::size_t(q[(j + 1) % q.size()])]));
            sum += best;
        }
        return sum / double(p.size());
    };
    return std::max(one(ga, a, gb, b), one(gb, b, ga, a));
}

}  // namespace

Geom clip_outside(const Geom& g, const Box& b, int material) {
    Geom out;
    out.crease = g.crease;
    const double lo[3] = {b.lo.x, b.lo.y, b.lo.z}, hi[3] = {b.hi.x, b.hi.y, b.hi.z};
    const auto beyond = [&](int plane, V3 p) { return plane % 2 == 0 ? axis(p, plane / 2) < lo[plane / 2] : axis(p, plane / 2) > hi[plane / 2]; };
    const auto at_of = [&](int plane) { return plane % 2 == 0 ? lo[plane / 2] : hi[plane / 2]; };
    // Where the planes after `plane` will cut the edge p-q, as they cut it in
    // what is left of the face (one cut a plane; what lies beyond goes, the
    // rest is cut on): the points, in order from p.
    const auto cuts_after = [&](int plane, V3 p, V3 q) {
        std::vector<V3> pts;
        const V3 from = p, dir = q - p;
        for (int j = plane + 1; j < 6; ++j) {
            const bool bp = beyond(j, p), bq = beyond(j, q);
            if (bp && bq) break;
            if (bp == bq) continue;
            const V3 x = crossing(p, q, j / 2, at_of(j));
            pts.push_back(x);
            (bp ? p : q) = x;
        }
        std::sort(pts.begin(), pts.end(), [&](V3 a, V3 c) { return dot(a - from, dir) < dot(c - from, dir); });
        return pts;
    };
    for (std::size_t f = 0; f + 2 < g.t.size(); f += 3) {
        const int m = g.mat.empty() ? material : g.mat[f / 3];
        std::vector<V3> rest{g.p[std::size_t(g.t[f])], g.p[std::size_t(g.t[f + 1])], g.p[std::size_t(g.t[f + 2])]};
        // Plane by plane: what lies beyond it is kept, what is short of it goes on.
        for (int plane = 0; plane < 6 && rest.size() >= 3; ++plane) {
            const int a = plane / 2;
            const double at = at_of(plane);
            std::vector<V3> out_part, in_part;
            std::vector<char> cut;  // which of out_part's points the plane made
            for (std::size_t i = 0; i < rest.size(); ++i) {
                const V3 p = rest[i], q = rest[(i + 1) % rest.size()];
                const bool bp = beyond(plane, p), bq = beyond(plane, q);
                if (bp) out_part.push_back(p), cut.push_back(0);
                else in_part.push_back(p);
                if (bp != bq) {
                    const V3 x = crossing(p, q, a, at);
                    out_part.push_back(x), cut.push_back(1);
                    in_part.push_back(x);
                }
            }
            if (out_part.size() >= 3) {
                // The edge the plane cut along is cut again by the planes after
                // it, in what is left: the same points here, or the two would
                // not meet edge to edge.
                std::vector<V3> whole;
                for (std::size_t i = 0; i < out_part.size(); ++i) {
                    whole.push_back(out_part[i]);
                    const std::size_t j = (i + 1) % out_part.size();
                    if (cut[i] && cut[j])
                        for (const V3& x : cuts_after(plane, out_part[i], out_part[j])) whole.push_back(x);
                }
                fan(whole, m, out);
            }
            rest = std::move(in_part);
        }
    }
    weld(out);
    return out;
}

bool zip(const Geom& outer, const Geom& inner, const Box& b, double cell, Geom& out) {
    const double e = 1e-7;
    const auto on_box = [&](V3 p) {
        const bool in = p.x >= b.lo.x - e && p.x <= b.hi.x + e && p.y >= b.lo.y - e && p.y <= b.hi.y + e && p.z >= b.lo.z - e && p.z <= b.hi.z + e;
        const bool face = std::abs(p.x - b.lo.x) <= e || std::abs(p.x - b.hi.x) <= e || std::abs(p.y - b.lo.y) <= e || std::abs(p.y - b.hi.y) <= e ||
                          std::abs(p.z - b.lo.z) <= e || std::abs(p.z - b.hi.z) <= e;
        return in && face;
    };
    std::vector<Loop> A, B;
    if (!rims(outer, on_box, A) || !rims(inner, [](V3) { return true; }, B)) return false;
    if (A.size() != B.size()) return false;
    // Each rim of the outside paired with the nearest of the inside's, one to one.
    std::vector<int> pair(A.size(), -1);
    std::vector<char> taken(B.size(), 0);
    for (std::size_t i = 0; i < A.size(); ++i) {
        double best = 1e300;
        int which = -1;
        for (std::size_t j = 0; j < B.size(); ++j) {
            if (taken[j]) continue;
            const double d = apart(outer, A[i], inner, B[j]);
            if (d < best) best = d, which = int(j);
        }
        if (which < 0 || best > 1.5 * cell) return false;
        // They run opposite ways round the strip between them.
        if (dot(area_of(outer, A[i]), area_of(inner, B[std::size_t(which)])) >= 0) return false;
        taken[std::size_t(which)] = 1;
        pair[i] = which;
    }
    out = outer;
    const int base = int(out.p.size());
    out.p.insert(out.p.end(), inner.p.begin(), inner.p.end());
    for (int v : inner.t) out.t.push_back(v + base);
    for (std::size_t f = 0; f < inner.t.size() / 3; ++f) out.mat.push_back(inner.mat.empty() ? 0 : inner.mat[f]);
    // The strip: down the two rims side by side, a face at a time, the shorter
    // way across each time.
    for (std::size_t i = 0; i < A.size(); ++i) {
        const Loop& a = A[i];
        Loop bl(B[std::size_t(pair[i])].rbegin(), B[std::size_t(pair[i])].rend());
        for (int& v : bl) v += base;
        const std::size_t na = a.size(), nb = bl.size();
        std::size_t j0 = 0;
        double near = 1e300;
        for (std::size_t j = 0; j < nb; ++j) {
            const double d = len(out.p[std::size_t(bl[j])] - out.p[std::size_t(a[0])]);
            if (d < near) near = d, j0 = j;
        }
        const auto A_ = [&](std::size_t k) { return a[k % na]; };
        const auto B_ = [&](std::size_t k) { return bl[(j0 + k) % nb]; };
        const auto P = [&](int v) { return out.p[std::size_t(v)]; };
        // The material of the face on the outside's rim, for the strip.
        int m = 0;
        for (std::size_t f = 0; f + 2 < outer.t.size(); f += 3)
            if (outer.t[f] == a[0] || outer.t[f + 1] == a[0] || outer.t[f + 2] == a[0]) {
                m = outer.mat.empty() ? 0 : outer.mat[f / 3];
                break;
            }
        std::size_t ia = 0, ib = 0;
        while (ia < na || ib < nb) {
            bool step_a;
            if (ia == na) step_a = false;
            else if (ib == nb) step_a = true;
            else step_a = len(P(A_(ia + 1)) - P(B_(ib))) <= len(P(A_(ia)) - P(B_(ib + 1)));
            if (step_a) {
                out.t.insert(out.t.end(), {A_(ia + 1), A_(ia), B_(ib)});
                ++ia;
            } else {
                out.t.insert(out.t.end(), {B_(ib), B_(ib + 1), A_(ia)});
                ++ib;
            }
            out.mat.push_back(m);
        }
    }
    return true;
}

void refine(Geom& g, const Sdf& f, double coarse, std::size_t budget, int max_grid) {
    if (g.t.empty() || budget == 0) return;
    std::size_t missed = 0;
    // Where the coarse faces miss the field: their middles off the surface.
    const double block = 6 * coarse, miss = 0.1 * coarse;
    const V3 origin = f.bb.lo;
    struct K {
        int x, y, z;
        bool operator<(const K& o) const { return x != o.x ? x < o.x : y != o.y ? y < o.y : z < o.z; }
    };
    std::map<K, int> flagged;
    for (std::size_t t = 0; t + 2 < g.t.size(); t += 3) {
        const V3 c = (g.p[std::size_t(g.t[t])] + g.p[std::size_t(g.t[t + 1])] + g.p[std::size_t(g.t[t + 2])]) * (1.0 / 3);
        if (std::abs(f.d(c)) <= miss) continue;
        ++missed;
        flagged[{int(std::floor((c.x - origin.x) / block)), int(std::floor((c.y - origin.y) / block)), int(std::floor((c.z - origin.z) / block))}] = -1;
    }
    // Missed nearly everywhere (a figure, all folds), there is nowhere to
    // spend more than anywhere else: the cell it is is the cell it gets.
    if (flagged.empty() || double(missed) > 0.4 * double(g.t.size() / 3)) return;
    // Blocks that touch are one place: one box round each.
    std::vector<Box> boxes;
    for (auto& [k, comp] : flagged) {
        if (comp >= 0) continue;
        const int id = int(boxes.size());
        Box b;
        std::vector<K> todo{k};
        comp = id;
        while (!todo.empty()) {
            const K q = todo.back();
            todo.pop_back();
            b.grow(origin + V3{q.x * block, q.y * block, q.z * block});
            b.grow(origin + V3{(q.x + 1) * block, (q.y + 1) * block, (q.z + 1) * block});
            for (int dx = -1; dx <= 1; ++dx)
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dz = -1; dz <= 1; ++dz) {
                        auto it = flagged.find({q.x + dx, q.y + dy, q.z + dz});
                        if (it != flagged.end() && it->second < 0) it->second = id, todo.push_back(it->first);
                    }
        }
        b.lo = b.lo - V3{coarse, coarse, coarse}, b.hi = b.hi + V3{coarse, coarse, coarse};
        boxes.push_back(b);
    }
    // Boxes that overlap after all are one box.
    for (bool merged = true; merged;) {
        merged = false;
        for (std::size_t i = 0; i < boxes.size() && !merged; ++i)
            for (std::size_t j = i + 1; j < boxes.size() && !merged; ++j)
                if (boxes[i].overlaps(boxes[j], 2 * coarse)) boxes[i].add(boxes[j]), boxes.erase(boxes.begin() + long(j)), merged = true;
    }
    // As fine as the budget allows there: what the boxes hold made again
    // with about the budget's faces before they are made fewer - and
    // never finer than a quarter of the cell.
    std::size_t held = 0;
    for (std::size_t t = 0; t + 2 < g.t.size(); t += 3) {
        const V3 c = (g.p[std::size_t(g.t[t])] + g.p[std::size_t(g.t[t + 1])] + g.p[std::size_t(g.t[t + 2])]) * (1.0 / 3);
        for (const Box& b : boxes)
            if (b.overlaps(Box{c, c}, 0)) {
                ++held;
                break;
            }
    }
    const double fine = std::max(coarse * 0.25, coarse * std::sqrt(double(held) / double(budget)));
    if (fine > coarse * 0.7) return;
    for (const Box& b : boxes) {
        Geom inner;
        const Box grid = surface_in(f, b, fine, max_grid, inner, nullptr);
        Geom out;
        if (zip(clip_outside(g, grid, 0), inner, grid, coarse, out)) g = std::move(out);
    }
}

}  // namespace sg::sculpt::kernel
