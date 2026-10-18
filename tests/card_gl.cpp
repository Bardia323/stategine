// Stategine - the card's own clock, against a real GL context.
//
// A frame's stages, as the card says it spent its time on them: known a few
// frames after they were drawn, never waited for; none known while the clock
// is off, and none after it is turned off again.
#include <cstdio>

#include "sg/gl/Window.hpp"
#include "sg/gl/World.hpp"
#include "sg/sg.hpp"

int main() {
    const int W = 320, H = 180;
    sg::gl::Window window(W, H, "card");
    sg::StateGraph g;
    auto& room = g.add<sg::Spatial3D>("room");
    g.set_initial("room");
    sg::Element& lamp = room.light("lamp", {2.0, 2.5, 2.0});
    lamp.params.set("dx", 0.0).set("dy", -1.0).set("dz", 0.0).set("inner", 0.6).set("outer", 0.9).set(sg::keys::intensity, 1.0);
    sg::Element& crate = room.fixture("crate", 2.0, 0.0, 2.0);
    crate.params.set(sg::keys::sx, 0.6).set(sg::keys::sy, 0.6).set(sg::keys::sz, 0.6);
    sg::Element& eye = room.camera();
    eye.params.set(sg::keys::x, 2.0).set(sg::keys::y, 1.4).set(sg::keys::z, 5.0).set(sg::keys::yaw, -1.5707963);

    sg::render::GLWorldView view;
    view.prepare(g);
    bool ok = true;
    const auto check = [&](bool c, const char* what) {
        std::printf("[%s] %s\n", c ? "ok" : "FAIL", what);
        ok = ok && c;
    };

    for (int i = 0; i < 4; ++i) view.render(room, W, H);
    check(view.card_times().frame == 0, "the clock off: nothing is known of the card");

    view.set_card_timing(true);
    // The crate moving under the lamp, so its map is drawn again each frame:
    // shadows cost the card something.
    bool known = false;
    for (int i = 0; i < 60 && !known; ++i) {
        crate.params.set(sg::keys::x, 2.0 + 0.02 * i);
        view.render(room, W, H);
        window.swap();
        known = view.card_times().frame != 0;
    }
    check(known, "the clock on: a frame's times come back within a few frames");
    const auto t = view.card_times();
    std::printf("card: relit %.3f feeds %.3f views %.3f scene %.3f (shadows %.3f) post %.3f total %.3f ms, of frame %llu\n", t.relit,
                t.feeds, t.views, t.scene, t.shadows, t.post, t.total, static_cast<unsigned long long>(t.frame));
    check(t.total > 0.0 && t.scene > 0.0 && t.post > 0.0, "the scene and what follows it took the card some time");
    check(t.shadows >= 0.0 && t.shadows <= t.scene + 1e-6 && t.air >= 0.0 && t.lit > 0.0 && t.mirrors + t.shadows + t.air + t.lit <= t.scene + 1e-3,
          "the eye's mirrors, shadow maps, air and lit scene are parts of its scene");
    const double parts = t.relit + t.feeds + t.views + t.scene + t.post;
    check(parts <= t.total + 1e-3 && parts >= 0.5 * t.total, "the stages add up to the frame");

    view.set_card_timing(false);
    view.render(room, W, H);
    check(view.card_times().frame == 0, "turned off again: nothing is said to be known");

    std::printf("%s\n", ok ? "card: ok" : "card: FAILED");
    return ok ? 0 : 1;
}
