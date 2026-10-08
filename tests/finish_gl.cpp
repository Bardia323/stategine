// Stategine - what a look lays over its finished picture, against a real GL
// context.
//
// A lit box in a world that is not a room - drawn on its clear colour, with
// no sky - seen through a vignette, so the empty round it falls smoothly.
// A look that says nothing of `deband` or `smear` draws as it always did. Its
// finish builds (prepare names nothing). Debanded, the box is as it was and
// the empty round it as bright as it was. Smeared, a box moved away leaves
// its trail where it stood, and a frame with no interval shows only now;
// a look cut to (fade 0) lets the trail go at once. And with both said 0
// again the picture is the plain one, to the level.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "sg/gl/Window.hpp"
#include "sg/gl/World.hpp"
#include "sg/sg.hpp"

namespace {

bool ok = true;
void check(bool c, const char* what) {
    std::printf("[%s] %s\n", c ? "ok" : "FAIL", what);
    ok = ok && c;
}

struct Picture {
    int w = 0, h = 0;
    std::vector<unsigned char> px;
    // The mean brightness of a box of it (0..1 of the picture each way).
    double luma(float x0, float y0, float x1, float y1) const {
        double m[3] = {0, 0, 0};
        int n = 0;
        for (int y = static_cast<int>(y0 * h); y < static_cast<int>(y1 * h); ++y)
            for (int x = static_cast<int>(x0 * w); x < static_cast<int>(x1 * w); ++x, ++n)
                for (int c = 0; c < 3; ++c) m[c] += px[(static_cast<std::size_t>(y) * w + x) * 3 + c];
        n = std::max(n, 1);
        return (0.2126 * m[0] + 0.7152 * m[1] + 0.0722 * m[2]) / n;
    }
    // The most any channel of a box of it (0..1 each way) differs from another's.
    double most_apart_in(const Picture& o, float x0, float y0, float x1, float y1) const {
        double d = 0;
        for (int y = static_cast<int>(y0 * h); y < static_cast<int>(y1 * h); ++y)
            for (int x = static_cast<int>(x0 * w); x < static_cast<int>(x1 * w); ++x)
                for (int c = 0; c < 3; ++c) {
                    const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 3 + c;
                    d = std::max(d, std::fabs(double(px[i]) - double(o.px[i])));
                }
        return d;
    }
    double most_apart(const Picture& o) const {
        double d = 0;
        for (std::size_t i = 0; i < px.size(); ++i) d = std::max(d, std::fabs(double(px[i]) - double(o.px[i])));
        return d;
    }
};

}  // namespace

