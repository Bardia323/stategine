// Land from a recipe: its statements read in order, each a pass over the
// heights as they are then; what lies on the land (water, covering, things)
// settled once the heights are all made.
#include "sg/domains/Terrain.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <mutex>
#include <sstream>
#include <unordered_map>

#include "SG_TERRAIN_CODE.hpp"
#include "TerrainKernel.hpp"
#include "sg/core/Cache.hpp"

namespace sg::terrain {

using namespace kernel;

// --- the land, read ---------------------------------------------------------------

float Land::at(const std::vector<float>& map, double x, double z) const {
    if (nx <= 0 || nz <= 0 || map.empty()) return 0;
    const double fx = std::clamp((x - x0) / cell, 0.0, double(nx)), fz = std::clamp((z - z0) / cell, 0.0, double(nz));
    const int i = std::min(int(fx), nx - 1), j = std::min(int(fz), nz - 1);
    const double u = fx - i, v = fz - j;
    const double a = map[index(i, j)], b = map[index(i + 1, j)], c = map[index(i, j + 1)], e = map[index(i + 1, j + 1)];
    return float((a * (1 - u) + b * u) * (1 - v) + (c * (1 - u) + e * u) * v);
}

double Land::height(double x, double z) const {
    const double edge = at(h, x, z);
    if (!to_beyond || inside(x, z)) return edge;
    // How far past the edge, and so how far towards `beyond`.
    const double dx = std::max({x0 - x, x - (x0 + w), 0.0}), dz = std::max({z0 - z, z - (z0 + d), 0.0});
    const double k = over > 0 ? smoothstep(0, 1, std::hypot(dx, dz) / over) : 1.0;
    return edge + (beyond - edge) * k;
}

Vec3d Land::normal(double x, double z) const {
    const double e = std::max(cell * 0.5, 0.05);
    const double dx = (height(x + e, z) - height(x - e, z)) / (2 * e), dz = (height(x, z + e) - height(x, z - e)) / (2 * e);
    const double l = std::sqrt(dx * dx + 1 + dz * dz);
    return {-dx / l, 1 / l, -dz / l};
}

double Land::slope(double x, double z) const {
    const Vec3d n = normal(x, z);
    return std::sqrt(std::max(0.0, 1 - n.y * n.y)) / std::max(n.y, 1e-6);
}

int surface_named(const std::string& n) {
    static const std::map<std::string, int> s{{"grass", 16}, {"sand", 8}, {"earth", 20}, {"mud", 20}, {"dirt", 20},
                                              {"asphalt", 21}, {"gravel", 21}, {"rock", 22}, {"stone", 22}, {"snow", 23},
                                              {"concrete", 11}, {"tiles", 1}, {"plain", 0}};
    const auto it = s.find(n);
    return it == s.end() ? -1 : it->second;
}

namespace kernel {

double Dice::next() {
    uint64_t z = (s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return double((z ^ (z >> 31)) >> 11) / double(1ull << 53);
}

namespace {
double lattice(int64_t i, int64_t j, uint32_t seed) {
    uint64_t h = uint64_t(i) * 0x9E3779B97F4A7C15ull ^ (uint64_t(j) + 0x632BE59BD9B4E019ull) * 0xC2B2AE3D27D4EB4Full ^ uint64_t(seed) * 0x165667B19E3779F9ull;
    h ^= h >> 31, h *= 0xBF58476D1CE4E5B9ull, h ^= h >> 29, h *= 0x94D049BB133111EBull, h ^= h >> 32;
    return double(h >> 11) / double(1ull << 53);
}
}  // namespace

double noise(double x, double z, uint32_t seed) {
    const double fx = std::floor(x), fz = std::floor(z);
    const int64_t i = int64_t(fx), j = int64_t(fz);
    double u = x - fx, v = z - fz;
    // Quintic: smooth in its slope too, so no creases where the cells meet.
    u = u * u * u * (u * (u * 6 - 15) + 10), v = v * v * v * (v * (v * 6 - 15) + 10);
    const double a = lattice(i, j, seed), b = lattice(i + 1, j, seed), c = lattice(i, j + 1, seed), d = lattice(i + 1, j + 1, seed);
    return (a * (1 - u) + b * u) * (1 - v) + (c * (1 - u) + d * u) * v;
}

double fbm(double x, double z, const Octaves& o) {
    double sum = 0, amp = 1, total = 0, f = 1;
    for (int k = 0; k < std::max(1, o.oct); ++k) {
        // Each octave turned a little, so the lattice never lines up.
        const double a = 0.5 * k, c = std::cos(a), s = std::sin(a);
        double n = noise((x * c - z * s) * f + 17.3 * k, (x * s + z * c) * f - 9.1 * k, o.seed + uint32_t(k) * 101u);
        if (o.ridged) n = 1 - std::fabs(n * 2 - 1), n *= n;
        else if (o.billow) n = std::fabs(n * 2 - 1);
        sum += n * amp, total += amp;
        amp *= o.gain, f *= o.lac;
    }
    return sum / total;
}

double smoothstep(double a, double b, double x) {
    const double t = std::clamp((x - a) / (b - a), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}

double within(double x, double lo, double hi, double soft) {
    if (soft <= 0) return x >= lo && x <= hi ? 1.0 : 0.0;
    return smoothstep(lo - soft, lo, x) * (1 - smoothstep(hi, hi + soft, x));
}

Curve curve(const std::vector<Vec3d>& q, double step) {
    Curve c;
    if (q.size() < 2) return c;
    const auto at = [&](std::size_t k, double t) {
        const Vec3d p0 = q[k == 0 ? 0 : k - 1], p1 = q[k], p2 = q[k + 1], p3 = q[std::min(q.size() - 1, k + 2)];
        const double t2 = t * t, t3 = t2 * t;
        return (p1 * 2.0 + (p2 - p0) * t + (p0 * 2.0 - p1 * 5.0 + p2 * 4.0 - p3) * t2 + (p1 * 3.0 - p0 - p2 * 3.0 + p3) * t3) * 0.5;
    };
    // Finely first, then by arc length.
    std::vector<Vec3d> fine;
    for (std::size_t k = 0; k + 1 < q.size(); ++k)
        for (int i = 0; i < 64; ++i) fine.push_back(at(k, i / 64.0));
    fine.push_back(q.back());
    double run = 0, next = 0;
    for (std::size_t i = 0; i < fine.size(); ++i) {
        if (i > 0) run += std::hypot(fine[i].x - fine[i - 1].x, fine[i].z - fine[i - 1].z);
        if (run >= next || i + 1 == fine.size()) {
            c.p.push_back({fine[i].x, 0, fine[i].z});
            c.s.push_back(run);
            next = run + step;
        }
    }
    return c;
}

}  // namespace kernel

// --- the recipe ------------------------------------------------------------------------

namespace {

std::vector<std::string> split(const std::string& s, char by) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == by) out.push_back(cur), cur.clear();
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

class Reader {
public:
    Land land;
    std::vector<WaterAsk> waters;
    std::vector<LayerAsk> layers;
    std::vector<ScatterAsk> scatters;
    std::vector<std::pair<Curve, double>> ways;  // roads with a surface: curve, width
    std::vector<Road> road_looks;
    std::vector<float> flow;
    std::vector<float> swamp;
    uint32_t seed = 1;

    void run(const std::string& text) {
        int n = 0;
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            ++n, line_ = n;
            if (const std::size_t hash = line.find('#'); hash != std::string::npos) line.resize(hash);
            line = trim(line);
            if (line.empty()) continue;
            statement(line);
        }
        if (land.nx == 0) sized(200, 200, 1, 0, 0);
        settle();
    }

private:
    int line_ = 0;
    std::map<std::string, std::string> vars_;

    void err(const std::string& m) { land.errors += "line " + std::to_string(line_) + ": " + m + "\n"; }

    std::string subst(const std::string& tok) {
        std::string out;
        for (std::size_t i = 0; i < tok.size(); ++i) {
            if (tok[i] != '$') {
                out += tok[i];
                continue;
            }
            std::size_t j = i + 1;
            while (j < tok.size() && (std::isalnum(static_cast<unsigned char>(tok[j])) || tok[j] == '_')) ++j;
            const std::string name = tok.substr(i + 1, j - i - 1);
            auto it = vars_.find(name);
            if (it == vars_.end()) err("no variable $" + name), out += "0";
            else out += "(" + it->second + ")";
            i = j - 1;
        }
        return out;
    }
    double num(const std::string& s, double fallback) {
        double v;
        if (sculpt::evaluate(subst(s), v)) return v;
        err("not a number: " + s);
        return fallback;
    }
    std::vector<double> nums(const std::string& s) {
        std::vector<double> out;
        for (const std::string& p : split(s, ',')) out.push_back(num(p, 0));
        return out;
    }
    struct Args {
        std::vector<std::string> pos;
        std::map<std::string, std::string> opt;
    };
    Args args(const std::vector<std::string>& t, std::size_t from) {
        Args a;
        for (std::size_t i = from; i < t.size(); ++i) {
            const std::size_t eq = t[i].find('=');
            if (eq != std::string::npos && eq > 0 && std::isalpha(static_cast<unsigned char>(t[i][0]))) a.opt[t[i].substr(0, eq)] = t[i].substr(eq + 1);
            else a.pos.push_back(t[i]);
        }
        return a;
    }
    double opt(const Args& a, const char* k, double fallback) {
        auto it = a.opt.find(k);
        return it == a.opt.end() ? fallback : num(it->second, fallback);
    }
    bool range(const Args& a, const char* k, Range& r) {
        auto it = a.opt.find(k);
        if (it == a.opt.end()) return false;
        // lo,hi - either left empty: no end that way (`road=6,` six metres off and further).
        const auto parts = split(it->second, ',');
        if (parts.size() != 2) return err(std::string(k) + "= takes lo,hi"), false;
        r.given = true;
        r.lo = trim(parts[0]).empty() ? -1e30 : num(parts[0], -1e30);
        r.hi = trim(parts[1]).empty() ? 1e30 : num(parts[1], 1e30);
        return true;
    }
    Rules rules(const Args& a) {
        Rules r;
        range(a, "height", r.height), range(a, "slope", r.slope), range(a, "wet", r.wet), range(a, "road", r.road), range(a, "water", r.water);
        r.noise = opt(a, "noise", 0), r.scale = opt(a, "patch", 30), r.soft = opt(a, "soft", -1), r.cover = opt(a, "cover", 1);
        return r;
    }
    Vec3d colour(const std::string& s, Vec3d fallback) {
        if (s.size() == 6 && std::all_of(s.begin(), s.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); })) {
            const unsigned v = unsigned(std::stoul(s, nullptr, 16));
            return {((v >> 16) & 255) / 255.0, ((v >> 8) & 255) / 255.0, (v & 255) / 255.0};
        }
        const auto v = nums(s);
        if (v.size() == 3) return {v[0], v[1], v[2]};
        err("a colour is rrggbb or r,g,b: " + s);
        return fallback;
    }
    std::vector<Vec3d> points(const std::vector<std::string>& pos, std::size_t from) {
        std::vector<Vec3d> out;
        for (std::size_t i = from; i < pos.size(); ++i) {
            const auto v = nums(pos[i]);
            if (v.size() == 2) out.push_back({v[0], 0, v[1]});
            else err("expected x,z but got " + pos[i]);
        }
        return out;
    }

