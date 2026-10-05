// A relation that holds at once; contents that come to agree with it in time.
//
// Two local states a and b and a global g (tests/dsl/relax*.sg). The
// restrictions ra, rb say at once what a and b must hold, given g; they are
// never applied to a's or b's own data. Their answer is carried into each
// side's aim (a kept functor), and each side relaxes toward it by its own
// arrow on the clock. What is checked:
//
//   1. the laws hold on both graphs (strict, sg::verify)
//   2. factorisation: g is the glued section of a and b, or it holds a datum
//      no piece does - told apart by the cover (descent, and locality)
//   3. a perturbation of g moves the target at once and the contents not at
//      all; then each side closes its gap by its own dynamics
//   4. the three outcomes of a step - converges, goes round (period k),
//      persists or grows - each in its own variant, classified from the gaps
//   5. no time is no change; restored data runs the same trajectory
//   6. the correlation: local relaxation breaks it on the way and restores it
//      only once both sides have arrived; a move of the correlation itself is
//      out of reach of either side, and needs g's own arrow
//   7. a step that overwrites its target instead of relaxing is refused by
//      the laws
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

#include "sg/core/Relax.hpp"
#include "sg/core/Sheaf.hpp"
#include "sg/dsl/Natives.hpp"
#include "sg/dsl/Runtime.hpp"
#include "sg/sg.hpp"

namespace sgen {
void build_relax_product(sg::StateGraph&, const sg::dsl::Natives&, sg::dsl::Bindings&);
void build_relax_bond(sg::StateGraph&, const sg::dsl::Natives&, sg::dsl::Bindings&);
}  // namespace sgen

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "[ok]  " : "[FAIL]", what.c_str());
    if (!ok) ++failures;
}

constexpr double kDt = 0.25;
constexpr double kTol = 1e-6;
const double kTurn = 2.0 * std::acos(-1.0);

// --- the insides of the declared arrows ---------------------------------------------------

// y - y*  <-  e^(-rate dt) R(spin dt) (y - y*): a linear step toward the aim.
void relax(sg::State&, sg::Element& e, sg::Element*, const sg::Event& ev) {
    const double dt = ev.args.num("dt");
    const double ax = e.params.num("aim_x"), ay = e.params.num("aim_y");
    const double dx = e.params.num("x") - ax, dy = e.params.num("y") - ay;
    const double s = std::exp(-e.params.num("rate") * dt), t = e.params.num("spin") * dt;
    const double c = std::cos(t), n = std::sin(t);
    e.params.set("x", ax + s * (c * dx - n * dy));
    e.params.set("y", ay + s * (n * dx + c * dy));
}

// The failing variant: the target written over the contents, whatever the time.
void snap(sg::State&, sg::Element& e, sg::Element*, const sg::Event&) {
    e.params.set("x", e.params.num("aim_x"));
    e.params.set("y", e.params.num("aim_y"));
}

void shift(sg::Element& e, const char* key, double d) { e.params.set(key, e.params.num(key) + d); }

sg::dsl::Natives natives(bool overwrite = false) {
    sg::dsl::Natives n;
    n.arrow("relax", overwrite ? snap : relax);
    n.arrow("shift_x", [](sg::State&, sg::Element& e, sg::Element*, const sg::Event& ev) { shift(e, "x", ev.args.num("d")); });
    n.arrow("shift_y", [](sg::State&, sg::Element& e, sg::Element*, const sg::Event& ev) { shift(e, "y", ev.args.num("d")); });
    // g's own: a correlated move that keeps ga.x + gb.x.
    n.arrow("kick_pair", [](sg::State& s, sg::Element&, sg::Element*, const sg::Event& ev) {
        shift(*s.find("ga"), "x", ev.args.num("d"));
        shift(*s.find("gb"), "x", -ev.args.num("d"));
    });
    n.arrow("shift_total", [](sg::State&, sg::Element& e, sg::Element*, const sg::Event& ev) { shift(e, "total", ev.args.num("d")); });
    n.arrow("set_share", [](sg::State&, sg::Element& e, sg::Element*, const sg::Event& ev) { e.params.set("share", ev.args.num("k")); });
    // g's own dynamics: ga.x and gb.x close the gap to total, half each.
    n.arrow("settle", [](sg::State& s, sg::Element& bond, sg::Element*, const sg::Event& ev) {
        sg::Element& ga = *s.find("ga");
        sg::Element& gb = *s.find("gb");
        const double off = ga.params.num("x") + gb.params.num("x") - bond.params.num("total");
        const double close = 0.5 * off * (1.0 - std::exp(-bond.params.num("share") * ev.args.num("dt")));
        if (close == 0.0) return;
        shift(ga, "x", -close);
        shift(gb, "x", -close);
    });
    return n;
}

