#include "sg/domains/Texture.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <mutex>

namespace sg {

namespace {

std::map<std::string, Texture::Generator>& generators() {
    static std::map<std::string, Texture::Generator> all;
    return all;
}

Texture::Reader& reader() {
    static Texture::Reader r;
    return r;
}

// Value noise that tiles with period `n` cells each way.
double lattice(int x, int y, int n, uint32_t seed) {
    x = ((x % n) + n) % n, y = ((y % n) + n) % n;
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<double>((h ^ (h >> 16)) & 0xffffff) / static_cast<double>(0xffffff);
}

double tiled_noise(double u, double v, int n, uint32_t seed) {
    const double x = u * n, y = v * n;
    const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
    const double fx = x - x0, fy = y - y0;
    const double sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
    const double a = lattice(x0, y0, n, seed), b = lattice(x0 + 1, y0, n, seed);
    const double c = lattice(x0, y0 + 1, n, seed), d = lattice(x0 + 1, y0 + 1, n, seed);
    return (a + (b - a) * sx) + ((c + (d - c) * sx) - (a + (b - a) * sx)) * sy;
}

void builtins() {
    static std::once_flag once;
    std::call_once(once, [] {
        auto& g = generators();
        g["plain"] = [](const Params&, int, double, double) { return std::array<double, 4>{1, 1, 1, 1}; };
        g["noise"] = [](const Params& p, int, double u, double v) {
            const int n = std::max(1, static_cast<int>(p.num("scale", 8.0)));
            const uint32_t seed = static_cast<uint32_t>(p.num("seed", 1.0));
            double s = 0, w = 0.5, t = 0;
            for (int o = 0; o < 4; ++o, w *= 0.5) s += w * tiled_noise(u, v, n << o, seed + o), t += w;
            const double k = 0.55 + 0.45 * s / t;
            return std::array<double, 4>{k, k, k, 1};
        };
        g["checks"] = [](const Params& p, int, double u, double v) {
            const int n = std::max(1, static_cast<int>(p.num("count", 4.0)));
            const double k = ((static_cast<int>(u * n) + static_cast<int>(v * n)) % 2) ? 1.0 : 0.6;
            return std::array<double, 4>{k, k, k, 1};
        };
    });
}

long long stamp_of(const std::string& path) {
    std::error_code ec;
    const auto t = std::filesystem::last_write_time(path, ec);
    return ec ? 0 : static_cast<long long>(t.time_since_epoch().count());
}

int to_byte(double c) { return static_cast<int>(std::lround(std::clamp(c, 0.0, 1.0) * 255.0)); }

}  // namespace

void Texture::define(const std::string& name, Generator g) {
    builtins();
    generators()[name] = std::move(g);
}

void Texture::set_reader(Reader r) { reader() = std::move(r); }

Texture::Texture(Key id, int cell_px) : Surface2D(id, 3, 2, cell_px) {
    builtins();
    set_srgb(true);
    Element& m = add_element(map_id(), Key{"texture"});
    m.params.set("generator", std::string("plain")).set("seed", 1.0).set("tint_r", 1.0).set("tint_g", 1.0).set("tint_b", 1.0);
    m.params.set("layer", std::string()).set("layer_mix", 1.0).set("tile", 0.0).set("blend", 0.0).set("relief", 0.0);
    // map --set--> map: whichever settings are given.
    loop(Key{"set"}, map_id(), set_event(), [](State&, Element& e, Element*, const Event& ev) {
        for (const auto& [k, v] : ev.args) e.params.set(k, v);
    });
}

bool Texture::stale() {
    const Element& m = map();
    const std::string layer = m.params.get_or<std::string>("layer", "");
    const long long t = layer.empty() ? 0 : stamp_of(layer);
    return m.params.stamp() != painted_stamp_ || layer != layer_path_ || t != layer_time_;
}

void Texture::paint() {
    const Element& m = map();
    painted_stamp_ = m.params.stamp();
    layer_path_ = m.params.get_or<std::string>("layer", "");
    layer_time_ = layer_path_.empty() ? 0 : stamp_of(layer_path_);
    auto& px = pixels();
    const int c = cell(), W = px_w();
    const auto it = generators().find(m.params.get_or<std::string>("generator", "plain"));
    const Generator& gen = it != generators().end() ? it->second : generators()["plain"];
    const double tr = m.params.num("tint_r", 1.0), tg = m.params.num("tint_g", 1.0), tb = m.params.num("tint_b", 1.0);
    for (int cl = 0; cl < 6; ++cl) {
        const int x0 = (cl % 3) * c, y0 = (cl / 3) * c;
        for (int y = 0; y < c; ++y)
            for (int x = 0; x < c; ++x) {
                const auto rgba = gen(m.params, cl, (x + 0.5) / c, 1.0 - (y + 0.5) / c);
                unsigned char* p = &px[(static_cast<std::size_t>(y0 + y) * W + (x0 + x)) * 4];
                p[0] = static_cast<unsigned char>(to_byte(rgba[0] * tr));
                p[1] = static_cast<unsigned char>(to_byte(rgba[1] * tg));
                p[2] = static_cast<unsigned char>(to_byte(rgba[2] * tb));
                p[3] = static_cast<unsigned char>(to_byte(rgba[3]));
            }
    }
    // The layer painted over it, stretched to the map if it is another size.
    int lw = 0, lh = 0;
    std::vector<unsigned char> layer;
    if (!layer_path_.empty() && reader() && reader()(layer_path_, lw, lh, layer) && lw > 0 && lh > 0) {
        const double mix = std::clamp(m.params.num("layer_mix", 1.0), 0.0, 1.0);
        const int H = px_h();
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const unsigned char* l = &layer[(static_cast<std::size_t>(y * lh / H) * lw + static_cast<std::size_t>(x * lw / W)) * 4];
                const double a = l[3] / 255.0 * mix;
                unsigned char* p = &px[(static_cast<std::size_t>(y) * W + x) * 4];
                for (int k = 0; k < 3; ++k) p[k] = static_cast<unsigned char>(std::lround(p[k] + (l[k] - p[k]) * a));
            }
    }
}

