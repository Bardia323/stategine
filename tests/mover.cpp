// A mover (sg/domains/Mover.hpp): told where to go, it goes there by
// MotionBricks' bricks and stands; asked ahead, no step waits on the model;
// carried onto a body, the body's joints are led. (Where the model cannot
// run - no ONNX Runtime, no .onnx - it stands, and says why.)
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>

#include "sg/core/Engine.hpp"
#include "sg/core/Laws.hpp"
#include "sg/core/Temporal.hpp"
#include "sg/domains/Being.hpp"
#include "sg/domains/Bricks.hpp"
#include "sg/domains/Mover.hpp"

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "[ok]  " : "[FAIL]", what.c_str());
    if (!ok) ++failures;
}
}  // namespace

int main() {
    using namespace sg;
    StateGraph g;
    auto& clock = g.add<Temporal>(Key{"time"});
    auto& m = g.add<Mover>(Key{"walker"}, 1.0, 2.0, 0.0);
    // A body with SOMA's joint names: its joints led by the mover's.
    auto& b = g.add<Being>(Key{"body"});
    b.joint("Hips", "", {0.0, 0.95, 0.0});
    b.joint("Spine1", "Hips", {0.0, 0.1, 0.0});
    b.joint("LeftLeg", "Hips", {0.0, -0.05, -0.1});
    b.joint("LeftShin", "LeftLeg", {0.0, -0.43, 0.0});
    b.joint("RightLeg", "Hips", {0.0, -0.05, 0.1});
    b.joint("RightShin", "RightLeg", {0.0, -0.43, 0.0});
    g.set_initial(m.id());
    drive(g, clock, m.id(), m.live_event(), false, Keeps::Always);
    drive(g, clock, b.id(), b.live_event(), false, Keeps::Always);
    g.port(m.id(), m.go_event());
    const auto pairs = bricks_joints(b);
    check(pairs.size() == 6, "the body's joints found among SOMA's: " + std::to_string(pairs.size()));
    move_body(g, m, b);
    const auto problems = g.validate();
    check(problems.empty(), "the graph is whole" + (problems.empty() ? std::string() : ": " + problems.front()));
    if (!bricks::Model::can() || !m.faults().empty()) {
        std::printf("(no model to run - %s: its going not checked)\n", m.faults().empty() ? "no ONNX Runtime" : m.faults().front().c_str());
        std::printf("%s\n", failures ? "FAILED" : "all ok");
        return failures ? 1 : 0;
    }
    Engine engine(g);
    engine.start(m.id());
    engine.send(m.id(), Event{m.go_event(), Params{}.set(keys::x, 5.0).set(keys::z, 3.0).set("speed", 1.2)});
    // (Its frames as a program's come, a sixtieth of a second apart: what is
    // asked ahead is made while they go on.)
    double worst = 0.0;
    for (int f = 0; f < 9 * 60; ++f) {
        const auto t0 = std::chrono::steady_clock::now();
        engine.tick(1.0 / 60.0);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (f > 2) worst = std::max(worst, ms);
        std::this_thread::sleep_until(t0 + std::chrono::microseconds(16667));
    }
    const Params& self = m.element(Mover::self_id()).params;
    const double off = std::hypot(self.num(keys::x) - 5.0, self.num(keys::z) - 3.0);
    check(off < 0.4, "it went where it was told: " + std::to_string(off) + " m from there");
    const Params& hips = m.element(Key{"j.Hips"}).params;
    check(hips.num("py") > 0.8 && hips.num("py") < 1.2, "and stands: its hips " + std::to_string(hips.num("py")) + " m up");
    check(worst < 20.0, "no step waited on the model: the longest " + std::to_string(worst) + " ms");
    const Element& led = b.element(Key{"LeftShin"});
    check(led.params.num("lead_at") > 0.5 && std::isfinite(led.params.num("lead_qw")), "the body's shin is led by the mover's");
    const auto t0 = std::chrono::steady_clock::now();
    const LawReport r = verify(g);
    check(r.ok(), "its laws hold (" + std::to_string(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count()) + " s)" + (r.ok() ? std::string() : ": " + r.str()));
    std::printf("%s\n", failures ? "FAILED" : "all ok");
    return failures ? 1 : 0;
}
