// Stategine - work done once and kept is the same as work done again.
//
// The engine keeps what it has worked out - which elements a portal joins and
// what they held, what the laws found, what validate() said - and looks again
// only when a stamp says something it read has changed. Every test here runs
// the kept way and the plain way side by side and holds them to the same
// answer; a cache that is faster and wrong is a bug, not an optimisation.
#include <cstdio>
#include <random>
#include <string>

#include "sg/core/Engine.hpp"
#include "sg/core/Laws.hpp"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "[ok]  " : "[FAIL]", what.c_str());
    if (!ok) ++failures;
}

bool same_params(const sg::Params& a, const sg::Params& b) {
    if (a.size() != b.size()) return false;
    for (const auto& kv : a)
        if (!b.has(kv.first) || !(b.get(kv.first) == kv.second)) return false;
    return true;
}

bool same_state(const sg::State& a, const sg::State& b) {
    if (a.elements().size() != b.elements().size()) return false;
    for (const sg::Element& e : a.elements()) {
        const sg::Element* f = b.find(e.id);
        if (!f || f->alive != e.alive || !same_params(e.params, f->params)) return false;
    }
    return same_params(a.params(), b.params());
}

void test_stamps_name_content() {
    sg::Params p;
    p.set("x", 1.0);
    const uint64_t s1 = p.stamp();
    p.set("x", 1.0);
    check(p.stamp() == s1, "setting a value it already holds is no change");
    p.set("x", 2.0);
    check(p.stamp() != s1, "setting a new one is");
    sg::Params q = p;
    check(q.stamp() == p.stamp(), "a copy holds the same content, and says so");
    q.erase("x");
    check(q.stamp() != p.stamp(), "taking a parameter away is a change");

    sg::State s("s");
    s.add_element("a", "thing").params.set("x", 1.0);
    const uint64_t v = s.content_version(), st = s.structure();
    {
        const sg::State::Snapshot snap = s.snapshot();
        s.element("a").params.set("x", 5.0);
        s.add_element("b", "thing");
        check(s.content_version() != v, "a trial's changes show in the version");
        s.restore(snap);
    }
    check(s.content_version() == v && s.structure() == st,
          "undone, the state is its old version again - stamps and all");
    s.find("a")->alive = false;
    check(s.content_version() != v, "whether an element is alive is part of the version");
}

// Two copies of one world: a host, and a guest shown in a Live portal and in
// a View portal. One copy carries every frame, one only what changed; every
// frame both are driven the same way - the host's data changed, the guest's
// own arrows run, elements appear - and every frame they must agree.
struct Twin {
    sg::StateGraph g;
    sg::Engine engine{g};
    sg::State* host = nullptr;
    sg::State* live = nullptr;
    sg::State* view = nullptr;

    explicit Twin(sg::Propagation mode) {
        host = &g.add<sg::State>("host");
        live = &g.add<sg::State>("live");
        view = &g.add<sg::State>("view");
        host->add_element("panel", "portal");
        host->add_element("window", "portal");
        for (int i = 0; i < 40; ++i) {
            const sg::Key id{"h" + std::to_string(i)};
            host->add_element(id, "thing").params.set("x", double(i));
            live->add_element(sg::Key{"l" + std::to_string(i)}, "token").params.set("x", double(i));
        }
        // The guest's own arrow moves one token every frame it is stepped.
        live->add_element("clock", "clock").params.set("n", int64_t{0});
        live->loop("tick", "clock", "tick", [](sg::State& s, sg::Element& c, sg::Element*, const sg::Event&) {
            const int64_t n = c.params.get_or<int64_t>("n", 0) + 1;
            c.params.set("n", n);
            if (n % 3 == 0) s.element(sg::Key{"l" + std::to_string(n % 40)}).params.set("x", double(n));
        });
        auto& in = g.add_functor("in", "host", "view");
        auto& out = g.add_functor("out", "live", "host");
        for (int i = 0; i < 40; ++i) {
            const sg::Key h{"h" + std::to_string(i)};
            in.on_object(h, sg::Key{"v" + std::to_string(i)},
                         sg::transport::swizzle({{"y", "x"}}));
            // Two tokens land on one host element: the second must still win.
            out.on_object(sg::Key{"l" + std::to_string(i)}, sg::Key{"h" + std::to_string(i / 2)},
                          i % 2 ? sg::transport::swizzle({{"w", "x"}}) : sg::transport::only({"x"}));
        }
        g.embed("live", "host", "panel", "live", sg::Key{}, "out", sg::EmbedSync::Live);
        g.embed("view", "host", "window", "view", "in", sg::Key{}, sg::EmbedSync::View);
        g.set_propagation("live", mode);
        g.set_propagation("view", mode);
        g.set_focus("live", false);
        g.set_focus("view", false);
        engine.start("host");
        engine.open_embed("live");
        engine.open_embed("view");
    }

