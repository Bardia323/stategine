// A source edited while its world runs, and what a world's sources say of it.
//
//   1. guards in the notation - `when <comparison>`, lowered to a plain, pure
//      Transition::Guard that keeps its text; the text in the facts; generated
//      code reading it back; a lambda's guard opaque, as an unnamed native is
//   2. the reload law - a source reloaded unchanged touches nothing: every
//      state's time and params bitwise as they were; reloaded changed, only
//      what changed is made again, each state kept with its own time, its
//      params, its open embeddings and its focus; a reload that cannot be made
//      whole changes nothing
//   3. saves stamped with the facts they were written against - a world whose
//      declarations moved on is named, and a save from before still loads
#include <cstdio>
#include <string>
#include <vector>

#include "sg/core/Temporal.hpp"
#include "sg/core/Text.hpp"
#include "sg/domains/Save.hpp"
#include "sg/dsl/Apply.hpp"
#include "sg/dsl/Compile.hpp"
#include "sg/dsl/Compiler.hpp"
#include "sg/dsl/Emit.hpp"
#include "sg/dsl/Facts.hpp"
#include "sg/dsl/Guard.hpp"
#include "sg/dsl/Parse.hpp"
#include "sg/sg.hpp"

namespace sgen {
void build_guarded(sg::StateGraph&, const sg::dsl::Natives&, sg::dsl::Bindings&);
}  // namespace sgen

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "[ok]  " : "[FAIL]", what.c_str());
    if (!ok) ++failures;
}

bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

bool any_has(const std::vector<std::string>& lines, const std::string& part) {
    for (const std::string& l : lines)
        if (has(l, part)) return true;
    return false;
}

const char* kShop = R"(
state shop {
    stock = 3
    element shelf { count = 2 }
}

state till

initial shop

transition shop -[buy]-> till name buy when from.stock > 0 and arg.n <= from[shelf].count
transition till -[back]-> shop name back
    when not (arg.why == "lost" or from != "till")
)";

sg::Event buy(sg::Value n) { return sg::Event{sg::Key{"buy"}, sg::Params{}.set("n", std::move(n))}; }

