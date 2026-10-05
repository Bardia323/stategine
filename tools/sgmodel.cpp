// sgmodel: a recipe in (sg::Modeler's language, docs/modeler.md), a picture of
// what it makes out - four views in one PNG - and, if asked, its mesh as an
// .obj. What it says on the way: faces, parts, size, whether it is closed,
// how long it took, and what the recipe got wrong.
//
//   sgmodel castle.recipe                 castle.png beside it
//   sgmodel castle.recipe -o out.png -s 1200 --obj out.obj --cell 0.05
//   sgmodel -e "box 1 1 1 / sub sphere 0.6" -o box.png   (' / ' between lines)
//   sgmodel hall.recipe --eye 0,1.6,5,0,10,90     one view from inside: x,y,z,yaw,pitch,fov
//
// `import` reads files beside the recipe.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <tuple>

#include "sg/domains/Modeler.hpp"

namespace fs = std::filesystem;

namespace {
std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Edges that are not shared by exactly two faces, once corners at one place
// are one corner: 0 for a closed surface.
std::size_t open_edges(const sg::sculpt::Model& m) {
    std::map<std::tuple<long, long, long>, int> ids;
    const auto id = [&](const float* c) {
        const auto key = std::make_tuple(std::lround(c[0] * 1e4), std::lround(c[1] * 1e4), std::lround(c[2] * 1e4));
        auto it = ids.find(key);
        if (it == ids.end()) it = ids.emplace(key, int(ids.size())).first;
        return it->second;
    };
    std::map<std::pair<int, int>, int> edges;
    for (const auto& p : m.parts)
        for (std::size_t i = 0; i + 23 < p.corners.size(); i += 24) {
            const int v[3] = {id(&p.corners[i]), id(&p.corners[i + 8]), id(&p.corners[i + 16])};
            for (int k = 0; k < 3; ++k) {
                const int a = v[k], b = v[(k + 1) % 3];
                if (a != b) ++edges[{std::min(a, b), std::max(a, b)}];
            }
        }
    std::size_t open = 0;
    for (const auto& [e, n] : edges) open += n != 2;
    return open;
}
}  // namespace

int main(int argc, char** argv) {
    std::string recipe, out, obj;
    fs::path from = ".";
    int size = 1000;
    sg::sculpt::Options o;
    bool eye_set = false;
    sg::Vec3d eye;
    double eye_yaw = 0, eye_pitch = 0, eye_fov = 90;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "-o") out = next();
        else if (a == "-s") size = std::max(64, std::atoi(next().c_str()));
        else if (a == "--obj") obj = next();
        else if (a == "--cell") o.cell = std::atof(next().c_str());
        else if (a == "--eye") {
            // x,y,z[,yaw[,pitch[,fov]]]: one view, in perspective, from there
            const std::string e = next();
            double v[6] = {0, 1.6, 0, 0, 0, 90};
            std::sscanf(e.c_str(), "%lf,%lf,%lf,%lf,%lf,%lf", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]);
            eye_set = true, eye = {v[0], v[1], v[2]}, eye_yaw = v[3], eye_pitch = v[4], eye_fov = v[5];
        }
        else if (a == "-e") {
            recipe = next();
            for (std::size_t at; (at = recipe.find(" / ")) != std::string::npos;) recipe.replace(at, 3, "\n");
            if (out.empty()) out = "model.png";
        } else {
            recipe = slurp(a);
            from = fs::path(a).parent_path();
            if (out.empty()) out = fs::path(a).replace_extension(".png").string();
        }
    }
    if (recipe.empty()) {
        std::fprintf(stderr, "sgmodel <recipe> [-o picture.png] [-s pixels] [--obj mesh.obj] [--cell metres] | -e \"line / line\"\n");
        return 2;
    }
    sg::sculpt::Files files;
    files.read = [from](const std::string& path, std::string& text) {
        for (const fs::path p : {fs::path(path), from / path})
            if (fs::exists(p)) return text = slurp(p), true;
        return false;
    };
    files.stamp = [](const std::string&) { return 0LL; };
    const auto t0 = std::chrono::steady_clock::now();
    const sg::sculpt::Model m = sg::sculpt::build(recipe, o, &files);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("%zu faces in %zu parts, %.3f x %.3f x %.3f m, built in %.0f ms; %zu open edges\n", m.triangles, m.parts.size(), m.size().x, m.size().y,
                m.size().z, ms, open_edges(m));
    std::printf("  from %.3f,%.3f,%.3f to %.3f,%.3f,%.3f; its foot (where a thing drawn from it stands) at %.3f,%.3f,%.3f\n", m.lo.x, m.lo.y, m.lo.z, m.hi.x,
                m.hi.y, m.hi.z, m.foot().x, m.foot().y, m.foot().z);
    for (const auto& p : m.parts) std::printf("  %s: %zu faces\n", p.material.empty() ? "(no material)" : p.material.c_str(), p.corners.size() / 24);
    if (!m.openings.empty()) std::printf("  %zu openings asked of the walls round it\n", m.openings.size());
    if (!m.errors.empty()) std::printf("%s", m.errors.c_str());
    std::ofstream(out, std::ios::binary) << sg::sculpt::png(eye_set ? sg::sculpt::picture_from(m, eye, eye_yaw, eye_pitch, eye_fov, size) : sg::sculpt::picture(m, size, size), size, size);
    std::printf("picture: %s\n", out.c_str());
    if (!obj.empty()) std::ofstream(obj) << sg::sculpt::to_obj(m, fs::path(obj).stem().string()), std::printf("mesh: %s\n", obj.c_str());
    return m.errors.empty() ? 0 : 1;
}
