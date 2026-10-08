// Stategine - the laws of the graph itself: what a transport and an arrow say
// they read and write (footprints), what a carry sees a frame late (lags),
// what goes when something is taken away (cleanup), keys that name another
// thing (references), embeddings that nest round or crowd one portal, and the
// lookups the engine runs by. Each shown holding and, where it can, breaking.
#include <cstdio>
#include <string>
#include <vector>

#include "sg/core/Engine.hpp"
#include "sg/core/Laws.hpp"
#include "sg/core/Temporal.hpp"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "[ok]  " : "[FAIL]", what.c_str());
    if (!ok) ++failures;
}

bool mentions(const std::vector<std::string>& lines, const std::string& part) {
    for (const std::string& l : lines)
        if (l.find(part) != std::string::npos) return true;
    return false;
}

bool any_law(const std::vector<sg::Violation>& vs, const std::string& law, const std::string& part) {
    for (const sg::Violation& v : vs)
        if (v.law == law && !v.refused && (v.lhs.find(part) != std::string::npos || v.detail.find(part) != std::string::npos)) return true;
    return false;
}

// --- footprints on transports -------------------------------------------------------
// A transport that reads its state's `hour` - through the state, as a look's
// carry reads its world's hour - and says so; one that reads it and does not;
// one that writes what it does not say.
void test_transport_footprints() {
    sg::StateGraph g;
    auto& a = g.add<sg::State>("a");
    auto& b = g.add<sg::State>("b");
    a.params().set("hour", 9.0).set("mood", 1.0);
    a.add_element("sun", "light").params.set("x", 1.0);
    a.add_element("other", "thing").params.set("v", 2.0);
    b.add_element("sky", "look");
    const sg::State* self = &a;
    sg::Functor f("a.sky", "a", "b");
    f.on_object("sun", "sky", [self](const sg::Element& sun, sg::Element& sky) {
        sky.params.set("light", self->params().num("hour") * sun.params.num("x"));
    });
    sg::Footprint fp;
    fp.params = {"hour"};
    fp.writes = {"light"};
    fp.writes_said = true;
    f.footprint("sun", fp);
    g.add_functor(std::move(f));
    g.connect("a", "go", "b", sg::Key{"a.sky"});
    check(sg::laws::footprints(g).empty(), "a transport that reads only what it says holds its footprint");

    // Says it reads nothing of its state, but reads the hour.
    sg::Footprint none;
    g.functor("a.sky")->footprint("sun", none);
    check(any_law(sg::laws::footprints(g), "footprint", "reads param hour"), "one that reads what it does not say is caught");

    // Reads what it says, writes what it does not.
    sg::Footprint narrow = fp;
    narrow.writes = {"shade"};
    g.functor("a.sky")->footprint("sun", narrow);
    check(any_law(sg::laws::footprints(g), "footprint", "writes light"), "one that writes outside what it says is caught");

    // A transport with no footprint says nothing, and nothing is checked.
    sg::Functor plain("a.plain", "a", "b");
    plain.on_object("other", "sky", [self](const sg::Element&, sg::Element& sky) { sky.params.set("m", self->params().num("mood")); });
    g.add_functor(std::move(plain));
    g.functor("a.sky")->footprint("sun", fp);
    check(sg::laws::footprints(g).empty(), "no footprint, nothing checked: as before");
}

