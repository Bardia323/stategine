// Walking on whatever the space says is down: level ground as it always was;
// over the edge of a cube whose faces pull, onto the next face; all the way
// round a planet. And a walker's step is a function of what it is given.
#include "sg/domains/Walk.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

static int failures = 0;
static void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

namespace {
using sg::Key;
using sg::Vec3d;
constexpr double kPi = 3.141592653589793;

sg::field::Solver solved(const sg::State& s) {
    sg::field::Solver f;
    f.rebuild(sg::fields_of(s));
    return f;
}
void steps(const sg::State& s, const sg::field::Solver& f, sg::Element& w, const sg::Stride& in, double seconds, double& t) {
    for (int i = 0; i < static_cast<int>(seconds * 60); ++i) {
        t += 1.0 / 60.0;
        sg::walk(s, f, w, in, 1.0 / 60.0, t);
    }
}
}  // namespace

int main() {
    {
        // Level ground: a floor, ordinary gravity.
        sg::Spatial3D s(Key{"hall"});
        s.params().set("g", 9.81);
        s.wall(Key{"floor"}, {0, -1, 0}, 40, 1, 40);
        sg::Element& w = s.camera();
        w.params.set(sg::keys::x, 0.0).set(sg::keys::y, 3.0).set(sg::keys::z, 0.0);
        const auto f = solved(s);
        double t = 0;
        steps(s, f, w, {}, 2.0, t);
        check(w.params.num("grounded") == 1.0 && std::fabs(w.params.num(sg::keys::y) - (1.65 - 0.3 + 0.3)) < 0.02,
              "a walker falls and stands on the floor, its eye its height above it");
        check(!w.params.has(Key{"stand_w"}), "and on level ground its ground is level: a heading, no more");
        steps(s, f, w, {1.0, 0, 0, 0, 3.0}, 2.0, t);
        check(std::fabs(w.params.num(sg::keys::x) - 6.0) < 0.6 && std::fabs(w.params.num(sg::keys::z)) < 1e-9,
              "it walks the way it looks, at the speed it asks");
        // Facing +x with y up, the right hand is +z: the same right the eye
        // sees (a view's right is forward cross up).
        const double z0 = w.params.num(sg::keys::z);
        steps(s, f, w, {0, 1.0, 0, 0, 3.0}, 1.0, t);
        const sg::Vec3d f0 = sg::forward_of(w), up0{0, 1, 0};
        const sg::Vec3d right{f0.y * up0.z - f0.z * up0.y, f0.z * up0.x - f0.x * up0.z, f0.x * up0.y - f0.y * up0.x};
        check(right.z > 0.9 && w.params.num(sg::keys::z) - z0 > 2.0, "and steps right to its right, as the eye sees it");
    }
    {
        // A cube whose faces pull: walk off the top, over the edge, down the side.
        sg::Spatial3D s(Key{"cube"});
        sg::Element& c = s.mesh(Key{"block"}, 0, -2, 0);
        c.params.set(sg::keys::sx, 4.0).set(sg::keys::sy, 4.0).set(sg::keys::sz, 4.0).set("solid", 1.0);
        sg::Element& field = s.add_element(Key{"pull"}, Key{"field"});
        field.params.set("field_shape", std::string("box")).set(sg::keys::x, 0.0).set(sg::keys::y, 0.0).set(sg::keys::z, 0.0);
        field.params.set(sg::keys::sx, 4.0).set(sg::keys::sy, 4.0).set(sg::keys::sz, 4.0).set("strength", -9.81).set("radius", 12.0);
        sg::Element& w = s.camera();
        w.params.set(sg::keys::x, 0.0).set(sg::keys::y, 2.0 + 1.65).set(sg::keys::z, 0.0).set(sg::keys::yaw, 0.0).set(sg::keys::pitch, 0.0);
        const auto f = solved(s);
        double t = 0;
        steps(s, f, w, {}, 1.0, t);
        check(w.params.num("grounded") == 1.0, "on top of the cube, standing");
        steps(s, f, w, {1.0, 0, 0, 0, 2.5}, 1.8, t);
        const Vec3d up = sg::up_of(w), eye = sg::position_of(w);
        check(w.params.num("grounded") == 1.0 && up.x > 0.95 && eye.x > 2.0 + 1.5 && std::fabs(eye.x - (2.0 + 1.65)) < 0.1,
              "walked over the edge: standing on the side face, the cube's +x its down");
        check(eye.y < 2.0, "and down that face, along the way it was going");
        steps(s, f, w, {1.0, 0, 0, 0, 2.5}, 2.4, t);
        check(w.params.num("grounded") == 1.0 && sg::up_of(w).y < -0.95 && std::fabs(sg::position_of(w).y - (-2.0 - 1.65)) < 0.1,
              "and on, over the next edge, onto the underside: standing upside down");
    }
    {
        // A planet: a ball of 20 m, pulling as mass does.
        sg::Spatial3D s(Key{"planet"});
        const double R = 20.0, g0 = 9.81;
        sg::Element& ball = s.mesh(Key{"ground"}, 0, -R, 0);
        ball.params.set(sg::keys::sx, 2 * R).set(sg::keys::sy, 2 * R).set(sg::keys::sz, 2 * R).set("shape", std::string("sphere")).set("solid", 1.0);
        sg::Element& field = s.add_element(Key{"mass"}, Key{"field"});
        field.params.set("field_shape", std::string("radial")).set("strength", -g0 * R * R).set("exponent", 2.0);
        sg::Element& w = s.camera();
        w.params.set(sg::keys::x, 0.0).set(sg::keys::y, R + 1.65).set(sg::keys::z, 0.0).set(sg::keys::yaw, 0.0);
        const auto f = solved(s);
        double t = 0;
        steps(s, f, w, {}, 1.0, t);
        // A quarter of the way round, at 4 m/s.
        steps(s, f, w, {1.0, 0, 0, 0, 4.0}, (kPi * 0.5 * R) / 4.0, t);
        const Vec3d up = sg::up_of(w), eye = sg::position_of(w);
        check(w.params.num("grounded") == 1.0 && std::fabs(sg::length(eye) - (R + 1.65 - 0.3 + 0.3)) < 0.1,
              "walking on a planet: always its height above the ground");
        check(dot(up, Vec3d{eye.x / sg::length(eye), eye.y / sg::length(eye), eye.z / sg::length(eye)}) > 0.999,
              "with the planet's centre always straight down");
        check(eye.x > R * 0.9 && std::fabs(eye.y) < R * 0.3, "a quarter of the way round, on its side, it walks on");
    }
    {
        // Through a doorway in a wall: what the doorway opens is the wall,
        // never the floor under it - the walker keeps its feet all the way.
        sg::Spatial3D s(Key{"passage"});
        s.params().set("g", 9.81);
        s.wall(Key{"floor"}, {0, -1, 0}, 40, 1, 40);
        s.wall(Key{"wall"}, {0, 0, 5.1}, 40, 3, 0.2);
        sg::Element& door = s.portal(Key{"door"}, {0, 1.025, 5.0}, 0.9, 2.05, -kPi * 0.5);
        door.params.set("walk", 1.0);
        sg::Element& w = s.camera();
        w.params.set(sg::keys::x, 0.0).set(sg::keys::y, 1.65).set(sg::keys::z, 2.0).set(sg::keys::yaw, kPi * 0.5);
        const auto f = solved(s);
        double t = 0, lowest = 99;
        for (int i = 0; i < 180; ++i) {
            steps(s, f, w, {1.0, 0, 0, 0, 2.4}, 1.0 / 60.0, t);
            lowest = std::min(lowest, w.params.num(sg::keys::y));
        }
        check(w.params.num(sg::keys::z) > 6.0 && lowest > 1.6, "through a doorway in a wall, with the floor under it all the way");
    }
    {
        // A flight of stairs: each step under the height of a stride, climbed.
        sg::Spatial3D s(Key{"flight"});
        s.params().set("g", 9.81);
        s.wall(Key{"floor"}, {0, -1, 0}, 40, 1, 40);
        for (int i = 0; i < 10; ++i) s.wall(Key{"step" + std::to_string(i)}, {0, 0, 2.0 + 0.3 * i + 0.15}, 4, 0.18 * (i + 1), 0.3);
        s.wall(Key{"landing"}, {0, 0, 5.0 + 2.0}, 4, 1.8, 4.0);
        sg::Element& w = s.camera();
        w.params.set(sg::keys::x, 0.0).set(sg::keys::y, 1.65).set(sg::keys::z, 0.5).set(sg::keys::yaw, kPi * 0.5);
        const auto f = solved(s);
        double t = 0;
        steps(s, f, w, {1.0, 0, 0, 0, 2.4}, 3.0, t);
        check(w.params.num(sg::keys::z) > 5.0 && std::fabs(w.params.num(sg::keys::y) - (1.8 + 1.65)) < 0.1,
              "up a flight of stairs, step by step, to stand on the top");
    }
    {
        // The same step from the same walker gives the same walker.
        sg::Spatial3D s(Key{"same"});
        s.params().set("g", 9.81);
        s.wall(Key{"floor"}, {0, -1, 0}, 40, 1, 40);
        sg::Element a = s.camera();
        a.params.set(sg::keys::y, 2.0).set(sg::keys::vx, 1.0);
        sg::Element b = a;
        const auto f = solved(s);
        sg::walk(s, f, a, {0.5, 0.2, 0.1, 0.0, 3.0, true}, 1.0 / 60.0, 1.0);
        sg::walk(s, f, b, {0.5, 0.2, 0.1, 0.0, 3.0, true}, 1.0 / 60.0, 1.0);
        check(sg::position_of(a).x == sg::position_of(b).x && sg::position_of(a).y == sg::position_of(b).y && a.params.num(sg::keys::vx) == b.params.num(sg::keys::vx) && a.params.num(sg::keys::yaw) == b.params.num(sg::keys::yaw), "a step is a function of what it is given: the laws can re-run it");
    }
    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