    void sized(double w, double d, double cell, double cx, double cz) {
        Land& L = land;
        L.cell = std::max(cell, 0.05);
        L.nx = std::max(2, int(std::lround(w / L.cell))), L.nz = std::max(2, int(std::lround(d / L.cell)));
        if (double(L.nx + 1) * double(L.nz + 1) > 16.8e6) {
            err("more than 16 million heights; a coarser cell");
            L.nx = std::min(L.nx, 4095), L.nz = std::min(L.nz, 4095);
        }
        L.w = L.nx * L.cell, L.d = L.nz * L.cell;
        L.x0 = cx - L.w / 2, L.z0 = cz - L.d / 2;
        const std::size_t n = std::size_t(L.nx + 1) * std::size_t(L.nz + 1);
        L.h.assign(n, 0.0f), L.road.assign(n, 1e6f), L.water.assign(n, 1e6f), L.wet.assign(n, 0.0f);
        flow.assign(n, 0.0f), swamp.assign(n, 0.0f);
    }

    // Each point of the land, by its place, given a new height.
    template <class F>
    void each(F f) {
        Land& L = land;
        for (int j = 0; j <= L.nz; ++j)
            for (int i = 0; i <= L.nx; ++i) {
                float& h = L.h[L.index(i, j)];
                h = float(f(L.x0 + i * L.cell, L.z0 + j * L.cell, double(h)));
            }
    }

