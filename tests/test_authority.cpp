// Stategine - who may change what, and that the graph notices.
//
// The world changes only through what the engine models: a state's own
// update, its arrows, functors, embeddings - and, for what the graph is made
// of, the graph's own operations, each counted. Whoever only watches (the
// engine's `current()`, a const graph) gets const, and the compiler holds it
// to that: the first half of this file is static_asserts, and costs nothing.
// The second half runs the ways the world *may* change and checks each still
// works - an embedding acting on a state other than its host above all - and
// that changing values never counts as changing structure.
#include <cstdio>
#include <string>
#include <type_traits>
#include <utility>

#include "sg/core/Engine.hpp"
#include "sg/core/Laws.hpp"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "[ok]  " : "[FAIL]", what.c_str());
    if (!ok) ++failures;
}

template <class T>
using no_ref = std::remove_reference_t<T>;
template <class T>
constexpr bool is_const_view = std::is_const<std::remove_pointer_t<no_ref<T>>>::value;

// --- watching is read-only, by type --------------------------------------------
using sg::Engine;
using sg::Key;
using sg::StateGraph;
static_assert(is_const_view<decltype(std::declval<Engine&>().current())>, "the engine shows its state const");
static_assert(is_const_view<decltype(std::declval<Engine&>().focused())>, "and the focused guest");
static_assert(is_const_view<decltype(std::declval<Engine&>().graph())>, "and its graph");
static_assert(is_const_view<decltype(std::declval<const StateGraph&>().find(Key{}))>, "a const graph's states are const");
static_assert(is_const_view<decltype(std::declval<const StateGraph&>().state(Key{}))>, "all of them");
static_assert(is_const_view<decltype(std::declval<const StateGraph&>().functor(Key{}))>, "and its functors");
static_assert(is_const_view<decltype(std::declval<const sg::State&>().find(Key{}))>, "a const state's elements are const");
static_assert(is_const_view<decltype(std::declval<const sg::State&>().params())>, "and its params");
// What joins states is declared, then read: nobody rewrites an embedding, a
// transition, a seam or an arrow in place behind the graph's back.
static_assert(is_const_view<decltype(std::declval<StateGraph&>().embedding(Key{}))>, "an embedding is read, even from the owner");
static_assert(is_const_view<decltype(std::declval<StateGraph&>().embed(Key{}, Key{}, Key{}))>, "embed hands back a view");
static_assert(is_const_view<decltype(std::declval<StateGraph&>().connect(Key{}, Key{}, Key{}))>, "and connect");
static_assert(is_const_view<decltype(std::declval<StateGraph&>().add_seam(sg::Seam{}))>, "and add_seam");
static_assert(is_const_view<decltype(std::declval<sg::State&>().loop(Key{}, Key{}, Key{}, nullptr))>, "and an arrow, once added");
static_assert(is_const_view<decltype(std::declval<StateGraph&>().embeddings())>, "the list of embeddings too");
// Whoever holds the graph itself may build and rewrite it.
static_assert(!is_const_view<decltype(std::declval<StateGraph&>().find(Key{}))>, "the graph's owner holds its states");
static_assert(!is_const_view<decltype(std::declval<StateGraph&>().functor(Key{}))>, "and its functors");
// And a state's arrows are handed the state itself, to act on.
static_assert(std::is_same<sg::Morphism::Handler,
                           std::function<void(sg::State&, sg::Element&, sg::Element*, const sg::Event&)>>::value,
              "an arrow acts on its own state");

// --- the ways the world changes, still working -------------------------------------
struct Clock : sg::State {
    explicit Clock(Key id) : sg::State(id) { add_element("hand", "hand"); }
    void on_update(const sg::Tick& t) override {
        sg::Element& h = element("hand");
        h.params.set("t", h.params.num("t") + t.dt);
    }
};

void test_the_legitimate_ways() {
    StateGraph g;
    auto& clock = g.add<Clock>("clock");
    auto& other = g.add<sg::State>("other");
    clock.add_element("bell", "bell");
    clock.loop("ring", "bell", "ring", [](sg::State&, sg::Element& b, sg::Element*, const sg::Event&) {
        b.params.set("rung", b.params.num("rung") + 1);
    });
    g.add_functor("copy", "clock", "other").on_object("hand", "hand");
    g.connect("clock", "go", "other", "copy");
    Engine e(g);
    e.start("clock");
    e.tick(0.5);
    check(clock.element("hand").params.num("t") == 0.5, "a state's own update changes its own data");
    e.fire("ring");
    e.tick(0.5);
    check(clock.element("bell").params.num("rung") == 1, "an arrow changes what it acts on");
    e.fire("go");
    e.tick(0.0);
    check(e.current() == &other && other.find("hand") && other.element("hand").params.num("t") == 1.0,
          "a functor carries data into the state it lands in");
}

