// The DSL: notation for a construction the engine already has.
//
// What is checked, in four parts:
//   1. the language - what it says, and what it refuses (the ontology's rules
//      as errors, each with the reason)
//   2. the vertical slice - Void > Terminal > Exit, exit.out carried through
//      the graph to a directed transition, Looks, a camera, input reaching the
//      focused guest, time on a Temporal, a seam crossed one way - compiled by
//      sgc into C++ and run by the engine, which knows nothing of the DSL
//   3. one construction, three readings - the plan's facts, the generated
//      C++'s graph, the plan applied to a live graph: the same
//   4. a world that compiles from inside - source and compiler are states, and
//      the change comes by graph.edit
#include <cstdio>
#include <string>

#include "sg/dsl/Apply.hpp"
#include "sg/dsl/Compile.hpp"
#include "sg/dsl/Compiler.hpp"
#include "sg/dsl/Emit.hpp"
#include "sg/dsl/Facts.hpp"
#include "sg/dsl/Parse.hpp"
#include "sg/sg.hpp"

namespace sgen {
void build_scenario(sg::StateGraph&, const sg::dsl::Natives&, sg::dsl::Bindings&);
void build_world(sg::StateGraph&, const sg::dsl::Natives&, sg::dsl::Bindings&);
void build_kitchen(sg::StateGraph&, const sg::dsl::Natives&, sg::dsl::Bindings&);
}  // namespace sgen

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "[ok]  " : "[FAIL]", what.c_str());
    if (!ok) ++failures;
}

bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

sg::dsl::Compiled compile(const std::string& src, sg::dsl::Options o = {}) { return sg::dsl::compile_source(src, "test.sg", o); }

// The errors of a source, as one text.
std::string refused(const std::string& src, sg::dsl::Options o = {}) {
    const sg::dsl::Compiled c = compile(src, o);
    return c.ok() ? "" : c.report();
}

template <class T>
int count(const sg::dsl::Plan& p) {
    int n = 0;
    for (const sg::dsl::Step& s : p.steps) n += std::holds_alternative<T>(s) ? 1 : 0;
    return n;
}

bool lists(const std::vector<std::string>& facts, const std::string& line) {
    for (const std::string& f : facts)
        if (f == line) return true;
    return false;
}

const char* kCamera = R"(
state camera : camera {
    element lens : portal { fov = 70.0 }
    lens -> lens : zoom(fov) on camera.zoom
}
)";

