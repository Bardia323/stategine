// Stategine - each part of a view asks only the lamps that can light it, against
// a real GL context (Quality::lamp_cells).
//
// A long hall lit by a row of lamps whose light ends a few metres off (each
// says its `range`), its air scattering their light. With the lamp cells and
// without, the picture is the same; with them, the card spends less on what
// it lights and on its air.
#include <cmath>
#include <cstdio>
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
    const int W = 960, H = 540;
    sg::gl::Window window(W, H, "lamps");
    sg::StateGraph g;
    auto& hall = g.add<sg::Spatial3D>("hall");
    g.set_initial("hall");
    hall.params().set("room_w", 4.0).set("room_d", 30.0).set("room_h", 3.0);
    // A row of lamps down the hall, each lighting a few metres round it; every
    // other one a spot shining down.
    for (int i = 0; i < 24; ++i) {
        sg::Element& l = hall.light(sg::Key{"lamp" + std::to_string(i)}, {1.0 + 2.0 * (i % 2), 2.6, 1.0 + 1.2 * i});
        l.params.set(sg::keys::intensity, 0.8).set("range", 3.5).set("cast", 0.0);
        if (i % 2) l.params.set("dx", 0.0).set("dy", -1.0).set("dz", 0.0).set("inner", 0.5).set("outer", 0.8);
        else l.params.set("inner", 3.14159).set("outer", 3.14159);
    }
    for (int i = 0; i < 10; ++i) {
        sg::Element& b = hall.fixture(sg::Key{"crate" + std::to_string(i)}, 1.0 + (i % 3), 0.0, 2.0 + 2.6 * i);
        b.params.set(sg::keys::sx, 0.6).set(sg::keys::sy, 0.8).set(sg::keys::sz, 0.6);
    }
    auto& hazy = g.add<sg::LookState>("hazy");
    hazy.setting(sg::passes::scene, "scatter", 0.4);
    sg::wear(g, "hall", "hazy");
    sg::Element& eye = hall.camera();
    eye.params.set(sg::keys::x, 2.0).set(sg::keys::y, 1.6).set(sg::keys::z, 0.4).set(sg::keys::yaw, 1.5707963).set(sg::keys::pitch, -0.05);

    sg::render::GLWorldView view;
    view.prepare(g);
    view.set_card_timing(true);
    sg::render::GLQuality with = view.quality(), without = view.quality();
    with.lamp_cells = true, without.lamp_cells = false;
    // The picture, and what the card spent lighting it and its air, a frame
    // (the eye turning a little each frame, so the air is gathered again).
    const auto run = [&](const sg::render::GLQuality& q, std::vector<unsigned char>& px, double& lit, double& air) {
        view.set_quality(q);
        lit = air = 0.0;
        int n = 0;
        uint64_t seen = 0;
        for (int i = 0; i < 90; ++i) {
            eye.params.set(sg::keys::yaw, 1.5707963 + 0.002 * (i % 2));
            view.render(hall, W, H);
            window.swap();
            const auto& t = view.card_times();
            if (i > 30 && t.frame != 0 && t.frame != seen) seen = t.frame, lit += t.lit, air += t.air, ++n;
        }
        eye.params.set(sg::keys::yaw, 1.5707963);
        for (int i = 0; i < 20; ++i) view.render(hall, W, H);
        px.assign(static_cast<std::size_t>(W) * H * 3, 0);
        sg::gl::glReadPixels(0, 0, W, H, sg::gl::GL_RGB, sg::gl::GL_UNSIGNED_BYTE, px.data());
        lit /= std::max(n, 1), air /= std::max(n, 1);
    };
    std::vector<unsigned char> a, b;
    double lit_with = 0, air_with = 0, lit_without = 0, air_without = 0;
    run(without, b, lit_without, air_without);
    run(with, a, lit_with, air_with);
    std::printf("lit %.3f ms with the cells, %.3f without; air %.3f, %.3f\n", lit_with, lit_without, air_with, air_without);
    check(lit_with < 0.8 * lit_without, "what is lit asks fewer lamps: the card spends less on it");
    check(air_with < 0.9 * air_without, "and on the air");
    // The same picture, to the level - with clear air (its gatherings sample
    // other points each time, so two runs of hazy air differ in their grain).
    hazy.setting(sg::passes::scene, "scatter", 0.0);
    double unused = 0;
    run(without, b, unused, unused);
    run(with, a, unused, unused);
    double apart = 0.0;
    int most = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const int d = std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i]));
        apart += d, most = std::max(most, d);
    }
    apart /= static_cast<double>(a.size() / 3);
    check(most == 0, "the picture with the lamp cells is the picture without (" + std::to_string(apart) + " levels a pixel apart, " +
                         std::to_string(most) + " at most)");
    return ok ? 0 : 1;
}
