// A being's motion, exactly: a leg of two bones reaching a point solved as a
// triangle, a cubic clip played by its tangents as glTF says, and a clip's
// keys thinned only where nothing a body shows would move.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "sg/core/Engine.hpp"
#include "sg/core/Laws.hpp"
#include "sg/core/Temporal.hpp"
#include "sg/domains/Being.hpp"
#include "sg/domains/Spatial.hpp"

static int failures = 0;
static void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    failures += ok ? 0 : 1;
}

namespace {

// A turn (w x y z) and its arithmetic, written out here so the checks do not
// lean on the code they check.
struct Q4 {
    double w = 1, x = 0, y = 0, z = 0;
};
double dot4(const Q4& a, const Q4& b) { return a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z; }
Q4 unit(Q4 q) {
    const double n = std::sqrt(dot4(q, q));
    return {q.w / n, q.x / n, q.y / n, q.z / n};
}
Q4 slerp(Q4 a, Q4 b, double t) {
    double c = dot4(a, b);
    if (c < 0) b = {-b.w, -b.x, -b.y, -b.z}, c = -c;
    double ka = 1 - t, kb = t;
    if (c <= 0.9995) {
        const double th = std::acos(std::min(1.0, c)), s = std::sin(th);
        ka = std::sin((1 - t) * th) / s, kb = std::sin(t * th) / s;
    }
    return unit({a.w * ka + b.w * kb, a.x * ka + b.x * kb, a.y * ka + b.y * kb, a.z * ka + b.z * kb});
}
Q4 about(sg::Vec3d axis, double a) {
    const double l = sg::length(axis), s = std::sin(a / 2) / l;
    return {std::cos(a / 2), axis.x * s, axis.y * s, axis.z * s};
}
// How far a point `reach` from the joint is moved between two turns.
double chord(const Q4& a, const Q4& b, double reach) {
    const double c = std::min(1.0, std::fabs(dot4(unit(a), unit(b))));
    return 2 * reach * std::sqrt(std::max(0.0, 1 - c * c));
}

}  // namespace

