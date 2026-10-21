#include "sg/domains/Distribution.hpp"
#include "sg/core/Work.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace sg {

namespace {

constexpr double kTau = 6.283185307179586;

// A number that wanders, the same for the same three: (0, 1].
uint64_t mix64(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
double unit(uint64_t seed, uint64_t i, uint64_t k) {
    const uint64_t h = mix64(seed ^ mix64(i * 0x100000001b3ULL + k));
    return (static_cast<double>(h >> 11) + 1.0) * (1.0 / 9007199254740993.0);
}
double gauss(uint64_t seed, uint64_t i, uint64_t k) {
    // (Box and Muller's.)
    return std::sqrt(-2.0 * std::log(unit(seed, i, k))) * std::cos(kTau * unit(seed, i, k + 1));
}
double gamma_draw(double a, uint64_t seed, uint64_t i, uint64_t k) {
    // (Marsaglia and Tsang's, a < 1 lifted.)
    if (a < 1.0) return gamma_draw(a + 1.0, seed, i, k) * std::pow(unit(seed, i, k + 97), 1.0 / a);
    const double d = a - 1.0 / 3.0, c = 1.0 / std::sqrt(9.0 * d);
    for (uint64_t t = 0; t < 64; ++t) {
        const double x = gauss(seed, i, k + 2 * t);
        double v = 1.0 + c * x;
        if (v <= 0.0) continue;
        v = v * v * v;
        if (std::log(unit(seed, i, k + 2 * t + 200)) < 0.5 * x * x + d - d * v + d * std::log(v)) return d * v;
    }
    return d;
}
// A Poisson count with mean `m` (Knuth's; a normal past a hundred).
int poisson_count(double m, uint64_t seed, uint64_t i) {
    if (m <= 0.0) return 0;
    if (m > 100.0) return std::max(0, static_cast<int>(std::lround(m + std::sqrt(m) * gauss(seed, i, 7))));
    const double l = std::exp(-m);
    double p = 1.0;
    int n = 0;
    do {
        p *= unit(seed, i, 11 + static_cast<uint64_t>(n));
        ++n;
    } while (p > l && n < 1000);
    return n - 1;
}

const std::set<std::string>& value_laws() {
    static const std::set<std::string> all{"uniform", "normal", "lognormal", "power", "beta", "vonmises", "choice"};
    return all;
}
const std::set<std::string>& point_laws() {
    static const std::set<std::string> all{"poisson", "disk", "thomas", "matern"};
    return all;
}
const std::set<std::string>& field_laws() {
    static const std::set<std::string> all{"bump", "stroke", "sum"};
    return all;
}

}  // namespace

Distribution::Distribution(Key id) : State(id) {}

Element& Distribution::law(Key name, const std::string& what, const Params& numbers) {
    Element* e = find(name);
    if (!e) e = &add_element(name, kinds::law);
    e->params = numbers;
    e->params.set("law", what);
    return *e;
}

const Element* Distribution::law(Key name) const { return find(name); }

std::vector<std::string> Distribution::faults() const { return distribution_faults(*this); }

double Law::num(const char* k, double fallback) const { return p.num(Key{k}, fallback); }

Law law_of(const Params& params, const std::string& prefix) {
    Law l;
    if (prefix.empty()) {
        l.p = params;
    } else {
        const std::string pre = prefix + ".";
        for (const auto& [k, v] : params) {
            const std::string& n = k.str();
            if (n.compare(0, pre.size(), pre) != 0) continue;
            const Key own{n.substr(pre.size())};
            if (const auto* d = std::get_if<double>(&v)) l.p.set(own, *d);
            else if (const auto* s = std::get_if<std::string>(&v)) l.p.set(own, *s);
            else if (const auto* i = std::get_if<int64_t>(&v)) l.p.set(own, static_cast<double>(*i));
        }
    }
    l.what = l.p.get_or<std::string>("law", "");
    l.seed = static_cast<uint64_t>(l.p.num(Key{"seed"}, 1.0)) * 0x9e3779b97f4a7c15ULL + std::hash<std::string>{}(l.what);
    return l;
}

double draw(const Law& law, uint64_t i) {
    const std::string& w = law.what;
    const uint64_t s = law.seed;
    if (w == "uniform") {
        const double lo = law.num("lo", 0.0), hi = law.num("hi", 1.0);
        return lo + (hi - lo) * unit(s, i, 0);
    }
    if (w == "normal") return law.num("mean", 0.0) + law.num("sd", 1.0) * gauss(s, i, 0);
    if (w == "lognormal") return law.num("median", 1.0) * std::exp(law.num("spread", 0.5) * gauss(s, i, 0));
    if (w == "power") {
        // (Pareto's, cut at hi: many near lo, fewer the larger.)
        const double lo = std::max(law.num("lo", 1.0), 1e-9), hi = std::max(law.num("hi", 10.0), lo), a = law.num("alpha", 1.5);
        const double u = unit(s, i, 0), la = std::pow(lo, -a), ha = std::pow(hi, -a);
        return std::pow(la - u * (la - ha), -1.0 / a);
    }
    if (w == "beta") {
        const double x = gamma_draw(law.num("a", 2.0), s, i, 0), y = gamma_draw(law.num("b", 2.0), s, i, 500);
        const double lo = law.num("lo", 0.0), hi = law.num("hi", 1.0);
        return lo + (hi - lo) * (x / std::max(x + y, 1e-12));
    }
    if (w == "vonmises") {
        // (Best and Fisher's.)
        const double mu = law.num("mean", 0.0), k = law.num("kappa", 1.0);
        if (k < 1e-6) return mu + kTau * (unit(s, i, 0) - 0.5);
        const double t = 1.0 + std::sqrt(1.0 + 4.0 * k * k), rho = (t - std::sqrt(2.0 * t)) / (2.0 * k), r = (1.0 + rho * rho) / (2.0 * rho);
        for (uint64_t n = 0; n < 64; ++n) {
            const double z = std::cos(kTau * 0.5 * unit(s, i, 3 * n)), f = (1.0 + r * z) / (r + z), c = k * (r - f);
            const double u2 = unit(s, i, 3 * n + 1);
            if (c * (2.0 - c) - u2 > 0.0 || std::log(c / u2) + 1.0 - c >= 0.0)
                return std::remainder(mu + (unit(s, i, 3 * n + 2) > 0.5 ? 1.0 : -1.0) * std::acos(f), kTau);
        }
        return mu;
    }
    if (w == "choice") {
        std::vector<double> ws;
        for (int k = 0;; ++k) {
            const Key key{"w" + std::to_string(k)};
            if (!law.p.has(key)) break;
            ws.push_back(std::max(law.p.num(key), 0.0));
        }
        double sum = 0.0;
        for (double x : ws) sum += x;
        double u = unit(s, i, 0) * sum;
        for (std::size_t k = 0; k < ws.size(); ++k) {
            if (u < ws[k]) return static_cast<double>(k);
            u -= ws[k];
        }
        return ws.empty() ? 0.0 : static_cast<double>(ws.size() - 1);
    }
    return 0.0;
}

std::vector<Vec3d> points(const Law& law, const Area& a, const std::function<double(double, double)>& density) {
    std::vector<Vec3d> out;
    const double w = a.x1 - a.x0, d = a.z1 - a.z0;
    if (w <= 0.0 || d <= 0.0) return out;
    const uint64_t s = law.seed;
    const double area100 = w * d / 100.0;
    const auto keep = [&](double x, double z, uint64_t i) {
        if (x < a.x0 || x > a.x1 || z < a.z0 || z > a.z1) return;
        if (density && unit(s, i, 9999) > std::clamp(density(x, z), 0.0, 1.0)) return;
        out.push_back({x, 0.0, z});
    };
    if (law.what == "poisson") {
        const int n = poisson_count(law.num("density", 1.0) * area100, s, 0);
        for (int i = 0; i < n; ++i) keep(a.x0 + w * unit(s, static_cast<uint64_t>(i) + 1, 0), a.z0 + d * unit(s, static_cast<uint64_t>(i) + 1, 1), static_cast<uint64_t>(i));
    } else if (law.what == "thomas" || law.what == "matern") {
        // Parents, as a Poisson's; round each, its children - a Gaussian
        // round it (Thomas), or anywhere in a disc (Matern).
        const double spread = law.what == "thomas" ? law.num("sd", 2.0) : law.num("radius", 3.0);
        // (Parents a little past the area too: their children fall in it.)
        const double m = 3.0 * spread;
        const int n = poisson_count(law.num("parents", 0.2) * (w + 2 * m) * (d + 2 * m) / 100.0, s, 0);
        uint64_t k = 0;
        for (int i = 0; i < n; ++i) {
            const uint64_t pi = static_cast<uint64_t>(i) + 1;
            const double px = a.x0 - m + (w + 2 * m) * unit(s, pi, 0), pz = a.z0 - m + (d + 2 * m) * unit(s, pi, 1);
            const int c = poisson_count(law.num("children", 8.0), s, pi * 7919);
            for (int j = 0; j < c; ++j, ++k) {
                const uint64_t ci = (pi << 20) + static_cast<uint64_t>(j);
                double dx, dz;
                if (law.what == "thomas") {
                    dx = spread * gauss(s, ci, 2), dz = spread * gauss(s, ci, 4);
                } else {
                    const double r = spread * std::sqrt(unit(s, ci, 2)), t = kTau * unit(s, ci, 3);
                    dx = r * std::cos(t), dz = r * std::sin(t);
                }
                keep(px + dx, pz + dz, ci);
            }
        }
    } else if (law.what == "disk") {
        // Bridson's: each new point a ring's width from one already kept,
        // none nearer than the spacing - blue noise.
        const double r = std::max(law.num("spacing", 1.0), 1e-3), cell = r / std::sqrt(2.0);
        const int gw = static_cast<int>(std::ceil(w / cell)), gd = static_cast<int>(std::ceil(d / cell));
        std::vector<int> grid(static_cast<std::size_t>(gw) * gd, -1);
        std::vector<Vec3d> all;
        std::vector<int> active;
        const auto put = [&](double x, double z) {
            all.push_back({x, 0.0, z});
            grid[static_cast<std::size_t>(std::min(gd - 1, static_cast<int>((z - a.z0) / cell))) * gw + std::min(gw - 1, static_cast<int>((x - a.x0) / cell))] =
                static_cast<int>(all.size() - 1);
            active.push_back(static_cast<int>(all.size() - 1));
        };
        put(a.x0 + w * unit(s, 0, 0), a.z0 + d * unit(s, 0, 1));
        uint64_t tries = 0;
        while (!active.empty()) {
            const std::size_t pick = static_cast<std::size_t>(unit(s, tries, 5) * static_cast<double>(active.size())) % active.size();
            const Vec3d p = all[static_cast<std::size_t>(active[pick])];
            bool found = false;
            for (int t = 0; t < 30; ++t, ++tries) {
                const double rr = r * (1.0 + unit(s, tries, 6)), th = kTau * unit(s, tries, 7);
                const double x = p.x + rr * std::cos(th), z = p.z + rr * std::sin(th);
                if (x < a.x0 || x >= a.x1 || z < a.z0 || z >= a.z1) continue;
                const int cx = static_cast<int>((x - a.x0) / cell), cz = static_cast<int>((z - a.z0) / cell);
                bool near = false;
                for (int j = std::max(0, cz - 2); j <= std::min(gd - 1, cz + 2) && !near; ++j)
                    for (int i = std::max(0, cx - 2); i <= std::min(gw - 1, cx + 2) && !near; ++i) {
                        const int q = grid[static_cast<std::size_t>(j) * gw + i];
                        if (q >= 0 && std::hypot(all[static_cast<std::size_t>(q)].x - x, all[static_cast<std::size_t>(q)].z - z) < r) near = true;
                    }
                if (near) continue;
                put(x, z);
                found = true;
                break;
            }
            if (!found) active.erase(active.begin() + static_cast<std::ptrdiff_t>(pick));
        }
        for (std::size_t i = 0; i < all.size(); ++i) keep(all[i].x, all[i].z, i);
    }
    return out;
}

namespace {

// A cluster law's parents over `a` (as points() draws them: past its edges
// by three spreads, so children falling in it are counted).
std::vector<Vec3d> parents_of(const Law& law, const Area& a) {
    std::vector<Vec3d> out;
    const double spread = law.what == "thomas" ? law.num("sd", 2.0) : law.num("radius", 3.0), m = 3.0 * spread;
    const double w = a.x1 - a.x0, d = a.z1 - a.z0;
    const int n = poisson_count(law.num("parents", 0.2) * (w + 2 * m) * (d + 2 * m) / 100.0, law.seed, 0);
    for (int i = 0; i < n; ++i) {
        const uint64_t pi = static_cast<uint64_t>(i) + 1;
        out.push_back({a.x0 - m + (w + 2 * m) * unit(law.seed, pi, 0), 0.0, a.z0 - m + (d + 2 * m) * unit(law.seed, pi, 1)});
    }
    return out;
}

std::vector<std::string> words(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ' ' || c == ',') {
            if (!cur.empty()) out.push_back(cur), cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

double intensity_of(const Distribution& d, const Law& law, double x, double z, const Area& area, int depth) {
    if (depth > 8) return 0.0;  // (a sum of itself: no more)
    const std::string& w = law.what;
    if (w == "poisson") return law.num("density", 1.0) / 100.0;
    if (w == "disk") {
        const double r = std::max(law.num("spacing", 1.0), 1e-3);
        return 0.68 / (r * r);  // (Bridson's packs about so many)
    }
    if (w == "bump") {
        const double sd = std::max(law.num("sd", 1.0), 1e-3), dx = x - law.num("at_x", 0.0), dz = z - law.num("at_z", 0.0);
        return law.num("amount", 10.0) * std::exp(-(dx * dx + dz * dz) / (2.0 * sd * sd)) / (kTau * sd * sd);
    }
    if (w == "stroke") {
        // (A bump drawn along a segment: its amount per metre of it, the
        // band as wide as `sd` either side, softening to `sd_to` at its end.)
        const double ax = law.num("from_x", 0.0), az = law.num("from_z", 0.0), bx = law.num("to_x", 0.0), bz = law.num("to_z", 0.0);
        const double ux = bx - ax, uz = bz - az, len2 = ux * ux + uz * uz;
        const double t = len2 > 1e-12 ? std::clamp(((x - ax) * ux + (z - az) * uz) / len2, 0.0, 1.0) : 0.0;
        const double sd0 = std::max(law.num("sd", 0.2), 1e-3), sd = std::max(sd0 + (law.num("sd_to", sd0) - sd0) * t, 1e-3);
        const double dx = x - (ax + ux * t), dz = z - (az + uz * t);
        const double fade = law.num("amount", 10.0) + (law.num("amount_to", law.num("amount", 10.0)) - law.num("amount", 10.0)) * t;
        return fade * std::exp(-(dx * dx + dz * dz) / (2.0 * sd * sd)) / (std::sqrt(kTau) * sd);
    }
    if (w == "thomas" || w == "matern") {
        const double c = law.num("children", 8.0);
        double sum = 0.0;
        for (const Vec3d& p : parents_of(law, area)) {
            const double dx = x - p.x, dz = z - p.z;
            if (w == "thomas") {
                const double sd = std::max(law.num("sd", 2.0), 1e-3);
                sum += c * std::exp(-(dx * dx + dz * dz) / (2.0 * sd * sd)) / (kTau * sd * sd);
            } else {
                const double r = std::max(law.num("radius", 3.0), 1e-3);
                if (dx * dx + dz * dz <= r * r) sum += c / (0.5 * kTau * r * r);
            }
        }
        return sum;
    }
    if (w == "sum") {
        double sum = 0.0;
        for (const std::string& n : words(law.p.get_or<std::string>("of", ""))) {
            const Element* e = d.law(Key{n});
            if (!e) continue;
            sum += law.num(("w_" + n).c_str(), 1.0) * intensity_of(d, law_of(e->params), x, z, area, depth + 1);
        }
        return sum;
    }
    return 0.0;
}

double halton(uint64_t i, uint64_t base) {
    double f = 1.0, r = 0.0;
    while (i > 0) {
        f /= static_cast<double>(base);
        r += f * static_cast<double>(i % base);
        i /= base;
    }
    return r;
}

}  // namespace

double intensity(const Distribution& d, Key name, double x, double z, const Area& area) {
    const Element* e = d.law(name);
    return e ? intensity_of(d, law_of(e->params), x, z, area, 0) : 0.0;
}

std::vector<Vec3d> sample(const Distribution& d, Key name, const Area& area, const std::string& sampler, double spacing) {
    std::vector<Vec3d> out;
    const Element* e = d.law(name);
    if (!e) return out;
    const Law law = law_of(e->params);
    // As drawn: a point law by its own process; a sum, each of its own -
    // their points together (superposition).
    if (sampler == "as") {
        if (point_laws().count(law.what)) return points(law, area);
        if (law.what == "sum") {
            for (const std::string& n : words(law.p.get_or<std::string>("of", ""))) {
                const std::vector<Vec3d> part = sample(d, Key{n}, area, "as", spacing);
                out.insert(out.end(), part.begin(), part.end());
            }
            return out;
        }
    }
    // Else by its intensity, thinned from the most it reaches (looked for on
    // a grid of the area, with room to spare).
    const double w = area.x1 - area.x0, dd = area.z1 - area.z0;
    if (w <= 0.0 || dd <= 0.0) return out;
    double most = 0.0;
    constexpr int kLook = 48;
    for (int j = 0; j <= kLook; ++j)
        for (int i = 0; i <= kLook; ++i)
            most = std::max(most, intensity_of(d, law, area.x0 + w * i / kLook, area.z0 + dd * j / kLook, area, 0));
    most *= 1.25;
    if (most <= 0.0) return out;
    const uint64_t s = law.seed ^ 0x51ed270b27f4e3a5ULL;
    const auto kept = [&](double x, double z, uint64_t i) { return unit(s, i, 31) * most <= intensity_of(d, law, x, z, area, 0); };
    if (sampler == "disk") {
        Law blue;
        blue.what = "disk", blue.seed = s, blue.p.set("spacing", spacing);
        const double base = 0.68 / (spacing * spacing);
        // (No denser than the disk packs: where the field asks for more, as dense as it may be.)
        for (const Vec3d& p : points(blue, area))
            if (unit(s, static_cast<uint64_t>(std::llround(p.x * 1000.0)) * 7919u + static_cast<uint64_t>(std::llround(p.z * 1000.0)), 33) * base <=
                intensity_of(d, law, p.x, p.z, area, 0))
                out.push_back(p);
        return out;
    }
    const int n = sampler == "halton" ? static_cast<int>(std::lround(most * w * dd)) : poisson_count(most * w * dd, s, 0);
    for (int i = 0; i < n; ++i) {
        const uint64_t k = static_cast<uint64_t>(i) + 1;
        const double u = sampler == "halton" ? halton(k + 17, 2) : unit(s, k, 0), v = sampler == "halton" ? halton(k + 17, 3) : unit(s, k, 1);
        const double x = area.x0 + w * u, z = area.z0 + dd * v;
        if (kept(x, z, k)) out.push_back({x, 0.0, z});
    }
    return out;
}

std::vector<unsigned char> field_picture(const Distribution& d, Key name, const Area& area, int px, double* most_out) {
    std::vector<double> f(static_cast<std::size_t>(px) * px, 0.0);
    // (Its rows across the machine's hands, each row its own: the laws are
    // only read. The most found after, in one order: the same on any hands.)
    work::bands(px, [&](int from, int to) {
        for (int j = from; j < to; ++j)
            for (int i = 0; i < px; ++i) {
                const double x = area.x0 + (area.x1 - area.x0) * (i + 0.5) / px, z = area.z0 + (area.z1 - area.z0) * (j + 0.5) / px;
                f[static_cast<std::size_t>(j) * px + i] = intensity(d, name, x, z, area);
            }
    });
    double most = 0.0;
    for (const double v : f) most = std::max(most, v);
    if (most_out) *most_out = most;
    std::vector<unsigned char> out(static_cast<std::size_t>(px) * px * 4, 255);
    for (std::size_t k = 0; k < f.size(); ++k) {
        const unsigned char v = static_cast<unsigned char>(std::lround(255.0 * (most > 0.0 ? f[k] / most : 0.0)));
        out[k * 4 + 0] = out[k * 4 + 1] = out[k * 4 + 2] = out[k * 4 + 3] = v;
    }
    return out;
}

void law_to(const Element& law, Element& user) {
    // (Its numbers under the name of the law: `<law>.lo`, `<law>.law` ...)
    const std::string pre = law.id.str() + ".";
    for (const auto& [k, v] : law.params) {
        const Key key{pre + k.str()};
        if (const auto* d = std::get_if<double>(&v)) {
            if (user.params.num(key, std::nan("")) != *d) user.params.set(key, *d);
        } else if (const auto* s = std::get_if<std::string>(&v)) {
            if (user.params.get_or<std::string>(key, "") != *s) user.params.set(key, *s);
        }
    }
}

std::vector<std::string> distribution_faults(const State& d) {
    std::vector<std::string> out;
    for (const Element& e : d.elements()) {
        if (!e.alive || e.kind != kinds::law) continue;
        const std::string name = "law " + d.id().str() + "." + e.id.str() + ": ";
        const std::string what = e.params.get_or<std::string>("law", "");
        if (!value_laws().count(what) && !point_laws().count(what) && !field_laws().count(what)) {
            out.push_back(name + "no law called '" + what + "'");
            continue;
        }
        for (const auto& [k, v] : e.params)
            if (const auto* x = std::get_if<double>(&v); x && !std::isfinite(*x)) out.push_back(name + k.str() + " is not a number");
        const auto& p = e.params;
        const auto bad = [&](bool b, const char* why) {
            if (b) out.push_back(name + why);
        };
        if (what == "uniform" || what == "beta") bad(p.num(Key{"hi"}, 1.0) < p.num(Key{"lo"}, 0.0), "its top is under its bottom");
        if (what == "normal") bad(p.num(Key{"sd"}, 1.0) < 0.0, "it spreads less than none");
        if (what == "lognormal") bad(p.num(Key{"median"}, 1.0) <= 0.0 || p.num(Key{"spread"}, 0.5) < 0.0, "its middle or its spread is not more than none");
        if (what == "power") bad(p.num(Key{"lo"}, 1.0) <= 0.0 || p.num(Key{"hi"}, 10.0) < p.num(Key{"lo"}, 1.0) || p.num(Key{"alpha"}, 1.5) <= 0.0, "it needs 0 < lo <= hi and alpha > 0");
        if (what == "beta") bad(p.num(Key{"a"}, 2.0) <= 0.0 || p.num(Key{"b"}, 2.0) <= 0.0, "its shapes are not more than none");
        if (what == "vonmises") bad(p.num(Key{"kappa"}, 1.0) < 0.0, "its concentration is less than none");
        if (what == "poisson") bad(p.num(Key{"density"}, 1.0) < 0.0, "it is less dense than none");
        if (what == "disk") bad(p.num(Key{"spacing"}, 1.0) <= 0.0, "its spacing is not more than none");
        if (what == "bump") bad(p.num(Key{"sd"}, 1.0) <= 0.0 || p.num(Key{"amount"}, 10.0) < 0.0, "its spread is not more than none, or its amount less");
        if (what == "stroke")
            bad(p.num(Key{"sd"}, 0.2) <= 0.0 || p.num(Key{"sd_to"}, p.num(Key{"sd"}, 0.2)) <= 0.0 || p.num(Key{"amount"}, 10.0) < 0.0 ||
                    p.num(Key{"amount_to"}, p.num(Key{"amount"}, 10.0)) < 0.0,
                "its spread is not more than none, or its amount less");
        if (what == "sum")
            for (const std::string& n : words(p.get_or<std::string>("of", "")))
                if (!d.find(Key{n})) out.push_back(name + "it adds '" + n + "', which is no law of it");
        if (what == "thomas" || what == "matern")
            bad(p.num(Key{"parents"}, 0.2) < 0.0 || p.num(Key{"children"}, 8.0) < 0.0 || p.num(Key{what == "thomas" ? "sd" : "radius"}, 2.0) <= 0.0,
                "its parents, children or spread are out of range");
    }
    return out;
}

}  // namespace sg
