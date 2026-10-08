// The rigid world's laws: what a step carries over is one cache, and with it
// put back a step tried again is the same step, to the last bit; a stack
// asleep wakes whole; and the index casts and rays go through is only an
// index - what they meet is what every body tried in turn would meet.
#include "sg/physics/Rigid.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace sg::rigid;

static int failures = 0;
static void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

// The same double, bit for bit.
static bool same(double a, double b) { return std::memcmp(&a, &b, sizeof a) == 0; }
static bool same(V3 a, V3 b) { return same(a.x, b.x) && same(a.y, b.y) && same(a.z, b.z); }
static bool same(const M3& a, const M3& b) {
    for (std::size_t i = 0; i < 9; ++i)
        if (!same(a.a[i], b.a[i])) return false;
    return true;
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

// A seeded stream of numbers in [0, 1).
struct Seeded {
    uint64_t s;
    double next() {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<double>(s >> 11) * (1.0 / 9007199254740992.0);
    }
    double in(double lo, double hi) { return lo + (hi - lo) * next(); }
};

// What one step left: every body where and how it is, what each joint
// pushed with, and what each body took from what touches it.
struct Outcome {
    std::vector<Body> bodies;
    std::vector<Joint> joints;
    std::vector<std::vector<World::Took>> took;
};

static Outcome outcome(const World& w) {
    Outcome o{w.bodies, w.joints, {}};
    for (std::size_t i = 0; i < w.bodies.size(); ++i) o.took.push_back(w.took(i));
    return o;
}

static bool same(const Outcome& a, const Outcome& b) {
    if (a.bodies.size() != b.bodies.size() || a.joints.size() != b.joints.size() || a.took.size() != b.took.size()) return false;
    for (std::size_t i = 0; i < a.bodies.size(); ++i) {
        const Body &x = a.bodies[i], &y = b.bodies[i];
        if (!same(x.x, y.x) || !same(x.r, y.r) || !same(x.v, y.v) || !same(x.w, y.w) || !same(x.idle, y.idle) || x.awake != y.awake ||
            x.island != y.island || !same(x.hit, y.hit))
            return false;
    }
    for (std::size_t k = 0; k < a.joints.size(); ++k) {
        const Joint &x = a.joints[k], &y = b.joints[k];
        if (!same(x.point, y.point) || !same(x.turn, y.turn) || !same(x.tilt1, y.tilt1) || !same(x.tilt2, y.tilt2) ||
            !same(x.drive, y.drive) || !same(x.pull, y.pull) || !same(x.low, y.low) || !same(x.high, y.high) || x.moving != y.moving)
            return false;
    }
    for (std::size_t i = 0; i < a.took.size(); ++i) {
        if (a.took[i].size() != b.took[i].size()) return false;
        for (std::size_t k = 0; k < a.took[i].size(); ++k)
            if (a.took[i][k].from != b.took[i][k].from || !same(a.took[i][k].impulse, b.took[i][k].impulse)) return false;
    }
    return true;
}

int main() {
    {
        // A step tried twice from the same start - the same bodies, the same
        // cache - is the same step, bit for bit, whatever was tried between:
        // a stack of crates, a door swung by its motor against its stop, a
        // chain hung from the ceiling and a weight on a spring.
        World w;
        w.add(floor_body());
        for (int i = 0; i < 3; ++i) w.add(box_body("c" + std::to_string(i), {0.01 * (i % 2), 0.2 + 0.401 * i, 0}, {0.2, 0.2, 0.2}, 5));
        w.add(box_body("door", {2.45, 1.0, 0}, {0.45, 1.0, 0.02}, 20));
        Joint& door = w.hinge("door", "", {2.0, 1.0, 0}, {0, 1, 0});
        door.motor = true, door.speed = 0.8, door.torque = 40;
        door.limit = true, door.lower = -1.2, door.upper = 1.2;
        for (int i = 0; i < 3; ++i) {
            w.add(box_body("link" + std::to_string(i), {-2.0, 2.3 - 0.3 * i, 0}, {0.03, 0.12, 0.03}, 0.5));
            w.ball("link" + std::to_string(i), i == 0 ? "" : "link" + std::to_string(i - 1), {-2.0, 2.42 - 0.3 * i, 0});
        }
        w.add(box_body("weight", {0, 2.0, 2.0}, {0.1, 0.1, 0.1}, 2));
        w.spring("weight", "", {0, 2.1, 2.0}, {0, 2.6, 2.0}, 3.0, 0.3);
        w.find("link2")->v = {0.8, 0, 0.3};
        for (int i = 0; i < 40; ++i) w.step(1.0 / 60.0, i / 60.0);

        const std::vector<Body> start = w.bodies;
        const World::Contacts cache = w.contacts();
        w.step(1.0 / 60.0, 40 / 60.0);
        const Outcome first = outcome(w);
        // Another trial between: other steps, of other lengths, leave every
        // contact and joint pushing with something else.
        for (int i = 0; i < 7; ++i) w.step(1.0 / 45.0, 1.0 + i / 45.0);
        const bool moved = !same(w.joints[0].drive, cache.joints[0].drive) || !same(w.joints[0].point, cache.joints[0].point);
        check(moved, "a trial between leaves the joints pushing with something else");
        w.bodies = start;
        w.set_contacts(cache);
        w.step(1.0 / 60.0, 40 / 60.0);
        check(same(first, outcome(w)), "stepped twice from the same start and cache, the world is the same to the last bit");
        // And what each took is told in order of whom from.
        bool ordered = true;
        for (std::size_t i = 0; i < w.bodies.size(); ++i) {
            const auto t = w.took(i);
            for (std::size_t k = 1; k < t.size(); ++k) ordered = ordered && t[k - 1].from < t[k].from;
        }
        check(ordered, "what a body took is told by whom from, in order");
        // The cache names the joints it holds: put back into another world's
        // joints, a joint it does not name starts from nothing.
        World::Contacts other = cache;
        other.joints[0].a = "someone else";
        w.set_contacts(other);
        check(same(w.joints[0].drive, 0.0) && length(w.joints[0].point) == 0.0 && same(w.joints[1].point, cache.joints[1].point),
              "a joint the cache does not name starts from nothing; the rest are put back");
    }
    {
        // A stack asleep keeps who it slept with: one of it woken wakes it
        // all, and a crate taken from under it leaves nothing in the air.
        World w;
        w.add(floor_body());
        for (int i = 0; i < 3; ++i) w.add(box_body("c" + std::to_string(i), {0, 0.2 + 0.401 * i, 0}, {0.2, 0.2, 0.2}, 5));
        for (int i = 0; i < 360 && w.any_awake(); ++i) w.step(1.0 / 60.0);
        const Body *c0 = w.find("c0"), *c1 = w.find("c1"), *c2 = w.find("c2");
        check(!c0->awake && !c1->awake && !c2->awake && c0->island >= 0 && c0->island == c1->island && c1->island == c2->island,
              "a stack falls asleep as one island");
        w.wake(*w.find("c2"));
        check(w.find("c0")->awake && w.find("c1")->awake && w.find("c2")->awake, "the top woken, the whole stack wakes");
        for (int i = 0; i < 360 && w.any_awake(); ++i) w.step(1.0 / 60.0);
        check(!w.find("c0")->awake && !w.find("c2")->awake, "and sleeps again");
        // Contacts kept across a removal: renumbered, not forgotten.
        w.wake(*w.find("c1"));
        w.step(1.0 / 60.0);
        const std::size_t before = w.touching().size();
        w.remove("c0");
        check(before > 0 && !w.touching().empty() && w.touching().size() < before, "a body removed takes its contacts; the others keep theirs");
        for (int i = 0; i < 120; ++i) w.step(1.0 / 60.0);
        check(w.find("c1")->x.y < 0.25 && w.find("c2")->x.y < 0.65,
              "the bottom crate taken away, the two above fall to the floor (" + std::to_string(w.find("c1")->x.y) + ")");
    }
    {
        // A crate pulled away from under a sleeping one: the one on it is of
        // its island, so goes with it rather than staying in the air.
        World w;
        w.add(floor_body());
        w.add(box_body("base", {0, 0.2, 0}, {0.2, 0.2, 0.2}, 5));
        w.add(box_body("top", {0, 0.601, 0}, {0.2, 0.2, 0.2}, 5));
        for (int i = 0; i < 360 && w.any_awake(); ++i) w.step(1.0 / 60.0);
        check(!w.find("top")->awake, "two crates, one on the other, sleep");
        Body& base = *w.find("base");
        w.teleport(base, {3, 0.2, 0}, M3{});
        for (int i = 0; i < 90; ++i) w.step(1.0 / 60.0);
        check(w.find("top")->x.y < 0.25, "the lower one taken from under it, the upper one falls (" + std::to_string(w.find("top")->x.y) + ")");
    }
    {
        // Casts and rays through the index meet what every body tried in turn
        // meets: the same thing, as far along, facing the same way - on a
        // seeded scene, and again after bodies are moved between steps.
        World indexed, every;
        every.sweep = false;
        Seeded rng{20261008};
        indexed.add(floor_body());
        every.add(floor_body());
        for (int i = 0; i < 90; ++i) {
            const V3 at{rng.in(-6, 6), rng.in(0, 3), rng.in(-6, 6)};
            const V3 half{rng.in(0.05, 0.6), rng.in(0.05, 0.6), rng.in(0.05, 0.6)};
            const M3 turn = from_euler(rng.in(-3, 3), rng.in(-1, 1), rng.in(-1, 1));
            Body b = box_body("b" + std::to_string(i), at, half, i % 3 == 0 ? 0.0 : rng.in(0.5, 20), turn);
            b.sensor = i % 17 == 5;
            indexed.add(b);
            every.add(b);
        }
        const auto agree = [&](Seeded& r, int n) {
            bool ok = true;
            for (int k = 0; k < n; ++k) {
                const V3 from{r.in(-7, 7), r.in(0, 3), r.in(-7, 7)}, to{r.in(-7, 7), r.in(-0.5, 3), r.in(-7, 7)};
                const Hull shape = Hull::prism({}, {r.in(0.05, 0.4), r.in(0.05, 0.4), r.in(0.05, 0.4)}, k % 2 ? 8 : 4);
                const std::string skip = k % 5 == 0 ? "b" + std::to_string(k % 90) : std::string{};
                double ta = -1, tb = -1;
                V3 na, nb;
                const Body* a = indexed.cast(shape, M3{}, from, to, &ta, &na, skip);
                const Body* b = every.cast(shape, M3{}, from, to, &tb, &nb, skip);
                ok = ok && (a ? a->id : "") == (b ? b->id : "") && same(ta, tb) && (!a || same(na, nb));
                const V3 d = normalize(to - from);
                double ra = -1, rb = -1;
                V3 ma, mb;
                const bool dyn = k % 3 == 0;
                const Body* x = indexed.ray(from, d, 12.0, &ra, dyn, skip, &ma);
                const Body* y = every.ray(from, d, 12.0, &rb, dyn, skip, &mb);
                ok = ok && (x ? x->id : "") == (y ? y->id : "") && same(ra, rb) && (!x || same(ma, mb));
            }
            return ok;
        };
        Seeded q1{7};
        check(agree(q1, 300), "casts and rays through the index meet what every body tried in turn meets");
        // Moved between steps, by whoever moves them: the index follows.
        Seeded mv{99};
        for (int i = 0; i < 30; ++i) {
            const std::string id = "b" + std::to_string((i * 7) % 90);
            const V3 to{mv.in(-6, 6), mv.in(0, 3), mv.in(-6, 6)};
            const M3 turn = from_euler(mv.in(-3, 3), 0, 0);
            indexed.teleport(*indexed.find(id), to, turn);
            every.teleport(*every.find(id), to, turn);
        }
        Seeded q2{8};
        check(agree(q2, 300), "and still, the bodies moved between steps");
        for (int i = 0; i < 20; ++i) indexed.step(1.0 / 60.0), every.step(1.0 / 60.0);
        Seeded q3{9};
        check(agree(q3, 300), "and still, after steps");
    }
    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