// --- 1. the language ----------------------------------------------------------------------
void language() {
    std::printf("-- the language\n");

    // A basic state.
    {
        const auto c = compile(R"(
state exit {
    element idle
    element playing
    element finished

    idle -> playing : start
    playing -> finished : win

    say win
}
)");
        check(c.ok(), "a basic state compiles: " + c.report());
        const auto f = sg::dsl::facts(c.plan);
        check(lists(f, "arrow exit start idle -> playing on start body=none"), "an arrow is an arrow, its trigger its name");
        check(lists(f, "says exit win"), "say is State::says");
        check(count<sg::dsl::plan::Element>(c.plan) == 3, "three elements, one of them each");
    }

    // A spatial state: what its class makes is its own.
    {
        const auto c = compile(R"(
state void : spatial3d {
    element terminal : portal {
        x = 0
        y = 1
        z = -2
    }
}
)");
        check(c.ok(), "a spatial state compiles: " + c.report());
        check(lists(sg::dsl::facts(c.plan), "eparam void terminal z d:-2"), "a number is a double, as a world's parameters are");
        const auto c2 = compile("state s : state { w = int(30) }");
        check(lists(sg::dsl::facts(c2.plan), "param s w i:30"), "int(...) is an integer");
    }

    // A camera: its own lens, and its own arrows, declared and not made again.
    {
        const auto c = compile(kCamera);
        check(c.ok(), "a camera compiles: " + c.report());
        check(count<sg::dsl::plan::Arrow>(c.plan) == 1 && !sg::dsl::facts(c.plan).empty(), "zoom is one declaration");
        const std::string wrong = refused(R"(
state camera : camera {
    lens -> lens : zoom(fov) on camera.other
}
)");
        check(has(wrong, "arrow zoom of camera is lens -> lens on camera.zoom"), "an arrow its kind makes cannot be redeclared as another");
        const std::string body = refused(R"(
state camera : camera {
    lens -> lens : zoom(fov) on camera.zoom native zoom_it
}
)");
        check(has(body, "is made by its kind"), "nor given a second body");
    }

    // The camera as it is written in the brief: arrows declared as its kind makes them.
    {
        const auto c = compile(R"(
state camera : camera {
    element lens : portal {
        fov = 70
    }

    lens -> lens : zoom(fov)
    lens -> lens : aim(x, y, z, yaw, pitch)
}
)");
        check(c.ok(), "the brief's camera compiles, its arrows declared as they are made: " + c.report());
    }

    // A Look, and a shader as state data.
    {
        const auto c = compile(R"(
state void.look : look {
    fade = 0.4
    element scene : pass {
        uFogDensity = 0.01
        uAmbient = 0.2
        uSky = [0.05, 0.06, 0.08]
    }
    element composite : pass {
        uBloom = 0.6
    }
}
state dream.look : look {
    element composite : pass {
        fs = file("dream.frag")
        uWarp = 0.7
    }
}
)",
                              [] {
                                  sg::dsl::Options o;
                                  o.read_file = [](const std::string& path, const std::string&, std::string& out) {
                                      out = "// " + path + "\nvoid main() {}\n";
                                      return true;
                                  };
                                  return o;
                              }());
        check(c.ok(), "a Look compiles: " + c.report());
        const auto f = sg::dsl::facts(c.plan);
        check(lists(f, "eparam void.look scene uSky.z d:0.08"), "a vector is its components, as a look reads them");
        bool shader = false;
        for (const std::string& l : f) shader = shader || (has(l, "eparam dream.look composite fs s:") && has(l, "void\\smain"));
        check(shader, "file(...) is the text of the file, as data of the pass");
        check(has(refused(R"(state a.look : look { element composite : pass { fs = file("x.frag") } })"), "reads the filesystem"),
              "with no reader, file(...) is refused: a source does no IO");
    }

    // wear and film: sugar that leaves only sg::wear and sg::film.
    {
        const auto c = compile(std::string(R"(
state room : spatial3d { element tripod : anchor { x = 0.0 } }
state room.look : look
wear room <- room.look
)") + kCamera + "film camera -> room\n    rig tripod\n");
        check(c.ok(), "wear and film compile: " + c.report());
        const auto f = sg::dsl::facts(c.plan);
        check(lists(f, sg::dsl::facts(c.plan).front()) && count<sg::dsl::plan::Wear>(c.plan) == 1 && count<sg::dsl::plan::Film>(c.plan) == 1,
              "one Wear step, one Film step");
        bool worn = false, filmed = false;
        for (const std::string& l : f) {
            worn = worn || l == "embed room/look:room.look host=room portal=look guest=room.look subject=- in=- out=- sync=commit propagate=onchange focus=1 follows=0";
            filmed = filmed || has(l, "embed camera.film.room host=camera portal=lens guest=room subject=- in=- out=camera.film.room.rig sync=live propagate=continuous focus=0 follows=1");
        }
        check(worn, "wear is the embedding sg::wear declares");
        check(filmed, "film is the embedding sg::film declares, rigged");
        check(has(refused("state a\nstate b\nwear a <- b"), "b is a state, not a look"), "a look is worn only if it is one");
    }

    // State ids have dots too: a name is read as the one state-and-element it can be.
    {
        const auto c = compile(R"(
state void { element pane : portal { open = true } }
state void.look : look
state void.pane2
embed void.pane -> void.look
)");
        check(c.ok(), "void.pane is void's pane, though void.look is a state: " + c.report());
        const std::string both = refused(R"(
state a { element b.c : portal }
state a.b { element c : portal }
state g
embed a.b.c -> g
)");
        check(has(both, "cannot tell which state a.b.c names an element of"), "and a name that is two readings is refused, not guessed: " + both);
        const auto sub = compile(R"(
state a { element x : portal }
state g
embed a.x -> g
    subject a
)");
        bool empty_subject = false;
        for (const auto& s : sub.plan.steps)
            if (const auto* e = std::get_if<sg::dsl::plan::Embed>(&s)) empty_subject = e->e.subject.empty();
        check(sub.ok() && empty_subject, "a subject that is the host is said as the engine says it: nothing");
    }

    // A functor, a transition, composition.
    {
        const auto c = compile(R"(
state a { element x }
state b { element y }
state c { element z }
functor ab : a -> b { object x -> y
    event a.out -> b.in }
functor bc : b -> c { object y -> z }
compose ac = ab ; bc
transition a -[a.out]-> b carry ab
)");
        check(c.ok(), "functors, composition and a transition compile: " + c.report());
        check(count<sg::dsl::plan::ComposeFunctors>(c.plan) == 1, "compose is compose_functors");
        const std::string bad = refused("state a { element x }\nstate b { element y }\nstate c { element z }\n"
                                        "functor ab : a -> b { object x -> y }\nfunctor ca : c -> a { object z -> x }\ncompose bad = ab ; ca");
        check(has(bad, "compose: cod(ab)=b != dom(ca)=c"), "composition is typed: cod(f) must be dom(g)");
        const std::string arrows = refused(R"(
state s {
    element p
    element q
    element r
    p -> q : f
    p -> q : g
    compose h = f ; g
}
)");
        check(has(arrows, "compose: cod(f)=q != dom(g)=p"), "arrows compose typed too");
    }

    // A functor's transports.
    {
        const auto c = compile(R"(
state a { element x }
state b { element y }
transport bend native euclidean_to_hyperbolic
functor f : a -> b {
    object x -> y via bend
}
)");
        check(c.ok(), "a named native transport compiles: " + c.report());
        bool native = false;
        for (const auto& l : sg::dsl::facts(c.plan)) native = native || l == "object f x -> y native:euclidean_to_hyperbolic";
        check(native, "the alias is gone: only the native is left");
    }

    // lens, keep, port, drive.
    {
        const auto c = compile(R"(
state time : temporal
state a { element x
    x -> x : step(dt) on a.step { x = x + vx * dt } }
state b { element y }
functor get : a -> b { object x -> y }
functor put : b -> a { object y -> x }
lens get <-> put
keep get
port a.step
drive time -> a
    event a.step
    keeps always
)");
        check(c.ok(), "lens, keep, port and drive compile: " + c.report());
        bool drive = false;
        for (const auto& l : sg::dsl::facts(c.plan))
            drive = drive || l == "drive time>a clock=time state=a trigger=a.step additive=0 line=a keeps=always";
        check(drive, "a drive is sg::drive: its name, its line, how long it keeps");
        const auto s = compile("state time : temporal\nstate desert { element d\n d -> d : day(dt) on desert.day { x = x + vx * dt } }\ndrive time -> desert.day keeps always");
        check(s.ok() && lists(sg::dsl::facts(s.plan), "drive time>desert clock=time state=desert trigger=desert.day additive=0 line=desert keeps=always"),
              "drive time -> desert.day is the same drive, the state and its event as named: " + s.report());

        // State built elsewhere: named, not made. Its relations are declared, and the graph checks them.
        const auto e = compile(R"(
extern state time : temporal
extern state doorway
extern functor model.in : doorway -> doorway
drive time -> doorway event door.step
port doorway event door.hold
port doorway.moved
keep model.in
initial doorway
)");
        check(e.ok(), "extern states and functors let the C++ and the notation be ported a declaration at a time: " + e.report());
        const auto f = sg::dsl::facts(e.plan);
        check(lists(f, "drive time>doorway clock=time state=doorway trigger=door.step additive=0 line=doorway keeps=active") &&
                  lists(f, "port doorway door.hold") && lists(f, "port doorway doorway.moved") && lists(f, "keep model.in") && lists(f, "initial doorway"),
              "and they lower to the drive, the ports, the kept functor and the initial state");
        check(count<sg::dsl::plan::State>(e.plan) == 0, "an extern state is not made");
        check(has(refused("extern state a\nextern state a"), "declared twice"), "and is one state");
        check(has(refused("state a\ninitial a\ninitial a"), "one initial state"), "the graph starts in one");
    }

    // Seams and transitions are different.
    {
        const std::string rooms = R"(
state room_a : spatial3d { element door : portal { x = 0.0 y = 0.0 z = 0.0 yaw = 0.0 w = 1.0 h = 2.0 } }
state room_b : spatial3d { element door : portal { x = 5.0 y = 0.0 z = 0.0 yaw = 0.0 w = 1.0 h = 2.0 } }
)";
        const auto seam_only = compile(rooms + "seam room_a.door <-> room_b.door\n");
        check(seam_only.ok(), "a seam compiles: " + seam_only.report());
        check(count<sg::dsl::plan::Glue>(seam_only.plan) == 1 && count<sg::dsl::plan::Connect>(seam_only.plan) == 0,
              "a seam alone implies no transition: no way to be active in either room");
        const auto one_way = compile(rooms + "seam room_a.door <-> room_b.door\ntransition room_a -[cross]-> room_b\n");
        int a_to_b = 0, b_to_a = 0;
        for (const auto& s : one_way.plan.steps)
            if (const auto* t = std::get_if<sg::dsl::plan::Connect>(&s)) {
                a_to_b += t->t.from == "room_a" && t->t.to == "room_b";
                b_to_a += t->t.from == "room_b" && t->t.to == "room_a";
            }
        check(count<sg::dsl::plan::Glue>(one_way.plan) == 1 && a_to_b == 1 && b_to_a == 0, "a seam and one transition: shared boundary, one-way traversal");
        const auto teleport = compile(rooms + "transition room_a -[teleport]-> room_b\n");
        check(count<sg::dsl::plan::Glue>(teleport.plan) == 0 && count<sg::dsl::plan::Connect>(teleport.plan) == 1,
              "a transition alone implies no seam: nothing is shared, and it is enough");
        const auto both = compile(rooms + "transition room_a -[go]-> room_b\ntransition room_b -[back]-> room_a\n");
        check(count<sg::dsl::plan::Glue>(both.plan) == 0 && count<sg::dsl::plan::Connect>(both.plan) == 2,
              "two transitions make no seam: they are independent structure");
        check(has(refused("state a : spatial3d { element d : wall }\nstate b : spatial3d { element d : portal }\nseam a.d <-> b.d"), "not a portal"),
              "a seam identifies doorways");
    }

    // when: an event mapping, and nothing at run time.
    {
        const auto c = compile(std::string(kCamera) + R"(
state player { element seat
    seat -> seat : sit on player.sit
    say player.sits }
when player.sits camera.zoom
)");
        check(c.ok(), "when compiles: " + c.report());
        check(count<sg::dsl::plan::Functor>(c.plan) == 1 && count<sg::dsl::plan::EventMap>(c.plan) == 1,
              "when is a functor with an event map, and nothing else");
        check(has(refused(std::string(kCamera) + "state player { element seat\n say player.sits }\nwhen player.sits camera.zoom(fov: 30)\n"),
                  "a functor relabels an event and passes its arguments as they are"),
              "a constant argument has no functor to carry it: refused, with why");
        check(has(refused(std::string(kCamera) + "state player { element seat }\nwhen player.sits camera.zoom\n"), "does not say player.sits"),
              "a functor carries only what a state says");
    }

    // bind: a table for the input adapter.
    {
        const auto c = compile(R"(
state player { element body
    body -> body : step(forward) on player.step { z = z + speed * forward }
    body -> body : use on player.use }
bind keyboard {
    W      -> player.step(forward: 1)
    S      -> player.step(forward: -1)
    Mouse1 -> player.use
}
)");
        check(c.ok(), "bind compiles: " + c.report());
        const auto* b = [&]() -> const sg::dsl::plan::Bind* {
            for (const auto& s : c.plan.steps)
                if (const auto* x = std::get_if<sg::dsl::plan::Bind>(&s)) return x;
            return nullptr;
        }();
        check(b && b->entries.size() == 3 && b->entries[1].args.num("forward") == -1.0, "a key is an event and its arguments");
        check(has(refused("state player { element body }\nbind keyboard { W -> player.x += 1 }"), "input never changes a state"),
              "input cannot write a state");
        check(has(refused("state player { element body }\nbind keyboard { W -> player.step(forward: 1) }"), "no arrow of player is triggered by player.step"),
              "a key names an event some arrow answers");
    }

    // Native computations.
    {
        const auto c = compile(R"(
state time : temporal
state world { element body
    body -> body : physics_step(dt) on world.step native physics_step }
drive time -> world.step keeps always
)");
        check(c.ok(), "a native arrow, declared, compiles: " + c.report());
        check(has(refused("state a { element x\n x -> x : load on a.load native read_file }"), "an arrow performs filesystem IO: native read_file"),
              "a native that reads files is refused");
        check(has(refused("state a { element x }\nstate b { element y }\nfunctor f : a -> b { object x -> y native write_log }"), "performs"),
              "and one that writes a log");
    }
}

