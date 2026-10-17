#include "sg/domains/Door.hpp"

#include <algorithm>
#include <cmath>

namespace sg {

namespace {

// The way open, and how far: whichever of lo, hi is not 0.
double open_of(const Element& e) {
    const double lo = e.params.num("lo"), hi = e.params.num("hi");
    return std::fabs(hi) >= std::fabs(lo) ? hi : lo;
}

}  // namespace

Door::Door(Key id, double open, double shut, double ajar) : State(id) {
    Element& d = add_element(leaf_id(), Key{"leaf"});
    const double a = std::clamp(ajar, 0.0, 1.0) * open;
    d.params.set("angle", a).set("vel", 0.0).set("lo", std::min(0.0, open)).set("hi", std::max(0.0, open)).set("target", a);
    d.params.set("held", 0.0).set("goal", 0.0).set("friction", 2.2).set("shut", shut).set("rattle", 0.0);
    add_element(way_id(), Key{"way"}).params.set("open", std::fabs(a) > kPassable ? 1.0 : 0.0).set("shut", a == 0.0 ? 1.0 : 0.0);
    // leaf --step--> leaf {dt}: it swings on, slowing; held, it follows the
    // hand; swung (E), it goes there and is let go. Swinging through shut
    // with little speed, it latches.
    loop(Key{"swing"}, leaf_id(), step_key(), [](State& st, Element& e, Element*, const Event& ev) {
        const double dt = std::min(ev.args.num(keys::dt), 0.05);
        if (dt <= 0) return;
        Element& way = st.element(way_id());
        // What its doorway is told: whether it can be walked through, and
        // whether it is shut close (nothing seen past it, from its side).
        const auto tell = [&way](double angle, double rattle) {
            const double open = std::fabs(angle) > kPassable ? 1.0 : 0.0, shut = angle == 0.0 && rattle <= 0.0 ? 1.0 : 0.0;
            if (way.params.num("open") != open) way.params.set("open", open);
            if (way.params.num("shut") != shut) way.params.set("shut", shut);
        };
        // Rattling: from the moment it was tried (stamped by this, its own
        // step, from its own time), for kRattle - how far through it is.
        if (e.params.num("rattle_ask") > 0.5) e.params.set("rattle_at", ev.args.num(keys::time) - dt).set("rattle_ask", 0.0);
        if (e.params.num("rattle") > 0 || e.params.has(Key{"rattle_at"})) {
            const double r = std::max(0.0, kRattle - (ev.args.num(keys::time) - e.params.num("rattle_at")));
            if (e.params.num("rattle") != r) e.params.set("rattle", r);
            if (r <= 0.0) e.params.erase(Key{"rattle_at"});
        }
        double a = e.params.num("angle"), v = e.params.num("vel");
        const bool held = e.params.num("held") > 0.5, goal = e.params.num("goal") > 0.5;
        if (!held && !goal && v == 0.0) return tell(a, e.params.num("rattle"));
        if (held) {
            // Pulled towards the hand, like a spring, not snapped to it.
            v = (e.params.num("target") - a) * 14.0;
        } else if (goal) {
            // Swung by a hand that lets go where it meant it to stop.
            v = (e.params.num("target") - a) * 5.0;
        } else {
            v *= std::exp(-e.params.num("friction") * dt);
        }
        const double was = a;
        a += v * dt;
        const double lo = e.params.num("lo"), hi = e.params.num("hi");
        if (a < lo) a = lo, v = -v * 0.1;  // wide open: a soft stop
        if (a > hi) a = hi, v = -v * 0.1;
        if (!held && !goal && (was != 0.0) && std::fabs(a) < 0.02 && std::fabs(v) < 0.6) a = 0, v = 0;  // latched
        if (goal && std::fabs(e.params.num("target") - a) < 0.004) a = e.params.num("target"), v = 0, e.params.set("goal", 0.0);
        if (std::fabs(v) < 1e-4) v = 0;
        e.params.set("angle", a).set("vel", v);
        tell(a, e.params.num("rattle"));
    });
    loop(Key{"hold"}, leaf_id(), hold_key(), [](State&, Element& e, Element*, const Event& ev) {
        e.params.set("held", 1.0).set("goal", 0.0).set("target", std::clamp(ev.args.num("angle"), e.params.num("lo"), e.params.num("hi")));
    });
    loop(Key{"release"}, leaf_id(), release_key(), [](State&, Element& e, Element*, const Event&) {
        e.params.set("held", 0.0);
    });
    loop(Key{"push"}, leaf_id(), push_key(), [](State&, Element& e, Element*, const Event& ev) {
        if (e.params.num("held") > 0.5) return;
        const double way = open_of(e) < 0 ? -1.0 : 1.0;
        e.params.set("vel", e.params.num("vel") + ev.args.num("speed") * way).set("goal", 0.0);
    });
    loop(Key{"to"}, leaf_id(), swing_key(), [](State&, Element& e, Element*, const Event& ev) {
        e.params.set("target", std::clamp(ev.args.num("open"), 0.0, 1.0) * open_of(e)).set("goal", 1.0).set("held", 0.0);
    });
    loop(Key{"rattle"}, leaf_id(), rattle_key(), [](State& st, Element& e, Element*, const Event&) {
        e.params.set("rattle", kRattle).set("rattle_ask", 1.0);
        st.element(way_id()).params.set("shut", 0.0);
    });
}

void door_hinge(const Element& leaf, Element& hinge) {
    const double open = std::fabs(leaf.params.num("hi")) >= std::fabs(leaf.params.num("lo")) ? leaf.params.num("hi") : leaf.params.num("lo");
    const double r = leaf.params.num("rattle");
    const double shake = r > 0 ? 0.01 * std::sin(r * 90.0) * (r / Door::kRattle) * (open < 0 ? -1.0 : 1.0) : 0.0;
    const double yaw = leaf.params.num("shut") + leaf.params.num("angle") + std::fabs(shake) * (open < 0 ? -1.0 : 1.0);
    if (hinge.params.num(keys::yaw) != yaw) hinge.params.set(keys::yaw, yaw);
}

void door_near(const Element& way, Element& doorway) {
    const double leave = way.params.num("open"), closed = way.params.num("shut");
    if (doorway.params.num("leave", 1.0) != leave) doorway.params.set("leave", leave);
    if (doorway.params.num("closed") != closed) doorway.params.set("closed", closed);
}

void door_far(const Element& way, Element& doorway) {
    const double leave = way.params.num("open");
    if (doorway.params.num("leave", 1.0) != leave) doorway.params.set("leave", leave);
}

}  // namespace sg
