// A ragdoll: a being's body with weight - asleep where the being means it,
// thrown by a hit and pulled by a hand against its muscles, coming back to
// where it means to be; its legs keeping it up unless it is let fall; the
// room's solids stopping it.
#include <cmath>
#include <cstdio>
#include <string>

#include "sg/core/Engine.hpp"
#include "sg/core/Laws.hpp"
#include "sg/core/Temporal.hpp"
#include "sg/domains/Being.hpp"
#include "sg/domains/Ragdoll.hpp"
#include "sg/domains/Spatial.hpp"

static int failures = 0;
static void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    failures += ok ? 0 : 1;
}

namespace {
double dist(const sg::Vec3d& a, const sg::Vec3d& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z)); }

struct Scene {
    sg::StateGraph g;
    sg::Temporal* clock;
    sg::Being* ann;
    sg::Ragdoll* rag;
    std::unique_ptr<sg::Engine> e;
    explicit Scene(bool with_wall = false) {
        clock = &g.add<sg::Temporal>("clock");
        ann = &g.add<sg::Being>(sg::Key{"ann"});
        sg::humanoid(*ann, 1.75);
        sg::drive(g, *clock, "ann", ann->live_event(), false, sg::Keeps::Always);
        rag = dynamic_cast<sg::Ragdoll*>(g.find(sg::ragdoll(g, *ann, *clock)));
        auto& room = g.add<sg::Spatial3D>("room");
        sg::Element& spot = room.add_element(sg::Key{"spot"}, sg::Key{"portal"});
        spot.params.set(sg::keys::x, 0.0).set(sg::keys::y, 0.0).set(sg::keys::z, 0.0).set(sg::keys::yaw, 0.0);
        if (with_wall) {
            // A post just out from her left hand, where a hit would swing it.
            sg::Element& w = room.add_element(sg::Key{"post"}, sg::kinds::wall);
            w.params.set(sg::keys::x, 0.0).set(sg::keys::y, 0.0).set(sg::keys::z, 0.42).set(sg::keys::sx, 1.0).set(sg::keys::sy, 2.0).set(sg::keys::sz, 0.1);
        }
        sg::collide_with(g, *rag, room.id(), spot.id);
        g.set_initial("ann");
        g.connect("ann", sg::Key{"ann.go"}, "room");
    }
    void start() {
        e = std::make_unique<sg::Engine>(g);
        e->set_strict(true);
        e->start();
    }
    void run(double seconds) {
        for (int i = 0, n = int(seconds * 60 + 0.5); i < n; ++i) e->tick(1.0 / 60);
    }
    void tell(sg::Key ev, sg::Params p) {
        rag->hear(sg::Event{ev, std::move(p)});
        rag->dispatch_pending();
    }
    sg::Vec3d hand() const { return ann->pose_of("hand_l").position; }
    bool awake() const { return rag->element(sg::Ragdoll::self_id()).params.num("awake") > 0.5; }
};
}  // namespace

