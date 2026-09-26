// Stategine - how the engine's costs grow with the world.
//
// bench.cpp measures the hot paths at one size; this one measures them at
// many (a thousand things to a million), and sets each shortcut the engine
// takes against the plain way it stands in for:
//
//   stamps          a parameter set to the value it holds is no change
//   OnChange        a portal carries only what changed since it last ran
//   validate reuse  a state's arrows, a functor's ends, checked again only
//                   when they are not what they were
//   LawCache        a law that held on this data holds on it still
//
// Every row is one measurement: what was run, on how many things, how long it
// took, how many operations a second that is, and how many allocations (and
// bytes) each operation cost - counted by the global operator new below.
// Where a shortcut and the plain way should leave the same world, both are
// run and the worlds compared: OK or MISMATCH.
//
// Not a test: run it by hand (sg_bench_scale), with the build optimised.
// `--quick` runs only the smaller sizes; `--big` adds a million objects to
// the portal cases (3, 4, 10), which alone take over a minute. Numbers name
// the cases to run: `sg_bench_scale 3 8`.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "sg/sg.hpp"

// The LawCache rows need sg::LawCache (Laws.hpp); build with
// -DSG_BENCH_LAWCACHE=0 to leave them out.
#ifndef SG_BENCH_LAWCACHE
#define SG_BENCH_LAWCACHE 1
#endif

