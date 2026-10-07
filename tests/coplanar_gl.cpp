// Stategine - no two surfaces fight, against a real GL context.
//
// A green plate lies flush in the face of a red wall - its face in the
// wall's, to the last bit - and is looked at square on and edge on, from two
// metres to forty: wherever the two coincide the smaller (the plate) is seen,
// every pixel, every view. And what stands a few centimetres behind the wall
// does not show through it, near or far.
#include <algorithm>
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
    const int W = 240, H = 240;
    sg::gl::Window window(W, H, "coplanar");
    sg::StateGraph g;
    auto& room = g.add<sg::Spatial3D>("room");
    room.params().set("room_w", 120.0).set("room_d", 120.0).set("room_h", 20.0);
    g.set_initial("room");
    sg::Element& sun = room.light("sun", {60.0, 18.0, 90.0});
    sun.params.set("dx", 0.0).set("dy", -0.3).set("dz", -1.0).set(sg::keys::intensity, 1.5);
    const auto box = [&](const std::string& id, double x, double y, double z, double sx, double sy, double sz, double r, double gr, double b) -> sg::Element& {
        sg::Element& e = room.fixture(sg::Key{id}, x, y, z);
        e.params.set(sg::keys::sx, sx).set(sg::keys::sy, sy).set(sg::keys::sz, sz);
        e.params.set(sg::keys::r, r).set(sg::keys::g, gr).set(sg::keys::b, b).set("roughness", 0.9);
        return e;
    };
    // The wall's face at z = 60.1; the plate's face too.
    box("wall", 60.0, 0.0, 60.0, 8.0, 6.0, 0.2, 0.8, 0.05, 0.05);
    box("plate", 60.0, 2.0, 60.09, 1.0, 1.0, 0.02, 0.05, 0.8, 0.05);
    // Four centimetres behind the wall's back, a yellow block.
    box("hidden", 62.5, 2.0, 59.6, 0.3, 1.0, 0.3, 0.9, 0.9, 0.05);
    sg::Element& eye = room.camera();
    sg::render::GLWorldView view;
    view.prepare(g);
    const auto look = [&](double dist, double turn, double at_x) {
        // On the plate's middle (or a point of the wall), from `dist`, `turn` round from square on.
        const double tx = at_x, ty = 2.5, tz = 60.1;
        const double ex = tx + std::sin(turn) * dist, ez = tz + std::cos(turn) * dist;
        eye.params.set(sg::keys::x, ex).set(sg::keys::y, ty).set(sg::keys::z, ez);
        eye.params.set(sg::keys::yaw, std::atan2(tz - ez, tx - ex)).set(sg::keys::pitch, 0.0).set("fov", 30.0);
        for (int i = 0; i < 3; ++i) view.render(room, W, H);
        std::vector<unsigned char> px(static_cast<std::size_t>(W) * H * 3);
        sg::gl::glReadPixels(0, 0, W, H, sg::gl::GL_RGB, sg::gl::GL_UNSIGNED_BYTE, px.data());
        return px;
    };
    int fights = 0, through = 0, views = 0;
    for (const double dist : {2.0, 5.0, 10.0, 20.0, 40.0})
        for (const double turn : {0.0, 0.6, 1.1, 1.35}) {
            // The plate's middle: a little square of pixels, every one green.
            const auto px = look(dist, turn, 60.0);
            // (Only where the plate surely is: half its seen width and height, at most three pixels.)
            const double px_per_m = (H / 2.0) / (dist * std::tan(15.0 * 3.14159265 / 180.0));
            const int hx = std::min(3, static_cast<int>(0.5 * std::cos(turn) * px_per_m * 0.5)), hy = std::min(3, static_cast<int>(0.25 * px_per_m));
            int here = 0;
            for (int y = H / 2 - hy; y <= H / 2 + hy; ++y)
                for (int x = W / 2 - hx; x <= W / 2 + hx; ++x) {
                    const unsigned char* p = &px[(static_cast<std::size_t>(y) * W + x) * 3];
                    if (p[0] > p[1]) ++here;
                }
            if (here) std::printf("  from %.0f m, %.2f round: %d pixels of the wall\n", dist, turn, here);
            fights += here;
            // The wall in front of the yellow block: never yellow.
            if (dist >= 5.0) {
                const auto behind = look(dist, turn * 0.5, 62.5);
                for (int y = H / 2 - 3; y <= H / 2 + 3; ++y)
                    for (int x = W / 2 - 3; x <= W / 2 + 3; ++x) {
                        const unsigned char* p = &behind[(static_cast<std::size_t>(y) * W + x) * 3];
                        if (p[1] > p[0] * 0.7 && p[1] > 40) ++through;
                    }
            }
            ++views;
        }
    check(fights == 0, "where a plate lies flush in a wall, the plate is seen - every pixel, from two metres to forty, square on to edge on (" +
                           std::to_string(fights) + " pixels of the wall)");
    check(through == 0, "and what stands just behind the wall does not show through it (" + std::to_string(through) + " pixels)");
    std::printf("%d views\n", views);
    return ok ? 0 : 1;
}
