#include <chrono>
#include "sg/domains/Texture.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>
#include <typeinfo>

#include "SG_TEXTURE_CODE.hpp"

namespace sg {

namespace {

std::map<std::string, Texture::Generator>& generators() {
    static std::map<std::string, Texture::Generator> all;
    return all;
}

std::map<std::string, Texture::Material>& materials() {
    static std::map<std::string, Texture::Material> all;
    return all;
}

// The generators whose code is this file's: what they paint is named by
// their name and this code's digest. One a program defines (or defines
// again) is code no digest knows.
std::set<std::string>& own_code() {
    static std::set<std::string> all{"plain", "noise", "checks", "rust"};
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

// Four octaves of tiling value noise, 0..1, `n` cells across at the coarsest.
double fbm(double u, double v, int n, uint32_t seed) {
    double s = 0, w = 0.5, t = 0;
    for (int o = 0; o < 4; ++o, w *= 0.5) s += w * tiled_noise(u, v, n << o, seed + 17 * o), t += w;
    return s / t;
}

// A material's colour and height, as a generator gives them.
Texture::Generator colour_of(Texture::Material m) {
    return [m = std::move(m)](const Params& p, int cell, double u, double v) {
        const Texture::Channels c = m(p, cell, u, v);
        return std::array<double, 4>{c.r, c.g, c.b, c.height};
    };
}

// Steel, painted, the paint chipped to bright metal at the edges of rust
// that has eaten through it: every channel of it. `rust` (0..1) how much,
// `scale` its patches across a cell, `paint_r/g/b` the paint's colour (as
// picked by eye), `seed`.
Texture::Channels rust(const Params& p, int, double u, double v) {
    const int n = std::max(1, static_cast<int>(p.num("scale", 3.0)));
    const uint32_t seed = static_cast<uint32_t>(p.num("seed", 1.0));
    const double amount = std::clamp(p.num("rust", 0.45), 0.0, 1.0);
    const double patches = fbm(u, v, n, seed), fine = fbm(u, v, n * 8, seed + 101), grain = tiled_noise(u, v, n * 64, seed + 7);
    // Where it has rusted: the patches past a level set by how much, with a
    // ragged edge; round it a band where the paint has flaked off the steel.
    const double level = 1.0 - amount, edge = patches + 0.12 * (fine - 0.5);
    const double rusted = smoothstep(level - 0.02, level + 0.06, edge);
    const double bare = smoothstep(level - 0.09, level - 0.03, edge) * (1.0 - rusted);
    Texture::Channels c;
    const double pr = p.num("paint_r", 0.30), pg = p.num("paint_g", 0.37), pb = p.num("paint_b", 0.34);
    const double wear = 0.9 + 0.2 * (fine - 0.5);
    double r = pr * wear, g = pg * wear, b = pb * wear, rough = 0.42 + 0.12 * fine, metal = 0.0, height = 0.55, occl = 1.0;
    // Bare steel: bright, smooth, metal, a hair below the paint.
    const double steel = 0.62 + 0.08 * grain;
    r += (steel - r) * bare, g += (steel - g) * bare, b += (steel * 1.02 - b) * bare;
    rough += (0.28 + 0.1 * grain - rough) * bare, metal += (1.0 - metal) * bare, height += (0.5 - height) * bare;
    // Rust: orange to dark brown by its own noise, rough, not metal, standing
    // up in flakes, its pits dark and shut to the light from all round.
    const double t = fine * 0.7 + grain * 0.3;
    const double rr = 0.45 - 0.2 * t, rg = 0.22 - 0.11 * t, rb = 0.10 - 0.05 * t;
    r += (rr - r) * rusted, g += (rg - g) * rusted, b += (rb - b) * rusted;
    rough += (0.88 + 0.1 * grain - rough) * rusted, metal -= metal * rusted;
    height += (0.6 + 0.4 * grain - height) * rusted;
    occl -= 0.45 * rusted * (1.0 - grain);
    c.r = r, c.g = g, c.b = b, c.height = height, c.roughness = rough, c.metal = metal, c.occlusion = occl;
    return c;
}

void builtins() {
    static std::once_flag once;
    std::call_once(once, [] {
        materials()["rust"] = rust;
        generators()["rust"] = colour_of(rust);
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

// A picture file (`lw` x `lh`, RGBA) laid over `px` (`W` x `H`, cells of `c`
// pixels): stretched over the whole of it, or - `per_cell` - whole in every
// cell, as one tile of a material is. Each pixel goes as far toward it as its
// alpha times `mix` says; `flip_green` turns a DirectX normal map's green.
void lay_over(std::vector<unsigned char>& px, int W, int H, int c, const std::vector<unsigned char>& over, int lw, int lh, bool per_cell,
              double mix, bool flip_green = false) {
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const int sx = per_cell ? (x % c) * lw / c : x * lw / W, sy = per_cell ? (y % c) * lh / c : y * lh / H;
            const unsigned char* l = &over[(static_cast<std::size_t>(sy) * lw + static_cast<std::size_t>(sx)) * 4];
            const double a = l[3] / 255.0 * mix;
            unsigned char* p = &px[(static_cast<std::size_t>(y) * W + x) * 4];
            for (int k = 0; k < 3; ++k) {
                const int to = k == 1 && flip_green ? 255 - l[k] : l[k];
                p[k] = static_cast<unsigned char>(std::lround(p[k] + (to - p[k]) * a));
            }
        }
}

}  // namespace

void Texture::define(const std::string& name, Generator g) {
    builtins();
    own_code().erase(name);
    generators()[name] = std::move(g);
}

void Texture::define_material(const std::string& name, Material m) {
    builtins();
    own_code().erase(name);
    generators()[name] = colour_of(m);
    materials()[name] = std::move(m);
}

void Texture::set_reader(Reader r) { reader() = std::move(r); }

bool Texture::has_surface() const {
    const Element& m = map();
    return materials().count(m.params.get_or<std::string>("generator", "plain")) > 0 ||
           !m.params.get_or<std::string>("surface_layer", "").empty();
}

const std::vector<unsigned char>& Texture::surface_raster() {
    // (Of the picture as it is now: brought up to date first, which costs
    // nothing when nothing changed.)
    catch_up();
    const Element& m = map();
    const std::string layer = m.params.get_or<std::string>("surface_layer", "");
    if (!has_surface()) {
        if (!surface_px_.empty()) std::vector<unsigned char>{}.swap(surface_px_), ++surface_revision_;
        return surface_px_;
    }
    if (!surface_px_.empty() && surface_made_ == revision()) return surface_px_;
    surface_made_ = revision();
    const int c = cell(), W = px_w(), H = px_h(), wide = cols();
    surface_px_.assign(static_cast<std::size_t>(W) * H * 4, 255);
    // Unless a material says, a surface as a thing is: open, 0.6 rough, not metal.
    const auto it = materials().find(m.params.get_or<std::string>("generator", "plain"));
    for (int cl = 0; cl < wide * rows(); ++cl) {
        const int x0 = (cl % wide) * c, y0 = (cl / wide) * c;
        for (int y = 0; y < c; ++y)
            for (int x = 0; x < c; ++x) {
                const Channels ch = it != materials().end() ? it->second(m.params, cl, (x + 0.5) / c, 1.0 - (y + 0.5) / c) : Channels{};
                unsigned char* p = &surface_px_[(static_cast<std::size_t>(y0 + y) * W + (x0 + x)) * 4];
                p[0] = static_cast<unsigned char>(to_byte(ch.occlusion));
                p[1] = static_cast<unsigned char>(to_byte(ch.roughness));
                p[2] = static_cast<unsigned char>(to_byte(ch.metal));
            }
    }
    // The surface layer over it, as the colour's layer is over the colour.
    int lw = 0, lh = 0;
    std::vector<unsigned char> over;
    if (!layer.empty() && reader() && reader()(layer, lw, lh, over) && lw > 0 && lh > 0)
        lay_over(surface_px_, W, H, c, over, lw, lh, m.params.num("per_cell", 0.0) > 0.5, 1.0);
    ++surface_revision_;
    return surface_px_;
}

bool Texture::has_normals() const {
    const Element& m = map();
    return m.params.num("normals", 0.0) > 0.5 || !m.params.get_or<std::string>("normal_layer", "").empty();
}

const std::vector<unsigned char>& Texture::normal_raster() {
    catch_up();
    const Element& m = map();
    const std::string layer = m.params.get_or<std::string>("normal_layer", "");
    if (!has_normals()) {
        if (!normal_px_.empty()) std::vector<unsigned char>{}.swap(normal_px_), ++normal_revision_;
        return normal_px_;
    }
    if (!normal_px_.empty() && normal_made_ == revision()) return normal_px_;
    normal_made_ = revision();
    const int c = cell(), W = px_w(), H = px_h(), wide = cols();
    normal_px_.assign(static_cast<std::size_t>(W) * H * 4, 255);
    // Flat unless it is made from the height: straight out of every cell.
    for (std::size_t i = 0; i < normal_px_.size(); i += 4) normal_px_[i] = normal_px_[i + 1] = 128;
    if (m.params.num("normals", 0.0) > 0.5) {
        // Made from the height as the generator says it, not as its picture
        // rounds it to 256 steps: the slope across half a pixel each way, in
        // metres of height (`relief` at full) a metre across (`tile` a cell;
        // a cell a metre where it does not tile).
        const std::string name = m.params.get_or<std::string>("generator", "plain");
        const auto mat = materials().find(name);
        const auto gen = generators().find(name);
        const auto height = [&](int cl, double u, double v) {
            if (mat != materials().end()) return mat->second(m.params, cl, u, v).height;
            return gen != generators().end() ? gen->second(m.params, cl, u, v)[3] : 1.0;
        };
        const double tile = m.params.num("tile", 0.0), relief = m.params.num("relief", 0.0);
        const double across = tile > 0.0 ? tile : 1.0, e = 0.5 / c;
        const auto at = [&](double s) { return tile > 0.0 ? s : std::clamp(s, 0.0, 1.0); };
        for (int cl = 0; cl < wide * rows(); ++cl) {
            const int x0 = (cl % wide) * c, y0 = (cl / wide) * c;
            for (int y = 0; y < c; ++y)
                for (int x = 0; x < c; ++x) {
                    const double u = (x + 0.5) / c, v = 1.0 - (y + 0.5) / c;
                    const double du = (height(cl, at(u + e), v) - height(cl, at(u - e), v)) / (at(u + e) - at(u - e));
                    const double dv = (height(cl, u, at(v + e)) - height(cl, u, at(v - e))) / (at(v + e) - at(v - e));
                    const double sx = -relief * du / across, sy = -relief * dv / across, len = std::sqrt(sx * sx + sy * sy + 1.0);
                    unsigned char* p = &normal_px_[(static_cast<std::size_t>(y0 + y) * W + (x0 + x)) * 4];
                    p[0] = static_cast<unsigned char>(to_byte(0.5 + 0.5 * sx / len));
                    p[1] = static_cast<unsigned char>(to_byte(0.5 + 0.5 * sy / len));
                    p[2] = static_cast<unsigned char>(to_byte(0.5 + 0.5 / len));
                }
        }
    }
    // A normal map read from a file, over the made one.
    int lw = 0, lh = 0;
    std::vector<unsigned char> over;
    if (!layer.empty() && reader() && reader()(layer, lw, lh, over) && lw > 0 && lh > 0)
        lay_over(normal_px_, W, H, c, over, lw, lh, m.params.num("per_cell", 0.0) > 0.5, 1.0, m.params.num("normal_dx", 0.0) > 0.5);
    ++normal_revision_;
    return normal_px_;
}

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

bool Texture::one_tile() const {
    const Params& p = map().params;
    return p.num("per_cell", 0.0) > 0.5 && p.is(Key{"generator"}, "plain") && own_code().count("plain") > 0;
}

void Texture::fit() {
    const bool one = one_tile();
    if (one ? (cols() != 1 || rows() != 1) : (cols() != 3 || rows() != 2)) resize(one ? 1 : 3, one ? 1 : 2);
}

Texture::Stamps Texture::look() const {
    const Params& p = map().params;
    Stamps s{};
    const char* files[3] = {"layer", "surface_layer", "normal_layer"};
    for (std::size_t i = 0; i < 3; ++i)
        if (const std::string* f = p.text(Key{files[i]}); f && !f->empty()) s[i] = stamp_of(*f);
    return s;
}

bool Texture::namable() const {
    // A kind of texture of a program's own paints with its own code (it
    // overrides paint): what it paints is known only by its pixels, never by
    // a Texture's settings - two of them alike in settings are not alike.
    if (typeid(*this) != typeid(Texture)) return false;
    const std::string* g = map().params.text(Key{"generator"});
    return own_code().count(g ? *g : std::string("plain")) > 0;
}

Digest Texture::made_of(const char* which) const {
    Hasher h;
    h.text("sg.texture").text(which).text(SG_TEXTURE_CODE).integer(cell()).integer(cols()).integer(rows()).integer(srgb() ? 1 : 0);
    // Its settings, in the order of their names (not the order they were set).
    std::vector<const std::pair<Key, Value>*> all;
    for (const auto& e : map().params) all.push_back(&e);
    std::sort(all.begin(), all.end(), [](const auto* a, const auto* b) { return a->first.str() < b->first.str(); });
    for (const auto* e : all) h.text(e->first.str()).integer(static_cast<int64_t>(e->second.index())).text(to_string(e->second));
    // And its files, where they stand: written again, they are other files.
    for (long long t : painted_files_) h.integer(t);
    return h.digest();
}

void Texture::named() {
    painted_stamp_ = map().params.stamp();
    painted_files_ = seen_;
    keyed_ = namable();
}

bool Texture::pixels_digest(Digest& out) const {
    if (!keyed_) return false;
    out = made_of("colour");
    return true;
}

bool Texture::surface_digest(Digest& out) const {
    if (!keyed_ || !has_surface()) return false;
    out = made_of("surface");
    return true;
}

bool Texture::normal_digest(Digest& out) const {
    if (!keyed_ || !has_normals()) return false;
    out = made_of("normal");
    return true;
}

bool Texture::name_now() {
    if (!namable()) return false;
    named();
    return true;
}

bool Texture::stale() {
    // Its grid, and its files' stamps - looked at once each time it is
    // brought up to date, never as it is drawn.
    fit();
    // Its files' stamps read again when its settings move (another file
    // named), and otherwise at most about once a second - each texture at a
    // moment of its own, so they are not all read in one frame. A stamp read
    // is a question to the file system (a tenth of a millisecond on some),
    // and a room wears hundreds of textures: read for every frame it is
    // drawn, that was most of a frame. A file changed from outside is seen
    // within the second.
    const uint64_t settings = map().params.stamp();
    const auto now = std::chrono::steady_clock::now();
    if (settings != looked_stamp_ || now >= next_look_) {
        seen_ = look();
        looked_stamp_ = settings;
        next_look_ = now + std::chrono::milliseconds(800 + static_cast<int>((reinterpret_cast<std::uintptr_t>(this) >> 4) % 400));
    }
    return settings != painted_stamp_ || seen_ != painted_files_;
}

void Texture::paint() {
    const Element& m = map();
    named();
    const std::string layer_path = m.params.get_or<std::string>("layer", "");
    auto& px = pixels();
    const int c = cell(), W = px_w(), wide = cols();
    const auto it = generators().find(m.params.get_or<std::string>("generator", "plain"));
    const Generator& gen = it != generators().end() ? it->second : generators()["plain"];
    const double tr = m.params.num("tint_r", 1.0), tg = m.params.num("tint_g", 1.0), tb = m.params.num("tint_b", 1.0);
    for (int cl = 0; cl < wide * rows(); ++cl) {
        const int x0 = (cl % wide) * c, y0 = (cl / wide) * c;
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
    if (!layer_path.empty() && reader() && reader()(layer_path, lw, lh, layer) && lw > 0 && lh > 0)
        lay_over(px, W, px_h(), c, layer, lw, lh, m.params.num("per_cell", 0.0) > 0.5, std::clamp(m.params.num("layer_mix", 1.0), 0.0, 1.0));
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