sg::LawOptions law_options() {
    sg::LawOptions o;
    o.args.set("d", 0.3).set("dt", kDt).set("k", 1.5);
    o.drive_dt = kDt;
    return o;
}

// A world: one of the two graphs, its engine, run strict.
struct World {
    sg::StateGraph graph;
    std::unique_ptr<sg::Engine> engine;
    std::vector<std::string> problems;

    explicit World(bool bond, bool overwrite = false) {
        sg::dsl::Bindings b;
        if (bond) sgen::build_relax_bond(graph, natives(overwrite), b);
        else sgen::build_relax_product(graph, natives(overwrite), b);
    }
    // A variant is built, not steered: its params set before the engine starts.
    void tune(const char* side, double rate, double spin) {
        sg::Element& e = *graph.find(side)->find(side);
        e.params.set("rate", rate);
        e.params.set("spin", spin);
    }
    void start() {
        engine = std::make_unique<sg::Engine>(graph);
        engine->set_strict(true);
        engine->on_problem = [this](const std::string& p) { problems.push_back(p); };
        engine->start();
    }
    void send(const char* event, const char* key, double v) {
        engine->send("g", sg::Event{sg::Key{event}, sg::Params{}.set(key, v)});
    }
    const sg::Functor& f(const char* name) const { return *graph.functor(name); }
    sg::Gap gap(const char* name) const { return sg::gap(graph, f(name)); }
    double num(const char* state, const char* el, const char* key) const {
        return graph.find(state)->find(el)->params.num(key);
    }
    // The correlation g holds, read on what the sides hold: a and b carried
    // back by their own sections and glued, against g's total.
    double correlation() const {
        sg::State glued("relax.glued");
        f("ia").apply(*graph.find("a"), glued);
        f("ib").apply(*graph.find("b"), glued);
        return glued.find("ga")->params.num("x") + glued.find("gb")->params.num("x") - num("g", "bond", "total");
    }
    sg::Cover cover() const {
        sg::Cover c;
        c.add("ga", "g", "a", "ra", "ia");
        c.add("gb", "g", "b", "rb", "ib");
        return c;
    }
};

sg::Gap scalar(const char* name, double v) {
    sg::Gap g;
    g.where.push_back(name);
    g.by.push_back(v);
    return g;
}

std::string join(const std::vector<std::string>& v) {
    std::string s;
    for (const std::string& x : v) s += (s.empty() ? "" : "; ") + x;
    return s;
}

// --- 1. the laws ----------------------------------------------------------------------------
void laws() {
    std::printf("-- the laws\n");
    for (bool bond : {false, true}) {
        World w(bond);
        const char* which = bond ? "the bond" : "the product";
        check(w.graph.validate().empty(), std::string(which) + ": graph.validate() is empty");
        const sg::LawReport r = sg::verify(w.graph, {}, law_options());
        check(r.holds(), std::string(which) + ": sg::verify holds, every equation checked: " + (r.holds() ? "" : r.str()));
        w.start();
        w.engine->run_fixed(kDt, 4);
        check(w.problems.empty(), std::string(which) + ": the engine's watch names nothing, run strict");
    }
}

// --- 2. factorisation ---------------------------------------------------------------------
void factorisation() {
    std::printf("-- does g factorise as a (x) b?\n");
    World p(false), q(true);
    const sg::Cover cp = p.cover(), cq = q.cover();
    check(cp.descent_defects(p.graph).empty() && cq.descent_defects(q.graph).empty(),
          "descent holds in both: the pieces agree with g wherever they meet");
    check(cp.sections(p.graph, "g").size() == 3 && cq.sections(q.graph, "g").size() == 3,
          "both glue: a section from g reaches each piece");
    const auto lp = sg::locality_defects(p.graph, cp, "g");
    const auto lq = sg::locality_defects(q.graph, cq, "g");
    check(lp.empty(), "the product: every datum of g is held by a piece and comes back as it was - g is the glued section of a and b" +
                          (lp.empty() ? std::string() : ": " + join(lp)));
    bool total = false;
    for (const std::string& d : lq) total = total || d == "g: bond.total is held by no piece";
    check(total, "the bond: g holds what no piece does, and the cover says what: " + join(lq));
    std::printf("       (product: %zu locality defects; bond: %zu)\n", lp.size(), lq.size());

    // Locality, failed by example: move the correlation, and nothing a piece
    // holds moves - two different g with the same restrictions.
    q.start();
    q.engine->run_fixed(kDt, 2);
    const double before = q.num("g", "bond", "total");
    q.send("g.retotal", "d", 0.5);
    q.engine->run_fixed(kDt, 2);
    check(q.num("g", "bond", "total") != before && q.gap("ra").norm() == 0 && q.gap("rb").norm() == 0,
          "the bond: g changed, and neither restriction sees it (both gaps stay 0)");
}