// Mounted in one state, acting on another: the host is where the guest hangs,
// the subject is what it is about. Containment is not authority.
void test_host_is_not_subject(sg::EmbedSync sync, const char* name) {
    StateGraph g;
    auto& host = g.add<sg::State>("host");
    auto& subject = g.add<sg::State>("subject");
    auto& guest = g.add<sg::State>("guest");
    host.add_element("panel", "portal");
    host.add_element("knob", "thing").params.set("v", 1.0);  // the host's own: must not move
    subject.add_element("knob", "thing").params.set("v", 5.0);
    guest.add_element("dial", "dial");
    guest.loop("turn", "dial", "turn", [](sg::State&, sg::Element& d, sg::Element*, const sg::Event& ev) {
        d.params.set("v", ev.args.num("to"));
    });
    const auto v_only = sg::transport::only({"v"});
    g.add_functor("in", "subject", "guest").on_object("knob", "dial", v_only);
    if (sync != sg::EmbedSync::View) g.add_functor("out", "guest", "subject").on_object("dial", "knob", v_only);
    g.embed("mount", "host", "panel", "guest", "in", sync == sg::EmbedSync::View ? Key{} : Key{"out"}, sync, "subject");
    g.connect("host", "nowhere", "subject");  // so the subject is reached
    check(sg::verify(g).structure.empty(), std::string(name) + ": the graph is whole");

    Engine e(g);
    e.start("host");
    e.open_embed("mount");
    check(guest.element("dial").params.num("v") == 5.0, std::string(name) + ": the guest opens on the subject, not the host");
    if (sync == sg::EmbedSync::View) {
        subject.element("knob").params.set("v", 7.0);
        e.tick(0.016);
        check(guest.element("dial").params.num("v") == 7.0, "View: the guest follows the subject");
        return;
    }
    e.fire(sg::Event{"turn", sg::Params{}.set("to", 9.0)});
    e.tick(0.016);
    if (sync == sg::EmbedSync::Live)
        check(subject.element("knob").params.num("v") == 9.0, "Live: turning the guest turns the subject at once");
    else
        check(subject.element("knob").params.num("v") == 5.0, "Commit: nothing lands before the guest is closed");
    e.close_embed("mount", true);
    check(subject.element("knob").params.num("v") == 9.0, std::string(name) + ": the subject holds what the guest did");
    check(host.element("knob").params.num("v") == 1.0, std::string(name) + ": and the host, where it hung, is untouched");
}

