// Stategine - fog pooled where a world says (sg/domains/Fog.hpp), against a
// real GL context.
//
// A dark wall across a field, the eye looking at it; a box of fog to one
// side between them. The wall seen through the fog is the fog's colour - its
// own, where it says one - and the wall seen past it is as it was; thinning
// as it rises, less of it at the eye's height. Its wisps move with the time
// of the world it is in: standing still while that does, the picture rests;
// moved when it goes on; and at a world time what a fresh view draws. Lit
// air (a look's `scatter`) scatters more of a lamp's light where it pools.
// And a fog's laws: one thinner than none, or drifting past the most, is a
// fault of its world.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "sg/domains/Fog.hpp"
#include "sg/gl/Window.hpp"
#include "sg/gl/World.hpp"
#include "sg/sg.hpp"

namespace {
bool ok = true;
void check(bool c, const std::string& what) {
    std::printf("[%s] %s\n", c ? "ok" : "FAIL", what.c_str());
    ok = ok && c;
}
struct Rgb {
    double r = 0, g = 0, b = 0;
    double sum() const { return r + g + b; }
};
}  // namespace

int main() {
    const int W = 480, H = 270;
    sg::gl::Window window(W, H, "fog");
    sg::StateGraph g;
    auto& field = g.add<sg::Spatial3D>("field");
    g.set_initial("field");
    field.params().set("sky", 1.0).set("far", 100.0);
    sg::Element& sun = field.light(sg::Key{"sun"}, {0.0, 50.0, 0.0});
    sun.params.set("sun", 1.0).set("dx", 0.2).set("dy", -1.0).set("dz", 0.3).set(sg::keys::intensity, 1.0).set("cast", 0.0);
    sg::Element& wall = field.fixture(sg::Key{"wall"}, 0.0, -2.0, 20.0);
    wall.params.set(sg::keys::sx, 60.0).set(sg::keys::sy, 12.0).set(sg::keys::sz, 0.2).set("r", 0.05).set("g", 0.05).set("b", 0.05);
    auto& look = g.add<sg::LookState>("plain");
    look.uniform(sg::passes::scene, "uFogDensity", 0.0)
        .uniform(sg::passes::scene, "uFogColor.x", 0.55)
        .uniform(sg::passes::scene, "uFogColor.y", 0.6)
        .uniform(sg::passes::scene, "uFogColor.z", 0.65);
    sg::wear(g, "field", "plain");
    sg::Element& eye = field.camera();
    eye.params.set(sg::keys::x, 0.0).set(sg::keys::y, 1.6).set(sg::keys::z, 0.0).set(sg::keys::yaw, 1.5707963).set(sg::keys::pitch, 0.0);

    sg::render::GLWorldView view;
    view.prepare(g);
    std::vector<unsigned char> px(static_cast<std::size_t>(W) * H * 3);
    const auto shot = [&](sg::render::GLWorldView& v, int frames = 12) {
        for (int i = 0; i < frames; ++i) {
            v.render(field, W, H);
            window.swap();
        }
        sg::gl::glReadPixels(0, 0, W, H, sg::gl::GL_RGB, sg::gl::GL_UNSIGNED_BYTE, px.data());
        // (SG_FOG_SHOTS=dir keeps each picture there, to be looked at.)
        static int kept = 0;
        if (const char* dir = std::getenv("SG_FOG_SHOTS")) {
            const std::string path = std::string(dir) + "/fog" + std::to_string(kept++) + ".ppm";
            if (FILE* f = std::fopen(path.c_str(), "wb")) {
                std::fprintf(f, "P6 %d %d 255\n", W, H);
                for (int j = H - 1; j >= 0; --j) std::fwrite(&px[static_cast<std::size_t>(j) * W * 3], 1, static_cast<std::size_t>(W) * 3, f);
                std::fclose(f);
            }
        }
    };
    const auto at = [&](double x, double y) {
        Rgb c;
        const int cx = static_cast<int>(x * W), cy = static_cast<int>(y * H);
        int n = 0;
        for (int j = cy - 4; j <= cy + 4; ++j)
            for (int i = cx - 4; i <= cx + 4; ++i) {
                const unsigned char* p = &px[(static_cast<std::size_t>(j) * W + i) * 3];
                c.r += p[0], c.g += p[1], c.b += p[2], ++n;
            }
        c.r /= n, c.g /= n, c.b /= n;
        return c;
    };
    const auto say = [](const char* what, const Rgb& c) { std::printf("%s: %.0f %.0f %.0f\n", what, c.r, c.g, c.b); };
    const auto apart = [](const std::vector<unsigned char>& a, const std::vector<unsigned char>& b, int* most = nullptr) {
        double sum = 0.0;
        int m = 0;
        for (std::size_t i = 0; i < a.size(); ++i) {
            const int d = std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i]));
            sum += d, m = std::max(m, d);
        }
        if (most) *most = m;
        return sum / static_cast<double>(a.size());
    };

    shot(view);
    const Rgb clear_l = at(0.25, 0.5), clear_r = at(0.75, 0.5);
    const std::vector<unsigned char> clear = px;
    say("the wall, left", clear_l), say("the wall, right", clear_r);

    // A box of fog 8 m wide, 3 m high and 6 m deep, half way to the wall,
    // off to one side.
    sg::add_fog(field, sg::Key{"mist"}, {-7.0, 0.0, 10.0}, {8.0, 3.0, 6.0}, 0.3, 0.5);
    // (Found again each time: an element added may move the others.)
    const auto el = [&](const char* id) -> sg::Params& { return field.find(sg::Key{id})->params; };
    shot(view);
    const Rgb fog_l = at(0.25, 0.5), fog_r = at(0.75, 0.5);
    say("through the fog", fog_l.sum() > fog_r.sum() ? fog_l : fog_r), say("past it", fog_l.sum() > fog_r.sum() ? fog_r : fog_l);
    const Rgb& in = fog_l.sum() > fog_r.sum() ? fog_l : fog_r;
    const Rgb& past = fog_l.sum() > fog_r.sum() ? fog_r : fog_l;
    const Rgb& past_clear = fog_l.sum() > fog_r.sum() ? clear_r : clear_l;
    const Rgb& in_clear = fog_l.sum() > fog_r.sum() ? clear_l : clear_r;
    // (The wall warm, the look's fog a cool grey: through it, cooler.)
    check(in.sum() > in_clear.sum() + 80.0 && in.b - in.r > in_clear.b - in_clear.r + 15.0,
          "the wall seen through the fog is the fog's colour, its look's (" + std::to_string(in.sum() - in_clear.sum()) + " levels lighter)");
    check(std::abs(past.sum() - past_clear.sum()) < 3.0, "the wall seen past it is as it was");
    const double in_x = fog_l.sum() > fog_r.sum() ? 0.25 : 0.75;

    // Its own colour, where it says one.
    el("mist").set("r", 0.9).set("g", 0.5).set("b", 0.2);
    shot(view);
    const Rgb tinted = at(in_x, 0.5);
    say("through it, its own colour", tinted);
    check(tinted.r > tinted.b + 50.0, "a fog with its own colour is that colour");
    el("mist").erase(sg::Key{"r"}), el("mist").erase(sg::Key{"g"}), el("mist").erase(sg::Key{"b"});

    // Thinning as it rises: at the eye's height (1.6 m up it) less of it.
    el("mist").set("fall", 1.0);
    shot(view);
    const Rgb thinned = at(in_x, 0.5);
    say("through it, thinning as it rises", thinned);
    check(thinned.sum() < in.sum() - 30.0 && thinned.sum() > in_clear.sum() + 20.0, "thinner as it rises: less of it at the eye's height, some still");
    el("mist").set("fall", 0.0);

    // Its wisps move with its world's time (`own_time` here; a drive's
    // Temporal line as well) - and with nothing else.
    el("mist").set("drift", 1.0).set("wisp", 1.5);
    field.params().set("own_time", 100.0);
    shot(view, 60);
    const std::vector<unsigned char> rested = px;
    shot(view, 1);
    int most = 0;
    apart(px, rested, &most);
    check(most == 0, "its world's time standing still, its wisps stand still (" + std::to_string(most) + " levels apart a frame on)");
    field.params().set("own_time", 130.0);
    shot(view, 60);
    const std::vector<unsigned char> later = px;
    const double moved = apart(later, rested);
    check(moved > 0.3, "half a minute on, they have moved (" + std::to_string(moved) + " levels a pixel)");
    {
        sg::render::GLWorldView fresh;
        fresh.prepare(g);
        shot(fresh, 60);
        const double fresh_apart = apart(px, later);
        check(fresh_apart < 0.5, "at a world time, the fog a fresh view draws (" + std::to_string(fresh_apart) + " levels a pixel apart)");
    }
    field.params().set("own_time", 0.0);
    el("mist").set("drift", 0.0);

    // Lit air: a lamp in the fog, the air round it brighter with the fog
    // than without (the look's `scatter`).
    look.setting(sg::passes::scene, "scatter", 0.6);
    el("sun").set(sg::keys::intensity, 0.0);
    field.light(sg::Key{"lamp"}, {-7.0, 2.6, 10.0});
    el("lamp").set(sg::keys::intensity, 6.0).set("cast", 0.0);
    el("mist").set("density", 0.0);
    shot(view, 40);
    const Rgb glow_thin = at(in_x, 0.62);
    el("mist").set("density", 0.05);
    shot(view, 40);
    const Rgb glow_thick = at(in_x, 0.62);
    say("the air by a lamp, no fog", glow_thin), say("the same, in fog", glow_thick);
    check(glow_thick.sum() > glow_thin.sum() + 15.0, "the air by a lamp in fog scatters more of its light");
    el("mist").set("density", 0.3);
    look.setting(sg::passes::scene, "scatter", 0.0);

    // The look's own air thinner up high (`uFogFalloff` from `uFogFloor`):
    // the wall high up less fogged than low down, where as thick everywhere
    // the two are fogged alike.
    el("mist").set("density", 0.0);
    look.uniform(sg::passes::scene, "uFogDensity", 0.06);
    shot(view);
    const Rgb level_low = at(0.5, 0.45), level_high = at(0.5, 0.75);
    look.uniform(sg::passes::scene, "uFogFalloff", 0.5).uniform(sg::passes::scene, "uFogFloor", 0.5);
    shot(view);
    const Rgb thin_low = at(0.5, 0.45), thin_high = at(0.5, 0.75);
    say("the look's air, low", level_low), say("and high", level_high), say("thinning up high, low", thin_low), say("and high", thin_high);
    check(std::abs(level_low.sum() - level_high.sum()) < 25.0 && thin_high.sum() < thin_low.sum() - 40.0 && thin_low.sum() > in_clear.sum() + 20.0,
          "the look's air thinning as it rises: the wall high up less fogged than low down");
    look.uniform(sg::passes::scene, "uFogDensity", 0.0).uniform(sg::passes::scene, "uFogFalloff", 0.0).uniform(sg::passes::scene, "uFogFloor", 0.0);

    // A world with no fog draws as it did.
    field.remove_element(sg::Key{"mist"});
    field.remove_element(sg::Key{"lamp"});
    el("sun").set(sg::keys::intensity, 1.0);
    shot(view);
    const double back = apart(px, clear, &most);
    check(most <= 2, "no fog, the picture is as it was (" + std::to_string(back) + " levels a pixel, " + std::to_string(most) + " at most)");

    // Its laws.
    sg::Element& bad = sg::add_fog(field, sg::Key{"bad"}, {0, 0, 5}, {2, 2, 2}, -1.0);
    bad.params.set("drift", 2.0);
    const std::vector<std::string> faults = field.faults();
    check(faults.size() == 2, "a fog thinner than none and one drifting past the most are its world's faults (" + std::to_string(faults.size()) + ")");
    for (const std::string& f : faults) std::printf("  %s\n", f.c_str());

    std::printf(ok ? "all ok\n" : "FAILED\n");
    return ok ? 0 : 1;
}
