#include "sg/domains/Light.hpp"

namespace sg {

Rgb mix(const Rgb& a, const Rgb& b, double t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
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

void spill(Element& lamp, const Surface2D& screen, double most, double ease) {
    const Rgb c = average_colour(screen);
    const double lum = 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b;
    const double m = std::max({c.r, c.g, c.b, 1e-4});
    const auto toward = [&](Key key, double to) {
        lamp.params.set(key, lamp.params.num(key) + (to - lamp.params.num(key)) * ease);
    };
    toward(keys::r, 0.35 + 0.65 * c.r / m);
    toward(keys::g, 0.35 + 0.65 * c.g / m);
    toward(keys::b, 0.35 + 0.65 * c.b / m);
    toward(keys::intensity, most * std::min(1.0, 0.12 + 1.5 * lum));
}

}  // namespace sg
