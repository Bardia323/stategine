// What covers the land and what stands on it: each layer where its ranges say
// (height, slope, wetness, nearness to road and water), broken up by noise so
// its edge is ragged, laid over those before it; and each thing strewn by the
// same rules - a jittered grid, so they neither clump nor line up.
#include <algorithm>
#include <cmath>

#include "TerrainKernel.hpp"

namespace sg::terrain::kernel {

namespace {

// How much the rules hold at a point: each range given, softened at its ends.
double holds(const Land& L, const Rules& r, double x, double z, double h, double slope_deg, double wet, double road, double water,
             const Octaves& o) {
    const double sh = r.soft >= 0 ? r.soft : 2.0, ss = r.soft >= 0 ? r.soft : 5.0, sw = r.soft >= 0 ? r.soft * 0.05 : 0.1,
                 sd = r.soft >= 0 ? r.soft : 0.5;
    double a = 1;
    if (r.height.given) a *= within(h, r.height.lo, r.height.hi, sh);
    if (r.slope.given) a *= within(slope_deg, r.slope.lo, r.slope.hi, ss);
    if (r.wet.given) a *= within(wet, r.wet.lo, r.wet.hi, sw);
    if (r.road.given) a *= within(road, r.road.lo, r.road.hi, sd);
    if (r.water.given) a *= within(water, r.water.lo, r.water.hi, sd);
    if (a <= 0) return 0;
    const double sc = std::max(r.scale, 0.1);
    if (r.cover < 1) {
        // In patches: where the noise is in its top `cover` (fbm gathers
        // about a half, so its quantiles are near it).
        const double n = fbm(x / sc + 31.7, z / sc - 12.9, o), t = 0.5 + (0.5 - std::clamp(r.cover, 0.0, 1.0)) * 0.36;
        a *= smoothstep(t - 0.035, t + 0.035, n);
    }
    if (r.noise > 0) {
        // Ragged: the edge where the rules fade moved in and out by the noise.
        const double n = fbm(x / sc, z / sc, o);
        a = smoothstep(0.3, 0.7, a + (n - 0.5) * 2 * r.noise);
    }
    (void)L;
    return a;
}

}  // namespace

void cover(Land& L, const std::vector<LayerAsk>& asks, uint32_t seed) {
    if (asks.empty()) {
        LayerAsk g;
        g.layer.name = "grass";
        L.layers.push_back(g.layer);
    }
    for (const LayerAsk& a : asks) L.layers.push_back(a.layer);
    const std::size_t n = L.h.size();
    L.splat.assign(n * 4, 0);
    for (int j = 0; j <= L.nz; ++j)
        for (int i = 0; i <= L.nx; ++i) {
            const std::size_t q = L.index(i, j);
            const double x = L.x0 + i * L.cell, z = L.z0 + j * L.cell;
            const double slope = std::atan(L.slope(x, z)) * 180 / kPi;
            double share[4] = {1, 0, 0, 0};
            for (std::size_t k = 1; k < asks.size(); ++k) {
                Octaves o;
                o.oct = 4, o.seed = seed * 97u + uint32_t(k) * 7919u;
                const double a = holds(L, asks[k].rules, x, z, L.h[q], slope, L.wet[q], L.road[q], L.water[q], o);
                for (std::size_t m = 0; m < k; ++m) share[m] *= 1 - a;
                share[k] = a;
            }
            for (int c = 0; c < 4; ++c) L.splat[q * 4 + std::size_t(c)] = (unsigned char)std::lround(std::clamp(share[c], 0.0, 1.0) * 255);
        }
}

void strew(Land& L, const std::vector<ScatterAsk>& asks) {
    for (const ScatterAsk& s : asks) {
        int thing = -1;
        for (std::size_t k = 0; k < L.things.size(); ++k)
            if (L.things[k].name == s.thing) thing = int(k);
        if (thing < 0) {
            L.errors += "scatter: no thing called " + s.thing + " (say it with `thing` first)\n";
            continue;
        }
        const int variants = int(L.things[std::size_t(thing)].recipes.size());
        Dice dice(s.seed);
        Octaves o;
        o.oct = 4, o.seed = s.seed + 11u;
        // How many to a hundred square metres: a grid that size, each cell's
        // one tried at a random place within it.
        double density = s.density;
        if (s.count > 0) density = s.count * 100.0 / std::max(1.0, L.w * L.d) * 3.0;
        const double step = std::max(s.spacing, std::sqrt(100.0 / std::max(density, 1e-6)));
        const double keep = std::min(1.0, density * step * step / 100.0);
        int made = 0;
        const int most = s.count > 0 ? s.count : s.most;
        for (double z = L.z0 + step * 0.5; z < L.z0 + L.d && made < most; z += step)
            for (double x = L.x0 + step * 0.5; x < L.x0 + L.w && made < most; x += step) {
                const double px = x + dice.spread(step * 0.45), pz = z + dice.spread(step * 0.45);
                const double roll = dice.next(), pick = dice.next(), yaw = dice.next() * 2 * kPi, size = dice.next();
                const double road = L.at(L.road, px, pz), water = L.at(L.water, px, pz);
                // Never in the water or on a road unless the rules ask for it.
                if (!s.rules.water.given && water < 0.5) continue;
                if (!s.rules.road.given && road < 1.0) continue;
                const double slope = std::atan(L.slope(px, pz)) * 180 / kPi;
                const double a = holds(L, s.rules, px, pz, L.height(px, pz), slope, L.at(L.wet, px, pz), road, water, o);
                if (roll >= a * keep) continue;
                Placed p;
                p.thing = thing, p.variant = std::min(variants - 1, int(pick * variants));
                p.scale = s.lo + (s.hi - s.lo) * size, p.yaw = yaw;
                p.at = {px, L.height(px, pz) - s.sink * p.scale, pz};
                L.placed.push_back(p);
                ++made;
            }
        if (s.count > 0 && made < s.count) L.errors += "scatter " + s.thing + ": room for " + std::to_string(made) + " of " + std::to_string(s.count) + "\n";
    }
}

}  // namespace sg::terrain::kernel
