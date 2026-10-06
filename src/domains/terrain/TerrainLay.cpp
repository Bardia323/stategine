// A land put in a world, looked at from above, made one model, kept as bytes.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

#include "TerrainKernel.hpp"
#include "sg/domains/Shapes.hpp"

namespace sg::terrain {

using namespace kernel;

namespace {

// What things are commonly made of, by the names the `trees` library and the
// modeller's own use: their colour, how rough, and the renderer's surface.
struct Look {
    Vec3d colour;
    double rough;
    int surface;
};
Look look_of(const std::string& material) {
    static const std::map<std::string, Look> looks{
        {"bark", {{0.24, 0.19, 0.14}, 0.9, 4}},    {"birch", {{0.80, 0.78, 0.72}, 0.8, 4}},  {"deadwood", {{0.40, 0.37, 0.33}, 0.95, 4}},
        {"leaf", {{0.20, 0.32, 0.12}, 0.8, 16}},   {"needles", {{0.10, 0.20, 0.11}, 0.85, 16}}, {"frond", {{0.24, 0.38, 0.14}, 0.75, 16}},
        {"grass", {{0.30, 0.40, 0.16}, 0.9, 16}},  {"reed", {{0.46, 0.45, 0.28}, 0.9, 16}},  {"stone", {{0.45, 0.44, 0.42}, 0.9, 22}},
        {"rock", {{0.42, 0.40, 0.38}, 0.9, 22}},   {"wood", {{0.36, 0.26, 0.17}, 0.7, 4}},   {"metal", {{0.35, 0.36, 0.38}, 0.4, 5}},
        {"brick", {{0.45, 0.24, 0.18}, 0.85, 13}}, {"plaster", {{0.75, 0.72, 0.66}, 0.9, 2}}, {"roof", {{0.30, 0.20, 0.17}, 0.8, 0}},
        {"glass", {{0.15, 0.2, 0.22}, 0.1, 0}}};
    const auto it = looks.find(material);
    return it == looks.end() ? Look{{0.55, 0.55, 0.55}, 0.8, 0} : it->second;
}

// Colours are written as they look (sRGB, as a picker gives them); the
// renderer lights in linear light.
Vec3d linear(const Vec3d& c) { return {std::pow(c.x, 2.2), std::pow(c.y, 2.2), std::pow(c.z, 2.2)}; }
void set_colour(Element& e, const Vec3d& c) {
    const Vec3d l = linear(c);
    e.params.set(keys::r, l.x).set(keys::g, l.y).set(keys::b, l.z);
}

}  // namespace

std::size_t lay(Spatial3D& world, const std::string& name, std::shared_ptr<const Land> land, const sculpt::Files* files) {
    if (!land) return 0;
    const Land& L = *land;
    // What was laid by this name before, gone.
    std::vector<Key> gone;
    for (const Element& e : world.elements())
        if (e.params.get_or<std::string>("laid_by", "") == name && e.id != Key{name}) gone.push_back(e.id);
    for (Key k : gone) world.remove_element(k);
    std::size_t made = 0;

    // The ground: the state's own terrain, drawn by its layers.
    const Key cover{name + ".cover"};
    world.picture(cover, L.nx + 1, L.nz + 1, L.splat);
    Element& g = world.terrain(Key{name}, [land](double x, double z) { return land->height(x, z); });
    g.params.set("laid_by", name).set("surface", 19.0).set("roughness", 0.9).set("splat", cover.str());
    // The cover's points are its pixels' middles.
    g.params.set("splat_x", L.x0 - L.cell * 0.5).set("splat_z", L.z0 - L.cell * 0.5).set("splat_w", (L.nx + 1) * L.cell).set("splat_d", (L.nz + 1) * L.cell);
    std::string surfaces;
    for (std::size_t k = 0; k < 4; ++k) {
        const Layer& l = L.layers[std::min(k, L.layers.size() - 1)];
        surfaces += (k ? "," : "") + std::to_string(l.surface);
        const Vec3d c = linear(l.colour);
        g.params.set("layer" + std::to_string(k), std::to_string(c.x) + "," + std::to_string(c.y) + "," + std::to_string(c.z));
    }
    g.params.set("layers", surfaces);
    // Drawn round the eye, fine near and coarse far, as far as the land goes.
    g.params.set("reach", std::clamp(std::hypot(L.w, L.d) * 0.75, 200.0, 4000.0)).set("cells", 240.0).set("step", std::min(0.5, L.cell * 0.5));
    ++made;

    const auto put = [&](const std::string& id, const std::vector<float>& corners, Vec3d lo, Vec3d hi) -> Element& {
        if (hi.y - lo.y < 0.02) lo.y -= 0.01, hi.y += 0.01;
        Vec3d size;
        world.model(Key{id + ".mesh"}, shapes::fit(corners, size, lo, hi));
        Element& e = world.fixture(Key{id}, (lo.x + hi.x) * 0.5, lo.y, (lo.z + hi.z) * 0.5);
        e.params.set(keys::sx, size.x).set(keys::sy, size.y).set(keys::sz, size.z);
        e.params.set("shape", std::string("model")).set("model", id + ".mesh").set("laid_by", name);
        ++made;
        return e;
    };
    for (std::size_t k = 0; k < L.roads.size(); ++k) {
        const Road& r = L.roads[k];
        if (r.corners.empty()) continue;
        Element& e = put(name + "." + r.name, r.corners, r.lo, r.hi);
        set_colour(e, r.colour);
        e.params.set("surface", double(r.surface)).set("roughness", 0.85).set("lines", r.lines ? 1.0 : 0.0);
    }
    for (std::size_t k = 0; k < L.waters.size(); ++k) {
        const Water& w = L.waters[k];
        if (w.corners.empty()) continue;
        const Key depth{name + "." + w.name + ".depth"};
        world.picture(depth, w.map_w, w.map_h, w.depth);
        Element& e = put(name + "." + w.name, w.corners, w.lo, w.hi);
        set_colour(e, w.shallow);
        const Vec3d deep = linear(w.deep);
        e.params.set("surface", 18.0).set("roughness", 0.08).set("mirror", 0.3).set("splat", depth.str()).set("deepest", w.deepest);
        e.params.set("deep_r", deep.x).set("deep_g", deep.y).set("deep_b", deep.z).set("flowing", w.flowing ? 1.0 : 0.0);
        // A lake ruffled, a pool still; the sea's swell is the sea's.
        e.params.set("calm", w.name.rfind("sea", 0) == 0 ? 0.0 : w.name.rfind("swamp", 0) == 0 ? 0.9 : 0.7);
    }

    // Things: each variant made once by the modeller, its parts models of
    // the state's; each placing one fixture a part.
    struct Made {
        std::shared_ptr<const sculpt::Model> model;
        std::vector<std::string> meshes;
        std::vector<Vec3d> sizes;
    };
    std::vector<std::vector<Made>> things(L.things.size());
    for (std::size_t t = 0; t < L.things.size(); ++t)
        for (std::size_t v = 0; v < L.things[t].recipes.size(); ++v) {
            Made m;
            m.model = sculpt::made(L.things[t].recipes[v], {}, files);
            for (std::size_t p = 0; p < m.model->parts.size(); ++p) {
                Vec3d size;
                const std::string mesh = name + "." + L.things[t].name + "." + std::to_string(v + 1) + "." + std::to_string(p) + ".mesh";
                world.model(Key{mesh}, shapes::fit(m.model->parts[p].corners, size, m.model->lo, m.model->hi));
                m.meshes.push_back(mesh), m.sizes.push_back(size);
            }
            things[t].push_back(std::move(m));
        }
    Dice tint(17);
    for (std::size_t k = 0; k < L.placed.size(); ++k) {
        const Placed& p = L.placed[k];
        const Made& m = things[std::size_t(p.thing)][std::size_t(p.variant)];
        // Stood by its recipe's origin: its foot turned and sized as it is.
        const Vec3d at = p.at + turn(Pose{{0, 0, 0}, p.yaw}, m.model->foot() * p.scale);
        const double shade = 0.85 + 0.3 * tint.next();
        for (std::size_t q = 0; q < m.meshes.size(); ++q) {
            const sculpt::Part& part = m.model->parts[q];
            Element& e = world.fixture(Key{name + "." + L.things[std::size_t(p.thing)].name + "." + std::to_string(k) + "." + std::to_string(q)}, at.x, at.y, at.z);
            e.params.set(keys::sx, m.sizes[q].x * p.scale).set(keys::sy, m.sizes[q].y * p.scale).set(keys::sz, m.sizes[q].z * p.scale);
            e.params.set(keys::yaw, p.yaw).set("shape", std::string("model")).set("model", m.meshes[q]).set("laid_by", name);
            const Look lk = look_of(part.material);
            set_colour(e, lk.colour * (part.material == "leaf" || part.material == "needles" || part.material == "grass" ? shade : 1.0));
            e.params.set("roughness", lk.rough).set("surface", double(lk.surface)).set("material", part.material);
            ++made;
        }
    }
    return made;
}

// --- looking at it -------------------------------------------------------------------

std::vector<unsigned char> map(const Land& L, int size, int& w, int& h) {
    const double longer = std::max(L.w, L.d);
    w = std::max(1, int(std::lround(size * L.w / longer))), h = std::max(1, int(std::lround(size * L.d / longer)));
    std::vector<unsigned char> rgb(std::size_t(w) * std::size_t(h) * 3);
    const Vec3d sun{-0.55, 0.62, -0.55};
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const double px = L.x0 + L.w * (x + 0.5) / w, pz = L.z0 + L.d * (y + 0.5) / h;
            const int i = std::clamp(int(std::lround((px - L.x0) / L.cell)), 0, L.nx), j = std::clamp(int(std::lround((pz - L.z0) / L.cell)), 0, L.nz);
            const std::size_t q = L.index(i, j);
            Vec3d c{0, 0, 0};
            for (std::size_t k = 0; k < 4 && k < L.layers.size(); ++k) c = c + L.layers[k].colour * (L.splat[q * 4 + k] / 255.0);
            const Vec3d n = L.normal(px, pz);
            const double lit = std::clamp(0.35 + 0.9 * std::max(0.0, dot(n, sun) / std::sqrt(dot(sun, sun))), 0.0, 1.4);
            c = c * lit;
            if (L.water[q] < 0) {
                double depth = 0;
                for (const Water& wa : L.waters)
                    if (px >= wa.lo.x && px <= wa.hi.x && pz >= wa.lo.z && pz <= wa.hi.z) depth = std::max(depth, (wa.hi.y - L.height(px, pz)) / wa.deepest);
                const double k = 1 - std::exp(-std::clamp(depth, 0.0, 1.0) * 3);
                c = Vec3d{0.32, 0.38, 0.36} * (1 - k) + Vec3d{0.05, 0.12, 0.16} * k;
            }
            if (L.road[q] < 0) c = c * 0.4 + Vec3d{0.12, 0.12, 0.12};
            unsigned char* o = &rgb[(std::size_t(y) * std::size_t(w) + std::size_t(x)) * 3];
            for (int k = 0; k < 3; ++k) o[k] = (unsigned char)std::lround(std::clamp((&c.x)[k], 0.0, 1.0) * 255);
        }
    // Things: a dark dot each.
    for (const Placed& p : L.placed) {
        const int x = int((p.at.x - L.x0) / L.w * w), y = int((p.at.z - L.z0) / L.d * h);
        const int r = std::max(1, int(std::lround(2.0 * p.scale * w / std::max(L.w, 1.0))));
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx) {
                if (dx * dx + dy * dy > r * r || x + dx < 0 || y + dy < 0 || x + dx >= w || y + dy >= h) continue;
                unsigned char* o = &rgb[(std::size_t(y + dy) * std::size_t(w) + std::size_t(x + dx)) * 3];
                o[0] = (unsigned char)(o[0] * 0.35), o[1] = (unsigned char)(o[1] * 0.45), o[2] = (unsigned char)(o[2] * 0.3);
            }
    }
    return rgb;
}