    void statement(const std::string& line) {
        std::vector<std::string> t;
        {
            std::istringstream in(line);
            std::string w;
            while (in >> w) t.push_back(w);
        }
        const std::string head = t[0];
        if (head == "thing") return thing(line);
        if (head != "size" && head != "let" && head != "seed" && land.nx == 0) sized(200, 200, 1, 0, 0);
        const Args a = args(t, 1);
        std::vector<double> p;
        for (const std::string& s : a.pos) {
            double v;
            if (sculpt::evaluate(subst(s), v)) p.push_back(v);
        }
        const auto need = [&](std::size_t n) {
            if (p.size() < n) return err(head + " needs " + std::to_string(n) + " numbers"), false;
            return true;
        };
        const uint32_t s = uint32_t(opt(a, "seed", seed));
        if (head == "let") {
            if (a.pos.size() < 2) return err("let name value");
            double v;
            vars_[a.pos[0]] = sculpt::evaluate(subst(a.pos[1]), v) ? std::to_string(v) : subst(a.pos[1]);
        } else if (head == "size") {
            if (!need(2)) return;
            Vec3d at;
            if (a.opt.count("at")) {
                const auto v = nums(a.opt.at("at"));
                if (v.size() == 2) at = {v[0], 0, v[1]};
            }
            sized(p[0], p[1], opt(a, "cell", 1), at.x, at.z);
        } else if (head == "seed") {
            if (need(1)) seed = uint32_t(p[0]);
        } else if (head == "beyond") {
            if (!need(1)) return;
            land.to_beyond = true, land.beyond = p[0], land.over = opt(a, "over", 50);
        } else if (head == "base") {
            if (need(1)) each([&](double, double, double) { return p[0]; });
        } else if (head == "noise") {
            if (!need(2)) return;
            Octaves o;
            o.oct = int(opt(a, "oct", 5)), o.gain = opt(a, "gain", 0.5), o.lac = opt(a, "lac", 2.0);
            o.ridged = opt(a, "ridged", 0) != 0, o.billow = opt(a, "billow", 0) != 0, o.seed = s * 7919u + 13u;
            const double amp = p[0], sc = std::max(p[1], 1e-3), warp = opt(a, "warp", 0);
            Octaves wo = o;
            wo.seed = o.seed + 555u, wo.ridged = wo.billow = false, wo.oct = 3;
            each([&](double x, double z, double h) {
                double u = x / sc, v = z / sc;
                if (warp > 0) u += (fbm(u, v, wo) - 0.5) * 2 * warp, v += (fbm(u + 5.2, v + 1.3, wo) - 0.5) * 2 * warp;
                return h + amp * (fbm(u, v, o) - (o.ridged ? 0.0 : 0.5));
            });
        } else if (head == "hill") {
            if (!need(4)) return;
            const std::string shape = a.opt.count("shape") ? a.opt.at("shape") : "dome";
            each([&](double x, double z, double h) {
                const double r = std::hypot(x - p[0], z - p[1]) / p[2];
                if (r >= 1) return h;
                double k = shape == "cone" ? 1 - r : shape == "mesa" ? 1 - smoothstep(0.7, 1.0, r) : 0.5 + 0.5 * std::cos(r * kPi);
                return h + p[3] * k;
            });
        } else if (head == "ridge") {
            if (!need(2)) return;
            const auto pts = points(a.pos, 2);
            if (pts.size() < 2) return err("ridge needs two or more x,z points");
            const Curve c = curve(pts, land.cell * 0.5);
            line_field(c, p[1] * 0.5, [&](double dist, double h) {
                return h + p[0] * (0.5 + 0.5 * std::cos(std::min(1.0, dist / (p[1] * 0.5)) * kPi));
            });
        } else if (head == "flatten") {
            if (!need(3)) return;
            const double level = a.opt.count("h") ? opt(a, "h", 0) : land.height(p[0], p[1]);
            const double soft = opt(a, "soft", p[2] * 0.5);
            each([&](double x, double z, double h) {
                const double r = std::hypot(x - p[0], z - p[1]);
                return h + (level - h) * (1 - smoothstep(p[2], p[2] + soft, r));
            });
        } else if (head == "plateau") {
            if (!need(1)) return;
            const double k = opt(a, "k", 0.15);
            each([&](double, double, double h) { return h > p[0] ? p[0] + (h - p[0]) * k : h; });
        } else if (head == "terrace") {
            if (!need(1) || p[0] <= 0) return;
            const double sharp = opt(a, "sharp", 0.6);
            each([&](double, double, double h) {
                const double f = h / p[0], fl = std::floor(f), t = f - fl;
                const double stepped = fl + smoothstep(0.5 - 0.5 * (1 - sharp), 0.5 + 0.5 * (1 - sharp), t);
                return (stepped * sharp + f * (1 - sharp)) * p[0];
            });
        } else if (head == "curve") {
            if (!need(1)) return;
            double lo = 1e30, hi = -1e30;
            for (float h : land.h) lo = std::min(lo, double(h)), hi = std::max(hi, double(h));
            if (hi <= lo) return;
            each([&](double, double, double h) { return lo + (hi - lo) * std::pow((h - lo) / (hi - lo), p[0]); });
        } else if (head == "tilt") {
            if (need(2)) each([&](double x, double z, double h) { return h + p[0] * x + p[1] * z; });
        } else if (head == "clamp") {
            if (need(2)) each([&](double, double, double h) { return std::clamp(h, p[0], p[1]); });
        } else if (head == "scale") {
            if (need(1)) each([&](double, double, double h) { return h * p[0]; });
        } else if (head == "smooth") {
            kernel::smooth(land, p.empty() ? 1 : int(p[0]));
        } else if (head == "erode") {
            const double area = double(land.nx) * land.nz;
            kernel::erode(land, flow, int(opt(a, "drops", std::min(area * 0.6, 600000.0))), opt(a, "strength", 1.0), s);
        } else if (head == "thermal") {
            kernel::thermal(land, p.empty() ? 20 : int(p[0]), opt(a, "talus", 35));
        } else if (head == "road" || head == "path") {
            if (!need(1)) return;
            const auto pts = points(a.pos, 1);
            if (pts.size() < 2) return err(head + " needs a width and two or more x,z points");
            Curve c = curve(pts, std::min(2.0, land.cell));
            const bool road = head == "road";
            level_way(land, c, p[0], opt(a, "bank", road ? p[0] * 0.6 : p[0]), opt(a, "sink", road ? 0.1 : 0.05), opt(a, "smooth", road ? 30 : 8));
            if (road) {
                Road r;
                r.name = a.opt.count("name") ? a.opt.at("name") : "road" + std::to_string(road_looks.size() + 1);
                r.width = p[0];
                if (a.opt.count("surface")) {
                    const int sf = surface_named(a.opt.at("surface"));
                    if (sf < 0) err("no surface " + a.opt.at("surface"));
                    else r.surface = sf;
                }
                if (a.opt.count("colour")) r.colour = colour(a.opt.at("colour"), r.colour);
                r.lines = opt(a, "lines", 1) != 0;
                ways.push_back({c, p[0]});
                road_looks.push_back(r);
            }
        } else if (head == "river") {
            if (!need(2)) return;
            const auto pts = points(a.pos, 2);
            if (pts.size() < 2) return err("river needs a width, a depth and two or more x,z points");
            WaterAsk w;
            w.kind = WaterAsk::River;
            w.name = a.opt.count("name") ? a.opt.at("name") : "river" + std::to_string(waters.size() + 1);
            w.along = curve(pts, std::min(2.0, land.cell));
            w.width = p[0];
            if (a.opt.count("colour")) w.shallow = colour(a.opt.at("colour"), w.shallow);
            if (a.opt.count("deep")) w.deep = colour(a.opt.at("deep"), w.deep);
            cut_river(land, w.along, p[0], p[1], w.surface);
            waters.push_back(w);
        } else if (head == "lake" || head == "sea" || head == "swamp") {
            WaterAsk w;
            w.kind = head == "lake" ? WaterAsk::Lake : head == "sea" ? WaterAsk::Sea : WaterAsk::Swamp;
            w.name = a.opt.count("name") ? a.opt.at("name") : head + std::to_string(waters.size() + 1);
            // A swamp's water is murky: peat-brown, black where deep.
            if (head == "swamp") w.shallow = {0.22, 0.21, 0.14}, w.deep = {0.05, 0.05, 0.03};
            if (a.opt.count("colour")) w.shallow = colour(a.opt.at("colour"), w.shallow);
            if (a.opt.count("deep")) w.deep = colour(a.opt.at("deep"), w.deep);
            if (head == "sea") {
                if (!need(1)) return;
                w.level = p[0], w.level_given = true;
            } else {
                if (!need(head == "lake" ? 2 : 3)) return;
                w.x = p[0], w.z = p[1], w.r = head == "swamp" ? p[2] : 0;
                if (a.opt.count("level")) w.level = opt(a, "level", 0), w.level_given = true;
            }
            if (head == "swamp") {
                // Low wet ground: brought down near its level, hummocky, with
                // hollows that will hold water.
                if (!w.level_given) w.level = land.height(w.x, w.z), w.level_given = true;
                const double pools = opt(a, "pools", 0.5);
                Octaves o;
                o.oct = 4, o.seed = s * 31u + 7u;
                Land& L = land;
                for (int j = 0; j <= L.nz; ++j)
                    for (int i = 0; i <= L.nx; ++i) {
                        const double x = L.x0 + i * L.cell, z = L.z0 + j * L.cell;
                        const double k = 1 - smoothstep(w.r * 0.6, w.r, std::hypot(x - w.x, z - w.z));
                        if (k <= 0) continue;
                        const double hummock = (fbm(x / 9, z / 9, o) - 0.5) * 1.4 + (0.5 - pools) * 0.5 + 0.12;
                        float& h = L.h[L.index(i, j)];
                        h = float(h + (w.level + hummock - h) * k * 0.9);
                        swamp[L.index(i, j)] = float(std::max(double(swamp[L.index(i, j)]), k));
                    }
            }
            waters.push_back(w);
        } else if (head == "layer") {
            if (a.pos.size() < 2) return err("layer name surface ...");
            LayerAsk l;
            l.layer.name = a.pos[0];
            l.layer.surface = surface_named(a.pos[1]);
            if (l.layer.surface < 0) return err("no surface " + a.pos[1] + " (grass sand earth mud rock gravel asphalt snow)");
            static const std::map<std::string, Vec3d> colours{{"grass", {0.30, 0.38, 0.17}}, {"sand", {0.62, 0.56, 0.42}}, {"earth", {0.32, 0.25, 0.18}},
                                                              {"mud", {0.17, 0.14, 0.10}}, {"dirt", {0.36, 0.29, 0.21}}, {"asphalt", {0.15, 0.15, 0.15}},
                                                              {"gravel", {0.42, 0.40, 0.37}}, {"rock", {0.42, 0.40, 0.38}}, {"stone", {0.45, 0.44, 0.42}},
                                                              {"snow", {0.92, 0.93, 0.96}}};
            if (auto it = colours.find(a.pos[1]); it != colours.end()) l.layer.colour = it->second;
            if (a.opt.count("colour")) l.layer.colour = colour(a.opt.at("colour"), l.layer.colour);
            l.rules = rules(a);
            if (layers.size() >= 4) return err("four layers at most");
            layers.push_back(l);
        } else if (head == "scatter") {
            if (a.pos.empty()) return err("scatter which thing?");
            ScatterAsk sc;
            sc.thing = a.pos[0];
            sc.rules = rules(a);
            sc.density = opt(a, "density", 1), sc.spacing = opt(a, "spacing", 0), sc.sink = opt(a, "sink", 0.2);
            sc.count = int(opt(a, "count", -1)), sc.most = int(opt(a, "most", 20000)), sc.seed = s * 131u + uint32_t(scatters.size()) * 977u + 3u;
            if (a.opt.count("scale")) {
                const auto v = nums(a.opt.at("scale"));
                if (v.size() == 2) sc.lo = v[0], sc.hi = v[1];
                else if (v.size() == 1) sc.lo = sc.hi = v[0];
            }
            scatters.push_back(sc);
        } else {
            err("no such statement: " + head);
        }
    }