std::vector<float> unit_shape(const std::string& shape) {
    std::vector<float> out;
    const auto corner = [&](const Vec3d& p, const Vec3d& n) {
        for (double v : {p.x, p.y, p.z, n.x, n.y, n.z, 0.0, 0.0}) out.push_back(static_cast<float>(v));
    };
    if (shape == "sphere" || shape == "cylinder") {
        const int sides = 24, rings = shape == "sphere" ? 12 : 1;
        const auto at = [&](int i, int j) {
            const double a = 2 * 3.14159265358979 * i / sides;
            if (shape == "cylinder") return Vec3d{0.5 * std::cos(a), j ? 0.5 : -0.5, 0.5 * std::sin(a)};
            const double b = 3.14159265358979 * j / rings - 3.14159265358979 / 2;
            return Vec3d{0.5 * std::cos(b) * std::cos(a), 0.5 * std::sin(b), 0.5 * std::cos(b) * std::sin(a)};
        };
        const auto normal = [&](const Vec3d& p) {
            const Vec3d q = shape == "cylinder" ? Vec3d{p.x, 0, p.z} : p;
            const double l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
            return Vec3d{q.x / l, q.y / l, q.z / l};
        };
        for (int j = 0; j < rings; ++j)
            for (int i = 0; i < sides; ++i) {
                const Vec3d a = at(i, j), b = at(i + 1, j), c = at(i + 1, j + 1), d = at(i, j + 1);
                for (const Vec3d& p : {a, b, c, a, c, d}) corner(p, normal(p));
            }
        if (shape == "cylinder")
            for (int i = 0; i < sides; ++i)
                for (int j = 0; j < 2; ++j) {
                    const Vec3d n{0, j ? 1.0 : -1.0, 0}, o{0, j ? 0.5 : -0.5, 0};
                    for (const Vec3d& p : {o, at(i, j), at(i + 1, j)}) corner(p, n);
                }
        return out;
    }
    // A box: two triangles a face.
    for (int axis = 0; axis < 3; ++axis)
        for (double s : {-1.0, 1.0}) {
            Vec3d n{0, 0, 0};
            (axis == 0 ? n.x : axis == 1 ? n.y : n.z) = s;
            const auto p = [&](double a, double b) {
                Vec3d q{0, 0, 0};
                (axis == 0 ? q.x : axis == 1 ? q.y : q.z) = 0.5 * s;
                (axis == 0 ? q.y : axis == 1 ? q.z : q.x) = a;
                (axis == 0 ? q.z : axis == 1 ? q.x : q.y) = b;
                return q;
            };
            for (const Vec3d& q : {p(-0.5, -0.5), p(0.5, -0.5), p(0.5, 0.5), p(-0.5, -0.5), p(0.5, 0.5), p(-0.5, 0.5)}) corner(q, n);
        }
    return out;
}

