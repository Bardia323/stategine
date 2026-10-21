// Stategine - how each pixel of the eye's view moved since the last frame
// (GLWorldView::set_motion: DLSS's motion vectors), against a real GL context.
//
// A bright post before a dark wall, and a second, still, beside it. Nothing
// moving and the eye still, every pixel says it has not moved - with the
// frames' jitter on as well (the jitter is left out). The post going
// sideways a few centimetres a frame says it went as far as it is seen to go
// in the picture, and about as far as the lens says it should; the wall
// behind it, and the still post, nothing. The eye turning, nothing moving,
// every pixel says how far the turn took it: as far as the picture moved,
// and about as far as the lens says - the still post as the wall.
#include <algorithm>
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

const int W = 640, H = 360;

// The motion as the view keeps it: two numbers a pixel, its lowest row first.
struct Motion {
    std::vector<float> v;
    int w = 0, h = 0;
    float x(int px, int py) const { return v[(static_cast<std::size_t>(py) * w + px) * 4]; }
    float y(int px, int py) const { return v[(static_cast<std::size_t>(py) * w + px) * 4 + 1]; }
    // The most any pixel says it moved.
    double most() const {
        double m = 0;
        for (int j = 0; j < h; ++j)
            for (int i = 0; i < w; ++i) m = std::max(m, std::hypot(double(x(i, j)), double(y(i, j))));
        return m;
    }
    // The middle (median) of what the pixels in a box say, across.
    double across(int x0, int x1, int y0, int y1) const {
        std::vector<float> a;
        for (int j = y0; j < y1; ++j)
            for (int i = x0; i < x1; ++i) a.push_back(x(i, j));
        std::nth_element(a.begin(), a.begin() + a.size() / 2, a.end());
        return a[a.size() / 2];
    }
};
Motion read_motion(const sg::render::GLWorldView& view) {
    Motion m;
    const sg::gl::RenderTarget& t = view.motion();
    if (!t.valid()) return m;
    m.w = t.width(), m.h = t.height();
    m.v.resize(static_cast<std::size_t>(m.w) * m.h * 4);
    t.bind();
    sg::gl::glReadPixels(0, 0, m.w, m.h, sg::gl::GL_RGBA, sg::gl::GL_FLOAT, m.v.data());
    sg::gl::glBindFramebuffer(sg::gl::GL_FRAMEBUFFER, 0);
    return m;
}

// Where the bright post is across the middle rows of the picture (the mean
// x of its pixels), on the screen.
double post_at(int lo = 0, int hi = W) {
    std::vector<unsigned char> px(static_cast<std::size_t>(W) * H * 3);
    sg::gl::glReadPixels(0, 0, W, H, sg::gl::GL_RGB, sg::gl::GL_UNSIGNED_BYTE, px.data());
    if (const char* keep = std::getenv("SG_MOTION_SHOTS")) {
        static int k = 0;
        const std::string path = std::string(keep) + "/m" + std::to_string(k++) + ".ppm";
        if (FILE* f = std::fopen(path.c_str(), "wb")) {
            std::fprintf(f, "P6 %d %d 255\n", W, H);
            for (int j = H - 1; j >= 0; --j) std::fwrite(&px[static_cast<std::size_t>(j) * W * 3], 1, static_cast<std::size_t>(W) * 3, f);
            std::fclose(f);
        }
    }
    double sum = 0, n = 0;
    for (int y = H / 2 - 20; y < H / 2 + 20; ++y)
        for (int x = lo; x < hi; ++x) {
            const unsigned char* p = &px[(static_cast<std::size_t>(y) * W + x) * 3];
            const double w = p[0] > 128 ? 1.0 : 0.0;
            sum += w * x, n += w;
        }
    return n > 0 ? sum / n : -1;
}

}  // namespace

