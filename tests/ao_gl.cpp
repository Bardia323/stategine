// Stategine - ambient occlusion (GTAO, the composite's `ao`) against a real GL
// context: a crease is hidden from the light from all round, an open floor is
// not. A box stood on the floor of a room, seen from above and to the side:
// along its foot and in the room's corners the occlusion goes deep; across
// the open floor it stays open - a flat surface is never greyed by itself,
// wherever on the screen it is (slices turned evenly on the screen, not round
// the eye's ray, greyed a floor 10% at the frame's corners).
#include <algorithm>
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
float at(const std::vector<float>& v, double p) {
    std::vector<float> s = v;
    std::sort(s.begin(), s.end());
    return s[static_cast<std::size_t>(p * static_cast<double>(s.size() - 1))];
}
}  // namespace

int main() {
    const int W = 640, H = 360;
    sg::gl::Window window(W, H, "ao");
    sg::StateGraph g;
    auto& room = g.add<sg::Spatial3D>("room");
    g.set_initial("room");
    room.params().set("room_w", 6.0).set("room_d", 6.0).set("room_h", 3.0);
    sg::Element& box = room.fixture(sg::Key{"box"}, 3.0, 0.0, 3.0);
    box.params.set(sg::keys::sx, 1.0).set(sg::keys::sy, 0.8).set(sg::keys::sz, 1.0);
    room.light(sg::Key{"lamp"}, {3.0, 2.8, 3.0}).params.set(sg::keys::intensity, 1.0);
    auto& look = g.add<sg::LookState>("air");
    look.setting(sg::passes::composite, "ao", 1.0).setting(sg::passes::composite, "ao.radius", 0.6);
    sg::wear(g, "room", "air");
    sg::Element& eye = room.camera();
    sg::render::GLWorldView view;
    view.prepare(g);
    const auto shoot = [&](double x, double y, double z, double yaw, double pitch) {
        eye.params.set(sg::keys::x, x).set(sg::keys::y, y).set(sg::keys::z, z).set(sg::keys::yaw, yaw).set(sg::keys::pitch, pitch);
        for (int i = 0; i < 4; ++i) view.render(room, W, H);
        int w = 0, h = 0;
        std::vector<float> ao = view.occlusion(w, h);
        // (SG_AO_TEST_PGM=prefix: each look written out, to see.)
        if (const char* f = std::getenv("SG_AO_TEST_PGM")) {
            static int n = 0;
            std::string out = std::string(f) + std::to_string(n++) + ".pgm", head = "P5 " + std::to_string(w) + " " + std::to_string(h) + " 255\n";
            std::vector<unsigned char> px(ao.size());
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) px[static_cast<std::size_t>(y) * w + x] = static_cast<unsigned char>(std::clamp(ao[static_cast<std::size_t>(h - 1 - y) * w + x], 0.0f, 1.0f) * 255.0f);
            if (FILE* fp = std::fopen(out.c_str(), "wb")) {
                std::fwrite(head.data(), 1, head.size(), fp);
                std::fwrite(px.data(), 1, px.size(), fp);
                std::fclose(fp);
            }
        }
        return ao;
    };
    // The box on the floor and the far corner, from a corner of the room.
    const auto seen = shoot(0.6, 2.2, 0.6, 0.785, -0.45);
    const float deep = at(seen, 0.01), middle = at(seen, 0.5);
    std::printf("box and corners: 1%% %.3f, median %.3f\n", deep, middle);
    check(deep < 0.65, "along the box's foot and in the corners the light from all round is hidden deep");
    check(middle > 0.85, "most of what is seen is open");
    // The open floor alone, straight down from over a clear patch, well away from the walls and the box.
    const auto floor = shoot(1.5, 0.8, 4.6, 0.785, -1.5);
    const float least = at(floor, 0.01);
    std::printf("open floor: 1%% %.3f\n", least);
    check(least > 0.92, "an open floor is not greyed by itself");
    std::printf("%s\n", ok ? "all ok" : "FAILED");
    return ok ? 0 : 1;
}
