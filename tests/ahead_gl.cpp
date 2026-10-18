// Stategine - warming ahead, against a real GL context (GLWorldView::ahead,
// warm_ahead).
//
// A hall, and a gallery one transition on from it holding a hundred and
// twenty things each of a shape of its own. Gone into cold, the gallery's
// first frame makes every one of their shapes as it draws them. Warmed ahead
// - the worlds within a step of the hall found, and given to be made a few
// things a frame while the hall is drawn - its first frame makes none.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "sg/domains/Shapes.hpp"
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
    const int W = 480, H = 270;
    sg::gl::Window window(W, H, "ahead");
    sg::StateGraph g;
    auto& hall = g.add<sg::Spatial3D>("hall");
    auto& gallery = g.add<sg::Spatial3D>("gallery");
    g.set_initial("hall");
    g.connect("hall", sg::Key{"hall.go"}, "gallery");
    hall.params().set("room_w", 6.0).set("room_d", 6.0);
    gallery.params().set("room_w", 12.0).set("room_d", 12.0);
    hall.light("lamp", {3, 2.6, 3});
    gallery.light("lamp", {6, 2.6, 6});
    // Each thing a vase of its own shape (its profile its own).
    for (int n = 0; n < 120; ++n) {
        std::vector<sg::shapes::P2> profile;
        for (int i = 0; i <= 8; ++i) profile.push_back({0.2 + 0.1 * std::sin(i * 0.7 + n * 0.37), i / 8.0});
        sg::Vec3d size;
        const sg::Key model{"vase" + std::to_string(n)};
        gallery.model(model, sg::shapes::fit(sg::shapes::lathe(profile, 16), size));
        sg::Element& e = gallery.mesh(sg::Key{"v" + std::to_string(n)}, 1.0 + (n % 10), 0.0, 1.0 + (n / 10));
        e.params.set("shape", std::string("model")).set("model", model.str()).set(sg::keys::sx, size.x * 0.6).set(sg::keys::sy, size.y * 0.6)
            .set(sg::keys::sz, size.z * 0.6);
    }
    gallery.camera().params.set(sg::keys::x, 6.0).set(sg::keys::y, 1.6).set(sg::keys::z, 11.0).set(sg::keys::yaw, -1.5707963).set(sg::keys::pitch, -0.3);

    // Cold: the hall drawn, then the gallery's first frame.
    int cold = 0;
    {
        sg::render::GLWorldView view;
        view.prepare(g);
        for (int i = 0; i < 5; ++i) view.render(hall, W, H), window.swap();
        view.render(gallery, W, H);
        cold = view.times().made_late;
        window.swap();
    }
    // Warmed ahead while the hall is drawn.
    int warm = -1, ahead_made = 0, frames = 0;
    std::size_t found = 0;
    {
        sg::render::GLWorldView view;
        view.prepare(g);
        const std::vector<sg::Spatial3D*> next = view.ahead(g, hall, 1);
        found = next.size();
        view.warm_ahead(next);
        for (; frames < 600 && (frames < 5 || view.warming_ahead() > 0); ++frames) {
            view.render(hall, W, H);
            ahead_made += view.times().warmed_ahead;
            window.swap();
        }
        view.render(gallery, W, H);
        warm = view.times().made_late;
        window.swap();
    }
    check(found == 1, "a step on from the hall: the gallery");
    check(cold >= 120, "gone into cold, its first frame makes its things' shapes (" + std::to_string(cold) + ")");
    check(warm == 0 && ahead_made >= 120, "warmed ahead, it makes none (" + std::to_string(ahead_made) + " made ahead over " + std::to_string(frames) +
                                              " frames of the hall)");
    return ok ? 0 : 1;
}