int main() {
    sg::gl::Window window(W, H, "motion");
    sg::StateGraph g;
    auto& field = g.add<sg::Spatial3D>("field");
    g.set_initial("field");
    field.params().set("far", 60.0).set("enclosed", 0.0);
    const auto box = [&](sg::Element& e, double sx, double sy, double sz, double grey) -> sg::Element& {
        e.params.set(sg::keys::sx, sx).set(sg::keys::sy, sy).set(sg::keys::sz, sz).set("r", grey).set("g", grey).set("b", grey)
            .set("roughness", 1.0).set("surface", 0.0);
        return e;
    };
    box(field.fixture("ground", 10.0, -0.5, 0.0), 40.0, 1.0, 40.0, 0.05);
    box(field.fixture("wall", 10.0, 0.0, 0.0), 0.5, 30.0, 40.0, 0.05);
    // Looked at from x = 0 along +x: the post that moves 5 m off, the still
    // one beside it (dimmer: not counted as the bright one).
    const double d = 5.0;
    sg::Element& post = box(field.mesh("post", d, 0.0, 0.0), 0.2, 3.0, 0.2, 0.9);
    post.params.set("emissive", 2.0);
    box(field.fixture("still", d, 0.0, -1.0), 0.2, 3.0, 0.2, 0.3).params.set("emissive", 0.3);
    auto& look = g.add<sg::LookState>("look");
    look.uniform(sg::passes::scene, "uFogDensity", 0.0).uniform(sg::passes::scene, "uAmbient", 0.3);
    look.uniform(sg::passes::composite, "uGrain", 0.0).uniform(sg::passes::composite, "uVignette", 0.0).uniform(sg::passes::composite, "uBloomStrength", 0.0);
    look.fade(0.0);
    sg::wear(g, "field", "look");
    sg::Element& eye = field.camera();
    const double fov = 40.0;
    eye.params.set(sg::keys::x, 0.0).set(sg::keys::y, 1.0).set(sg::keys::z, 0.0).set(sg::keys::yaw, 0.0).set(sg::keys::pitch, 0.0).set(sg::keys::fov, fov);
    // (A lens's pixels: how far a pixel is, at a metre off, across the view.)
    const double focal = 0.5 * H / std::tan(fov * M_PI / 360.0);

    sg::render::GLQuality q;
    q.msaa = 0;
    sg::render::GLWorldView view(q);
    for (const auto& p : view.prepare(g)) std::printf("  ! %s\n", p.c_str());
    view.set_frame_delta(1.0 / 30.0);
    // (Each frame shown as the next is begun: what is read after `frames`
    // is the last one drawn, not yet swapped away.)
    bool drawn = false;
    const auto frames = [&](int n) {
        for (int i = 0; i < n; ++i) {
            if (drawn) window.swap();
            view.render(field, W, H);
            drawn = true;
        }
    };

    // Nothing asked: nothing made.
    frames(2);
    check(!view.motion().valid(), "no motion kept unless it is asked for");
    view.set_motion(true);
    frames(4);
    const Motion still = read_motion(view);
    std::printf("  still: the most any pixel says it moved %.4f px\n", still.most());
    check(still.w == W && still.h == H && still.most() < 0.01, "nothing moving, the eye still: no pixel moved");
    // And with the frames' jitter: left out of what is said.
    q.taa = true;
    view.set_quality(q);
    frames(5);
    const Motion jittered = read_motion(view);
    std::printf("  still, jittered: the most any pixel says it moved %.4f px\n", jittered.most());
    check(jittered.most() < 0.05, "the jitter is no motion");

    // (Measured without the frames' smoothing: the picture where it is now.)
    q.taa = false;
    view.set_quality(q);
    // The post going sideways, 3 cm a frame.
    const double step = 0.03;
    frames(2);
    const double before = post_at(W / 2 - 80, W / 2 + 80);
    post.params.set(sg::keys::z, post.params.num(sg::keys::z) + step);
    frames(1);
    const double after = post_at(W / 2 - 80, W / 2 + 80);
    const Motion moving = read_motion(view);
    const int px = static_cast<int>(std::lround(after));
    const double said = moving.across(px - 2, px + 3, H / 2 - 20, H / 2 + 20);
    const double lens = step * focal / (d - 0.1);  // (its face, a tenth nearer)
    std::printf("  the post: moved %.2f px in the picture, says %.2f px (the lens: %.2f); the wall beside it says %.3f, the still post %.3f\n",
                after - before, said, lens, moving.across(40, 60, H / 2 - 20, H / 2 + 20),
                moving.across(static_cast<int>(W / 2 - focal * 1.0 / d) - 2, static_cast<int>(W / 2 - focal * 1.0 / d) + 2, H / 2 - 20, H / 2 + 20));
    check(std::abs(said - (after - before)) < 0.35 && std::abs(std::abs(said) - lens) < 0.08 * lens,
          "a thing going sideways: its pixels say how far it went in the picture");
    check(std::abs(moving.across(40, 60, H / 2 - 20, H / 2 + 20)) < 0.01, "the wall behind it says it did not move");
    // (The post that stands still, a metre to its side: its middle, on the screen.)
    const int still_x = static_cast<int>(std::lround(W / 2.0 + focal * 1.0 / (d - 0.1) * (said > 0 ? -1.0 : 1.0)));
    check(std::abs(moving.across(still_x - 2, still_x + 3, H / 2 - 20, H / 2 + 20)) < 0.01, "and the still post beside it neither");
    // Standing again: it says it is still.
    frames(2);
    check(read_motion(view).most() < 0.05, "stopped: nothing moved");

    // The eye turning, half a degree a frame.
    const double turn = 0.5 * M_PI / 180.0;
    frames(1);
    const double was_at = post_at();
    eye.params.set(sg::keys::yaw, eye.params.num(sg::keys::yaw) + turn);
    frames(1);
    const double now_at = post_at();
    const Motion panned = read_motion(view);
    const int at = static_cast<int>(std::lround(now_at));
    const double post_said = panned.across(at - 2, at + 3, H / 2 - 20, H / 2 + 20);
    const double wall_said = panned.across(W / 2 + 100, W / 2 + 120, H / 2 - 20, H / 2 + 20);
    const double pan = focal * std::tan(turn);
    std::printf("  turned: the picture moved %.2f px, the post says %.2f, the wall %.2f (the lens: %.2f at the middle)\n", now_at - was_at, post_said,
                wall_said, pan);
    check(std::abs(post_said - (now_at - was_at)) < 0.35 && std::abs(std::abs(post_said) - pan) < 0.08 * pan,
          "the eye turning: each pixel says how far the turn took it");
    check(std::abs(wall_said - post_said) < 0.1 * pan + 0.1, "near or far alike (a turn moves all the picture as one)");
    std::printf(ok ? "all ok\n" : "FAILED\n");
    return ok ? 0 : 1;
}