    void frame(uint32_t seed, int f) {
        std::mt19937 rng(seed);
        // Sparse edits to the host, some to the same value again.
        for (int k = 0; k < 3; ++k) {
            const int i = int(rng() % 40);
            host->element(sg::Key{"h" + std::to_string(i)}).params.set("x", double(rng() % 5));
        }
        // Now and then the view's own data is touched: the next carry must
        // put it right, as carrying every frame would.
        if (f % 7 == 0) view->element("v3").params.set("y", -1.0);
        // And now and then the host grows.
        if (f % 11 == 0) host->add_element(sg::Key{"extra" + std::to_string(f)}, "thing");
        live->emit(sg::Event{"tick"});
        engine.tick(1.0 / 60.0);
    }
};

void test_on_change_is_continuous() {
    Twin a(sg::Propagation::Continuous), b(sg::Propagation::OnChange);
    bool agree = true;
    int first = -1;
    for (int f = 0; f < 300 && agree; ++f) {
        a.frame(uint32_t(f * 7919 + 1), f);
        b.frame(uint32_t(f * 7919 + 1), f);
        agree = same_state(*a.host, *b.host) && same_state(*a.live, *b.live) && same_state(*a.view, *b.view);
        if (!agree) first = f;
    }
    check(agree, "carrying only what changed leaves every state as carrying all of it" +
                     (first < 0 ? std::string() : " (differs at frame " + std::to_string(first) + ")"));
}

void test_a_replaced_functor_is_carried() {
    Twin t(sg::Propagation::OnChange);
    t.frame(1, 1);
    sg::Functor in("in", "host", "view");
    in.on_object("h0", "v0", sg::transport::swizzle({{"z", "x"}}));
    t.g.set_functor(std::move(in));
    t.engine.tick(1.0 / 60.0);
    check(t.view->element("v0").params.has("z"), "a functor put in place of another is carried at once");
}

void test_manual_and_on_event() {
    sg::StateGraph g;
    auto& host = g.add<sg::State>("host");
    auto& guest = g.add<sg::State>("guest");
    host.add_element("panel", "portal");
    host.add_element("a", "thing").params.set("x", 1.0);
    g.add_functor("in", "host", "guest").on_object("a", "a");
    g.embed("e", "host", "panel", "guest", "in", sg::Key{}, sg::EmbedSync::View);
    g.set_propagation("e", sg::Propagation::Manual);
    sg::Engine engine(g);
    engine.start("host");
    engine.open_embed("e");
    host.element("a").params.set("x", 2.0);
    engine.tick(0.016);
    check(guest.element("a").params.num("x") == 1.0, "a Manual view does not follow by itself");
    engine.sync_embed("e");
    check(guest.element("a").params.num("x") == 2.0, "and follows when synced");

    g.set_propagation("e", sg::Propagation::OnEvent);
    host.element("a").params.set("x", 3.0);
    engine.tick(0.016);
    check(guest.element("a").params.num("x") == 2.0, "an OnEvent view waits for an event");
    engine.fire(sg::Event{"poke"});
    engine.tick(0.016);
    check(guest.element("a").params.num("x") == 3.0, "and follows when one crosses into it");
}

std::size_t count(const sg::LawReport& r) { return r.structure.size() + r.violations.size(); }