// --- structure is counted, values are not --------------------------------------
void test_revisions() {
    StateGraph g;
    auto& a = g.add<sg::State>("a");
    auto& b = g.add<sg::State>("b");
    a.add_element("x", "thing");
    b.add_element("y", "thing");
    a.add_element("portal", "portal");
    sg::Functor& f = g.add_functor("f", "a", "b");
    g.connect("a", "go", "b");
    Engine e(g);
    e.start("a");

    uint64_t r = g.revision();
    for (int i = 0; i < 10000; ++i) a.element("x").params.set("v", double(i));
    a.element("x").params.erase("v");
    e.tick(0.016);
    e.fire("nothing");
    e.tick(0.016);
    check(g.revision() == r, "ten thousand changed values, and frames: the structure's count stands still");

    const auto counts = [&](const char* what, auto&& op) {
        const uint64_t before = g.revision();
        op();
        check(g.revision() > before, std::string("counted: ") + what);
    };
    counts("an element added", [&] { a.add_element("z", "thing"); });
    counts("an element taken away", [&] { a.remove_element("z"); });
    counts("an arrow added", [&] { a.loop("spin", "x", "spin", nullptr); });
    counts("an arrow taken away with its element", [&] { a.add_element("w", "thing"); a.loop("w", "w", "w", nullptr); a.remove_with_arrows("w"); });
    counts("a state added", [&] { g.add<sg::State>("c"); });
    counts("a functor added", [&] { g.add_functor("h", "b", "a"); });
    counts("an object mapped by a functor the graph holds", [&] { f.on_object("x", "y"); });
    counts("an arrow mapped", [&] { f.on_morphism("spin", "spin"); });
    counts("a functor replaced", [&] { g.set_functor(sg::Functor("f", "a", "b")); });
    counts("an embedding added", [&] { g.embed("m", "a", "portal", "c", Key{}, Key{}); });
    counts("its sync changed", [&] { g.set_sync("m", sg::EmbedSync::Live); });
    counts("its propagation changed", [&] { g.set_propagation("m", sg::Propagation::Continuous); });
    counts("an embedding dropped", [&] { g.drop_embedding("m"); });
    counts("a transition added", [&] { g.connect("b", "back", "a"); });
    counts("a seam added", [&] { g.add_seam(sg::Seam{"s", "a", "b", {}, {}, {}, {}, {}, {}}); });
    counts("a seam dropped", [&] { g.drop_seam("s"); });
    counts("the initial state moved", [&] { g.set_initial("a"); });

    r = g.revision();
    g.set_focus("m", false);
    e.fire("nothing");
    e.tick(0.016);
    check(g.revision() == r, "whether an embedding takes focus, and frames, are not structure");

    // A copy of a held functor belongs to no graph: changing it is not
    // changing the graph.
    sg::Functor copy = *g.functor("f");
    copy.on_object("portal", "y");
    check(g.revision() == r, "a copy of a functor is not the graph's");
}

// Rewritten while it runs: the engine notices at its next look, not at every
// change.
void test_rewritten_while_running() {
    StateGraph g;
    g.add<sg::State>("a").add_element("x", "thing");
    Engine e(g);
    std::string said;
    e.on_problem = [&](const std::string& p) { said = p; };
    e.set_watch_interval(0.0);
    e.start("a");
    e.tick(0.016);
    const uint64_t r = g.revision();
    g.add<sg::State>("stray");
    check(g.revision() > r && said.empty(), "a state added while running is counted - and nothing is checked yet");
    e.tick(0.016);
    check(said.find("stray") != std::string::npos, "the next frame sees the count moved, checks, and names it");
    said.clear();
    for (int i = 0; i < 100; ++i) e.tick(0.016);
    check(said.empty(), "unchanged, it is not checked again");
}

// A law's trial may not rewrite the graph: the rewrite is refused, the trial
// undone, and the law says so. What it does inside the state it runs on is
// undone as always.
void test_checks_do_not_rewrite() {
    StateGraph g;
    auto& s = g.add<sg::State>("s");
    s.add_element("x", "thing");
    s.loop("grow", "x", "grow", [](sg::State& st, sg::Element&, sg::Element*, const sg::Event&) {
        if (!st.find("sprout")) st.add_element("sprout", "thing");
    });
    StateGraph* graph = &g;
    s.loop("meddle", "x", "meddle", [graph](sg::State&, sg::Element&, sg::Element*, const sg::Event&) {
        if (!graph->functor("sneaky")) graph->add_functor("sneaky", "s", "s");
    });
    const std::size_t functors = g.functors().size();
    const sg::LawReport r = sg::verify(g);
    bool named = false;
    for (const auto& v : r.violations)
        if (v.detail.find("rewritten while it was being checked") != std::string::npos) named = true;
    check(named, "an arrow that rewrites the graph during a check is reported, not run");
    check(g.functors().size() == functors && !g.functor("sneaky"), "and the graph is as it was");
    check(!s.find("sprout"), "an element an arrow adds on trial is taken away again");

    // Outside a check, the same arrow may rewrite the graph: the world may
    // change itself.
    Engine e(g);
    e.start("s");
    e.fire("meddle");
    e.tick(0.0);
    check(g.functor("sneaky") != nullptr, "while the game runs, an arrow may still rewrite the graph");
}

}  // namespace

int main() {
    test_the_legitimate_ways();
    test_host_is_not_subject(sg::EmbedSync::Live, "Live");
    test_host_is_not_subject(sg::EmbedSync::Commit, "Commit");
    test_host_is_not_subject(sg::EmbedSync::View, "View");
    test_revisions();
    test_rewritten_while_running();
    test_checks_do_not_rewrite();
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "all good", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
