// A director: a state of its own that stages others through the graph - its
// marks carried onto their things by kept functors, its cues said and carried
// to their events (sg/domains/Director.hpp).
#include <cmath>
#include <cstdio>
#include <string>

#include "sg/core/Engine.hpp"
#include "sg/core/Laws.hpp"
#include "sg/core/Temporal.hpp"
#include "sg/domains/Director.hpp"
#include "sg/domains/Spatial.hpp"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

bool near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps; }

const char* kScript = R"(
# a camera set, pushed in and turned on the box; the box slid; the lights out
0   cam x=0 y=1.6 z=0 aim_x=4 aim_y=1 aim_z=0
2   cam x=2 ease=smooth
0.5 box x=4 z=0
1.5 box z=2 ease=in
1   say lights_out circuit=main level=0.25
3   cam fov=40 ease=cut
)";

struct World {
    sg::StateGraph g;
    sg::Director* d = nullptr;
    sg::State* set = nullptr;
};

void make(World& w) {
    auto& clock = w.g.add<sg::Temporal>("clock");
    auto& set = w.g.add<sg::State>("set");
    set.add_element("cam", "camera").params.set(sg::keys::fov, 60.0);
    set.add_element("box", "thing").params.set(sg::keys::y, 0.5);
    set.add_element("lamp", "lamp").params.set("on", 1.0);
    set.loop("dark", "lamp", "set.dark", [](sg::State&, sg::Element& e, sg::Element*, const sg::Event& ev) {
        const std::string* c = ev.args.text("circuit");
        if (c && *c == "main") e.params.set("on", ev.args.num("level"));
    });
    set.loop("idle", "lamp", "set.tick", [](sg::State&, sg::Element&, sg::Element*, const sg::Event&) {});
    sg::drive(w.g, clock, "set", "set.tick");
    w.g.set_initial("set");
    auto& d = w.g.add<sg::Director>("director", kScript);
    sg::bind(w.g, d, "cam", "set", "cam");
    sg::bind(w.g, d, "box", "set", "box");
    sg::cue(w.g, d, "lights_out", "set", "set.dark");
    set.says("set.action");
    set.loop("go", "lamp", "set.go", [](sg::State& s, sg::Element&, sg::Element*, const sg::Event&) { s.emit("set.action"); });
    sg::action(w.g, d, "set", "set.action");
    sg::drive(w.g, clock, "director", d.tick_event(), false, sg::Keeps::Always);
    w.d = &d;
    w.set = &set;
}

void test_script() {
    World w;
    make(w);
    const sg::Director& d = *w.d;
    check(d.faults().empty(), "the script reads, and moves only marks the director has");
    check(near(d.length(), 3.0), "the script is as long as its last line");
    auto at = d.at(1.0);
    const sg::Params& cam = at["cam"];
    // smooth from 0 to 2 over 2 s, half way: half way there.
    check(near(cam.num(sg::keys::x), 1.0), "a smooth key half way is half way");
    check(near(cam.num(sg::keys::yaw), std::atan2(0.0, 3.0)) && near(cam.num(sg::keys::pitch), std::atan2(-0.6, 3.0)),
          "a mark with an aim looks at it, by the engine's heading");
    check(!cam.has("aim_x") && !cam.has(sg::keys::fov), "the aim is not carried; a track says nothing before its first key");
    check(d.at(0.25).count("box") == 0, "before its first key a mark says nothing at all");
    check(near(d.at(1.0)["box"].num(sg::keys::z), 0.5) && near(d.at(1.5)["box"].num(sg::keys::z), 2.0),
          "a key eased in from the key before");
    check(!d.at(2.999)["cam"].has(sg::keys::fov) && near(d.at(3.0)["cam"].num(sg::keys::fov), 40.0), "a cut is nothing, then all");
    sg::Director bad("bad", "0 nobody x=1\nsoon cam x=2\n");
    check(bad.faults().size() == 2, "a director says what its script gets wrong");
}

void test_staged() {
    World w;
    make(w);
    {
        const sg::LawReport r = sg::verify(w.g);
        check(r.holds(), "a director and what it stages are lawful" + (r.holds() ? std::string() : ": " + r.str()));
    }
    check(w.g.validate().empty(), "every state is reached");
    sg::Engine e(w.g);
    e.start();
    const double dt = 1.0 / 60.0;
    e.run_fixed(dt, 30);  // half a second, carried the frame after
    const sg::Element& cam = w.set->element("cam");
    const sg::Element& lamp = w.set->element("lamp");
    check(cam.params.has(sg::keys::yaw) && near(cam.params.num(sg::keys::y), 1.6), "the camera is where the director set it");
    check(lamp.params.num("on") == 1.0, "before its cue the lamp is lit");
    e.run_fixed(dt, 40);
    check(lamp.params.num("on") == 0.25, "the cue said with its args, carried to the lamp's own arrow: down");
    e.run_fixed(dt, 200);
    check(near(cam.params.num(sg::keys::x), 2.0) && near(cam.params.num(sg::keys::fov), 40.0) &&
              near(w.set->element("box").params.num(sg::keys::z), 2.0),
          "at the end everything is where the script left it");
    // Done, the director holds nothing: what moves a thing now has it.
    w.set->element("box").params.set(sg::keys::z, 7.0);
    e.run_fixed(dt, 10);
    check(near(w.set->element("box").params.num(sg::keys::z), 7.0), "the script over, a thing is its own again");
    // Action: the set rolls the director again, and the scene plays from its top.
    e.fire(sg::Event{sg::Key{"set.go"}});
    e.run_fixed(dt, 60);
    check(near(w.set->element("box").params.num(sg::keys::z), w.d->at(1.0)["box"].num(sg::keys::z), 1e-6) &&
              near(w.set->element("cam").params.num(sg::keys::x), w.d->at(1.0)["cam"].num(sg::keys::x), 1e-6),
          "rolled by the set's own word, the scene starts again from its top");
}

}  // namespace

int main() {
    test_script();
    test_staged();
    std::printf("%s\n", failures ? "FAILED" : "director ok");
    return failures ? 1 : 0;
}