// --- the rules of the ontology, refused ----------------------------------------------------------
void ontology() {
    std::printf("-- what the ontology refuses\n");
    const std::string timer = refused("state vale { element world { elapsed = 0.0 } }");
    check(has(timer, "state vale keeps a second clock elapsed") && has(timer, "use a Temporal drive"), "a second clock: " + timer);
    check(has(refused("state vale { time = 0.0 }"), "second clock time"), "and `time`, which is a clock's");
    check(refused("state clock : temporal { time = 0.0 }").empty(), "a Temporal keeps time: it is what it is");

    const std::string direct = refused(std::string(kCamera) + "state room { element r\n camera.fov = 30.0 }\n");
    check(has(direct, "direct write from room to camera.fov") && has(direct, "target camera.zoom through a graph-declared interface"),
          "a direct write into another state: " + direct);
    check(has(refused("camera.fov = 30"), "no statement writes into a state from outside it"), "and one at the top");
    check(has(refused("camera.fov += 1"), "no statement writes into a state from outside it"), "or an addition");

    const std::string io = refused("state a { element x }\nstate b { element y }\nfunctor f : a -> b { object x -> y { z = file(\"a\") } }");
    check(has(io, "does no IO") || has(io, "filesystem"), "file(...) in a transport: " + io);
    const std::string io2 = refused("state a { element x }\nstate b { element y }\nfunctor f : a -> b { object x -> y native read_file }");
    check(has(io2, "functor transport performs filesystem IO") && has(io2, "external effects must cross a declared device/port boundary"),
          "a transport that reads: " + io2);

    check(has(refused("state a { element x\n on_update }"), "`on_update` is not in the language"), "no on_update");
    check(has(refused("callback foo"), "a callback is not in the language") && has(refused("callback foo"), "graph-declared interfaces"),
          "no callback that mutates another state");
    check(has(refused("emit foo"), "emit + dispatch"), "no emit and dispatch as orchestration");
    check(has(refused("timer t"), "use a Temporal drive"), "no timer");
    check(has(refused("state a { mode = \"x\" }\nmode m"), "which guest has the input is focus"), "no mode enum duplicating focus");

    const std::string undriven = refused("state a { element x\n x -> x : step(dt) on a.step { x = x + vx * dt } }");
    check(has(undriven, "reads dt, but no drive keeps event a.step going"), "an arrow that reads dt is driven: " + undriven);
    check(has(refused("state a { element x }\nstate b : temporal\nstate c { element y\n y -> y : f(dt) on c.f native f }\ndrive a -> c event c.f"),
              "a drive is kept on a Temporal"),
          "a drive needs a clock");
    check(has(refused("state a\nstate a"), "state a is declared twice"), "no two states of a name");
    check(has(refused("state a { element x }\ntransition a -[e]-> nowhere"), "no state nowhere"), "a transition to nothing");
    check(has(refused("state a { element x }\nfunctor f : a -> a { object y -> x }"), "state a has no element y"), "an object that is not there");
    check(has(refused("state a { element x }\nembed a.y -> a"), "state a has no element y"), "a portal that is not there");
    check(has(refused("state s : nosuchkind"), "no kind nosuchkind"), "a kind that is not one");
    check(has(refused("state a { element x\n x -> y : f }"), "state a has no element y"), "an arrow to nothing");
    check(has(refused("state a { element x }\nstate b { element y }\nfunctor f : a -> b { object x -> y }\ntransition b -[e]-> a carry f"),
              "which starts at a"),
          "a transition carries a functor from where it leaves");
    check(has(refused("extern state ghost", [] {
                       sg::dsl::Options o;
                       o.live_states["desk"] = "state";
                       return o;
                   }()),
              "extern state ghost is not in the graph"),
          "an extern state is in the graph it is compiled into");

    // A program that fails builds nothing: no plan at all.
    check(compile("state vale { elapsed = 0.0 }").plan.steps.empty(), "an ill-formed program lowers to no plan");
    // Not an error: several problems reported together.
    const std::string many = refused("state a { elapsed = 1.0 }\nstate b { ticks = 2.0 }");
    check(has(many, "elapsed") && has(many, "ticks"), "every problem is reported, not the first");
}

