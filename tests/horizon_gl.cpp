// Stategine - a terrain's horizon toward the sun, against a real GL context.
//
// Land with a ridge 120 m high across it, a low sun behind the ridge, far
// past the sun's own shadow map: the ground 420 m behind the ridge is in the
// ridge's shadow, the ground beside it (where the ridge has ended) is not;
// the sun high, both are lit alike; a terrain that says `horizon` 0 has
// none. The horizon is made again only as the sun's bearing turns: not as
// the eye turns, nor as the sun climbs.
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
}  // namespace

int main() {
    const int W = 320, H = 180;
    sg::gl::Window window(W, H, "horizon");
    sg::StateGraph g;
    auto& field = g.add<sg::Spatial3D>("field");
    g.set_initial("field");
    field.params().set("sky", 1.0).set("far", 400.0);
    // A ridge along z at x 400, 120 m high, ending past |z| 150.
    field.terrain(sg::Key{"land"}, [](double x, double z) {
        const double across = (x - 400.0) / 40.0, along = 1.0 / (1.0 + std::exp((std::fabs(z) - 150.0) / 10.0));
        return 120.0 * std::exp(-across * across) * along;
    });
    field.find(sg::Key{"land"})->params.set("r", 0.5).set("g", 0.5).set("b", 0.5);
    field.light(sg::Key{"sun"}, {0.0, 50.0, 0.0});
    const auto el = [&](const char* id) -> sg::Params& { return field.find(sg::Key{id})->params; };
    el("sun").set("sun", 1.0).set(sg::keys::intensity, 1.0);
    // From +x, `up` (radians) over the horizon, `turn` round from there.
    const auto sun_at = [&](double up, double turn = 0.0) {
        el("sun").set("dx", -std::cos(up) * std::cos(turn)).set("dy", -std::sin(up)).set("dz", -std::cos(up) * std::sin(turn));
    };
    sg::Element& eye = field.camera();
    eye.params.set(sg::keys::yaw, 0.0).set(sg::keys::pitch, -1.5);

    sg::render::GLWorldView view;
    view.prepare(g);
    std::vector<unsigned char> px(static_cast<std::size_t>(W) * H * 3);
    // How bright the ground straight under the eye is, 30 m over (x, z).
    const auto ground = [&](double x, double z) {
        eye.params.set(sg::keys::x, x).set(sg::keys::y, 30.0).set(sg::keys::z, z);
        for (int i = 0; i < 8; ++i) {
            view.render(field, W, H);
            window.swap();
        }
        sg::gl::glReadPixels(0, 0, W, H, sg::gl::GL_RGB, sg::gl::GL_UNSIGNED_BYTE, px.data());
        double sum = 0.0;
        int n = 0;
        for (int j = H / 2 - 6; j <= H / 2 + 6; ++j)
            for (int i = W / 2 - 6; i <= W / 2 + 6; ++i) {
                const unsigned char* p = &px[(static_cast<std::size_t>(j) * W + i) * 3];
                sum += p[0] + p[1] + p[2], ++n;
            }
        return sum / n;
    };

    sun_at(0.2);  // (its tangent 0.2: the ridge, 120 m high 420 m off, rises 0.29 toward it)
    const double behind = ground(-20.0, 0.0), beside = ground(-20.0, 300.0);
    std::printf("low sun: behind the ridge %.0f, beside it %.0f\n", behind, beside);
    check(behind < 0.6 * beside, "a low sun behind a ridge leaves the ground behind it in shadow");
    sun_at(1.0);
    const double high_behind = ground(-20.0, 0.0), high_beside = ground(-20.0, 300.0);
    std::printf("high sun: behind %.0f, beside %.0f\n", high_behind, high_beside);
    check(std::fabs(high_behind - high_beside) < 0.03 * high_beside, "a high one lights both alike");

    // Made again only as the sun's bearing turns.
    int made = 0;
    for (int i = 0; i < 10; ++i) {
        eye.params.set(sg::keys::yaw, 0.1 * i);
        view.render(field, W, H);
        made += view.times().horizons_made;
    }
    check(made == 0, "turning the eye makes no horizon again (" + std::to_string(made) + ")");
    made = 0;
    for (int i = 0; i < 10; ++i) {
        sun_at(0.2 + 0.05 * i);
        view.render(field, W, H);
        made += view.times().horizons_made;
    }
    check(made == 0, "nor the sun climbing (" + std::to_string(made) + ")");
    sun_at(0.2, 0.3);
    view.render(field, W, H);
    check(view.times().horizons_made == 1, "the sun's bearing turning makes it again");
    eye.params.set(sg::keys::yaw, 0.0);

    // None where the terrain says none.
    sun_at(0.2);
    el("land").set("horizon", 0.0);
    const double none_behind = ground(-20.0, 0.0), none_beside = ground(-20.0, 300.0);
    std::printf("no horizon: behind %.0f, beside %.0f\n", none_behind, none_beside);
    check(std::fabs(none_behind - none_beside) < 0.03 * none_beside, "a terrain that says `horizon` 0 has none");

    // (SG_HORIZON_SHOT=file keeps a look over the land at a low sun there.)
    if (const char* path = std::getenv("SG_HORIZON_SHOT")) {
        el("land").set("horizon", 640.0);
        sun_at(0.15, 0.2);
        eye.params.set(sg::keys::x, -60.0).set(sg::keys::y, 80.0).set(sg::keys::z, 40.0).set(sg::keys::yaw, 0.3).set(sg::keys::pitch, -0.45);
        for (int i = 0; i < 8; ++i) view.render(field, W, H), window.swap();
        sg::gl::glReadPixels(0, 0, W, H, sg::gl::GL_RGB, sg::gl::GL_UNSIGNED_BYTE, px.data());
        if (FILE* f = std::fopen(path, "wb")) {
            std::fprintf(f, "P6 %d %d 255\n", W, H);
            for (int j = H - 1; j >= 0; --j) std::fwrite(&px[static_cast<std::size_t>(j) * W * 3], 1, static_cast<std::size_t>(W) * 3, f);
            std::fclose(f);
        }
    }

    std::printf(ok ? "all ok\n" : "FAILED\n");
    return ok ? 0 : 1;
}