// --- footprints on arrows -------------------------------------------------------------
void test_arrow_footprints() {
    sg::StateGraph g;
    auto& s = g.add<sg::State>("s");
    s.params().set("speed", 2.0).set("colour", 3.0);
    s.add_element("x", "thing").params.set("v", 0.0);
    s.add_element("y", "thing").params.set("w", 5.0);
    s.loop("go", "x", "go", [](sg::State& st, sg::Element& x, sg::Element*, const sg::Event& ev) {
        x.params.set("v", x.params.num("v") + st.params().num("speed") * ev.args.num("dt", 1.0));
    });
    sg::Footprint fp;
    fp.params = {"speed"};
    fp.writes = {"v"};
    fp.writes_said = true;
    s.with_footprint("go", fp);
    check(sg::laws::footprints(g).empty(), "an arrow that reads and writes what it says holds its footprint");

    sg::Footprint blind;
    blind.writes = {"v"};
    blind.writes_said = true;
    s.with_footprint("go", blind);
    check(any_law(sg::laws::footprints(g), "footprint", "reads param speed"), "an arrow reading a param it does not say is caught");

    s.loop("peek", "x", "peek", [](sg::State& st, sg::Element& x, sg::Element*, const sg::Event&) {
        x.params.set("v", st.element("y").params.num("w"));
    });
    sg::Footprint own;
    own.writes = {"v"};
    own.writes_said = true;
    s.with_footprint("go", fp);
    s.with_footprint("peek", own);
    check(any_law(sg::laws::footprints(g), "footprint", "reads y.w"), "an arrow reading another element it does not say is caught");
    own.elements = {"y"};
    s.with_footprint("peek", own);
    check(sg::laws::footprints(g).empty(), "said, it holds");
    check(s.element("x").params.num("v") == 0.0, "and checking it changed nothing");
}

// --- a carry that runs every frame runs only on change, once its reads are said -------
int carried = 0;

void test_continuous_on_change() {
    for (const bool said : {false, true}) {
        sg::StateGraph g;
        auto& h = g.add<sg::State>("h");
        g.add<sg::State>("v").add_element("b", "thing");
        h.params().set("hour", 1.0);
        h.add_element("a", "thing").params.set("x", 1.0);
        h.add_element("p", "portal");
        h.add_element("elsewhere", "thing").params.set("q", 0.0);
        const sg::State* self = &h;
        sg::Functor in("h.in", "h", "v");
        in.on_object("a", "b", [self](const sg::Element& a, sg::Element& b) {
            ++carried;
            b.params.set("y", a.params.num("x") + self->params().num("hour"));
        });
        if (said) {
            sg::Footprint fp;
            fp.params = {"hour"};
            in.footprint("a", fp);
        }
        g.add_functor(std::move(in));
        const sg::Key em = g.embed("h.v", "h", "p", "v", "h.in", sg::Key{}, sg::EmbedSync::View).name;
        g.set_propagation(em, sg::Propagation::Continuous);
        g.set_initial("h");
        sg::Engine e(g);
        e.start();
        e.open_embed(em);
        e.run_fixed(0.1, 2);
        carried = 0;
        e.run_fixed(0.1, 3);
        const int idle = carried;
        h.element("elsewhere").params.set("q", 1.0);
        e.run_fixed(0.1, 1);
        const int other = carried - idle;
        h.params().set("hour", 2.0);
        e.run_fixed(0.1, 1);
        const int moved = carried - idle - other;
        if (said) {
            check(idle == 0 && other == 0 && moved == 1, "a Continuous carry with its reads said runs only when one of them moves");
            check(g.state("v").element("b").params.num("y") == 3.0, "and carries what it would have");
        } else {
            check(idle == 3, "unsaid, a Continuous carry runs every frame, as before");
        }
    }
}

// --- a frame late -----------------------------------------------------------------------
void test_lags() {
    sg::StateGraph g;
    auto& s = g.add<sg::State>("s");
    auto& t = g.add<sg::State>("t");
    auto& clock = g.add<sg::Temporal>("clock");
    s.add_element("x", "thing");
    t.add_element("y", "thing");
    s.loop("move", "x", "s.tick", [](sg::State&, sg::Element& x, sg::Element*, const sg::Event& ev) {
        x.params.set("v", x.params.num("v") + ev.args.num(sg::keys::dt));
    });
    sg::Footprint writes;
    writes.writes = {"v"};
    writes.writes_said = true;
    s.with_footprint("move", writes);
    sg::drive(g, clock, "s", "s.tick");
    sg::Functor k("s.t", "s", "t");
    k.on_object("x", "y");
    k.footprint("x", sg::Footprint{});
    g.add_functor(std::move(k));
    g.keep("s.t");
    check(!sg::laws::lags(g).empty(), "a kept carry reading what a driven arrow writes later in the frame is a frame late, and said");
    sg::Footprint meant;
    meant.lags = true;
    g.functor("s.t")->footprint("x", meant);
    check(sg::laws::lags(g).empty(), "unless it says it lags");
    (void)t;
}