sculpt::Model model(const Land& L, double ground, bool with_things) {
    sculpt::Model m;
    m.lo = {1e30, 1e30, 1e30}, m.hi = {-1e30, -1e30, -1e30};
    const auto grow = [&](float x, float y, float z) {
        m.lo = {std::min(m.lo.x, double(x)), std::min(m.lo.y, double(y)), std::min(m.lo.z, double(z))};
        m.hi = {std::max(m.hi.x, double(x)), std::max(m.hi.y, double(y)), std::max(m.hi.z, double(z))};
    };
    // The ground, a part to each layer by which covers most of each face.
    const double step = ground > 0 ? ground : std::max(L.cell, std::max(L.w, L.d) / 300.0);
    const int nx = std::max(1, int(L.w / step)), nz = std::max(1, int(L.d / step));
    std::vector<sculpt::Part> layers(L.layers.size());
    for (std::size_t k = 0; k < layers.size(); ++k) layers[k].material = L.layers[k].name;
    for (int j = 0; j < nz; ++j)
        for (int i = 0; i < nx; ++i) {
            const double x0 = L.x0 + L.w * i / nx, x1 = L.x0 + L.w * (i + 1) / nx, z0 = L.z0 + L.d * j / nz, z1 = L.z0 + L.d * (j + 1) / nz;
            const double mx = (x0 + x1) / 2, mz = (z0 + z1) / 2;
            const int ci = std::clamp(int(std::lround((mx - L.x0) / L.cell)), 0, L.nx), cj = std::clamp(int(std::lround((mz - L.z0) / L.cell)), 0, L.nz);
            std::size_t best = 0;
            for (std::size_t k = 1; k < 4 && k < layers.size(); ++k)
                if (L.splat[L.index(ci, cj) * 4 + k] > L.splat[L.index(ci, cj) * 4 + best]) best = k;
            const double xs[4] = {x0, x0, x1, x1}, zs[4] = {z0, z1, z1, z0};
            for (int t : {0, 1, 2, 0, 2, 3}) {
                const Vec3d n = L.normal(xs[t], zs[t]);
                const float y = float(L.height(xs[t], zs[t]));
                layers[best].corners.insert(layers[best].corners.end(), {float(xs[t]), y, float(zs[t]), float(n.x), float(n.y), float(n.z), 0.0f, 0.0f});
                grow(float(xs[t]), y, float(zs[t]));
            }
        }
    for (auto& l : layers)
        if (!l.corners.empty()) m.parts.push_back(std::move(l));
    sculpt::Part roads, water;
    roads.material = "road", water.material = "water";
    for (const Road& r : L.roads) roads.corners.insert(roads.corners.end(), r.corners.begin(), r.corners.end());
    for (const Water& w : L.waters) water.corners.insert(water.corners.end(), w.corners.begin(), w.corners.end());
    if (!roads.corners.empty()) m.parts.push_back(std::move(roads));
    if (!water.corners.empty()) m.parts.push_back(std::move(water));
    if (with_things && !L.placed.empty()) {
        std::map<std::string, sculpt::Part> by;
        std::vector<std::vector<std::shared_ptr<const sculpt::Model>>> made(L.things.size());
        for (std::size_t t = 0; t < L.things.size(); ++t)
            for (const std::string& r : L.things[t].recipes) made[t].push_back(sculpt::made(r));
        for (const Placed& p : L.placed) {
            const auto& mm = made[std::size_t(p.thing)][std::size_t(p.variant)];
            const Pose turned{{0, 0, 0}, p.yaw};
            for (const sculpt::Part& part : mm->parts) {
                sculpt::Part& into = by[part.material];
                into.material = part.material;
                for (std::size_t k = 0; k + 8 <= part.corners.size(); k += 8) {
                    const Vec3d v = p.at + turn(turned, Vec3d{part.corners[k], part.corners[k + 1], part.corners[k + 2]} * p.scale);
                    const Vec3d n = turn(turned, Vec3d{part.corners[k + 3], part.corners[k + 4], part.corners[k + 5]});
                    into.corners.insert(into.corners.end(), {float(v.x), float(v.y), float(v.z), float(n.x), float(n.y), float(n.z), 0.0f, 0.0f});
                }
            }
        }
        for (auto& [k, part] : by) m.parts.push_back(std::move(part));
    }
    for (const auto& p : m.parts) m.triangles += p.corners.size() / 24;
    m.errors = L.errors;
    return m;
}

