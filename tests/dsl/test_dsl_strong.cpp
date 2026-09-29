// Two invariants of the notation, held hard.
//
//   apply = atomic       a plan is made whole or not at all: a graph that meets a
//                        plan that fails - early, late, or in what it would make
//                        of the graph - is exactly as it was
//   facts = faithful     what the engine holds as data is in the facts, so a
//                        declaration that differs in any value the engine can
//                        inspect is not an equivalent declaration
#include <cstdio>
#include <string>

#include "sg/dsl/Apply.hpp"
#include "sg/dsl/Compile.hpp"
#include "sg/dsl/Compiler.hpp"
#include "sg/dsl/Facts.hpp"
#include "sg/sg.hpp"

namespace sgen {
void build_strongworld(sg::StateGraph&, const sg::dsl::Natives&, sg::dsl::Bindings&);
}

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "[ok]  " : "[FAIL]", what.c_str());
    if (!ok) ++failures;
}

bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

sg::dsl::Natives natives() {
    sg::dsl::Natives n;
    for (const char* name : {"foo", "bar"}) {
        n.arrow(name, [](sg::State&, sg::Element&, sg::Element*, const sg::Event&) {});
        n.transport(name, [](const sg::Element&, sg::Element&) {});
        n.edit(name, [](sg::StateGraph&, const sg::Event&) { return sg::Params{}; });
    }
    return n;
}

sg::dsl::Compiled compile(const std::string& src) {
    const sg::dsl::Compiled c = sg::dsl::compile_source(src, "strong.sg");
    if (!c.ok()) std::printf("%s", c.report().c_str());
    return c;
}

sg::dsl::Applied make(const std::string& text, sg::StateGraph& g) {
    // what is made on the base world names it: hall is built elsewhere, as far as the program is concerned
    const std::string src = text.find("initial hall") == std::string::npos ? "extern state hall\n" + text : text;
    const sg::dsl::Compiled c = compile(src);
    sg::dsl::Bindings bindings;
    return c.ok() ? sg::dsl::apply(c.plan, g, natives(), &bindings) : sg::dsl::Applied{false, "does not compile"};
}

// --- the world the plans are made on -----------------------------------------------------------
const char* kBase = R"(
initial hall
state time : temporal
state hall : spatial3d {
    element gate : portal { x = 0.0 y = 0.0 z = 0.0 yaw = 0.0 w = 1.0 h = 2.0 }
    element n : object { v = 0.0 one = 1.0 }
    n -> n : run(dt) on hall.run { v = v + one * dt }
}
drive time -> hall.run
    keeps active
state annex : spatial3d {
    element gate : portal { x = 5.0 y = 0.0 z = 0.0 yaw = 0.0 w = 1.0 h = 2.0 }
}
seam hall.gate <-> annex.gate
    name base
transition hall -[go.annex]-> annex
)";

using sg::dsl::Plan;
namespace plan = sg::dsl::plan;

std::vector<std::string> everything(const sg::StateGraph& g) { return sg::dsl::facts(g, sg::dsl::Scope::Whole); }