    // `thing name [variants=n] : recipe / recipe ...`
    void thing(const std::string& line) {
        const std::size_t colon = line.find(" : ");
        if (colon == std::string::npos) return err("thing name [variants=n] : <recipe>");
        std::istringstream in(line.substr(0, colon));
        std::string w, name;
        in >> w >> name;
        int variants = 1;
        while (in >> w)
            if (w.rfind("variants=", 0) == 0) variants = std::max(1, int(num(w.substr(9), 1)));
        std::string recipe = line.substr(colon + 3);
        // The modeller's lines, split by ` / `.
        for (std::size_t k; (k = recipe.find(" / ")) != std::string::npos;) recipe.replace(k, 3, "\n");
        Thing t;
        t.name = name;
        for (int v = 1; v <= variants; ++v) {
            std::string r = recipe;
            for (std::size_t k = 0; (k = r.find("$v", k)) != std::string::npos;) {
                if (k + 2 < r.size() && (std::isalnum(static_cast<unsigned char>(r[k + 2])) || r[k + 2] == '_')) {
                    k += 2;
                    continue;
                }
                r.replace(k, 2, std::to_string(v));
            }
            t.recipes.push_back(r);
        }
        land.things.push_back(t);
    }

    // A pass over the points near a curve: each given a new height by its
    // distance from it.
    template <class F>
    void line_field(const Curve& c, double reach, F f) {
        Land& L = land;
        std::vector<float> best(L.h.size(), 1e30f);
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
                    float& bst = best[L.index(i, j)];
                    if (dist < bst) bst = float(dist);
                }
        }
        for (std::size_t k = 0; k < L.h.size(); ++k)
            if (best[k] < reach) L.h[k] = float(f(best[k], L.h[k]));
    }

    void settle() {
        settle_water(land, waters, swamp);
        wetness(land, flow, swamp);
        cover(land, layers, seed);
        for (std::size_t k = 0; k < ways.size(); ++k) {
            Road r = ribbon(land, ways[k].first, ways[k].second);
            const Road& look = road_looks[k];
            r.name = look.name, r.surface = look.surface, r.colour = look.colour, r.lines = look.lines;
            land.roads.push_back(std::move(r));
        }
        strew(land, scatters);
    }
};

std::mutex& memo_mutex() {
    static std::mutex m;
    return m;
}
std::unordered_map<std::string, std::shared_ptr<const Land>>& memo() {
    static std::unordered_map<std::string, std::shared_ptr<const Land>> m;
    return m;
}

}  // namespace

Land make(const std::string& recipe) {
    Reader r;
    r.run(recipe);
    return std::move(r.land);
}

std::shared_ptr<const Land> build(const std::string& recipe) {
    {
        std::lock_guard<std::mutex> g(memo_mutex());
        if (auto it = memo().find(recipe); it != memo().end()) return it->second;
    }
    const Digest key = Hasher{}.text("sg.terrain").text(SG_TERRAIN_CODE).text(recipe).digest();
    std::string bytes;
    auto land = std::make_shared<Land>();
    if (!(cache::load("terrain", key, bytes) && kernel::from_bytes(bytes, *land))) {
        *land = make(recipe);
        cache::store("terrain", key, kernel::to_bytes(*land));
    }
    std::lock_guard<std::mutex> g(memo_mutex());
    if (memo().size() > 16) memo().clear();
    return memo().emplace(recipe, std::move(land)).first->second;
}

}  // namespace sg::terrain
