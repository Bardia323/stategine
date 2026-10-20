// Stategine - Grounds: natural surfaces made from laws. Each material's maps
// tile (the first column goes on from the last), the same params make the
// same maps, its channels are in their ranges and not flat (things lie on
// it), what is said of it shows (more moss, greener; a wetter mud, smoother);
// and a Texture wears one by name once define() has run.
// SG_GROUNDS_SHOW=folder: each material's four maps as PNGs there, to look at.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "sg/domains/Grounds.hpp"
#include "sg/domains/Modeler.hpp"
#include "sg/domains/Texture.hpp"

namespace {
int failed = 0;
void check(bool ok, const std::string& what) {
    std::printf("[%s] %s\n", ok ? "ok" : "FAIL", what.c_str());
    failed += !ok;
}
double mean(const std::vector<unsigned char>& px, int ch) {
    double s = 0;
    for (std::size_t i = ch; i < px.size(); i += 4) s += px[i];
    return s / (px.size() / 4) / 255.0;
}
double spread(const std::vector<unsigned char>& px, int ch) {
    const double m = mean(px, ch);
    double s = 0;
    for (std::size_t i = ch; i < px.size(); i += 4) s += (px[i] / 255.0 - m) * (px[i] / 255.0 - m);
    return std::sqrt(s / (px.size() / 4));
}
// How much the tile's last column differs from its first, against how much
// neighbouring columns inside it differ: a seam is a jump the inside has not.
double seam(const std::vector<unsigned char>& px, int n) {
    double edge = 0, inside = 0;
    for (int y = 0; y < n; ++y) {
        edge += std::abs(px[(y * n + n - 1) * 4 + 1] - px[(y * n) * 4 + 1]);
        inside += std::abs(px[(y * n + n / 2) * 4 + 1] - px[(y * n + n / 2 + 1) * 4 + 1]);
    }
    return edge / std::max(inside, 1.0);
}
void show(const std::string& dir, const std::string& name, const std::vector<unsigned char>& rgba, int n) {
    std::vector<unsigned char> rgb(static_cast<std::size_t>(n) * n * 3);
    for (std::size_t i = 0; i < rgb.size() / 3; ++i)
        for (int c = 0; c < 3; ++c) rgb[i * 3 + c] = rgba[i * 4 + c];
    std::ofstream(dir + "/" + name + ".png", std::ios::binary) << sg::sculpt::png(rgb, n, n);
}
}  // namespace

int main() {
    const char* dir = std::getenv("SG_GROUNDS_SHOW");
    const int n = dir ? std::atoi(std::getenv("SG_GROUNDS_PX") ? std::getenv("SG_GROUNDS_PX") : "1024") : 128;
    for (const char* name : {"meadow", "lichen_rock", "gravel", "mud"}) {
        sg::Params p;
        p.set("tile", dir ? 1.0 : 0.5).set("seed", 3.0);
        const auto t = sg::grounds::tile_maps(name, p, n);
        const std::string m = name;
        check(t.px == n && t.albedo.size() == static_cast<std::size_t>(n) * n * 4, m + ": four maps of the size asked");
        check(seam(t.albedo, n) < 3.0 && seam(t.height, n) < 3.0, m + ": its maps tile, no seam at their edge");
        check(spread(t.height, 0) > 0.02 && spread(t.albedo, 1) > 0.02, m + ": things lie on it - its height and colour vary");
        check(mean(t.normal, 2) > 0.6, m + ": its normals face out of it");
        const auto again = sg::grounds::tile_maps(name, p, n);
        check(again.albedo == t.albedo && again.height == t.height, m + ": the same params make the same maps");
        if (dir) {
            show(dir, m + "_albedo", t.albedo, n), show(dir, m + "_normal", t.normal, n);
            show(dir, m + "_surface", t.surface, n), show(dir, m + "_height", t.height, n);
        }
    }
    {
        sg::Params p;
        p.set("tile", 0.5).set("seed", 2.0);
        const auto bare = sg::grounds::tile_maps("meadow", sg::Params(p).set("moss", 0.0).set("grass", 0.0), n);
        const auto mossy = sg::grounds::tile_maps("meadow", sg::Params(p).set("moss", 1.0).set("grass", 0.0), n);
        check(mean(mossy.albedo, 1) - mean(mossy.albedo, 0) > mean(bare.albedo, 1) - mean(bare.albedo, 0) + 0.01, "meadow: more moss, greener");
        const auto dry = sg::grounds::tile_maps("mud", sg::Params(p).set("wet", 0.0), n);
        const auto wet = sg::grounds::tile_maps("mud", sg::Params(p).set("wet", 1.0), n);
        check(mean(wet.surface, 1) < mean(dry.surface, 1) - 0.1, "mud: wetter, smoother");
    }
    {
        sg::grounds::define();
        sg::Texture t(sg::Key{"rock"}, 32);
        t.hear(sg::Event{sg::Texture::set_event(), sg::Params{}.set("generator", std::string("lichen_rock")).set("tile", 0.5)});
        t.dispatch_pending();
        check(t.has_surface() && spread(t.raster(), 0) > 0.02, "a Texture wears one by name, its surface map with it");
    }
    std::printf("%s\n", failed ? "FAILED" : "all passed");
    return failed ? 1 : 0;
}