std::vector<unsigned char> projection_guide(const std::vector<float>& tris, int c) {
    const int W = 3 * c, H = 2 * c;
    std::vector<unsigned char> out(static_cast<std::size_t>(W) * H * 4, 0);
    // Each cell, as `skin_uv` reads it: where on the cell a point is seen, and
    // how near the eye it is, looking at the thing from that way.
    const auto view = [](int cell, const Vec3d& p, double& u, double& v, double& near) {
        switch (cell) {
            case 0: u = 0.5 - p.z, v = p.y + 0.5, near = p.x; break;
            case 1: u = p.z + 0.5, v = p.y + 0.5, near = -p.x; break;
            case 2: u = p.x + 0.5, v = p.y + 0.5, near = p.z; break;
            case 3: u = 0.5 - p.x, v = p.y + 0.5, near = -p.z; break;
            case 4: u = p.x + 0.5, v = 0.5 - p.z, near = p.y; break;
            default: u = p.x + 0.5, v = p.z + 0.5, near = -p.y; break;
        }
    };
    static const Vec3d axes[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {0, 1, 0}, {0, -1, 0}};
    for (int cell = 0; cell < 6; ++cell) {
        const int x0 = (cell % 3) * c, y0 = (cell / 3) * c;
        std::vector<double> depth(static_cast<std::size_t>(c) * c, -1e9);
        std::vector<double> shade(static_cast<std::size_t>(c) * c, -1.0);
        for (std::size_t t = 0; t + 24 <= tris.size(); t += 24) {
            double su[3], sv[3], sn[3];
            for (int k = 0; k < 3; ++k) {
                const float* f = &tris[t + 8 * k];
                view(cell, {f[0], f[1], f[2]}, su[k], sv[k], sn[k]);
                su[k] *= c, sv[k] = (1.0 - sv[k]) * c;
            }
            const float* f = &tris[t];
            const double lit = 0.3 + 0.7 * std::max(0.0, f[3] * axes[cell].x + f[4] * axes[cell].y + f[5] * axes[cell].z);
            const double area = (su[1] - su[0]) * (sv[2] - sv[0]) - (su[2] - su[0]) * (sv[1] - sv[0]);
            if (std::fabs(area) < 1e-12) continue;
            const int bx0 = std::max(0, static_cast<int>(std::floor(std::min({su[0], su[1], su[2]})))),
                      bx1 = std::min(c - 1, static_cast<int>(std::ceil(std::max({su[0], su[1], su[2]})))),
                      by0 = std::max(0, static_cast<int>(std::floor(std::min({sv[0], sv[1], sv[2]})))),
                      by1 = std::min(c - 1, static_cast<int>(std::ceil(std::max({sv[0], sv[1], sv[2]}))));
            for (int y = by0; y <= by1; ++y)
                for (int x = bx0; x <= bx1; ++x) {
                    const double px = x + 0.5, py = y + 0.5;
                    const double w0 = ((su[1] - px) * (sv[2] - py) - (su[2] - px) * (sv[1] - py)) / area;
                    const double w1 = ((su[2] - px) * (sv[0] - py) - (su[0] - px) * (sv[2] - py)) / area;
                    const double w2 = 1 - w0 - w1;
                    if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                    const double z = w0 * sn[0] + w1 * sn[1] + w2 * sn[2];
                    const std::size_t i = static_cast<std::size_t>(y) * c + x;
                    if (z > depth[i]) depth[i] = z, shade[i] = lit;
                }
        }
        for (int y = 0; y < c; ++y)
            for (int x = 0; x < c; ++x) {
                const std::size_t i = static_cast<std::size_t>(y) * c + x;
                unsigned char* p = &out[(static_cast<std::size_t>(y0 + y) * W + (x0 + x)) * 4];
                const bool in = shade[i] >= 0;
                // Its outline: where it ends, and where its depth breaks.
                bool edge = false;
                for (const auto& [dx, dy] : {std::pair{1, 0}, std::pair{0, 1}, std::pair{-1, 0}, std::pair{0, -1}}) {
                    const int nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= c || ny >= c) continue;
                    const std::size_t j = static_cast<std::size_t>(ny) * c + nx;
                    if ((shade[j] >= 0) != in || (in && std::fabs(depth[j] - depth[i]) > 0.04)) edge = true;
                }
                const bool border = x == 0 || y == 0 || x == c - 1 || y == c - 1;
                const int k = edge ? 20 : in ? static_cast<int>(60 + 150 * shade[i]) : 0;
                p[0] = p[1] = p[2] = static_cast<unsigned char>(border ? 255 : k);
                p[3] = static_cast<unsigned char>(border || edge ? 255 : in ? 110 : 0);
            }
    }
    return out;
}

}  // namespace sg
