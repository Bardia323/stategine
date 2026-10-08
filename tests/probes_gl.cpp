// Stategine - light probes, against a real GL context.
//
// A long room lit by one lamp at one end: baked, the probe by the lamp holds
// more of its light come back off the room than the probe at the far end -
// the room is no longer lit alike from end to end, and the end by the lamp
// gains more from them than the far one. Light adds: a lamp dimmed or turned
// another colour changes what the probes give by exactly as much, with no
// bake. And the laws: a probe out of its room, numbers that are not, a set
// for a lamp that is not there, a bake of a room that has since changed -
// each is named; baked as it is, the graph is sound.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <string>
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

double luma(const sg::Rgb& c) { return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b; }

bool says(const std::vector<std::string>& faults, const char* what) {
    for (const auto& f : faults)
        if (f.find(what) != std::string::npos) return true;
    return false;
}

}  // namespace

int main() {
    const int W = 320, H = 180;
    sg::gl::Window window(W, H, "probes");
    sg::StateGraph g;
    auto& room = g.add<sg::Spatial3D>("room");
    room.params().set("room_w", 12.0).set("room_d", 4.0).set("room_h", 3.0);
    g.set_initial("room");
    sg::Element& lamp = room.light("lamp", {1.2, 2.8, 2.0}, 1.0, 1.0, 1.0);
    lamp.params.set("dx", 0.0).set("dy", -1.0).set("dz", 0.0).set("inner", 0.8).set("outer", 1.3).set(sg::keys::intensity, 1.5);
    sg::add_probe(room, "near", {3.0, 0.0, 2.0}, {6.0, 3.0, 4.0}, 0.8);
    sg::add_probe(room, "far", {9.0, 0.0, 2.0}, {6.0, 3.0, 4.0}, 0.8);
    sg::Element& eye = room.camera();
    // Looking across the room at its long wall, the whole length of it.
    eye.params.set(sg::keys::x, 6.0).set(sg::keys::y, 1.5).set(sg::keys::z, 0.2).set(sg::keys::yaw, 1.5707963).set(sg::keys::pitch, 0.0)
        .set(sg::keys::fov, 90.0);
    auto& look = g.add<sg::LookState>("room.plain");
    look.uniform(sg::passes::composite, "uGrain", 0.0).uniform(sg::passes::composite, "uVignette", 0.0)
        .uniform(sg::passes::composite, "uBloomStrength", 0.0).fade(0.0);
    sg::wear(g, "room", "room.plain");

    check(sg::probe_faults(room).empty(), "probes not yet baked say nothing wrong");

    sg::render::GLWorldView view;
    const auto problems = view.prepare(g);
    for (const auto& p : problems) std::printf("  ! %s\n", p.c_str());
    check(problems.empty(), "the looks build");
    view.set_fixed_step(1.0 / 60.0);
    const auto shot = [&] {
        std::vector<unsigned char> p(static_cast<std::size_t>(W) * H * 3);
        for (int i = 0; i < 3; ++i) view.render(room, W, H);
        sg::gl::glReadPixels(0, 0, W, H, sg::gl::GL_RGB, sg::gl::GL_UNSIGNED_BYTE, p.data());
        return p;
    };
    // The mean brightness of a band of the picture, across (0..1).
    const auto band = [&](const std::vector<unsigned char>& p, double x0, double x1) {
        double s = 0;
        int n = 0;
        for (int y = H / 4; y < 3 * H / 4; ++y)
            for (int x = static_cast<int>(x0 * W); x < static_cast<int>(x1 * W); ++x, ++n)
                s += 0.2126 * p[(y * W + x) * 3] + 0.7152 * p[(y * W + x) * 3 + 1] + 0.0722 * p[(y * W + x) * 3 + 2];
        return s / std::max(n, 1);
    };
    // (Before: no light from all round but the look's - not even relit as
    // the renderer relights a room's probes that hold no bake.)
    room.params().set("gi", 0.0);
    const auto before = shot();
    room.params().set("gi", 1.0);

    const auto bakes = view.bake_probes(room, 32, 2);
    check(bakes.size() == 2 && bakes[0].sets.count("lamp") == 1, "a bake: a set for the lamp, for each probe");
    sg::bake_into(room, bakes);
    const sg::Element& near = room.element(sg::Key{"near"});
    const sg::Element& far = room.element(sg::Key{"far"});
    const sg::Sh9 n0 = sg::probe_light(room, near), f0 = sg::probe_light(room, far);
    const double up_n = luma(n0.irradiance({0, 1, 0})), up_f = luma(f0.irradiance({0, 1, 0}));
    const double side_n = luma(n0.irradiance({0, 0, 1})), side_f = luma(f0.irradiance({0, 0, 1}));
    std::printf("  near: up %.4f side %.4f   far: up %.4f side %.4f\n", up_n, side_n, up_f, side_f);
    check(up_n > 0.0 && side_n > 1.5 * side_f && up_n > 1.5 * up_f, "the probe by the lamp holds more of its light than the far one");

    const auto after = shot();
    if (const char* dump = std::getenv("SG_PROBE_DUMP"))
        for (const auto& [name, p] : {std::pair{"/probes-before.ppm", &before}, std::pair{"/probes-after.ppm", &after}})
            if (FILE* f = std::fopen((std::string(dump) + name).c_str(), "wb")) {
                std::fprintf(f, "P6\n%d %d\n255\n", W, H);
                for (int y = H - 1; y >= 0; --y) std::fwrite(p->data() + static_cast<std::size_t>(y) * W * 3, 1, static_cast<std::size_t>(W) * 3, f);
                std::fclose(f);
            }
    const double gain_near = band(after, 0.75, 1.0) - band(before, 0.75, 1.0);  // (the lamp's end is on the right)
    const double gain_far = band(after, 0.0, 0.25) - band(before, 0.0, 0.25);
    std::printf("  drawn: the lamp's end gains %.1f levels, the far end %.1f\n", gain_near, gain_far);
    check(gain_near > 1.0 && gain_near > gain_far, "drawn, the end by the lamp gains more from its probe than the far end");

    // Light adds: dimmed by half, or its green halved - exactly so.
    lamp.params.set(sg::keys::intensity, 0.75);
    const sg::Sh9 n1 = sg::probe_light(room, near);
    lamp.params.set(sg::keys::intensity, 1.5).set(sg::keys::g, 0.5);
    const sg::Sh9 n2 = sg::probe_light(room, near);
    lamp.params.set(sg::keys::g, 1.0);
    bool halves = true;
    for (std::size_t k = 0; k < 9; ++k) {
        for (std::size_t c = 0; c < 3; ++c) halves = halves && std::fabs(n1.c[k][c] - 0.5 * n0.c[k][c]) < 1e-9;
        halves = halves && std::fabs(n2.c[k][1] - 0.5 * n0.c[k][1]) < 1e-9 && std::fabs(n2.c[k][0] - n0.c[k][0]) < 1e-9;
    }
    check(halves, "a lamp dimmed, or of another colour: the probes give exactly as much less, with no bake");

    // The laws.
    check(sg::probe_faults(room).empty() && sg::verify(g).ok(), "baked as the room is, the graph is sound");
    lamp.params.set(sg::keys::x, 2.0);
    check(says(sg::probe_faults(room), "stale"), "a lamp moved: the probes are stale");
    lamp.params.set(sg::keys::x, 1.2);
    check(sg::probe_faults(room).empty(), "and back where it was baked, they are not");
    room.element(sg::Key{"far"}).params.set(sg::keys::x, 11.0);
    check(says(sg::probe_faults(room), "out of its room"), "a probe's box out of its room is named");
    room.element(sg::Key{"far"}).params.set(sg::keys::x, 9.0);
    room.element(sg::Key{"near"}).params.set(sg::Key{"sh.ghost"}, n0.text());
    check(says(sg::probe_faults(room), "no lamp here"), "a set for a lamp that is not there is named");
    room.element(sg::Key{"near"}).params.erase(sg::Key{"sh.ghost"});
    const std::string kept = *room.element(sg::Key{"near"}).params.text(sg::Key{"sh.lamp"});
    room.element(sg::Key{"near"}).params.set(sg::Key{"sh.lamp"}, "nan" + kept.substr(kept.find(' ')));
    check(says(sg::probe_faults(room), "not finite"), "numbers that are not are named");
    room.element(sg::Key{"near"}).params.set(sg::Key{"sh.lamp"}, kept);
    check(sg::probe_faults(room).empty(), "put back, nothing is wrong");

    // Relit, the lamps out of what is baked: the surroundings drawn once,
    // a lamp's set its light on them - the same sets as a bake makes, near
    // enough; and a lamp moved costs only its own set.
    using Clock = std::chrono::steady_clock;
    const auto ms = [](Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    const auto apart = [&](const std::vector<sg::ProbeBake>& baked, const std::vector<sg::ProbeBake>& relit) {
        double worst = 0.0;
        for (std::size_t i = 0; i < baked.size() && i < relit.size(); ++i) {
            const sg::Sh9& b = baked[i].sets.at("lamp");
            const sg::Sh9& r = relit[i].sets.at("lamp");
            const sg::Vec3d ways[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
            double most = 0.0;
            for (const auto& d : ways) most = std::max(most, luma(b.irradiance(d)));
            for (const auto& d : ways) {
                std::printf("    %-4s (%+.0f %+.0f %+.0f): baked %.4f relit %.4f\n", baked[i].probe.str().c_str(), d.x, d.y, d.z,
                            luma(b.irradiance(d)), luma(r.irradiance(d)));
                worst = std::max(worst, std::fabs(luma(r.irradiance(d)) - luma(b.irradiance(d))) / std::max(most, 1e-9));
            }
        }
        return worst;
    };
    auto t0 = Clock::now();
    const auto baked0 = view.bake_probes(room, 32, 2);
    auto t1 = Clock::now();
    const auto relit0 = view.relight_probes(room, 32, 2);
    auto t2 = Clock::now();
    const double e0 = apart(baked0, relit0);
    std::printf("  as it stands: baked in %.1f ms, relit in %.1f ms (the surroundings drawn); apart by %.1f%% at most\n", ms(t0, t1),
                ms(t1, t2), 100.0 * e0);
    check(e0 < 0.15, "relit, the lamps out of the bake: the sets a bake makes, within 15%");
    // The lamp carried to the far end, turned to the long wall.
    lamp.params.set(sg::keys::x, 9.5).set(sg::keys::z, 1.0).set("dx", 0.3).set("dy", -0.6).set("dz", 0.7);
    t0 = Clock::now();
    const auto baked1 = view.bake_probes(room, 32, 2);
    t1 = Clock::now();
    const auto relit1 = view.relight_probes(room, 32, 2);
    t2 = Clock::now();
    const double e1 = apart(baked1, relit1);
    std::printf("  moved: baked in %.1f ms, relit in %.1f ms (its own set alone); apart by %.1f%% at most\n", ms(t0, t1), ms(t1, t2),
                100.0 * e1);
    check(e1 < 0.15, "a lamp moved, relit with nothing drawn again from the probes: within 15% of a bake");
    t0 = Clock::now();
    view.relight_probes(room, 32, 2);
    t1 = Clock::now();
    std::printf("  nothing moved: relit in %.2f ms\n", ms(t0, t1));
    check(ms(t0, t1) < 5.0, "nothing moved: nothing drawn, next to nothing done");
    return ok ? 0 : 1;
}