// --- apply is atomic ------------------------------------------------------------------------------
void atomic() {
    std::printf("-- apply is atomic\n");

    // A valid plan: every declaration appears.
    {
        sg::StateGraph g;
        check(make(kBase, g).ok, "the base world is made");
        const auto before = everything(g);
        const auto result = make(R"(
state lamp : spatial3d { element bulb : mesh { x = 1.0 } }
state lamp.look : look { fade = 0.2 }
wear lamp <- lamp.look
transition hall -[go.lamp]-> lamp
port lamp event lamp.set
functor lamp.f : hall -> lamp { event hall.lit -> lamp.lit }
)", g);
        // lamp has no arrow on lamp.set: the port is refused by the compiler, the rest is not made either
        check(!result.ok, "a program with one bad line is not made in part: " + result.why);
        check(sg::dsl::difference(before, everything(g)).empty(), "and the graph is as it was");

        const auto ok = make(R"(
state lamp : spatial3d { element bulb : mesh { x = 1.0 } }
state lamp.look : look { fade = 0.2 }
wear lamp <- lamp.look
transition hall -[go.lamp]-> lamp
functor lamp.f : hall -> lamp { event hall.lit -> lamp.lit }
)", g);
        check(ok.ok, "a valid plan is made: " + ok.why);
        const auto after = everything(g);
        const auto diff = sg::dsl::difference(before, after);
        bool state = false, worn = false, transition = false, functor = false, slot = false;
        for (const std::string& l : after) {
            state = state || l == "state lamp space3d";
            worn = worn || has(l, "embed lamp/look:lamp.look");
            transition = transition || has(l, "transition hall-go.lamp->lamp");
            functor = functor || l == "functor lamp.f hall -> lamp";
            slot = slot || l == "element lamp look look_slot";
        }
        check(state && worn && transition && functor && slot, "and every declaration appears: the state, its look worn, the transition, the functor");
        check(g.has_default(sg::Key{"lamp"}), "and the new state has its start");
        check(!diff.empty() && sg::verify(g).ok(), "and the laws hold");
    }

    // A plan refused before anything is touched.
    {
        sg::StateGraph g;
        make(kBase, g);
        const auto before = everything(g);
        const uint64_t revision = g.revision();
        const auto again = make("state annex : spatial3d", g);
        check(!again.ok && has(again.why, "already in the graph"), "a name taken: " + again.why);
        check(sg::dsl::difference(before, everything(g)).empty() && g.revision() == revision, "the graph is as it was, and never counted a change");
    }

    // Plans that fail late: made state by state, and then a step that cannot be.
    struct Case {
        const char* what;
        Plan plan;
        const char* because;
    };
    std::vector<Case> late;
    const auto prelude = [](Plan& p) {
        p.steps.push_back(plan::State{sg::Key{"early"}, "spatial3d"});
        p.steps.push_back(plan::Element{sg::Key{"early"}, sg::Key{"door"}, sg::Key{"portal"}, false});
        p.steps.push_back(plan::Says{sg::Key{"early"}, sg::Key{"early.said"}});
        p.steps.push_back(plan::Functor{sg::Key{"early.f"}, sg::Key{"early"}, sg::Key{"hall"}, false});
        p.steps.push_back(plan::Port{sg::Key{"early"}, sg::Key{"early.said"}});
        sg::Transition t;
        t.from = "hall";
        t.to = "early";
        t.trigger = "go.early";
        p.steps.push_back(plan::Connect{t});
        p.steps.push_back(plan::Wear{sg::Key{"hall"}, sg::Key{"early"}});  // a live host gains a look slot ...
    };
    {
        Plan p;
        prelude(p);
        p.steps.push_back(plan::Glue{sg::Key{"late.seam"}, sg::Key{"hall"}, sg::Key{"ghost"}, sg::Key{"early"}, sg::Key{"door"}, {}});
        late.push_back({"a doorway that is not there", std::move(p), "no element ghost"});
    }
    {
        Plan p;
        prelude(p);
        p.steps.push_back(plan::ComposeFunctors{sg::Key{"late.chain"}, {sg::Key{"early.f"}, sg::Key{"nowhere"}}});
        late.push_back({"a composition of a functor that is not there", std::move(p), "unknown"});
    }
    {
        Plan p;
        prelude(p);
        sg::Drive d;
        d.name = "time>early";
        d.clock = "no.clock";
        d.state = "early";
        d.trigger = "early.run";
        p.steps.push_back(plan::Drive{d});
        late.push_back({"a drive on a clock that is not there", std::move(p), "no.clock"});
    }
    {
        Plan p;
        prelude(p);
        sg::Drive d;
        d.name = "time>early";
        d.clock = "time";  // a real clock: it gains a timeline, and then the plan fails
        d.state = "early";
        d.trigger = "early.run";
        p.steps.push_back(plan::Drive{d});
        p.steps.push_back(plan::Wear{sg::Key{"nobody"}, sg::Key{"early"}});
        late.push_back({"a clock that gained a timeline, and a wearer who is not there", std::move(p), "nobody"});
    }
    {
        Plan p;
        prelude(p);
        p.steps.push_back(plan::Arrow{sg::Key{"early"}, sg::Key{"a"}, sg::Key{"ghost"}, sg::Key{"ghost2"}, sg::Key{"early.a"}, plan::Arrow::Body::None, {}, {}, false});
        late.push_back({"an arrow between elements that are not there", std::move(p), "missing"});
    }
    {
        Plan p;
        prelude(p);  // valid so far, but the state it made has nothing reaching it but a transition: fine - and this one does not:
        p.steps.push_back(plan::State{sg::Key{"island"}, "state"});
        late.push_back({"a state no interface reaches", std::move(p), "unreachable"});
    }
    {
        Plan p;
        prelude(p);
        sg::Embedding e;
        e.host = "nobody";
        e.portal = "p";
        e.guest = "early";
        e.name = "late.embed";
        p.steps.push_back(plan::Embed{e});
        late.push_back({"an embedding in a state that is not there", std::move(p), "unknown host"});
    }
    {
        Plan p;
        prelude(p);
        p.steps.push_back(plan::Film{sg::Key{"cam"}, sg::Key{"hall"}, sg::Key{"rig"}});  // a camera that is not a state
        late.push_back({"a camera that is not there", std::move(p), "unknown"});
    }
    for (Case& c : late) {
        sg::StateGraph g;
        make(kBase, g);
        const auto before = everything(g);
        const std::size_t states = g.size();
        sg::dsl::Bindings bindings;
        const auto r = sg::dsl::apply(c.plan, g, natives(), &bindings);
        check(!r.ok && has(r.why, "nothing was changed"), std::string(c.what) + ": refused - " + r.why);
        const auto diff = sg::dsl::difference(before, everything(g));
        check(diff.empty() && g.size() == states, std::string("  and the graph is fact for fact as it was (") + std::to_string(diff.size()) + " differences)");
        for (std::size_t i = 0; i < diff.size() && i < 6; ++i) std::printf("       %s\n", diff[i].c_str());
        check(!g.contains(sg::Key{"early"}) && g.find(sg::Key{"hall"})->find(sg::Key{"look"}) == nullptr, "  the live host has no look slot, the state made first is gone");
        check(g.functor(sg::Key{"early.f"}) == nullptr && g.transition(sg::Key{"hall-go.early->early"}) == nullptr && !g.has_port(sg::Key{"early"}, sg::Key{"early.said"}),
              "  nor its functor, its transition, its port");
        check(static_cast<const sg::Temporal&>(g.state("time")).has_timeline(sg::Key{"early"}) == false, "  the clock has no line of its own for it");
        // and the graph still works as the graph it was
        sg::Engine engine(g);
        engine.set_strict(true);
        engine.start(sg::Key{"hall"});
        engine.tick(0.1);
        check(engine.problems().empty() && sg::verify(g).ok(), "  it runs, and the laws hold");
    }

    // After a refusal the same names are free: the plan that could not be made leaves no trace to collide with.
    {
        sg::StateGraph g;
        make(kBase, g);
        Plan bad;
        bad.steps.push_back(plan::State{sg::Key{"twice"}, "state"});
        bad.steps.push_back(plan::Element{sg::Key{"twice"}, sg::Key{"e"}, sg::Key{"object"}, false});
        bad.steps.push_back(plan::ComposeFunctors{sg::Key{"c"}, {sg::Key{"nowhere"}}});
        sg::dsl::Bindings b;
        check(!sg::dsl::apply(bad, g, natives(), &b).ok, "a plan fails late");
        const auto retry = make(R"(
state twice { element e }
transition hall -[go.twice]-> twice
)", g);
        check(retry.ok, "and its names are free for the next: " + retry.why);
    }

    // Not while the graph is being checked.
    {
        sg::StateGraph g;
        make(kBase, g);
        const auto c = compile("extern state hall\nstate late\ntransition hall -[go.late]-> late\n");
        sg::dsl::Bindings b;
        sg::dsl::Applied r;
        {
            sg::StateGraph::Sealed sealed(g);
            r = sg::dsl::apply(c.plan, g, natives(), &b);
        }
        check(!r.ok && has(r.why, "being checked") && !g.contains(sg::Key{"late"}), "sealed: refused before anything: " + r.why);
    }
}