// --- 3, 4, 5. a perturbation, its outcomes, time --------------------------------------------
struct Run {
    std::vector<sg::Gap> gaps;
    sg::Orbit orbit;
};

// Kick g, then watch a's gap for `steps` frames.
Run kicked(double rate, double spin, int steps) {
    World w(false);
    w.tune("a", rate, spin);
    w.start();
    w.engine->run_fixed(kDt, 3);
    w.send("g.kick", "d", 1.0);
    w.engine->tick(kDt);
    Run r;
    for (int i = 0; i < steps; ++i) {
        w.engine->tick(kDt);
        r.gaps.push_back(w.gap("ra"));
    }
    r.orbit = sg::orbit(r.gaps, kTol);
    return r;
}

void perturbation() {
    std::printf("-- a perturbation of g: the target at once, the contents in time\n");
    World w(false);
    w.start();
    w.engine->run_fixed(kDt, 3);
    check(w.gap("ra").norm() == 0 && w.gap("rb").norm() == 0, "before: a and b are what g says they are");
    w.send("g.kick", "d", 1.0);
    w.engine->tick(kDt);  // the port delivers: g moves
    const sg::Gap at_once = w.gap("ra");
    check(std::fabs(at_once.norm() - 1.0) < 1e-12 && w.num("a", "a", "x") == 1.0,
          "the frame g moves, a's gap is the whole kick (" + std::to_string(at_once.norm()) +
              ") and a has not moved: the relation is not applied to a's data");
    w.engine->tick(kDt);
    check(w.num("a", "a", "aim_x") == 2.0 && w.num("b", "b", "aim_y") == 1.5,
          "the next frame the aims are the restrictions' answer, before a step is taken toward anything else");
    check(w.num("a", "a", "x") > 1.0 && w.num("a", "a", "x") < 2.0, "and a has taken one step toward it, by its own arrow: x = " +
                                                                         std::to_string(w.num("a", "a", "x")));

    std::printf("-- the outcomes of a step, read from the gap\n");
    const Run conv = kicked(2.0, 1.0, 64);
    check(conv.orbit.kind == sg::Orbit::Kind::Converges && conv.orbit.monotone,
          "rate 2, spin 1: converges, the gap shrinking every step: " + conv.orbit.str());
    const Run cyc = kicked(0.0, kTurn, 64);
    check(cyc.orbit.kind == sg::Orbit::Kind::Periodic && cyc.orbit.period == 4,
          "rate 0, a turn per second at 4 steps a second: periodic, period 4 - never the aim: " + cyc.orbit.str());
    const Run wander = kicked(0.0, 1.0, 64);
    check(wander.orbit.kind == sg::Orbit::Kind::Persists && wander.orbit.period == 0 && wander.orbit.last > 0.5,
          "rate 0, spin 1 rad/s: persists, bounded and never back: " + wander.orbit.str());
    const Run grow = kicked(-0.5, 0.0, 64);
    check(grow.orbit.kind == sg::Orbit::Kind::Grows, "rate -0.5: grows: " + grow.orbit.str());
    const Run spiral = kicked(-0.2, 1.0, 64);
    check(spiral.orbit.kind == sg::Orbit::Kind::Grows && spiral.orbit.period == 0, "rate -0.2, spin 1: spirals out: " + spiral.orbit.str());

    std::printf("-- no time is no change; restored, the same trajectory\n");
    World t(false);
    t.tune("a", 2.0, 1.0);
    t.start();
    t.engine->run_fixed(kDt, 3);
    t.send("g.kick", "d", 1.0);
    t.engine->run_fixed(kDt, 4);
    const std::string a0 = sg::to_text(*t.graph.find("a"));
    const sg::Gap g0 = t.gap("ra");
    for (int i = 0; i < 3; ++i) t.engine->tick(0.0);
    check(sg::to_text(*t.graph.find("a")) == a0 && t.gap("ra").by == g0.by, "three frames of no time: a is exactly as it was");

    const char* states[] = {"g", "a", "b", "time"};
    std::vector<std::string> saved;
    for (const char* s : states) saved.push_back(sg::to_text(*t.graph.find(s)));
    std::vector<sg::Gap> first, second;
    for (int i = 0; i < 30; ++i) {
        t.engine->tick(kDt);
        first.push_back(t.gap("ra"));
    }
    bool restored = true;
    for (std::size_t i = 0; i < saved.size(); ++i) restored = sg::from_text(*t.graph.find(states[i]), saved[i]) && restored;
    for (int i = 0; i < 30; ++i) {
        t.engine->tick(kDt);
        second.push_back(t.gap("ra"));
    }
    bool same = restored && first.size() == second.size();
    for (std::size_t i = 0; same && i < first.size(); ++i) same = first[i].by == second[i].by;
    check(same, "restored from its own data, it runs the same 30 steps, to the bit");
}

