// The rigid bodies on their own: things come to rest and sleep, stack, tip
// over, roll to a stop; a table taken by its edge turns over; a mug held
// comes up square; and all of it is quick enough.
#include "sg/physics/Rigid.hpp"

namespace rigid = sg::rigid;

#include <chrono>
#include <cstdio>
#include <string>

using namespace rigid;

static int failures = 0;
static void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

static Body floor_body() {
    Body f;
    f.id = "floor";
    f.hulls.push_back(Hull::box({0, -0.5, 0}, {50, 0.5, 50}));
    f.set_mass(0);
    return f;
}

static Body box_body(const std::string& id, V3 at, V3 half, double mass, const M3& turn = M3{}) {
    Body b;
    b.id = id;
    b.hulls.push_back(Hull::box({}, half));
    b.x = at;
    b.r = turn;
    b.set_mass(mass);
    return b;
}

static Body table_body(V3 at) {
    // As the catalogue has it: a top 1.4 x 0.04 x 0.8 at 0.71, four legs.
    Body t;
    t.id = "table";
    t.hulls.push_back(Hull::box({0, 0.73, 0}, {0.7, 0.02, 0.4}));
    for (int i = 0; i < 4; ++i)
        t.hulls.push_back(Hull::box({i % 2 ? 0.63 : -0.63, 0.355, i / 2 ? 0.33 : -0.33}, {0.03, 0.355, 0.03}));
    t.x = at;
    t.set_mass(20);
    return t;
}

static void run(World& w, double seconds) {
    for (int i = 0; i < static_cast<int>(seconds * 60); ++i) w.step(1.0 / 60.0);
}