// --- the engine's own checkpoint ---------------------------------------------------------------------------
void checkpoint() {
    std::printf("-- checkpoint and rollback\n");
    sg::StateGraph g;
    make(kBase, g);
    make("state keep { element k }\ntransition hall -[go.keep]-> keep\n", g);
    const auto before = everything(g);
    const sg::Checkpoint mark = g.checkpoint();
    // Everything a graph can be given.
    auto& s = g.add<sg::State>("temp");
    s.add_element("e", "object");
    g.add_functor("temp.f", "temp", "hall");
    g.compose_functors("temp.c", {sg::Key{"temp.f"}});
    g.connect(sg::Key{"hall"}, sg::Key{"go.temp"}, sg::Key{"temp"});
    g.embed(sg::Key{"temp.e"}, sg::Key{"temp"}, sg::Key{"e"}, sg::Key{"hall"}, sg::Key{}, sg::Key{});
    g.port(sg::Key{"temp"}, sg::Key{"temp.ev"});
    g.keep(sg::Key{"temp.f"});
    g.lens(sg::Key{"temp.f"}, sg::Key{"temp.f"});
    sg::Drive d;
    d.name = "time>temp";
    d.clock = "time";
    d.state = "temp";
    d.trigger = "temp.run";
    g.drive(d);
    // A thing replaced by name comes back as it was.
    sg::Drive again;
    again = g.drives().front();
    again.keeps = sg::Keeps::Always;
    g.drive(again);
    g.keep_default(sg::Key{"temp"});
    g.set_initial(sg::Key{"temp"});
    g.rollback(mark);
    check(sg::dsl::difference(before, everything(g)).empty(), "what was added, replaced and re-pointed is put back, fact for fact");
    check(g.initial() == "hall" && !g.contains(sg::Key{"temp"}) && !g.has_default(sg::Key{"temp"}), "the initial state, the states and their defaults too");
    // The indexes are whole again: things can be added and found.
    g.add<sg::State>("temp").add_element("e", "object");
    g.connect(sg::Key{"hall"}, sg::Key{"go.temp"}, sg::Key{"temp"});
    check(g.transition(sg::Key{"hall-go.temp->temp"}) != nullptr && g.transition(sg::Key{"hall-go.keep->keep"}) != nullptr, "and the graph goes on from there");
    check(g.resolve(g.state("hall"), sg::Event{sg::Key{"go.keep"}}) != nullptr, "dispatch by trigger finds what was there");
}