// --- 1. guards --------------------------------------------------------------------------------
void guards() {
    const sg::dsl::Compiled c = sg::dsl::compile_source(kShop, "shop.sg");
    check(c.ok(), "a guarded transition compiles: " + c.report());
    if (!c.ok()) return;

    const std::vector<std::string> said = sg::dsl::facts(c.plan);
    check(any_has(said, "transition buy shop -[buy]-> till kind=switch carry=- enter=[] guard=when(from.stock > 0 and arg.n <= from[shelf].count) action=none"),
          "its fact says the comparison it is");
    check(any_has(said, "guard=when(not (arg.why == \"lost\" or from != \"till\"))"), "and a guard of `not`, `or` and words, as canonical text");
    const std::string cpp = sg::dsl::emit_cpp(c.plan, "shop");
    check(has(cpp, "t.guard = sg::dsl::guard(") && has(cpp, "#include \"sg/dsl/Guard.hpp\""), "generated code reads the guard back from its text");

    sg::StateGraph g;
    sg::dsl::Bindings b;
    const sg::dsl::Applied a = sg::dsl::apply(c.plan, g, {}, &b);
    check(a.ok, "and it is made: " + a.why);
    if (!a.ok) return;
    const sg::Transition* t = g.transition(sg::Key{"buy"});
    check(t && t->guard && !t->guard.text.empty(), "the engine's guard keeps its text");
    if (!t) return;
    sg::State& shop = g.state(sg::Key{"shop"});
    check(t->guard(shop, buy(2.0)) && !t->guard(shop, buy(3.0)), "as many as are on the shelf, and no more");
    check(t->guard(shop, buy(int64_t{1})) && t->guard(shop, buy(true)), "a whole number and a flag are numbers");
    check(!t->guard(shop, sg::Event{sg::Key{"buy"}}), "an argument not there is nothing: not less than anything");
    check(!t->guard(shop, buy(std::string("2"))), "a word is not a number");
    shop.params().set("stock", 0.0);
    check(!t->guard(shop, buy(1.0)), "and nothing is sold with no stock");
    shop.params().set("stock", 3.0);
    check(sg::dsl::guard(t->guard.text).text == t->guard.text, "a guard read back from its text is that text");
    bool threw = false;
    try {
        sg::dsl::guard("stock > 0");
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "a text that is not a guard is refused");

    // The engine takes it as it takes any guard.
    sg::Engine e(g);
    e.set_strict(true);
    e.start();
    e.fire(buy(5.0));
    e.tick(0.0);
    check(e.current() && e.current()->id() == sg::Key{"shop"}, "five are not on the shelf: still in the shop");
    e.fire(buy(1.0));
    e.tick(0.0);
    check(e.current() && e.current()->id() == sg::Key{"till"}, "one is: at the till");
    e.fire(sg::Event{sg::Key{"back"}, sg::Params{}.set("why", std::string("lost"))});
    e.tick(0.0);
    check(e.current() && e.current()->id() == sg::Key{"till"}, "the way back lost: still at the till");
    e.fire(sg::Event{sg::Key{"back"}, sg::Params{}.set("why", std::string("done"))});
    e.tick(0.0);
    check(e.current() && e.current()->id() == sg::Key{"shop"}, "found: back in the shop");

    // The generated C++ is the same graph, guard text and all.
    sg::StateGraph built;
    sg::dsl::Bindings b2;
    sgen::build_guarded(built, {}, b2);
    const auto absent = sg::dsl::missing(c.plan, built);
    check(absent.empty() && sg::dsl::unverified(c.plan, built).empty(), "the generated C++'s guards are the source's");
    for (const std::string& l : absent) std::printf("       missing: %s\n", l.c_str());

    // A guard written in C++ says nothing of itself: opaque, as an unnamed native is.
    sg::StateGraph hand;
    hand.add<sg::State>(sg::Key{"shop"}).params().set("stock", 3.0);
    hand.state(sg::Key{"shop"}).add_element(sg::Key{"shelf"}, sg::Key{"object"}).params.set("count", 2.0);
    hand.add<sg::State>(sg::Key{"till"});
    hand.set_initial(sg::Key{"shop"});
    for (const char* name : {"buy", "back"}) {
        sg::Transition tr;
        tr.name = sg::Key{name};
        tr.from = sg::Key{std::string(name) == "buy" ? "shop" : "till"};
        tr.to = sg::Key{std::string(name) == "buy" ? "till" : "shop"};
        tr.trigger = sg::Key{name};
        tr.guard = [](const sg::State&, const sg::Event&) { return true; };
        hand.connect(std::move(tr));
    }
    check(any_has(sg::dsl::facts(hand), "guard=opaque"), "a lambda's guard is opaque in the facts");
    const auto unproven = sg::dsl::unverified(c.plan, hand);
    check(unproven.size() == 2 && any_has(unproven, "guard=when("), "beside a source, a lambda's guard is unverified, not missing");
    check(!any_has(sg::dsl::missing(c.plan, hand), "transition "), "and no transition is missing");

    // What the guard reads is the state it leaves and the event: nothing else is a word of it.
    const auto bare = sg::dsl::compile_source("state a\nstate b\ninitial a\ntransition a -[go]-> b when stock > 0\n");
    check(!bare.ok() && has(bare.report(), "neither"), "a bare word is neither the state left nor the event: " + bare.report());
    const auto stray = sg::dsl::compile_source("state a\nstate b\ninitial a\ntransition a -[go]-> b when from[shelf].n > 0\n");
    check(!stray.ok() && has(stray.report(), "no element shelf"), "an element the state left does not have is named");
    // A `when` of its own, on the next line, is still an event's mapping.
    const sg::dsl::Parsed p = sg::dsl::parse("transition a -[go]-> b\nwhen a.x b.y\n");
    check(p.ok() && p.program.transitions.size() == 1 && p.program.transitions[0].guard.kind == sg::dsl::GuardAst::Kind::None &&
              p.program.whens.size() == 1,
          "a `when` on a line of its own is not a guard");
}

