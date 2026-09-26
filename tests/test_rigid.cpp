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
    // --- fast things and thin walls ---------------------------------------------------------
    {
        // A small thing thrown hard at a thin wall - straight at it, at a
        // slant, spinning - at a step of a sixtieth and of a twentieth of a
        // second: it stops at the wall, whatever the speed, never through.
        struct Throw {
            double speed, slant, spin, dt;
        };
        int through = 0, tried = 0;
        std::string worst;
        for (const Throw t : {Throw{10, 0, 0, 1 / 60.0}, Throw{30, 0, 0, 1 / 60.0}, Throw{60, 0, 0, 1 / 60.0}, Throw{30, 0.6, 0, 1 / 60.0},
                              Throw{30, 0, 40, 1 / 60.0}, Throw{15, 0, 0, 1 / 20.0}, Throw{40, 0.4, 20, 1 / 20.0}}) {
            World w;
            w.gravity = {0, 0, 0};
            Body wall;
            wall.id = "wall";
            wall.hulls.push_back(Hull::box({}, {0.01, 1.0, 1.0}));  // two centimetres thick
            wall.x = {1.0, 1.0, 0};
            wall.set_mass(0);
            w.add(wall);
            w.add(box_body("pebble", {0, 1.0, 0}, {0.015, 0.015, 0.015}, 0.05));
            Body& p = *w.find("pebble");
            p.v = V3{std::cos(t.slant), 0, std::sin(t.slant)} * t.speed;
            p.w = {t.spin, t.spin * 0.7, 0};
            for (int i = 0; i < static_cast<int>(1.0 / t.dt); ++i) w.step(t.dt);
            ++tried;
            if (w.find("pebble")->x.x > 1.0) {
                ++through;
                worst += " " + std::to_string(static_cast<int>(t.speed)) + " m/s at " + std::to_string(t.slant) + " rad, step " +
                         std::to_string(t.dt) + ";";
            }
        }
        check(through == 0, "thrown hard at a thin wall, it never goes through (" + std::to_string(through) + " of " +
                                std::to_string(tried) + " did:" + worst + ")");
    }
    // --- casts and sensors ---------------------------------------------------------------------
    {
        // A box carried down onto a table stops on its top; carried along
        // above it, it meets nothing.
        World w;
        w.add(floor_body());
        w.add(table_body({0, 0, 0}));
        const Hull box = Hull::box({}, {0.1, 0.1, 0.1});
        double at = 0;
        V3 n;
        const Body* hit = w.cast(box, M3{}, {0.2, 2.0, 0.1}, {0.2, 0.0, 0.1}, &at, &n);
        const double bottom = 2.0 - 2.0 * at - 0.1;
        check(hit && hit->id == "table" && std::fabs(bottom - 0.75) < 0.005 && n.y > 0.99,
              "a box cast down onto a table meets its top (its bottom at " + std::to_string(bottom) + ", facing up " + std::to_string(n.y) + ")");
        check(!w.cast(box, M3{}, {-3, 2.0, 0}, {3, 2.0, 0}, &at), "cast along above it, it meets nothing");
    }
    {
        // A sensor in the air, and another on the floor: a box dropped
        // through the first falls on through it, noted coming in and going
        // out; it comes to rest in the second, and is still in it asleep.
        World w;
        w.add(floor_body());
        Body gate;
        gate.id = "gate";
        gate.hulls.push_back(Hull::box({}, {0.5, 0.2, 0.5}));
        gate.x = {0, 1.2, 0};
        gate.set_mass(0);
        gate.sensor = true;
        w.add(gate);
        Body plate = gate;
        plate.id = "plate";
        plate.x = {0, 0.1, 0};
        w.add(plate);
        w.add(box_body("crate", {0, 2.0, 0}, {0.1, 0.1, 0.1}, 2));
        bool came = false, went = false;
        for (int i = 0; i < 240; ++i) {
            w.step(1.0 / 60.0);
            for (const auto& e : w.entered()) came |= e == World::Inside{"gate", "crate"};
            for (const auto& e : w.left()) went |= e == World::Inside{"gate", "crate"};
        }
        const bool resting = !w.find("crate")->awake && std::fabs(w.find("crate")->x.y - 0.1) < 0.01;
        check(came && went, "a box falling through a sensor is noted coming in and going out, and falls on through");
        check(resting && w.inside().count({"plate", "crate"}) && !w.inside().count({"gate", "crate"}),
              "come to rest in a sensor on the floor, it is still inside it, asleep");
    }
    // --- walkers ----------------------------------------------------------------------------
    {
        const auto fixed = [](World& w, const std::string& id, V3 at, V3 half, const M3& turn = M3{}) {
            Body b;
            b.id = id;
            b.hulls.push_back(Hull::box({}, half, turn));
            b.x = at;
            b.set_mass(0);
            w.add(b);
        };
        const auto stroll = [](World& w, Walker& p, V3 velocity, double seconds) {
            for (int i = 0; i < static_cast<int>(seconds * 60); ++i) w.walk(p, velocity * (1.0 / 60.0), 1.0 / 60.0);
        };
        {
            // Into a wall: stopped at it; walking at it aslant: along it.
            World w;
            w.add(floor_body());
            fixed(w, "wall", {3.0, 1.5, 0}, {0.1, 1.5, 5.0});
            Walker p;
            p.at = {0, 0, 0};
            stroll(w, p, {1.5, 0, 0}, 3.0);
            const bool stopped = std::fabs(p.at.x - (2.9 - p.radius)) < 0.02 && std::fabs(p.at.z) < 1e-6 && p.grounded;
            stroll(w, p, {1.0, 0, 1.0}, 1.0);
            check(stopped && std::fabs(p.at.x - (2.9 - p.radius)) < 0.02 && p.at.z > 0.9,
                  "a walker walks up to a wall and stops; aslant, slides along it (at " + std::to_string(p.at.x) + ", " + std::to_string(p.at.z) + ")");
        }
        {
            // Up a flight of eight stairs 0.15 high and 0.3 deep, and back down.
            World w;
            w.add(floor_body());
            for (int i = 0; i < 8; ++i) fixed(w, "stair" + std::to_string(i), {1.0 + 0.3 * i + 0.15, 0.075 * (i + 1), 0}, {0.15, 0.075 * (i + 1), 1.0});
            Walker p;
            stroll(w, p, {1.0, 0, 0}, 3.2);  // to the top stair
            const double up = p.at.y;
            stroll(w, p, {-1.0, 0, 0}, 4.0);
            check(std::fabs(up - 1.2) < 0.02 && std::fabs(p.at.y) < 0.02 && p.grounded,
                  "a walker climbs a flight of stairs to the top (" + std::to_string(up) + " m) and comes back down");
        }
        {
            // A ramp at 20 degrees is walked up; one at 60 is not.
            const auto ramp = [&](double degrees) {
                World w;
                w.add(floor_body());
                const double a = degrees * 3.14159265358979 / 180.0;
                // Four metres long, its foot on the floor at x = 2.
                fixed(w, "ramp", {2.0 + 2.0 * std::cos(a), 2.0 * std::sin(a) - 0.05 * std::cos(a), 0}, {2.0, 0.05, 1.0},
                      axis_angle({0, 0, 1}, a));
                Walker p;
                stroll(w, p, {1.2, 0, 0}, 4.0);
                return p.at.y;
            };
            const double gentle = ramp(20), steep = ramp(60);
            check(gentle > 0.8 && steep < 0.45, "a walker goes up a gentle ramp (" + std::to_string(gentle) + " m) but not a steep one (" +
                                                   std::to_string(steep) + " m)");
        }
        {
            // Off the edge of a table-high ledge: falls, and lands on the floor.
            World w;
            w.add(floor_body());
            fixed(w, "ledge", {0, 0.4, 0}, {1.0, 0.4, 1.0});
            Walker p;
            p.at = {0, 0.8, 0};
            stroll(w, p, {0, 0, 0}, 0.2);
            const double on_ledge = p.at.y;
            stroll(w, p, {1.5, 0, 0}, 1.5);
            check(std::fabs(on_ledge - 0.8) < 0.01 && std::fabs(p.at.y) < 0.01 && p.at.x > 1.5 && p.grounded,
                  "off the edge of a ledge, a walker falls and lands on the floor");
        }
        {
            // On a platform that moves and turns: carried with it.
            World w;
            fixed(w, "raft", {0, -0.1, 0}, {2.0, 0.1, 2.0});
            Walker p;
            p.at = {1.0, 0, 0};
            stroll(w, p, {0, 0, 0}, 0.1);
            Body& raft = *w.find("raft");
            for (int i = 0; i < 120; ++i) {
                w.moved(raft, raft.x + V3{0.02, 0, 0}, axis_angle({0, 1, 0}, 0.005) * raft.r);
                w.walk(p, {}, 1.0 / 60.0);
            }
            // Where the point it stood on went.
            const V3 expect = raft.x + axis_angle({0, 1, 0}, 0.6) * V3{1.0, 0, 0};
            check(length(V3{p.at.x - expect.x, 0, p.at.z - expect.z}) < 0.02 && p.grounded,
                  "a walker on a raft that moves and turns goes with it (off by " +
                      std::to_string(length(V3{p.at.x - expect.x, 0, p.at.z - expect.z})) + " m)");
        }
        {
            // A light crate in the way is shoved; a heavy chest is not.
            World w;
            w.add(floor_body());
            w.add(box_body("crate", {1.5, 0.2, 0}, {0.2, 0.2, 0.2}, 5));
            w.add(box_body("chest", {1.5, 0.4, 3.0}, {0.4, 0.4, 0.4}, 400));
            run(w, 1.0);
            Walker p, q;
            q.at = {0, 0, 3.0};
            for (int i = 0; i < 120; ++i) {
                w.walk(p, {1.2 / 60.0, 0, 0}, 1.0 / 60.0);
                w.walk(q, {1.2 / 60.0, 0, 0}, 1.0 / 60.0);
                w.step(1.0 / 60.0);
            }
            const double crate = w.find("crate")->x.x, chest = w.find("chest")->x.x;
            check(crate > 2.0 && chest < 1.6, "a walker shoves a light crate aside (" + std::to_string(crate) + ") but not a heavy chest (" +
                                                   std::to_string(chest) + ")");
        }
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
