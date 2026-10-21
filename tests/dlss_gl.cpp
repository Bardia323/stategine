// Stategine - the card's own upscaler (Quality::upscaler: NVIDIA's DLSS on
// the Vulkan backend), against a real window.
//
// A room of boxes of many sizes and colours under a lamp, the eye still. The
// engine's TAA at the screen's pixels is the picture to match. Asked for
// DLSS where it can run (Vulkan, an NVIDIA card, DLSS's file found), the
// picture is the screen's size, lit, the same picture as the TAA's, about as
// sharp, and still from frame to frame once settled - drawn at two thirds of
// the pixels each way, and at all of them (DLAA). Where it cannot (drawn by
// OpenGL: SG_GPU=gl, or no NVIDIA card), the view says why, once, and the
// eye is smoothed by the TAA instead - the same picture, never a failure.
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
#ifdef _WIN32
    _putenv("SG_NO_VSYNC=1");
#else
    setenv("SG_NO_VSYNC", "1", 1);
#endif
#if defined(SG_NATIVE_GPU)
    // (Vulkan, where nothing says otherwise: DLSS is its backend's.)
    sg::gl::use_api(sg::gl::Api::vulkan);
#endif
    sg::gl::Window window(W, H, "dlss");
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
    const char* keep = std::getenv("SG_DLSS_SHOTS");

    struct Shot {
        std::vector<unsigned char> px;
        double flicker = 0;  // the mean change from one frame to the next, settled, in levels
        double scene_ms = 0, post_ms = 0;
        bool by_card = false;
        std::string said;
    };
    const auto read = [&] {
        std::vector<unsigned char> px(static_cast<std::size_t>(W) * H * 3);
        sg::gl::glReadPixels(0, 0, W, H, sg::gl::GL_RGB, sg::gl::GL_UNSIGNED_BYTE, px.data());
        return px;
    };
    const auto shot = [&](const char* name, int upscaler, int mode) {
        sg::render::GLQuality q;
        q.taa = true;
        q.msaa = 0;
        q.upscaler = upscaler;
        q.dlss_mode = mode;
        sg::render::GLWorldView view(q);
        view.prepare(g);
        view.set_frame_delta(1.0 / 60.0);
        // (Settled: two cycles of the jitter and the history's half-life many times over.)
        for (int i = 0; i < 120; ++i) {
            view.render(room, W, H);
            window.swap();
        }
        Shot s;
        view.set_card_timing(true);
        for (int i = 0; i < 20; ++i) {
            view.render(room, W, H);
            window.swap();
        }
        s.scene_ms = view.card_times().scene, s.post_ms = view.card_times().post;
        view.set_card_timing(false);
        // (Each frame read before it is shown: the picture just drawn.)
        std::vector<unsigned char> last;
        for (int i = 0; i < 12; ++i) {
            view.render(room, W, H);
            std::vector<unsigned char> now = read();
            if (!last.empty()) {
                double d = 0;
                for (std::size_t k = 0; k < now.size(); ++k) d += std::abs(int(now[k]) - int(last[k]));
                s.flicker += d / static_cast<double>(now.size()) / 11.0;
            }
            last = std::move(now);
            window.swap();
        }
        s.px = std::move(last);
        s.by_card = view.upscaled_by_card();
        s.said = view.upscaler_said();
        if (keep) {
            const std::string path = std::string(keep) + "/" + name + ".ppm";
            if (FILE* f = std::fopen(path.c_str(), "wb")) {
                std::fprintf(f, "P6 %d %d 255\n", W, H);
                for (int j = H - 1; j >= 0; --j) std::fwrite(&s.px[static_cast<std::size_t>(j) * W * 3], 1, static_cast<std::size_t>(W) * 3, f);
                std::fclose(f);
            }
        }
        return s;
    };
    const auto detail = [&](const std::vector<unsigned char>& p) {
        double sum = 0;
        for (int y = 0; y < H; ++y)
            for (int x = 1; x < W; ++x)
                for (int c = 0; c < 3; ++c)
                    sum += std::abs(int(p[(static_cast<std::size_t>(y) * W + x) * 3 + c]) - int(p[(static_cast<std::size_t>(y) * W + x - 1) * 3 + c]));
        return sum / (static_cast<double>(W) * H * 3);
    };
    const auto mean = [](const std::vector<unsigned char>& p) {
        double s = 0;
        for (unsigned char c : p) s += c;
        return s / static_cast<double>(p.size());
    };
    const auto apart = [](const std::vector<unsigned char>& a, const std::vector<unsigned char>& b) {
        double d = 0;
        for (std::size_t i = 0; i < a.size(); ++i) d += std::abs(int(a[i]) - int(b[i]));
        return d / static_cast<double>(a.size());
    };

    using Q = sg::render::Quality;
    const Shot taa = shot("taa", Q::upscale_taa, Q::dlss_quality);
    const Shot dlss = shot("dlss_quality", Q::upscale_dlss, Q::dlss_quality);
    std::printf("TAA: scene %.3f ms, post %.3f ms, detail %.2f, flicker %.3f\n", taa.scene_ms, taa.post_ms, detail(taa.px), taa.flicker);
    if (!dlss.by_card) {
        std::printf("DLSS not here (%s): the TAA instead\n", dlss.said.c_str());
        check(!dlss.said.empty(), "DLSS asked for where it cannot be: the view says why");
        check(apart(dlss.px, taa.px) < 3.0, "and the eye is smoothed by the TAA in its place: the same picture");
        std::printf(ok ? "all ok (DLSS itself not tried here)\n" : "FAILED\n");
        return ok ? 0 : 1;
    }
    const Shot dlaa = shot("dlaa", Q::upscale_dlaa, Q::dlss_quality);
    // Moving: the eye turning a little each frame. The frame drawn as it
    // turns against the picture settled where it stopped - how far behind
    // what is made of the frames before leaves the picture (it is carried by
    // how each pixel moved: DLSS by the motion the view keeps, the TAA by
    // its own reprojection).
    const auto lag = [&](int upscaler) {
        sg::render::GLQuality q;
        q.taa = true, q.msaa = 0, q.upscaler = upscaler;
        sg::render::GLWorldView view(q);
        view.prepare(g);
        view.set_frame_delta(1.0 / 60.0);
        const double yaw = eye.params.num(sg::keys::yaw);
        for (int i = 0; i < 60; ++i) {
            view.render(room, W, H);
            window.swap();
        }
        for (int i = 0; i < 40; ++i) {
            eye.params.set(sg::keys::yaw, yaw + 0.002 * (i + 1));
            view.render(room, W, H);
            window.swap();
        }
        view.render(room, W, H);  // (the same place: the first frame of standing)
        const std::vector<unsigned char> moving = read();
        window.swap();
        sg::render::GLWorldView fresh(q);
        fresh.prepare(g);
        for (int i = 0; i < 120; ++i) {
            fresh.render(room, W, H);
            window.swap();
        }
        fresh.render(room, W, H);
        const std::vector<unsigned char> settled = read();
        window.swap();
        eye.params.set(sg::keys::yaw, yaw);
        return apart(moving, settled);
    };
    const double taa_lag = lag(Q::upscale_taa), dlss_lag = lag(Q::upscale_dlss);
    std::printf("turning: %.2f levels from the settled picture with the TAA, %.2f with DLSS\n", taa_lag, dlss_lag);
    check(dlss_lag < 1.25 * taa_lag + 0.2, "turning, DLSS keeps up with the picture as the TAA does (each pixel's motion carries it)");
    for (const auto& [name, s] : {std::pair{"DLSS quality", &dlss}, std::pair{"DLAA", &dlaa}}) {
        std::printf("%s: scene %.3f ms, post %.3f ms, detail %.2f, flicker %.3f, %.2f levels from the TAA's, mean %.1f\n", name, s->scene_ms, s->post_ms,
                    detail(s->px), s->flicker, apart(s->px, taa.px), mean(s->px));
        check(s->by_card, std::string(name) + ": made by the card");
        check(mean(s->px) > 0.6 * mean(taa.px), std::string(name) + ": the screen's picture, lit (not black)");
        check(apart(s->px, taa.px) < 6.0, std::string(name) + ": the same picture as the TAA's");
        check(detail(s->px) > 0.8 * detail(taa.px), std::string(name) + ": about as sharp");
        check(s->flicker < 0.5, std::string(name) + ": still from frame to frame, settled");
    }
    check(dlss.scene_ms < dlaa.scene_ms, "DLSS at two thirds of the pixels: the scene costs the card less than at all of them");
    std::printf(ok ? "all ok\n" : "FAILED\n");
    return ok ? 0 : 1;
}