// --- 2. the reload law -----------------------------------------------------------------------
const char* kRoom = R"(
state clock : temporal
initial room

state room {
    speed = 2
    element ball { x = 0 }
    element tv : portal { w = 1 h = 1 open = true }
    ball -> ball : roll(dt) on room.tick { x = x + speed * dt }
}

state show {
    element screen { lit = 1 }
}

drive clock -> room event room.tick keeps always
embed room.tv -> show name room.show follows true
transition room -[go]-> show name go when from.speed > 1
transition show -[back]-> room name back
)";

std::string edited(std::string text, const std::string& from, const std::string& to) {
    const std::size_t at = text.find(from);
    if (at != std::string::npos) text.replace(at, from.size(), to);
    return text;
}

// Every state, as it is: its text, the stamp of its params and of each element's.
struct Seen {
    std::vector<std::string> texts;
    std::vector<uint64_t> stamps;
};
Seen seen(const sg::StateGraph& g) {
    Seen s;
    for (sg::Key id : g.ids()) {
        const sg::State& st = g.state(id);
        s.texts.push_back(sg::to_text(st));
        s.stamps.push_back(st.params().stamp());
        for (const sg::Element& e : st.elements()) s.stamps.push_back(e.params.stamp());
    }
    return s;
}

double line_time(const sg::StateGraph& g) { return static_cast<const sg::Temporal&>(g.state(sg::Key{"clock"})).time(sg::Key{"room"}); }

sg::dsl::Compiled live(const std::string& text, const sg::StateGraph& g) {
    sg::dsl::Options o = sg::dsl::options_for(g);
    o.conform = true;
    return sg::dsl::compile_source(text, "room.sg", o);
}

