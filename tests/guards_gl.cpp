// Stategine - the seams the photographic rooms found, closed, against a real
// GL context. Each was a mistake the engine let through; each is now either
// impossible or said, and this holds it so (docs/workflows.md section 11,
// "guarded by").
//
//   worn       what the graph says a thing wears is shown on it, with no
//              program binding it by hand; a const graph says what is not
//   far        a room made one space with another is drawn out to the far
//              room's end, whatever its own `far` says
//   skin       a picture worn by a thin card is seen whole on its face, not
//              as one cell of six
//   mouse      the frames the mouse is taken, the cursor's jump is no turn
//   cutout     a card cut out by alpha that wears a texture (whose fourth
//              channel is its height) is named
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "sg/domains/Atlas.hpp"
#include "sg/domains/Texture.hpp"
#include "sg/gl/Window.hpp"
#include "sg/gl/World.hpp"
#include "sg/sg.hpp"

namespace {
bool ok = true;
void check(bool c, const std::string& what) {
    std::printf("[%s] %s\n", c ? "ok" : "FAIL", what.c_str());
    ok = ok && c;
}
constexpr int W = 160, H = 120;

std::vector<unsigned char> read_frame() {
    std::vector<unsigned char> px(static_cast<std::size_t>(W) * H * 3);
    sg::gl::glReadPixels(0, 0, W, H, sg::gl::GL_RGB, sg::gl::GL_UNSIGNED_BYTE, px.data());
    return px;
}
// The middle of the picture: its mean colour over a few pixels.
std::array<double, 3> middle(const std::vector<unsigned char>& px, int at_x = W / 2, int at_y = H / 2) {
    std::array<double, 3> m{0, 0, 0};
    int n = 0;
    for (int y = at_y - 2; y <= at_y + 2; ++y)
        for (int x = at_x - 2; x <= at_x + 2; ++x, ++n)
            for (int c = 0; c < 3; ++c) m[c] += px[(static_cast<std::size_t>(y) * W + x) * 3 + c];
    for (double& v : m) v /= n;
    return m;
}

// A lit room with a box in it, the eye square on to it.
struct Box {
    sg::StateGraph g;
    sg::Spatial3D* room = nullptr;
    Box(double sx, double sy, double sz) {
        room = &g.add<sg::Spatial3D>("room");
        room->params().set("room_w", 10.0).set("room_d", 10.0).set("room_h", 4.0).set("ambient", 0.2);
        g.set_initial("room");
        room->light("lamp", {5.0, 3.0, 8.0}).params.set(sg::keys::intensity, 0.6).set("range", 20.0);
        sg::Element& box = room->mesh("box", 5.0, 1.0, 5.0);
        box.params.set(sg::keys::sx, sx).set(sg::keys::sy, sy).set(sg::keys::sz, sz).set(sg::keys::r, 0.8).set(sg::keys::g, 0.8).set(sg::keys::b, 0.8);
        sg::Element& eye = room->camera();
        eye.params.set(sg::keys::x, 5.0).set(sg::keys::y, 1.0 + sy * 0.5).set(sg::keys::z, 5.0 + 2.5).set(sg::keys::yaw, -1.5707963).set(sg::keys::pitch, 0.0).set("fov", 40.0);
    }
};

void worn() {
    // Green all over: a picture to wear.
    sg::Texture::define("guards.green", [](const sg::Params&, int, double, double) { return std::array<double, 4>{0.05, 0.8, 0.05, 0.5}; });
    for (const bool by_itself : {true, false}) {
        Box b(1.0, 1.0, 1.0);
        auto& paint = b.g.add<sg::Texture>("paint", 64);
        paint.element(sg::Texture::map_id()).params.set("generator", std::string("guards.green"));
        b.g.set_focus(b.g.embed("wears.box", "room", "box", paint.id(), sg::Key{}, sg::Key{}).name, false);
        sg::render::GLWorldView view;
        view.set_fixed_step(1.0 / 60.0);
        std::vector<std::string> said;
        if (by_itself) said = view.prepare(b.g);
        else said = view.prepare(static_cast<const sg::StateGraph&>(b.g));
        for (int i = 0; i < 3; ++i) view.render(*b.room, W, H);
        const auto m = middle(read_frame());
        const bool green = m[1] > 2.0 * m[0] && m[1] > 2.0 * m[2];
        const bool named = std::any_of(said.begin(), said.end(), [](const std::string& s) { return s.find("nothing bound it") != std::string::npos; });
        if (by_itself)
            check(green && !named, "a thing the graph says wears a picture is shown wearing it, no program binding it (" + std::to_string(m[0]) + " " +
                                       std::to_string(m[1]) + " " + std::to_string(m[2]) + ")");
        else
            check(!green && named, "prepared const, a picture nothing bound is named, not passed over in silence");
    }
}

void far() {
    // A room 4 m deep that sees 10 m, made one space with a hall 40 m long
    // beyond its door: a red block at the hall's far end, 40 m off.
    sg::StateGraph g;
    for (const char* n : {"near", "hall"}) {
        const bool near = std::string(n) == "near";
        auto& r = g.add<sg::Room>(sg::Key{n}, 4.0, near ? 4.0 : 40.0, 3.0);
        r.params().set("far", near ? 10.0 : 60.0).set("ambient", 0.4);
        sg::Element* hole = r.add_opening("hole", near ? 2 : 0, 2.0, 1.4, 2.4, 0.0, true);
        r.opens(hole->id, true);
        r.lay_walls();
        r.portal("door", {2.0, 1.2, near ? 4.2 : -0.2}, 1.4, 2.4, near ? -1.5707963267949 : 1.5707963267949).params.set("walk", 1.0).set("sheaf", 1.0);
        r.light("lamp", {2.0, 2.6, near ? 2.0 : 36.0}).params.set(sg::keys::intensity, 2.0).set("range", 12.0);
        if (!near) {
            sg::Element& red = r.fixture(sg::Key{"red"}, 2.0, 0.0, 38.0);
            red.params.set(sg::keys::sx, 3.0).set(sg::keys::sy, 3.0).set(sg::keys::sz, 0.4).set(sg::keys::r, 0.9).set(sg::keys::g, 0.02).set(sg::keys::b, 0.02).set("emissive", 1.0);
        }
    }
    g.set_initial("near");
    sg::walkway(g, "doorway", "near", "door", "hall", "door");
    auto& near = dynamic_cast<sg::Spatial3D&>(g.state("near"));
    near.camera().params.set(sg::keys::x, 2.0).set(sg::keys::y, 1.2).set(sg::keys::z, 1.0).set(sg::keys::yaw, 1.5707963).set(sg::keys::pitch, 0.0).set("fov", 30.0);
    check(sg::sheafify(g, sg::Key{"near"}).size() == 2, "(the room and the hall are one space)");
    sg::render::GLWorldView view;
    view.set_fixed_step(1.0 / 60.0);
    view.prepare(g);
    for (int i = 0; i < 3; ++i) view.render(near, W, H);
    const auto m = middle(read_frame());
    check(m[0] > 60.0 && m[0] > 1.5 * m[1], "a hall made one space with a room is drawn to its far end, past where the room's own sight (`far`) ends (" +
                                               std::to_string(m[0]) + " " + std::to_string(m[1]) + " " + std::to_string(m[2]) + ")");
}

// A picture: green in its middle, red round it.
struct Middle : sg::Surface2D {
    using Surface2D::Surface2D;
    void paint() override {
        for (int y = 0; y < px_h(); ++y)
            for (int x = 0; x < px_w(); ++x) {
                const bool mid = std::fabs((x + 0.5) / px_w() - 0.5) < 0.2 && std::fabs((y + 0.5) / px_h() - 0.5) < 0.2;
                put(x, y, mid ? 10 : 220, mid ? 220 : 10, 10);
            }
    }
};

void skin() {
    // A card 2 mm thick wearing a picture: its face shows the whole of it,
    // its middle green - not a sixth of it, the corner of an atlas.
    Box b(1.0, 1.0, 0.002);
    auto& pic = b.g.add<Middle>("pic", 4, 4, 16);
    b.g.set_focus(b.g.embed("wears.card", "room", "box", pic.id(), sg::Key{}, sg::Key{}).name, false);
    sg::render::GLWorldView view;
    view.set_fixed_step(1.0 / 60.0);
    view.prepare(b.g);
    for (int i = 0; i < 3; ++i) view.render(*b.room, W, H);
    const auto m = middle(read_frame());
    check(m[1] > 2.0 * m[0], "a picture worn by a thin card is seen whole on its face (its middle: " + std::to_string(m[0]) + " " + std::to_string(m[1]) + " " +
                                 std::to_string(m[2]) + ")");
}

void probes() {
    // A hall 40 m by 40 asking its light from all round in boxes a metre
    // across: more than the scene shader takes - said, not passed over.
    sg::StateGraph g;
    auto& r = g.add<sg::Spatial3D>("hall");
    r.params().set("room_w", 40.0).set("room_d", 40.0).set("room_h", 4.0).set("probe_cell", 1.0);
    g.set_initial("hall");
    sg::render::GLWorldView view;
    const auto said = view.prepare(g);
    check(std::any_of(said.begin(), said.end(), [](const std::string& s) { return s.find("coarser than asked") != std::string::npos; }),
          "a room whose light from all round is clamped far coarser than its `probe_cell` asks is named");
    r.params().set("room_w", 6.0).set("room_d", 6.0);
    sg::render::GLWorldView fine;
    const auto none = fine.prepare(g);
    check(std::none_of(none.begin(), none.end(), [](const std::string& s) { return s.find("coarser than asked") != std::string::npos; }), "and one that has what it asks is not");
}

void cutout() {
    // A card cut out by its picture's alpha, wearing a texture: a texture's
    // fourth channel is its height - said, not cut silently whole.
    Box b(1.0, 1.0, 0.002);
    b.room->element(sg::Key{"box"}).params.set("cutout", 0.5);
    auto& paint = b.g.add<sg::Texture>("paint", 64);
    paint.element(sg::Texture::map_id()).params.set("generator", std::string("guards.green"));
    b.g.set_focus(b.g.embed("wears.box", "room", "box", paint.id(), sg::Key{}, sg::Key{}).name, false);
    sg::render::GLWorldView view;
    const auto said = view.prepare(b.g);
    check(std::any_of(said.begin(), said.end(), [](const std::string& s) { return s.find("nothing is cut") != std::string::npos; }),
          "a thing cut out by its picture's alpha that wears a texture (whose alpha is its height) is named");
    // And glass that wears a picture: seen through, the picture lost - named.
    b.room->element(sg::Key{"box"}).params.set("glass", 0.82);
    sg::render::GLWorldView again;
    const auto glass = again.prepare(b.g);
    check(std::any_of(glass.begin(), glass.end(), [](const std::string& s) { return s.find("is glass") != std::string::npos; }),
          "a thing of glass that wears a picture is named: is it glass?");
}

void mouse(sg::gl::Window& w) {
    // The cursor somewhere; the mouse taken, and the cursor jumps (to the
    // middle, as a capture puts it): no turn read from the jump.
    w.poll();
    glfwSetCursorPos(w.handle(), 10.0, 10.0);
    w.poll(), w.poll(), w.poll(), w.poll();
    w.capture_mouse(true);
    glfwSetCursorPos(w.handle(), 140.0, 90.0);
    double seen = 0.0;
    for (int i = 0; i < 3; ++i) w.poll(), seen += std::fabs(w.mouse_dx()) + std::fabs(w.mouse_dy());
    check(seen == 0.0, "the frames the mouse is taken read no movement: the cursor's jump is not a turn (" + std::to_string(seen) + ")");
    glfwSetCursorPos(w.handle(), 150.0, 90.0);
    w.poll();
    check(std::fabs(w.mouse_dx() - 10.0) < 1e-6, "and after them the mouse moved is read as it moved");
    w.capture_mouse(false);
}

}  // namespace

int main() {
    sg::gl::Window window(W, H, "guards");
    mouse(window);
    worn();
    far();
    skin();
    cutout();
    probes();
    return ok ? 0 : 1;
}
