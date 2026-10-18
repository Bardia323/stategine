// Stategine - a sky as a planet's, against a real GL context (a look's
// `atmosphere`).
//
// A field under the sky, two pale blocks in it, one near and one far. Its
// look says its sky is a planet's: at noon the sky is blue overhead and
// paler at the horizon; at sunset, low towards the sun, it is red, and dark
// overhead; the far block is seen through more air than the near one, and
// is the nearer the sky's colour for it. The sky's pictures are made again
// only when the sun or the eye's height moves. Clouds as a volume
// (`clouds.cover`): overhead, a clear sky is clouded over - less blue - and
// the ground under them is in their shadow; turning the eye never makes
// their dome whole again; their time is the world's alone - still when it
// is, moved when it goes on, and at a world time the same as a fresh view's. A look that says `atmosphere` and `clouds.cover` 0
// draws exactly as one that says nothing of them.
#include <cmath>
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
struct Rgb {
    double r = 0, g = 0, b = 0;
};
}  // namespace

int main() {
    const int W = 480, H = 270;
    sg::gl::Window window(W, H, "sky");
    sg::StateGraph g;
    auto& field = g.add<sg::Spatial3D>("field");
    g.set_initial("field");
    field.params().set("sky", 1.0).set("far", 400.0);
    sg::Element& sun = field.light(sg::Key{"sun"}, {0.0, 50.0, 0.0});
    sun.params.set("sun", 1.0).set("r", 1.0).set("g", 1.0).set("b", 1.0).set(sg::keys::intensity, 1.0).set("cast", 0.0);
    // Towards +z, as high as `up` (radians) over the horizon.
    const auto sun_at = [&](double up) { sun.params.set("dx", 0.0).set("dy", -std::sin(up)).set("dz", -std::cos(up)); };
    for (const auto& [name, z] : {std::pair<const char*, double>{"near", 12.0}, {"far", 300.0}}) {
        sg::Element& b = field.fixture(sg::Key{name}, 0.0, 0.0, z);
        const double s = z / 12.0;  // (as big on the screen as each other)
        b.params.set(sg::keys::sx, 1.0 * s).set(sg::keys::sy, 4.0 * s).set(sg::keys::sz, 0.2 * s).set("r", 0.8).set("g", 0.8).set("b", 0.8);
    }
    auto& earth = g.add<sg::LookState>("earth");
    // (No fog of the look's own, so what the far block is seen through is the planet's air.)
    earth.uniform(sg::passes::scene, "uFogDensity", 0.0).setting(sg::passes::scene, "atmosphere", 1.0).setting(sg::passes::scene, "atmosphere.scale", 0.02);
    sg::wear(g, "field", "earth");
    sg::Element& eye = field.camera();

    sg::render::GLWorldView view;
    view.prepare(g);
    std::vector<unsigned char> px(static_cast<std::size_t>(W) * H * 3);
    const auto shot = [&](double yaw, double pitch, int frames = 12) {
        eye.params.set(sg::keys::x, 0.0).set(sg::keys::y, 1.6).set(sg::keys::z, 0.0).set(sg::keys::yaw, yaw).set(sg::keys::pitch, pitch);
        for (int i = 0; i < frames; ++i) {
            view.render(field, W, H);
            window.swap();
        }
        sg::gl::glReadPixels(0, 0, W, H, sg::gl::GL_RGB, sg::gl::GL_UNSIGNED_BYTE, px.data());
        // (SG_SKY_SHOTS=dir keeps each picture there, to be looked at.)
        static int kept = 0;
        if (const char* dir = std::getenv("SG_SKY_SHOTS")) {
            const std::string path = std::string(dir) + "/sky" + std::to_string(kept++) + ".ppm";
            if (FILE* f = std::fopen(path.c_str(), "wb")) {
                std::fprintf(f, "P6 %d %d 255\n", W, H);
                for (int j = H - 1; j >= 0; --j) std::fwrite(&px[static_cast<std::size_t>(j) * W * 3], 1, static_cast<std::size_t>(W) * 3, f);
                std::fclose(f);
            }
        }
    };
    // The average of a patch of the picture, `x`, `y` its middle as parts of
    // the picture across and up.
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
    const double ahead = 1.5707963;  // towards +z, where the sun is

    sun_at(1.1);
    shot(ahead, 1.45);
    const Rgb noon_up = at(0.5, 0.5);
    shot(ahead + 3.14159265, 0.02);
    const Rgb noon_low = at(0.5, 0.6);
    say("noon, overhead", noon_up), say("noon, the horizon", noon_low);
    check(noon_up.b > noon_up.r * 1.3 && noon_up.b > noon_up.g, "at noon the sky overhead is blue");
    check(noon_low.r + noon_low.g + noon_low.b > noon_up.r + noon_up.g + noon_up.b && noon_low.r / noon_low.b > noon_up.r / noon_up.b,
          "and paler at the horizon");

    sun_at(0.03);
    shot(ahead, 0.04);
    const Rgb dusk_low = at(0.3, 0.55);
    shot(ahead, 1.45);
    const Rgb dusk_up = at(0.5, 0.5);
    say("sunset, towards the sun", dusk_low), say("sunset, overhead", dusk_up);
    check(dusk_low.r > dusk_low.b * 1.2, "at sunset the sky low towards the sun is red");
    check(dusk_up.r + dusk_up.g + dusk_up.b < noon_up.r + noon_up.g + noon_up.b, "and darker overhead than at noon");

    // The blocks: the far one through 6 km of air (300 m at 20 m a metre),
    // the near one through a quarter of one - each against the sky beside it.
    const auto apart = [](const Rgb& a, const Rgb& b) { return std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b); };
    sun_at(0.8);
    shot(ahead, 0.0);
    const Rgb near = at(0.5, 0.62), beside_near = at(0.3, 0.62);
    // (The near one moved out of the way of the far.)
    sg::Element& near_block = *field.find(sg::Key{"near"});
    near_block.params.set(sg::keys::x, 40.0);
    shot(ahead, 0.0);
    const Rgb far = at(0.5, 0.62), beside_far = at(0.3, 0.62);
    near_block.params.set(sg::keys::x, 0.0);
    say("the near block", near), say("the far block", far), say("the sky beside it", beside_far);
    check(apart(far, beside_far) < 0.6 * apart(near, beside_near), "the far block is nearer the sky's colour, seen through more air");
    check(far.b > far.r && apart(far, beside_far) > 10.0, "and bluer, and still to be seen");

    // Made again only when the sun or the eye's height moves.
    shot(ahead, 0.3, 4);
    int made = 0;
    for (int i = 0; i < 10; ++i) {
        eye.params.set(sg::keys::yaw, ahead + 0.01 * i);
        view.render(field, W, H);
        made += view.times().skies_made;
    }
    check(made == 0, "turning the eye makes no sky again (" + std::to_string(made) + " made)");
    sun_at(0.81);
    view.render(field, W, H);
    check(view.times().skies_made == 1, "the sun moving makes it again");

    // Clouds as a volume, at noon: overhead, clear and then clouded over.
    sun_at(1.3);
    earth.setting(sg::passes::scene, "clouds.cover", 0.0);
    shot(ahead, 1.45);
    const Rgb clear_up = at(0.5, 0.5);
    earth.setting(sg::passes::scene, "clouds.cover", 0.95);
    shot(ahead, 1.45);
    const Rgb cloudy_up = at(0.5, 0.5);
    say("overhead, clear", clear_up), say("overhead, clouded over", cloudy_up);
    check(cloudy_up.b - cloudy_up.r < 0.6 * (clear_up.b - clear_up.r), "clouds over a clear sky: less blue overhead");
    // Turning under them makes no dome whole again (a part of it is made each frame).
    int domes = 0;
    for (int i = 0; i < 10; ++i) {
        eye.params.set(sg::keys::yaw, ahead + 0.05 * i);
        view.render(field, W, H);
        domes += view.times().domes_made;
    }
    check(domes == 0, "turning under the clouds makes no dome whole again (" + std::to_string(domes) + " made)");
    // Their time is the world's (`own_time` here; a drive's Temporal line
    // as well): standing still, they stand still - the picture rests, frame
    // after frame; gone on a minute, they have moved, the dome made whole for
    // the jump; and what is drawn at a world time is what a fresh view draws
    // at it, whatever frames came before.
    earth.setting(sg::passes::scene, "clouds.cover", 0.5);
    field.params().set("own_time", 100.0);
    shot(ahead, 1.2, 90);
    const std::vector<unsigned char> rested = px;
    shot(ahead, 1.2, 1);
    int stirred = 0;
    for (std::size_t i = 0; i < px.size(); ++i) stirred = std::max(stirred, std::abs(static_cast<int>(px[i]) - static_cast<int>(rested[i])));
    check(stirred == 0, "world time standing still, the clouds stand still (" + std::to_string(stirred) + " levels apart a frame on)");
    field.params().set("own_time", 160.0);
    view.render(field, W, H);
    check(view.times().domes_made == 1, "a minute of world time on at once makes the dome whole");
    shot(ahead, 1.2, 90);
    const std::vector<unsigned char> later = px;
    double moved_by = 0.0;
    for (std::size_t i = 0; i < px.size(); ++i) moved_by += std::abs(static_cast<int>(px[i]) - static_cast<int>(rested[i]));
    moved_by /= static_cast<double>(px.size());
    check(moved_by > 1.0, "a minute on, the wind has moved them (" + std::to_string(moved_by) + " levels a pixel)");
    {
        sg::render::GLWorldView fresh_view;
        fresh_view.prepare(g);
        for (int i = 0; i < 90; ++i) fresh_view.render(field, W, H), window.swap();
        sg::gl::glReadPixels(0, 0, W, H, sg::gl::GL_RGB, sg::gl::GL_UNSIGNED_BYTE, px.data());
        double apart_by = 0.0;
        for (std::size_t i = 0; i < px.size(); ++i) apart_by += std::abs(static_cast<int>(px[i]) - static_cast<int>(later[i]));
        apart_by /= static_cast<double>(px.size());
        check(apart_by < 1.0, "at a world time, the clouds a fresh view draws (" + std::to_string(apart_by) + " levels a pixel apart)");
    }
    field.params().set("own_time", 0.0);  // (as it was: a state that says no time is at 0)

    // The ground under them, in the open and then in their shadow (the sun
    // dimmed so the ground is not all white).
    sg::Element& ground = field.fixture(sg::Key{"ground"}, 0.0, -0.1, 0.0);
    ground.params.set(sg::keys::sx, 400.0).set(sg::keys::sy, 0.2).set(sg::keys::sz, 400.0).set("r", 0.35).set("g", 0.45).set("b", 0.25);
    field.find(sg::Key{"sun"})->params.set(sg::keys::intensity, 0.08);
    earth.setting(sg::passes::scene, "clouds.cover", 0.0);
    shot(ahead, -0.6);
    const Rgb open = at(0.5, 0.3);
    earth.setting(sg::passes::scene, "clouds.cover", 0.95);
    shot(ahead, -0.6);
    const Rgb shaded = at(0.5, 0.3);
    say("the ground in the open", open), say("the ground under the clouds", shaded);
    check(shaded.g < 0.6 * open.g, "the ground under a clouded sky is in their shadow");
    field.find(sg::Key{"sun"})->params.set(sg::keys::intensity, 1.0);

    // The air lit by the sun (the look's `scatter`), seen over black ground:
    // under a clouded sky, their shadow falls through it too - less of it
    // lit. (What it adds, scatter on against off, clear and clouded.) And it
    // rests while the world's time stands still.
    ground.params.set("r", 0.0).set("g", 0.0).set("b", 0.0).set("roughness", 1.0);
    earth.setting(sg::passes::scene, "atmosphere", 0.0);  // (the planet's air aside: the lamps' alone)
    const auto air_added = [&](double cover) {
        earth.setting(sg::passes::scene, "clouds.cover", cover);
        earth.setting(sg::passes::scene, "scatter", 0.0);
        shot(ahead, -0.8, 40);
        const Rgb without = at(0.5, 0.3);
        earth.setting(sg::passes::scene, "scatter", 0.02);
        shot(ahead, -0.8, 40);
        const Rgb with = at(0.5, 0.3);
        return (with.r + with.g + with.b) - (without.r + without.g + without.b);
    };
    const double air_clear = air_added(0.0), air_clouded = air_added(0.95);
    std::printf("the sun's light in the air: clear %.1f, under the clouds %.1f\n", air_clear, air_clouded);
    // (What the air adds is measured after the picture is toned, which
    // gives more levels to what it adds over the darker, clouded ground: the
    // same light in both would come out more under the clouds.)
    check(air_clear > 5.0 && air_clouded < 0.8 * air_clear, "the clouds' shadow falls through the lit air too");
    field.params().set("own_time", 300.0);
    earth.setting(sg::passes::scene, "clouds.cover", 0.5);
    shot(ahead, -0.25, 120);
    const std::vector<unsigned char> air_rested = px;
    shot(ahead, -0.25, 1);
    int air_stirred = 0;
    for (std::size_t i = 0; i < px.size(); ++i) air_stirred = std::max(air_stirred, std::abs(static_cast<int>(px[i]) - static_cast<int>(air_rested[i])));
    check(air_stirred == 0, "its world's time standing still, the lit air under the clouds rests (" + std::to_string(air_stirred) + " levels a frame on)");
    field.params().set("own_time", 0.0);
    earth.setting(sg::passes::scene, "scatter", 0.0);
    earth.setting(sg::passes::scene, "atmosphere", 1.0);
    ground.params.set(sg::keys::x, 5000.0);

    // A look that says `atmosphere` and `clouds.cover` 0 draws as one that
    // says nothing of them.
    earth.setting(sg::passes::scene, "clouds.cover", 0.0);
    earth.setting(sg::passes::scene, "atmosphere", 0.0);
    shot(ahead, 0.1, 20);
    const std::vector<unsigned char> off = px;
    auto& plain = g.add<sg::LookState>("plain");
    plain.uniform(sg::passes::scene, "uFogDensity", 0.0);
    sg::wear(g, "field", "plain");
    view.prepare(g);
    shot(ahead, 0.1, 20);
    int most = 0;
    for (std::size_t i = 0; i < px.size(); ++i) most = std::max(most, std::abs(static_cast<int>(px[i]) - static_cast<int>(off[i])));
    check(most == 0, "a look saying `atmosphere` and `clouds.cover` 0 draws as one saying nothing of them (" + std::to_string(most) + " levels at most apart)");
    return ok ? 0 : 1;
}