// --- 2. the vertical slice --------------------------------------------------------------------------
sg::dsl::Natives scenario_natives() {
    sg::dsl::Natives n;
    // exit: playing --win--> finished. The game says it is out.
    n.arrow("exit_won", [](sg::State& s, sg::Element&, sg::Element*, const sg::Event&) { s.emit(sg::Event{sg::Key{"exit.out"}}); });
    // player: seat --sit--> seat. Sitting is said, with what the sitter asked (a field of view).
    n.arrow("player_sits", [](sg::State& s, sg::Element&, sg::Element*, const sg::Event& ev) {
        s.emit(sg::Event{sg::Key{"player.sits"}, ev.args});
    });
    return n;
}

void scenario() {
    std::printf("-- the vertical slice: built by generated C++, run by the engine\n");
    sg::StateGraph graph;
    sg::dsl::Bindings bindings;
    const sg::dsl::Natives natives = scenario_natives();
    sgen::build_scenario(graph, natives, bindings);

    check(graph.validate().empty(), "graph.validate() is empty");
    for (const std::string& p : graph.validate()) std::printf("       %s\n", p.c_str());
    check(sg::look_defects(graph).empty(), "the looks are as declared");

    sg::Engine engine(graph);
    engine.set_strict(true);
    engine.set_watch_hooks(true);
    engine.start(sg::Key{"void"});
    for (int i = 0; i < 5; ++i) engine.tick(0.1);

    check(engine.current() && engine.current()->id() == "void", "the engine starts in the void");
    check(engine.embed_open(sg::Key{"void/terminal:terminal"}) && engine.embed_open(sg::Key{"terminal/screen:exit"}),
          "Void > Terminal > Exit: both embeddings opened by their portals");
    check(engine.focused() && engine.focused()->id() == "exit", "input goes to the focused guest: exit");

    // Time on a Temporal.
    const auto& clock = static_cast<const sg::Temporal&>(graph.state("time"));
    const double t = clock.time(sg::Key{"exit"});
    check(t > 0.45 && t < 0.55, "exit's time is its line on the clock: " + std::to_string(t));
    const double x = graph.state("exit").element("runner").params.num("x");
    check(std::abs(x - 2.0 * t) < 1e-9, "the runner moved by arrows on the drive's dt: x = " + std::to_string(x));
    check(engine.frame() == 5 && graph.state("void").params().num("time", -1.0) == -1.0, "no state keeps a time of its own");

    // The way the camera is aimed: a person sits, and says so; the graph carries it.
    engine.send(sg::Key{"player"}, sg::Event{sg::Key{"player.sit"}, sg::Params{}.set("fov", 30.0)});
    engine.tick(0.1);
    check(graph.state("camera").element("lens").params.num("fov") == 30.0,
          "player.sits crossed `when` to camera.zoom, an arrow of the camera, which set the field of view");

    // Input reaches the focused guest: an event, fired through the engine.
    check(bindings.press(engine, "keyboard", "Enter"), "Enter is bound");
    check(bindings.press(engine, "keyboard", "Space"), "Space is bound");
    check(!bindings.press(engine, "keyboard", "Q"), "Q is not");
    engine.tick(0.1);
    check(graph.state("exit").params().num("x", 0.0) == 0.0, "an input adapter wrote no state of exit");
    engine.tick(0.1);
    engine.tick(0.1);
    check(engine.current() && engine.current()->id() == "room", "exit.out, said by the game in the terminal, took the engine from void to room");
    check(graph.transition(sg::Key{"void-exit.out->room"}) != nullptr && graph.transition(sg::Key{"void-exit.out->room"})->functor == "gate.ab",
          "by a transition the graph declares, carrying the seam's travel one way");

    // A seam and one way.
    check(graph.seams().size() == 2, "the rooms share seams: boundaries, identified from both sides");
    int back = 0, forth = 0;
    for (const sg::Transition& tr : graph.transitions()) {
        back += tr.from == "annex" && tr.to == "room";
        forth += tr.from == "room" && tr.to == "annex";
    }
    check(forth == 1 && back == 0, "and one transition, room to annex: no way back is declared");
    {
        const sg::Transition* crossing = graph.transition(sg::Key{"room-cross->annex"});
        check(crossing && crossing->enter.num("arrived") == 1.0 && !crossing->action,
              "`with` is the transition's `enter`, data, not a lambda: what the state entered is told");
        sg::Params told;
        if (crossing) graph.cross(*crossing, graph.state("room"), &graph.state("annex"), sg::Event{sg::Key{"cross"}}, told);
        check(told.num("arrived") == 1.0, "and the engine's own crossing tells it");
    }
    engine.fire(sg::Key{"cross"});
    engine.tick(0.1);
    check(engine.current() && engine.current()->id() == "annex", "cross took the engine through the door");
    engine.fire(sg::Key{"cross"});
    engine.tick(0.1);
    check(engine.current() && engine.current()->id() == "annex", "there is no way back: a seam does not imply one");

    // The laws see everything the DSL declared.
    const sg::LawReport rep = sg::verify(graph);
    check(rep.ok(), "sg::verify: no law is broken" + std::string(rep.ok() ? "" : "\n" + rep.str()));
    check(engine.check_graph().empty(), "the engine's watch reports nothing");
    check(engine.problems().empty(), "and never did");
    check(graph.reachable().size() == graph.size(), "every state is reachable through an interface");
    graph.keep_defaults();
}