// --- 6. the correlation ---------------------------------------------------------------------
void correlation() {
    std::printf("-- a correlation of g's own: what local relaxation can and cannot reach\n");
    World w(true);
    w.tune("a", 3.0, 0.0);
    w.tune("b", 0.5, 0.0);
    w.start();
    w.engine->run_fixed(kDt, 3);
    check(std::fabs(w.correlation()) < 1e-12, "before: a.x + b.x = total");

    // A correlated move of g: the sum is kept, but each side sees only its term.
    w.send("g.kick", "d", 1.0);
    w.engine->tick(kDt);
    std::vector<sg::Gap> ga, gb, corr;
    for (int i = 0; i < 160; ++i) {
        w.engine->tick(kDt);
        ga.push_back(w.gap("ra"));
        gb.push_back(w.gap("rb"));
        corr.push_back(scalar("correlation", w.correlation()));
    }
    const sg::Orbit oa = sg::orbit(ga, kTol), ob = sg::orbit(gb, kTol), oc = sg::orbit(corr, kTol);
    check(oa.kind == sg::Orbit::Kind::Converges && oa.monotone && ob.kind == sg::Orbit::Kind::Converges && ob.monotone,
          "kick: each side's own gap closes, monotone (a: " + oa.str() + "; b: " + ob.str() + ")");
    check(oc.kind == sg::Orbit::Kind::Converges && !oc.monotone && oc.peak > 0.3,
          "the correlation, held before and after, is broken on the way - a arrives before b - and holds again only once both have: " +
              oc.str());

    // The correlation itself moved: no restriction sees it, so no side has a target to relax to.
    w.send("g.retotal", "d", 0.5);
    w.engine->tick(kDt);
    ga.clear(), gb.clear(), corr.clear();
    for (int i = 0; i < 64; ++i) {
        w.engine->tick(kDt);
        ga.push_back(w.gap("ra"));
        gb.push_back(w.gap("rb"));
        corr.push_back(scalar("correlation", w.correlation()));
    }
    const sg::Orbit ra = sg::orbit(ga, kTol), rc = sg::orbit(corr, kTol);
    check(ra.kind == sg::Orbit::Kind::Converges && ra.peak < kTol && sg::orbit(gb, kTol).peak < kTol,
          "retotal: the sides' gaps are nothing throughout - there is nothing for them to do");
    check(rc.kind == sg::Orbit::Kind::Persists && rc.period == 1 && std::fabs(rc.last - 0.5) < 1e-9,
          "and the correlation is off by the whole move, for ever: " + rc.str());

    // g's own arrow, turned on by a declared event: g moves its terms, the
    // restrictions carry that to the sides, and they follow.
    w.send("g.couple", "k", 2.0);
    w.engine->tick(kDt);
    corr.clear();
    for (int i = 0; i < 240; ++i) {
        w.engine->tick(kDt);
        corr.push_back(scalar("correlation", w.correlation()));
    }
    const sg::Orbit cc = sg::orbit(corr, kTol);
    check(cc.kind == sg::Orbit::Kind::Converges, "couple: with g's own dynamics on, the correlation is reached: " + cc.str());
    check(w.gap("ra").norm() < kTol && w.gap("rb").norm() < kTol, "and both sides have followed g there");
    check(w.problems.empty(), "the engine's watch named nothing: " + join(w.problems));
}

// --- 7. what is not a relaxation -------------------------------------------------------------
void overwrite() {
    std::printf("-- a step that writes the target over the contents is refused\n");
    World w(false, true);
    // Built away from its aim, so that there is something to overwrite.
    w.graph.find("a")->find("a")->params.set("x", 0.0);
    const sg::LawReport r = sg::verify(w.graph, {}, law_options());
    bool drives = false;
    for (const sg::Violation& v : r.violations) drives = drives || v.law.find("drive") != std::string::npos;
    check(!r.ok() && drives, "sg::verify finds the drive law broken: no time changed it (" + std::to_string(r.violations.size()) +
                                 " violations)");
    for (const sg::Violation& v : r.violations)
        if (v.law.find("drive") != std::string::npos) {
            std::printf("       %s on %s: %s from %s left %s and %s (%s)\n", v.law.c_str(), v.where.c_str(), v.key.c_str(), v.before.c_str(), v.left.c_str(), v.right.c_str(), v.lhs.c_str());
            break;
        }
}

}  // namespace

int main() {
    laws();
    factorisation();
    perturbation();
    correlation();
    overwrite();
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "all held", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
