// Stategine - light through a doorway, against a real GL context.
//
// Two rooms glued at a doorway: the near one dark, the far one with a lamp
// by its side of the opening, shining out through it. The near room's floor
// in front of the doorway is lit by that lamp - and not when the doorway
// says `light` = 0, and not when something stands shut in the opening.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <tuple>
#include <vector>

#include "sg/gl/Window.hpp"
#include "sg/render/GLWorld.hpp"
#include "sg/sg.hpp"

namespace {

// How bright the picture is over the floor in front of the doorway, between
// `x0` and `x1` of the width.
double floor_brightness(int w, int h, double x0 = 0.3, double x1 = 0.7) {
    std::vector<unsigned char> px(static_cast<std::size_t>(w) * h * 3);
    sg::gl::glReadPixels(0, 0, w, h, sg::gl::GL_RGB, sg::gl::GL_UNSIGNED_BYTE, px.data());
    double sum = 0;
    int n = 0;
    for (int y = h / 10; y < h * 4 / 10; ++y)
        for (int x = static_cast<int>(w * x0); x < static_cast<int>(w * x1); ++x, ++n) {
            const unsigned char* p = &px[(static_cast<std::size_t>(y) * w + x) * 3];
            sum += 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
        }
    return sum / n;
}

}  // namespace

int main() {
    const int W = 320, H = 180;
    sg::gl::Window window(W, H, "doorway light");
    sg::StateGraph g;
    auto& near = g.add<sg::Spatial3D>("near");
    auto& far = g.add<sg::Spatial3D>("far");
    g.set_initial("near");

    // The near room is dark but for what comes through its doorway.
    auto& dark = g.add<sg::LookState>("dark");
    dark.uniform(sg::passes::scene, "uAmbient", 0.0);
    sg::wear(g, "near", "dark");

    // Each doorway faces into its own room: the near one on the north wall,
    // the far one on the south wall of its room.
    near.portal("pa", {7.0, 1.0, 0.3}, 1.0, 2.0, 1.5707963);
    far.portal("pb", {7.0, 1.0, 11.7}, 1.0, 2.0, -1.5707963);
    // A lamp by the far side of the opening, shining out through it.
    sg::Element& lamp = far.light("lamp", {7.0, 1.8, 10.9});
    lamp.params.set("dx", 0.0).set("dy", -0.35).set("dz", 1.0).set("inner", 0.9).set("outer", 1.4).set(sg::keys::intensity, 0.4);
    // A slab the size of the opening, hung off a hinge that stands in it or
    // is put away - as a door's leaf hangs: its own place is the hinge's.
    near.anchor("hinge", {7.0, 0.0, 30.0});
    sg::Element& slab = near.fixture("slab", 0.0, -0.05, 0.0);
    slab.params.set(sg::keys::sx, 1.1).set(sg::keys::sy, 2.1).set(sg::keys::sz, 0.05);
    sg::attach_to(slab, "hinge");
    // A door as doors are made: a leaf on a hinge at one side of the
    // opening, with panels standing proud of it on both faces (they cover
    // what the leaf covers - no more).
    near.anchor("door_hinge", {6.5, 0.0, 30.0});
    for (const auto& [id, y, z, sy, sz] : {std::tuple{"leaf", -0.05, 0.0, 2.1, 0.04}, std::tuple{"panel_a", 0.1, 0.03, 0.8, 0.02},
                                          std::tuple{"panel_b", 1.0, 0.03, 0.8, 0.02}, std::tuple{"panel_c", 0.1, -0.03, 0.8, 0.02},
                                          std::tuple{"panel_d", 1.0, -0.03, 0.8, 0.02}}) {
        sg::Element& p = near.fixture(id, 0.5, y, z);
        p.params.set(sg::keys::sx, id == std::string("leaf") ? 1.0 : 0.8).set(sg::keys::sy, sy).set(sg::keys::sz, sz);
        sg::attach_to(p, "door_hinge");
    }

    sg::Element& eye = near.camera();
    eye.params.set(sg::keys::x, 7.0).set(sg::keys::y, 1.6).set(sg::keys::z, 3.5).set(sg::keys::yaw, -1.5707963).set(sg::keys::pitch, -0.55);

    sg::render::GLWorldView view;
    view.prepare(g);
    view.bind_world("pa", &far, sg::portal_carry(near.element("pa"), far.element("pb")), "pb");
    view.bind_world("pb", &near, sg::portal_carry(far.element("pb"), near.element("pa")), "pa");
    const auto shot = [&] {
        for (int i = 0; i < 3; ++i) view.render(near, W, H);
        return floor_brightness(W, H);
    };

    const double through = shot();
    near.element("pa").params.set("light", 0.0);
    const double none = shot();
    near.element("pa").params.set("light", 1.0);
    near.element("hinge").params.set(sg::keys::z, 0.3);
    const double shut = shot();
    // Half shut: a slab over one half of the opening shades the floor on
    // that side and leaves the other lit - a shadow, not the whole of the
    // light dimmed by half.
    near.element("slab").params.set(sg::keys::sx, 0.55).set(sg::keys::x, -0.25);
    shot();
    const double left = floor_brightness(W, H, 0.3, 0.45), right = floor_brightness(W, H, 0.55, 0.7);
    near.element("hinge").params.set(sg::keys::z, 30.0);
    // The door swung open a little at a time: the light comes in as it
    // opens, more the wider it is - not only once it is wide open.
    near.element("door_hinge").params.set(sg::keys::z, 0.3);
    std::vector<double> swing;
    for (int deg : {0, 15, 30, 45, 60, 75, 90}) {
        near.element("door_hinge").params.set(sg::keys::yaw, deg * 3.14159265 / 180.0);
        swing.push_back(shot());
    }
    near.element("door_hinge").params.set(sg::keys::z, 30.0);

    std::printf("floor by the doorway: open %.1f, light = 0 %.1f, shut %.1f; half shut: left %.1f right %.1f\n", through,
                none, shut, left, right);
    bool ok = true;
    const auto check = [&](bool c, const char* what) {
        std::printf("[%s] %s\n", c ? "ok" : "FAIL", what);
        ok = ok && c;
    };
    check(through > none + 8.0, "the far room's lamp lights the near floor through the doorway");
    check(shut < none + 2.0, "a slab shut in the opening keeps it out, hung off a hinge as a door is");
    check(std::abs(right - left) > 40.0 && std::min(left, right) < none + 20.0,
          "half shut, it throws a shadow the shape of what is in the way");
    std::printf("a door swung open 0..90 degrees:");
    for (double b : swing) std::printf(" %.1f", b);
    std::printf("\n");
    bool grows = swing.front() < none + 2.0 && swing[2] > none + 15.0;
    for (std::size_t i = 1; i < swing.size(); ++i) grows = grows && swing[i] >= swing[i - 1] - 3.0;
    check(grows, "a door opening lets the light in as it opens, more the wider it is");
    return ok ? 0 : 1;
}
