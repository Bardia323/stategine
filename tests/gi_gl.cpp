// Stategine - the light from all round traced (`trace.gi`, docs/trace.md),
// against a real GL context.
//
// A closed room, its walls its own, a window in its north wall glued to a
// bright open world whose sun comes in at it and lays a patch on the floor.
// A look that says `trace.gi` 0 draws exactly as one that says nothing of it.
// Traced: the ceiling over the sunlit patch is lit by it more than the probes
// alone light it; a red wall gives the white floor beside it its colour; a
// box set on the floor darkens the floor at its foot.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "sg/dsl/Natives.hpp"
#include "sg/dsl/Runtime.hpp"
#include "sg/gl/Window.hpp"
#include "sg/gl/World.hpp"
#include "sg/sg.hpp"
namespace sgen { void build_gi(sg::StateGraph&, const sg::dsl::Natives&, sg::dsl::Bindings&); }

namespace {
bool ok = true;
void check(bool c, const std::string& what) {
    std::printf("[%s] %s\n", c ? "ok" : "FAIL", what.c_str());
    ok = ok && c;
}
struct Rgb {
    double r = 0, g = 0, b = 0;
    double luma() const { return 0.2126 * r + 0.7152 * g + 0.0722 * b; }
};
}  // namespace

int main() {
    const int W = 320, H = 240;
    sg::gl::Window window(W, H, "gi");
    sg::StateGraph g;
    auto& room = g.add<sg::Spatial3D>("room");
    auto& out = g.add<sg::Spatial3D>("out");
    g.set_initial("room");

    // The room: four metres square, 2.6 high; its floor and ceiling the
    // renderer's own shell, white; its walls its own, a window in the north
    // one, 1.2 m wide and high, its sill a metre up.
    room.params().set("room_w", 4.0).set("room_d", 4.0).set("room_h", 2.6).set("far", 40.0);
    room.params().set("floor_r", 0.8).set("floor_g", 0.8).set("floor_b", 0.8).set("floor_roughness", 1.0);
    room.params().set("ceiling_r", 0.8).set("ceiling_g", 0.8).set("ceiling_b", 0.8);
    const auto white = [](sg::Element& e) { e.params.set("r", 0.8).set("g", 0.8).set("b", 0.8).set("roughness", 1.0); };
    sg::Element& west = room.wall("west", {-0.1, 0.0, 2.0}, 0.2, 2.6, 4.4);
    white(west);
    white(room.wall("east", {4.1, 0.0, 2.0}, 0.2, 2.6, 4.4));
    white(room.wall("south", {2.0, 0.0, 4.1}, 4.4, 2.6, 0.2));
    white(room.wall("north_l", {0.6, 0.0, -0.1}, 1.6, 2.6, 0.2));
    white(room.wall("north_r", {3.4, 0.0, -0.1}, 1.6, 2.6, 0.2));
    white(room.wall("north_sill", {2.0, 0.0, -0.1}, 1.2, 1.0, 0.2));
    white(room.wall("north_head", {2.0, 2.2, -0.1}, 1.2, 0.4, 0.2));
    room.portal("window", {2.0, 1.6, 0.0}, 1.2, 1.2, 1.5707963);
    // A grey box, put on the floor or away.
    sg::Element& box = room.fixture("box", 3.2, 0.0, 3.0);
    box.params.set(sg::keys::sx, 0.6).set(sg::keys::sy, 0.6).set(sg::keys::sz, 0.6).set("r", 0.5).set("g", 0.5).set("b", 0.5).set("roughness", 1.0);
    auto& dim = g.add<sg::LookState>("dim");
    dim.uniform(sg::passes::scene, "uAmbient", 0.0).uniform(sg::passes::scene, "uFogDensity", 0.0);
    dim.uniform(sg::passes::composite, "uGrain", 0.0).uniform(sg::passes::composite, "uVignette", 0.0)
        .uniform(sg::passes::composite, "uBloomStrength", 0.0).uniform(sg::passes::composite, "uExposure", 0.12).fade(0.0);
    dim.setting(sg::passes::scene, "trace", 1.0).setting(sg::passes::scene, "trace.mirror", 0.0);
    sg::wear(g, "room", "dim");

    // Out of the window: an open world, its ground and its sky, and its
    // sun low in the north shining in.
    out.params().set("sky", 1.0).set("far", 300.0);
    sg::Element& ground = out.fixture("ground", 2.0, -1.0, -50.0);
    ground.params.set(sg::keys::sx, 200.0).set(sg::keys::sy, 1.0).set(sg::keys::sz, 100.0).set("r", 0.4).set("g", 0.4).set("b", 0.4);
    sg::Element& sun = out.light("sun", {0.0, 50.0, -50.0});
    sun.params.set("sun", 1.0).set("r", 1.0).set("g", 1.0).set("b", 1.0).set(sg::keys::intensity, 1.0);
    sun.params.set("dx", 0.0).set("dy", -0.6).set("dz", 0.8);
    out.portal("view", {2.0, 1.6, 0.0}, 1.2, 1.2, -1.5707963);
    auto& day = g.add<sg::LookState>("day");
    day.uniform(sg::passes::scene, "uAmbient", 1.0).uniform(sg::passes::scene, "uFogDensity", 0.0);
    day.uniform(sg::passes::scene, "uSky", 0.5, 0.6, 0.8).uniform(sg::passes::scene, "uGround", 0.2, 0.18, 0.15);
    sg::wear(g, "out", "day");

    sg::dsl::Bindings bindings;
    sgen::build_gi(g, {}, bindings);
    check(g.validate().empty(), "the graph is sound");

    sg::Element& eye = room.camera();
    const auto look_from = [&](double x, double y, double z, double yaw, double pitch, double fov) {
        eye.params.set(sg::keys::x, x).set(sg::keys::y, y).set(sg::keys::z, z).set(sg::keys::yaw, yaw).set(sg::keys::pitch, pitch).set(sg::keys::fov, fov);
    };
    std::vector<unsigned char> px(static_cast<std::size_t>(W) * H * 3);
    const auto view_of = [&] {
        auto v = std::make_unique<sg::render::GLWorldView>();
        const auto problems = v->prepare(g);
        for (const auto& p : problems) std::printf("  ! %s\n", p.c_str());
        v->set_fixed_step(1.0 / 60.0);
        v->bind_world("window", &out, sg::portal_carry(room.element("window"), out.element("view")), "view");
        v->bind_world("view", &room, sg::portal_carry(out.element("view"), room.element("window")), "window");
        return v;
    };
    const auto shot = [&](sg::render::GLWorldView& v, int frames) {
        for (int i = 0; i < frames; ++i) {
            v.render(room, W, H);
            window.swap();
        }
        sg::gl::glReadPixels(0, 0, W, H, sg::gl::GL_RGB, sg::gl::GL_UNSIGNED_BYTE, px.data());
        if (const char* dir = std::getenv("SG_GI_SHOTS")) {
            static int kept = 0;
            const std::string path = std::string(dir) + "/gi" + std::to_string(kept++) + ".ppm";
            if (FILE* f = std::fopen(path.c_str(), "wb")) {
                std::fprintf(f, "P6 %d %d 255\n", W, H);
                for (int j = H - 1; j >= 0; --j) std::fwrite(&px[static_cast<std::size_t>(j) * W * 3], 1, static_cast<std::size_t>(W) * 3, f);
                std::fclose(f);
            }
        }
        return px;
    };
    // The middle of the picture, nine pixels by nine.
    const auto middle = [&] {
        Rgb c;
        int n = 0;
        for (int j = H / 2 - 4; j <= H / 2 + 4; ++j)
            for (int i = W / 2 - 4; i <= W / 2 + 4; ++i, ++n) {
                const unsigned char* p = &px[(static_cast<std::size_t>(j) * W + i) * 3];
                c.r += p[0], c.g += p[1], c.b += p[2];
            }
        c.r /= n, c.g /= n, c.b /= n;
        return c;
    };

    // A look that says `trace.gi` 0 draws as one that says nothing of it.
    look_from(2.0, 1.5, 3.6, -1.5707963, -0.2, 70.0);
    std::vector<unsigned char> plain, zero;
    {
        auto v = view_of();
        plain = shot(*v, 8);
    }
    dim.setting(sg::passes::scene, "trace.gi", 0.0);
    {
        auto v = view_of();
        zero = shot(*v, 8);
    }
    check(plain == zero, "a look that says trace.gi 0 draws exactly as one that says nothing of it");

    // Each looked at from its own place, its history let fill (a ray a pixel
    // a frame), probes alone and traced.
    struct Seen {
        Rgb ceiling, ceiling_away, near_red, near_white, foot, foot_bare;
    };
    const auto measure = [&](bool traced) {
        dim.setting(sg::passes::scene, "trace.gi", traced ? 1.0 : 0.0).setting(sg::passes::scene, "trace.gi.rays", 1.0);
        auto v = view_of();
        Seen s;
        // The ceiling over the patch of sun on the floor (x 1.4..2.6, z 1.3..2.9).
        look_from(2.0, 0.9, 2.1, 0.0, 1.55, 40.0);
        shot(*v, 48);
        s.ceiling = middle();
        // And a metre and a half south of it, toward the far wall.
        look_from(2.0, 0.9, 3.6, 0.0, 1.55, 40.0);
        shot(*v, 48);
        s.ceiling_away = middle();
        // The floor a quarter of a metre from the west wall, red or white.
        west.params.set("r", 0.8).set("g", 0.06).set("b", 0.05);
        look_from(0.25, 1.4, 3.3, 0.0, -1.5707, 30.0);
        shot(*v, 48);
        s.near_red = middle();
        west.params.set("r", 0.8).set("g", 0.8).set("b", 0.8);
        shot(*v, 48);
        s.near_white = middle();
        // The floor five centimetres from the box's south face, the box
        // there and put away.
        look_from(3.2, 1.6, 3.35, 0.0, -1.5707, 30.0);
        shot(*v, 48);
        s.foot = middle();
        box.params.set(sg::keys::x, 30.0);
        shot(*v, 48);
        s.foot_bare = middle();
        box.params.set(sg::keys::x, 3.2);
        return s;
    };
    const Seen probes = measure(false), traced = measure(true);
    const auto say = [](const char* what, const Rgb& p, const Rgb& t) {
        std::printf("%-34s probes %6.1f %6.1f %6.1f | traced %6.1f %6.1f %6.1f\n", what, p.r, p.g, p.b, t.r, t.g, t.b);
    };
    say("ceiling over the sunlit floor", probes.ceiling, traced.ceiling);
    say("ceiling 1.5 m south of it", probes.ceiling_away, traced.ceiling_away);
    say("floor by the west wall, red", probes.near_red, traced.near_red);
    say("floor by the west wall, white", probes.near_white, traced.near_white);
    say("floor at the box's foot", probes.foot, traced.foot);
    say("floor there, no box", probes.foot_bare, traced.foot_bare);

    // (The probes' boxes, two metres across, give the light from all round
    // alike over a box: over the patch of sun and off it nearly the same.)
    const double over = traced.ceiling.luma() / std::max(traced.ceiling_away.luma(), 1.0);
    const double over_probes = probes.ceiling.luma() / std::max(probes.ceiling_away.luma(), 1.0);
    std::printf("the ceiling over the sunlit floor over the ceiling away from it: traced %.2f, probes %.2f\n", over, over_probes);
    check(over > over_probes + 0.1 && traced.ceiling.luma() > 0.85 * probes.ceiling.luma(),
          "the ceiling over the sunlit floor is lit by it, traced, more than the ceiling away from it - more so than by the probes alone");
    const double red = traced.near_red.r / std::max(traced.near_red.g, 1.0), plain_wall = traced.near_white.r / std::max(traced.near_white.g, 1.0);
    std::printf("the floor's red over green by the red wall %.2f, by the white wall %.2f\n", red, plain_wall);
    check(red > 1.15 * plain_wall, "a red wall gives the white floor beside it its colour");
    const double contact = traced.foot.luma() / std::max(traced.foot_bare.luma(), 1.0);
    const double contact_probes = probes.foot.luma() / std::max(probes.foot_bare.luma(), 1.0);
    std::printf("the floor at the box's foot over the floor there with no box: traced %.2f, probes %.2f\n", contact, contact_probes);
    check(contact < 0.85 && contact < contact_probes - 0.05, "a box on the floor darkens the floor at its foot");

    std::printf("%s\n", ok ? "all ok" : "FAILED");
    return ok ? 0 : 1;
}