// --- 3. one construction, three readings -------------------------------------------------------------
void readings() {
    std::printf("-- one construction, read three ways\n");
    std::string text;
    {
        FILE* f = std::fopen(SG_DSL_DIR "/scenario.sg", "rb");
        check(f != nullptr, "the scenario source is there");
        if (!f) return;
        char buf[4096];
        std::size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
        std::fclose(f);
    }
    const sg::dsl::Compiled c = compile(text);
    check(c.ok(), "the scenario compiles in process too: " + c.report());
    if (!c.ok()) return;

    sg::StateGraph built;  // by the generated C++
    sg::dsl::Bindings b1;
    const sg::dsl::Natives natives = scenario_natives();
    sgen::build_scenario(built, natives, b1);

    sg::StateGraph applied;  // by the same plan, made on a graph
    sg::dsl::Bindings b2;
    const sg::dsl::Applied a = sg::dsl::apply(c.plan, applied, natives, &b2);
    check(a.ok, "the plan applies to a fresh graph: " + a.why);

    const auto declared = sg::dsl::missing(c.plan, built);
    check(declared.empty(), "everything the plan declares is in the generated C++'s graph");
    for (const std::string& l : declared) std::printf("       missing: %s\n", l.c_str());
    const auto diff = sg::dsl::difference(sg::dsl::facts(built), sg::dsl::facts(applied));
    check(diff.empty(), "the generated C++'s graph and the applied plan's are the same graph");
    for (const std::string& l : diff) std::printf("       %s\n", l.c_str());
    check(b1.size() == b2.size() && b1.size() == 2, "and the same input table");

    // A plan is made whole or not at all: applied again, it is refused before anything changes.
    const std::size_t before = sg::dsl::facts(applied).size();
    const sg::dsl::Applied again = sg::dsl::apply(c.plan, applied, natives, &b2);
    check(!again.ok && has(again.why, "already in the graph") && sg::dsl::facts(applied).size() == before, "a second time it is refused, and changes nothing: " + again.why);
    sg::dsl::Natives none;
    sg::StateGraph third;
    check(!sg::dsl::apply(c.plan, third, none, &b2).ok && third.size() == 0, "a native nobody registered is refused before anything is made");
}