// --- the world that compiles from inside obeys the same ------------------------------------------------------
void inside() {
    std::printf("-- a world that compiles from inside is all or nothing too\n");
    sg::StateGraph graph;
    sg::dsl::Bindings bindings;
    sg::dsl::Natives available, own;
    sg::dsl::register_compiler(own, available, &bindings);
    sgen::build_strongworld(graph, own, bindings);
    sg::Engine engine(graph);
    engine.set_strict(true);
    engine.start(sg::Key{"desk"});
    engine.tick(0.1);
    const auto say = [&](const std::string& program) {
        engine.send(sg::Key{"source"}, sg::Event{sg::Key{"source.set"}, sg::Params{}.set("text", program)});
        engine.send(sg::Key{"source"}, sg::Event{sg::Key{"source.request"}});
        for (int i = 0; i < 4; ++i) engine.tick(0.1);
    };
    const sg::State& compiler = graph.state("compiler");
    const auto why = [&] { return compiler.element("report").params.get_or<std::string>("why", ""); };
    const auto phase = [&] { return compiler.params().get_or<std::string>("phase", ""); };

    // Bad source: compiles, and then the graph it would make does not validate.
    // (the source and the compiler hold the text and the answer: that is their data, and it changes)
    const auto structure = [&] {
        std::vector<std::string> out;
        for (const std::string& l : everything(graph)) {
            const bool own = l.rfind("param source ", 0) == 0 || l.rfind("eparam source ", 0) == 0 || l.rfind("param compiler ", 0) == 0 ||
                             l.rfind("eparam compiler ", 0) == 0;
            if (!own) out.push_back(l);
        }
        return out;
    };
    const auto before = structure();
    say(R"(
extern state desk
state island : spatial3d { element e : mesh { x = 1.0 } }
state isle
transition desk -[go.isle]-> isle
state second
)");
    check(phase() == "error" && has(why(), "does not validate") && has(why(), "second"), "bad source: the compiler reports error - " + why());
    check(sg::dsl::difference(before, structure()).empty(), "  and the graph is as it was: not the island, not the isle that was fine on its own");
    check(engine.problems().empty(), "  and the engine's watch never saw a half");

    // Bad source: a name already in the graph.
    say("state source\n");
    check(phase() == "error" && has(why(), "already in the graph"), "bad source, a name taken: " + why());
    check(sg::dsl::difference(before, structure()).empty(), "  graph unchanged");

    // Valid source: the whole change.
    say(R"(
extern state desk
state lamp : spatial3d { element bulb : mesh { x = 1.0 } }
state lamp.look : look { fade = 0.2 }
wear lamp <- lamp.look
transition desk -[go.lamp]-> lamp
)");
    check(phase() == "success", "valid source: success (" + phase() + ": " + why() + ")");
    check(graph.contains(sg::Key{"lamp"}) && graph.contains(sg::Key{"lamp.look"}) && graph.embedding(sg::Key{"lamp/look:lamp.look"}) &&
              graph.transition(sg::Key{"desk-go.lamp->lamp"}),
          "  the entire change is in: states, the look worn, the transition");
    check(engine.problems().empty() && graph.validate().empty(), "  and the graph validates");
}

