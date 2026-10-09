// Stategine - no two surfaces fight, against a real GL context.
//
// A green plate lies flush in the face of a red wall - its face in the
// wall's, to the last bit - and is looked at square on and edge on, from two
// metres to forty: wherever the two coincide the smaller (the plate) is seen,
// every pixel, every view. And what stands a few centimetres behind the wall
// does not show through it, near or far. And seen in a mirror - whose eye is
// as far behind the glass as the eye is before it, and so sees the wall from
// farther off, more edge on - the plate is seen just the same.
#include <algorithm>
#include <cmath>
#include <cstdio>
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
    const int W = 240, H = 240;
    sg::gl::Window window(W, H, "coplanar");
    sg::StateGraph g;
    auto& room = g.add<sg::Spatial3D>("room");
    room.params().set("room_w", 120.0).set("room_d", 120.0).set("room_h", 20.0);
    g.set_initial("room");
    sg::Element& sun = room.light("sun", {60.0, 18.0, 90.0});
    sun.params.set("dx", 0.0).set("dy", -0.3).set("dz", -1.0).set(sg::keys::intensity, 1.5);
    const auto box = [&](const std::string& id, double x, double y, double z, double sx, double sy, double sz, double r, double gr, double b) -> sg::Element& {
        sg::Element& e = room.fixture(sg::Key{id}, x, y, z);
        e.params.set(sg::keys::sx, sx).set(sg::keys::sy, sy).set(sg::keys::sz, sz);
        e.params.set(sg::keys::r, r).set(sg::keys::g, gr).set(sg::keys::b, b).set("roughness", 0.9);
        return e;
    };
    // The wall's face at z = 60.1; the plate's face too.
    box("wall", 60.0, 0.0, 60.0, 8.0, 6.0, 0.2, 0.8, 0.05, 0.05);
    box("plate", 60.0, 2.0, 60.09, 1.0, 1.0, 0.02, 0.05, 0.8, 0.05);
    // Four centimetres behind the wall's back, a yellow block.
    box("hidden", 62.5, 2.0, 59.6, 0.3, 1.0, 0.3, 0.9, 0.9, 0.05);
    sg::Element& eye = room.camera();
    sg::render::GLWorldView view;
    std::printf("reversed depth: %d\n", sg::gl::reversed_depth() ? 1 : 0);
    view.prepare(g);
    const auto look = [&](double dist, double turn, double at_x) {
        // On the plate's middle (or a point of the wall), from `dist`, `turn` round from square on.
        const double tx = at_x, ty = 2.5, tz = 60.1;
        const double ex = tx + std::sin(turn) * dist, ez = tz + std::cos(turn) * dist;
        eye.params.set(sg::keys::x, ex).set(sg::keys::y, ty).set(sg::keys::z, ez);
        eye.params.set(sg::keys::yaw, std::atan2(tz - ez, tx - ex)).set(sg::keys::pitch, 0.0).set("fov", 30.0);
        for (int i = 0; i < 3; ++i) view.render(room, W, H);
        std::vector<unsigned char> px(static_cast<std::size_t>(W) * H * 3);
        sg::gl::glReadPixels(0, 0, W, H, sg::gl::GL_RGB, sg::gl::GL_UNSIGNED_BYTE, px.data());
        return px;
    };
    int fights = 0, through = 0, views = 0;
    for (const double dist : {2.0, 5.0, 10.0, 20.0, 40.0})
        for (const double turn : {0.0, 0.6, 1.1, 1.35}) {
            // The plate's middle: a little square of pixels, every one green.
            const auto px = look(dist, turn, 60.0);
            // (Only where the plate surely is: half its seen width and height, at most three pixels.)
            const double px_per_m = (H / 2.0) / (dist * std::tan(15.0 * 3.14159265 / 180.0));
            const int hx = std::min(3, static_cast<int>(0.5 * std::cos(turn) * px_per_m * 0.5)), hy = std::min(3, static_cast<int>(0.25 * px_per_m));
            int here = 0;
            for (int y = H / 2 - hy; y <= H / 2 + hy; ++y)
                for (int x = W / 2 - hx; x <= W / 2 + hx; ++x) {
                    const unsigned char* p = &px[(static_cast<std::size_t>(y) * W + x) * 3];
                    if (p[0] > p[1]) ++here;
                }
            if (here) std::printf("  from %.0f m, %.2f round: %d pixels of the wall\n", dist, turn, here);
            fights += here;
            // The wall in front of the yellow block: never yellow.
            if (dist >= 5.0) {
                const auto behind = look(dist, turn * 0.5, 62.5);
                for (int y = H / 2 - 3; y <= H / 2 + 3; ++y)
                    for (int x = W / 2 - 3; x <= W / 2 + 3; ++x) {
                        const unsigned char* p = &behind[(static_cast<std::size_t>(y) * W + x) * 3];
                        if (p[1] > p[0] * 0.7 && p[1] > 40) ++through;
                    }
            }
            ++views;
        }
    check(fights == 0, "where a plate lies flush in a wall, the plate is seen - every pixel, from two metres to forty, square on to edge on (" +
                           std::to_string(fights) + " pixels of the wall)");
    check(through == 0, "and what stands just behind the wall does not show through it (" + std::to_string(through) + " pixels)");

    // Two linings laid on the wall, one over the other, of much the same size
    // - plaster the height of a room, beadboard along its foot - their faces
    // in one plane: the smaller is seen, from ten metres to sixty, square on
    // to edge on. (Few steps of size part them, where a plate has many.)
    box("plaster", 60.0, 1.0, 60.105, 7.6, 3.0, 0.01, 0.8, 0.05, 0.05);
    box("panelling", 60.0, 1.0, 60.105, 7.6, 1.2, 0.01, 0.05, 0.8, 0.05);
    // And a sheet laid a millimetre and a half off the wall's face, as a
    // room's finishes are: seen from far off and edge on, a few depth steps
    // of a buffer whose near plane is a few centimetres.
    box("sheet", 60.0, 2.4, 60.111, 7.6, 1.2, 0.0002, 0.05, 0.8, 0.05);
    int panel_fights = 0;
    for (const double dist : {10.0, 20.0, 40.0, 60.0})
        for (const double turn : {0.0, 0.6, 0.9, 1.1, 1.35}) {
          for (const double ty : {1.5, 3.0}) {
            const double tx = 60.0, tz = 60.11;
            const double ex = tx + std::sin(turn) * dist, ez = tz + std::cos(turn) * dist;
            if (ex < 0.5 || ex > 119.5 || ez > 119.5) continue;
            eye.params.set(sg::keys::x, ex).set(sg::keys::y, ty).set(sg::keys::z, ez);
            eye.params.set(sg::keys::yaw, std::atan2(tz - ez, tx - ex)).set(sg::keys::pitch, 0.0).set("fov", 30.0);
            for (int i = 0; i < 3; ++i) view.render(room, W, H);
            std::vector<unsigned char> px(static_cast<std::size_t>(W) * H * 3);
            sg::gl::glReadPixels(0, 0, W, H, sg::gl::GL_RGB, sg::gl::GL_UNSIGNED_BYTE, px.data());
            int here = 0;
            for (int y = H / 2 - 1; y <= H / 2 + 1; ++y)
                for (int x = W / 2 - 1; x <= W / 2 + 1; ++x) {
                    const unsigned char* p = &px[(static_cast<std::size_t>(y) * W + x) * 3];
                    if (p[0] > p[1]) ++here;
                }
            if (here) std::printf("  linings at %.1f m up, from %.0f m, %.2f round: %d pixels of the wall\n", ty, dist, turn, here);
            panel_fights += here;
          }
        }
    check(panel_fights == 0, "beadboard laid on plaster, their faces in one plane, and a sheet a hair off a wall, are seen - every pixel, near and far, square on to edge on (" +
                                 std::to_string(panel_fights) + " pixels of the wall)");

    // A big sheet three millimetres before a small tile - the tile, being
    // small, drawn nearer by its size: it must not come through the sheet,
    // however far off (with a depth of a few centimetres' near plane and
    // integer steps, from twenty metres on it did).
    box("tile", 20.0, 1.0, 60.0, 1.0, 1.0, 0.0002, 0.8, 0.05, 0.05);
    box("cover", 20.0, 0.0, 60.003, 40.0, 10.0, 0.0002, 0.05, 0.8, 0.05);
    int through_sheet = 0;
    for (const double dist : {5.0, 10.0, 20.0, 40.0})
        for (const double turn : {0.0, 0.6, 1.1}) {
            const double tx = 20.0, ty = 1.5, tz = 60.003;
            const double ex = tx + std::sin(turn) * dist, ez = tz + std::cos(turn) * dist;
            eye.params.set(sg::keys::x, ex).set(sg::keys::y, ty).set(sg::keys::z, ez);
            eye.params.set(sg::keys::yaw, std::atan2(tz - ez, tx - ex)).set(sg::keys::pitch, 0.0).set("fov", 30.0);
            for (int i = 0; i < 3; ++i) view.render(room, W, H);
            std::vector<unsigned char> px(static_cast<std::size_t>(W) * H * 3);
            sg::gl::glReadPixels(0, 0, W, H, sg::gl::GL_RGB, sg::gl::GL_UNSIGNED_BYTE, px.data());
            int here = 0;
            for (int y = H / 2 - 1; y <= H / 2 + 1; ++y)
                for (int x = W / 2 - 1; x <= W / 2 + 1; ++x) {
                    const unsigned char* p = &px[(static_cast<std::size_t>(y) * W + x) * 3];
                    if (p[0] > p[1]) ++here;
                }
            if (here) std::printf("  the tile through the sheet, from %.0f m, %.2f round: %d pixels\n", dist, turn, here);
            through_sheet += here;
        }
    check(through_sheet == 0, "a small tile three millimetres behind a sheet does not come through it, near or far (" + std::to_string(through_sheet) + " pixels)");

    // A mirror thirty metres before the wall, facing it: the eye stands
    // between, looking into the glass at the plate's image (the plate
    // mirrored in it) - the mirror's eye thirty-five to sixty metres off.
    sg::Element& glass = box("mirror", 60.0, 0.0, 90.1, 100.0, 8.0, 0.02, 0.02, 0.02, 0.02);
    glass.params.set("reflects", 1.0).set("roughness", 0.0);
    int mirrored = 0, mirror_views = 0;
    for (const double dist : {35.0, 50.0, 60.0})
        for (const double turn : {0.0, 0.6, 0.9, 1.0}) {
            const double before = std::cos(turn) * dist;
            if (before < 31.0 || before > 59.0) continue;  // (the eye must stand between the wall and the glass)
          for (const double ty : {1.5, 3.0}) {
            const double tx = 60.0, tz = 120.09;  // the linings, mirrored in the glass
            const double ex = tx + std::sin(turn) * dist, ez = tz - std::cos(turn) * dist;
            eye.params.set(sg::keys::x, ex).set(sg::keys::y, ty).set(sg::keys::z, ez);
            eye.params.set(sg::keys::yaw, std::atan2(tz - ez, tx - ex)).set(sg::keys::pitch, 0.0).set("fov", 30.0);
            for (int i = 0; i < 3; ++i) view.render(room, W, H);
            std::vector<unsigned char> px(static_cast<std::size_t>(W) * H * 3);
            sg::gl::glReadPixels(0, 0, W, H, sg::gl::GL_RGB, sg::gl::GL_UNSIGNED_BYTE, px.data());
            const double px_per_m = (H / 2.0) / (dist * std::tan(15.0 * 3.14159265 / 180.0));
            const int hx = std::min(3, static_cast<int>(0.5 * std::cos(turn) * px_per_m * 0.5)), hy = std::min(3, static_cast<int>(0.25 * px_per_m));
            int here = 0, green = 0;
            for (int y = H / 2 - hy; y <= H / 2 + hy; ++y)
                for (int x = W / 2 - hx; x <= W / 2 + hx; ++x) {
                    const unsigned char* p = &px[(static_cast<std::size_t>(y) * W + x) * 3];
                    if (p[0] > p[1]) ++here;
                    else ++green;
                }
            if (here) std::printf("  in the mirror, at %.1f m up, from %.0f m, %.2f round: %d pixels of the wall\n", ty, dist, turn, here);
            mirrored += here;
            mirror_views += green > 0;
          }
        }
    check(mirror_views > 0 && mirrored == 0, "and in a mirror too - every pixel, however far behind the glass its eye stands (" +
                                                 std::to_string(mirrored) + " pixels of the wall)");
    std::printf("%d views\n", views);
    return ok ? 0 : 1;
}
