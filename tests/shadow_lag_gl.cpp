// Stategine - a shadow is where its thing is, the frame it is there, against
// a real GL context.
//
// Six lamps shine down on a floor and a box walks across it a few centimetres
// a frame: every frame, each of its six shadows is where the box is - the
// picture the frame it moved is the picture of the box standing there (drawn
// again with nothing moved, until nothing changes). And the eye walking with
// nothing else moving: the lamps' shadows stay put, as they should.
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
}  // namespace

int main() {
    const int W = 320, H = 180;
    sg::gl::Window window(W, H, "shadow lag");
    sg::StateGraph g;
    auto& room = g.add<sg::Spatial3D>("room");
    room.params().set("room_w", 10.0).set("room_d", 10.0).set("room_h", 4.0);
    g.set_initial("room");
    // Six lamps in a ring, each shining down on the middle of the floor.
    for (int i = 0; i < 6; ++i) {
        const double a = i * 1.0471976;
        sg::Element& l = room.light("lamp" + std::to_string(i), {5.0 + 2.5 * std::cos(a), 3.6, 5.0 + 2.5 * std::sin(a)});
        l.params.set("dx", -std::cos(a) * 0.6).set("dy", -1.0).set("dz", -std::sin(a) * 0.6).set("inner", 0.6).set("outer", 0.9);
        l.params.set(sg::keys::intensity, 0.5);
    }
    sg::Element& box = room.fixture("box", 3.5, 0.0, 5.0);
    box.params.set(sg::keys::sx, 0.4).set(sg::keys::sy, 1.2).set(sg::keys::sz, 0.4);
    sg::Element& eye = room.camera();
    eye.params.set(sg::keys::x, 5.0).set(sg::keys::y, 3.0).set(sg::keys::z, 9.6).set(sg::keys::yaw, -1.5707963).set(sg::keys::pitch, -0.55);

    sg::render::GLWorldView view;
    view.prepare(g);
    const auto shot = [&] {
        view.render(room, W, H);
        std::vector<unsigned char> px(static_cast<std::size_t>(W) * H * 3);
        sg::gl::glReadPixels(0, 0, W, H, sg::gl::GL_RGB, sg::gl::GL_UNSIGNED_BYTE, px.data());
        return px;
    };
    const auto apart = [](const std::vector<unsigned char>& a, const std::vector<unsigned char>& b) {
        double most = 0, sum = 0;
        for (std::size_t i = 0; i < a.size(); ++i) {
            const double d = std::fabs(double(a[i]) - double(b[i]));
            most = std::max(most, d), sum += d;
        }
        return std::pair{most, sum / a.size()};
    };
    for (int i = 0; i < 8; ++i) shot();  // (settled)

    double worst = 0, worst_mean = 0;
    for (int f = 0; f < 10; ++f) {
        box.params.set(sg::keys::x, 3.5 + 0.06 * (f + 1));
        const auto now = shot();
        const int drawn = view.times().shadow_maps;
        std::vector<unsigned char> settled;
        for (int i = 0; i < 8; ++i) settled = shot();
        const auto [most, mean] = apart(now, settled);
        std::printf("  frame %d: the box moved, %d maps drawn; against it standing there: most %.0f, mean %.3f\n", f,
                    drawn, most, mean);
        worst = std::max(worst, most), worst_mean = std::max(worst_mean, mean);
    }
    check(worst_mean < 0.01 && worst <= 2.0, "every frame, every lamp's shadow of a moving thing is where the thing is");

    // The eye walking, nothing else moving: no map is drawn again.
    int maps = 0;
    for (int f = 0; f < 6; ++f) {
        eye.params.set(sg::keys::x, 5.0 + 0.05 * f);
        shot();
        maps += view.times().shadow_maps;
    }
    check(maps == 0, "the eye walking past still things draws no lamp's map again");
    return ok ? 0 : 1;
}