// ---------------------------------------------------------------------------
// Counting allocations: every operator new in the program passes through here.
// One thread builds and runs everything, so plain counters do.
// ---------------------------------------------------------------------------
namespace {
std::size_t g_allocs = 0;
std::size_t g_bytes = 0;

void* counted_alloc(std::size_t n) {
    ++g_allocs;
    g_bytes += n;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
}  // namespace

void* operator new(std::size_t n) { return counted_alloc(n); }
void* operator new[](std::size_t n) { return counted_alloc(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {

using Clock = std::chrono::steady_clock;

bool g_quick = false;
bool g_big = false;

// What one measurement cost: wall time, and what was allocated during it.
struct Cost {
    double secs = 0;
    std::size_t allocs = 0;
    std::size_t bytes = 0;
};

template <typename Fn>
Cost measure(Fn&& fn) {
    const std::size_t a0 = g_allocs, b0 = g_bytes;
    const auto t0 = Clock::now();
    fn();
    Cost c;
    c.secs = std::chrono::duration<double>(Clock::now() - t0).count();
    c.allocs = g_allocs - a0;
    c.bytes = g_bytes - b0;
    return c;
}

// Run `fn` again and again until `seconds` have gone by (at least `min_reps`
// times, at most `max_reps`); the cost of all of it, and how many times.
template <typename Fn>
Cost repeat_for(double seconds, long min_reps, long max_reps, long& reps, Fn&& fn) {
    reps = 0;
    return measure([&] {
        const auto t0 = Clock::now();
        while (reps < max_reps &&
               (reps < min_reps || std::chrono::duration<double>(Clock::now() - t0).count() < seconds)) {
            fn();
            ++reps;
        }
    });
}

std::string human(long n) {
    if (n >= 1000000 && n % 1000000 == 0) return std::to_string(n / 1000000) + "M";
    if (n >= 1000 && n % 1000 == 0) return std::to_string(n / 1000) + "K";
    return std::to_string(n);
}

std::string rate(double r) {
    char buf[32];
    if (r >= 1e9) std::snprintf(buf, sizeof buf, "%.2fG", r / 1e9);
    else if (r >= 1e6) std::snprintf(buf, sizeof buf, "%.2fM", r / 1e6);
    else if (r >= 1e3) std::snprintf(buf, sizeof buf, "%.2fK", r / 1e3);
    else std::snprintf(buf, sizeof buf, "%.2f", r);
    return buf;
}

std::string bytes(double b) {
    char buf[32];
    if (b >= 1 << 20) std::snprintf(buf, sizeof buf, "%.1fMB", b / (1 << 20));
    else if (b >= 1 << 10) std::snprintf(buf, sizeof buf, "%.1fKB", b / (1 << 10));
    else std::snprintf(buf, sizeof buf, "%.0fB", b);
    return buf;
}

void skipped(const std::string& what, long n, const std::string& why);

void header(const char* title) {
    std::printf("\n== %s\n", title);
    std::printf("%-30s %6s %8s %11s %10s %9s %9s  %s\n", "case", "N", "ops", "time", "ops/s",
                "alloc/op", "B/op", "note");
}

// One row: `ops` operations of a kind (frames, applies, validations...) cost `c`.
void row(const std::string& what, long n, long ops, const Cost& c, const std::string& note = "") {
    char t[32];
    if (c.secs >= 1) std::snprintf(t, sizeof t, "%.2f s", c.secs);
    else std::snprintf(t, sizeof t, "%.2f ms", c.secs * 1e3);
    const double per = ops ? 1.0 / ops : 0;
    std::printf("%-30s %6s %8ld %11s %10s %9.2f %9s  %s\n", what.c_str(), human(n).c_str(), ops, t,
                rate(ops / c.secs).c_str(), c.allocs * per, bytes(c.bytes * per).c_str(),
                note.c_str());
    std::fflush(stdout);
}

void skipped(const std::string& what, long n, const std::string& why) {
    std::printf("%-30s %6s %8s %11s %10s %9s %9s  skipped: %s\n", what.c_str(), human(n).c_str(),
                "-", "-", "-", "-", "-", why.c_str());
    std::fflush(stdout);
}

std::vector<long> sizes(long cap) {
    std::vector<long> out;
    for (long n : {1000L, 10000L, 100000L, 1000000L})
        if (n <= cap && !(g_quick && n > 10000)) out.push_back(n);
    return out;
}

// The sizes of a case whose million takes too long for an ordinary run.
std::vector<long> sizes_big(const char* what) {
    if (g_big) return sizes(1000000);
    if (!g_quick) skipped(what, 1000000, "over 20 s at a million (building both worlds); run with --big");
    return sizes(100000);
}

// Frames enough to see a cost of `n` things a frame, not so many the run drags.
long frames_for(long n, double budget, long lo, long hi) {
    return std::max(lo, std::min(hi, static_cast<long>(budget / static_cast<double>(n))));
}

constexpr double kDt = 1.0 / 60.0;

sg::Key name(const char* prefix, long i) { return sg::Key{prefix + std::to_string(i)}; }

// An element placed in a state with a position and nothing else: no arrow,
// so it costs a frame nothing (what Spatial3D::fixture is, for any state).
sg::Element& place(sg::State& s, sg::Key id, sg::Key kind, double x, double y, double z) {
    sg::Element& e = s.add_element(id, kind);
    sg::set_position(e, {x, y, z});
    return e;
}

// How many objects a portal's functor carried: every transport it runs counts.
std::size_t g_carried = 0;

sg::Transport counted(sg::Transport t) {
    return [t = std::move(t)](const sg::Element& s, sg::Element& d) {
        ++g_carried;
        t(s, d);
    };
}

// Which elements a mutating frame changes: none, one in a hundred (a
// different hundredth each frame), or all.
enum class Mutation { Idle, Sparse, Dense };
const char* mutation_name(Mutation m) {
    return m == Mutation::Idle ? "idle" : m == Mutation::Sparse ? "sparse 1%" : "dense 100%";
}

// Edits made to a state between frames. They stand in for what the state's
// own arrows would do; the bench writes them directly so that what is
// measured is the portal, not the edit.
void mutate(std::vector<sg::Element*>& els, Mutation m, long frame) {
    if (m == Mutation::Idle) return;
    const std::size_t step = m == Mutation::Sparse ? 100 : 1;
    const double v = 0.5 * static_cast<double>(frame);
    for (std::size_t i = static_cast<std::size_t>(frame) % step; i < els.size(); i += step)
        els[i]->params.set(sg::keys::x, v + static_cast<double>(i));
}

// Every x, y, z of a list of elements, in order: what two worlds are compared on.
std::vector<double> positions(const std::vector<sg::Element*>& els) {
    std::vector<double> out;
    out.reserve(els.size() * 3);
    for (const sg::Element* e : els) {
        out.push_back(e->params.num(sg::keys::x));
        out.push_back(e->params.num(sg::keys::y));
        out.push_back(e->params.num(sg::keys::z));
    }
    return out;
}

// ===========================================================================
// 1. An idle world: N things, not one arrow. A frame should cost the same
//    whatever N is.
// ===========================================================================
void idle_world() {
    header("1. idle world: N fixtures, no arrows (frames)");
    for (long n : sizes(1000000)) {
        sg::StateGraph graph;
        auto& world = graph.add<sg::Spatial3D>("world");
        for (long i = 0; i < n; ++i) world.fixture(name("f", i), double(i % 1000), 0, double(i / 1000));
        sg::Engine engine(graph);
        engine.start();
        engine.run_fixed(kDt, 10);
        const long frames = 2000;
        const Cost c = measure([&] { engine.run_fixed(kDt, frames); });
        row("idle frame", n, frames, c);
    }
}

// ===========================================================================
// 2. An active world: N moving meshes, each with its integrator arrow.
// ===========================================================================
void active_world() {
    header("2. active world: N meshes with velocity (frames)");
    for (long n : sizes(1000000)) {
        sg::StateGraph graph;
        auto& world = graph.add<sg::Spatial3D>("world");
        for (long i = 0; i < n; ++i)
            world.mesh(name("m", i), double(i % 1000), 0, double(i / 1000)).params.set(sg::keys::vx, 1.0);
        sg::Engine engine(graph);
        engine.start();
        engine.run_fixed(kDt, 2);
        const long frames = frames_for(n, 4e6, 5, 500);
        const Cost c = measure([&] { engine.run_fixed(kDt, frames); });
        char note[64];
        std::snprintf(note, sizeof note, "%.1f ns/body", c.secs * 1e9 / (double(frames) * n));
        row("active frame", n, frames, c, note);
    }
}

// ===========================================================================
// 3 & 4. One portal, N objects across it: a Live one (the guest edited, `out`
//        carries the edits to the host every frame) and a View one (the host
//        changes, `in` refreshes the guest). OnChange against Continuous,
//        with the edited side idle, 1% edited a frame, all of it edited.
// ===========================================================================
struct PortalWorld {
    std::unique_ptr<sg::StateGraph> graph = std::make_unique<sg::StateGraph>();
    std::unique_ptr<sg::Engine> engine;
    std::vector<sg::Element*> edited;  // the side the edits are made on
    std::vector<sg::Element*> shown;   // the side the functor writes
};

// host "host": N fixtures b<i> and a portal; guest "guest": N tokens t<i>.
// in  (host -> guest): x -> x, z -> y     out (guest -> host): x -> x, y -> z
// A quiet host does not integrate: nothing in the world is stamped in a frame
// but what the bench edits. (A host that does emits its step event with a
// fresh `dt` argument every frame, and that is a stamp too.)
std::unique_ptr<PortalWorld> portal_world(long n, sg::EmbedSync sync, sg::Propagation how,
                                          bool quiet_host = false) {
    auto w = std::make_unique<PortalWorld>();
    sg::StateGraph& g = *w->graph;
    auto& host = g.add<sg::Spatial3D>("host");
    host.set_integrating(!quiet_host);
    auto& guest = g.add<sg::Spatial2D>("guest", 64, 64);
    guest.set_integrating(false);  // edited, not simulated
    host.portal("panel", {0, 2, 0}, 2, 2);
    std::vector<std::pair<sg::Key, sg::Key>> objects;
    objects.reserve(static_cast<std::size_t>(n));
    std::vector<sg::Element*> hs, gs;
    for (long i = 0; i < n; ++i) {
        hs.push_back(&place(host, name("b", i), sg::kinds::mesh, double(i), 0, double(i % 7)));
        gs.push_back(&place(guest, name("t", i), sg::kinds::sprite, 0, 0, 0));
        objects.emplace_back(hs.back()->id, gs.back()->id);
    }
    using sg::keys::x;
    using sg::keys::y;
    using sg::keys::z;
    const bool live = sync == sg::EmbedSync::Live;
    g.add_lens("in", "out", "host", "guest", objects,
               counted(sg::transport::swizzle({{x, x}, {y, z}})),
               counted(sg::transport::swizzle({{x, x}, {z, y}})));
    sg::Embedding e;
    e.name = "portal";
    e.host = "host";
    e.portal = "panel";
    e.guest = "guest";
    e.in = "in";
    e.out = live ? sg::Key{"out"} : sg::Key{};
    e.sync = sync;
    e.propagate = how;
    e.focus = false;
    g.embed(e);
    g.set_initial("host");
    w->engine = std::make_unique<sg::Engine>(g);
    w->engine->start();
    w->engine->open_embed("portal");
    w->edited = live ? gs : hs;
    w->shown = live ? hs : gs;
    return w;
}

void portal_case(sg::EmbedSync sync) {
    const bool live = sync == sg::EmbedSync::Live;
    header(live ? "3. Live portal, N objects: `out` guest -> host (frames)"
                : "4. View portal, N objects: `in` host -> guest (frames)");
    for (long n : sizes_big(live ? "Live portal" : "View portal")) {
        // A dense frame carries every object twice over for Live (before and
        // after the host steps) - budget the frames on that.
        const long frames = frames_for(n, 3e6, 5, 400);
        std::vector<double> result[2];
        std::size_t carried[2][3] = {};
        Cost cost[2][3];
        const sg::Propagation modes[2] = {sg::Propagation::OnChange, sg::Propagation::Continuous};
        for (int m = 0; m < 2; ++m) {
            auto w = portal_world(n, sync, modes[m]);
            w->engine->run_fixed(kDt, 2);  // the memo built, the first carry done
            long frame = 0;
            for (int k = 0; k < 3; ++k) {
                const Mutation mu = static_cast<Mutation>(k);
                g_carried = 0;
                cost[m][k] = measure([&] {
                    for (long f = 0; f < frames; ++f, ++frame) {
                        mutate(w->edited, mu, frame);
                        w->engine->tick(kDt);
                    }
                });
                carried[m][k] = g_carried;
            }
            result[m] = positions(w->shown);
        }
        // Idle, with nothing else in the world stamped either: OnChange then
        // sees at once that nothing anywhere changed.
        {
            auto w = portal_world(n, sync, sg::Propagation::OnChange, true);
            w->engine->run_fixed(kDt, 2);
            g_carried = 0;
            const Cost c = measure([&] { w->engine->run_fixed(kDt, static_cast<uint64_t>(frames)); });
            row("OnChange   idle, quiet host", n, frames, c,
                "carried " + std::to_string(g_carried / static_cast<std::size_t>(frames)) + "/frame");
        }
        const bool same = result[0] == result[1];
        for (int k = 0; k < 3; ++k) {
            for (int m = 0; m < 2; ++m) {
                char note[128];
                std::snprintf(note, sizeof note, "carried %.0f/frame", double(carried[m][k]) / frames);
                std::string label = std::string(m == 0 ? "OnChange   " : "Continuous ") + mutation_name(static_cast<Mutation>(k));
                std::string s = note;
                if (m == 1) {
                    const double d = cost[0][k].secs / cost[1][k].secs;
                    char cmp[64];
                    std::snprintf(cmp, sizeof cmp, "  OnChange %s by %.0f%%", d <= 1 ? "wins" : "loses",
                                  d <= 1 ? (1 / d - 1) * 100 : (d - 1) * 100);
                    s += cmp;
                    if (k == 2) s += same ? "  worlds equal: OK" : "  worlds equal: MISMATCH";
                }
                row(label, n, frames, cost[m][k], s);
            }
        }
    }
}

// ===========================================================================
// 5. Many portals: K guests, each of four tokens, embedded Live in one host,
//    all open, one in a hundred edited a frame.
// ===========================================================================
struct ManyWorld {
    std::unique_ptr<sg::StateGraph> graph = std::make_unique<sg::StateGraph>();
    std::unique_ptr<sg::Engine> engine;
    std::vector<std::vector<sg::Element*>> guests;  // each guest's tokens
    std::vector<sg::Element*> host_bodies;
};

std::unique_ptr<ManyWorld> many_world(long k, sg::Propagation how) {
    auto w = std::make_unique<ManyWorld>();
    sg::StateGraph& g = *w->graph;
    auto& host = g.add<sg::Spatial3D>("host");
    using sg::keys::x;
    using sg::keys::y;
    using sg::keys::z;
    const sg::Transport put = counted(sg::transport::swizzle({{x, x}, {z, y}}));
    for (long i = 0; i < k; ++i) {
        const std::string gi = "g" + std::to_string(i);
        host.portal(sg::Key{"p" + gi}, {double(i), 2, 0}, 1, 1);
        auto& guest = g.add<sg::Spatial2D>(sg::Key{gi}, 8, 8);
        guest.set_integrating(false);
        sg::Functor& out = g.add_functor(sg::Key{"out" + gi}, sg::Key{gi}, "host");
        w->guests.emplace_back();
        for (int j = 0; j < 4; ++j) {
            const sg::Key t{"t" + std::to_string(j)};
            const sg::Key b{"b" + gi + "_" + std::to_string(j)};
            w->guests.back().push_back(&place(guest, t, sg::kinds::sprite, 0, 0, 0));
            w->host_bodies.push_back(&place(host, b, sg::kinds::mesh, 0, 0, 0));
            out.on_object(t, b, put);
        }
        sg::Embedding e;
        e.name = sg::Key{"e" + gi};
        e.host = "host";
        e.portal = sg::Key{"p" + gi};
        e.guest = sg::Key{gi};
        e.out = sg::Key{"out" + gi};
        e.sync = sg::EmbedSync::Live;
        e.propagate = how;
        e.focus = false;
        g.embed(e);
    }
    g.set_initial("host");
    w->engine = std::make_unique<sg::Engine>(g);
    w->engine->start();
    for (long i = 0; i < k; ++i) w->engine->open_embed(sg::Key{"eg" + std::to_string(i)});
    return w;
}

void many_embeddings() {
    header("5. many embeddings: K Live guests of 4 tokens in one host (frames)");
    for (long k : {1000L, 10000L}) {
        const long frames = k >= 10000 ? 60 : 300;
        std::vector<double> result[2];
        Cost cost[2][2];
        std::size_t carried[2][2] = {};
        const sg::Propagation modes[2] = {sg::Propagation::OnChange, sg::Propagation::Continuous};
        for (int m = 0; m < 2; ++m) {
            auto w = many_world(k, modes[m]);
            w->engine->run_fixed(kDt, 2);
            long frame = 0;
            for (int idle = 1; idle >= 0; --idle) {
                g_carried = 0;
                cost[m][idle] = measure([&] {
                    for (long f = 0; f < frames; ++f, ++frame) {
                        if (!idle)
                            for (std::size_t i = static_cast<std::size_t>(frame) % 100; i < w->guests.size(); i += 100)
                                w->guests[i][0]->params.set(sg::keys::x, double(frame));
                        w->engine->tick(kDt);
                    }
                });
                carried[m][idle] = g_carried;
            }
            result[m] = positions(w->host_bodies);
        }
        const bool same = result[0] == result[1];
        for (int idle = 1; idle >= 0; --idle)
            for (int m = 0; m < 2; ++m) {
                char note[96];
                std::snprintf(note, sizeof note, "carried %.0f/frame", double(carried[m][idle]) / frames);
                std::string s = note;
                if (m == 1 && idle == 0) s += same ? "  worlds equal: OK" : "  worlds equal: MISMATCH";
                row(std::string(m == 0 ? "OnChange   " : "Continuous ") + (idle ? "all idle" : "1% edited"), k,
                    frames, cost[m][idle], s);
            }
    }
}

// ===========================================================================
// 6. Deep composition: a chain of D functors S0 -> S1 -> ... -> SD, as one
//    registered composite and as D functors applied one after another.
// ===========================================================================
void deep_composition() {
    header("6. functor composition: depth D, N objects (objects carried)");
    const long n = 1000;
    const int max_depth = 64;
    sg::StateGraph graph;
    std::vector<sg::State*> states;
    for (int s = 0; s <= max_depth; ++s) {
        auto& st = graph.add<sg::State>(name("S", s));
        for (long i = 0; i < n; ++i) place(st, name("e", i), sg::kinds::sprite, double(i), double(s), 0);
        states.push_back(&st);
    }
    const sg::Transport xy = sg::transport::only({sg::keys::x, sg::keys::y});
    std::vector<sg::Key> chain;
    for (int s = 0; s < max_depth; ++s) {
        sg::Functor& f = graph.add_functor(name("F", s), name("S", s), name("S", s + 1));
        for (long i = 0; i < n; ++i) f.on_object(name("e", i), name("e", i), xy);
        chain.push_back(f.name());
    }
    for (int d : {2, 4, 8, 16, 32, 64}) {
        const std::vector<sg::Key> part(chain.begin(), chain.begin() + d);
        const sg::Functor& comp = graph.compose_functors(name("comp", d), part);
        long reps = 0;
        const Cost one = repeat_for(0.25, 3, 100000, reps, [&] { comp.apply(*states[0], *states[static_cast<std::size_t>(d)]); });
        row("composite, depth " + std::to_string(d), n, reps * n, one);
        const Cost steps = repeat_for(0.25, 3, 100000, reps, [&] {
            for (int s = 0; s < d; ++s) graph.functor(part[static_cast<std::size_t>(s)])->apply(*states[static_cast<std::size_t>(s)], *states[static_cast<std::size_t>(s) + 1]);
        });
        row("chain applied, depth " + std::to_string(d), n, reps * n, steps);
    }
}

// ===========================================================================
// 7. validate(): N states in a chain, joined alternately by a transition
//    carrying a functor and by an embedding.
// ===========================================================================
void validation() {
    header("7. StateGraph::validate, N states chained (validations)");
    for (long n : sizes(100000)) {
        sg::StateGraph g;
        for (long i = 0; i < n; ++i) {
            auto& s = g.add<sg::State>(name("s", i));
            s.add_element("a", sg::kinds::anchor);
            s.add_element("b", sg::kinds::anchor);
            s.add_element("p", sg::kinds::portal);
            s.arrow("f", "a", "b", "go", nullptr);
        }
        for (long i = 0; i + 1 < n; ++i) {
            if (i % 2 == 0) {
                sg::Functor& f = g.add_functor(name("F", i), name("s", i), name("s", i + 1));
                f.on_object("a", "a").on_object("b", "b").on_morphism("f", "f");
                g.connect(name("s", i), "next", name("s", i + 1), f.name());
            } else {
                g.embed(name("E", i), name("s", i), "p", name("s", i + 1), sg::Key{}, sg::Key{});
            }
        }
        g.set_initial("s0");
        const std::size_t problems = g.validate(false).size();
        long reps = 0;
        const Cost plain = repeat_for(0.4, 3, 2000, reps, [&] { g.validate(false); });
        row("from scratch (reuse=false)", n, reps, plain, "problems " + std::to_string(problems));
        g.validate(true);  // what is kept, taken once
        const Cost same = repeat_for(0.4, 3, 2000, reps, [&] { g.validate(true); });
        row("reuse, nothing changed", n, reps, same);
        sg::State& mid = g.state(name("s", n / 2));
        long k = 0;
        const Cost one = repeat_for(0.4, 3, 2000, reps, [&] {
            mid.add_element(name("x", k++), sg::kinds::anchor);
            g.validate(true);
        });
        row("reuse, 1 state restructured", n, reps, one);
        const Cost topo = repeat_for(0.4, 3, 2000, reps, [&] {
            const long guest = 1 + (k % (n - 1));
            g.embed(name("T", k++), "s0", "p", name("s", guest), sg::Key{}, sg::Key{});
            g.validate(true);
        });
        row("reuse, +1 embedding each time", n, reps, topo,
            "problems " + std::to_string(g.validate(true).size()));
    }
    skipped("validate", 1000000, "a million states is over 2 GB of graph and minutes to build");
}

// ===========================================================================
// 8. The laws: sg::verify from scratch, against sg::verify with a LawCache.
// ===========================================================================
// A quad of states A -> B -> D, A -> C -> D, each state three bodies with a
// loop that moves one by `dt`, the functors carrying bodies and loops alike.
// Every quad's square is declared to commute; quad 0's C -> D shifts x by one,
// so its square does not - there is always a violation to agree on.
struct LawWorld {
    sg::StateGraph g;
    std::vector<sg::Diagram> diagrams;
    sg::LawOptions opts;
    long quads = 0;
};

const sg::Key kTick{"tick"};

void law_state(sg::StateGraph& g, sg::Key id) {
    auto& s = g.add<sg::State>(id);
    for (int j = 0; j < 3; ++j) {
        const sg::Key e = name("e", j);
        place(s, e, sg::kinds::sprite, double(j), 0, 0);
        s.loop(sg::Key{"bump." + e.str()}, e, kTick, [](sg::State&, sg::Element& el, sg::Element*, const sg::Event& ev) {
            el.params.set(sg::keys::x, el.params.num(sg::keys::x) + ev.args.num(sg::keys::dt));
        });
    }
}

sg::Functor law_functor(sg::Key name, sg::Key from, sg::Key to, bool shift) {
    sg::Functor f(name, from, to);
    for (int j = 0; j < 3; ++j) {
        const sg::Key e = ::name("e", j);
        sg::Transport t = nullptr;
        if (shift)
            t = [](const sg::Element& s, sg::Element& d) {
                sg::transport::copy_all(s, d);
                d.params.set(sg::keys::x, s.params.num(sg::keys::x) + 1.0);
            };
        f.on_object(e, e, t).on_morphism(sg::Key{"bump." + e.str()}, sg::Key{"bump." + e.str()});
    }
    return f;
}

std::unique_ptr<LawWorld> law_world(long quads) {
    auto w = std::make_unique<LawWorld>();
    w->quads = quads;
    sg::StateGraph& g = w->g;
    sg::Diagram squares("squares");
    for (long q = 0; q < quads; ++q) {
        const std::string s = std::to_string(q);
        const sg::Key A{"A" + s}, B{"B" + s}, C{"C" + s}, D{"D" + s};
        for (sg::Key id : {A, B, C, D}) law_state(g, id);
        g.add_functor(law_functor(sg::Key{"AB" + s}, A, B, false));
        g.add_functor(law_functor(sg::Key{"AC" + s}, A, C, false));
        g.add_functor(law_functor(sg::Key{"BD" + s}, B, D, false));
        g.add_functor(law_functor(sg::Key{"CD" + s}, C, D, q == 0));
        g.connect(A, "ab", B);
        g.connect(A, "ac", C);
        g.connect(B, "bd", D);
        g.connect(C, "cd", D);
        if (q + 1 < quads) g.connect(D, "next", sg::Key{"A" + std::to_string(q + 1)});
        squares.commutes(sg::Path(A).functor(sg::Key{"AB" + s}).functor(sg::Key{"BD" + s}),
                         sg::Path(A).functor(sg::Key{"AC" + s}).functor(sg::Key{"CD" + s}));
    }
    g.set_initial("A0");
    w->diagrams.push_back(std::move(squares));
    w->opts.args.set(sg::keys::dt, 0.1);
    return w;
}

// The same edits, applied between verifies, to every run: nothing, one
// state's data, one functor B -> D replaced by an equal one.
enum class LawEdit { None, Data, Functor };

void law_edit(LawWorld& w, LawEdit edit, long i) {
    const long q = w.quads > 1 ? 1 + i % (w.quads - 1) : 0;  // spare quad 0's violation
    const std::string s = std::to_string(q);
    if (edit == LawEdit::Data) {
        w.g.state(sg::Key{"A" + s}).element("e1").params.set(sg::keys::y, double(i));
    } else if (edit == LawEdit::Functor) {
        w.g.set_functor(law_functor(sg::Key{"BD" + s}, sg::Key{"B" + s}, sg::Key{"D" + s}, false));
    }
}

void laws_case() {
    header("8. laws: sg::verify plain vs with a LawCache (verifies)");
    struct Size {
        const char* label;
        long quads;
    };
    const Size shapes[] = {{"small (1 quad)", 1}, {"large (100 quads)", 100}, {"large (1000 quads)", 1000}};
    for (const Size& sh : shapes) {
        if (g_quick && sh.quads > 100) continue;
        const long states = sh.quads * 4;
        for (LawEdit edit : {LawEdit::None, LawEdit::Data, LawEdit::Functor}) {
            const char* ename = edit == LawEdit::None ? "stable" : edit == LawEdit::Data ? "1 state's data changed" : "1 functor B->D replaced";
            std::printf("-- %s, %s\n", sh.label, ename);
            // Plain: every law run from scratch every time.
            std::size_t plain_violations = 0;
            double plain_secs = 0;
            {
                auto w = law_world(sh.quads);
                long reps = 0, i = 0;
                const Cost c = repeat_for(0.6, 2, 2000, reps, [&] {
                    law_edit(*w, edit, i++);
                    plain_violations = sg::verify(w->g, w->diagrams, w->opts).violations.size();
                });
                plain_secs = c.secs / reps;
                (void)plain_secs;  // read by the LawCache rows
                row("  plain verify", states, reps, c, "violations " + std::to_string(plain_violations));
            }
#if SG_BENCH_LAWCACHE
            const std::pair<const char*, sg::LawCache::Strategy> strategies[] = {
                {"  cache Adaptive", sg::LawCache::Strategy::Adaptive},
                {"  cache Direct", sg::LawCache::Strategy::Direct},
                {"  cache Incremental", sg::LawCache::Strategy::Incremental}};
            for (const auto& st : strategies) {
                auto w = law_world(sh.quads);
                sg::LawCache cache(st.second);
                std::size_t violations = 0;
                const Cost first = measure([&] { violations = sg::verify(w->g, cache, w->diagrams, w->opts).violations.size(); });
                const auto before = cache.stats();
                long reps = 0, i = 0;
                bool agree = true;
                const Cost c = repeat_for(0.6, 2, 20000, reps, [&] {
                    law_edit(*w, edit, i++);
                    const std::size_t v = sg::verify(w->g, cache, w->diagrams, w->opts).violations.size();
                    agree = agree && v == plain_violations;
                });
                const auto after = cache.stats();
                const double eq = double(after.equations - before.equations);
                const double hit = eq > 0 ? double(after.reused - before.reused) / eq * 100 : 0;
                char note[256];
                std::snprintf(note, sizeof note,
                              "x%.1f vs plain  cold %.1f ms  hit %.1f%%  recomputed %.0f/verify  sides reused %.0f/verify  "
                              "keep %.3f ms/verify  mem %s  violations %s",
                              plain_secs / (c.secs / reps), first.secs * 1e3, hit,
                              double(after.recomputed - before.recomputed) / reps,
                              double(after.sides_reused - before.sides_reused) / reps,
                              (after.bookkeeping_ns - before.bookkeeping_ns) / 1e6 / reps,
                              bytes(double(cache.memory())).c_str(),
                              agree && violations == plain_violations ? (std::to_string(plain_violations) + " = plain: OK").c_str()
                                    : "MISMATCH");
                row(st.first, states, reps, c, note);
            }
#endif
        }
    }
#if !SG_BENCH_LAWCACHE
    std::printf("(LawCache rows left out: built without SG_BENCH_LAWCACHE)\n");
#endif
}

// ===========================================================================
// 9. What a parameter set costs: to the value it holds (no change, no stamp)
//    and to a new one (a new stamp); and what reading a state's content
//    version costs, a pass over its elements.
// ===========================================================================
void params_set() {
    header("9. Params::set and State::content_version (element ops)");
    for (long n : sizes(1000000)) {
        sg::State s(sg::Key{"p" + std::to_string(n)});
        std::vector<sg::Element*> els;
        for (long i = 0; i < n; ++i) {
            sg::Element& e = place(s, name("e", i), sg::kinds::sprite, double(i), 0, 0);
            e.params.set(sg::keys::vx, 0.0).set(sg::keys::glyph, std::string("#"));
            els.push_back(&e);
        }
        const long reps = std::max(1L, 2000000L / n);
        const Cost same = measure([&] {
            for (long r = 0; r < reps; ++r)
                for (long i = 0; i < n; ++i) els[static_cast<std::size_t>(i)]->params.set(sg::keys::x, double(i));
        });
        row("set, equal value (no stamp)", n, reps * n, same);
        const Cost fresh = measure([&] {
            for (long r = 0; r < reps; ++r)
                for (long i = 0; i < n; ++i) els[static_cast<std::size_t>(i)]->params.set(sg::keys::x, double(i + r + 1));
        });
        row("set, new value (stamped)", n, reps * n, fresh,
            "equal-value set is " + std::to_string(int(100 * same.secs / fresh.secs)) + "% of a new one");
        const Cost glyph = measure([&] {
            for (long r = 0; r < reps; ++r)
                for (long i = 0; i < n; ++i) els[static_cast<std::size_t>(i)]->params.set(sg::keys::glyph, std::string("#"));
        });
        row("set, equal string", n, reps * n, glyph);
        std::uint64_t sink = 0;
        const Cost cv = measure([&] {
            for (long r = 0; r < reps; ++r) sink ^= s.content_version();
        });
        row("content_version()", n, reps * n, cv, sink ? "" : " ");
    }
}

// ===========================================================================
// 10. A topology in motion: every frame the guest of a Live portal gains an
//     element and loses one, so the OnChange memo is built again each frame;
//     1% of the tokens edited too.
// ===========================================================================
void churn() {
    header("10. churn: Live portal, guest +1/-1 element each frame (frames)");
    for (long n : sizes_big("churn")) {
        const long frames = frames_for(n, 2e6, 5, 300);
        std::vector<double> result[2];
        Cost cost[2];
        std::size_t carried[2] = {};
        const sg::Propagation modes[2] = {sg::Propagation::OnChange, sg::Propagation::Continuous};
        const sg::Key extra[2] = {"extra0", "extra1"};
        for (int m = 0; m < 2; ++m) {
            auto w = portal_world(n, sg::EmbedSync::Live, modes[m]);
            sg::State& guest = w->graph->state("guest");
            w->engine->run_fixed(kDt, 2);
            g_carried = 0;
            cost[m] = measure([&] {
                for (long f = 0; f < frames; ++f) {
                    guest.add_element(extra[f % 2], sg::kinds::sprite);
                    guest.remove_element(extra[(f + 1) % 2]);
                    mutate(w->edited, Mutation::Sparse, f);
                    w->engine->tick(kDt);
                }
            });
            carried[m] = g_carried;
            result[m] = positions(w->shown);
        }
        for (int m = 0; m < 2; ++m) {
            char note[128];
            std::snprintf(note, sizeof note, "carried %.0f/frame", double(carried[m]) / frames);
            std::string s = note;
            if (m == 1) {
                const double d = cost[0].secs / cost[1].secs;
                char cmp[64];
                std::snprintf(cmp, sizeof cmp, "  OnChange %s by %.0f%%", d <= 1 ? "wins" : "loses",
                              d <= 1 ? (1 / d - 1) * 100 : (d - 1) * 100);
                s += cmp;
                s += result[0] == result[1] ? "  worlds equal: OK" : "  worlds equal: MISMATCH";
            }
            row(m == 0 ? "OnChange   churn" : "Continuous churn", n, frames, cost[m], s);
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> only;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--quick") == 0) g_quick = true;
        else if (std::strcmp(argv[i], "--big") == 0) g_big = true;
        else only.emplace_back(argv[i]);  // case numbers to run: `sg_bench_scale 3 8`
    }
    const auto want = [&](const char* k) {
        return only.empty() || std::find(only.begin(), only.end(), k) != only.end();
    };
    std::printf("stategine scale bench%s  (alloc/op, B/op: operator new calls and bytes per op)\n",
                g_quick ? " --quick" : g_big ? " --big" : "");
    const auto t0 = Clock::now();
    if (want("1")) idle_world();
    if (want("2")) active_world();
    if (want("3")) portal_case(sg::EmbedSync::Live);
    if (want("4")) portal_case(sg::EmbedSync::View);
    if (want("5")) many_embeddings();
    if (want("6")) deep_composition();
    if (want("7")) validation();
    if (want("8")) laws_case();
    if (want("9")) params_set();
    if (want("10")) churn();
    std::printf("\ntotal %.1f s\n", std::chrono::duration<double>(Clock::now() - t0).count());
    return 0;
}