// --- taking things away ------------------------------------------------------------------
void test_cleanup() {
    sg::StateGraph g;
    auto& a = g.add<sg::State>("a");
    auto& b = g.add<sg::State>("b");
    auto& c = g.add<sg::State>("c");
    a.add_element("p", "portal");
    a.add_element("door", "portal");
    b.add_element("in", "thing");
    c.add_element("door", "portal");
    a.add_element("x", "thing");
    g.add_functor("a.b", "a", "b").on_object("x", "in");
    sg::Embedding e;
    e.name = "a.p";
    e.host = "a";
    e.portal = "p";
    e.guest = "b";
    e.in = "a.b";
    e.sync = sg::EmbedSync::View;
    e.cleanup = sg::Cleanup::Cascade;
    g.embed(e);
    sg::Seam sm;
    sm.name = "a|c";
    sm.a = "a";
    sm.b = "c";
    sm.boundary_a = {"door"};
    sm.boundary_b = {"door"};
    g.add_seam(sm);

    const sg::Removal own = g.removal_of_embedding("a.p");
    check(own.ok() && own.embeddings.size() == 1 && own.functors.size() == 1, "an embedding that cascades takes its functor with it");
    g.remove(own);
    check(!g.embedding("a.p") && !g.functor("a.b"), "and both are gone, as one change");

    const sg::Removal door = g.removal("a", "door");
    check(door.ok() && door.seams.size() == 1, "taking a doorway away takes the seam it is a boundary of");
    g.remove(door);
    check(!g.seam("a|c") && !a.find("door"), "the seam and the doorway are gone");

    sg::Transition t;
    t.from = "a";
    t.to = "c";
    t.trigger = "go";
    t.cleanup = sg::Cleanup::Refuse;
    g.connect(t);
    const sg::Removal no = g.removal("c");
    check(!no.ok(), "a relation that refuses refuses the whole removal");
    g.set_initial("a");
    sg::Engine en(g);
    en.start();
    check(!en.remove_now(no).empty() && g.contains("c"), "and nothing goes");
    check(!g.removal("a").ok(), "the initial state is never taken away");

    // Asked for in a frame, done at the start of the next, what is open
    // closed through the engine first.
    g.add_functor("a.b2", "a", "b").on_object("x", "in");
    const sg::Key em = g.embed("a.p2", "a", "p", "b", "a.b2", sg::Key{}, sg::EmbedSync::View).name;
    en.open_embed(em);
    en.remove(g.removal("b"));
    check(g.contains("b"), "a removal asked for waits for the next frame");
    en.tick(0.1);
    check(!g.contains("b") && !g.embedding(em) && !g.functor("a.b2"), "then the state goes, with what touched it");
    check(g.validate().empty(), "and the graph left is whole");
    (void)b;
    (void)c;
}

