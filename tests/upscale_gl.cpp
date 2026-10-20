// Stategine - a view drawn at fewer pixels than the screen's and made whole
// over frames (Quality::render_scale, with taa), against a real GL context.
//
// A room of boxes of many sizes and colours under a lamp, the eye still:
// drawn at the screen's pixels with taa, and at three quarters of them each
// way, the picture made over frames is the same picture - as sharp, its
// edges where they are - and the scene costs the card less.
#include <chrono>
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
    const int W = 1280, H = 720;
    // (The card's time compared: never held to the screen's refresh - an
    // idle card slows its clocks, and what it does takes longer.)
#ifdef _WIN32
    _putenv("SG_NO_VSYNC=1");
#else
    setenv("SG_NO_VSYNC", "1", 1);
#endif
    sg::gl::Window window(W, H, "upscale");
    sg::StateGraph g;
    auto& room = g.add<sg::Spatial3D>("room");
    g.set_initial("room");
    room.light("lamp", {4, 3.2, 4}).params.set(sg::keys::intensity, 1.4).set("outer", 1.5);
    int n = 0;
    for (int i = 0; i < 24; ++i)
        for (int j = 0; j < 24; ++j, ++n) {
            const double k = std::sin(n * 12.9898) * 43758.5453, f = k - std::floor(k);
            sg::Element& e = room.fixture(sg::Key{"t" + std::to_string(n)}, 0.5 + i * 0.3, 0.0, 0.5 + j * 0.3);
            e.params.set(sg::keys::sx, 0.06 + 0.12 * f).set(sg::keys::sy, 0.1 + 0.8 * f * f).set(sg::keys::sz, 0.08)
                .set(sg::keys::yaw, f * 6.28).set(sg::keys::r, f).set(sg::keys::g, 1 - f).set(sg::keys::b, 0.4);
        }
    sg::Element& eye = room.camera();
    eye.params.set(sg::keys::x, 4.0).set(sg::keys::y, 2.4).set(sg::keys::z, 9.5).set(sg::keys::yaw, -1.5707963).set(sg::keys::pitch, -0.45);

    struct Shot {
        std::vector<unsigned char> px;
        double scene_ms = 0;
    };
    const auto shot = [&](float scale) {
        sg::render::GLQuality q;
        q.taa = true;
        q.msaa = 0;
        q.render_scale = scale;
        sg::render::GLWorldView view(q);
        view.prepare(g);
        // (Enough frames for the picture made of them to settle: two cycles
        // of the jitter and the history's own half-life many times over.)
        for (int i = 0; i < 90; ++i) {
            view.render(room, W, H);
            window.swap();
        }
        Shot s;
        view.set_card_timing(true);
        for (int i = 0; i < 20; ++i) {
            view.render(room, W, H);
            window.swap();
        }
        s.scene_ms = view.card_times().scene;
        view.set_card_timing(false);
        view.render(room, W, H);
        s.px.resize(static_cast<std::size_t>(W) * H * 3);
        sg::gl::glReadPixels(0, 0, W, H, sg::gl::GL_RGB, sg::gl::GL_UNSIGNED_BYTE, s.px.data());
        return s;
    };
    // How much the picture changes from each pixel to the next: its sharpness.
    const auto detail = [&](const std::vector<unsigned char>& p) {
        double sum = 0;
        for (int y = 0; y < H; ++y)
            for (int x = 1; x < W; ++x)
                for (int c = 0; c < 3; ++c)
                    sum += std::abs(int(p[(static_cast<std::size_t>(y) * W + x) * 3 + c]) - int(p[(static_cast<std::size_t>(y) * W + x - 1) * 3 + c]));
        return sum / (static_cast<double>(W) * H * 3);
    };

    const Shot full = shot(1.0f);
    const Shot part = shot(0.75f);
    double diff = 0;
    for (std::size_t i = 0; i < full.px.size(); ++i) diff += std::abs(int(full.px[i]) - int(part.px[i]));
    diff /= static_cast<double>(full.px.size());
    const double d_full = detail(full.px), d_part = detail(part.px);
    std::printf("the screen's pixels: scene %.3f ms, detail %.2f; three quarters each way: scene %.3f ms, detail %.2f; %.2f levels apart\n",
                full.scene_ms, d_full, part.scene_ms, d_part, diff);
    check(diff < 4.0, "drawn at three quarters of the pixels each way, made whole over frames: the same picture");
    check(d_part > 0.8 * d_full, "and nearly as sharp: its edges and detail kept, not smeared");
    check(part.scene_ms < full.scene_ms, "and the scene costs the card less");
    std::printf(ok ? "all ok\n" : "FAILED\n");
    return ok ? 0 : 1;
}
