// States stepped at once, on lanes of their own, and work shared out by a
// state over its own elements (sg/core/Work.hpp): a frame comes out the same
// however many hands it was done on.
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "sg/core/Engine.hpp"
#include "sg/core/Laws.hpp"
#include "sg/core/Temporal.hpp"
#include "sg/core/Work.hpp"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

constexpr int kWorlds = 6;

// Six worlds, each a weight of work a step (its own arrow, on its own
// element); the first says it moved, and a hub hears it through a functor.
// With `lanes`, the worlds are on three lanes, two to a lane.
void make(sg::StateGraph& g, bool lanes, int work) {
    auto& clock = g.add<sg::Temporal>("clock");
    auto& hub = g.add<sg::State>("hub");
    hub.add_element("count", "n").params.set("n", 0.0).set("seen", 0.0).set("sum", 0.0);
    hub.loop("hear", "count", "hub.heard", [](sg::State&, sg::Element& e, sg::Element*, const sg::Event&) {
        e.params.set("n", e.params.num("n") + 1.0).set("sum", e.params.num("sum") + e.params.num("seen"));
    });
    hub.loop("idle", "count", "hub.tick", [](sg::State&, sg::Element&, sg::Element*, const sg::Event&) {});
    sg::drive(g, clock, "hub", "hub.tick", false, sg::Keeps::Always);
    g.set_initial("hub");
    for (int k = 0; k < kWorlds; ++k) {
        const std::string id = "w" + std::to_string(k);
        auto& w = g.add<sg::State>(id);
        w.add_element("x", "n").params.set("v", 1.0 + k);
        const bool says = k == 0;
        if (says) w.says(sg::Key{id + ".moved"});
        w.loop("turn", "x", sg::Key{id + ".tick"}, [work, says, id](sg::State& s, sg::Element& e, sg::Element*, const sg::Event& ev) {
            const double dt = ev.args.num("dt");
            if (dt == 0.0) return;  // no time, no change
            double v = e.params.num("v"), w = v;
            for (int i = 0; i < work; ++i) w = std::sin(w) * 0.999 + dt;
            e.params.set("v", v + dt * w);
            if (says) s.emit(sg::Event{sg::Key{id + ".moved"}});
        });
        sg::drive(g, clock, sg::Key{id}, sg::Key{id + ".tick"}, false, sg::Keeps::Always, lanes ? 1 + k % 3 : 0);
        // (Reached from the hub, as every state is from somewhere.)
        g.add_functor(sg::Key{"hub.to." + id}, "hub", sg::Key{id}).on_event("hub.poke", sg::Key{id + ".poke"});
    }
    g.add_functor("w0.to.hub", "w0", "hub")
        .on_object("x", "count", [](const sg::Element& s, sg::Element& d) { d.params.set("seen", s.params.num("v")); })
        .on_event("w0.moved", "hub.heard");
}

struct Run {
    std::vector<double> v;
    double n = 0, sum = 0, ms = 0;
    bool listened_here = true;
    int heard = 0;
};

Run run(bool lanes, int work, int frames) {
    sg::StateGraph g;
    make(g, lanes, work);
    Run r;
    const auto here = std::this_thread::get_id();
    g.state("w1").bus().subscribe("w1.tick", [&r, here](const sg::Event&) {
        ++r.heard;
        if (std::this_thread::get_id() != here) r.listened_here = false;
    });
    sg::Engine e(g);
    e.start();
    const auto t0 = std::chrono::steady_clock::now();
    e.run_fixed(1.0 / 60.0, static_cast<uint64_t>(frames));
    r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / frames;
    for (int k = 0; k < kWorlds; ++k) r.v.push_back(g.state(sg::Key{"w" + std::to_string(k)}).element("x").params.num("v"));
    const auto& c = g.state("hub").element("count").params;
    r.n = c.num("n");
    r.sum = c.num("sum");
    return r;
}

void test_lanes_are_one_frame() {
    {
        sg::StateGraph g;
        make(g, true, 50);
        const sg::LawReport r = sg::verify(g);
        check(r.holds(), "worlds on lanes are lawful: a lane is not a clock" + (r.holds() ? std::string() : ": " + r.str()));
        bool on = false;
        for (const sg::Drive& d : g.drives()) on = on || (d.state == sg::Key{"w2"} && d.lane == 3);
        check(on, "a drive keeps the lane it was given");
        check(g.set_lane("clock>w2", 0) && !g.set_lane("nobody", 1), "set_lane moves a drive's state off its lane, and names no drive that is not");
    }
    const int work = 200000, frames = 20;
    const Run one = run(false, work, frames);
    const Run many = run(true, work, frames);
    bool same = one.v == many.v;  // exactly: the same arrows on the same numbers
    check(same, "on lanes each world comes out exactly as on one hand");
    check(one.n == frames && many.n == frames && one.sum == many.sum,
          "and what a world on a lane says is heard by the hub, through its functor, as on one hand");
    check(many.heard == frames && many.listened_here, "a listener of a state on a lane hears it on the engine's thread, every frame");
    std::printf("     a frame of six worlds: %.2f ms on one hand, %.2f ms on lanes (%u hands)\n", one.ms, many.ms, sg::work::hands());
    if (sg::work::hands() >= 4) check(many.ms < one.ms * 0.75, "three lanes at once take less time than one after another");

    sg::work::set_hands(1);
    const Run alone = run(true, work, 3);
    sg::work::set_hands(0);
    const Run ref = run(false, work, 3);
    check(alone.v == ref.v, "one hand: lanes are stepped in turn, and the same");
}

void test_work_shared_out() {
    std::vector<double> out(10000, 0.0);
    sg::work::each(out.size(), [&](std::size_t i) { out[i] = std::sqrt(static_cast<double>(i)); });
    bool all = true;
    for (std::size_t i = 0; i < out.size(); ++i) all = all && out[i] == std::sqrt(static_cast<double>(i));
    check(all, "each: every piece done once, each writing its own");

    std::atomic<int> inner{0};
    sg::work::each(8, [&](std::size_t) { sg::work::each(100, [&](std::size_t) { ++inner; }); });
    check(inner.load() == 800, "work given out from inside a piece is done, never stuck waiting for a hand");

    bool threw = false;
    try {
        sg::work::each(50, [](std::size_t i) {
            if (i == 17) throw std::runtime_error("seventeen");
        });
    } catch (const std::runtime_error& e) {
        threw = std::string(e.what()) == "seventeen";
    }
    check(threw, "what a piece throws is thrown to whoever gave the work out");

    const auto here = std::this_thread::get_id();
    bool mine_here = false;
    std::atomic<int> others{0};
    sg::work::beside([&] { mine_here = std::this_thread::get_id() == here; }, 64, [&](std::size_t) { ++others; });
    check(mine_here && others.load() == 64, "beside: the caller's own work on its own thread, the rest shared out");
}

}  // namespace

int main() {
    test_work_shared_out();
    test_lanes_are_one_frame();
    std::printf("%s\n", failures ? "FAILED" : "all lanes ok");
    return failures ? 1 : 0;
}