int main() {
    {
        // Mass: a 1 x 2 x 3 box of 6 kg - I = m/12 (b^2 + c^2) each way.
        Body b = box_body("b", {}, {0.5, 1.0, 1.5}, 6);
        const M3 I = inverse(b.inv_inertia_local);
        check(std::fabs(I(0, 0) - 6.0 / 12 * (4 + 9)) < 1e-6 && std::fabs(I(1, 1) - 6.0 / 12 * (1 + 9)) < 1e-6 &&
                  std::fabs(I(2, 2) - 6.0 / 12 * (1 + 4)) < 1e-6 && length(b.com_local) < 1e-9,
              "a box's inertia is a box's");
        Body t = table_body({});
        check(t.com_local.y > 0.55 && t.com_local.y < 0.73, "a table's mass sits high, in its top");
        M3 e = from_euler(0.7, 0.3, -1.1);
        double y, p, r;
        to_euler(e, y, p, r);
        check(std::fabs(y - 0.7) < 1e-9 && std::fabs(p - 0.3) < 1e-9 && std::fabs(r + 1.1) < 1e-9, "turns go to euler and back");
    }
    {
        World w;
        w.add(floor_body());
        w.add(box_body("book", {0, 0.5, 0}, {0.1, 0.02, 0.15}, 0.6));
        run(w, 3);
        const Body& b = *w.find("book");
        check(std::fabs(b.x.y - 0.02) < 0.004, "a book dropped lies on the floor (y " + std::to_string(b.x.y) + ")");
        check(!b.awake, "and sleeps");
    }
    {
        // Five crates, one on another, a little off true.
        World w;
        w.add(floor_body());
        for (int i = 0; i < 5; ++i) w.add(box_body("c" + std::to_string(i), {0.01 * (i % 2), 0.2 + 0.401 * i, 0}, {0.2, 0.2, 0.2}, 5));
        run(w, 6);
        const Body& top = *w.find("c4");
        check(std::fabs(top.x.x) < 0.03 && top.x.y > 1.7, "a stack of five stands (top at " + std::to_string(top.x.x) + ", " + std::to_string(top.x.y) + ")");
        check(!top.awake, "and sleeps");
    }
    {
        // A box dropped on its edge falls onto a face.
        World w;
        w.add(floor_body());
        w.add(box_body("b", {0, 0.6, 0}, {0.15, 0.15, 0.15}, 2, axis_angle({0, 0, 1}, 0.5)));
        run(w, 4);
        const Body& b = *w.find("b");
        double y, p, r;
        to_euler(b.r, y, p, r);
        const double up = std::fabs(b.r.col(1).y);
        const double side = std::max({std::fabs(b.r.col(0).y), up, std::fabs(b.r.col(2).y)});
        check(side > 0.999 && std::fabs(b.x.y - 0.15) < 0.004, "a box on its edge falls onto a face");
    }
    {
        // A cylinder on its side, pushed: it rolls, and stops.
        World w;
        w.add(floor_body());
        Body c;
        c.id = "can";
        c.hulls.push_back(Hull::prism({}, {0.04, 0.06, 0.04}, 24));
        c.x = {0, 0.045, 0};
        c.r = axis_angle({1, 0, 0}, 1.5707963);
        c.set_mass(0.3);
        c.v = {0.8, 0, 0.0};
        w.add(c);
        run(w, 1.0);
        const double moved = w.find("can")->x.x;
        run(w, 8);
        const Body& b = *w.find("can");
        check(moved > 0.15 && b.x.x >= moved - 1e-3 && !b.awake, "a can on its side rolls and stops (" + std::to_string(b.x.x) + " m)");
    }
    {
        // A table stands; taken by one end of its top and lifted, it turns
        // over on the other end.
        World w;
        w.add(floor_body());
        w.add(table_body({0, 0, 0}));
        run(w, 2);
        Body& t = *w.find("table");
        check(t.r.col(1).y > 0.999 && std::fabs(t.x.y) < 0.004 && !t.awake, "a table stands on its legs and sleeps");
        const V3 edge{0.7, 0.75, 0};
        World::Grab& g = w.grab("table", edge, t.x + edge, 700);
        for (int i = 0; i < 180; ++i) {
            const double k = i / 180.0;
            g.target = V3{0.7 - 0.9 * k, 0.75 + 1.3 * k, 0};
            w.step(1.0 / 60.0);
        }
        w.release("table");
        run(w, 4);
        check(w.find("table")->r.col(1).y < -0.9, "taken by its edge and lifted over, it lands on its back (up " +
                                                      std::to_string(w.find("table")->r.col(1).y) + ")");
    }
    {
        // A mug held out in front, carried about: it stays square and near.
        World w;
        w.add(floor_body());
        Body m;
        m.id = "mug";
        m.hulls.push_back(Hull::prism({0, 0.048, 0}, {0.041, 0.048, 0.041}, 16));
        m.set_mass(0.35);
        w.add(m);
        World::Grab& g = w.grab("mug", {0, 0.048, 0}, {0, 1.2, 0}, 60);
        g.turns = true;
        g.turn = axis_angle({0, 1, 0}, 0.4);
        g.torque = 4;
        double worst = 0;
        for (int i = 0; i < 240; ++i) {
            g.target = V3{std::sin(i * 0.03) * 0.5, 1.2, std::cos(i * 0.03) * 0.5 - 0.5};
            w.step(1.0 / 60.0);
            if (i > 60) worst = std::max(worst, length(w.find("mug")->com() - g.target));
        }
        const Body& b = *w.find("mug");
        check(worst < 0.08 && b.r.col(1).y > 0.98, "a mug carried stays in the hand and upright (" + std::to_string(worst) + " m)");
        w.release("mug");
        run(w, 3);
        check(w.find("mug")->x.y < 0.3, "let go, it falls");
    }
    {
        // A heap of forty books and crates onto a table: how long a step takes.
        World w;
        w.add(floor_body());
        w.add(table_body({0, 0, 0}));
        for (int i = 0; i < 40; ++i)
            w.add(box_body("b" + std::to_string(i), {-0.5 + 0.25 * (i % 5), 1.0 + 0.1 * (i / 5), -0.2 + 0.1 * (i % 3)},
                           {0.08, 0.02 + 0.01 * (i % 3), 0.11}, 0.6, axis_angle(normalize({1, 2, 3}), i * 0.4)));
        const auto t0 = std::chrono::steady_clock::now();
        const int steps = 300;
        for (int i = 0; i < steps; ++i) w.step(1.0 / 60.0);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / steps;
        int asleep = 0;
        bool sane = true;
        for (const Body& b : w.bodies) {
            asleep += b.dynamic() && !b.awake;
            sane &= b.x.y > -0.05 && b.x.y < 2.0;
        }
        std::printf("     %.3f ms a step, %d of 41 asleep\n", ms, asleep);
        check(sane, "forty things heaped on a table stay in the world");
#ifdef NDEBUG
        const double most = 4.0;
#else
        const double most = 30.0;  // (unoptimised: only that it is not wildly slower)
#endif
        check(ms < most, "a step of forty-one bodies takes under " + std::to_string(static_cast<int>(most)) + " ms");
    }
    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
