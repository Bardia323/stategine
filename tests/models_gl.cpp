// Stategine - a model made again is drawn as it is now, against a real GL
// context.
//
// A thing's model is made again every frame (as a skinned being's is, posed
// anew): a slab in one third of a fixed box, the left, the middle, the
// right, round and round, the thing's own params never changing. Every
// frame shows the slab where the model has it that frame - never a model
// made before it, never another's. (The renderer once kept a model's mesh
// by where its corners lay in memory: a model made again where an old one
// had been was drawn as the old one - a being flipping back to a pose it
// had left, or vanishing.)
#include <algorithm>
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
    const int W = 240, H = 120;
    sg::gl::Window window(W, H, "models");
    sg::StateGraph g;
    auto& room = g.add<sg::Spatial3D>("room");
    room.params().set("room_w", 120.0).set("room_d", 120.0).set("room_h", 20.0);
    g.set_initial("room");
    sg::Element& sun = room.light("sun", {60.0, 18.0, 90.0});
    sun.params.set("dx", 0.0).set("dy", -0.3).set("dz", -1.0).set(sg::keys::intensity, 1.5);
    // The slab, in the third `k` of a box 3 m wide, 1 m tall, 0.2 m deep.
    const auto slab = [](int k, sg::Vec3d& size) {
        const double x0 = -1.5 + k;
        auto v = sg::shapes::extrude({{x0, 0}, {x0 + 1, 0}, {x0 + 1, 1}, {x0, 1}}, 0.2);
        return sg::shapes::fit(std::move(v), size, {-1.5, 0, -0.1}, {1.5, 1, 0.1});
    };
    sg::Vec3d size;
    room.model("slab", slab(0, size));
    sg::Element& thing = room.mesh(sg::Key{"thing"}, 60.0, 2.0, 60.0);
    thing.params.set("shape", std::string("model")).set("model", std::string("slab"));
    thing.params.set(sg::keys::sx, size.x).set(sg::keys::sy, size.y).set(sg::keys::sz, size.z);
    thing.params.set(sg::keys::r, 0.9).set(sg::keys::g, 0.9).set(sg::keys::b, 0.9).set("roughness", 0.9);
    sg::Element& eye = room.camera();
    eye.params.set(sg::keys::x, 60.0).set(sg::keys::y, 2.5).set(sg::keys::z, 66.0);
    eye.params.set(sg::keys::yaw, -1.5707963).set(sg::keys::pitch, 0.0).set("fov", 30.0);
    sg::render::GLWorldView view;
    view.prepare(g);
    // How bright each third of the box is, as drawn.
    const auto thirds = [&](double out[3]) {
        std::vector<unsigned char> px(static_cast<std::size_t>(W) * H * 3);
        sg::gl::glReadPixels(0, 0, W, H, sg::gl::GL_RGB, sg::gl::GL_UNSIGNED_BYTE, px.data());
        // 3 m at 6 m, 30 degrees tall: 120 px is 3.2 m, so a metre is ~37 px.
        for (int k = 0; k < 3; ++k) {
            const int cx = W / 2 + (k - 1) * 37;
            double sum = 0;
            int n = 0;
            for (int y = H / 2 - 6; y <= H / 2 + 6; ++y)
                for (int x = cx - 6; x <= cx + 6; ++x, ++n) sum += px[(static_cast<std::size_t>(y) * W + x) * 3 + 1];
            out[k] = sum / n;
        }
    };
    // Twice: the thing's params still, and touched every frame (so only
    // the model's own making can say it is new).
    int wrong = 0, frames = 0;
    for (int f = 0; f < 120; ++f) {
        const int k = f % 3;
        room.model("slab", slab(k, size));
        if (f >= 60) thing.params.set("touched", double(f));
        view.render(room, W, H);
        double b[3];
        thirds(b);
        int lit = 0;
        for (int i = 1; i < 3; ++i)
            if (b[i] > b[lit]) lit = i;
        if (lit != k || b[k] < std::max(b[(k + 1) % 3], b[(k + 2) % 3]) + 20) {
            if (wrong < 6) std::printf("  frame %d: the model has the slab in third %d, drawn in %d (%.0f %.0f %.0f)\n", f, k, lit, b[0], b[1], b[2]);
            ++wrong;
        }
        ++frames;
    }
    check(wrong == 0, "a model made again each frame is drawn as it is that frame (" + std::to_string(wrong) + " of " + std::to_string(frames) + " frames show another)");
    return ok ? 0 : 1;
}
