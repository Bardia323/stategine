// Stategine - ripples on water (sg::ripple), against a real GL context.
//
// A still pool under a low sun, seen from above at a slant. A ripple begun
// on it: a world-second on, its ring is in the picture, where it has spread
// to; world time standing still, it stands still, frame after frame; at a
// world time a fresh view draws it the same; once it has died away the pool
// is as it would be had it never been.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

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
    const int W = 320, H = 180;
    sg::gl::Window window(W, H, "ripple");
    sg::StateGraph g;
    auto& field = g.add<sg::Spatial3D>("pool");
    g.set_initial("pool");
    field.params().set("sky", 1.0).set("far", 100.0);
    sg::Element& water = field.fixture("water", 0.0, 0.0, 0.0);
    water.params.set(sg::keys::sx, 20.0).set(sg::keys::sy, 0.05).set(sg::keys::sz, 20.0).set("surface", 18.0).set("roughness", 0.08);
    water.params.set("r", 0.1).set("g", 0.2).set("b", 0.25).set("calm", 1.0);
    field.light(sg::Key{"sun"}, {0.0, 50.0, 0.0});
    field.find(sg::Key{"sun"})->params.set("sun", 1.0).set(sg::keys::intensity, 1.5).set("dx", 0.0).set("dy", -0.35).set("dz", -0.94);
    sg::Element& eye = field.camera();
    eye.params.set(sg::keys::x, 0.0).set(sg::keys::y, 6.0).set(sg::keys::z, -8.0).set(sg::keys::yaw, 1.5707963).set(sg::keys::pitch, -0.6);
    const auto el = [&](const char* id) -> sg::Element& { return *field.find(sg::Key{id}); };

    sg::render::GLWorldView view;
    view.prepare(g);
    view.set_fixed_step(1.0 / 60);
    const auto shot = [&](sg::render::GLWorldView& v, int frames) {
        std::vector<unsigned char> px(static_cast<std::size_t>(W) * H * 3);
        for (int i = 0; i < frames; ++i) v.render(field, W, H), window.swap();
        sg::gl::glReadPixels(0, 0, W, H, sg::gl::GL_RGB, sg::gl::GL_UNSIGNED_BYTE, px.data());
        return px;
    };
    const auto apart = [](const std::vector<unsigned char>& a, const std::vector<unsigned char>& b, int& most) {
        double sum = 0.0;
        most = 0;
        for (std::size_t i = 0; i < a.size(); ++i) {
            const int d = std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i]));
            sum += d, most = std::max(most, d);
        }
        return sum / static_cast<double>(a.size());
    };
    int most = 0;
    field.params().set("own_time", 10.0);
    const std::vector<unsigned char> still = shot(view, 30);

    sg::ripple(el("water"), 0.0, 2.0, 1.0, 10.0);
    field.params().set("own_time", 11.0);
    const std::vector<unsigned char> ringed = shot(view, 30);
    const double changed = apart(ringed, still, most);
    std::printf("a world-second after a ripple: %.3f levels a pixel apart, %d at most\n", changed, most);
    check(changed > 0.05 && most > 20, "a ripple's ring is on the water a world-second on");
    // (SG_RIPPLE_SHOT=file keeps that picture.)
    if (const char* path = std::getenv("SG_RIPPLE_SHOT"))
        if (FILE* f = std::fopen(path, "wb")) {
            std::fprintf(f, "P6 %d %d 255\n", W, H);
            for (int j = H - 1; j >= 0; --j) std::fwrite(&ringed[static_cast<std::size_t>(j) * W * 3], 1, static_cast<std::size_t>(W) * 3, f);
            std::fclose(f);
        }

    const std::vector<unsigned char> again = shot(view, 1);
    apart(again, ringed, most);
    check(most == 0, "its world's time standing still, it stands still (" + std::to_string(most) + " levels a frame on)");
    {
        sg::render::GLWorldView fresh;
        fresh.prepare(g);
        fresh.set_fixed_step(1.0 / 60);
        const std::vector<unsigned char> theirs = shot(fresh, 30);
        const double d = apart(theirs, ringed, most);
        check(d < 0.05, "at a world time, a fresh view draws it the same (" + std::to_string(d) + " levels a pixel apart)");
    }
    field.params().set("own_time", 12.0);
    const std::vector<unsigned char> later = shot(view, 30);
    const double moved = apart(later, ringed, most);
    check(moved > 0.05, "a world-second more, it has spread on (" + std::to_string(moved) + " levels a pixel)");
    field.params().set("own_time", 20.0);
    const std::vector<unsigned char> gone = shot(view, 30);
    // (Against the pool at that time with no ripple ever begun on it.)
    std::vector<sg::Key> kept;
    for (const auto& [k, v] : el("water").params)
        if (k.str().rfind("ripple", 0) == 0) kept.push_back(k);
    for (sg::Key k : kept) el("water").params.erase(k);
    const std::vector<unsigned char> never = shot(view, 30);
    const double left = apart(gone, never, most);
    check(most <= 2, "died away, the pool is as it was (" + std::to_string(left) + " levels a pixel, " + std::to_string(most) + " at most)");

    std::printf(ok ? "all ok\n" : "FAILED\n");
    return ok ? 0 : 1;
}
