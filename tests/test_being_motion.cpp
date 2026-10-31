// A being's motion, exactly: a leg of two bones reaching a point solved as a
// triangle, a cubic clip played by its tangents as glTF says, a clip's keys
// thinned only where nothing a body shows would move, feet set down on the
// ground as it goes, one motion changed into another inertialized, and one
// body in two places carried on from where the other left it.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <tuple>
#include <vector>

#include "sg/core/Engine.hpp"
#include "sg/core/Laws.hpp"
#include "sg/core/Temporal.hpp"
#include "sg/domains/Being.hpp"
#include "sg/domains/Cast.hpp"
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
    {
        // Its feet, found by its shape; how fast a walk's ground goes, read
        // from the walk; and a foot set down stays where it was put while the
        // body goes on over it - even going faster than its walk. Two legs
        // of 0.45 + 0.45 m stepping in place: each thigh swung from 20 degrees
        // ahead to 20 behind with its knee straight (on the ground), and back
        // with its knee bent (lifted) - the ground going 2 L sin 20 a half
        // second under it.
        const auto walker = [](sg::StateGraph& g, bool plant) -> sg::Being& {
            auto& clock = g.add<sg::Temporal>("clock");
            auto& b = g.add<sg::Being>(sg::Key{"walker"});
            b.joint("hips", "", {0, 0.92, 0}, {}, 400);
            std::string keys;
            for (const auto& [side, z, lag] : {std::tuple{std::string("_l"), 0.1, 0.0}, std::tuple{std::string("_r"), -0.1, 0.5}}) {
                b.joint("thigh" + side, "hips", {0, 0, z}, {}, 400);
                b.joint("shin" + side, "thigh" + side, {0, -0.45, 0}, {}, 400);
                b.joint("foot" + side, "shin" + side, {0, -0.45, 0}, {}, 400);
                b.joint("toe" + side, "foot" + side, {0.12, -0.02, 0}, {}, 400);
                for (int i = 0; i <= 8; ++i) {
                    const double u = std::fmod(i / 8.0 + lag, 1.0), t = i / 8.0;
                    const double thigh = u < 0.5 ? 20 - 80 * u : -20 + 80 * (u - 0.5), knee = u < 0.5 ? 0.0 : -40 * std::sin((u - 0.5) * 2 * 3.14159265358979);
                    keys += std::to_string(t) + " thigh" + side + " 0 " + std::to_string(thigh) + " 0\n" + std::to_string(t) + " shin" + side + " 0 " + std::to_string(knee) + " 0\n";
                }
            }
            b.clip("walk", keys);
            if (plant)
                for (sg::Key a : b.ankles()) b.plant(a.str());
            sg::drive(g, clock, "walker", b.live_event());
            g.set_initial("walker");
            return b;
        };
        sg::StateGraph g0;
        sg::Being& b0 = walker(g0, false);
        const std::vector<sg::Key> feet = b0.ankles();
        check(feet.size() == 2 && ((feet[0] == sg::Key{"foot_l"} && feet[1] == sg::Key{"foot_r"}) || (feet[0] == sg::Key{"foot_r"} && feet[1] == sg::Key{"foot_l"})),
              "a body's ankles are found by its shape: the two feet, not its toes or knees");
        sg::StateGraph gh;
        auto& person = gh.add<sg::Being>(sg::Key{"person"});
        sg::humanoid(person, 1.75);
        check(person.ankles().size() == 2, "and a person's two feet the same");
        // An MHR rig: low off each shin hang its twist helpers (leaves,
        // `l_lowleg_twist1..4_proc`) beside the foot, whose own chain goes on
        // to the ball - its ankle the foot, one a leg, never a helper.
        {
            sg::StateGraph gm;
            auto& mhr = gm.add<sg::Being>(sg::Key{"mhr"});
            mhr.joint("root", "", {0, 0.95, 0});
            for (const auto& [side, z] : {std::pair{std::string("l_"), 0.1}, std::pair{std::string("r_"), -0.1}}) {
                mhr.joint(side + "upleg", "root", {0, -0.05, z});
                mhr.joint(side + "lowleg", side + "upleg", {0, -0.42, 0});
                for (int k = 1; k <= 4; ++k) mhr.joint(side + "lowleg_twist" + std::to_string(k) + "_proc", side + "lowleg", {0, -0.09 * k, 0});
                mhr.joint(side + "foot", side + "lowleg", {0, -0.40, 0});
                mhr.joint(side + "talocrural", side + "foot", {0, -0.01, 0});
                mhr.joint(side + "subtalar", side + "talocrural", {0.02, -0.02, 0});
                mhr.joint(side + "ball", side + "subtalar", {0.14, -0.04, 0});
            }
            const std::vector<sg::Key> found = mhr.ankles();
            std::string said;
            for (sg::Key k : found) said += " " + k.str();
            check(found.size() == 2 && std::count(found.begin(), found.end(), sg::Key{"l_foot"}) == 1 && std::count(found.begin(), found.end(), sg::Key{"r_foot"}) == 1,
                  "an MHR rig's ankles are its feet, not the twist helpers hanging off its shins (found" + said + ")");
        }
        const double ground = b0.ground_speed("walk"), made = 2 * 0.9 * std::sin(20 * 3.14159265358979 / 180) / 0.5;
        check(std::fabs(ground - made) < 0.15 * made, "a walk in place says how fast its ground goes (" + std::to_string(ground) + " m/s, made " + std::to_string(made) + ")");
        // Gone a fifth faster than its walk: unplanted, its feet slide.
        double slid[2] = {0, 0}, down[2] = {0, 0};
        for (int planted = 0; planted < 2; ++planted) {
            sg::StateGraph g;
            sg::Being& b = walker(g, planted == 1);
            if (planted) {
                const sg::LawReport r = sg::verify(g);
                for (const auto& v : r.violations) std::printf("     %s: %s\n", v.where.c_str(), v.detail.c_str());
                check(r.structure.empty() && r.ok(), "a being whose feet are set down keeps the laws");
            }
            sg::Engine e(g);
            e.set_strict(true);
            e.start();
            const double v = 1.2 * ground;
            e.fire(sg::Event{b.play_event(), sg::Params{}.set("clip", std::string("walk")).set("fade", 0.0)});
            e.fire(sg::Event{b.travel_event(), sg::Params{}.set(sg::keys::x, v)});
            double x = 0, was = 0;
            bool had = false;
            for (int i = 0; i < 300; ++i) {
                e.tick(1.0 / 60);
                x += v / 60;
                // (On the ground: the left foot's first half of its walk, its middle.)
                const double at = x + b.pose_of("foot_l").position.x, phase = std::fmod((i + 1) / 60.0, 1.0);
                const bool on = phase > 0.05 && phase < 0.4;
                if (i > 60 && on && had) slid[planted] += std::fabs(at - was), down[planted] += 1.0 / 60;
                was = at, had = on;
            }
        }
        check(down[1] > 0.5 && slid[0] > 0.3 && slid[1] < 0.15 * slid[0],
              "a foot set down stays on the ground as the body goes faster than its walk (" + std::to_string(slid[1]) + " m slid in " + std::to_string(down[1]) +
                  " s on it; unplanted " + std::to_string(slid[0]) + " m)");
    }
    {
        // On uneven ground: a sole set down turned to the ground under it; its
        // hips leaned up a slope; what each footfall stood on; and its balance
        // - swayed by its load off-centre, steadied back, past its bounds a
        // stumble. And told by pace from the land itself: the slope round it,
        // the way the ground faces under each foot.
        const auto stander = [](sg::StateGraph& g) -> sg::Being& {
            auto& clock = g.add<sg::Temporal>("clock");
            auto& b = g.add<sg::Being>(sg::Key{"walker"});
            b.joint("hips", "", {0, 0.92, 0}, {}, 400);
            std::string keys;
            for (const auto& [side, z, lag] : {std::tuple{std::string("_l"), 0.1, 0.0}, std::tuple{std::string("_r"), -0.1, 0.5}}) {
                b.joint("thigh" + side, "hips", {0, 0, z}, {}, 400);
                b.joint("shin" + side, "thigh" + side, {0, -0.45, 0}, {}, 400);
                b.joint("foot" + side, "shin" + side, {0, -0.45, 0}, {}, 400);
                b.joint("toe" + side, "foot" + side, {0.12, -0.02, 0}, {}, 400);
                for (int i = 0; i <= 8; ++i) {
                    const double u = std::fmod(i / 8.0 + lag, 1.0), t = i / 8.0;
                    const double thigh = u < 0.5 ? 20 - 80 * u : -20 + 80 * (u - 0.5), knee = u < 0.5 ? 0.0 : -40 * std::sin((u - 0.5) * 2 * 3.14159265358979);
                    keys += std::to_string(t) + " thigh" + side + " 0 " + std::to_string(thigh) + " 0\n" + std::to_string(t) + " shin" + side + " 0 " + std::to_string(knee) + " 0\n";
                }
            }
            b.clip("walk", keys);
            for (sg::Key a : b.ankles()) b.plant(a.str());
            sg::drive(g, clock, "walker", b.live_event(), false, sg::Keeps::Always);
            g.set_initial("walker");
            return b;
        };
        const double a = 0.25;
        double pitch[2] = {0, 0};
        for (int tilted = 0; tilted < 2; ++tilted) {
            sg::StateGraph g;
            sg::Being& b = stander(g);
            sg::Engine e(g);
            e.start();
            sg::Params travel;
            if (tilted) travel.set("tilt_foot_l_x", -std::sin(a)).set("tilt_foot_l_z", 0.0).set("tilt_foot_r_x", -std::sin(a)).set("tilt_foot_r_z", 0.0);
            e.fire(sg::Event{b.travel_event(), travel});
            for (int i = 0; i < 90; ++i) e.tick(1.0 / 60);
            pitch[tilted] = b.pose_of("foot_l").pitch;
        }
        check(std::fabs(std::fabs(pitch[1] - pitch[0]) - a) < 0.05,
              "a sole set down on ground rising ahead is turned to it (" + std::to_string(pitch[1] - pitch[0]) + " rad, the ground " + std::to_string(a) + ")");
        {
            sg::StateGraph g;
            sg::Being& b = stander(g);
            sg::Engine e(g);
            e.start();
            const double flat = b.pose_of("hips").pitch;
            e.fire(sg::Event{b.travel_event(), sg::Params{}.set("slope_ahead", 0.3).set("slope_side", 0.0)});
            for (int i = 0; i < 120; ++i) e.tick(1.0 / 60);
            const double up = b.pose_of("hips").pitch - flat;
            check(std::fabs(std::fabs(up) - 0.35 * 0.3) < 0.03, "its hips leaned up a slope ahead (" + std::to_string(up) + " rad for a slope of 0.3)");
        }
        {
            sg::StateGraph g;
            sg::Being& b = stander(g);
            sg::Engine e(g);
            e.start();
            const double ground = b.ground_speed("walk");
            e.fire(sg::Event{b.play_event(), sg::Params{}.set("clip", std::string("walk")).set("fade", 0.0)});
            e.fire(sg::Event{b.travel_event(), sg::Params{}.set(sg::keys::x, ground).set("on_foot_l", std::string("gravel")).set("on_foot_r", std::string("gravel"))});
            for (int i = 0; i < 180; ++i) e.tick(1.0 / 60);
            const sg::Params& self = b.element(sg::Being::self_id()).params;
            check(self.num("footfalls") > 0 && self.get_or<std::string>("footfall_on", "") == "gravel",
                  "each footfall says what it stood on (" + std::to_string(static_cast<int>(self.num("footfalls"))) + " on " + self.get_or<std::string>("footfall_on", "") + ")");
        }
        {
            sg::StateGraph g;
            sg::Being& b = stander(g);
            sg::Engine e(g);
            e.set_strict(true);
            e.start();
            const sg::Params& self = b.element(sg::Being::self_id()).params;
            e.fire(sg::Event{b.balance_event(), sg::Params{}.set("load", 30.0).set("load_x", 0.0).set("load_z", 0.5)});
            for (int i = 0; i < 300; ++i) e.tick(1.0 / 60);
            const double loaded = self.num("balance");
            e.fire(sg::Event{b.balance_event(), sg::Params{}.set("steady_z", 0.2)});
            for (int i = 0; i < 300; ++i) e.tick(1.0 / 60);
            const double steadied = self.num("balance");
            check(loaded < 0.75 && steadied > 0.95, "a load to one side sways it (balance " + std::to_string(loaded) + "); steadied against it, it stands (" +
                                                       std::to_string(steadied) + ")");
            const sg::LawReport r = sg::verify(g);
            check(r.structure.empty() && r.ok(), "and its balance keeps the laws: a step of its own, the same stepped again");
            e.fire(sg::Event{b.balance_event(), sg::Params{}.set("load", 100.0).set("steady_z", 0.0)});
            for (int i = 0; i < 600; ++i) e.tick(1.0 / 60);
            check(self.num("stumbles") >= 1.0, "too much of it, it stumbles (" + std::to_string(static_cast<int>(self.num("stumbles"))) + ")");
        }
        {
            // Land rising 0.3 a metre along x, a being standing on it facing +x.
            sg::StateGraph g;
            sg::Being& b = stander(g);
            auto& land = g.add<sg::Spatial3D>("land");
            land.terrain(sg::Key{"hill"}, [](double x, double) { return 0.3 * x; });
            land.fixture(sg::spot_of(land, b.id()), 5.0, 1.5, 5.0);
            const std::vector<sg::Event> told = sg::pace(land, b, {0.5, 0.0, 0.0}, 0.0);
            const sg::Event* travel = nullptr;
            for (const sg::Event& ev : told)
                if (ev.name == b.travel_event()) travel = &ev;
            const double want = -0.3 / std::sqrt(1.09);
            check(travel && std::fabs(travel->args.num("slope_ahead") - std::atan(0.3)) < 0.02 && std::fabs(travel->args.num("tilt_foot_l_x") - want) < 0.02,
                  "pace tells it the land: rising ahead " + std::to_string(travel ? travel->args.num("slope_ahead") : 0.0) + " rad, the ground under its foot facing back " +
                      std::to_string(travel ? travel->args.num("tilt_foot_l_x") : 0.0));
        }
    }
    {
        // Inertialized: from standing to walking at once, the difference let
        // go of - each joint goes on as it was going, no kick, and is the
        // walk's own a few half-lives on.
        double kick[2] = {0, 0};
        sg::Vec3d end[2];
        for (int inert = 0; inert < 2; ++inert) {
            sg::StateGraph g;
            auto& clock = g.add<sg::Temporal>("clock");
            auto& b = g.add<sg::Being>(sg::Key{"walker"});
            sg::humanoid(b, 1.75);
            sg::drive(g, clock, "walker", b.live_event());
            g.set_initial("walker");
            sg::Engine e(g);
            e.set_strict(true);
            e.start();
            e.fire(sg::Event{b.play_event(), sg::Params{}.set("clip", std::string("idle")).set("fade", 0.0)});
            for (int i = 0; i < 60; ++i) e.tick(1.0 / 60);
            e.fire(sg::Event{b.play_event(), sg::Params{}.set("clip", std::string("walk")).set("fade", 0.0).set("inert", inert ? 0.1 : 0.0)});
            sg::Vec3d p0 = b.pose_of("foot_l").position, p1 = p0;
            for (int i = 0; i < 90; ++i) {
                e.tick(1.0 / 60);
                const sg::Vec3d p = b.pose_of("foot_l").position;
                // How much faster or slower the foot goes from one step to the next.
                if (i < 20) kick[inert] = std::max(kick[inert], sg::length((p - p1) - (p1 - p0)) * 3600.0);
                p0 = p1, p1 = p;
            }
            end[inert] = p1;
            if (inert) check(b.element(sg::Being::self_id()).params.num("inert") < 0.5, "and once it is let go of, nothing is kept of it");
        }
        check(kick[1] < 0.7 * kick[0], "inertialized, a body starts walking with less of a kick (" + std::to_string(kick[1]) + " m/s2, faded at once " + std::to_string(kick[0]) + ")");
        check(sg::distance(end[0], end[1]) < 0.01, "and a second and a half on it walks as the walk does (" + std::to_string(sg::distance(end[0], end[1])) + " m apart)");
    }
    {
        // One body in two places (carry_body): while one lives, the other is
        // kept as it is - a frame on, as the engine carries at the start of
        // the next - and stepped in its turn, it goes on from there; what is
        // carried into one that does not live is carried no further.
        sg::StateGraph g;
        auto& clock = g.add<sg::Temporal>("clock");
        auto& here = g.add<sg::Being>(sg::Key{"here"});
        auto& there = g.add<sg::Being>(sg::Key{"there"});
        auto& beyond = g.add<sg::Being>(sg::Key{"beyond"});
        for (sg::Being* b : {&here, &there, &beyond}) {
            sg::humanoid(*b, 1.75);
            sg::drive(g, clock, b->id(), b->live_event());
        }
        sg::carry_body(g, here, there);
        sg::carry_body(g, there, here);
        sg::carry_body(g, there, beyond);
        g.connect("here", "go", "there", sg::TransitionKind::Switch);
        g.connect("there", "back", "here", sg::TransitionKind::Switch);
        g.set_initial("here");
        sg::Engine e(g);
        e.set_strict(true);
        e.start();
        e.fire(sg::Event{here.play_event(), sg::Params{}.set("clip", std::string("walk")).set("fade", 0.0)});
        const sg::Params bind = beyond.element(sg::Key{"thigh_l"}).params;
        sg::Params was;
        for (int i = 0; i < 40; ++i) {
            was = here.element(sg::Key{"thigh_l"}).params;
            e.tick(1.0 / 60);
        }
        // (`there` now as `here` was before this frame's step.)
        const sg::Params& now = there.element(sg::Key{"thigh_l"}).params;
        check(now.num("qw") == was.num("qw") && now.num("qx") == was.num("qx") && now.num("awx") == was.num("awx"),
              "a body in two places: the other kept as the one that lives was a frame before");
        check(beyond.element(sg::Key{"thigh_l"}).params.num("qw") == bind.num("qw") && beyond.element(sg::Key{"thigh_l"}).params.num("qx") == bind.num("qx"),
              "and nothing carried into one that does not live carried on from it");
        // Gone over: it goes on walking from where the other left off.
        const sg::Vec3d left = here.pose_of("foot_l").position, before = there.pose_of("foot_l").position;
        e.fire(sg::Event{sg::Key{"go"}});
        e.tick(1.0 / 60);
        e.tick(1.0 / 60);
        const sg::Vec3d went = there.pose_of("foot_l").position;
        check(sg::distance(went, left) < 0.05 && sg::distance(went, before) < 0.08,
              "and stepped in its turn, it walks on from there (" + std::to_string(sg::distance(went, left)) + " m from where the other left it)");
        e.tick(1.0 / 60);
        check(here.element(sg::Key{"thigh_l"}).params.num("qw") != was.num("qw"), "and the one left is kept as it goes on");
    }
    return failures == 0 ? 0 : 1;
}