// --- the laws see what the DSL declared ------------------------------------------------------------
void laws() {
    std::printf("-- the engine's laws hold what the DSL declares\n");
    // Two like rooms and a transition that carries a viewer from one to the other
    // with no seam: the engine's seam law says there is no way back. The DSL
    // does not judge it; the laws do, on the graph it built.
    const auto c = compile(R"(
state a : spatial3d { element door : portal { x = 0.0 y = 0.0 z = 0.0 yaw = 0.0 w = 1.0 h = 2.0 } }
state b : spatial3d { element door : portal { x = 5.0 y = 0.0 z = 0.0 yaw = 0.0 w = 1.0 h = 2.0 } }
functor emerge : a -> b {
    object camera -> camera
}
transition a -[go]-> b
    carry emerge
)");
    check(c.ok(), "it compiles: the notation says what is there: " + c.report());
    sg::StateGraph g;
    sg::dsl::Bindings bindings;
    check(sg::dsl::apply(c.plan, g, {}, &bindings).ok, "and it is made");
    const sg::LawReport r = sg::verify(g);
    check(!r.ok() && has(r.str(), "one way: emerge carries a into b"), "sg::verify names it: a check that can fail");

    // Give it the seam, and the same transition is lawful: the seam's travel is what it carries.
    const auto fixed = compile(R"(
state a : spatial3d { element door : portal { x = 0.0 y = 0.0 z = 0.0 yaw = 0.0 w = 1.0 h = 2.0 } }
state b : spatial3d { element door : portal { x = 5.0 y = 0.0 z = 0.0 yaw = 0.0 w = 1.0 h = 2.0 } }
seam a.door <-> b.door
    name gate
transition a -[go]-> b
    carry gate.ab
)");
    sg::StateGraph g2;
    check(fixed.ok() && sg::dsl::apply(fixed.plan, g2, {}, &bindings).ok, "with a seam, and its travel carried: " + fixed.report());
    {
        const sg::LawReport r2 = sg::verify(g2);
        check(r2.ok(), "the laws hold" + (r2.ok() ? std::string() : "\n" + r2.str()));
    }
    // A drive with a clock that is not one, and a broken seam, are the engine's to refuse too.
    const auto skew = compile(R"(
state a : spatial3d { element door : portal { x = 0.0 y = 0.0 z = 0.0 yaw = 0.0 w = 1.0 h = 2.0 } }
state b : spatial3d { element door : portal { x = 5.0 y = 0.0 z = 0.0 yaw = 0.0 w = 3.0 h = 2.0 } }
seam a.door <-> b.door
)");
    sg::StateGraph g3;
    check(skew.ok() && sg::dsl::apply(skew.plan, g3, {}, &bindings).ok, "doorways of different sizes compile: the DSL does not measure");
    check(!sg::verify(g3).ok(), "sg::verify says the two sides do not agree");
}

