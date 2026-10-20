#include "sg/domains/Grounds.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

#include "sg/core/Cache.hpp"
#include "sg/domains/Distribution.hpp"
#include "sg/domains/Texture.hpp"

#include "SG_GROUNDS_CODE.hpp"

namespace sg::grounds {

namespace {

constexpr double kPi = 3.14159265358979323846;

uint64_t mix64(uint64_t h) {
    h ^= h >> 33, h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33, h *= 0xc4ceb9fe1a85ec53ULL;
    return h ^ (h >> 33);
}
double u01(uint64_t h) { return static_cast<double>(mix64(h) >> 11) * (1.0 / 9007199254740992.0); }
double smooth(double a, double b, double x) {
    const double t = std::clamp((x - a) / (b - a), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}
int wrap(int i, int n) { return ((i % n) + n) % n; }

// Value noise of the plane, in metres, tiling every `T` metres: features
// about `scale` across (as many cells across the tile as fit whole).
double vnoise(double x, double z, double scale, double T, uint64_t seed) {
    const int n = std::max(1, static_cast<int>(std::lround(T / scale)));
    const double fx = x / T * n, fz = z / T * n;
    const int x0 = static_cast<int>(std::floor(fx)), z0 = static_cast<int>(std::floor(fz));
    const double tx = fx - x0, tz = fz - z0;
    const double sx = tx * tx * tx * (tx * (tx * 6 - 15) + 10), sz = tz * tz * tz * (tz * (tz * 6 - 15) + 10);
    const auto at = [&](int i, int j) { return u01(seed ^ (static_cast<uint64_t>(wrap(i, n)) * 0x9e3779b97f4a7c15ULL) ^ (static_cast<uint64_t>(wrap(j, n)) * 0xc2b2ae3d27d4eb4fULL)); };
    const double a = at(x0, z0), b = at(x0 + 1, z0), c = at(x0, z0 + 1), d = at(x0 + 1, z0 + 1);
    return (a + (b - a) * sx) * (1 - sz) + (c + (d - c) * sx) * sz;
}
// Its octaves, each half the size and half as strong: 0..1.
double fbm(double x, double z, double scale, double T, uint64_t seed, int octaves = 4) {
    double s = 0, w = 1, t = 0;
    for (int o = 0; o < octaves; ++o, w *= 0.5, scale *= 0.5) s += w * vnoise(x, z, scale, T, seed + 131 * o), t += w;
    return s / t;
}

// A value law of the Distribution domain, by its numbers.
Law law(const char* what, std::initializer_list<std::pair<const char*, double>> numbers) {
    Law l;
    l.what = what;
    for (const auto& [k, v] : numbers) l.p.set(Key{k}, v);
    return l;
}

// Every thing of one strewing near (x, z): the things of its cell and of the
// eight round it - each cell holding a Poisson number of them (`mean`), at
// places drawn uniformly in it (a Poisson process), the whole tiling every
// `T` metres. A thing no larger than its cell is found wherever it reaches.
// `f(dx, dz, id)`: how far the point is from the thing, and the thing's own
// number for its draws.
template <class F>
void near_things(double x, double z, double T, double cell, double mean, uint64_t seed, F&& f) {
    const int n = std::max(1, static_cast<int>(std::lround(T / cell)));
    const double c = T / n;
    const int ci = static_cast<int>(std::floor(x / c)), cj = static_cast<int>(std::floor(z / c));
    const double none = std::exp(-mean);
    for (int dj = -1; dj <= 1; ++dj)
        for (int di = -1; di <= 1; ++di) {
            const int i = ci + di, j = cj + dj;
            const uint64_t base = mix64(seed * 0x2545f4914f6cdd1dULL ^ (static_cast<uint64_t>(wrap(i, n)) * 0x9e3779b97f4a7c15ULL) ^
                                        (static_cast<uint64_t>(wrap(j, n)) * 0x165667b19e3779f9ULL));
            // (How many: Poisson's, by inverting its distribution.)
            const double u = u01(base);
            double p = none, cdf = none;
            int k = 0;
            while (u > cdf && k < 64) ++k, p *= mean / k, cdf += p;
            for (int e = 0; e < k; ++e) {
                const uint64_t id = mix64(base + 0x632be59bd9b4e019ULL * static_cast<uint64_t>(e + 1));
                const double px = (i + u01(id ^ 0x51)) * c, pz = (j + u01(id ^ 0x77)) * c;
                f(x - px, z - pz, id, px, pz);
            }
        }
}

// A point of a ground: how high (metres), its colour (as seen), how rough,
// how open to the sky.
struct Pt {
    double h = 0, r = 0, g = 0, b = 0, rough = 0.9, open = 1;
};
// A layer's best thing at a point - the one standing highest there - laid
// over what is under it: its height added where it covers, its colour over.
struct Top {
    double h = 0, a = 0, r = 0, g = 0, b = 0, rough = 0.9;
    void offer(double hh, double aa, double rr, double gg, double bb, double ro) {
        if (aa <= 0 || hh * aa < h * a) return;
        h = hh, a = aa, r = rr, g = gg, b = bb, rough = ro;
    }
};
void lay(Pt& s, const Top& t) {
    if (t.a <= 0) return;
    s.h += t.h * t.a;
    s.r += (t.r - s.r) * t.a, s.g += (t.g - s.g) * t.a, s.b += (t.b - s.b) * t.a;
    s.rough += (t.rough - s.rough) * t.a;
}

// A capsule's distance across, `dx, dz` from its foot, laid `len` along `angle`,
// bent by `bend` (metres off its line at its tip), tapering to `tip` of its
// width: how far across its half-width (0 on its line, 1 at its edge) and how
// far along (0..1); beyond its end, more than 1 across.
void across_blade(double dx, double dz, double len, double angle, double width, double bend, double tip, double& across, double& along) {
    const double cx = std::cos(angle), cz = std::sin(angle);
    const double a = dx * cx + dz * cz, q = -dx * cz + dz * cx;
    const double t = std::clamp(a / len, 0.0, 1.0);
    const double off = q - bend * t * t;
    const double w = width * 0.5 * (1.0 - (1.0 - tip) * t);
    const double over = a < 0 ? -a : a > len ? a - len : 0.0;
    across = std::hypot(off, over) / std::max(w, 1e-6);
    along = t;
}

// --- the laws of their sizes ----------------------------------------------------------
// (Pebbles and stones: a power law, many small and few large - as clasts are
// measured, index about 2.5; moss cushions and grass blades: lognormal, a
// typical size and a spread round it, as plant parts grow.)
const Law& pebble_size() {
    static const Law l = law("power", {{"lo", 0.003}, {"hi", 0.022}, {"alpha", 2.5}});
    return l;
}
const Law& stone_size() {
    static const Law l = law("power", {{"lo", 0.004}, {"hi", 0.04}, {"alpha", 2.3}});
    return l;
}
const Law& cushion_size() {
    static const Law l = law("lognormal", {{"median", 0.03}, {"spread", 0.45}});
    return l;
}
const Law& blade_length() {
    static const Law l = law("lognormal", {{"median", 0.06}, {"spread", 0.35}});
    return l;
}
// (Blades combed one way - by the wind, by water running down, by what
// walked through - their ways gathered round it as a von Mises law.)
Law blade_way(double kappa) { return law("vonmises", {{"mean", 0.0}, {"kappa", kappa}}); }
const Law& lichen_size() {
    static const Law l = law("power", {{"lo", 0.003}, {"hi", 0.07}, {"alpha", 2.2}});
    return l;
}

// --- the layers ------------------------------------------------------------------------

// Soil: brown, finely lumpy.
void soil(Pt& s, double x, double z, double T, uint64_t seed, double r, double g, double b) {
    const double broad = fbm(x, z, 0.25, T, seed + 1), fine = fbm(x, z, 0.012, T, seed + 2, 3);
    s.h = 0.006 * broad + 0.003 * fine;
    const double k = 0.78 + 0.3 * broad + 0.16 * (fine - 0.5);
    s.r = r * k, s.g = g * k, s.b = b * k, s.rough = 0.95;
}

// Pebbles lying in it, of a power law of sizes, `mean` a cell 4 cm across.
void pebbles(Pt& s, double x, double z, double T, uint64_t seed, double mean) {
    Top t;
    near_things(x, z, T, 0.045, mean, seed, [&](double dx, double dz, uint64_t id, double, double) {
        const double r = std::min(draw(pebble_size(), id), 0.04);
        const double turn = u01(id ^ 3) * kPi, squash = 0.65 + 0.35 * u01(id ^ 4);
        const double a = dx * std::cos(turn) + dz * std::sin(turn), q = (-dx * std::sin(turn) + dz * std::cos(turn)) / squash;
        const double d = std::hypot(a, q) / r;
        if (d >= 1.0) return;
        const double tone = 0.34 + 0.26 * u01(id ^ 5), warm = 0.03 * (u01(id ^ 6) - 0.3);
        t.offer(r * 0.55 * std::sqrt(1 - d * d), smooth(1.0, 0.85, d), tone + warm, tone, tone - warm * 1.5, 0.62);
    });
    lay(s, t);
}

// Grass blades lying and standing, living and dead (`dry` of them), `mean` a
// cell 8 cm across; tufted where `clumps` gathers them.
void blades(Pt& s, double x, double z, double T, uint64_t seed, double mean, double dry, double gather, double comb = 0.0) {
    Top t;
    const Law way = blade_way(std::max(comb, 1e-3));
    near_things(x, z, T, 0.09, mean, seed, [&](double dx, double dz, uint64_t id, double px, double pz) {
        if (gather > 0 && u01(id ^ 9) > smooth(0.3, 0.7, fbm(px, pz, 0.25, T, seed + 41, 2)) * gather + (1 - gather)) return;
        const double len = std::min(draw(blade_length(), id), 0.085);
        const double width = 0.0022 * (0.7 + 0.6 * u01(id ^ 1));
        double across, along;
        across_blade(dx, dz, len, comb > 0 ? draw(way, id ^ 2) : 2 * kPi * u01(id ^ 2), width, (u01(id ^ 3) - 0.5) * 0.3 * len, 0.25, across, along);
        if (across >= 1.0) return;
        const bool dead = u01(id ^ 4) < dry;
        const double tone = 0.85 + 0.3 * u01(id ^ 5), tipk = 1.0 + 0.25 * along;
        const double grey = u01(id ^ 7);  // (dead a while: greyer)
        const double r = dead ? 0.55 - 0.12 * grey : 0.31, g = dead ? 0.49 - 0.08 * grey : 0.41, b = dead ? 0.33 - 0.02 * grey : 0.16;
        // (A blade arches over what is under it: higher at its middle.)
        const double lift = 0.004 * std::sin(kPi * along) * u01(id ^ 6) + 0.0012 * std::sqrt(1 - across * across);
        t.offer(lift + 0.001, smooth(1.0, 0.65, across), r * tone * tipk, g * tone * tipk, b * tone, dead ? 0.75 : 0.62);
    });
    lay(s, t);
}

// Moss cushions, of lognormal sizes, `mean` a cell 13 cm across, kept where
// the moss took (`where`: the field of it, 0..1).
void moss(Pt& s, double x, double z, double T, uint64_t seed, double mean, const std::function<double(double, double)>& where) {
    Top t;
    near_things(x, z, T, 0.13, mean, seed, [&](double dx, double dz, uint64_t id, double px, double pz) {
        if (u01(id ^ 11) > where(px, pz)) return;
        const double r = std::min(draw(cushion_size(), id), 0.12);
        // (A lobed edge, as a cushion grows out unevenly.)
        const double ang = std::atan2(dz, dx);
        const double d = std::hypot(dx, dz) / (r * (1.0 + 0.05 * std::sin(5 * ang + 6.28 * u01(id ^ 12)) + 0.04 * std::sin(11 * ang) +
                                                    0.1 * (vnoise(x, z, 0.004, T, id) - 0.5)));
        if (d >= 1.0) return;
        const double dome = std::pow(1 - d * d, 0.6);
        // (Its shoots: a fine bobbled surface, their tips catching the light.)
        const double shoots = vnoise(x, z, 0.0025, T, seed + 13) * 0.6 + vnoise(x, z, 0.0012, T, seed + 14) * 0.4;
        const double h = r * 0.42 * dome + 0.0015 * shoots;
        const double hue = u01(id ^ 13), light = (0.7 + 0.45 * dome) * (0.85 + 0.3 * shoots);
        const double r0 = 0.24 + 0.14 * hue, g0 = 0.31 + 0.08 * hue, b0 = 0.11 + 0.03 * hue;
        const double dark = 0.8 + 0.2 * shoots;  // (between its shoots, a little shadow)
        t.offer(h, smooth(1.0, 0.7, d), r0 * light * dark, g0 * light * dark, b0 * light * dark, 0.96);
    });
    lay(s, t);
}

// Twigs: few, long, thin, dark.
void twigs(Pt& s, double x, double z, double T, uint64_t seed, double mean) {
    Top t;
    near_things(x, z, T, 0.3, mean, seed, [&](double dx, double dz, uint64_t id, double, double) {
        const double len = 0.05 + 0.2 * u01(id ^ 1), width = 0.003 + 0.004 * u01(id ^ 2);
        double across, along;
        across_blade(dx, dz, len, 2 * kPi * u01(id ^ 3), width, (u01(id ^ 4) - 0.5) * 0.04, 0.7, across, along);
        if (across >= 1.0) return;
        const double tone = 0.75 + 0.4 * u01(id ^ 5);
        t.offer(width * 0.8 * std::sqrt(1 - across * across) + 0.002, smooth(1.0, 0.8, across), 0.30 * tone, 0.22 * tone, 0.15 * tone, 0.85);
    });
    lay(s, t);
}

// --- the materials ---------------------------------------------------------------------

double tile_of(const Params& p) { return std::max(0.1, p.num("tile", 2.0)); }
uint64_t seed_of(const Params& p, int cell) { return static_cast<uint64_t>(std::max(0.0, p.num("seed", 1.0))) * 1000003ULL + static_cast<uint64_t>(cell) * 7919ULL; }

Texture::Channels finish(const Pt& s, const Params& p, double open) {
    Texture::Channels c;
    c.r = std::clamp(s.r, 0.0, 1.0), c.g = std::clamp(s.g, 0.0, 1.0), c.b = std::clamp(s.b, 0.0, 1.0);
    c.height = std::clamp(s.h / std::max(1e-4, p.num("depth", 0.04)), 0.0, 1.0);
    c.roughness = std::clamp(s.rough, 0.02, 1.0), c.metal = 0, c.occlusion = std::clamp(open, 0.0, 1.0);
    return c;
}

Texture::Channels meadow(const Params& p, int cell, double u, double v) {
    const double T = tile_of(p), x = u * T, z = v * T;
    const uint64_t seed = seed_of(p, cell);
    const double mossy = std::clamp(p.num("moss", 0.6), 0.0, 1.0), dry = std::clamp(p.num("dry", 0.45), 0.0, 1.0);
    Pt s;
    soil(s, x, z, T, seed, 0.27, 0.21, 0.15);
    pebbles(s, x, z, T, seed + 100, 0.35 * (1 - 0.5 * mossy));
    // (Where moss took: patches half a metre across, more of them the mossier.)
    const auto where = [&](double px, double pz) {
        return smooth(0.62 - 0.5 * mossy, 0.82 - 0.35 * mossy, fbm(px, pz, 0.5, T, seed + 31, 3)) * std::min(1.0, mossy * 3.0);
    };
    moss(s, x, z, T, seed + 200, 9.0, where);
    // (A meadow's litter is thousands of blades a square metre: the soil
    // shows only between them. Two strewings - a mat lying low, and the
    // newer fallen over it.)
    // (Dry and green in patches a metre across, as a sward is.)
    const double patch = fbm(x, z, 1.0, T, seed + 37, 2);
    const double dry_here = std::clamp(dry + 0.5 * (patch - 0.5), 0.0, 1.0), comb = p.num("comb", 1.5);
    blades(s, x, z, T, seed + 300, p.num("grass", 1.0) * 45.0, dry_here, 0.35, comb);
    blades(s, x, z, T, seed + 350, p.num("grass", 1.0) * 20.0, dry_here * 0.8, 0.6, comb * 0.5);
    twigs(s, x, z, T, seed + 400, 0.3 * p.num("twigs", 1.0));
    // (Its hollows shut to the sky as deep as they lie under what is round them.)
    const double open = 0.45 + 0.55 * smooth(0.0, 0.02, s.h);
    return finish(s, p, open);
}

Texture::Channels lichen_rock(const Params& p, int cell, double u, double v) {
    const double T = tile_of(p), x = u * T, z = v * T;
    const uint64_t seed = seed_of(p, cell);
    const double tone = p.num("tone", 0.46), lichened = std::clamp(p.num("lichen", 0.5), 0.0, 1.0), mossy = std::clamp(p.num("moss", 0.3), 0.0, 1.0);
    Pt s;
    // The stone: broad stains, its surface rough at every size, its grains.
    const double broad = fbm(x, z, 0.4, T, seed + 1, 3), mid = fbm(x, z, 0.05, T, seed + 2, 4), fine = vnoise(x, z, 0.002, T, seed + 3);
    s.h = 0.02 * broad + 0.008 * mid + 0.0008 * fine;
    const double stain = 0.8 + 0.35 * (broad - 0.5) + 0.12 * (mid - 0.5);
    s.r = tone * stain * 1.02, s.g = tone * stain, s.b = tone * stain * 0.96, s.rough = 0.72 + 0.1 * fine;
    {
        // Its crystals: dark mica, white quartz and feldspar, a little pink.
        Top t;
        near_things(x, z, T, 0.006, 1.4, seed + 4, [&](double dx, double dz, uint64_t id, double, double) {
            const double r = 0.0005 + 0.0014 * u01(id ^ 1);
            const double d = std::hypot(dx, dz * (0.6 + 0.4 * u01(id ^ 2))) / r;
            if (d >= 1.0) return;
            const double k = u01(id ^ 3);
            const double c = tone * (k < 0.25 ? 0.55 : k < 0.65 ? 1.22 : 1.08);
            t.offer(0.0002, smooth(1.0, 0.6, d) * 0.6, c * (k >= 0.65 ? 1.08 : 1.0), c, c * (k >= 0.65 ? 0.9 : 1.0), k < 0.25 ? 0.45 : 0.7);
        });
        lay(s, t);
    }
    // Its cracks: where a ridged noise is at its crest, cut in, dark.
    const double ridge = 1.0 - std::abs(2.0 * fbm(x, z, 0.35, T, seed + 5, 3) - 1.0);
    const double crack = smooth(0.955, 0.99, ridge + 0.03 * (fine - 0.5));
    s.h -= 0.012 * crack;
    s.r *= 1 - 0.6 * crack, s.g *= 1 - 0.6 * crack, s.b *= 1 - 0.58 * crack;
    // Moss where water stays: in the cracks and the hollows.
    const double hollow = smooth(0.42, 0.3, broad * 0.6 + mid * 0.4) + crack * 0.8;
    const double mossk = std::clamp(hollow * mossy * 1.5, 0.0, 1.0) * smooth(0.3, 0.6, vnoise(x, z, 0.01, T, seed + 6));
    if (mossk > 0) {
        const double shoots = vnoise(x, z, 0.0022, T, seed + 7);
        Top t;
        t.offer(0.004 + 0.002 * shoots, mossk, 0.26 * (0.8 + 0.4 * shoots), 0.36 * (0.8 + 0.4 * shoots), 0.09, 0.96);
        lay(s, t);
    }
    // Lichen: rosettes, many small and few large, gathered where the stone
    // stays dry and lit - pale grey-green most, some grey-white, a few
    // orange, a few black.
    {
        Top t;
        const double gather = lichened * 1.6;
        near_things(x, z, T, 0.16, 5.0, seed + 8, [&](double dx, double dz, uint64_t id, double px, double pz) {
            if (u01(id ^ 1) > std::clamp(smooth(0.35, 0.7, fbm(px, pz, 0.6, T, seed + 9, 2)) * gather, 0.0, 1.0)) return;
            const double r = std::min(draw(lichen_size(), id), 0.075);
            const double ang = std::atan2(dz, dx);
            const double d = std::hypot(dx, dz) / (r * (1.0 + 0.1 * std::sin(7 * ang + 6.28 * u01(id ^ 2)) + 0.06 * std::sin(13 * ang)));
            if (d >= 1.0) return;
            const double k = u01(id ^ 3);
            double cr = 0.66, cg = 0.69, cb = 0.56;
            if (k > 0.6) cr = 0.76, cg = 0.76, cb = 0.71;
            if (k > 0.86) cr = 0.80, cg = 0.55, cb = 0.18;
            if (k > 0.93) cr = 0.16, cg = 0.16, cb = 0.14;
            // (Older at its middle: darker, crazed.)
            const double age = 0.8 + 0.2 * d + 0.1 * (vnoise(x, z, 0.003, T, seed + 10) - 0.5);
            t.offer(0.0009, smooth(1.0, 0.88, d), cr * age, cg * age, cb * age, 0.92);
        });
        lay(s, t);
    }
    s.h += 0.012;  // (its cracks cut below where it begins)
    const double open = 0.55 + 0.45 * (1.0 - crack) * smooth(0.0, 0.5, broad + 0.2);
    return finish(s, p, open);
}

Texture::Channels gravel(const Params& p, int cell, double u, double v) {
    const double T = tile_of(p), x = u * T, z = v * T;
    const uint64_t seed = seed_of(p, cell);
    Pt s;
    soil(s, x, z, T, seed, 0.40, 0.35, 0.28);
    {
        // Its stones, packed: a power law of sizes, angular, of mixed rock.
        Top t;
        // (Packed: so many to a cell that each lies against the next, the
        // fines showing only between them.)
        near_things(x, z, T, 0.07, 26.0 * p.num("stones", 1.0), seed + 1, [&](double dx, double dz, uint64_t id, double, double) {
            const double r = std::min(draw(stone_size(), id), 0.065);
            const double turn = u01(id ^ 1) * kPi, squash = 0.6 + 0.4 * u01(id ^ 2), sharp = 2.4 + 2.0 * u01(id ^ 3);
            const double a = std::abs(dx * std::cos(turn) + dz * std::sin(turn)) / r, q = std::abs(-dx * std::sin(turn) + dz * std::cos(turn)) / (r * squash);
            const double d = std::pow(std::pow(a, sharp) + std::pow(q, sharp), 1.0 / sharp);
            if (d >= 1.0) return;
            const double k = u01(id ^ 4), tone = 0.34 + 0.24 * u01(id ^ 5);
            const double cr = k < 0.7 ? tone : k < 0.9 ? tone * 1.06 : tone * 0.62, cg = k < 0.7 ? tone : k < 0.9 ? tone * 0.98 : tone * 0.62,
                         cb = k < 0.7 ? tone * 0.97 : k < 0.9 ? tone * 0.88 : tone * 0.63;
            // (Flat-topped, as broken stone lies, its facets a little lit.)
            const double h = std::min(r * 0.6 * std::sqrt(1 - d * d), r * 0.32) + 0.0004 * vnoise(x, z, 0.003, T, id);
            t.offer(h, smooth(1.0, 0.9, d), cr, cg, cb, 0.6 + 0.2 * u01(id ^ 6));
        });
        lay(s, t);
    }
    if (p.num("grass", 0.2) > 0) blades(s, x, z, T, seed + 2, 10.0 * p.num("grass", 0.2), 0.5, 1.0);
    const double open = 0.4 + 0.6 * smooth(0.002, 0.018, s.h);
    return finish(s, p, open);
}

Texture::Channels mud(const Params& p, int cell, double u, double v) {
    const double T = tile_of(p), x = u * T, z = v * T;
    const uint64_t seed = seed_of(p, cell);
    const double wet = std::clamp(p.num("wet", 0.5), 0.0, 1.0);
    Pt s;
    soil(s, x, z, T, seed, 0.25, 0.20, 0.15);
    s.h += 0.01 * fbm(x, z, 0.3, T, seed + 1, 3);
    s.rough = 0.5 + 0.35 * (1 - wet);
    pebbles(s, x, z, T, seed + 2, 0.25);
    blades(s, x, z, T, seed + 3, 2.0, 0.9, 0.0);
    // A film of water over its low: darker, smooth, level.
    const double level = 0.003 + 0.012 * wet;
    const double film = smooth(level, level - 0.003, s.h) * wet;
    s.r *= 1 - 0.45 * film, s.g *= 1 - 0.45 * film, s.b *= 1 - 0.4 * film;
    s.rough += (0.18 - s.rough) * film;
    s.h = std::max(s.h, level * film);
    // (Wet, all of it darker than dry silt.)
    s.r *= 1 - 0.25 * wet, s.g *= 1 - 0.25 * wet, s.b *= 1 - 0.22 * wet;
    return finish(s, p, 0.6 + 0.4 * smooth(0.0, 0.015, s.h));
}

}  // namespace

void define() {
    static std::once_flag once;
    std::call_once(once, [] {
        Texture::define_material("meadow", meadow);
        Texture::define_material("lichen_rock", lichen_rock);
        Texture::define_material("gravel", gravel);
        Texture::define_material("mud", mud);
    });
}

TileMaps tile_maps(const std::string& material, const Params& params, int px) {
    const std::function<Texture::Channels(const Params&, int, double, double)> m = material == "meadow"        ? meadow
                                                                                  : material == "lichen_rock" ? lichen_rock
                                                                                  : material == "gravel"      ? gravel
                                                                                  : material == "mud"         ? mud
                                                                                                              : nullptr;
    TileMaps t;
    if (!m || px <= 0) return t;
    t.px = px;
    const std::size_t n = static_cast<std::size_t>(px) * px;
    // Kept, by what they are made from: the material, its params, the size,
    // this code.
    Hasher key;
    key.text("sg.grounds").text(material).text(SG_GROUNDS_CODE).integer(px);
    for (const auto& [k, v] : params) {
        key.text(k.str());
        if (const double* d = std::get_if<double>(&v)) key.number(*d);
        else if (const std::string* s = std::get_if<std::string>(&v)) key.text(*s);
    }
    const Digest id = key.digest();
    std::string kept;
    if (cache::load("grounds", id, kept) && kept.size() == n * 16) {
        t.albedo.assign(kept.begin(), kept.begin() + n * 4);
        t.normal.assign(kept.begin() + n * 4, kept.begin() + n * 8);
        t.surface.assign(kept.begin() + n * 8, kept.begin() + n * 12);
        t.height.assign(kept.begin() + n * 12, kept.end());
        return t;
    }
    std::vector<double> h(n);
    t.albedo.assign(n * 4, 255), t.normal.assign(n * 4, 255), t.surface.assign(n * 4, 255), t.height.assign(n * 4, 255);
    const auto byte = [](double v) { return static_cast<unsigned char>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0)); };
    // (Rows from the top: the first row is v nearest 1, as a picture is read.)
    const auto rows = [&](int from, int step) {
        for (int y = from; y < px; y += step)
            for (int x = 0; x < px; ++x) {
                const Texture::Channels c = m(params, 0, (x + 0.5) / px, 1.0 - (y + 0.5) / px);
                const std::size_t i = static_cast<std::size_t>(y) * px + x;
                h[i] = c.height;
                unsigned char* a = &t.albedo[i * 4];
                a[0] = byte(c.r), a[1] = byte(c.g), a[2] = byte(c.b);
                unsigned char* s = &t.surface[i * 4];
                s[0] = byte(c.occlusion), s[1] = byte(c.roughness), s[2] = byte(c.metal);
                unsigned char* g = &t.height[i * 4];
                g[0] = g[1] = g[2] = byte(c.height);
            }
    };
    const int threads = static_cast<int>(std::max(1u, std::min(32u, std::thread::hardware_concurrency())));
    std::vector<std::thread> pool;
    for (int k = 0; k < threads; ++k) pool.emplace_back(rows, k, threads);
    for (auto& th : pool) th.join();
    // How open each point is to the sky: how far it lies under the ground
    // round it, a centimetre and four round (the height blurred, the tile
    // wrapping) - its hollows, the gaps between stones and blades, shut.
    // Taken into its occlusion, and a little into its colour, as a
    // photographed set's albedo keeps some of it.
    {
        const double depth_m = std::max(1e-4, params.num("depth", 0.04)), cell_m = std::max(0.1, params.num("tile", 2.0)) / px;
        std::vector<double> near(n), far(n), tmp(n);
        // (A box blur, rows then columns, the tile wrapping.)
        const auto blur = [&](const std::vector<double>& in, std::vector<double>& out, int r) {
            const double w = 1.0 / (2 * r + 1);
            for (int y = 0; y < px; ++y) {
                const std::size_t row = static_cast<std::size_t>(y) * px;
                double sum = 0;
                for (int k = -r; k <= r; ++k) sum += in[row + wrap(k, px)];
                for (int x = 0; x < px; ++x) {
                    tmp[row + x] = sum * w;
                    sum += in[row + wrap(x + r + 1, px)] - in[row + wrap(x - r, px)];
                }
            }
            for (int x = 0; x < px; ++x) {
                double sum = 0;
                for (int k = -r; k <= r; ++k) sum += tmp[static_cast<std::size_t>(wrap(k, px)) * px + x];
                for (int y = 0; y < px; ++y) {
                    out[static_cast<std::size_t>(y) * px + x] = sum * w;
                    sum += tmp[static_cast<std::size_t>(wrap(y + r + 1, px)) * px + x] - tmp[static_cast<std::size_t>(wrap(y - r, px)) * px + x];
                }
            }
        };
        blur(h, near, std::max(1, static_cast<int>(std::lround(0.01 / cell_m))));
        blur(h, far, std::max(1, static_cast<int>(std::lround(0.04 / cell_m))));
        for (std::size_t i = 0; i < n; ++i) {
            const double under = (std::max(0.0, near[i] - h[i]) * 0.6 + std::max(0.0, far[i] - h[i]) * 0.4) * depth_m;
            const double open = std::exp(-under / 0.004);
            t.surface[i * 4] = static_cast<unsigned char>(std::lround(t.surface[i * 4] * open));
            const double k = 0.75 + 0.25 * open;
            for (int c = 0; c < 3; ++c) t.albedo[i * 4 + c] = static_cast<unsigned char>(std::lround(t.albedo[i * 4 + c] * k));
        }
    }
    // The normal from the height, at full precision, across the tile's seam
    // as within it: metres of rise over metres across.
    const double depth = std::max(1e-4, params.num("depth", 0.04)), step = std::max(0.1, params.num("tile", 2.0)) / px;
    for (int y = 0; y < px; ++y)
        for (int x = 0; x < px; ++x) {
            const auto H = [&](int xx, int yy) { return h[static_cast<std::size_t>(wrap(yy, px)) * px + wrap(xx, px)]; };
            const double du = (H(x + 1, y) - H(x - 1, y)) * depth / (2 * step), dv = (H(x, y - 1) - H(x, y + 1)) * depth / (2 * step);
            const double l = std::sqrt(du * du + dv * dv + 1.0);
            unsigned char* o = &t.normal[(static_cast<std::size_t>(y) * px + x) * 4];
            o[0] = byte(0.5 - 0.5 * du / l), o[1] = byte(0.5 - 0.5 * dv / l), o[2] = byte(0.5 + 0.5 / l);
        }
    kept.clear();
    for (const auto* maps : {&t.albedo, &t.normal, &t.surface, &t.height}) kept.append(maps->begin(), maps->end());
    cache::store("grounds", id, kept);
    return t;
}

}  // namespace sg::grounds
