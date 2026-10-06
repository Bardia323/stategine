// Ways across the land: a road levelled to its own smoothed profile (as an
// engineer would grade it: cut through the rises, built up over the dips) and
// banked back into the land beside it; a path likewise, narrower and gentler;
// a river cut downhill, its bed deepest in the middle, its water's level
// falling along it.
#include <algorithm>
#include <cmath>

#include "TerrainKernel.hpp"

namespace sg::terrain::kernel {

namespace {

// The heights along a curve, averaged over `window` metres of it.
std::vector<double> profile(const Land& L, const Curve& c, double window) {
    std::vector<double> raw(c.p.size()), out(c.p.size());
    for (std::size_t k = 0; k < c.p.size(); ++k) raw[k] = L.height(c.p[k].x, c.p[k].z);
    std::size_t a = 0, b = 0;
    double sum = 0;
    for (std::size_t k = 0; k < c.p.size(); ++k) {
        while (b < c.p.size() && c.s[b] <= c.s[k] + window * 0.5) sum += raw[b++];
        while (a < b && c.s[a] < c.s[k] - window * 0.5) sum -= raw[a++];
        out[k] = b > a ? sum / double(b - a) : raw[k];
    }
    return out;
}

// Each point of the land within `reach` of the curve: how far from it, and
// the curve's own height (c.p[].y) where it is nearest.
template <class F>
void near_curve(Land& L, const Curve& c, double reach, F f) {
    std::vector<float> best(L.h.size(), 1e30f), level(L.h.size(), 0.0f);
    for (std::size_t k = 0; k + 1 < c.p.size(); ++k) {
        const Vec3d a = c.p[k], b = c.p[k + 1];
        const int i0 = std::max(0, int((std::min(a.x, b.x) - reach - L.x0) / L.cell)), i1 = std::min(L.nx, int((std::max(a.x, b.x) + reach - L.x0) / L.cell) + 1);
        const int j0 = std::max(0, int((std::min(a.z, b.z) - reach - L.z0) / L.cell)), j1 = std::min(L.nz, int((std::max(a.z, b.z) + reach - L.z0) / L.cell) + 1);
        const double dx = b.x - a.x, dz = b.z - a.z, ll = dx * dx + dz * dz;
        for (int j = j0; j <= j1; ++j)
            for (int i = i0; i <= i1; ++i) {
                const double x = L.x0 + i * L.cell, z = L.z0 + j * L.cell;
                const double t = ll > 0 ? std::clamp(((x - a.x) * dx + (z - a.z) * dz) / ll, 0.0, 1.0) : 0.0;
                const double dist = std::hypot(x - (a.x + dx * t), z - (a.z + dz * t));
                const std::size_t q = L.index(i, j);
                if (dist < best[q]) best[q] = float(dist), level[q] = float(a.y + (b.y - a.y) * t);
            }
    }
    for (std::size_t q = 0; q < L.h.size(); ++q)
        if (best[q] < reach) f(q, double(best[q]), double(level[q]));
}

}  // namespace

void level_way(Land& L, Curve& c, double width, double bank, double sink, double smooth_m) {
    if (c.p.size() < 2) return;
    const std::vector<double> pr = profile(L, c, smooth_m);
    for (std::size_t k = 0; k < c.p.size(); ++k) c.p[k].y = pr[k];
    const double half = width * 0.5;
    near_curve(L, c, half + bank, [&](std::size_t q, double dist, double level) {
        // Flat across the way and a shoulder beside it; blended back to the
        // land over the bank.
        const double way = level - sink;
        const double t = smoothstep(half + 0.4, half + bank, dist);
        L.h[q] = float(way + (L.h[q] - way) * t);
        L.road[q] = std::min(L.road[q], float(dist - half));
    });
}

void cut_river(Land& L, Curve& c, double width, double depth, std::vector<double>& surface) {
    if (c.p.size() < 2) return;
    // Water runs downhill: the profile never rises along the way it flows.
    std::vector<double> pr = profile(L, c, 12);
    for (std::size_t k = 1; k < pr.size(); ++k) pr[k] = std::min(pr[k], pr[k - 1]);
    const double half = width * 0.5, bank = width * 0.8;
    for (std::size_t k = 0; k < c.p.size(); ++k) c.p[k].y = pr[k];
    near_curve(L, c, half + bank, [&](std::size_t q, double dist, double level) {
        double cut;
        if (dist < half) {
            const double r = dist / half;
            cut = level - depth * (1 - r * r);
        } else {
            // The banks come down to the water's edge, and no lower.
            cut = level + (L.h[q] - level) * smoothstep(half, half + bank, dist);
        }
        L.h[q] = float(std::min(double(L.h[q]), cut));
    });
    surface.resize(pr.size());
    for (std::size_t k = 0; k < pr.size(); ++k) surface[k] = pr[k] - depth * 0.3;
}

Road ribbon(const Land& L, const Curve& c, double width) {
    Road r;
    r.width = width;
    r.lo = {1e30, 1e30, 1e30}, r.hi = {-1e30, -1e30, -1e30};
    const std::size_t n = c.p.size();
    if (n < 2) return r;
    std::vector<Vec3d> rows[3];
    for (std::size_t k = 0; k < n; ++k) {
        const Vec3d a = c.p[k == 0 ? 0 : k - 1], b = c.p[std::min(n - 1, k + 1)];
        double tx = b.x - a.x, tz = b.z - a.z;
        const double l = std::hypot(tx, tz);
        tx /= l, tz /= l;
        const Vec3d side{-tz, 0, tx};
        for (int s = 0; s < 3; ++s) {
            Vec3d p = c.p[k] + side * ((s - 1) * width * 0.5);
            // Just above the levelled ground, so the ground never shows through.
            p.y = L.height(p.x, p.z) + 0.06;
            rows[s].push_back(p);
            r.lo = {std::min(r.lo.x, p.x), std::min(r.lo.y, p.y), std::min(r.lo.z, p.z)};
            r.hi = {std::max(r.hi.x, p.x), std::max(r.hi.y, p.y), std::max(r.hi.z, p.z)};
        }
        r.centre.push_back(rows[1].back());
    }
    const auto corner = [&](const Vec3d& p, const Vec3d& nrm, double u, double v) {
        r.corners.insert(r.corners.end(), {float(p.x), float(p.y), float(p.z), float(nrm.x), float(nrm.y), float(nrm.z), float(u), float(v)});
    };
    for (std::size_t k = 0; k + 1 < n; ++k)
        for (int s = 0; s < 2; ++s) {
            const Vec3d A = rows[s][k], B = rows[s + 1][k], C = rows[s + 1][k + 1], D = rows[s][k + 1];
            Vec3d nrm = cross(C - A, B - A);
            if (nrm.y < 0) nrm = nrm * -1.0;
            const double l = std::sqrt(dot(nrm, nrm));
            nrm = l > 0 ? nrm * (1 / l) : Vec3d{0, 1, 0};
            const double u0 = s * 0.5, u1 = u0 + 0.5, v0 = c.s[k], v1 = c.s[k + 1];
            // Turned to face up: A, C, B and A, D, C or the other way, as the curve runs.
            const bool up = cross(B - A, C - A).y > 0;
            if (up) {
                corner(A, nrm, u0, v0), corner(B, nrm, u1, v0), corner(C, nrm, u1, v1);
                corner(A, nrm, u0, v0), corner(C, nrm, u1, v1), corner(D, nrm, u0, v1);
            } else {
                corner(A, nrm, u0, v0), corner(C, nrm, u1, v1), corner(B, nrm, u1, v0);
                corner(A, nrm, u0, v0), corner(D, nrm, u0, v1), corner(C, nrm, u1, v1);
            }
        }
    return r;
}

}  // namespace sg::terrain::kernel
