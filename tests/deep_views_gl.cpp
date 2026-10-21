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
    // (b's lens: none, until a wide one is tried below.)
    auto& bowed = g.add<sg::LookState>("bowed");
    bowed.uniform(sg::passes::composite, "uBarrel", 0.0);
    sg::wear(g, "b", "bowed");

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

    // A window in its own look draws only the part of the screen it is seen
    // in: what is seen through it is what a view through it shows - near,
    // far (a small window), and as the eye moves - and none of it is left
    // undrawn. (Its own picture, developed in its own look and taken back
    // through the tone curve, differs from a view's by a few levels.)
    hazy.setting(sg::passes::scene, "scatter", 0.0);
    const auto apart = [](const std::vector<unsigned char>& x, const std::vector<unsigned char>& y) {
        double d = 0.0;
        for (std::size_t i = 0; i < x.size(); ++i) d += std::abs(static_cast<int>(x[i]) - static_cast<int>(y[i]));
        return d / static_cast<double>(x.size() / 3);
    };
    const auto black = [](const std::vector<unsigned char>& x) {
        int n = 0;
        for (std::size_t i = 0; i < x.size(); i += 3) n += x[i] + x[i + 1] + x[i + 2] == 0;
        return n;
    };
    for (const auto& [z, x] : std::vector<std::pair<double, double>>{{3.7, 7.0}, {5.6, 7.0}, {5.6, 6.2}}) {
        eye.params.set(sg::keys::z, z).set(sg::keys::x, x);
        a.element("pa").params.set("own_look", 0.0);
        const auto viewed = picture();
        a.element("pa").params.set("own_look", 1.0);
        const auto own = picture();
        a.element("pa").params.set("own_look", 0.0);
        const double d = apart(viewed, own);
        const int holes = black(own) - black(viewed);
        check(d < 6.0 && holes <= 0, "a window in its own look, the eye at z " + std::to_string(z) + " x " + std::to_string(x) +
                                           ", shows what a view through it shows (" + std::to_string(d) + " levels a pixel apart, " +
                                           std::to_string(holes) + " pixels more of nothing)");
    }

    // A window in its own look whose world's lens is a wide one: the lens is
    // the eye's, laid on the whole picture once - the picture through the
    // window is not bowed again inside it (nor reaches past the part of the
    // screen it was drawn in).
    {
        eye.params.set(sg::keys::z, 3.7).set(sg::keys::x, 7.0);
        a.element("pa").params.set("own_look", 1.0);
        const auto straight = picture();
        bowed.uniform(sg::passes::composite, "uBarrel", 0.3);
        // (A step aside and back: the window's picture drawn again, not held.)
        eye.params.set(sg::keys::x, 7.05);
        picture();
        eye.params.set(sg::keys::x, 7.0);
        const auto wide = picture();
        a.element("pa").params.set("own_look", 0.0);
        bowed.uniform(sg::passes::composite, "uBarrel", 0.0);
        const double d = apart(straight, wide);
        check(d < 0.05, "a window in its own look, its world's lens a wide one: the picture through it bowed only by the eye's lens (" +
                            std::to_string(d) + " levels a pixel apart)");
    }

    // A doorway the eye cannot see (Quality::skip_unseen): a screen stood
    // between the eye and it. Its view is asked of what stands before it and
    // left out by the card - the picture is the picture all the same, and
    // with the screen gone the view is drawn as ever.
    eye.params.set(sg::keys::z, 3.7).set(sg::keys::x, 7.0);
    sg::render::GLQuality asking = view.quality(), drawing = view.quality();
    asking.skip_unseen = true, drawing.skip_unseen = false;
    sg::Element& blocker = a.fixture("blocker", 7.0, 0.0, 2.0);
    blocker.params.set(sg::keys::sx, 6.0).set(sg::keys::sy, 3.0).set(sg::keys::sz, 0.3);
    for (const bool hidden : {true, false}) {
        blocker.alive = hidden;
        view.set_quality(drawing);
        const auto drawn = picture();
        view.set_quality(asking);
        const auto asked_picture = picture();
        const int asked = view.times().views_asked;
        const double d = apart(drawn, asked_picture);
        check(asked > 0 && d < 0.05, std::string(hidden ? "a doorway hidden" : "a doorway in sight") + ": its view asked (" + std::to_string(asked) +
                                         "), the picture as it is with every view drawn (" + std::to_string(d) + " levels a pixel apart)");
    }
    blocker.alive = false;
    // A doorway behind the eye that a mirror before it shows: never asked
    // (the mirror's eye sees it), so its view is in the glass as ever.
    {
        sg::Element& glass = a.fixture("glass", 7.0, 0.2, 6.2);
        glass.params.set(sg::keys::sx, 4.0).set(sg::keys::sy, 2.6).set(sg::keys::sz, 0.02).set(sg::keys::r, 0.02).set(sg::keys::g, 0.02)
            .set(sg::keys::b, 0.02).set("reflects", 1.0).set("roughness", 0.0);
        eye.params.set(sg::keys::z, 3.7).set(sg::keys::yaw, kQuarter);
        view.set_quality(drawing);
        const auto drawn = picture();
        view.set_quality(asking);
        const auto asked_picture = picture();
        const double d = apart(drawn, asked_picture);
        check(d < 0.05, "a doorway behind the eye that a mirror shows: in the glass as ever (" + std::to_string(d) + " levels a pixel apart)");
        glass.alive = false;
        eye.params.set(sg::keys::yaw, -kQuarter);
    }
    for (const auto& why : g.validate()) std::printf("  %s\n", why.c_str());
    check(g.validate().empty(), "and the rooms are one graph, glued by their seams");
    return ok ? 0 : 1;
}
