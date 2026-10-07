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
        check(!w.awake() && sg::distance(w.hand(), hand0) < 1e-6, "left alone it sleeps, and the being stands as it means to");

        // Hit across the forearm: thrown, the shoulder giving with it.
        const sg::Vec3d chest0 = w.ann->pose_of("chest").position;
        w.tell(w.rag->hit_event(), sg::Params{}.set("bone", std::string("forearm_l")).set(sg::keys::z, 6.0));
        double most = 0;
        for (int i = 0; i < 30; ++i) {
            w.run(1.0 / 60);
            most = std::max(most, w.hand().z - hand0.z);
        }
        check(w.awake() && most > 0.12, "hit, the arm is thrown (the hand " + std::to_string(most) + " m out)");
        check(sg::distance(w.ann->pose_of("foot_l").position, foot0) < 0.03, "and the legs, with weight of their own, hold her up");
        w.run(4.0);
        check(sg::distance(w.hand(), hand0) < 0.05, "its muscles bring it back where it means to be (" + std::to_string(sg::distance(w.hand(), hand0)) + " m off)");
        check(!w.awake(), "and, settled, it sleeps again");
        (void)chest0;
    }
    {
        // Held by the hand and pulled: the arm drawn out, the shoulder and
        // chest giving; let go, it swings back past and settles.
        Scene w;
        w.start();
        w.run(0.5);
        const sg::Vec3d hand0 = w.hand(), chest0 = w.ann->pose_of("neck").position, chest_at_rest = w.ann->pose_of("chest").position;
        const sg::Vec3d to{hand0.x + 0.35, hand0.y + 0.25, hand0.z + 0.2};
        w.tell(w.rag->grab_event(), sg::Params{}.set("bone", std::string("hand_l")).set(sg::keys::x, to.x).set(sg::keys::y, to.y).set(sg::keys::z, to.z).set("force", 150.0));
        w.run(1.5);
        const double drawn = sg::distance(w.hand(), hand0), leaned = sg::distance(w.ann->pose_of("neck").position, chest0);
        check(drawn > 0.15, "pulled, the hand goes with the hand that holds it (" + std::to_string(drawn) + " m)");
        check(leaned > 0.003, "and the pull goes on into the body (the neck " + std::to_string(leaned) + " m over)");
        w.tell(w.rag->let_go_event(), sg::Params{});
        // Which way it is going back, and whether it goes past.
        // (On the body: the arm against the chest, which sways too.)
        const auto on_body = [&] { return w.hand() - w.ann->pose_of("chest").position; };
        const sg::Vec3d rest = hand0 - chest_at_rest, back = rest - on_body();
        double past = 0;
        for (int i = 0; i < 120; ++i) {
            w.run(1.0 / 60);
            const sg::Vec3d d = on_body() - rest;
            past = std::max(past, (d.x * back.x + d.y * back.y + d.z * back.z) / std::sqrt(back.x * back.x + back.y * back.y + back.z * back.z));
        }
        check(past > 0.005, "let go, it swings on past where it means to be (" + std::to_string(past) + " m)");
        w.run(6.0);
        check(sg::distance(w.hand(), hand0) < 0.05 && !w.awake(), "and settles back, and sleeps");
    }
    {
        // What a body took from what it touched: a block dropped on the
        // floor was pushed up by it, about as hard as its weight over the step.
        using namespace sg::rigid;
        World w;
        Body floor;
        floor.id = "floor";
        floor.hulls.push_back(Hull::box({0, -0.5, 0}, {5, 0.5, 5}));
        floor.set_mass(0);
        w.add(floor);
        Body b;
        b.id = "b", b.x = {0, 0.1, 0};
        b.hulls.push_back(Hull::box({}, {0.1, 0.1, 0.1}));
        b.set_mass(2);
        w.add(b);
        for (int i = 0; i < 60; ++i) w.step(1.0 / 60);
        const auto took = w.took(1);
        const double up = took.empty() ? 0 : took[0].impulse.y;
        check(took.size() == 1 && took[0].from == 0 && std::fabs(up - 2 * 9.81 / 60) < 0.1, "a block at rest on the floor took its weight from it, a step at a time (" + std::to_string(up) + " N s)");
    }
    {
        // Her bones as solids of another world (a room she stands in, turned
        // a quarter): the head is a body as big as her skull, where her head is.
        Scene w;
        const sg::Element& head = w.rag->element(sg::Key{"head"});
        const sg::rigid::Body b = sg::Ragdoll::body(head, "ann.head", 7);
        sg::rigid::V3 x;
        sg::rigid::M3 r;
        sg::Ragdoll::pose_in(head, {10, 0, 0}, 1.5707963, x, r);
        const sg::Vec3d at = w.ann->pose_of("head").position;
        double lo = 1e9, hi = -1e9;
        for (const auto& v : b.hulls[0].v) lo = std::min(lo, v.y), hi = std::max(hi, v.y);
        check(b.group == 7 && b.mass > 1.0 && hi - lo > 0.18, "a head is a solid as big as the skull (" + std::to_string(hi - lo) + " m tall, " + std::to_string(b.mass) + " kg)");
        check(std::fabs(x.y - at.y) < 1e-9 && std::fabs(x.x - 10 - at.z) < 1e-6 && std::fabs(x.z + at.x) < 1e-6, "and stands where her head is, in the room she is turned in");
    }
    {
        // The bone a hand on the body touches: the one whose solid is nearest.
        Scene w;
        const sg::Vec3d shin = w.ann->pose_of("shin_l").position, foot = w.ann->pose_of("foot_l").position;
        check(w.rag->nearest({shin.x, (shin.y + foot.y) * 0.5, shin.z}) == sg::Key{"shin_l"}, "a hand halfway down the shin is on the shin");
    }
    {
        // A knock from another world (what the room's loose things or a
        // walker did to a bone, carried onto it): taken once, and it moves her.
        Scene w;
        w.start();
        w.run(0.3);
        const sg::Vec3d hand0 = w.hand();
        w.rag->element(sg::Key{"forearm_l"}).params.set("kz", 6.0).set("knock_n", 1.0);
        w.run(0.25);
        const double moved = sg::distance(w.hand(), hand0);
        check(w.awake() && moved > 0.02, "knocked, it wakes and the arm goes (" + std::to_string(moved) + " m)");
        check(w.rag->element(sg::Key{"forearm_l"}).params.num("knock_seen") == 1.0, "and the knock is taken once");
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
    {
        // Kicked: the lower body has weight too - the shin swings, and the
        // leg comes back under her.
        Scene w;
        w.start();
        w.run(0.3);
        const sg::Vec3d foot0 = w.ann->pose_of("foot_r").position;
        w.tell(w.rag->hit_event(), sg::Params{}.set("bone", std::string("shin_r")).set(sg::keys::x, 20.0));
        double most = 0;
        for (int i = 0; i < 30; ++i) {
            w.run(1.0 / 60);
            most = std::max(most, sg::distance(w.ann->pose_of("foot_r").position, foot0));
        }
        check(most > 0.08, "kicked, the leg swings (the foot " + std::to_string(most) + " m out)");
        w.run(4.0);
        check(sg::distance(w.ann->pose_of("foot_r").position, foot0) < 0.03 && !w.awake(), "and comes back under her, and she settles");
    }
    {
        // Taken hold of by the head and pulled: the head has a body to hold.
        Scene w;
        w.start();
        w.run(0.3);
        const sg::Vec3d head0 = w.ann->pose_of("head").position;
        const sg::Vec3d top{head0.x, head0.y + 0.15, head0.z};
        w.tell(w.rag->grab_event(), sg::Params{}.set("bone", std::string("head")).set("ax", top.x).set("ay", top.y).set("az", top.z)
                                        .set(sg::keys::x, top.x + 0.4).set(sg::keys::y, top.y).set(sg::keys::z, top.z));
        w.run(1.0);
        check(w.ann->pose_of("head").position.x > head0.x + 0.05, "held by the crown and pulled, the head goes with the hand");
    }
    {
        // A hand of five fingers, three joints each (a Mixamo skeleton has
        // them): too small to matter, they ride the hand - no body of their
        // own - and the hand thrown and come back is as it was.
        sg::StateGraph g;
        auto& clock = g.add<sg::Temporal>("clock");
        auto& b = g.add<sg::Being>(sg::Key{"ann"});
        sg::humanoid(b, 1.75);
        for (int f = 0; f < 5; ++f) {
            std::string up = "hand_l";
            for (int k = 0; k < 3; ++k) {
                const std::string name = "finger" + std::to_string(f) + "_" + std::to_string(k);
                b.joint(name, up, {k == 0 ? 0.02 * (f - 2) : 0.0, -0.03, 0.0});
                up = name;
            }
        }
        sg::drive(g, clock, "ann", b.live_event(), false, sg::Keeps::Always);
        auto* rag = dynamic_cast<sg::Ragdoll*>(g.find(sg::ragdoll(g, b, clock)));
        g.set_initial("ann");
        int riding = 0;
        for (sg::Key k : rag->bones()) riding += rag->element(k).params.num("rides") > 0.5;
        check(riding == 15 && rag->element(sg::Key{"hand_l"}).params.num("rides") < 0.5 && rag->element(sg::Key{"hand_l"}).params.num("armature") < 0.01,
              "fifteen finger joints ride the hand (" + std::to_string(riding) + "), which is a body as it was");
        const sg::LawReport r = sg::verify(g);
        check(r.ok(), "and the laws hold");
        sg::Engine e(g);
        e.start();
        for (int i = 0; i < 20; ++i) e.tick(1.0 / 60);
        const sg::Vec3d tip0 = b.pose_of("finger2_2").position;
        rag->hear(sg::Event{rag->hit_event(), sg::Params{}.set("bone", std::string("forearm_l")).set(sg::keys::z, 12.0)});
        rag->dispatch_pending();
        double most = 0;
        for (int i = 0; i < 30; ++i) {
            e.tick(1.0 / 60);
            most = std::max(most, sg::distance(b.pose_of("finger2_2").position, tip0));
        }
        for (int i = 0; i < 360; ++i) e.tick(1.0 / 60);
        check(most > 0.05 && sg::distance(b.pose_of("finger2_2").position, tip0) < 0.05, "the fingers go with the hand thrown, and come back with it");
    }
    {
        // Made at a hundredth of its size and sized up before it lives (as a
        // Mixamo export is fitted): what it means is what it was made, not
        // what it was when its first joints were.
        {
            sg::Being b(sg::Key{"small"});
            sg::humanoid(b, 0.0175);
            b.resize(100.0);
            const auto& h = b.element("hips").params;
            check(std::fabs(h.num("ax") - h.num("px")) + std::fabs(h.num("ay") - h.num("py")) < 1e-9,
                  "made small and sized up before living, it means the body it now is (hips meant " + std::to_string(h.num("ay")) + ", are " + std::to_string(h.num("py")) + ")");
        }
        // Made with its feet in the floor: its own pose, not a disturbance -
        // its ragdoll sleeps.
        sg::StateGraph g;
        auto& clock = g.add<sg::Temporal>("clock");
        auto& b = g.add<sg::Being>(sg::Key{"ann"});
        sg::humanoid(b, 1.75);
        b.lift(-0.08);
        sg::drive(g, clock, "ann", b.live_event(), false, sg::Keeps::Always);
        auto* rag = dynamic_cast<sg::Ragdoll*>(g.find(sg::ragdoll(g, b, clock)));
        g.set_initial("ann");
        sg::Engine e(g);
        e.start();
        bool woke = false;
        for (int i = 0; i < 60; ++i) {
            e.tick(1.0 / 60);
            woke = woke || rag->element(sg::Ragdoll::self_id()).params.num("awake") > 0.5;
        }
        check(!woke, "made with its feet in the floor, its ragdoll sleeps: that is its own pose, not a disturbance");
    }
    {
        // A body whose meant pose is of a body it no longer is (made, then
        // changed, with nothing to say so) says so itself: the graph's
        // validation names it, so it is heard at start and refused by verify.
        sg::StateGraph g;
        auto& b = g.add<sg::Being>(sg::Key{"ann"});
        sg::humanoid(b, 1.75);
        g.set_initial("ann");
        check(g.validate().empty(), "a body that means what it is has no fault");
        b.element("chest").params.set("ay", 0.2);  // (a meant pose taken from another body)
        bool named = false;
        for (const auto& e : g.validate()) named = named || (e.find("state ann") != std::string::npos && e.find("chest") != std::string::npos);
        check(named && !sg::verify(g).structure.empty(), "a body whose meant pose does not fit its own bones is named by the graph's validation, and verify refuses it");
    }
    {
        // Anchored to its place, a few centimetres: shoved, it sways within
        // them; run into, it does not step and does not fall.
        Scene w;
        w.start();
        w.run(0.3);
        w.tell(w.rag->anchor_event(), sg::Params{}.set("reach", 0.04));
        const sg::Vec3d hips0 = w.ann->pose_of("hips").position;
        w.tell(w.rag->hit_event(), sg::Params{}.set("bone", std::string("chest")).set(sg::keys::x, 170.0));
        double most = 0, steps = 0, fell = 0;
        for (int i = 0; i < 180; ++i) {
            w.run(1.0 / 60);
            const auto& self = w.rag->element(sg::Ragdoll::self_id());
            const sg::Vec3d h = w.ann->pose_of("hips").position;
            most = std::max(most, std::hypot(h.x - hips0.x, h.z - hips0.z));
            steps = std::max(steps, self.params.num("steps"));
            fell = std::max(fell, self.params.num("fallen"));
        }
        check(most <= 0.045 && steps == 0 && fell == 0, "anchored, run into, it moves no further than its reach (" + std::to_string(most) + " m), never steps, never falls");
    }
    {
        // Balance. Nudged at the chest: the hips sway over the feet and come
        // back; no step is needed.
        Scene w;
        w.start();
        w.run(0.3);
        w.tell(w.rag->hit_event(), sg::Params{}.set("bone", std::string("chest")).set(sg::keys::x, 6.0));
        double most = 0, steps = 0;
        for (int i = 0; i < 90; ++i) {
            w.run(1.0 / 60);
            const auto& self = w.rag->element(sg::Ragdoll::self_id());
            most = std::max(most, std::hypot(self.params.num("sway_x"), self.params.num("sway_z")));
            steps = std::max(steps, self.params.num("steps"));
        }
        check(most > 0.001 && steps == 0, "nudged, the hips sway over the feet (" + std::to_string(most) + " m) and no foot moves");
    }
    {
        // Shoved: the sway would come to rest past her soles - a foot steps
        // out to catch it; then she is steady, her feet step home, and she
        // settles where she stood.
        Scene w;
        w.start();
        w.run(0.3);
        const sg::Vec3d foot0 = w.ann->pose_of("foot_l").position, foot1 = w.ann->pose_of("foot_r").position;
        w.tell(w.rag->hit_event(), sg::Params{}.set("bone", std::string("chest")).set(sg::keys::x, 60.0));
        double steps = 0, out = 0, fell = 0;
        for (int i = 0; i < 90; ++i) {
            w.run(1.0 / 60);
            const auto& self = w.rag->element(sg::Ragdoll::self_id());
            steps = std::max(steps, self.params.num("steps"));
            fell = std::max(fell, self.params.num("fallen"));
            out = std::max(out, std::max(sg::distance(w.ann->pose_of("foot_l").position, foot0), sg::distance(w.ann->pose_of("foot_r").position, foot1)));
        }
        check(steps >= 1 && out > 0.1 && fell == 0, "shoved, she steps to catch herself (" + std::to_string(int(steps)) + " steps, a foot " + std::to_string(out) + " m out) and does not fall");
        w.run(8.0);
        const double back = std::max(sg::distance(w.ann->pose_of("foot_l").position, foot0), sg::distance(w.ann->pose_of("foot_r").position, foot1));
        check(back < 0.05 && !w.awake(), "then her feet step home, and she settles where she stood (" + std::to_string(back) + " m off)");
    }
    {
        // Shoved harder than a step can catch: she falls, lies a while, and
        // gets up again.
        Scene w;
        w.start();
        w.run(0.3);
        const double up = w.ann->pose_of("hips").position.y;
        w.tell(w.rag->hit_event(), sg::Params{}.set("bone", std::string("chest")).set(sg::keys::x, 170.0));
        double low = up;
        for (int i = 0; i < 240; ++i) {
            w.run(1.0 / 60);
            low = std::min(low, w.ann->pose_of("hips").position.y);
        }
        check(low < 0.5 * up, "run into (170 N s), too hard to catch, she falls (hips down to " + std::to_string(low) + " m)");
        w.run(8.0);
        check(w.ann->pose_of("hips").position.y > 0.9 * up, "and gets up again (hips " + std::to_string(w.ann->pose_of("hips").position.y) + " m up)");
    }
    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
