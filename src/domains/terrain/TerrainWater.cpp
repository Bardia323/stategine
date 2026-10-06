// Water on the land. A lake fills the hollow it is asked in up to the level
// where it would spill (the lowest rim on the way out - found by flooding
// from the hollow lowest-first until the flood reaches the edge), or to the
// level it is given; the sea lies over everything below its level; a swamp's
// pools are its hollows below its level; a river's water follows its bed.
// Each is a surface (a flat one coarse, as it is flat: what it covers past
// its shore is under the land and never seen) and a map of how deep it is,
// for its colour. Then every point's distance from the water - negative in it.
#include <algorithm>
#include <cmath>
#include <queue>

#include "TerrainKernel.hpp"

namespace sg::terrain::kernel {

namespace {

constexpr float kNone = -1e30f;

// Chamfer distance (3-4) from every point where `seed` holds, in metres.
std::vector<float> distance_from(const Land& L, const std::vector<char>& seed) {
    const float big = 1e6f, a = float(L.cell), b = float(L.cell * std::sqrt(2.0));
    std::vector<float> d(seed.size());
    for (std::size_t k = 0; k < d.size(); ++k) d[k] = seed[k] ? 0.0f : big;
    for (int j = 0; j <= L.nz; ++j)
        for (int i = 0; i <= L.nx; ++i) {
            float& v = d[L.index(i, j)];
            if (i > 0) v = std::min(v, d[L.index(i - 1, j)] + a);
            if (j > 0) v = std::min(v, d[L.index(i, j - 1)] + a);
            if (i > 0 && j > 0) v = std::min(v, d[L.index(i - 1, j - 1)] + b);
            if (i < L.nx && j > 0) v = std::min(v, d[L.index(i + 1, j - 1)] + b);
        }
    for (int j = L.nz; j >= 0; --j)
        for (int i = L.nx; i >= 0; --i) {
            float& v = d[L.index(i, j)];
            if (i < L.nx) v = std::min(v, d[L.index(i + 1, j)] + a);
            if (j < L.nz) v = std::min(v, d[L.index(i, j + 1)] + a);
            if (i < L.nx && j < L.nz) v = std::min(v, d[L.index(i + 1, j + 1)] + b);
            if (i > 0 && j < L.nz) v = std::min(v, d[L.index(i - 1, j + 1)] + b);
        }
    return d;
}

int clampi(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }

}  // namespace

void settle_water(Land& L, const std::vector<WaterAsk>& asks, std::vector<float>& swamp) {
    (void)swamp;
    const std::size_t n = L.h.size();
    std::vector<float> level(n, kNone);
    std::vector<int> body(n, -1);
    std::vector<WaterAsk> kept;
    const auto mark = [&](std::size_t q, double lv, int id) {
        if (double(L.h[q]) < lv) level[q] = float(lv), body[q] = id;
    };
    for (const WaterAsk& w : asks) {
        const int id = int(kept.size());
        bool any = false;
        if (w.kind == WaterAsk::Sea) {
            for (std::size_t q = 0; q < n; ++q)
                if (L.h[q] < w.level) mark(q, w.level, id), any = true;
        } else if (w.kind == WaterAsk::Swamp) {
            for (int j = 0; j <= L.nz; ++j)
                for (int i = 0; i <= L.nx; ++i) {
                    const double x = L.x0 + i * L.cell, z = L.z0 + j * L.cell;
                    if (std::hypot(x - w.x, z - w.z) > w.r) continue;
                    const std::size_t q = L.index(i, j);
                    if (L.h[q] < w.level) mark(q, w.level, id), any = true;
                }
        } else if (w.kind == WaterAsk::River) {
            // The water along the bed, at the river's level where it is nearest.
            const Curve& c = w.along;
            const double reach = w.width * 0.5 + L.cell;
            for (std::size_t k = 0; k + 1 < c.p.size(); ++k) {
                const Vec3d a = c.p[k], b = c.p[k + 1];
                const int i0 = clampi(int((std::min(a.x, b.x) - reach - L.x0) / L.cell), 0, L.nx), i1 = clampi(int((std::max(a.x, b.x) + reach - L.x0) / L.cell) + 1, 0, L.nx);
                const int j0 = clampi(int((std::min(a.z, b.z) - reach - L.z0) / L.cell), 0, L.nz), j1 = clampi(int((std::max(a.z, b.z) + reach - L.z0) / L.cell) + 1, 0, L.nz);
                const double dx = b.x - a.x, dz = b.z - a.z, ll = dx * dx + dz * dz;
                for (int j = j0; j <= j1; ++j)
                    for (int i = i0; i <= i1; ++i) {
                        const double x = L.x0 + i * L.cell, z = L.z0 + j * L.cell;
                        const double t = ll > 0 ? std::clamp(((x - a.x) * dx + (z - a.z) * dz) / ll, 0.0, 1.0) : 0.0;
                        if (std::hypot(x - (a.x + dx * t), z - (a.z + dz * t)) > reach) continue;
                        const double lv = w.surface[k] + (w.surface[k + 1] - w.surface[k]) * t;
                        const std::size_t q = L.index(i, j);
                        if (body[q] == id && level[q] >= lv) continue;
                        mark(q, lv, id), any = any || double(L.h[q]) < lv;
                    }
            }
        } else {
            // A lake: flood from the hollow, lowest first, until the edge.
            const int si = clampi(int(std::lround((w.x - L.x0) / L.cell)), 0, L.nx), sj = clampi(int(std::lround((w.z - L.z0) / L.cell)), 0, L.nz);
            double lv = w.level;
            if (!w.level_given) {
                using Item = std::pair<float, std::size_t>;
                std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
                std::vector<char> seen(n, 0);
                const std::size_t s0 = L.index(si, sj);
                pq.push({L.h[s0], s0}), seen[s0] = 1;
                double run = -1e30;
                lv = L.h[s0];
                while (!pq.empty()) {
                    const auto [hh, q] = pq.top();
                    pq.pop();
                    run = std::max(run, double(hh));
                    const int i = int(q % std::size_t(L.nx + 1)), j = int(q / std::size_t(L.nx + 1));
                    if (i == 0 || j == 0 || i == L.nx || j == L.nz) {
                        lv = run - 0.05;
                        break;
                    }
                    static const int di[4] = {1, -1, 0, 0}, dj[4] = {0, 0, 1, -1};
                    for (int k = 0; k < 4; ++k) {
                        const std::size_t r = L.index(i + di[k], j + dj[k]);
                        if (!seen[r]) seen[r] = 1, pq.push({L.h[r], r});
                    }
                }
            }
            // What is connected to the hollow below the level.
            std::vector<std::size_t> todo{L.index(si, sj)};
            std::vector<char> in(n, 0);
            if (double(L.h[todo[0]]) < lv) in[todo[0]] = 1;
            else todo.clear();
            while (!todo.empty()) {
                const std::size_t q = todo.back();
                todo.pop_back();
                mark(q, lv, id), any = true;
                const int i = int(q % std::size_t(L.nx + 1)), j = int(q / std::size_t(L.nx + 1));
                static const int di[4] = {1, -1, 0, 0}, dj[4] = {0, 0, 1, -1};
                for (int k = 0; k < 4; ++k) {
                    const int a = i + di[k], b = j + dj[k];
                    if (a < 0 || b < 0 || a > L.nx || b > L.nz) continue;
                    const std::size_t r = L.index(a, b);
                    if (!in[r] && double(L.h[r]) < lv) in[r] = 1, todo.push_back(r);
                }
            }
            if (!any) L.errors += "lake " + w.name + ": no hollow at " + std::to_string(w.x) + "," + std::to_string(w.z) + " to hold water\n";
        }
        WaterAsk k = w;
        kept.push_back(k);
        (void)any;
    }

    // How far from the water every point is: out of it positive, in it negative.
    std::vector<char> wet(n), dry(n);
    for (std::size_t q = 0; q < n; ++q) wet[q] = body[q] >= 0, dry[q] = !wet[q];
    const std::vector<float> out = distance_from(L, wet), in = distance_from(L, dry);
    for (std::size_t q = 0; q < n; ++q) L.water[q] = wet[q] ? -in[q] : out[q];

    // Each body's surface and its depths.
    for (std::size_t id = 0; id < kept.size(); ++id) {
        const WaterAsk& w = kept[id];
        int i0 = L.nx, i1 = 0, j0 = L.nz, j1 = 0;
        double deepest = 0;
        for (int j = 0; j <= L.nz; ++j)
            for (int i = 0; i <= L.nx; ++i) {
                const std::size_t q = L.index(i, j);
                if (body[q] != int(id)) continue;
                i0 = std::min(i0, i), i1 = std::max(i1, i), j0 = std::min(j0, j), j1 = std::max(j1, j);
                deepest = std::max(deepest, double(level[q] - L.h[q]));
            }
        if (i0 > i1) continue;
        // Past its shore, under the land: two points round.
        i0 = std::max(0, i0 - 2), j0 = std::max(0, j0 - 2), i1 = std::min(L.nx, i1 + 2), j1 = std::min(L.nz, j1 + 2);
        Water wa;
        wa.name = w.name, wa.shallow = w.shallow, wa.deep = w.deep, wa.deepest = std::max(deepest, 0.5);
        wa.flowing = w.kind == WaterAsk::River;
        wa.lo = {L.x0 + i0 * L.cell, 1e30, L.z0 + j0 * L.cell}, wa.hi = {L.x0 + i1 * L.cell, -1e30, L.z0 + j1 * L.cell};
        // The level at every point of its box: where it is, its own; round it, its nearest's.
        const int bw = i1 - i0 + 1, bh = j1 - j0 + 1;
        std::vector<float> lv(std::size_t(bw) * std::size_t(bh), kNone);
        for (int j = j0; j <= j1; ++j)
            for (int i = i0; i <= i1; ++i)
                if (body[L.index(i, j)] == int(id)) lv[std::size_t(j - j0) * std::size_t(bw) + std::size_t(i - i0)] = level[L.index(i, j)];
        for (int pass = 0; pass < 3; ++pass) {
            std::vector<float> next = lv;
            for (int j = 0; j < bh; ++j)
                for (int i = 0; i < bw; ++i) {
                    float& v = next[std::size_t(j) * std::size_t(bw) + std::size_t(i)];
                    if (v != kNone) continue;
                    double s = 0;
                    int c = 0;
                    for (int dj = -1; dj <= 1; ++dj)
                        for (int di = -1; di <= 1; ++di) {
                            const int a = i + di, b = j + dj;
                            if (a < 0 || b < 0 || a >= bw || b >= bh) continue;
                            const float u = lv[std::size_t(b) * std::size_t(bw) + std::size_t(a)];
                            if (u != kNone) s += u, ++c;
                        }
                    if (c) v = float(s / c);
                }
            lv.swap(next);
        }
        // Its surface: quads of `step` points, where it is or reaches.
        const int step = std::max(1, int(std::ceil(std::sqrt(double(bw) * bh / 30000.0))));
        const bool flat = w.kind != WaterAsk::River;
        const auto level_at = [&](int i, int j) {
            i = clampi(i, i0, i1), j = clampi(j, j0, j1);
            return double(lv[std::size_t(j - j0) * std::size_t(bw) + std::size_t(i - i0)]);
        };
        const auto put = [&](double x, double y, double z) {
            const float u = float((x - wa.lo.x) / std::max(1e-6, wa.hi.x - wa.lo.x)), v = float((z - wa.lo.z) / std::max(1e-6, wa.hi.z - wa.lo.z));
            wa.corners.insert(wa.corners.end(), {float(x), float(y), float(z), 0.0f, 1.0f, 0.0f, u, v});
            wa.lo.y = std::min(wa.lo.y, y), wa.hi.y = std::max(wa.hi.y, y);
        };
        for (int j = j0; j < j1; j += step)
            for (int i = i0; i < i1; i += step) {
                const int ie = std::min(i + step, i1), je = std::min(j + step, j1);
                bool reached = false;
                double at[4] = {0, 0, 0, 0};
                for (int b = j; b <= je && !reached; ++b)
                    for (int a = i; a <= ie && !reached; ++a) reached = lv[std::size_t(b - j0) * std::size_t(bw) + std::size_t(a - i0)] != kNone;
                if (!reached) continue;
                const int ci[4] = {i, ie, ie, i}, cj[4] = {j, j, je, je};
                double mean = 0;
                int known = 0;
                for (int k = 0; k < 4; ++k) {
                    const double v = level_at(ci[k], cj[k]);
                    if (v > kNone * 0.5) mean += v, ++known;
                }
                mean = known ? mean / known : 0;
                for (int k = 0; k < 4; ++k) {
                    const double v = level_at(ci[k], cj[k]);
                    at[k] = v > kNone * 0.5 ? v : mean;
                    if (flat) at[k] = mean;
                }
                const double x0 = L.x0 + i * L.cell, x1 = L.x0 + ie * L.cell, z0 = L.z0 + j * L.cell, z1 = L.z0 + je * L.cell;
                // Faces up: (x0,z0) (x0,z1) (x1,z1), and (x0,z0) (x1,z1) (x1,z0).
                put(x0, at[0], z0), put(x0, at[3], z1), put(x1, at[2], z1);
                put(x0, at[0], z0), put(x1, at[2], z1), put(x1, at[1], z0);
            }
        if (flat) {
            // One level: the mean of what it holds, everywhere on it.
            double s = 0;
            int c = 0;
            for (std::size_t q = 0; q < n; ++q)
                if (body[q] == int(id)) s += level[q], ++c;
            const float lvl = float(c ? s / c : 0);
            for (std::size_t k = 1; k < wa.corners.size(); k += 8) wa.corners[k] = lvl;
            wa.lo.y = wa.hi.y = lvl;
        }
        // How deep, over its box.
        wa.map_w = std::min(bw, 1024), wa.map_h = std::min(bh, 1024);
        wa.depth.assign(std::size_t(wa.map_w) * std::size_t(wa.map_h) * 4, 255);
        for (int y = 0; y < wa.map_h; ++y)
            for (int x = 0; x < wa.map_w; ++x) {
                const double px = wa.lo.x + (wa.hi.x - wa.lo.x) * (x + 0.5) / wa.map_w, pz = wa.lo.z + (wa.hi.z - wa.lo.z) * (y + 0.5) / wa.map_h;
                const int i = clampi(int(std::lround((px - L.x0) / L.cell)), i0, i1), j = clampi(int(std::lround((pz - L.z0) / L.cell)), j0, j1);
                const double lvl = flat ? wa.lo.y : level_at(i, j);
                const double dd = std::clamp((lvl - L.height(px, pz)) / wa.deepest, 0.0, 1.0);
                unsigned char* px4 = &wa.depth[(std::size_t(y) * std::size_t(wa.map_w) + std::size_t(x)) * 4];
                px4[0] = (unsigned char)std::lround(dd * 255), px4[1] = px4[2] = 0, px4[3] = 255;
            }
        L.waters.push_back(std::move(wa));
    }
}

}  // namespace sg::terrain::kernel