int main() {
    {
        // A leg: a hip, a knee a little bent, an ankle - stiff enough to be
        // where it is meant at once, so the solve itself is what is measured.
        sg::StateGraph g;
        auto& clock = g.add<sg::Temporal>("clock");
        auto& b = g.add<sg::Being>(sg::Key{"leg"});
        b.joint("hip", "", {0, 1, 0}, {}, 1e6);
        b.joint("knee", "hip", {0, -0.45, 0}, {0, 10, 0}, 1e6);
        b.joint("ankle", "knee", {0, -0.45, 0}, {}, 1e6);
        b.goal("foot", "ankle", 2);
        sg::drive(g, clock, "leg", b.live_event());
        g.set_initial("leg");
        const sg::LawReport r = sg::verify(g);
        for (const auto& v : r.violations) std::printf("     %s: %s\n", v.where.c_str(), v.detail.c_str());
        check(r.structure.empty() && r.ok(), "a leg of two bones keeps the laws");
        sg::Engine e(g);
        e.set_strict(true);
        e.start();
        const sg::Vec3d aim{0.2, 0.35, 0.1};
        e.fire(sg::Event{b.reach_event(), sg::Params{}.set("goal", std::string("foot")).set(sg::keys::x, aim.x).set(sg::keys::y, aim.y).set(sg::keys::z, aim.z).set("fade", 1e-4)});
        for (int i = 0; i < 4; ++i) e.tick(1.0 / 60);
        const double off = sg::distance(b.pose_of("ankle").position, aim);
        check(off < 1e-4, "two bones reach a point they can reach exactly (" + std::to_string(off) + " m off)");
        const double thigh = sg::distance(b.pose_of("knee").position, b.pose_of("hip").position);
        const double shin = sg::distance(b.pose_of("ankle").position, b.pose_of("knee").position);
        check(std::fabs(thigh - 0.45) < 1e-9 && std::fabs(shin - 0.45) < 1e-9, "and its bones are as long as they were");

        // With a pole: the knee goes to its side of the line from hip to foot.
        const sg::Vec3d pole{0.0, 0.6, 1.0};
        e.fire(sg::Event{b.reach_event(), sg::Params{}
                                              .set("goal", std::string("foot"))
                                              .set(sg::keys::x, aim.x)
                                              .set(sg::keys::y, aim.y)
                                              .set(sg::keys::z, aim.z)
                                              .set("fade", 1e-4)
                                              .set("pole_x", pole.x)
                                              .set("pole_y", pole.y)
                                              .set("pole_z", pole.z)});
        for (int i = 0; i < 4; ++i) e.tick(1.0 / 60);
        const sg::Vec3d hip = b.pose_of("hip").position, knee = b.pose_of("knee").position;
        const sg::Vec3d dir = (aim - hip) * (1.0 / sg::length(aim - hip));
        const auto across = [&](const sg::Vec3d& p) {
            const sg::Vec3d v = p - hip;
            return v - dir * sg::dot(v, dir);
        };
        const double side = sg::dot(across(knee), across(pole));
        const double off2 = sg::distance(b.pose_of("ankle").position, aim);
        check(side > 0 && off2 < 1e-4, "with a pole, the knee bends toward it and the foot is still on the point (" + std::to_string(off2) + " m off)");

        // Out of reach and softened: straightened toward it, never quite, and pointing at it.
        const sg::Vec3d far{0.0, -0.5, 0.6};
        e.fire(sg::Event{b.reach_event(), sg::Params{}
                                              .set("goal", std::string("foot"))
                                              .set(sg::keys::x, far.x)
                                              .set(sg::keys::y, far.y)
                                              .set(sg::keys::z, far.z)
                                              .set("fade", 1e-4)
                                              .set("soften", 0.9)});
        for (int i = 0; i < 4; ++i) e.tick(1.0 / 60);
        const sg::Vec3d ankle = b.pose_of("ankle").position;
        const double reach = sg::distance(ankle, hip), along = sg::dot(ankle - hip, (far - hip) * (1.0 / sg::length(far - hip)));
        check(reach < 0.9 && reach > 0.85 && std::fabs(along - reach) < 1e-6, "out of reach, softened: nearly straight, toward the point (" + std::to_string(reach) + " m)");
    }
    {
        // A cubic clip (glTF's CUBICSPLINE): value, tangent in, tangent out a
        // key; between two keys Hermite's cubic, its tangents scaled by the
        // time between them, then made a unit turn again.
        sg::StateGraph g;
        auto& b = g.add<sg::Being>(sg::Key{"cubic"});
        b.joint("j", "", {0, 0, 0});
        const double c = std::sqrt(0.5);
        b.clip("swing",
               "0 j qc 1 0 0 0  0 0 0 0  0 0 0.3 0.8\n"
               "2 j qc 0.70710678118654757 0 0 0.70710678118654757  0 0.1 0 0.5  0 0 0 0\n",
               false);
        const Q4 v0{1, 0, 0, 0}, out0{0, 0, 0.3, 0.8}, v1{c, 0, 0, c}, in1{0, 0.1, 0, 0.5};
        double most = 0;
        for (double t : {0.0, 0.3, 0.9, 1.4, 2.0}) {
            const double span = 2.0, s = t / span, s2 = s * s, s3 = s2 * s;
            const double h00 = 2 * s3 - 3 * s2 + 1, h10 = s3 - 2 * s2 + s, h01 = -2 * s3 + 3 * s2, h11 = s3 - s2;
            const auto at = [&](double a0, double t0, double a1, double t1) { return h00 * a0 + h10 * span * t0 + h01 * a1 + h11 * span * t1; };
            const Q4 want = unit({at(v0.w, out0.w, v1.w, in1.w), at(v0.x, out0.x, v1.x, in1.x), at(v0.y, out0.y, v1.y, in1.y), at(v0.z, out0.z, v1.z, in1.z)});
            double q[4];
            if (!b.sample("swing", sg::Key{"j"}, t, q)) most = 1e9;
            const Q4 got{q[0], q[1], q[2], q[3]};
            most = std::max(most, std::sqrt(std::max(0.0, 1 - dot4(got, want) * dot4(got, want))));
        }
        check(most < 1e-6, "a cubic clip is sampled as glTF says, by Hermite's cubic (" + std::to_string(most) + " apart at most)");
        // And a linear one by halving to its key, then slerped.
        std::string keys;
        for (int k = 0; k <= 200; ++k) keys += std::to_string(k * 0.05) + " j 0 " + std::to_string(k * 0.4) + " 0\n";
        b.clip("many", keys, false);
        double q[4];
        b.sample("many", sg::Key{"j"}, 7.025, q);
        // (Every key turns about one axis, so the check is the angle alone:
        // halfway between 56.0 and 56.4 degrees.)
        const double angle = 2 * std::acos(std::min(1.0, std::fabs(q[0]))) * 180 / 3.14159265358979;
        check(std::fabs(angle - 56.2) < 1e-3, "a clip of many keys is found by halving and slerped between two (" + std::to_string(angle) + " degrees)");
    }
    {
        // Keys thinned: a turn sampled thirty times a second, smooth, then
        // still; every key thrown away still within the tolerance at the
        // joint's furthest descendant.
        std::vector<double> times, values, places;
        const sg::Vec3d axis{0.3, 1.0, 0.2};
        for (int k = 0; k <= 300; ++k) {
            const double t = k / 30.0;
            const double a = t < 7 ? 0.8 * std::sin(1.3 * t) + 0.1 * std::sin(3 * t) : 0.8 * std::sin(1.3 * 7) + 0.1 * std::sin(3 * 7);
            const Q4 q = about(axis, a);
            times.push_back(t);
            values.insert(values.end(), {q.w, q.x, q.y, q.z});
            places.insert(places.end(), {0.1 * t, 0.9 + 0.05 * std::sin(6 * t), 0.0});
        }
        const double reach = 0.9, tol = 0.0005;
        const std::vector<std::size_t> kept = sg::kept_keys(times, values, 4, reach, tol);
        double worst = 0;
        std::size_t at = 0;
        for (std::size_t m = 0; m < times.size(); ++m) {
            while (at + 1 < kept.size() && kept[at + 1] <= m) ++at;
            const std::size_t a = kept[at], b = at + 1 < kept.size() ? kept[at + 1] : a;
            const double s = b > a ? (times[m] - times[a]) / (times[b] - times[a]) : 0.0;
            const Q4 qa{values[a * 4], values[a * 4 + 1], values[a * 4 + 2], values[a * 4 + 3]};
            const Q4 qb{values[b * 4], values[b * 4 + 1], values[b * 4 + 2], values[b * 4 + 3]};
            const Q4 qm{values[m * 4], values[m * 4 + 1], values[m * 4 + 2], values[m * 4 + 3]};
            worst = std::max(worst, chord(slerp(qa, qb, s), qm, reach));
        }
        check(!kept.empty() && kept.front() == 0 && kept.back() == times.size() - 1, "thinned, a track keeps its first key and its last");
        check(kept.size() < times.size() / 2, "and far fewer keys (" + std::to_string(kept.size()) + " of " + std::to_string(times.size()) + ")");
        check(worst < tol, "and its furthest descendant within the tolerance at every key (" + std::to_string(worst * 1000) + " mm)");
        const std::vector<std::size_t> kp = sg::kept_keys(times, places, 3, 1.0, tol);
        double worst_p = 0;
        at = 0;
        for (std::size_t m = 0; m < times.size(); ++m) {
            while (at + 1 < kp.size() && kp[at + 1] <= m) ++at;
            const std::size_t a = kp[at], b = at + 1 < kp.size() ? kp[at + 1] : a;
            const double s = b > a ? (times[m] - times[a]) / (times[b] - times[a]) : 0.0;
            double d2 = 0;
            for (int i = 0; i < 3; ++i) {
                const double v = places[a * 3 + i] + (places[b * 3 + i] - places[a * 3 + i]) * s - places[m * 3 + i];
                d2 += v * v;
            }
            worst_p = std::max(worst_p, std::sqrt(d2));
        }
        check(kp.size() < times.size() && worst_p < tol, "a travel thinned the same, within the tolerance (" + std::to_string(kp.size()) + " keys)");
    }
    {
        // How fast the clips turn a joint (`aw`), read from the clips; and a
        // layer masked to one leg moves only that leg.
        sg::StateGraph g;
        auto& clock = g.add<sg::Temporal>("clock");
        auto& b = g.add<sg::Being>(sg::Key{"walker"});
        sg::humanoid(b, 1.75);
        sg::drive(g, clock, "walker", b.live_event());
        g.set_initial("walker");
        sg::Engine e(g);
        e.set_strict(true);
        e.start();
        e.fire(sg::Event{b.play_event(), sg::Params{}.set("clip", std::string("walk")).set("fade", 0.0).set("mask", std::string("hips 0, thigh_l 1"))});
        const sg::Vec3d right0 = b.pose_of("foot_r").position;
        double left = 0, right = 0, spin = 0;
        for (int i = 0; i < 60; ++i) {
            e.tick(1.0 / 60);
            const sg::Element& th = b.element(sg::Key{"thigh_l"});
            spin = std::max(spin, std::sqrt(th.params.num("awx") * th.params.num("awx") + th.params.num("awy") * th.params.num("awy") +
                                            th.params.num("awz") * th.params.num("awz")));
            left = std::max(left, std::fabs(b.pose_of("foot_l").position.x));
            right = std::max(right, sg::distance(b.pose_of("foot_r").position, right0));
        }
        check(left > 0.1 && right < 1e-6, "masked to the left leg, only the left leg walks (" + std::to_string(left) + ", " + std::to_string(right) + ")");
        check(spin > 1.0 && spin <= 20.0, "and the clip says how fast the thigh swings (" + std::to_string(spin) + " rad/s at most)");
    }
    return failures == 0 ? 0 : 1;
}