// --- keys that name another thing ---------------------------------------------------------
void test_references() {
    sg::StateGraph g;
    auto& s = g.add<sg::State>("s");
    s.add_element("a", "thing").params.set("parent", std::string("b"));
    s.add_element("b", "thing").params.set("parent", std::string("c"));
    s.add_element("c", "thing");
    s.refers("parent");
    check(!mentions(g.validate(), "parent"), "a parent that is there, with no ring, is sound");
    check(s.referrers("parent", "b") == std::vector<sg::Key>{"a"}, "who names b is looked up");
    s.element("c").params.set("parent", std::string("b"));
    check(s.referrers("parent", "b").size() == 2, "the lookup follows what the elements say");
    s.element("c").params.set("parent", std::string("a"));
    check(mentions(g.validate(), "goes round"), "a ring of parents is named");
    s.element("c").params.set("parent", std::string("nobody"));
    check(mentions(g.validate(), "which is not there"), "a parent that is not there is named");
    s.element("c").params.set("parent", std::string("a b"));
    check(mentions(g.validate(), "more than one"), "a key that names two things is named");
    s.element("c").params.set("parent", std::string());
    check(!mentions(g.validate(), "parent"), "an empty one names nothing, soundly");
    sg::Reference wears;
    wears.to_state = true;
    s.refers("wears", wears);
    s.element("a").params.set("wears", std::string("paint"));
    check(mentions(g.validate(), "names paint"), "one that names a state is held to the graph's states");
}

// --- embeddings that nest round, and crowd a portal ----------------------------------------
void test_nesting() {
    sg::StateGraph g;
    auto& h = g.add<sg::State>("h");
    auto& k = g.add<sg::State>("k");
    h.add_element("p", "portal");
    k.add_element("q", "portal");
    g.embed("h.p", "h", "p", "k", sg::Key{}, sg::Key{});
    g.embed("k.q", "k", "q", "h", sg::Key{}, sg::Key{});
    check(mentions(g.validate(), "nest round"), "a ring of embeddings that does not say it recurses is named");
    g.drop_embedding("k.q");
    sg::Embedding again;
    again.name = "k.q";
    again.host = "k";
    again.portal = "q";
    again.guest = "h";
    again.recurses = true;
    g.embed(again);
    check(!mentions(g.validate(), "nest round"), "one that says so is meant");

    sg::StateGraph g2;
    auto& host = g2.add<sg::State>("host");
    g2.add<sg::State>("one");
    g2.add<sg::State>("two");
    host.add_element("screen", "portal");
    g2.embed("s1", "host", "screen", "one", sg::Key{}, sg::Key{});
    g2.embed("s2", "host", "screen", "two", sg::Key{}, sg::Key{});
    check(g2.embeddings_at("host", "screen").size() == 2, "the embeddings in a portal are looked up");
    sg::Engine e(g2);
    e.start();
    e.open_embed("s1");
    e.open_embed("s2");
    check(mentions(g2.validate(), "open at once"), "two guests open in one portal are named");
    host.element("screen").params.set(sg::shared_key(), 1.0);
    check(!mentions(g2.validate(), "open at once"), "unless the portal says it is shared");
    (void)k;
}

// --- the lookups -----------------------------------------------------------------------------
void test_indexes() {
    sg::StateGraph g;
    auto& a = g.add<sg::State>("a");
    g.add<sg::State>("b");
    a.says("a.ask");
    g.edit("a", "a.ask", [](sg::StateGraph&, const sg::Event&) { return sg::Params{}; });
    check(g.edits_for("a", "a.ask").size() == 1 && g.edit_named("a:a.ask"), "edits are looked up by who asks and what");
    g.add_functor("ab", "a", "b");
    g.add_functor("ab2", "a", "b");
    check(g.functors_between("a", "b").size() == 2 && g.functors_from("a").size() == 2, "functors by their ends");
    sg::Seam s;
    s.name = "ab.seam";
    s.a = "a";
    s.b = "b";
    g.add_seam(s);
    check(g.seams_of("a").size() == 1 && g.seams_of("b").size() == 1, "seams by either side");
    g.drop_seam("ab.seam");
    check(g.seams_of("a").empty(), "made again when the graph is rewired");
    g.connect("a", "go", "b", sg::Key{"ab"});
    check(g.transitions_carrying("ab").size() == 1, "transitions by what they carry");
}

}  // namespace

int main() {
    test_transport_footprints();
    test_arrow_footprints();
    test_continuous_on_change();
    test_lags();
    test_cleanup();
    test_references();
    test_nesting();
    test_indexes();
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "all good", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
