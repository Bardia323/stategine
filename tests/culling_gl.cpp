// Stategine - what is not seen is not drawn, against a real GL context.
//
// A lamp shines one way. A shadow map holds what the lamp sees: a thing moved
// where it sees does the map again, a thing moved behind it does not - the
// map stands, and costs nothing. And a speck, less than a pixel across at its
// distance, is not drawn, where the same thing near is.
#include <cstdio>

#include "sg/gl/Window.hpp"
#include "sg/gl/World.hpp"
#include "sg/sg.hpp"

int main() {
    const int W = 320, H = 180;
    sg::gl::Window window(W, H, "culling");
    sg::StateGraph g;
    auto& room = g.add<sg::Spatial3D>("room");
    g.set_initial("room");

    // A lamp at one end of a long room, shining along it.
    sg::Element& lamp = room.light("lamp", {3.0, 2.0, 8.0});
    lamp.params.set("dx", 0.0).set("dy", -0.2).set("dz", -1.0).set("inner", 0.3).set("outer", 0.45).set(sg::keys::intensity, 1.0);
    // One thing in the light's way and one behind the lamp.
    sg::Element& before = room.fixture("before", 3.0, 0.0, 4.0);
    before.params.set(sg::keys::sx, 0.5).set(sg::keys::sy, 0.5).set(sg::keys::sz, 0.5);
    sg::Element& behind = room.fixture("behind", 3.0, 0.0, 11.0);
    behind.params.set(sg::keys::sx, 0.5).set(sg::keys::sy, 0.5).set(sg::keys::sz, 0.5);
    // A speck: a centimetre across, far from where we look from. And the same near.
    sg::Element& speck = room.fixture("speck", 3.0, 1.0, -60.0);
    speck.params.set(sg::keys::sx, 0.01).set(sg::keys::sy, 0.01).set(sg::keys::sz, 0.01).set("cast", 0.0);
    sg::Element& eye = room.camera();
    eye.params.set(sg::keys::x, 3.0).set(sg::keys::y, 1.0).set(sg::keys::z, 9.0).set(sg::keys::yaw, -1.5707963).set(sg::keys::pitch, 0.0);

    sg::render::GLWorldView view;
    view.prepare(g);
    const auto frame = [&] {
        view.render(room, W, H);
        return view.times();
    };
    for (int i = 0; i < 3; ++i) frame();  // (settled)

    bool ok = true;
    const auto check = [&](bool c, const char* what) {
        std::printf("[%s] %s\n", c ? "ok" : "FAIL", what);
        ok = ok && c;
    };
    check(frame().shadow_maps == 0, "nothing moving: no shadow map is drawn again");
    behind.params.set(sg::keys::x, 3.4);
    const auto behind_moved = frame();
    check(behind_moved.shadow_maps == 0, "a thing moved behind the lamp, where it sees nothing: its map stands");
    before.params.set(sg::keys::x, 3.4);
    const auto before_moved = frame();
    check(before_moved.shadow_maps > 0, "a thing moved in the light's way: its map is drawn again");
    check(before_moved.shadow_casters > 0 && before_moved.shadow_casters <= 3 * before_moved.shadow_maps,
          "with the things it holds, not all of them");

    // The speck, looked at from far off, then from near.
    const int far_draws = frame().draws + frame().instanced;
    room.element("speck").params.set(sg::keys::z, 8.5);
    const int near_draws = frame().draws + frame().instanced;
    std::printf("things drawn: speck far %d, near %d\n", far_draws, near_draws);
    check(near_draws > far_draws, "a speck too small to be seen is not drawn, and is when it is near");
    room.params().set("lod_px", 0.0);
    room.element("speck").params.set(sg::keys::z, -60.0);
    const int all_draws = frame().draws + frame().instanced;
    check(all_draws >= near_draws, "and the room can ask for everything to be drawn");
    return ok ? 0 : 1;
}
