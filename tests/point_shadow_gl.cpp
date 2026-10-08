// Stategine - shadows from a lamp with no cone, against a real GL context.
//
// A bare bulb lights all round it, so it shadows all round it: its maps are
// six, a face of a cube each, and a box standing beside it - level with it,
// where one map pointed down would never see - throws its shadow on the wall
// behind. In an empty room the six maps show no seam: the room is lit as it
// would be with no shadows at all, wherever the faces meet. And the maps are
// drawn once: a still room draws none again.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
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

using Picture = std::vector<unsigned char>;

// How many pixels of `a` are darker than `b` by more than `by` levels, and
// the most any differs either way.
void compare(const Picture& a, const Picture& b, int by, int& darker, int& most) {
    darker = 0, most = 0;
    for (std::size_t i = 0; i + 2 < a.size(); i += 3) {
        const int la = a[i] + a[i + 1] + a[i + 2], lb = b[i] + b[i + 1] + b[i + 2];
        if (lb - la > 3 * by) ++darker;
        most = std::max(most, std::abs(la - lb) / 3);
    }
}

}  // namespace

int main() {
    const int W = 320, H = 180;
    sg::gl::Window window(W, H, "point shadows");
    sg::StateGraph g;
    auto& room = g.add<sg::Spatial3D>("room");
    room.params().set("room_w", 8.0).set("room_d", 8.0).set("room_h", 3.0);
    g.set_initial("room");
    // A bare bulb low in the middle of the room: no cone, light all round.
    sg::Element& bulb = room.light("bulb", {4.0, 1.0, 4.0});
    bulb.params.set("inner", 3.14159265).set("outer", 3.14159265).set(sg::keys::intensity, 1.2).set("fixture", 0.0);
    sg::Element& eye = room.camera();
    // Looking at the wall the box will stand before, from off to one side.
    eye.params.set(sg::keys::x, 5.0).set(sg::keys::y, 1.0).set(sg::keys::z, 1.0).set(sg::keys::yaw, 0.785398).set(sg::keys::pitch, 0.0);

    auto& look = g.add<sg::LookState>("room.plain");
    look.uniform(sg::passes::composite, "uGrain", 0.0)
        .uniform(sg::passes::composite, "uVignette", 0.0)
        .uniform(sg::passes::composite, "uBloomStrength", 0.0)
        .fade(0.0);
    sg::wear(g, "room", "room.plain");

    sg::render::GLWorldView view;
    const auto problems = view.prepare(g);
    for (const auto& p : problems) std::printf("  ! %s\n", p.c_str());
    check(problems.empty(), "the looks build");
    view.set_fixed_step(1.0 / 60.0);
    view.set_timing(true);
    const auto shot = [&] {
        Picture p;
        for (int i = 0; i < 3; ++i) view.render(room, W, H);
        p.resize(static_cast<std::size_t>(W) * H * 3);
        sg::gl::glReadPixels(0, 0, W, H, sg::gl::GL_RGB, sg::gl::GL_UNSIGNED_BYTE, p.data());
        return p;
    };
    int darker = 0, most = 0;

    // An empty room - but for a floor and a wall that cast, and so are seen
    // by the maps they are lit through: lit as if nothing cast at all, where
    // the faces meet and at a slant (no seam, no acne).
    room.fixture("slab", 4.0, 0.0, 4.0).params.set(sg::keys::sx, 7.8).set(sg::keys::sy, 0.04).set(sg::keys::sz, 7.8);
    room.fixture("panel", 7.85, 0.0, 4.0).params.set(sg::keys::sx, 0.1).set(sg::keys::sy, 2.9).set(sg::keys::sz, 7.8);
    const Picture empty = shot();
    bulb.params.set("shadow_floor", 1.0);  // light left in its full shadow: all of it
    const Picture unshadowed = shot();
    if (const char* dump = std::getenv("SG_POINT_DUMP"))
        for (const auto& [name, p] : {std::pair{"/point-empty.ppm", &empty}, std::pair{"/point-unshadowed.ppm", &unshadowed}})
            if (FILE* f = std::fopen((std::string(dump) + name).c_str(), "wb")) {
                std::fprintf(f, "P6\n%d %d\n255\n", W, H);
                for (int y = H - 1; y >= 0; --y) std::fwrite(p->data() + static_cast<std::size_t>(y) * W * 3, 1, static_cast<std::size_t>(W) * 3, f);
                std::fclose(f);
            }
    compare(empty, unshadowed, 2, darker, most);
    std::printf("  empty room: %d pixels darker for its shadows, the most %d levels\n", darker, most);
    // (But for the line where the wall stands on the floor: as any map has.)
    check(darker < W * H / 100, "six maps and no seam: an empty room is lit as with no shadows");

    // A box beside the bulb, level with it: its shadow falls on the wall
    // behind it - where a map pointed down never looks.
    sg::Element& box = room.fixture("box", 6.0, 0.0, 4.0);
    box.params.set(sg::keys::sx, 0.6).set(sg::keys::sy, 1.6).set(sg::keys::sz, 0.6);
    const Picture box_unshadowed = shot();
    bulb.params.set("shadow_floor", -1.0);
    const Picture box_shadowed = shot();
    compare(box_shadowed, box_unshadowed, 12, darker, most);
    std::printf("  a box beside the bulb: %d pixels darker for its shadow\n", darker);
    check(darker > W * H / 50, "a box beside a bare bulb throws its shadow on the wall behind");

    // Still, nothing is drawn again.
    view.render(room, W, H);
    check(view.times().shadow_maps == 0, "a still room draws none of its six maps again");
    return ok ? 0 : 1;
}