// --- the rest of the notation, once each -----------------------------------------------------------
void kitchen() {
    std::printf("-- the rest of the notation, built and run\n");
    bool noted = false;
    sg::dsl::Natives natives;
    natives.transport("tenfold", [](const sg::Element& s, sg::Element& d) { d.params.set("x", s.params.num("x") * 10.0); });
    natives.edit("note_shown", [&noted](sg::StateGraph&, const sg::Event&) {
        noted = true;
        return sg::Params{}.set("noted", true);
    });
    sg::StateGraph graph;
    sg::dsl::Bindings bindings;
    sgen::build_kitchen(graph, natives, bindings);
    check(graph.validate().empty(), "it validates");
    for (const std::string& p : graph.validate()) std::printf("       %s\n", p.c_str());
    const sg::LawReport rep = sg::verify(graph);
    check(rep.ok(), "sg::verify: the lens, the composite functor and the composed arrows hold" + std::string(rep.ok() ? "" : "\n" + rep.str()));

    sg::Engine engine(graph);
    engine.set_strict(true);
    engine.start(sg::Key{"hall"});
    engine.tick(0.1);

    // Composition of arrows: twice = inc ; inc, and it is one arrow, on its own event.
    engine.fire(sg::Key{"hall.twice"});
    engine.tick(0.1);
    check(graph.state("hall").element("n").params.num("v") == 2.0, "twice = inc ; inc: it does what its parts do in order");
    engine.fire(sg::Key{"hall.inc"});
    engine.tick(0.1);
    check(graph.state("hall").element("n").params.num("v") == 3.0, "and inc is still inc");
    const sg::Morphism* twice = graph.state("hall").morphism(sg::Key{"twice"});
    check(twice && twice->parts.size() == 2 && twice->parts[0] == "inc", "the graph keeps the chain, for the laws");
    check(graph.composite_chain(sg::Key{"there_and_back"}) && graph.composite_chain(sg::Key{"there_and_back"})->size() == 2,
          "a composite functor keeps its chain too");

    // A kept functor: the view follows the model.
    graph.state("model").element("m").params.set("x", 5.0);
    engine.tick(0.1);
    check(graph.state("view").element("v").params.num("x") == 5.0, "keep: the view follows the model whenever it changes");
    check(graph.state("view").element("v").params.num("y") == 0.0, "and only what `only(x)` says crosses");

    // Transports.
    graph.functor(sg::Key{"scale"})->apply(graph.state("model"), graph.state("scaled"));
    check(graph.state("scaled").element("w").params.num("y") == 11.0, "a step it says: y = 2 * x + 1");
    graph.functor(sg::Key{"swap"})->apply(graph.state("model"), graph.state("scaled"));
    check(graph.state("scaled").element("w").params.num("z") == 5.0, "swizzle(z = x)");
    graph.functor(sg::Key{"warp"})->apply(graph.state("model"), graph.state("scaled"));
    check(graph.state("scaled").element("bent").params.num("x") == 50.0, "a native transport, by an alias");

    // push and pop.
    engine.fire(sg::Key{"open.menu"});
    engine.tick(0.1);
    check(engine.current() && engine.current()->id() == "menu" && engine.stack().size() == 2, "push: the menu opens over the hall");
    engine.fire(sg::Key{"close.menu"});
    engine.tick(0.1);
    check(engine.current() && engine.current()->id() == "hall" && engine.stack().size() == 1, "pop: and closes, the hall as it was");

    // An edit: what a state says asks the graph to change; the graph declared it, native inside.
    graph.state("hall").emit(sg::Event{sg::Key{"hall.shown"}});
    engine.tick(0.1);
    engine.tick(0.1);
    check(noted, "edit: a state said hall.shown, and the graph's edit ran");

    // The three readings agree here too.
    std::string text;
    if (FILE* f = std::fopen(SG_DSL_DIR "/kitchen.sg", "rb")) {
        char buf[4096];
        std::size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
        std::fclose(f);
    }
    const sg::dsl::Compiled c = compile(text);
    check(c.ok(), "kitchen.sg compiles in process: " + c.report());
    if (!c.ok()) return;
    sg::StateGraph fresh, applied;
    sg::dsl::Bindings b1, b2;
    sgen::build_kitchen(fresh, natives, b1);
    check(sg::dsl::apply(c.plan, applied, natives, &b2).ok, "and its plan applies to a graph");
    check(sg::dsl::missing(c.plan, fresh).empty(), "everything it declares is in the generated C++'s graph");
    const auto diff = sg::dsl::difference(sg::dsl::facts(fresh), sg::dsl::facts(applied));
    check(diff.empty(), "the generated C++'s graph and the applied plan's are one graph");
    for (const std::string& l : diff) std::printf("       %s\n", l.c_str());
}