void reload_law() {
    const sg::dsl::Compiled v1 = sg::dsl::compile_source(kRoom, "room.sg");
    check(v1.ok(), "the room compiles: " + v1.report());
    if (!v1.ok()) return;
    sg::StateGraph g;
    sg::dsl::Bindings b;
    const sg::dsl::Applied a = sg::dsl::apply(v1.plan, g, {}, &b);
    check(a.ok && g.validate().empty(), "and is made: " + a.why);
    if (!a.ok) return;
    sg::Engine e(g);
    e.set_strict(true);
    e.start();
    for (int i = 0; i < 5; ++i) e.tick(0.1);
    const double x0 = g.state(sg::Key{"room"}).element(sg::Key{"ball"}).params.num("x");
    check(x0 > 0.5 && x0 < 1.5, "it runs: the ball rolls on the room's own line");
    const bool open = e.embed_open(sg::Key{"room.show"});
    const sg::State* focus = e.focused();
    check(open, "the show is open in the room's portal");

    // The law: an unchanged source, reloaded, touches nothing.
    const Seen before = seen(g);
    const double t0 = line_time(g);
    const uint64_t rev = g.revision();
    const sg::dsl::Compiled same = live(kRoom, g);
    check(same.ok(), "the same source compiles against the running graph: " + same.report());
    const sg::dsl::Reloaded r0 = sg::dsl::reload(v1.plan, same.plan, g, {}, &b);
    const Seen after = seen(g);
    check(r0.ok && r0.changed.empty(), "reloaded unchanged, nothing is made again: " + r0.why);
    check(after.texts == before.texts && after.stamps == before.stamps, "every state's params bitwise as they were, and their stamps");
    check(line_time(g) == t0 && g.revision() == rev, "every state's time as it was, and the graph's revision");

    // A changed source: only what changed is made again.
    const std::string v2_text = edited(edited(edited(kRoom, "x = x + speed * dt", "x = x + 2 * speed * dt"), "speed = 2", "speed = 9"),
                                       "element ball { x = 0 }", "element ball { x = 0 }\n    element ball2 { y = 5 }");
    const sg::dsl::Compiled v2 = live(v2_text, g);
    check(v2.ok(), "the edited source compiles: " + v2.report());
    const sg::State* room_was = &g.state(sg::Key{"room"});
    const sg::dsl::Reloaded r1 = sg::dsl::reload(v1.plan, v2.plan, g, {}, &b);
    check(r1.ok && r1.changed == std::vector<std::string>{"state room"}, "reloaded edited, the room alone is made again: " + r1.why);
    const sg::State& room = g.state(sg::Key{"room"});
    check(&room == room_was, "the same state, kept in place");
    check(room.params().num("speed") == 2.0, "its params carried: the speed it had, not the source's new start");
    check(room.element(sg::Key{"ball"}).params.num("x") == x0, "the ball where it was");
    check(room.find(sg::Key{"ball2"}) && room.element(sg::Key{"ball2"}).params.num("y") == 5.0, "what the source newly says is added");
    check(line_time(g) == t0, "its time where it was");
    check(e.embed_open(sg::Key{"room.show"}) == open && e.focused() == focus, "its embedding still open, its focus where it was");
    check(g.validate().empty(), "the graph validates");
    e.tick(0.1);
    const double x1 = g.state(sg::Key{"room"}).element(sg::Key{"ball"}).params.num("x");
    check(x1 - x0 > 0.39 && x1 - x0 < 0.41, "the arrow is the new one: twice as fast, at the speed carried");
    check(line_time(g) > t0 + 0.09 && line_time(g) < t0 + 0.11, "and its time goes on from where it was");
    const sg::LawReport laws = sg::verify(g);
    check(laws.ok(), "the reloaded world keeps the laws" + std::string(laws.ok() ? "" : "\n" + laws.str()));

    // A changed guard: the transition alone, declared again.
    const std::string v3_text = edited(v2_text, "when from.speed > 1", "when from.speed > 5");
    const sg::dsl::Compiled v3 = live(v3_text, g);
    const sg::dsl::Reloaded r2 = sg::dsl::reload(v2.plan, v3.plan, g, {}, &b);
    check(r2.ok && r2.changed == std::vector<std::string>{"transition go"}, "a guard edited: the transition alone is made again: " + r2.why);
    check(any_has(sg::dsl::facts(g, sg::dsl::Scope::Relations), "guard=when(from.speed > 5)"), "and the facts say the new comparison");
    e.fire(sg::Key{"go"});
    e.tick(0.0);
    check(e.current() && e.current()->id() == sg::Key{"room"}, "a speed of 2 is not more than 5: still in the room");

    // All or nothing.
    const std::vector<std::string> facts_before = sg::dsl::facts(g);
    const Seen held = seen(g);
    const std::string v4_text = edited(v3_text, "on room.tick { x = x + 2 * speed * dt }", "on room.tick { x = x + 2 * speed * dt }\n    ball -> ball : spin on room.spin native nobody");
    const sg::dsl::Compiled v4 = live(v4_text, g);
    const sg::dsl::Reloaded r3 = sg::dsl::reload(v3.plan, v4.plan, g, {}, &b);
    check(!r3.ok && has(r3.why, "nobody"), "a native nobody registered is refused: " + r3.why);
    const std::string v5_text = edited(v3_text, "state show {", "state show : spatial2d {");
    const sg::dsl::Compiled v5 = live(v5_text, g);
    const sg::dsl::Reloaded r4 = sg::dsl::reload(v3.plan, v5.plan, g, {}, &b);
    check(!r4.ok && has(r4.why, "kind"), "a state of another kind is another state, refused: " + r4.why);
    const std::string v6_text = edited(v3_text, "transition show -[back]-> room name back", "transition show -[back]-> nowhere name back");
    check(!live(v6_text, g).ok(), "a transition to nowhere does not compile");
    check(sg::dsl::facts(g) == facts_before && seen(g).texts == held.texts, "and nothing was changed by either");

    // The in-world edit, as a compiler asks it.
    sg::dsl::Natives natives;
    sg::dsl::Natives available;
    sg::dsl::register_compiler(natives, available, &b);
    const sg::Edit::Apply edit = natives.edit("compile_reload");
    const sg::Params answer = edit(g, sg::Event{sg::Key{"compiler.reload"}, sg::Params{}.set("before", v3_text).set("text", v3_text)});
    check(answer.get_or<bool>("ok", false) && answer.get_or<std::string>("changed", "x").empty(), "the compiler's reload edit, asked the same source, changes nothing");
}

