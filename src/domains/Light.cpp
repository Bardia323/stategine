#include <algorithm>
#include <cstdio>
#include "sg/domains/Light.hpp"

namespace sg {

Rgb mix(const Rgb& a, const Rgb& b, double t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

Rgb kelvin(double K) {
    const double t = std::clamp(std::isfinite(K) ? K : 6500.0, 1667.0, 25000.0);
    const double t1 = 1e3 / t, t2 = t1 * t1, t3 = t2 * t1;  // thousands of kelvin, inverted
    // Where the locus is (Kim, Kang, Kim, Lee and Hwang 2002), x then y.
    const double x = t <= 4000.0 ? -0.2661239 * t3 - 0.2343589 * t2 + 0.8776956 * t1 + 0.179910
                                 : -3.0258469 * t3 + 2.1070379 * t2 + 0.2226347 * t1 + 0.240390;
    const double x2 = x * x, x3 = x2 * x;
    const double y = t <= 2222.0   ? -1.1063814 * x3 - 1.34811020 * x2 + 2.18555832 * x - 0.20219683
                     : t <= 4000.0 ? -0.9549476 * x3 - 1.37418593 * x2 + 2.09137015 * x - 0.16748867
                                   : 3.0817580 * x3 - 5.87338670 * x2 + 3.75112997 * x - 0.37001483;
    // As bright as white (Y = 1), into linear sRGB (D65).
    const double X = x / y, Y = 1.0, Z = (1.0 - x - y) / y;
    Rgb c{3.2404542 * X - 1.5371385 * Y - 0.4985314 * Z, -0.9692660 * X + 1.8760108 * Y + 0.0415560 * Z,
          0.0556434 * X - 0.2040259 * Y + 1.0572252 * Z};
    // A candle is redder than sRGB has a red for: no channel below none,
    // and as bright as white again after.
    c.r = std::max(c.r, 0.0), c.g = std::max(c.g, 0.0), c.b = std::max(c.b, 0.0);
    const double lum = 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b;
    return {c.r / lum, c.g / lum, c.b / lum};
}


Daylight daylight(double hour, const Rgb& ground) {
    constexpr double pi = 3.14159265358979323846;
    const double h = std::fmod(std::fmod(hour, 24.0) + 24.0, 24.0);
    const double e = std::sin((h - 6.0) / 24.0 * 2.0 * pi) * 1.05;  // the sun's height, radians
    const double az = (h - 6.0) / 12.0 * pi;                         // east at dawn, west at dusk
    const double day = smoothstep(-0.10, 0.30, e);
    const double gold = std::exp(-std::pow((e - 0.04) / 0.13, 2.0));
    const double night = 1.0 - smoothstep(-0.30, -0.03, e);
    Daylight s;
    s.elevation = e;
    s.top = mix({0.003, 0.005, 0.018}, {0.16, 0.34, 0.70}, day);
    s.top = mix(s.top, {0.24, 0.26, 0.46}, gold * 0.6);
    s.horizon = mix({0.018, 0.026, 0.05}, {0.80, 0.75, 0.64}, day);
    s.horizon = mix(s.horizon, {1.0, 0.50, 0.22}, gold * 0.75);
    s.sky_ambient = mix({0.10, 0.13, 0.26}, {0.40, 0.50, 0.70}, day);
    s.ground_ambient = mix({0.05, 0.05, 0.07}, ground, day);
    s.ground_ambient = mix(s.ground_ambient, {ground.r * 1.1, ground.g * 0.8, ground.b * 0.6}, gold * 0.5);
    s.ambient = 0.05 + 0.37 * day + 0.06 * gold;
    s.exposure = 1.45 - 0.5 * day;
    s.stars = night;
    s.day = day;
    const double sun_strength = 0.085 * smoothstep(-0.03, 0.12, e);
    const double moon_strength = 0.035 * smoothstep(-0.02, 0.15, -e);
    const double ce = std::cos(e);
    if (sun_strength >= moon_strength) {
        s.sun = {ce * std::cos(az), std::sin(e), ce * std::sin(az)};
        s.light = mix({1.0, 0.48, 0.2}, {1.0, 0.93, 0.82}, smoothstep(0.02, 0.45, e));
        s.intensity = sun_strength;
    } else {
        // The moon rides opposite the sun, dimmer and blue.
        s.sun = {-ce * std::cos(az), -std::sin(e), -ce * std::sin(az)};
        s.light = {0.2, 0.24, 0.33};
        s.intensity = moon_strength;
    }
    return s;
}

void show_daylight(LookState& l, const Daylight& d) {
    l.uniform(passes::scene, "uSkyTop", d.top.r, d.top.g, d.top.b)
        .uniform(passes::scene, "uSkyHorizon", d.horizon.r, d.horizon.g, d.horizon.b)
        .uniform(passes::scene, "uFogColor", d.horizon.r * 0.95, d.horizon.g * 0.95, d.horizon.b * 0.95)
        .uniform(passes::scene, "uSky", d.sky_ambient.r, d.sky_ambient.g, d.sky_ambient.b)
        .uniform(passes::scene, "uGround", d.ground_ambient.r, d.ground_ambient.g, d.ground_ambient.b)
        .uniform(passes::scene, "uAmbient", d.ambient)
        .uniform(passes::scene, "uStars", d.stars)
        .uniform(passes::composite, "uExposure", d.exposure);
}

void aim_rays(LookState& look, const Element& cam, const Vec3d& dir, double strength, const Rgb& colour) {
    const Vec3d f = forward_of(cam);
    const double half = cam.params.num(keys::fov, 70.0) * 3.14159265358979323846 / 360.0;
    look.uniform(passes::composite, "uRayDir", dir.x, dir.y, dir.z)
        .uniform(passes::composite, "uCamFwd", f.x, f.y, f.z)
        .uniform(passes::composite, "uTanHalf", std::tan(half))
        .uniform(passes::composite, "uRays", std::max(0.0, strength))
        .uniform(passes::composite, "uRayColor", colour.r, colour.g, colour.b);
}

Rgb average_colour(const Surface2D& s) {
    const int w = s.px_w(), h = s.px_h();
    Rgb sum{0, 0, 0};
    int n = 0;
    for (int y = h / 20; y < h; y += std::max(1, h / 12))
        for (int x = w / 24; x < w; x += std::max(1, w / 16)) {
            const unsigned char* q = s.pixel(x, y);
            sum.r += std::pow(q[0] / 255.0, 2.2), sum.g += std::pow(q[1] / 255.0, 2.2),
                sum.b += std::pow(q[2] / 255.0, 2.2);
            ++n;
        }
    if (n) sum.r /= n, sum.g /= n, sum.b /= n;
    return sum;
}

Spill spill_of(const Surface2D& screen, double most) {
    const Rgb c = average_colour(screen);
    const double lum = 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b;
    const double m = std::max({c.r, c.g, c.b, 1e-4});
    const auto step = [](double v) { return std::round(v * 512.0) / 512.0; };
    Spill s;
    s.r = step(0.35 + 0.65 * c.r / m);
    s.g = step(0.35 + 0.65 * c.g / m);
    s.b = step(0.35 + 0.65 * c.b / m);
    s.level = step(most * std::min(1.0, 0.12 + 1.5 * lum));
    return s;
}

void spill(Element& lamp, const Spill& to, double dt, bool on, double tau) {
    if (!(dt > 0.0)) return;  // no time: the identity
    const double k = 1.0 - std::exp(-dt / std::max(tau, 1e-9));
    const auto toward = [&](Key key, double goal) {
        const double v = lamp.params.num(key);
        lamp.params.set(key, std::fabs(goal - v) < 1.0 / 1024.0 ? goal : v + (goal - v) * k);
    };
    toward(keys::r, to.r);
    toward(keys::g, to.g);
    toward(keys::b, to.b);
    if (on)
        toward(keys::intensity, to.level);
    else
        lamp.params.set(keys::intensity, 0.0);
}

namespace {
// Whether `p` is shut in by `e`: inside its box, and - where it is a model -
// its own faces round it most ways out.
bool shut_in(const Spatial3D& room, const Element& e, const Vec3d& p) {
    const double hx = 0.5 * e.params.num(keys::sx, 0.0), hz = 0.5 * e.params.num(keys::sz, 0.0), sy = e.params.num(keys::sy, 0.0);
    if (hx <= 0.0 || hz <= 0.0 || sy <= 0.0) return false;
    const Pose at = world_pose(room, e);
    const Vec3d l = local_of(at, p);
    if (std::fabs(l.x) >= hx || std::fabs(l.z) >= hz || l.y <= 0.0 || l.y >= sy) return false;
    const std::string shape = e.params.get_or<std::string>("shape", "");
    if (shape != "model") return true;
    const std::vector<float>* made = room.model(Key{e.params.get_or<std::string>("model", "")});
    if (!made || made->size() < 24) return false;
    // In the model's unit box: rays out every way, against its triangles.
    const Vec3d o{l.x / (2.0 * hx), l.y / sy - 0.5, l.z / (2.0 * hz)};
    static const double r = 0.57735026919;
    const Vec3d ways[14] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {r, r, r}, {r, r, -r}, {r, -r, r}, {r, -r, -r},
                            {-r, r, r}, {-r, r, -r}, {-r, -r, r}, {-r, -r, -r}};
    int met = 0;
    for (const Vec3d& d : ways) {
        bool hit = false;
        for (std::size_t i = 0; i + 24 <= made->size() && !hit; i += 24) {
            const Vec3d a{(*made)[i], (*made)[i + 1], (*made)[i + 2]}, b{(*made)[i + 8], (*made)[i + 9], (*made)[i + 10]},
                c{(*made)[i + 16], (*made)[i + 17], (*made)[i + 18]};
            const Vec3d e1 = b - a, e2 = c - a, q = cross(d, e2);
            const double det = dot(e1, q);
            if (std::fabs(det) < 1e-12) continue;
            const Vec3d s = o - a;
            const double u = dot(s, q) / det;
            if (u < 0.0 || u > 1.0) continue;
            const Vec3d t = cross(s, e1);
            const double v = dot(d, t) / det;
            if (v < 0.0 || u + v > 1.0) continue;
            hit = dot(e2, t) / det > 1e-6;
        }
        met += hit ? 1 : 0;
    }
    return met >= 12;
}
}  // namespace

std::vector<std::string> lamp_faults(const Spatial3D& room) {
    std::vector<std::string> out;
    const Params& rp = room.params();
    int lamps = 0, real = 0;
    bool sun = false;
    for (const Element& l : room.elements()) {
        if (!l.alive || l.kind != kinds::light) continue;
        if (l.params.num(Key{"sun"}, 0.0) > 0.5) {
            sun = true;
            continue;
        }
        const bool indirect = l.params.num(Key{"indirect"}, 0.0) >= 0.5;
        ++lamps;
        real += !indirect || l.params.num(Key{"bounce"}, 1.0) < 0.5 ? 1 : 0;
        if (indirect || l.params.num(keys::intensity, 1.0) <= 0.0) continue;
        // What holds it: shadowed by it, a lamp lights nothing past it.
        const Vec3d at = world_position(room, l);
        for (const Element& e : room.elements()) {
            if (!e.alive || (e.kind != kinds::mesh && e.kind != kinds::wall) || e.params.num(Key{"cast"}, 1.0) < 0.5) continue;
            if (!shut_in(room, e, at)) continue;
            out.push_back("lamp " + l.id.str() + " stands inside " + e.id.str() + ", which casts: shadowed by what holds it, it lights nothing past it - say `cast` 0 on " +
                          e.id.str() + " (a shade, a globe, a fixture's glass: what its light comes through)");
            break;
        }
    }
    // A lamp in a fixture that has a bulb (a part of material `bulb`, or `lamps...`) is at
    // that bulb: its light comes from there, every way its shade lets it -
    // not from a point in front of it, which leaves dark what is behind the
    // bulb (the wall a sconce hangs on) and lights what the shade would hide.
    for (const Element& l : room.elements()) {
        if (!l.alive || l.kind != kinds::light || l.params.num(Key{"sun"}, 0.0) > 0.5 || l.params.num(Key{"indirect"}, 0.0) >= 0.5) continue;
        const Vec3d at = world_position(room, l);
        for (const Element& b : room.elements()) {
            // (A bulb: a part said to be one - `bulb`, or a fitting's `lamps`.)
            const std::string m = b.params.get_or<std::string>("material", "");
            if (!b.alive || b.kind != kinds::mesh || (m.find("bulb") == std::string::npos && m.rfind("lamps", 0) != 0)) continue;
            const Pose bp = world_pose(room, b);
            const double hx = 0.5 * b.params.num(keys::sx, 0.0), hz = 0.5 * b.params.num(keys::sz, 0.0), sy = b.params.num(keys::sy, 0.0);
            // (Only the fixture it is in: within the box of what holds the bulb.)
            const Vec3d in = local_of(bp, at);
            if (std::fabs(in.x) > hx + 0.05 || std::fabs(in.z) > hz + 0.05 || in.y < -0.05 || in.y > sy + 0.05) continue;
            const std::vector<float>* made = room.model(Key{b.params.get_or<std::string>("model", "")});
            if (!made || made->size() < 8) continue;
            Vec3d lo{1e30, 1e30, 1e30}, hi{-1e30, -1e30, -1e30};
            for (std::size_t i = 0; i + 8 <= made->size(); i += 8) {
                const Vec3d c{(*made)[i] * hx * 2.0, ((*made)[i + 1] + 0.5) * sy, (*made)[i + 2] * hz * 2.0};
                lo = {std::min(lo.x, c.x), std::min(lo.y, c.y), std::min(lo.z, c.z)}, hi = {std::max(hi.x, c.x), std::max(hi.y, c.y), std::max(hi.z, c.z)};
            }
            const double off = std::hypot(std::max({lo.x - in.x, 0.0, in.x - hi.x}), std::max({lo.y - in.y, 0.0, in.y - hi.y}), std::max({lo.z - in.z, 0.0, in.z - hi.z}));
            if (off > 0.03) {
                char b2[64];
                std::snprintf(b2, sizeof b2, "%.0f cm", off * 100.0);
                out.push_back("lamp " + l.id.str() + " is " + b2 + " from the bulb of the fixture it is in (" + b.id.str() +
                              "): its light comes from a point beside the bulb - stand it at the bulb (a lamp lit every way, its shade said `cast` 0)");
            }
            break;
        }
    }
    const bool closed = rp.num(Key{"sky"}, 0.0) < 0.5 && rp.num(Key{"gi"}, 1.0) > 0.5;
    if (lamps > 0 && real == 0 && !sun && closed)
        out.push_back("lit by nothing: every lamp of it stands in for bounce (`indirect`), and where its light from all round holds such lamps give way to it - "
                      "a bulb is a real lamp (a spot, or a lamp without `indirect`), or say `bounce` 0 on a fill that lights");
    return out;
}

}  // namespace sg
