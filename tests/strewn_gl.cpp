// Stategine - things strewn by the many (Spatial3D::placings), against a real
// GL context.
//
// A wooded land laid twice: its things as fixtures of their own, a part
// each, and strewn (`strewn far`): a few elements, each standing for all of
// one part's placings. Seen from the same eye they are the same picture,
// shadows and all; the strewn land is a few elements where the other is
// thousands, and its things drawn are those in view. Its `far` says how far
// out they are drawn: nearer, fewer.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "sg/domains/Light.hpp"
#include "sg/domains/Terrain.hpp"
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
    const int W = 480, H = 270;
    sg::gl::Window window(W, H, "strewn");
    struct Laid {
        sg::StateGraph g;
        sg::Spatial3D* world = nullptr;
        std::size_t things = 0;
        std::shared_ptr<const sg::terrain::Land> land;
    };
    const auto lay = [&](Laid& l, const std::string& extra) {
        l.world = &l.g.add<sg::Spatial3D>("land");
        l.g.set_initial("land");
        l.land = sg::terrain::build(sg::terrain::preset("hills") + extra);
        l.things = sg::terrain::lay(*l.world, "land", l.land);
        l.world->params().set("sky", 1.0).set("far", 600.0);
        const sg::Daylight d = sg::daylight(15.5, {0.3, 0.32, 0.22});
        sg::Element& sun = l.world->light("sun", {0, 400, 0}, d.light.r, d.light.g, d.light.b);
        sun.params.set("sun", 1.0).set(sg::keys::intensity, d.intensity).set("extent", 60.0);
        sun.params.set("dx", -d.sun.x).set("dy", -d.sun.y).set("dz", -d.sun.z);
        auto& look = l.g.add<sg::LookState>("land.look");
        sg::show_daylight(look, d);
        look.uniform(sg::passes::composite, "uGrain", 0.0).fade(0.0);
        sg::wear(l.g, "land", "land.look");
        sg::Element& eye = l.world->camera();
        const double x = -60.0, z = -60.0;
        eye.params.set(sg::keys::x, x).set(sg::keys::y, l.land->height(x, z) + 12.0).set(sg::keys::z, z);
        eye.params.set(sg::keys::yaw, 0.785398).set(sg::keys::pitch, -0.12).set(sg::keys::fov, 70.0);
    };
    Laid fixtures, strewn;
    lay(fixtures, "");
    lay(strewn, "\nstrewn 400\n");
    std::printf("laid: %zu things as %zu elements; strewn, %zu things as %zu elements\n", fixtures.things, fixtures.world->elements().size(),
                strewn.things, strewn.world->elements().size());
    check(strewn.things == fixtures.things && strewn.things > 1000, "strewn, as many things on the land");
    check(strewn.world->elements().size() * 20 < fixtures.world->elements().size(), "as a few elements, where they were thousands");

    const auto shot = [&](Laid& l, sg::render::GLWorldView& view, double& ms) {
        std::vector<unsigned char> px(static_cast<std::size_t>(W) * H * 3);
        for (int i = 0; i < 6; ++i) view.render(*l.world, W, H), window.swap();
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 10; ++i) view.render(*l.world, W, H);
        sg::gl::glFinish();
        ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 10.0;
        sg::gl::glReadPixels(0, 0, W, H, sg::gl::GL_RGB, sg::gl::GL_UNSIGNED_BYTE, px.data());
        return px;
    };
    sg::render::GLWorldView fixed_view, strewn_view;
    fixed_view.prepare(fixtures.g);
    strewn_view.prepare(strewn.g);
    fixed_view.set_fixed_step(1.0 / 60), strewn_view.set_fixed_step(1.0 / 60);
    double fixed_ms = 0, strewn_ms = 0;
    const std::vector<unsigned char> a = shot(fixtures, fixed_view, fixed_ms);
    const std::vector<unsigned char> b = shot(strewn, strewn_view, strewn_ms);
    const int drawn = strewn_view.times().strewn;
    double sum = 0;
    std::size_t off = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const int d = std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i]));
        sum += d, off += d > 24 ? 1 : 0;
    }
    std::printf("a frame: %.2f ms as fixtures, %.2f strewn (%d strewn drawn); %.3f levels a pixel apart, %zu of %zu past 24\n", fixed_ms, strewn_ms,
                drawn, sum / a.size(), off, a.size());
    check(sum / a.size() < 1.0 && off < a.size() / 200, "the same picture, shadows and all");
    check(drawn > 0 && drawn < static_cast<int>(strewn.things), "only those in view drawn");
    // (SG_STREWN_SHOTS=dir keeps both pictures there.)
    if (const char* dir = std::getenv("SG_STREWN_SHOTS"))
        for (const auto& [name, px] : {std::pair<const char*, const std::vector<unsigned char>*>{"fixtures", &a}, {"strewn", &b}}) {
            const std::string path = std::string(dir) + "/" + name + ".ppm";
            if (FILE* f = std::fopen(path.c_str(), "wb")) {
                std::fprintf(f, "P6 %d %d 255\n", W, H);
                for (int j = H - 1; j >= 0; --j) std::fwrite(&(*px)[static_cast<std::size_t>(j) * W * 3], 1, static_cast<std::size_t>(W) * 3, f);
                std::fclose(f);
            }
        }

    // Nearer `far`, fewer.
    for (sg::Element& e : strewn.world->elements())
        if (e.kind == sg::kinds::strewn) e.params.set("far", 40.0);
    double near_ms = 0;
    shot(strewn, strewn_view, near_ms);
    const int near_drawn = strewn_view.times().strewn;
    std::printf("`far` 40 m: %d drawn\n", near_drawn);
    check(near_drawn < drawn / 2, "drawn out to its `far` only");

    std::printf(ok ? "all ok\n" : "FAILED\n");
    return ok ? 0 : 1;
}