int main() {
    {
        // A ball's muscle (sg::rigid): a rod on a fixed block, aimed half a
        // radian round, goes there - past it a little, and back. Of one
        // group, the two pass through each other.
        using namespace sg::rigid;
        World w;
        w.fields.clear();
        Body a;
        a.id = "a", a.group = 1, a.x = {0, 1, 0};
        a.hulls.push_back(Hull::box({0, 0, 0}, {0.1, 0.1, 0.1}));
        a.set_mass(0);
        w.add(a);
        Body b;
        b.id = "b", b.group = 1, b.x = {0, 1, 0};
        b.hulls.push_back(Hull::box({0, -0.2, 0}, {0.05, 0.2, 0.05}));
        b.set_mass(2);
        w.add(b);
        Joint j;
        j.kind = Joint::Ball, j.a = "a", j.b = "b", j.muscle = true, j.aim = axis_angle({0, 0, 1}, 0.5), j.aim_hertz = 2;
        w.joints.push_back(j);
        double most = 0;
        for (int i = 0; i < 360; ++i) {
            w.step(1.0 / 120);
            most = std::max(most, log_map(w.find("b")->r).z);
        }
        const double end = log_map(w.find("b")->r).z;
        check(std::fabs(end - 0.5) < 0.01 && most > 0.52, "a muscle turns a limb where it is aimed, past and back (" + std::to_string(most) + " at most, " + std::to_string(end) + " at last)");
    }
    {
        Scene w;
        const sg::LawReport r = sg::verify(w.g);
        for (const auto& v : r.violations) std::printf("     %s: %s\n", v.where.c_str(), v.detail.c_str());
        check(r.structure.empty() && r.ok(), "a being and its ragdoll, carried both ways, keep the laws");
        check(w.rag->bones().size() == w.ann->joints().size(), "a body of a bone for every joint");
        w.start();
        const sg::Vec3d hand0 = w.hand(), foot0 = w.ann->pose_of("foot_l").position;
        w.run(1.0);
        check(!w.awake() && dist(w.hand(), hand0) < 1e-6, "left alone it sleeps, and the being stands as it means to");

        // Hit across the forearm: thrown, the shoulder giving with it.
        const sg::Vec3d chest0 = w.ann->pose_of("chest").position;
        w.tell(w.rag->hit_event(), sg::Params{}.set("bone", std::string("forearm_l")).set(sg::keys::z, 6.0));
        double most = 0;
        for (int i = 0; i < 30; ++i) {
            w.run(1.0 / 60);
            most = std::max(most, w.hand().z - hand0.z);
        }
        check(w.awake() && most > 0.12, "hit, the arm is thrown (the hand " + std::to_string(most) + " m out)");
        check(dist(w.ann->pose_of("foot_l").position, foot0) < 1e-3, "and the legs stand as they were");
        w.run(4.0);
        check(dist(w.hand(), hand0) < 0.05, "its muscles bring it back where it means to be (" + std::to_string(dist(w.hand(), hand0)) + " m off)");
        check(!w.awake(), "and, settled, it sleeps again");
        (void)chest0;
    }
    {
        // Held by the hand and pulled: the arm drawn out, the shoulder and
        // chest giving; let go, it swings back past and settles.
        Scene w;
        w.start();
        w.run(0.5);
        const sg::Vec3d hand0 = w.hand(), chest0 = w.ann->pose_of("neck").position;
        const sg::Vec3d to{hand0.x + 0.6, hand0.y + 0.3, hand0.z + 0.3};
        w.tell(w.rag->grab_event(), sg::Params{}.set("bone", std::string("hand_l")).set(sg::keys::x, to.x).set(sg::keys::y, to.y).set(sg::keys::z, to.z).set("force", 300.0));
        w.run(1.5);
        const double drawn = dist(w.hand(), hand0), leaned = dist(w.ann->pose_of("neck").position, chest0);
        check(drawn > 0.25, "pulled, the hand goes with the hand that holds it (" + std::to_string(drawn) + " m)");
        check(leaned > 0.01, "and the pull goes on into the body (the neck " + std::to_string(leaned) + " m over)");
        w.tell(w.rag->let_go_event(), sg::Params{});
        // Which way it is going back, and whether it goes past.
        const sg::Vec3d back = hand0 - w.hand();
        double past = 0;
        for (int i = 0; i < 120; ++i) {
            w.run(1.0 / 60);
            const sg::Vec3d d = w.hand() - hand0;
            past = std::max(past, (d.x * back.x + d.y * back.y + d.z * back.z) / std::sqrt(back.x * back.x + back.y * back.y + back.z * back.z));
        }
        check(past > 0.005, "let go, it swings on past where it means to be (" + std::to_string(past) + " m)");
        w.run(4.0);
        check(dist(w.hand(), hand0) < 0.05 && !w.awake(), "and settles back, and sleeps");
    }
    {
        // A post beside her: a hit that would throw the arm through it is
        // stopped by it.
        Scene w(true);
        w.start();
        w.run(0.3);
        w.tell(w.rag->hit_event(), sg::Params{}.set("bone", std::string("forearm_l")).set(sg::keys::z, 9.0));
        double most = 0;
        for (int i = 0; i < 60; ++i) {
            w.run(1.0 / 60);
            most = std::max(most, w.hand().z);
        }
        check(most < 0.42, "the room's solids stop it: the hand never in the post (" + std::to_string(most) + ")");
    }
    {
        // Hit hard with its legs given weight: all its strength gone, it falls.
        Scene w;
        w.rag->element(sg::Ragdoll::self_id()).params.set("full", 1.0);
        w.start();
        w.run(0.3);
        w.tell(w.rag->hit_event(), sg::Params{}.set("bone", std::string("chest")).set(sg::keys::x, -200.0));
        w.run(2.0);
        const double hips = w.ann->pose_of("hips").position.y;
        check(hips < 0.6, "hit hard and let fall, it goes down (hips " + std::to_string(hips) + " m up)");
    }
    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