// --- facts are faithful ------------------------------------------------------------------------------------------
const char* kHub = R"(
initial hub
state hub : spatial3d {
    element seat : portal { x = 0.0 y = 0.0 z = 0.0 yaw = 0.0 w = 1.0 h = 2.0 open = true }
    element n : object { v = 0.0 one = 1.0 }
    element m : object { v = 0.0 }
    say hub.shown
)";

// Two programs that differ in one value; the graph one makes is not the other's.
void differs(const std::string& what, const std::string& in_hub, const std::string& outside, const std::string& in_hub2, const std::string& outside2) {
    const std::string tail = "\nstate other : spatial3d { element seat : portal { x = 5.0 y = 0.0 z = 0.0 yaw = 0.0 w = 1.0 h = 2.0 } element extra : object }\n"
                             "state guest { element g }\ntransition hub -[to.other]-> other\ntransition hub -[to.guest]-> guest\n";
    const sg::dsl::Compiled a = compile(kHub + in_hub + "\n}\n" + outside + tail);
    const sg::dsl::Compiled b = compile(kHub + in_hub2 + "\n}\n" + outside2 + tail);
    if (!a.ok() || !b.ok()) return check(false, what + ": both compile");
    sg::StateGraph g;
    sg::dsl::Bindings bindings;
    const auto r = sg::dsl::apply(a.plan, g, natives(), &bindings);
    if (!r.ok) return check(false, what + ": the first is made: " + r.why);
    const auto own = sg::dsl::missing(a.plan, g);
    const auto other = sg::dsl::missing(b.plan, g);
    check(own.empty() && !other.empty(), what + ": the graph one makes is not the other's (" + (other.empty() ? std::string("EQUAL") : other.front()) + ")");
}

