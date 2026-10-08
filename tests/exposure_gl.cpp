// Stategine - the eye's exposure, against a real GL context.
//
// A look that says nothing of exposure is drawn as it was, whatever the
// light. A look that says `exposure.auto` has the eye adjust: a room lit
// sixteen times as brightly is seen about as bright, where with a fixed
// exposure it is blown out. The eye eases there - never more than its rate a
// frame - and settles at once on a first frame or when asked: the same
// picture however it was come to.
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
    double luma() const {
        double s = 0;
        for (std::size_t i = 0; i + 2 < px.size(); i += 3) s += 0.2126 * px[i] + 0.7152 * px[i + 1] + 0.0722 * px[i + 2];
        return s / static_cast<double>(px.size() / 3);
    }
    double differs(const Picture& o) const {
        double most = 0;
        for (std::size_t i = 0; i < px.size(); ++i) most = std::max(most, std::abs(double(px[i]) - double(o.px[i])));
        return most;
    }
};

}  // namespace

int main() {
    const int W = 240, H = 136;
    sg::gl::Window window(W, H, "exposure");
    sg::StateGraph g;
    auto& room = g.add<sg::Spatial3D>("room");
    room.params().set("room_w", 6.0).set("room_d", 6.0).set("room_h", 3.0);
    g.set_initial("room");
    sg::Element& lamp = room.light("lamp", {3.0, 2.6, 3.0});
    lamp.params.set(sg::keys::intensity, 0.6);
    // (nothing in the way: every pixel the lamp lights, so the light seen is the lamp's)
    sg::Element& eye = room.camera();
    eye.params.set(sg::keys::x, 3.0).set(sg::keys::y, 1.6).set(sg::keys::z, 5.5).set(sg::keys::yaw, -1.5707963).set(sg::keys::pitch, -0.5);

    auto& fixed = g.add<sg::LookState>("room.fixed");
    fixed.uniform(sg::passes::scene, "uAmbient", 0.0).uniform(sg::passes::composite, "uGrain", 0.0).uniform(sg::passes::composite, "uVignette", 0.0).fade(0.0);
    auto& off = g.add<sg::LookState>("room.off");
    off.uniform(sg::passes::scene, "uAmbient", 0.0).uniform(sg::passes::composite, "uGrain", 0.0).uniform(sg::passes::composite, "uVignette", 0.0).fade(0.0);
    off.setting(sg::passes::scene, "exposure.auto", 0.0);
    auto& adjusts = g.add<sg::LookState>("room.adjusts");
    adjusts.uniform(sg::passes::scene, "uAmbient", 0.0).uniform(sg::passes::composite, "uGrain", 0.0).uniform(sg::passes::composite, "uVignette", 0.0).fade(0.0);
    adjusts.setting(sg::passes::scene, "exposure.auto", 1.0).setting(sg::passes::scene, "exposure.rate", 2.0);
    sg::wear(g, "room", "room.fixed");
    sg::wear(g, "room", "room.off");
    sg::wear(g, "room", "room.adjusts");

    sg::render::GLWorldView view;
    const auto problems = view.prepare(g);
    for (const auto& p : problems) std::printf("  ! %s\n", p.c_str());
    check(problems.empty(), "the looks build");
    view.set_fixed_step(1.0 / 60.0);
    const auto shot = [&](sg::render::GLWorldView& v) {
        v.render(room, W, H);
        Picture p{W, H, std::vector<unsigned char>(static_cast<std::size_t>(W) * H * 3)};
        sg::gl::glReadPixels(0, 0, W, H, sg::gl::GL_RGB, sg::gl::GL_UNSIGNED_BYTE, p.px.data());
        return p;
    };
    const auto settled = [&](sg::render::GLWorldView& v) {
        Picture p;
        for (int i = 0; i < 4; ++i) p = shot(v);
        return p;
    };

    // Saying nothing, and saying it is off, are the same picture; the eye
    // does not adjust.
    sg::set_look(room, "room.fixed");
    const Picture plain = settled(view);
    sg::set_look(room, "room.off");
    const Picture said_off = settled(view);
    check(plain.differs(said_off) == 0.0 && view.exposure_stops() == 0.0, "a look that says nothing of exposure is drawn as before");

    // Fixed: a room lit sixteen times as brightly is much brighter.
    sg::set_look(room, "room.fixed");
    lamp.params.set(sg::keys::intensity, 0.6 * 16.0);
    const Picture fixed_bright = settled(view);
    lamp.params.set(sg::keys::intensity, 0.6);
    std::printf("  fixed: dim %.1f, bright %.1f\n", plain.luma(), fixed_bright.luma());

    // Adjusting: the eye opens in the dim room and closes in the bright, and
    // the two are seen about alike.
    sg::set_look(room, "room.adjusts");
    const Picture first = shot(view);
    const double dim_stops = view.exposure_stops();
    const Picture dim = settled(view);
    check(first.differs(dim) <= 1.0, "the first frame the eye adjusts is already settled");
    lamp.params.set(sg::keys::intensity, 0.6 * 16.0);
    view.settle_exposure();
    const Picture bright = settled(view);
    const double bright_stops = view.exposure_stops();
    std::printf("  adjusting: dim %.1f (%.2f stops), bright %.1f (%.2f stops)\n", dim.luma(), dim_stops, bright.luma(), bright_stops);
    check(std::abs(dim_stops - bright_stops - 4.0) < 1.0, "sixteen times the light: the eye closes by about four stops");
    check(std::abs(dim.luma() - bright.luma()) < 0.35 * std::abs(plain.luma() - fixed_bright.luma()),
          "and the dim room and the bright one are seen about alike");

    // Eased: the light goes down at once; the eye opens no faster than its
    // rate a frame, and comes to where it settles.
    lamp.params.set(sg::keys::intensity, 0.6);
    double was = view.exposure_stops(), most = 0.0;
    Picture last;
    for (int i = 0; i < 240; ++i) {
        last = shot(view);
        most = std::max(most, std::abs(view.exposure_stops() - was));
        was = view.exposure_stops();
    }
    check(most <= 2.0 / 60.0 + 1e-6 && most > 0.0, "the eye eases, at its rate, never a jump");
    check(std::abs(was - dim_stops) < 0.05, "and comes to where it settles");

    // Settled: the same picture as a view that has only ever seen it.
    sg::render::GLWorldView fresh;
    fresh.prepare(g);
    fresh.set_fixed_step(1.0 / 60.0);
    lamp.params.set(sg::keys::intensity, 0.6 * 16.0);
    for (int i = 0; i < 3; ++i) shot(view);
    view.settle_exposure();
    const Picture asked = shot(view);
    const Picture once = shot(fresh);
    check(asked.differs(once) <= 1.0, "settled, a shot is the same picture however it was come to");
    return ok ? 0 : 1;
}