// --- 3. saves stamped with their facts ---------------------------------------------------------
void stamped_saves() {
    sg::StateGraph g;
    sg::State& base = g.add<sg::State>(sg::Key{"base"});
    base.add_element(sg::Key{"gold"}, sg::Key{"purse"}).params.set("n", 7.0);
    base.loop(sg::Key{"count"}, sg::Key{"gold"}, sg::Key{"base.count"}, [](sg::State&, sg::Element&, sg::Element*, const sg::Event&) {});
    sg::Save& save = g.add<sg::Save>(sg::Key{"progress"});
    save.keep(sg::Key{"base"});
    g.set_initial(sg::Key{"base"});
    g.keep_defaults();

    const auto ask = [&](const char* what, sg::Params p) {
        p.set("save", std::string("progress")).set("slot", std::string("1"));
        return sg::Event{sg::Key{std::string("progress.") + what}, std::move(p)};
    };
    const sg::Params saved = sg::Save::restrict_kept(g, ask("restrict", {}));
    const std::string text = saved.get_or<std::string>("text", "");
    check(saved.get_or<bool>("ok", false) && has(text, "\nfacts ") && has(text, "\nfact base element\\sgold "),
          "a save is stamped: a digest of the kept states' facts, and each declaration's");
    sg::Save::Text read;
    check(sg::Save::read_text(text, read) && !read.facts.empty() && read.facts == sg::Save::digest_of(read.stamps) &&
              read.states.size() == 1,
          "and read back, the stamps are what the digest was made of");

    const sg::Params same = sg::Save::extend_kept(g, ask("extend", sg::Params{}.set("text", text)));
    check(same.get_or<bool>("ok", false) && same.get_or<bool>("stamped", false) && same.get_or<std::string>("differs", "x").empty(),
          "loaded against the world it was written in, nothing differs");

    // The world's declarations move on: a purse more, an arrow made otherwise.
    g.state(sg::Key{"base"}).add_element(sg::Key{"silver"}, sg::Key{"purse"});
    const sg::Params moved = sg::Save::extend_kept(g, ask("extend", sg::Params{}.set("text", text)));
    const std::string differs = moved.get_or<std::string>("differs", "");
    check(moved.get_or<bool>("ok", false) && has(differs, "base: element silver is declared since"),
          "loaded into a world declared otherwise, it names what differs: " + differs);

    // A save from before stamps: it loads as it did, and says it is not stamped.
    std::string old;
    for (std::size_t at = 0; at < text.size();) {
        std::size_t nl = text.find('\n', at);
        if (nl == std::string::npos) nl = text.size();
        const std::string line = text.substr(at, nl - at);
        if (line.rfind("fact", 0) != 0) old += line + "\n";
        at = nl + 1;
    }
    check(!has(old, "fact"), "(a save written before stamps)");
    const sg::Params loaded = sg::Save::extend_kept(g, ask("extend", sg::Params{}.set("text", old)));
    check(loaded.get_or<bool>("ok", false) && !loaded.get_or<bool>("stamped", true) && loaded.get_or<std::string>("differs", "x").empty() &&
              g.state(sg::Key{"base"}).element(sg::Key{"gold"}).params.num("n") == 7.0,
          "an old save still loads, and names nothing: " + loaded.get_or<std::string>("why", ""));
}

}  // namespace

int main() {
    guards();
    reload_law();
    stamped_saves();
    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
