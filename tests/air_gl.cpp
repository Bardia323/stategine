// Stategine - a world's air lit by its lamps, and its look's grade, against
// a real GL context.
//
// A lamp shines down a dark room through air its look says scatters: the
// air in the lamp's cone glows, and a board held in the cone throws a
// shadow through the glow - darker below the board than beside it. A view
// that does not move, of a world that does not move, gathers its air a few
// times - each at other points of its cells, averaged - and then rests.
// A look that says nothing of its air gathers none and draws as it did. The
// look's grade is the look's: brighter by a stop, grey without saturation,
// and as it was with none. And a lamp's glow spreads further the deeper into
// thick air it is seen.
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

struct Picture {
    int w = 0, h = 0;
    std::vector<unsigned char> px;
    // The mean of a box of it (0..1 of the picture each way), per channel.
    void mean(float x0, float y0, float x1, float y1, double out[3]) const {
        out[0] = out[1] = out[2] = 0;
        int n = 0;
        for (int y = static_cast<int>(y0 * h); y < static_cast<int>(y1 * h); ++y)
            for (int x = static_cast<int>(x0 * w); x < static_cast<int>(x1 * w); ++x, ++n)
                for (int c = 0; c < 3; ++c) out[c] += px[(static_cast<std::size_t>(y) * w + x) * 3 + c];
        for (int c = 0; c < 3; ++c) out[c] /= std::max(n, 1);
    }
    double luma(float x0, float y0, float x1, float y1) const {
        double m[3];
        mean(x0, y0, x1, y1, m);
        return 0.2126 * m[0] + 0.7152 * m[1] + 0.0722 * m[2];
    }
};

}  // namespace