int main() {
    const int W = 320, H = 180;
    sg::gl::Window window(W, H, "finish");
    sg::StateGraph g;
    auto& world = g.add<sg::Spatial3D>("void");
    world.params().set("enclosed", 0.0);
    g.set_initial("void");
    sg::Element& lamp = world.light("lamp", {4.0, 4.0, 6.0});
    lamp.params.set(sg::keys::intensity, 2.0);
    sg::Element& box = world.mesh("box", 4.0, 1.8, 4.0);
    box.params.set(sg::keys::sx, 0.8).set(sg::keys::sy, 0.8).set(sg::keys::sz, 0.8)
        .set(sg::keys::r, 0.9).set(sg::keys::g, 0.9).set(sg::keys::b, 0.9);
    sg::Element& eye = world.camera();
    eye.params.set(sg::keys::x, 4.0).set(sg::keys::y, 1.8).set(sg::keys::z, 7.6).set(sg::keys::yaw, -1.5707963).set(sg::keys::pitch, 0.0);

    // Two looks alike but for how they are come to: one fades, one cuts.
    const auto look = [&](const char* id, double fade) -> sg::LookState& {
        auto& l = g.add<sg::LookState>(id);
        l.uniform(sg::passes::scene, "uAmbient", 0.4)
            .setting(sg::passes::scene, "clear.x", 0.20)
            .setting(sg::passes::scene, "clear.y", 0.24)
            .setting(sg::passes::scene, "clear.z", 0.30)
            .uniform(sg::passes::composite, "uVignette", 1.2)
            .uniform(sg::passes::composite, "uGrain", 0.0)
            .uniform(sg::passes::composite, "uBloomStrength", 0.0)
            .fade(fade);
        return l;
    };
    auto& soft = look("void.soft", 0.5);
    auto& cut = look("void.cut", 0.0);
    sg::wear(g, "void", "void.soft");
    sg::wear(g, "void", "void.cut");
    sg::set_look(world, "void.soft");

    sg::render::GLWorldView view;
    // Said before prepare, so the finish is built - and checked - with the looks.
    soft.setting(sg::passes::composite, "deband", 1.0).setting(sg::passes::composite, "smear", 0.8)
        .setting(sg::passes::composite, "smear.blur", 1.5);
    const auto problems = view.prepare(g);
    for (const auto& p : problems) std::printf("  ! %s\n", p.c_str());
    check(problems.empty(), "a look that debands and smears: its finish builds");

    const auto shot = [&] {
        view.render(world, W, H);
        Picture p{W, H, std::vector<unsigned char>(static_cast<std::size_t>(W) * H * 3)};
        sg::gl::glReadPixels(0, 0, W, H, sg::gl::GL_RGB, sg::gl::GL_UNSIGNED_BYTE, p.px.data());
        return p;
    };
    const auto settle = [&] {
        for (int i = 0; i < 40; ++i) shot();
        return shot();
    };

    // Plain: neither said.
    soft.setting(sg::passes::composite, "deband", 0.0).setting(sg::passes::composite, "smear", 0.0);
    view.set_fixed_step(1.0 / 30.0);
    const Picture plain = settle();
    const double box_plain = plain.luma(0.46, 0.45, 0.54, 0.55), empty_plain = plain.luma(0.0, 0.0, 0.15, 0.25);
    std::printf("  plain: box %.1f, the empty corner %.1f\n", box_plain, empty_plain);
    check(box_plain > empty_plain + 10.0, "the box is seen on the empty round it");

    // Like with like. Nothing in these frames moves with time or frame: the
    // world is driven by no clock (its time 0, so the dither's pattern is the
    // same every frame), grain is 0, and the interval moves only fades and
    // the smear. What differs between the plain picture and a finished one
    // is the way it goes: written straight to the screen, or composited into
    // the finish's RGBA16F picture and written from it. A half float keeps
    // 11 bits, so a value near the middle between two 8-bit levels may land
    // on the other: at most a level. So the finished picture is held to one
    // through the same way with nothing to do - smear said, but a frame with
    // no interval keeps none of the last (and the history is the same half
    // floats, so a second trip through it changes nothing).
    soft.setting(sg::passes::composite, "smear", 0.5);
    view.set_fixed_step(0.0);
    const Picture through = settle();
    soft.setting(sg::passes::composite, "smear", 0.0);
    view.set_fixed_step(1.0 / 30.0);
    const double rounding = through.most_apart(plain);
    std::printf("  through the finish with nothing to do: at most %.0f level apart\n", rounding);
    check(rounding <= 1.0, "the finish's half floats round the picture by at most a level");

    // Debanded: the box exactly as it was, the empty as bright as it was.
    soft.setting(sg::passes::composite, "deband", 1.0);
    const Picture debanded = settle();
    const double box_d = debanded.luma(0.46, 0.45, 0.54, 0.55), empty_d = debanded.luma(0.0, 0.0, 0.15, 0.25);
    std::printf("  debanded: box %.1f, the empty corner %.1f\n", box_d, empty_d);
    const double box_apart = debanded.most_apart_in(through, 0.46, 0.45, 0.54, 0.55);
    std::printf("  debanded: the box's pixels differ by at most %.0f from the same way undebanded\n", box_apart);
    check(box_apart == 0.0, "debanded: what is drawn is exactly as it was");
    check(std::fabs(empty_d - empty_plain) < 2.0, "debanded: the empty is as bright as it was");
    soft.setting(sg::passes::composite, "deband", 0.0);

    // Smeared: the box moved away leaves a trail where it stood.
    const auto moved = [&](double smear, double step) {
        soft.setting(sg::passes::composite, "smear", smear);
        box.params.set(sg::keys::x, 4.0);
        view.set_fixed_step(1.0 / 30.0);
        settle();
        box.params.set(sg::keys::x, 6.0);
        view.set_fixed_step(step);
        const Picture p = shot();
        view.set_fixed_step(1.0 / 30.0);
        return p;
    };
    const double trail0 = moved(0.0, 1.0 / 30.0).luma(0.46, 0.45, 0.54, 0.55);
    const double trail1 = moved(0.8, 1.0 / 30.0).luma(0.46, 0.45, 0.54, 0.55);
    const double still = moved(0.8, 0.0).luma(0.46, 0.45, 0.54, 0.55);
    std::printf("  where the box stood: %.1f plain, %.1f smeared, %.1f with no interval\n", trail0, trail1, still);
    check(trail1 > trail0 + 5.0, "smeared: a box moved away leaves its trail");
    check(std::fabs(still - trail0) < 1.0, "a frame with no interval shows only now");

    // A look cut to lets the trail go at once.
    cut.setting(sg::passes::composite, "smear", 0.8);
    soft.setting(sg::passes::composite, "smear", 0.8);
    box.params.set(sg::keys::x, 4.0);
    settle();
    box.params.set(sg::keys::x, 6.0);
    sg::set_look(world, "void.cut");
    const double after_cut = shot().luma(0.46, 0.45, 0.54, 0.55);
    std::printf("  where the box stood, the frame a look cut to: %.1f\n", after_cut);
    check(std::fabs(after_cut - trail0) < 1.0, "cut to: the trail is let go");
    sg::set_look(world, "void.soft");
    cut.setting(sg::passes::composite, "smear", 0.0);

    // Both said 0 again: the plain picture, to the level.
    soft.setting(sg::passes::composite, "smear", 0.0).setting(sg::passes::composite, "deband", 0.0);
    box.params.set(sg::keys::x, 4.0);
    const Picture back = settle();
    check(back.most_apart(plain) <= 1.0, "neither said: the picture as it was");
    return ok ? 0 : 1;
}
