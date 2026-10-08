#include "sg/domains/Probe.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

#include "sg/core/Cache.hpp"

namespace sg {

std::array<double, 9> Sh9::basis(const Vec3d& d) {
    const double x = d.x, y = d.y, z = d.z;
    return {0.282095,
            0.488603 * y, 0.488603 * z, 0.488603 * x,
            1.092548 * x * y, 1.092548 * y * z, 0.315392 * (3.0 * z * z - 1.0), 1.092548 * x * z, 0.546274 * (x * x - y * y)};
}

void Sh9::add(const Vec3d& d, const Rgb& radiance, double w) {
    const auto y = basis(d);
    for (std::size_t k = 0; k < 9; ++k) {
        c[k][0] += radiance.r * y[k] * w;
        c[k][1] += radiance.g * y[k] * w;
        c[k][2] += radiance.b * y[k] * w;
    }
}

void Sh9::add(const Sh9& o, const Rgb& s) {
    for (std::size_t k = 0; k < 9; ++k) {
        c[k][0] += o.c[k][0] * s.r;
        c[k][1] += o.c[k][1] * s.g;
        c[k][2] += o.c[k][2] * s.b;
    }
}

namespace {

// Each band's share as it reaches a surface (Ramamoorthi and Hanrahan's
// cosine lobe, pi, 2pi/3, pi/4), over pi: 1, 2/3, 1/4.
Rgb evaluate(const Sh9& sh, const Vec3d& d, double w1, double w2) {
    const auto y = Sh9::basis(unit(d));
    double out[3] = {0, 0, 0};
    for (std::size_t k = 0; k < 9; ++k) {
        const double w = k == 0 ? 1.0 : k < 4 ? w1 : w2;
        for (int i = 0; i < 3; ++i) out[i] += sh.c[k][static_cast<std::size_t>(i)] * y[k] * w;
    }
    return {std::max(out[0], 0.0), std::max(out[1], 0.0), std::max(out[2], 0.0)};
}

}  // namespace

Rgb Sh9::irradiance(const Vec3d& n) const { return evaluate(*this, n, 2.0 / 3.0, 0.25); }

Rgb Sh9::radiance(const Vec3d& d, double rough) const {
    const double r = std::clamp(rough, 0.0, 1.0);
    return evaluate(*this, d, 1.0 + (2.0 / 3.0 - 1.0) * r, 1.0 + (0.25 - 1.0) * r);
}

bool Sh9::finite() const {
    for (const auto& k : c)
        for (double v : k)
            if (!std::isfinite(v)) return false;
    return true;
}

std::string Sh9::text() const {
    std::string out;
    char buf[32];
    for (const auto& k : c)
        for (double v : k) {
            std::snprintf(buf, sizeof buf, "%.7g", v);
            if (!out.empty()) out += ' ';
            out += buf;
        }
    return out;
}

bool Sh9::parse(const std::string& s, Sh9& out) {
    const char* p = s.c_str();
    for (auto& k : out.c)
        for (double& v : k) {
            char* end = nullptr;
            v = std::strtod(p, &end);
            if (end == p) return false;
            p = end;
        }
    while (*p == ' ') ++p;
    return *p == '\0';
}

Element& add_probe(Spatial3D& world, Key id, Vec3d base, Vec3d size, double soft) {
    Element& e = world.add_element(id, kinds::probe);
    set_position(e, base);
    e.params.set(keys::sx, size.x).set(keys::sy, size.y).set(keys::sz, size.z).set(Key{"soft"}, soft);
    return e;
}

std::vector<const Element*> probes_of(const State& world) {
    std::vector<const Element*> out;
    for (const Element& e : world.elements())
        if (e.alive && e.kind == kinds::probe) out.push_back(&e);
    return out;
}

std::map<std::string, Sh9> probe_sets(const Element& probe, bool* readable) {
    std::map<std::string, Sh9> out;
    if (readable) *readable = true;
    for (const auto& [k, v] : probe.params) {
        const std::string& name = k.str();
        if (name.rfind("sh.", 0) != 0) continue;
        Sh9 sh;
        const std::string* t = std::get_if<std::string>(&v);
        if (!t || !Sh9::parse(*t, sh)) {
            if (readable) *readable = false;
            continue;
        }
        out[name.substr(3)] = sh;
    }
    return out;
}

Sh9 probe_light(const State& world, const Element& probe) { return probe_light(world, probe_sets(probe)); }

Sh9 probe_light(const State& world, const std::map<std::string, Sh9>& sets) {
    Sh9 out;
    for (const auto& [light, set] : sets) {
        const Element* l = world.find(Key{light});
        if (!l || !l->alive || l->kind != kinds::light) continue;
        // (As the renderer reads a lamp: white-ish unless it says, at 1.)
        const double i = l->params.num(keys::intensity, 1.0);
        out.add(set, Rgb{i * l->params.num(keys::r, 1.0), i * l->params.num(keys::g, 0.93), i * l->params.num(keys::b, 0.82)});
    }
    return out;
}

std::string probe_digest(const State& world, const Element& probe) {
    Hasher h;
    h.text("probe");
    for (const char* k : {"room_w", "room_d", "room_h", "sky"}) h.number(world.params().num(Key{k}, 0.0));
    const auto box = [&](const Element& e) {
        for (Key k : {keys::x, keys::y, keys::z, keys::sx, keys::sy, keys::sz, keys::yaw, keys::pitch, keys::roll, keys::w, keys::h})
            h.number(e.params.num(k, 0.0));
    };
    for (const Element& e : world.elements()) {
        if (!e.alive) continue;
        if (e.kind == kinds::wall) {
            h.text(e.id.str()).text(e.kind.str());
            box(e);
        } else if (e.kind == kinds::light && e.params.num(Key{"sun"}) < 0.5 && e.params.num(Key{"indirect"}) < 0.5) {
            // (A sun goes round with the day, and one standing in for bounce
            // is not baked: neither stales a probe.)
            h.text(e.id.str());
            box(e);
            for (const char* k : {"dx", "dy", "dz", "inner", "outer", "falloff"}) h.number(e.params.num(Key{k}, 0.0));
        }
    }
    box(probe);
    return h.digest().hex();
}

void bake_into(Spatial3D& world, const std::vector<ProbeBake>& bakes) {
    for (const ProbeBake& b : bakes) {
        Element* e = world.find(b.probe);
        if (!e || e->kind != kinds::probe) continue;
        std::vector<Key> old;
        for (const auto& kv : e->params)
            if (kv.first.str().rfind("sh.", 0) == 0) old.push_back(kv.first);
        for (Key k : old) e->params.erase(k);
        for (const auto& [light, sh] : b.sets) e->params.set(Key{"sh." + light}, sh.text());
        e->params.set(Key{"digest"}, b.digest);
    }
}

std::vector<std::string> probe_faults(const State& world) {
    std::vector<std::string> out;
    const Params& p = world.params();
    const bool boxed = p.has(Key{"room_w"}) && p.has(Key{"room_d"}) && p.num(Key{"sky"}, 0.0) < 0.5;
    const double w = p.num(Key{"room_w"}), d = p.num(Key{"room_d"}), h = p.num(Key{"room_h"}, 3.0);
    for (const Element* e : probes_of(world)) {
        const std::string name = world.id().str() + "." + e->id.str();
        if (boxed) {
            // Its corners, turned as it is, within the room (a hand's slack).
            const Pose at = world_pose(world, *e);
            const double hx = 0.5 * e->params.num(keys::sx), hz = 0.5 * e->params.num(keys::sz), sy = e->params.num(keys::sy);
            const double c = std::cos(at.yaw), s = std::sin(at.yaw), slack = 0.05;
            bool inside = at.position.y >= -slack && at.position.y + sy <= h + slack;
            for (int i = 0; i < 4 && inside; ++i) {
                const double lx = (i & 1 ? hx : -hx), lz = (i & 2 ? hz : -hz);
                const double x = at.position.x + c * lx - s * lz, z = at.position.z + s * lx + c * lz;
                inside = x >= -slack && x <= w + slack && z >= -slack && z <= d + slack;
            }
            if (!inside) out.push_back("probe " + name + ": its box goes out of its room");
        }
        bool readable = true;
        const auto sets = probe_sets(*e, &readable);
        if (!readable) out.push_back("probe " + name + ": a set of it is not 27 numbers");
        for (const auto& [light, sh] : sets) {
            if (!sh.finite()) out.push_back("probe " + name + ": its set for " + light + " is not finite");
            const Element* l = world.find(Key{light});
            if (!l || !l->alive || l->kind != kinds::light) out.push_back("probe " + name + ": it holds a set for " + light + ", which is no lamp here");
        }
        const std::string* baked = e->params.text(Key{"digest"});
        if (baked && !baked->empty() && *baked != probe_digest(world, *e))
            out.push_back("probe " + name + ": stale - the room or its lamps have changed since it was baked (bake it again)");
    }
    return out;
}

}  // namespace sg
