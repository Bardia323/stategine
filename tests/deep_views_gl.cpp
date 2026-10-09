// Stategine - what is seen through a doorway seen through a doorway, against
// a real GL context.
//
// Three rooms in a row: the eye's (a), a middle one (b), a far one (c). A
// view two doorways on is the room as it is - its lit air too: the haze of
// a lamp in the far room is seen through both doorways. And a mirror in
// the middle room, seen through the first doorway, shows what is before it -
// through the doorway behind it, the eye's own room: a red wall that stands
// behind the eye, seen nowhere but in that mirror.
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
namespace sgen { void build_deep_views(sg::StateGraph&, const sg::dsl::Natives&, sg::dsl::Bindings&); }

namespace {
bool ok = true;
void check(bool c, const std::string& what) {
    std::printf("[%s] %s\n", c ? "ok" : "FAIL", what.c_str());
    ok = ok && c;
}
}  // namespace

int main() {
    const int W = 320, H = 180;
    sg::gl::Window window(W, H, "deep views");
    sg::StateGraph g;
    auto& a = g.add<sg::Spatial3D>("a");
    auto& b = g.add<sg::Spatial3D>("b");
    auto& c = g.add<sg::Spatial3D>("c");
    g.set_initial("a");
    constexpr double kQuarter = 1.5707963;

    // a: lit, the eye looking north through its doorway; behind the eye a red wall.
    a.portal("pa", {7.0, 1.25, 0.3}, 3.0, 2.5, kQuarter);
    sg::Element& lamp_a = a.light("lamp", {7.0, 2.6, 5.0});
    lamp_a.params.set("dx", 0.0).set("dy", -1.0).set("dz", 0.0).set("inner", 1.2).set("outer", 1.5).set(sg::keys::intensity, 0.35);
    sg::Element& red = a.fixture("red", 7.0, 0.0, 8.0);
    red.params.set(sg::keys::sx, 8.0).set(sg::keys::sy, 3.0).set(sg::keys::sz, 0.2).set(sg::keys::r, 0.9).set(sg::keys::g, 0.05).set(sg::keys::b, 0.05);
    sg::Element& eye = a.camera();
    eye.params.set(sg::keys::x, 7.0).set(sg::keys::y, 1.6).set(sg::keys::z, 3.7).set(sg::keys::yaw, -kQuarter).set(sg::keys::pitch, 0.0).set("fov", 70.0);

    // b: its doorway from a on its south side, its doorway on to c on its
    // north side, and a mirror standing in it facing back south.
    b.portal("pb", {7.0, 1.25, 6.0}, 3.0, 2.5, -kQuarter);
    b.portal("pc", {6.2, 1.1, 0.3}, 1.2, 2.2, kQuarter);
    sg::Element& lamp_b = b.light("lamp", {7.0, 2.6, 3.0});
    lamp_b.params.set("dx", 0.0).set("dy", -1.0).set("dz", 0.0).set("inner", 1.2).set("outer", 1.5).set(sg::keys::intensity, 0.2);
    sg::Element& mirror = b.fixture("mirror", 8.0, 0.2, 3.0);
    mirror.params.set(sg::keys::sx, 1.0).set(sg::keys::sy, 2.0).set(sg::keys::sz, 0.02).set(sg::keys::r, 0.02).set(sg::keys::g, 0.02).set(sg::keys::b, 0.02);
    mirror.params.set("reflects", 1.0).set("roughness", 0.0);

    // c: dark, its air hazy, a lamp's cone falling through it by the doorway.
    c.portal("pd", {6.2, 1.1, 11.7}, 1.2, 2.2, -kQuarter);
    sg::Element& lamp_c = c.light("lamp", {6.2, 2.8, 9.5});
    lamp_c.params.set("dx", 0.0).set("dy", -1.0).set("dz", 0.0).set("inner", 0.4).set("outer", 0.6).set(sg::keys::intensity, 3.0);
    auto& hazy = g.add<sg::LookState>("hazy");
    hazy.uniform(sg::passes::scene, "uAmbient", 0.0);
    hazy.uniform(sg::passes::scene, "uFogDensity", 0.02);
    hazy.setting(sg::passes::scene, "scatter", 0.0);
    sg::wear(g, "c", "hazy");

    sg::render::GLWorldView view;
    sg::dsl::Bindings bindings;
    sgen::build_deep_views(g, {}, bindings);
    view.prepare(g);
    view.bind_world("pa", &b, sg::portal_carry(a.element("pa"), b.element("pb")), "pb");
    view.bind_world("pb", &a, sg::portal_carry(b.element("pb"), a.element("pa")), "pa");
    view.bind_world("pc", &c, sg::portal_carry(b.element("pc"), c.element("pd")), "pd");
    view.bind_world("pd", &b, sg::portal_carry(c.element("pd"), b.element("pc")), "pc");
    const auto picture = [&] {
        for (int i = 0; i < 4; ++i) view.render(a, W, H);
        std::vector<unsigned char> px(static_cast<std::size_t>(W) * H * 3);
        sg::gl::glReadPixels(0, 0, W, H, sg::gl::GL_RGB, sg::gl::GL_UNSIGNED_BYTE, px.data());
        return px;
    };

    // The far room's lit air, through two doorways.
    const auto clear = picture();
    hazy.setting(sg::passes::scene, "scatter", 0.5);
    const auto lit_air = picture();
    double moved = 0.0;
    for (std::size_t i = 0; i < clear.size(); ++i) moved += std::abs(static_cast<int>(lit_air[i]) - static_cast<int>(clear[i]));
    moved /= static_cast<double>(clear.size() / 3);
    check(moved > 0.5, "the far room's lit air is seen through two doorways (" + std::to_string(moved) + " levels a pixel)");

    // The mirror in the middle room, seen through the first doorway, shows
    // the eye's room through the doorway behind it.
    if (const char* out = std::getenv("SG_DEEP_DUMP"))
        if (FILE* f = std::fopen(out, "wb")) {
            std::fprintf(f, "P6 %d %d 255 ", W, H);
            for (int y = H - 1; y >= 0; --y) std::fwrite(&lit_air[static_cast<std::size_t>(y) * W * 3], 1, static_cast<std::size_t>(W) * 3, f);
            std::fclose(f);
        }
    int reds = 0;
    for (std::size_t i = 0; i < lit_air.size(); i += 3)
        if (lit_air[i] > 40 && lit_air[i] > 2 * lit_air[i + 1] && lit_air[i] > 2 * lit_air[i + 2]) ++reds;
    check(reds > 40, "a mirror seen through a doorway shows, through the doorway behind it, the room the eye is in (" + std::to_string(reds) +
                         " pixels of the red wall)");
    for (const auto& why : g.validate()) std::printf("  %s\n", why.c_str());
    check(g.validate().empty(), "and the rooms are one graph, glued by their seams");
    return ok ? 0 : 1;
}