// --- 4. a world that compiles from inside --------------------------------------------------------------
void inside() {
    std::printf("-- a world that compiles from inside\n");
    sg::StateGraph graph;
    sg::dsl::Bindings bindings;
    sg::dsl::Natives available;  // what a source compiled from inside may name
    sg::dsl::Natives natives;
    sg::dsl::register_compiler(natives, available, &bindings);
    sgen::build_world(graph, natives, bindings);

    check(graph.validate().empty(), "the world of a source and a compiler validates");
    for (const std::string& p : graph.validate()) std::printf("       %s\n", p.c_str());
    check(graph.edits().size() == 1 && graph.edits()[0].state == "compiler", "the compiler's way to the graph is an edit the graph declares");

    sg::Engine engine(graph);
    engine.set_strict(true);
    engine.start(sg::Key{"desk"});
    engine.tick(0.1);

    const std::string program = R"(
extern state desk
state lamp : spatial3d {
    element bulb : mesh { x = 1.0 y = 2.0 z = 3.0 }
}
state lamp.look : look {
    fade = 0.2
    element scene : pass { uAmbient = 0.9 }
}
wear lamp <- lamp.look
transition desk -[go.lamp]-> lamp
)";
    const std::size_t before = graph.size();
    const uint64_t revision = graph.revision();
    engine.send(sg::Key{"source"}, sg::Event{sg::Key{"source.set"}, sg::Params{}.set("text", program)});
    engine.tick(0.1);
    check(graph.state("source").element("document").params.get_or<std::string>("text", "") == program, "the text is held by an element of an ordinary state");
    check(graph.size() == before && graph.revision() == revision, "setting the text did not touch the graph's structure");

    engine.send(sg::Key{"source"}, sg::Event{sg::Key{"source.request"}});
    engine.tick(0.1);
    // The compiler asked; the edit is applied at the start of the next frame.
    engine.tick(0.1);
    engine.tick(0.1);
    const sg::State& compiler = graph.state("compiler");
    check(compiler.params().get_or<std::string>("phase", "") == "success", "the compiler settled: success (" + compiler.params().get_or<std::string>("phase", "") + ")");
    check(graph.contains(sg::Key{"lamp"}) && graph.contains(sg::Key{"lamp.look"}), "the change came: states the source declares are in the graph");
    check(compiler.element("report").params.get_or<bool>("ok", false), "the answer, compiler.change.done, was heard by the compiler");
    check(graph.embedding(sg::Key{"lamp/look:lamp.look"}) != nullptr && graph.transition(sg::Key{"desk-go.lamp->lamp"}) != nullptr,
          "an embedding and a transition, the objects the C++ API would have made");
    check(graph.has_default(sg::Key{"lamp"}), "and the new state has its start, as every state has");
    check(graph.validate().empty() && engine.problems().empty(), "the graph still validates: the engine's watch says nothing");
    engine.fire(sg::Key{"go.lamp"});
    engine.tick(0.1);
    check(engine.current() && engine.current()->id() == "lamp", "the world it made can be gone into");

    // A program that breaks a rule changes nothing.
    sg::StateGraph g2;
    sg::dsl::Bindings b2;
    sg::dsl::Natives n2;
    sg::dsl::register_compiler(n2, available, &b2);
    sgen::build_world(g2, n2, b2);
    sg::Engine e2(g2);
    e2.set_strict(true);
    e2.start(sg::Key{"desk"});
    e2.tick(0.1);
    const std::size_t states = g2.size();
    e2.send(sg::Key{"source"}, sg::Event{sg::Key{"source.set"}, sg::Params{}.set("text", "state vale { element w { elapsed = 0.0 } }")});
    e2.send(sg::Key{"source"}, sg::Event{sg::Key{"source.request"}});
    for (int i = 0; i < 4; ++i) e2.tick(0.1);
    const sg::State& c2 = g2.state("compiler");
    check(c2.params().get_or<std::string>("phase", "") == "error", "a source that breaks a rule: the compiler says error");
    check(has(c2.element("report").params.get_or<std::string>("why", ""), "second clock elapsed"), "and why");
    check(g2.size() == states && !g2.contains(sg::Key{"vale"}), "and the graph is as it was");

    // Nothing compiled from inside can bring code in, or do IO.
    e2.send(sg::Key{"source"}, sg::Event{sg::Key{"source.set"}, sg::Params{}.set("text", "state a { element x\n x -> x : f on a.f native some_native }")});
    e2.send(sg::Key{"source"}, sg::Event{sg::Key{"source.request"}});
    for (int i = 0; i < 4; ++i) e2.tick(0.1);
    check(c2.params().get_or<std::string>("phase", "") == "error" && has(c2.element("report").params.get_or<std::string>("why", ""), "no native arrow some_native"),
          "a native the host did not register cannot be named: " + c2.element("report").params.get_or<std::string>("why", ""));
    e2.send(sg::Key{"source"}, sg::Event{sg::Key{"source.set"}, sg::Params{}.set("text", "state a { element b { fs = file(\"x\") } }")});
    e2.send(sg::Key{"source"}, sg::Event{sg::Key{"source.request"}});
    for (int i = 0; i < 4; ++i) e2.tick(0.1);
    check(has(c2.element("report").params.get_or<std::string>("why", ""), "reads the filesystem"), "and it does no IO");
}

}  // namespace

int main() {
    language();
    ontology();
    scenario();
    readings();
    laws();
    kitchen();
    inside();
    std::printf("\n%s\n", failures ? "SOME TESTS FAILED" : "all tests passed");
    return failures ? 1 : 0;
}