void faithful() {
    std::printf("-- facts are faithful\n");
    differs("transition constants: person = 1 / person = 2", "", "transition hub -[go]-> guest\n    with person = 1.0\n", "", "transition hub -[go]-> guest\n    with person = 2.0\n");
    differs("transition constants: a key", "", "transition hub -[go]-> guest\n    with person = 1.0\n", "", "transition hub -[go]-> guest\n    with human = 1.0\n");
    differs("transition constants: none / some", "", "transition hub -[go]-> guest\n", "", "transition hub -[go]-> guest\n    with person = 1.0\n");
    differs("transition constants: a number / a word", "", "transition hub -[go]-> guest\n    with person = 1.0\n", "", "transition hub -[go]-> guest\n    with person = \"1\"\n");
    differs("transition kind", "", "transition hub -[go]-> guest\n", "", "transition hub -[go]-> guest push\n");
    differs("native arrow: foo / bar", "n -> n : act on hub.act native foo", "", "n -> n : act on hub.act native bar", "");
    differs("affine arrow: a coefficient", "n -> n : inc on hub.inc { v = v + one }", "", "n -> n : inc on hub.inc { v = v + 2 * one }", "");
    differs("affine arrow: a term", "n -> n : inc on hub.inc { v = v + one }", "", "n -> n : inc on hub.inc { v = v + v }", "");
    differs("affine arrow: a bias", "n -> n : inc on hub.inc { v = v + 1 }", "", "n -> n : inc on hub.inc { v = v + 2 }", "");
    differs("affine arrow: the argument", "n -> n : inc(dt) on hub.inc { v = v + one * dt }", "state time : temporal\ndrive time -> hub.inc",
            "n -> n : inc(dt) on hub.inc { v = v + one }", "state time : temporal\ndrive time -> hub.inc");
    differs("affine arrow: read from the target", "n -> m : inc on hub.inc { v = n.v + one }", "", "n -> m : inc on hub.inc { v = m.v + one }", "");
    differs("arrow ends", "n -> m : inc on hub.inc", "", "m -> n : inc on hub.inc", "");
    differs("arrow trigger", "n -> n : inc on hub.inc", "", "n -> n : inc on hub.dec", "");
    differs("composition chain: f ; g / g ; f", "n -> n : f on hub.f { v = v + one }\n n -> n : g on hub.g { v = v + 1 }\n compose fg = f ; g",
            "", "n -> n : f on hub.f { v = v + one }\n n -> n : g on hub.g { v = v + 1 }\n compose fg = g ; f", "");
    differs("composition trigger", "n -> n : f on hub.f\n compose ff = f ; f on hub.ff", "", "n -> n : f on hub.f\n compose ff = f ; f on hub.ff2", "");
    differs("transport: only(v) / only(one)", "", "functor f : hub -> guest { object n -> g only(v) }", "", "functor f : hub -> guest { object n -> g only(one) }");
    differs("transport: swizzle", "", "functor f : hub -> guest { object n -> g swizzle(a = v) }", "", "functor f : hub -> guest { object n -> g swizzle(a = one) }");
    differs("transport: swizzle rest", "", "functor f : hub -> guest { object n -> g swizzle(a = v) }", "", "functor f : hub -> guest { object n -> g swizzle(a = v) rest }");
    differs("transport: a step it says", "", "functor f : hub -> guest { object n -> g { a = 2 * v } }", "", "functor f : hub -> guest { object n -> g { a = 3 * v } }");
    differs("transport: copy / a step", "", "functor f : hub -> guest { object n -> g }", "", "functor f : hub -> guest { object n -> g { a = v } }");
    differs("transport: native foo / bar", "", "functor f : hub -> guest { object n -> g native foo }", "", "functor f : hub -> guest { object n -> g native bar }");
    differs("transport: native / a step", "", "functor f : hub -> guest { object n -> g native foo }", "", "functor f : hub -> guest { object n -> g { a = v } }");
    differs("functor event map", "", "functor f : hub -> guest { object n -> g\n event hub.shown -> guest.a }", "", "functor f : hub -> guest { object n -> g\n event hub.shown -> guest.b }");
    differs("composite functors: f ; g / g ; f", "",
            "functor f : hub -> guest { object n -> g }\nfunctor h : guest -> hub { object g -> m }\ncompose fh = f ; h", "",
            "functor f : hub -> guest { object n -> g }\nfunctor h : guest -> hub { object g -> m }\ncompose fh = h ; f");
    differs("embedding: sync", "", "embed hub.seat -> guest sync commit", "", "embed hub.seat -> guest sync live");
    differs("embedding: focus", "", "embed hub.seat -> guest focus true", "", "embed hub.seat -> guest focus false");
    differs("embedding: follows", "", "embed hub.seat -> guest follows true", "", "embed hub.seat -> guest follows false");
    differs("embedding: propagate", "", "embed hub.seat -> guest sync live propagate continuous", "", "embed hub.seat -> guest sync live propagate onchange");
    differs("embedding: name", "", "embed hub.seat -> guest name a", "", "embed hub.seat -> guest name b");
    differs("seam: also", "", "seam hub.seat <-> other.seat name s\n    also hub.n <-> other.extra", "", "seam hub.seat <-> other.seat name s");
    differs("drive: keeps", "n -> n : run(dt) on hub.run { v = v + one * dt }", "state time : temporal\ndrive time -> hub.run keeps active",
            "n -> n : run(dt) on hub.run { v = v + one * dt }", "state time : temporal\ndrive time -> hub.run keeps always");
    differs("drive: additive", "n -> n : run(dt) on hub.run { v = v + one * dt }", "state time : temporal\ndrive time -> hub.run",
            "n -> n : run(dt) on hub.run { v = v + one * dt }", "state time : temporal\ndrive time -> hub.run additive");
    differs("edit: reply", "", "edit hub on hub.shown native foo reply a.done", "", "edit hub on hub.shown native foo reply b.done");
    differs("edit: native foo / bar", "", "edit hub on hub.shown native foo", "", "edit hub on hub.shown native bar");
    differs("param value: a double / an integer", "n -> n : f on hub.f", "state x { w = 1.0 }\ntransition hub -[to.x]-> x",
            "n -> n : f on hub.f", "state x { w = int(1) }\ntransition hub -[to.x]-> x");
    differs("says", "n -> n : f on hub.f", "", "n -> n : f on hub.f\n say hub.other", "");

    // The same declaration is equal to itself, and to the C++ that makes it - by what C++ can say.
    {
        const std::string prog = std::string(kHub) + "n -> n : act on hub.act native foo\n}\nstate guest { element g }\ntransition hub -[to.guest]-> guest\n"
                                 "transition hub -[go]-> guest\n    with person = 1.0\n";
        const sg::dsl::Compiled c = compile(prog);
        sg::StateGraph by_plan, by_hand;
        sg::dsl::Bindings b;
        check(c.ok() && sg::dsl::apply(c.plan, by_plan, natives(), &b).ok, "one program");
        check(sg::dsl::missing(c.plan, by_plan).empty() && sg::dsl::unverified(c.plan, by_plan).empty(), "  its graph holds every fact, the native by name, none unnamed");

        auto& hub = by_hand.add<sg::Spatial3D>("hub");
        hub.add_element("seat", "portal");
        hub.add_element("n", "object");
        hub.add_element("m", "object");
        hub.element("seat").params.set("x", 0.0).set("y", 0.0).set("z", 0.0).set("yaw", 0.0).set("w", 1.0).set("h", 2.0).set("open", true);
        hub.element("n").params.set("v", 0.0).set("one", 1.0);
        hub.element("m").params.set("v", 0.0);
        hub.says("hub.shown");
        hub.loop("act", "n", "hub.act", [](sg::State&, sg::Element&, sg::Element*, const sg::Event&) {});  // a lambda: no name
        by_hand.add<sg::State>("guest").add_element("g", "object");
        by_hand.connect(sg::Key{"hub"}, sg::Key{"to.guest"}, sg::Key{"guest"});
        sg::Transition t;
        t.from = "hub";
        t.to = "guest";
        t.trigger = "go";
        t.enter.set("person", 1.0);
        by_hand.connect(std::move(t));
        check(sg::dsl::missing(c.plan, by_hand).empty(), "  the C++ that says the same is the same: enter, arrow, everything");
        const auto unnamed = sg::dsl::unverified(c.plan, by_hand);
        check(unnamed.size() == 1 && has(unnamed[0], "native:foo"), "  but its lambda is not shown to be `foo`: reported, not proven");

        sg::StateGraph wrong;
        wrong.add<sg::Spatial3D>("hub");
        check(!sg::dsl::missing(c.plan, wrong).empty(), "  and a graph without them is missing them");

        // person = 2 in the C++ against person = 1 in the notation: not the same declaration.
        sg::StateGraph two;
        two.add<sg::State>("hub");
        two.add<sg::State>("guest");
        sg::Transition t2;
        t2.from = "hub";
        t2.to = "guest";
        t2.trigger = "go";
        t2.enter.set("person", 2.0);
        two.connect(std::move(t2));
        bool person_differs = false;
        for (const std::string& l : sg::dsl::missing(c.plan, two)) person_differs = person_differs || (has(l, "transition hub-go->guest") && has(l, "enter=[person=d:1]"));
        check(person_differs, "  person = 1 is not person = 2");
    }
}

}  // namespace

int main() {
    atomic();
    checkpoint();
    inside();
    faithful();
    std::printf("\n%s\n", failures ? "SOME TESTS FAILED" : "all tests passed");
    return failures ? 1 : 0;
}
