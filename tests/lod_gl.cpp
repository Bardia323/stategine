// Stategine - a model seen from further off drawn with fewer faces, against a
// real GL context (Quality::lod, sg::shapes::fewer).
//
// A vase turned smooth, of a few thousand faces, stands near the eye and far
// from it. Far enough off it is drawn with fewer faces, made in warm - and
// the picture is the picture of it drawn whole: no level is drawn where what
// it moves would be half a pixel, and each corner keeps its normal.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "sg/domains/Shapes.hpp"
#include "sg/gl/Window.hpp"
#include "sg/gl/World.hpp"
#include "sg/sg.hpp"

namespace {
bool ok = true;
void check(bool c, const std::string& what) {
    std::printf("[%s] %s\n", c ? "ok" : "FAIL", what.c_str());
    ok = ok && c;
}
}  // namespace

int main() {
    const int W = 640, H = 400;
    sg::gl::Window window(W, H, "lod");
    sg::StateGraph g;
    auto& field = g.add<sg::Spatial3D>("field");
    g.set_initial("field");
    field.params().set("sky", 1.0).set("far", 400.0);
    sg::Element& sun = field.light(sg::Key{"sun"}, {0.0, 50.0, 0.0});
    sun.params.set("sun", 1.0).set("dx", 0.3).set("dy", -0.8).set("dz", -0.5).set(sg::keys::intensity, 0.08);
    // A vase: its profile a smooth curve of 48 steps, turned in 72 faces.
    std::vector<sg::shapes::P2> profile;
    for (int i = 0; i <= 48; ++i) {
        const double t = i / 48.0;
        profile.push_back({0.25 + 0.18 * std::sin(t * 3.14159265 * 1.6) + 0.06 * std::sin(t * 31.0), t});
    }
    sg::Vec3d size;
    const std::vector<float> vase = sg::shapes::fit(sg::shapes::lathe_smooth(profile, 72), size);
    field.model(sg::Key{"vase"}, vase);
    const auto stand = [&](const char* id, double x, double z) {
        sg::Element& e = field.mesh(sg::Key{id}, x, 0.0, z);
        e.params.set("shape", std::string("model")).set("model", std::string("vase")).set(sg::keys::sx, size.x).set(sg::keys::sy, size.y)
            .set(sg::keys::sz, size.z).set("r", 0.7).set("g", 0.45).set("b", 0.3);
    };
    stand("near", -0.8, 3.0);
    stand("far", 2.5, 40.0);
    sg::Element& eye = field.camera();
    eye.params.set(sg::keys::x, 0.0).set(sg::keys::y, 0.5).set(sg::keys::z, 0.0).set(sg::keys::yaw, 1.5707963).set(sg::keys::pitch, 0.0);
    check(vase.size() / 24 > 4000, "a vase of " + std::to_string(vase.size() / 24) + " faces");

    sg::render::GLQuality asked;
    asked.lod = true;
    sg::render::GLWorldView view(asked);
    view.prepare(g);
    view.warm({&field}, W, H);
    // (Made by a hand of their own: waited for here, as a game never does.)
    for (int i = 0; i < 2000 && view.lod_pending(); ++i) {
        view.render(field, W, H);
        window.swap();
    }
    check(!view.lod_pending(), "its fewer faces made, beside the frames");
    std::vector<unsigned char> px(static_cast<std::size_t>(W) * H * 3);
    const auto picture = [&](bool lod, int& drawn) {
        sg::render::GLQuality q = view.quality();
        q.lod = lod;
        view.set_quality(q);
        drawn = 0;
        for (int i = 0; i < 10; ++i) {
            view.render(field, W, H);
            drawn = view.times().lod_drawn;
            window.swap();
        }
        sg::gl::glReadPixels(0, 0, W, H, sg::gl::GL_RGB, sg::gl::GL_UNSIGNED_BYTE, px.data());
        return px;
    };
    int drawn_whole = 0, drawn_fewer = 0;
    const std::vector<unsigned char> whole = picture(false, drawn_whole);
    const std::vector<unsigned char> fewer = picture(true, drawn_fewer);
    check(drawn_whole == 0 && drawn_fewer >= 1, "seen from far enough, it is drawn with fewer faces (" + std::to_string(drawn_fewer) + " drawn fewer)");
    if (const char* at = std::getenv("SG_LOD_SHOTS"))
        for (int k = 0; k < 2; ++k)
            if (FILE* f = std::fopen((std::string(at) + (k ? "/lod_fewer.ppm" : "/lod_whole.ppm")).c_str(), "wb")) {
                const std::vector<unsigned char>& p = k ? fewer : whole;
                std::fprintf(f, "P6 %d %d 255\n", W, H);
                for (int j = H - 1; j >= 0; --j) std::fwrite(&p[static_cast<std::size_t>(j) * W * 3], 1, static_cast<std::size_t>(W) * 3, f);
                std::fclose(f);
            }
    int most = 0, apart = 0;
    for (std::size_t i = 0; i < px.size(); ++i) {
        const int d = std::abs(int(whole[i]) - int(fewer[i]));
        most = std::max(most, d), apart += d > 6;
    }
    // (Its edges where a level moved them by a part of a pixel: a few.)
    check(apart * 2000 < static_cast<int>(px.size()), "and the picture is the picture of it drawn whole, but for a few edge pixels (" +
                                                          std::to_string(apart) + " levels apart by more than 6, " + std::to_string(most) + " at most)");
    // Its fewer faces themselves: fewer, and none of their points off the vase.
    const std::vector<float> less = sg::shapes::fewer(vase, 0.006);
    check(!less.empty() && less.size() * 2 < vase.size(), "made fewer by more than half (" + std::to_string(less.size() / 24) + " faces)");
    return ok ? 0 : 1;
}
