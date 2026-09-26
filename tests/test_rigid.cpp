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
        // A heavy plank asleep on the floor with a light block asleep on it:
        // the plank taken by its end and pulled along wakes before the block
        // does, and pushes against it - as against a wall, while it sleeps.
        // Woken, the block must not be handed those impulses: it rides along.
        World w;
        w.add(floor_body());
        w.add(box_body("plank", {0, 0.05, 0}, {0.8, 0.05, 0.1}, 45));
        w.add(box_body("block", {0, 0.12, 0}, {0.06, 0.02, 0.02}, 0.02));
        run(w, 3);
        check(!w.find("plank")->awake && !w.find("block")->awake, "a plank and a block on it come to rest, and sleep");
        // As a thing put down lies: a little sunk in what it is on.
        Body& block = *w.find("block");
        block.x.y -= 0.003;
        block.place();
        // The plank taken by its end and pulled along; the block wakes a
        // step after it.
        World::Grab& g = w.grab("plank", {0.8, 0, 0}, {0.8, 0.05, 0}, 600);
        g.hertz = 4;
        double fastest = 0;
        for (int i = 0; i < 90; ++i) {
            g.target = V3{0.8 - 0.4 * i / 90.0, 0.05, 0.8 * i / 90.0};
            w.step(1.0 / 60.0);
            fastest = std::max(fastest, w.find("block")->v.y);
        }
        check(fastest < 0.3, "pulled along, the block asleep on it wakes and rides, not flung off (rising at most " +
                                 std::to_string(fastest) + " m/s)");
    }
    // --- joints ---------------------------------------------------------------------------
    {
        // A weight on a ball joint to the room, let go out to the side: it
        // swings, and stays as far from where it hangs as it was.
        World w;
        w.add(box_body("bob", {1.0, 2.0, 0}, {0.1, 0.1, 0.1}, 2));
        w.ball("bob", "", {0, 2.0, 0});
        double worst = 0, lowest = 9;
        for (int i = 0; i < 240; ++i) {
            w.step(1.0 / 60.0);
            const Body& b = *w.find("bob");
            worst = std::max(worst, std::fabs(length(b.x - V3{0, 2.0, 0}) - 1.0));
            lowest = std::min(lowest, b.x.y);
        }
        check(lowest < 1.1 && worst < 0.01, "a pendulum on a ball joint swings, and keeps its length (off by " + std::to_string(worst) + " m)");
    }
    {
        // A door on its hinges: a leaf 0.9 wide hung from the room on a
        // vertical axis at one edge, pushed. It turns about that axis only -
        // its hinge stays put, it stays upright - and stops at its limits.
        const auto door = [](World& w) {
            w.add(box_body("leaf", {0.45, 1.0, 0}, {0.45, 1.0, 0.02}, 20));
            return &w.hinge("leaf", "", {0, 1.0, 0}, {0, 1, 0});
        };
        {
            World w;
            w.gravity = {0, -9.81, 0};
            door(w);
            // Pushed open: turning about its hinges, as a door does.
            w.find("leaf")->w = {0, -2.0, 0}, w.find("leaf")->v = {0, 0, 0.9};
            double drift = 0, tilt = 0;
            for (int i = 0; i < 60; ++i) {
                w.step(1.0 / 60.0);
                const Body& b = *w.find("leaf");
                drift = std::max(drift, length(b.x + b.r * V3{-0.45, 0, 0} - V3{0, 1.0, 0}));
                tilt = std::max(tilt, 1.0 - b.r.col(1).y);
            }
            check(drift < 0.01 && tilt < 0.001 && std::fabs(w.angle(w.joints[0])) > 1.0,
                  "a door pushed swings about its hinges, which stay put, upright (hinge off by " + std::to_string(drift) + " m, leaning " +
                      std::to_string(tilt) + ", turned " + std::to_string(w.angle(w.joints[0])) + " rad)");
        }
        {
            World w;
            Joint* j = door(w);
            j->limit = true, j->lower = -1.6, j->upper = 0.0;
            w.find("leaf")->w = {0, -6.0, 0}, w.find("leaf")->v = {0, 0, 2.7};
            double most = 0, least = 0;
            for (int i = 0; i < 120; ++i) {
                w.step(1.0 / 60.0);
                most = std::max(most, w.angle(w.joints[0]));
                least = std::min(least, w.angle(w.joints[0]));
            }
            check(least > -1.65 && most < 0.05, "flung open, it stops at its limit (" + std::to_string(least) + " rad) and at its frame");
        }
        {
            World w;
            Joint* j = door(w);
            j->spring = true, j->target = 0.0, j->hertz = 0.6, j->damping = 1.0;
            w.find("leaf")->w = {0, -3.0, 0}, w.find("leaf")->v = {0, 0, 1.35};
            double widest = 0;
            for (int i = 0; i < 360; ++i) {
                w.step(1.0 / 60.0);
                widest = std::min(widest, w.angle(w.joints[0]));
            }
            check(widest < -0.5 && std::fabs(w.angle(w.joints[0])) < 0.03,
                  "with a closer, pushed open it swings back shut (" + std::to_string(w.angle(w.joints[0])) + " rad)");
        }
    }
    {
        // A wheel on an axle, driven by a motor: it comes up to its speed.
        World w;
        Body wheel;
        wheel.id = "wheel";
        wheel.hulls.push_back(Hull::prism({}, {0.3, 0.05, 0.3}, 20));
        wheel.x = {0, 1.0, 0};
        wheel.set_mass(3);
        w.add(wheel);
        Joint& j = w.hinge("wheel", "", {0, 1.0, 0}, {0, 1, 0});
        j.motor = true, j.speed = 4.0, j.torque = 20.0;
        for (int i = 0; i < 120; ++i) w.step(1.0 / 60.0);
        const double spin = w.find("wheel")->w.y;
        check(std::fabs(spin - 4.0) < 0.1, "a motor brings a wheel up to its speed (" + std::to_string(spin) + " rad/s)");
    }
    {
        // A chain of ten links hung from the room: it stays together.
        World w;
        for (int i = 0; i < 10; ++i) w.add(box_body("link" + std::to_string(i), {0.1 + 0.2 * i, 3.0, 0}, {0.1, 0.02, 0.02}, 0.5));
        w.ball("link0", "", {0, 3.0, 0});
        for (int i = 1; i < 10; ++i) w.ball("link" + std::to_string(i - 1), "link" + std::to_string(i), {0.2 * i, 3.0, 0});
        double worst = 0, bottom = 9;
        for (int s = 0; s < 240; ++s) {
            w.step(1.0 / 60.0);
            bottom = std::min(bottom, w.find("link9")->x.y);
            for (const Joint& j : w.joints) {
                const Body* a = w.find(j.a);
                const Body* b = j.b.empty() ? nullptr : w.find(j.b);
                const V3 pa = a->x + a->r * j.la, pb = b ? b->x + b->r * j.lb : j.lb;
                worst = std::max(worst, length(pb - pa));
            }
        }
        check(worst < 0.02 && bottom < 1.5, "a chain of ten swings down and stays together (links apart by " + std::to_string(worst) + " m)");
    }
    {
        // A weight on a spring from the room: it bounces, and comes to rest
        // where the spring holds it.
        World w;
        w.add(box_body("weight", {0, 1.0, 0}, {0.1, 0.1, 0.1}, 1));
        w.spring("weight", "", {0, 1.0, 0}, {0, 2.0, 0}, 1.5, 0.3);
        for (int i = 0; i < 600; ++i) w.step(1.0 / 60.0);
        // Hanging, it stretches until the spring bears its weight: k x = m g.
        const double k = 1.0 * std::pow(2 * 3.14159265358979 * 1.5, 2.0);
        const double sag = 9.81 / k, y = w.find("weight")->x.y;
        check(std::fabs((1.0 - y) - sag) < 0.02, "a weight on a spring comes to rest where the spring bears it (sagged " +
                                                     std::to_string(1.0 - y) + " m, " + std::to_string(sag) + " expected)");
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
    // Pairs by sweeping along x are the pairs every-against-every finds: a
    // jumble of three hundred things falling onto a table and each other,
    // stepped both ways, touches the same hulls and moves the same, step by step.
    const auto jumble = [](World& w, int n, double spread) {
        w.add(floor_body());
        w.add(table_body({0, 0, 0}));
        for (int i = 0; i < n; ++i) {
            const double k = std::sin(i * 12.9898) * 43758.5453, f = k - std::floor(k);
            const double g = std::sin(i * 78.233) * 12345.678, h = g - std::floor(g);
            w.add(box_body("j" + std::to_string(i), {spread * (f - 0.5), 0.9 + 0.12 * (i % 17), spread * (h - 0.5)},
                           {0.05 + 0.1 * f, 0.03 + 0.05 * h, 0.06 + 0.08 * (1 - f)}, 0.3 + f,
                           axis_angle(normalize({f, 1.0, h}), i * 0.7)));
        }
    };
    {
        World swept, every;
        every.sweep = false;
        jumble(swept, 300, 3.0);
        jumble(every, 300, 3.0);
        bool same_pairs = true;
        double apart = 0;
        for (int s = 0; s < 120; ++s) {
            swept.step(1.0 / 60.0);
            every.step(1.0 / 60.0);
            same_pairs &= swept.touching() == every.touching();
            for (std::size_t i = 0; i < swept.bodies.size(); ++i)
                apart = std::max(apart, length(swept.bodies[i].x - every.bodies[i].x));
        }
        check(same_pairs && !swept.touching().empty(), "swept along x, the pairs touching are those every-against-every finds (" +
                                                           std::to_string(swept.touching().size()) + " at the end)");
        check(apart == 0.0, "and everything moves the same, to the last bit (" + std::to_string(apart) + " m apart)");
    }
    {
        // Two thousand things falling on a wide floor: a step of each.
        const auto time = [&](bool sweep) {
            World w;
            w.sweep = sweep;
            jumble(w, 2000, 30.0);
            for (int s = 0; s < 10; ++s) w.step(1.0 / 60.0);  // falling, all awake
            const auto t0 = std::chrono::steady_clock::now();
            const int steps = 20;
            for (int s = 0; s < steps; ++s) w.step(1.0 / 60.0);
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / steps;
        };
        const double every = time(false), swept = time(true);
        std::printf("     2002 bodies: every pair %.2f ms a step, swept %.2f ms\n", every, swept);
        check(swept < every * 0.5, "with two thousand things, sweeping takes under half the time");
    }
    // A thing's shape: its big parts, and every small one at its outside.
    {
        std::vector<PartBox> chair;
        chair.push_back({{-0.25, 0.4, -0.25}, {0.25, 0.46, 0.25}, 0.015});      // the seat
        chair.push_back({{-0.2, 0.46, 0.2}, {0.2, 1.0, 0.24}, 0.0086});         // the back
        for (int k = 0; k < 5; ++k) {                                            // five castors, on the floor
            const double a = k * 1.2566, x = std::cos(a) * 0.28, z = std::sin(a) * 0.28;
            chair.push_back({{x - 0.03, 0.0, z - 0.03}, {x + 0.03, 0.06, z + 0.03}, 0.0002});
        }
        chair.push_back({{-0.21, 1.0, 0.19}, {0.21, 1.04, 0.25}, 0.0001});       // the top rail
        chair.push_back({{0.0, 0.3, 0.0}, {0.02, 0.32, 0.02}, 0.00001});          // a knob under the seat
        const auto kept = outline(chair);
        const auto has = [&](std::size_t i) { return std::find(kept.begin(), kept.end(), i) != kept.end(); };
        bool castors = true;
        for (std::size_t i = 2; i < 7; ++i) castors &= has(i);
        check(has(0) && has(1), "a thing's shape has its big parts");
        check(castors && has(7), "and every small part at its outside - all five castors, the top rail");
        check(!has(8), "but not a small part inside it");
    }

    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