int main() {
    const int W = 320, H = 180;
    sg::gl::Window window(W, H, "air");
    sg::StateGraph g;
    auto& room = g.add<sg::Spatial3D>("room");
    room.params().set("room_w", 8.0).set("room_d", 8.0).set("room_h", 4.0);
    g.set_initial("room");
    // A narrow lamp high up, shining straight down the middle of the room.
    sg::Element& lamp = room.light("lamp", {4.0, 3.8, 4.0});
    lamp.params.set("dx", 0.0).set("dy", -1.0).set("dz", 0.0).set("inner", 0.25).set("outer", 0.35).set(sg::keys::intensity, 2.0);
    // A board in the cone, off to one side of it: its shadow falls through
    // the glowing air below it.
    sg::Element& board = room.fixture("board", 4.3, 2.8, 4.0);
    board.params.set(sg::keys::sx, 0.6).set(sg::keys::sy, 0.04).set(sg::keys::sz, 1.6);
    sg::Element& eye = room.camera();
    eye.params.set(sg::keys::x, 4.0).set(sg::keys::y, 1.8).set(sg::keys::z, 7.6).set(sg::keys::yaw, -1.5707963).set(sg::keys::pitch, 0.1);

    auto& air = g.add<sg::LookState>("room.air");
    air.uniform(sg::passes::scene, "uAmbient", 0.02)
        .uniform(sg::passes::scene, "uFogDensity", 0.02)
        .uniform(sg::passes::composite, "uVignette", 0.0)
        .uniform(sg::passes::composite, "uGrain", 0.0)
        .uniform(sg::passes::composite, "uBloomStrength", 0.0)
        .fade(0.0);
    auto& clear = g.add<sg::LookState>("room.clear");
    clear.uniform(sg::passes::scene, "uAmbient", 0.02)
        .uniform(sg::passes::scene, "uFogDensity", 0.02)
        .uniform(sg::passes::composite, "uVignette", 0.0)
        .uniform(sg::passes::composite, "uGrain", 0.0)
        .uniform(sg::passes::composite, "uBloomStrength", 0.0)
        .fade(0.0);
    sg::wear(g, "room", "room.clear");
    sg::wear(g, "room", "room.air");

    sg::render::GLWorldView view;
    const auto problems = view.prepare(g);
    for (const auto& p : problems) std::printf("  ! %s\n", p.c_str());
    check(problems.empty(), "the looks build");
    const auto shot = [&] {
        view.render(room, W, H);
        Picture p{W, H, std::vector<unsigned char>(static_cast<std::size_t>(W) * H * 3)};
        sg::gl::glReadPixels(0, 0, W, H, sg::gl::GL_RGB, sg::gl::GL_UNSIGNED_BYTE, p.px.data());
        return p;
    };

    // The air says nothing: none is gathered.
    sg::set_look(room, "room.clear");
    for (int i = 0; i < 3; ++i) shot();
    const Picture plain = shot();
    check(view.times().air_built == 0, "air that scatters nothing: none is gathered");

    // The air scatters: the lamp's cone glows, and is shadowed by the board.
    air.setting(sg::passes::scene, "scatter", 0.03).setting(sg::passes::scene, "scatter.ahead", 0.2);
    sg::set_look(room, "room.air");
    shot();
    check(view.times().air_built > 0, "air that scatters: it is gathered");
    int gathered = 1;
    for (int i = 0; i < 24; ++i) {
        shot();
        gathered += view.times().air_built;
    }
    const Picture lit = shot();
    check(view.times().air_built == 0 && gathered > 1 && gathered <= 16,
          "a still view of a still world gathers its air a few times, averaged, and then rests");
    if (const char* dump = std::getenv("SG_AIR_DUMP")) {
        for (const auto& [name, p] : {std::pair{"/air-without.ppm", &plain}, std::pair{"/air-with.ppm", &lit}})
            if (FILE* f = std::fopen((std::string(dump) + name).c_str(), "wb")) {
                std::fprintf(f, "P6\n%d %d\n255\n", W, H);
                for (int y = H - 1; y >= 0; --y) std::fwrite(&p->px[static_cast<std::size_t>(y) * W * 3], 1, static_cast<std::size_t>(W) * 3, f);
                std::fclose(f);
            }
    }
    // (The picture's rows run bottom up: y 0 is the bottom.) Below the lamp,
    // in the middle of the picture, the air between the eye and the far
    // wall: lit where the cone is, and darker in the board's shadow.
    const double before = plain.luma(0.40, 0.30, 0.60, 0.60), glow = lit.luma(0.40, 0.30, 0.60, 0.60);
    std::printf("  the cone's air: %.1f without, %.1f with\n", before, glow);
    check(glow > before + 3.0, "the air in the lamp's cone glows");
    const double open = lit.luma(0.43, 0.25, 0.49, 0.55), shade = lit.luma(0.51, 0.25, 0.57, 0.55);
    const double open0 = plain.luma(0.43, 0.25, 0.49, 0.55), shade0 = plain.luma(0.51, 0.25, 0.57, 0.55);
    std::printf("  beside the board's shadow %.1f (%.1f without), in it %.1f (%.1f without)\n", open, open0, shade, shade0);
    check(open - open0 > 1.5 * (shade - shade0), "and the board's shadow falls through it");
    eye.params.set(sg::keys::x, 4.2);
    shot();
    check(view.times().air_built > 0, "the eye moved: the air is gathered again");

    // The grade: a stop brighter, then no colour at all.
    eye.params.set(sg::keys::x, 4.0);
    clear.uniform(sg::passes::composite, "uGradeExposure", 1.0);
    sg::set_look(room, "room.clear");
    shot();
    const Picture brighter = shot();
    check(brighter.luma(0.0, 0.0, 1.0, 1.0) > plain.luma(0.0, 0.0, 1.0, 1.0) + 2.0, "a grade a stop up: brighter");
    clear.uniform(sg::passes::composite, "uGradeExposure", 0.0).uniform(sg::passes::composite, "uGradeSaturation", -1.0);
    shot();
    const Picture grey = shot();
    double m[3];
    grey.mean(0.0, 0.0, 1.0, 1.0, m);
    double c[3];
    plain.mean(0.0, 0.0, 1.0, 1.0, c);
    std::printf("  mean colour %.1f %.1f %.1f, graded grey %.1f %.1f %.1f\n", c[0], c[1], c[2], m[0], m[1], m[2]);
    check(std::fabs(m[0] - m[1]) < 1.5 && std::fabs(m[1] - m[2]) < 1.5, "a grade with no saturation: grey");
    clear.uniform(sg::passes::composite, "uGradeSaturation", 0.0);
    shot();
    const Picture back = shot();
    double d = 0;
    for (std::size_t i = 0; i < back.px.size(); ++i) d = std::max(d, std::fabs(double(back.px[i]) - double(plain.px[i])));
    check(d <= 1.0, "a grade of nothing: the picture as it was");

    // Glow in thick air: the lamp's glow spreads the more, the deeper into
    // the air it is seen.
    clear.uniform(sg::passes::composite, "uBloomStrength", 0.4).uniform(sg::passes::scene, "uFogDensity", 0.08);
    shot();
    const Picture glow0 = shot();
    clear.uniform(sg::passes::composite, "uFogBloom", 3.0);
    shot();
    const Picture glow1 = shot();
    const double around0 = glow0.luma(0.38, 0.62, 0.62, 0.98), around1 = glow1.luma(0.38, 0.62, 0.62, 0.98);
    std::printf("  round the lamp: %.1f, with the air's glow %.1f\n", around0, around1);
    check(around1 > around0 + 1.0, "glow in thick air spreads further");
    return ok ? 0 : 1;
}