void test_law_cache_is_the_laws() {
    // A ledger and its mirror, a square of functors A -> B -> D, A -> C -> D.
    sg::StateGraph g;
    for (const char* id : {"A", "B", "C", "D"}) {
        auto& s = g.add<sg::State>(id);
        for (int i = 0; i < 20; ++i) s.add_element(sg::Key{"e" + std::to_string(i)}, "cell").params.set("v", double(i));
    }
    g.state("A").loop("bump", "e0", "bump", [](sg::State&, sg::Element& e, sg::Element*, const sg::Event&) {
        e.params.set("v", e.params.num("v") + 1.0);
    });
    const auto straight = [](sg::Key name, sg::Key from, sg::Key to) {
        sg::Functor f(name, from, to);
        for (int i = 0; i < 20; ++i) f.on_object(sg::Key{"e" + std::to_string(i)}, sg::Key{"e" + std::to_string(i)}, sg::transport::only({"v"}));
        return f;
    };
    g.add_functor(straight("ab", "A", "B"));
    g.add_functor(straight("ac", "A", "C"));
    g.add_functor(straight("bd", "B", "D"));
    g.add_functor(straight("cd", "C", "D"));
    g.connect("A", "go", "B");
    g.connect("A", "go2", "C");
    g.connect("B", "go", "D");
    sg::Diagram square("square");
    square.commutes(sg::Path("A").functor("ab").functor("bd"), sg::Path("A").functor("ac").functor("cd"));

    for (auto strategy : {sg::LawCache::Strategy::Incremental, sg::LawCache::Strategy::Adaptive}) {
        sg::LawCache cache(strategy);
        const char* name = strategy == sg::LawCache::Strategy::Adaptive ? "adaptive" : "incremental";
        const std::size_t plain = count(sg::verify(g, {square}));
        check(count(sg::verify(g, cache, {square})) == plain, std::string(name) + ": the first check is the plain one");
        cache.reset_stats();
        check(count(sg::verify(g, cache, {square})) == plain, std::string(name) + ": and so is the second");
        if (strategy == sg::LawCache::Strategy::Incremental)
            check(cache.stats().recomputed == 0 && cache.stats().reused == cache.stats().equations,
                  "unchanged, every answer is the one kept");

        // Break the square: B -> D now doubles.
        sg::Functor bd("bd", "B", "D");
        for (int i = 0; i < 20; ++i)
            bd.on_object(sg::Key{"e" + std::to_string(i)}, sg::Key{"e" + std::to_string(i)},
                         [](const sg::Element& s, sg::Element& d) { d.params.set("v", s.params.num("v") * 2); });
        g.set_functor(bd);
        cache.reset_stats();
        const sg::LawReport broken = sg::verify(g, cache, {square});
        check(count(broken) == count(sg::verify(g, {square})) && count(broken) > plain,
              std::string(name) + ": a functor replaced is seen, and the square breaks");
        if (strategy == sg::LawCache::Strategy::Incremental)
            check(cache.stats().sides_reused > 0, "and the side it did not touch is not run again");

        // Put it back; change the data instead.
        g.set_functor(straight("bd", "B", "D"));
        g.state("A").element("e5").params.set("v", 100.0);
        check(count(sg::verify(g, cache, {square})) == count(sg::verify(g, {square})),
              std::string(name) + ": changed data is checked again");
        g.state("C").element("e5").params.set("v", -3.0);
        check(count(sg::verify(g, cache, {square})) == count(sg::verify(g, {square})),
              std::string(name) + ": wherever it changed");
    }
}

void test_validate_reuse_is_validate() {
    sg::StateGraph g;
    auto& a = g.add<sg::State>("a");
    auto& b = g.add<sg::State>("b");
    a.add_element("x", "thing");
    a.loop("spin", "x", "spin", nullptr);
    g.connect("a", "go", "b");
    check(g.validate() == g.validate(false), "kept and fresh agree on a sound graph");
    a.arrow("dangle", "x", "nowhere", "d", nullptr);
    check(g.validate() == g.validate(false) && !g.validate().empty(), "and on a broken arrow, added later");
    g.add<sg::State>("lost");
    check(g.validate() == g.validate(false), "and on a state nobody reaches");
    b.add_element("y", "thing");
    g.add_functor("f", "a", "b").on_object("x", "y").on_morphism("spin", "missing");
    check(g.validate() == g.validate(false), "and on a functor whose arrow has no image");
    a.remove_with_arrows("x");
    check(g.validate() == g.validate(false), "and after the element and its arrows are gone");
}

}  // namespace

int main() {
    test_stamps_name_content();
    test_on_change_is_continuous();
    test_a_replaced_functor_is_carried();
    test_manual_and_on_event();
    test_law_cache_is_the_laws();
    test_validate_reuse_is_validate();
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "all good", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