// --- kept as bytes ---------------------------------------------------------------------

namespace kernel {

namespace {
struct Out {
    std::string s;
    template <class T>
    void put(const T& v) { s.append(reinterpret_cast<const char*>(&v), sizeof v); }
    void text(const std::string& t) { put(uint64_t(t.size())), s += t; }
    template <class T>
    void vec(const std::vector<T>& v) { put(uint64_t(v.size())), s.append(reinterpret_cast<const char*>(v.data()), v.size() * sizeof(T)); }
    void v3(const Vec3d& v) { put(v.x), put(v.y), put(v.z); }
};
struct In {
    const std::string& s;
    std::size_t at = 0;
    bool ok = true;
    template <class T>
    T get() {
        T v{};
        if (at + sizeof v > s.size()) return ok = false, v;
        std::memcpy(&v, s.data() + at, sizeof v), at += sizeof v;
        return v;
    }
    std::string text() {
        const uint64_t n = get<uint64_t>();
        if (!ok || at + n > s.size()) return ok = false, std::string();
        std::string t = s.substr(at, n);
        at += n;
        return t;
    }
    template <class T>
    std::vector<T> vec() {
        const uint64_t n = get<uint64_t>();
        if (!ok || at + n * sizeof(T) > s.size()) return ok = false, std::vector<T>();
        std::vector<T> v(n);
        std::memcpy(v.data(), s.data() + at, n * sizeof(T)), at += n * sizeof(T);
        return v;
    }
    Vec3d v3() {
        const double x = get<double>(), y = get<double>(), z = get<double>();
        return {x, y, z};
    }
};
}  // namespace

std::string to_bytes(const Land& L) {
    Out o;
    o.put(uint32_t(0x4C414E44)), o.put(uint32_t(1));
    o.put(L.x0), o.put(L.z0), o.put(L.w), o.put(L.d), o.put(L.cell), o.put(L.nx), o.put(L.nz);
    o.vec(L.h), o.vec(L.wet), o.vec(L.road), o.vec(L.water);
    o.put(L.beyond), o.put(L.over), o.put(uint8_t(L.to_beyond));
    o.put(uint64_t(L.layers.size()));
    for (const Layer& l : L.layers) o.text(l.name), o.put(l.surface), o.v3(l.colour);
    o.vec(L.splat);
    o.put(uint64_t(L.roads.size()));
    for (const Road& r : L.roads) {
        o.text(r.name), o.put(r.width), o.put(uint64_t(r.centre.size()));
        for (const Vec3d& c : r.centre) o.v3(c);
        o.vec(r.corners), o.v3(r.lo), o.v3(r.hi), o.put(r.surface), o.v3(r.colour), o.put(uint8_t(r.lines));
    }
    o.put(uint64_t(L.waters.size()));
    for (const Water& w : L.waters) {
        o.text(w.name), o.vec(w.corners), o.v3(w.lo), o.v3(w.hi), o.v3(w.shallow), o.v3(w.deep), o.put(w.deepest);
        o.put(w.map_w), o.put(w.map_h), o.vec(w.depth), o.put(uint8_t(w.flowing));
    }
    o.put(uint64_t(L.things.size()));
    for (const Thing& t : L.things) {
        o.text(t.name), o.put(uint64_t(t.recipes.size()));
        for (const std::string& r : t.recipes) o.text(r);
    }
    o.put(uint64_t(L.placed.size()));
    for (const Placed& p : L.placed) o.put(p.thing), o.put(p.variant), o.v3(p.at), o.put(p.yaw), o.put(p.scale);
    o.text(L.errors);
    return o.s;
}

bool from_bytes(const std::string& bytes, Land& L) {
    In in{bytes};
    if (in.get<uint32_t>() != 0x4C414E44 || in.get<uint32_t>() != 1) return false;
    L.x0 = in.get<double>(), L.z0 = in.get<double>(), L.w = in.get<double>(), L.d = in.get<double>(), L.cell = in.get<double>();
    L.nx = in.get<int>(), L.nz = in.get<int>();
    L.h = in.vec<float>(), L.wet = in.vec<float>(), L.road = in.vec<float>(), L.water = in.vec<float>();
    L.beyond = in.get<double>(), L.over = in.get<double>(), L.to_beyond = in.get<uint8_t>() != 0;
    const uint64_t nl = in.get<uint64_t>();
    for (uint64_t k = 0; k < nl && in.ok; ++k) {
        Layer l;
        l.name = in.text(), l.surface = in.get<int>(), l.colour = in.v3();
        L.layers.push_back(l);
    }
    L.splat = in.vec<unsigned char>();
    const uint64_t nr = in.get<uint64_t>();
    for (uint64_t k = 0; k < nr && in.ok; ++k) {
        Road r;
        r.name = in.text(), r.width = in.get<double>();
        const uint64_t nc = in.get<uint64_t>();
        for (uint64_t c = 0; c < nc && in.ok; ++c) r.centre.push_back(in.v3());
        r.corners = in.vec<float>(), r.lo = in.v3(), r.hi = in.v3(), r.surface = in.get<int>(), r.colour = in.v3(), r.lines = in.get<uint8_t>() != 0;
        L.roads.push_back(std::move(r));
    }
    const uint64_t nw = in.get<uint64_t>();
    for (uint64_t k = 0; k < nw && in.ok; ++k) {
        Water w;
        w.name = in.text(), w.corners = in.vec<float>(), w.lo = in.v3(), w.hi = in.v3(), w.shallow = in.v3(), w.deep = in.v3(), w.deepest = in.get<double>();
        w.map_w = in.get<int>(), w.map_h = in.get<int>(), w.depth = in.vec<unsigned char>(), w.flowing = in.get<uint8_t>() != 0;
        L.waters.push_back(std::move(w));
    }
    const uint64_t nt = in.get<uint64_t>();
    for (uint64_t k = 0; k < nt && in.ok; ++k) {
        Thing t;
        t.name = in.text();
        const uint64_t n = in.get<uint64_t>();
        for (uint64_t r = 0; r < n && in.ok; ++r) t.recipes.push_back(in.text());
        L.things.push_back(std::move(t));
    }
    const uint64_t np = in.get<uint64_t>();
    for (uint64_t k = 0; k < np && in.ok; ++k) {
        Placed p;
        p.thing = in.get<int>(), p.variant = in.get<int>(), p.at = in.v3(), p.yaw = in.get<double>(), p.scale = in.get<double>();
        L.placed.push_back(p);
    }
    L.errors = in.text();
    return in.ok && in.at == bytes.size();
}

}  // namespace kernel

}  // namespace sg::terrain
