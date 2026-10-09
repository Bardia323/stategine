// Stategine - how things are drawn, changed while they are drawn.
//
// A renderer that follows a Graphics state draws by it from the next frame:
// a setting changed through the state's own arrow (samples, shadow maps,
// glow, exposure, packing, TAA) is seen in the picture, and put back, the
// picture is what it was - nothing left over from the other settings.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "sg/gl/Window.hpp"
#include "sg/gl/World.hpp"
#include "sg/render/Graphics.hpp"
#include "sg/sg.hpp"

namespace {
bool ok = true;
void check(bool c, const std::string& what) {
    std::printf("[%s] %s\n", c ? "ok" : "FAIL", what.c_str());
    ok = ok && c;
}
double mean_difference(const std::vector<unsigned char>& a, const std::vector<unsigned char>& b) {
    double sum = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) sum += std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i]));
    return sum / static_cast<double>(a.size());
}
}  // namespace

int main() {
    const int W = 200, H = 150;
    sg::gl::Window window(W, H, "graphics");
    sg::StateGraph g;
    auto& room = g.add<sg::Spatial3D>("room");
    room.params().set("room_w", 10.0).set("room_d", 10.0).set("room_h", 4.0);
    sg::Element& lamp = room.light("lamp", {5.0, 3.5, 5.0});
    lamp.params.set(sg::keys::intensity, 2.0);
    sg::Element& crate = room.fixture(sg::Key{"crate"}, 5.0, 0.5, 5.0);
    crate.params.set(sg::keys::sx, 1.0).set(sg::keys::sy, 1.0).set(sg::keys::sz, 1.0);
    crate.params.set(sg::keys::r, 0.7).set(sg::keys::g, 0.4).set(sg::keys::b, 0.2);
    sg::Element& eye = room.camera();
    eye.params.set(sg::keys::x, 1.5).set(sg::keys::y, 1.6).set(sg::keys::z, 1.5);
    eye.params.set(sg::keys::yaw, std::atan2(3.5, 3.5)).set(sg::keys::pitch, -0.2).set("fov", 60.0);

    sg::render::Quality start;
    start.msaa = 4, start.taa = false;
    auto& gfx = g.add<sg::render::Graphics>(sg::Key{"graphics"}, start);
    g.port(gfx.id(), sg::render::Graphics::set_event());
    // (Asked of from the room - a menu, a command - as a program would.)
    g.add_functor(sg::Key{"room.graphics"}, sg::Key{"room"}, gfx.id()).on_event(sg::Key{"room.graphics"}, sg::render::Graphics::set_event());
    g.set_initial("room");
    for (const auto& why : g.validate()) std::printf("  %s\n", why.c_str());
    check(g.validate().empty(), "the settings are a state of the graph, reached from where they are asked");

    sg::render::GLWorldView view(gfx.quality());
    view.follow(&gfx);
    view.prepare(g);
    const auto picture = [&] {
        for (int i = 0; i < 4; ++i) view.render(room, W, H);
        std::vector<unsigned char> px(static_cast<std::size_t>(W) * H * 3);
        sg::gl::glReadPixels(0, 0, W, H, sg::gl::GL_RGB, sg::gl::GL_UNSIGNED_BYTE, px.data());
        return px;
    };
    const auto set = [&](sg::Params p) {
        gfx.hear(sg::Event{sg::render::Graphics::set_event(), std::move(p)});
        gfx.dispatch_pending();
    };

    const auto first = picture();
    {
        sg::Params p;
        p.set(sg::Key{"exposure"}, 3.0);
        set(std::move(p));
    }
    const auto brighter = picture();
    check(view.quality().exposure == 3.0f, "the renderer draws by what the state says now");
    check(mean_difference(first, brighter) > 2.0, "and the picture shows it (" + std::to_string(mean_difference(first, brighter)) + " levels)");

    // Every setting another way, and drawn so.
    {
        sg::Params p;
        p.set(sg::Key{"msaa"}, 0.0).set(sg::Key{"taa"}, true).set(sg::Key{"shadow_size"}, 512.0);
        p.set(sg::Key{"shadow_budget_mb"}, 64.0).set(sg::Key{"pack"}, false).set(sg::Key{"bloom_passes"}, 2.0);
        p.set(sg::Key{"instancing"}, false).set(sg::Key{"reflections"}, false);
        set(std::move(p));
    }
    const auto other = picture();
    check(view.quality().msaa == 0 && view.quality().taa && view.quality().shadow_size == 512, "samples, smoothing and shadow maps changed while drawing");
    (void)other;

    // And back as at first: the picture as it was.
    {
        sg::Params p;
        sg::render::quality_to_params(start, p);
        set(std::move(p));
    }
    const auto again = picture();
    const double back = mean_difference(first, again);
    check(back < 0.5, "put back, the picture is what it was (" + std::to_string(back) + " levels off)");
    return ok ? 0 : 1;
}
