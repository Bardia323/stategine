// sgland: a terrain recipe drawn as a game would draw it - laid in an open
// world (sg::terrain::lay), under the sky at an hour, lit by the sun with its
// shadows, in air as thick as asked - by the engine's own renderer, to a PNG.
// For whoever writes a land to see it as it will be seen; sgterrain draws the
// map, this the place.
//
//   sgland land.terrain                         a view over it, land.view.png
//   sgland -p lake -o lake.png --hour 17        a preset, late in the day
//   sgland land.terrain --eye 0,0,-150,90,-4,70 --above 1.7
//                                               x,y,z,yaw,pitch,fov (degrees;
//                                               yaw 0 looks along +x, 90 along
//                                               +z); --above puts the eye that
//                                               high over the ground at x,z
//   sgland land.terrain --fog 0.02 --sky 0.6,0.6,0.6   thick grey air
//   sgland land.terrain -w 1920 -h 1080
//
// `import` and the things' recipes read files beside the recipe.
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "sg/domains/Light.hpp"
#include "sg/domains/Terrain.hpp"
#include "sg/gl/Window.hpp"
#include "sg/gl/World.hpp"
#include "sg/sg.hpp"

namespace fs = std::filesystem;

namespace {
std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
}  // namespace

int main(int argc, char** argv) {
    std::string recipe, out, name = "land";
    fs::path dir = ".";
    int W = 1280, H = 720, frames = 6;
    double hour = 15.5, fog = 0.0, above = -1;
    bool eye_set = false, sky_set = false;
    double ev[6] = {0, 0, 0, 0, -8, 70}, sky[3] = {0.6, 0.6, 0.6};
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "-o") out = next();
        else if (a == "-w") W = std::max(64, std::atoi(next().c_str()));
        else if (a == "-h") H = std::max(64, std::atoi(next().c_str()));
        else if (a == "--frames") frames = std::max(1, std::atoi(next().c_str()));
        else if (a == "--hour") hour = std::atof(next().c_str());
        else if (a == "--fog") fog = std::atof(next().c_str());
        else if (a == "--above") above = std::atof(next().c_str());
        else if (a == "--sky") std::sscanf(next().c_str(), "%lf,%lf,%lf", &sky[0], &sky[1], &sky[2]), sky_set = true;
        else if (a == "--eye") std::sscanf(next().c_str(), "%lf,%lf,%lf,%lf,%lf,%lf", &ev[0], &ev[1], &ev[2], &ev[3], &ev[4], &ev[5]), eye_set = true;
        else if (a == "-p") {
            name = next();
            recipe = sg::terrain::preset(name);
            for (char& c : name)
                if (c == ' ') c = '_';
        } else if (a == "-e") {
            recipe = next();
            for (std::size_t k; (k = recipe.find(" ; ")) != std::string::npos;) recipe.replace(k, 3, "\n");
        } else {
            recipe = slurp(a);
            name = fs::path(a).replace_extension().string();
            dir = fs::path(a).parent_path();
        }
    }
    if (recipe.empty()) {
        std::fprintf(stderr, "sgland <recipe> | -p <preset> | -e \"line ; line\" [-o out.png] [-w px -h px] [--eye x,y,z,yaw,pitch,fov] [--above m] "
                             "[--hour h] [--fog density] [--sky r,g,b]\n");
        return 1;
    }
    sg::gl::Window window(W, H, "sgland");
    sg::StateGraph g;
    auto& world = g.add<sg::Spatial3D>("land");
    g.set_initial("land");
    sg::sculpt::Files files;
    files.read = [&](const std::string& p, std::string& text) {
        const fs::path f = fs::path(p).is_absolute() ? fs::path(p) : dir / p;
        if (!fs::exists(f)) return false;
        text = slurp(f);
        return true;
    };
    files.stamp = [&](const std::string& p) {
        std::error_code ec;
        const auto t = fs::last_write_time(fs::path(p).is_absolute() ? fs::path(p) : dir / p, ec);
        return ec ? 0LL : static_cast<long long>(t.time_since_epoch().count());
    };
    const auto land = sg::terrain::build(recipe);
    if (!land->errors.empty()) std::printf("errors:\n%s", land->errors.c_str());
    const std::size_t made = sg::terrain::lay(world, "land", land, &files);
    std::printf("%.0f x %.0f m laid: %zu things in the world\n", land->w, land->d, made);
    world.params().set("sky", 1.0).set("far", std::clamp(std::hypot(land->w, land->d), 300.0, 4000.0));

    // The sky at the hour, its sun, and the air.
    const sg::Daylight d = sg::daylight(hour, {0.3, 0.32, 0.22});
    sg::Element& sun = world.light("sun", {0, 400, 0}, d.light.r, d.light.g, d.light.b);
    sun.params.set("sun", 1.0).set(sg::keys::intensity, d.intensity).set("extent", 60.0);
    sun.params.set("dx", -d.sun.x).set("dy", -d.sun.y).set("dz", -d.sun.z);
    auto& look = g.add<sg::LookState>("land.look");
    sg::show_daylight(look, d);
    // Air as thin as a clear day's over land this size, unless asked thicker.
    look.uniform(sg::passes::scene, "uFogDensity", fog > 0 ? fog : 1.2 / std::max(400.0, std::hypot(land->w, land->d)));
    if (fog > 0) {
        if (sky_set)
            look.uniform(sg::passes::scene, "uFogColor", sky[0], sky[1], sky[2])
                .uniform(sg::passes::scene, "uSkyTop", sky[0], sky[1], sky[2])
                .uniform(sg::passes::scene, "uSkyHorizon", sky[0], sky[1], sky[2]);
    }
    look.uniform(sg::passes::composite, "uGrain", 0.0).fade(0.0);
    sg::wear(g, "land", "land.look");

    // The eye: as asked, or over a corner, looking across to the middle.
    if (!eye_set) {
        const double ex = land->x0 + land->w * 0.12, ez = land->z0 + land->d * 0.12;
        ev[0] = ex, ev[2] = ez;
        ev[3] = std::atan2(land->z0 + land->d * 0.5 - ez, land->x0 + land->w * 0.5 - ex) * 180 / 3.14159265358979;
        above = above < 0 ? std::max(land->w, land->d) * 0.08 : above;
    }
    if (above >= 0) ev[1] = land->height(ev[0], ev[2]) + above;
    sg::Element& eye = world.camera();
    eye.params.set(sg::keys::x, ev[0]).set(sg::keys::y, ev[1]).set(sg::keys::z, ev[2]);
    eye.params.set(sg::keys::yaw, ev[3] * 3.14159265358979 / 180).set(sg::keys::pitch, ev[4] * 3.14159265358979 / 180).set(sg::keys::fov, ev[5]);
    std::printf("eye at %.1f,%.1f,%.1f yaw %.0f pitch %.0f\n", ev[0], ev[1], ev[2], ev[3], ev[4]);

    sg::render::GLWorldView view;
    for (const auto& p : view.prepare(g)) std::printf("! %s\n", p.c_str());
    view.set_fixed_step(1.0 / 60);
    std::vector<unsigned char> px(static_cast<std::size_t>(W) * H * 3), rgb(px.size());
    for (int f = 0; f < frames; ++f) view.render(world, W, H);
    sg::gl::glReadPixels(0, 0, W, H, sg::gl::GL_RGB, sg::gl::GL_UNSIGNED_BYTE, px.data());
    for (int y = 0; y < H; ++y) std::copy_n(&px[static_cast<std::size_t>(H - 1 - y) * W * 3], static_cast<std::size_t>(W) * 3, &rgb[static_cast<std::size_t>(y) * W * 3]);
    if (out.empty()) out = name + ".view.png";
    std::ofstream o(out, std::ios::binary);
    o << sg::sculpt::png(rgb, W, H);
    std::printf("view: %s\n", out.c_str());
    return 0;
}
