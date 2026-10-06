// sgterrain: a terrain recipe in (sg::terrain's language, docs/terrain.md), a
// picture of the land out - from above, shaded, its layers, water, roads and
// things - and what it says on the way: its size, how long it took, what lies
// on it, what the recipe got wrong.
//
//   sgterrain land.terrain                      land.png beside it
//   sgterrain -p swamp -o swamp.png -s 1024     a preset (sgterrain --presets)
//   sgterrain -e "size 200 200 ; noise 10 80"   lines split by ' ; '
//   sgterrain land.terrain --eye 0,30,-120,90,-10,70 -v eye.png
//                                               one view, as the modeller draws
//                                               (x,y,z,yaw,pitch,fov; slow with
//                                               many things - see sgland for the
//                                               renderer's own)
//   sgterrain land.terrain --obj land.obj       all of it as a mesh
//   sgterrain --preset lake                     print a preset's recipe
//   sgterrain --words                           the language
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "sg/domains/Terrain.hpp"

namespace fs = std::filesystem;

namespace {
std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
bool write(const std::string& path, const std::string& bytes) {
    std::ofstream o(path, std::ios::binary);
    o << bytes;
    return bool(o);
}
}  // namespace

int main(int argc, char** argv) {
    std::string recipe, out, view, obj, name = "land";
    int size = 1024;
    bool eye_set = false;
    double ev[6] = {0, 30, 0, 0, -10, 70};
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "-o") out = next();
        else if (a == "-s") size = std::max(64, std::atoi(next().c_str()));
        else if (a == "-v") view = next();
        else if (a == "--obj") obj = next();
        else if (a == "--eye") {
            std::sscanf(next().c_str(), "%lf,%lf,%lf,%lf,%lf,%lf", &ev[0], &ev[1], &ev[2], &ev[3], &ev[4], &ev[5]);
            eye_set = true;
        } else if (a == "--words") {
            std::printf("%s", sg::terrain::words().c_str());
            return 0;
        } else if (a == "--presets") {
            for (const std::string& p : sg::terrain::presets()) std::printf("%s\n", p.c_str());
            return 0;
        } else if (a == "--preset") {
            std::printf("%s", sg::terrain::preset(next()).c_str());
            return 0;
        } else if (a == "-p") {
            name = next();
            recipe = sg::terrain::preset(name);
            if (recipe.empty()) return std::fprintf(stderr, "no preset %s (sgterrain --presets)\n", name.c_str()), 1;
            for (char& c : name)
                if (c == ' ') c = '_';
        } else if (a == "-e") {
            recipe = next();
            for (std::size_t k; (k = recipe.find(" ; ")) != std::string::npos;) recipe.replace(k, 3, "\n");
        } else {
            recipe = slurp(a);
            name = fs::path(a).replace_extension().string();
            if (recipe.empty()) return std::fprintf(stderr, "cannot read %s\n", a.c_str()), 1;
        }
    }
    if (recipe.empty()) {
        std::fprintf(stderr, "sgterrain <recipe> | -p <preset> | -e \"line ; line\" [-o map.png] [-s px] [--eye x,y,z,yaw,pitch,fov -v view.png] [--obj mesh.obj]\n");
        return 1;
    }
    const auto t0 = std::chrono::steady_clock::now();
    const sg::terrain::Land land = sg::terrain::make(recipe);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    double lo = 1e30, hi = -1e30;
    for (float h : land.h) lo = std::min(lo, double(h)), hi = std::max(hi, double(h));
    std::printf("%.0f x %.0f m, a height every %.2f m (%d x %d), from %.1f to %.1f m high, made in %.0f ms\n", land.w, land.d, land.cell, land.nx + 1,
                land.nz + 1, lo, hi, ms);
    for (std::size_t k = 0; k < land.layers.size() && k < 4; ++k) {
        double share = 0;
        for (std::size_t q = 0; q < land.h.size(); ++q) share += land.splat[q * 4 + k];
        std::printf("  layer %-8s surface %2d  covers %4.1f%%\n", land.layers[k].name.c_str(), land.layers[k].surface, 100.0 * share / 255.0 / double(land.h.size()));
    }
    for (const auto& w : land.waters)
        std::printf("  water %-8s at %.2f m, %.0f x %.0f m, %.1f m deep at most, %zu faces\n", w.name.c_str(), w.hi.y, w.hi.x - w.lo.x, w.hi.z - w.lo.z,
                    w.deepest, w.corners.size() / 24);
    for (const auto& r : land.roads) std::printf("  road  %-8s %.1f m wide, %zu faces\n", r.name.c_str(), r.width, r.corners.size() / 24);
    for (std::size_t t = 0; t < land.things.size(); ++t) {
        std::size_t n = 0;
        for (const auto& p : land.placed) n += p.thing == int(t);
        std::printf("  thing %-8s %zu variants, %zu placed\n", land.things[t].name.c_str(), land.things[t].recipes.size(), n);
    }
    if (!land.errors.empty()) std::printf("errors:\n%s", land.errors.c_str());

    int w, h;
    const auto rgb = sg::terrain::map(land, size, w, h);
    if (out.empty()) out = name + ".png";
    if (write(out, sg::sculpt::png(rgb, w, h))) std::printf("map: %s\n", out.c_str());
    if (eye_set || !obj.empty()) {
        const sg::sculpt::Model m = sg::terrain::model(land);
        std::printf("as one model: %zu faces in %zu parts\n", m.triangles, m.parts.size());
        if (eye_set) {
            if (view.empty()) view = name + ".view.png";
            const auto pic = sg::sculpt::picture_from(m, {ev[0], ev[1], ev[2]}, ev[3], ev[4], ev[5], size);
            if (write(view, sg::sculpt::png(pic, size, size))) std::printf("view: %s\n", view.c_str());
        }
        if (!obj.empty() && write(obj, sg::sculpt::to_obj(m, name))) std::printf("mesh: %s\n", obj.c_str());
    }
    return land.errors.empty() ? 0 : 2;
}
