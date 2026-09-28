// Stategine - the laws, compiled: the same answers, faster.
//
// What the algebra layer (sg/algebra) says of an equation must be what the
// verifier says: every report here is made twice - by running every
// equation, and with an accelerator that compiles what it can - and the two
// must be the same, on graphs that keep their laws and on graphs built to
// break them. Then the time each takes.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "sg/algebra/Compile.hpp"
#include "sg/gpu/AlgebraBackend.hpp"
#include "sg/sg.hpp"

// The kernel the CUDA and HIP backends run (src/gpu/job.inl), compiled for
// this machine, a loop standing in for the device's threads: held to the
// reference here, where there is no device to hold it to.
namespace hostkernel {
uint32_t thread = 0;
#define SG_DEVICE
#define SG_GLOBAL inline
#define SG_THREAD_INDEX ::hostkernel::thread
#include "../src/gpu/job.inl"
#undef SG_DEVICE
#undef SG_GLOBAL
#undef SG_THREAD_INDEX
}  // namespace hostkernel

namespace {

int failures = 0;

class HostKernelBackend : public sg::algebra::Backend {
public:
    std::string name() const override { return "cuda-kernel-on-host"; }
    bool available() const override { return true; }
    std::vector<sg::algebra::Verdict> run(const sg::algebra::Batch& b) override {
        std::vector<uint32_t> jobs, first;
        uint32_t scratch = 0;
        for (const auto& j : b.jobs) {
            const uint32_t f[8] = {j.lhs_first, j.lhs_count, j.rhs_first, j.rhs_count, j.x_first, j.slots, j.cmp_first, j.cmp_count};
            jobs.insert(jobs.end(), f, f + 8);
            first.push_back(scratch);
            scratch += 2 * j.slots;
        }
        std::vector<double> sc(scratch);
        std::vector<uint8_t> agree(b.size());
        std::vector<uint32_t> where(b.size());
        for (uint32_t t = 0; t < b.size(); ++t) {
            hostkernel::thread = t;
            hostkernel::sg_run_jobs(static_cast<uint32_t>(b.size()), jobs.data(), b.row_slot.data(), b.row_first.data(), b.row_count.data(),
                                    b.row_bias.data(), b.term_col.data(), b.term_k.data(), b.start.data(), b.cmp_slot.data(),
                                    b.cmp_how.data(), first.data(), sc.data(), agree.data(), where.data());
        }
        std::vector<sg::algebra::Verdict> out(b.size());
        for (std::size_t i = 0; i < out.size(); ++i) out[i] = sg::algebra::Verdict{agree[i], where[i]};
        return out;
    }
};

void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "[ok]  " : "[FAIL]", what.c_str());
    if (!ok) ++failures;
}

std::vector<std::string> lines(const sg::LawReport& r) {
    std::vector<std::string> out;
    for (const auto& v : r.violations) out.push_back("violation " + v.str());
    for (const auto& v : r.unchecked) out.push_back("unchecked " + v.str());
    for (const auto& v : r.bounded) out.push_back("bounded " + v.str());
    for (const auto& s : r.structure) out.push_back("structure " + s);
    std::sort(out.begin(), out.end());
    return out;
}

// The same report, however it was found.
bool same(const sg::LawReport& a, const sg::LawReport& b) {
    const auto x = lines(a), y = lines(b);
    if (x == y) return true;
    std::printf("  plain (%zu):\n", x.size());
    for (const auto& s : x) std::printf("    %s\n", s.c_str());
    std::printf("  accelerated (%zu):\n", y.size());
    for (const auto& s : y) std::printf("    %s\n", s.c_str());
    return false;
}

void test_operators() {
    using namespace sg::algebra;
    Operator a;
    a.set(0, {Term{0, 1.0}, Term{1, 0.5}});  // x <- x + v/2
    a.copy(0, 2);                           // z <- x
    Program p{3, a};
    const auto y = p.apply({1.0, 4.0, 0.0});
    check(y[0] == 3.0 && y[1] == 4.0 && y[2] == 3.0, "an operator is its rows, run in turn");
    Program twice = Program::compose(p, p);
    check(twice.apply({1.0, 4.0, 0.0})[0] == 5.0, "and two compose by running one after the other");
    const auto d = a.dense(3);
    const std::vector<double> x{1.0, 4.0, 0.0, 1.0};
    double z = 0;
    for (int j = 0; j < 4; ++j) z += d[2 * 4 + j] * x[j];
    check(z == 3.0, "as one matrix, the same");
    const auto t = p.adjoint();
    check(t[1 * 3 + 0] == 0.5 && t[0 * 3 + 1] == 0.0, "its adjoint is the transpose of what it does");
    check(same(Compare::Angle, 0.1, 0.1 + 6.283185307179586) && !same(Compare::Exact, 1.0, 1.0 + 1e-12) &&
              same(Compare::Number, 1.0, 1.0 + 1e-9),
          "values are held the same as the laws hold them");
}

// A world of bodies that integrate, and maps of it by transports that say
// what they do: a lawful graph, most of whose equations compile.
void build(sg::StateGraph& g, int bodies, bool broken) {
    auto& room = g.add<sg::Spatial3D>("room");
    auto& map = g.add<sg::Spatial3D>("map");
    auto& notes = g.add<sg::State>("notes");
    for (int i = 0; i < bodies; ++i) {
        sg::Element& b = room.body(sg::Key{"b" + std::to_string(i)}, {1.0 * i, 0.5, -2.0 * i});
        b.params.set(sg::keys::vx, 0.3 + i).set(sg::keys::vy, -0.1).set(sg::keys::vz, 0.25 * i).set(sg::keys::yaw, 0.1 * i);
        b.params.set("label", std::string("body ") + std::to_string(i)).set("lit", i % 2 == 0);
        map.body(sg::Key{"m" + std::to_string(i)}, {0, 0, 0});
    }
    // The map: x as it is, the world's z as its y; the rest renamed.
    sg::Functor& f = g.add_functor("room.map", "room", "map");
    for (int i = 0; i < bodies; ++i) {
        const sg::Key s{"b" + std::to_string(i)}, d{"m" + std::to_string(i)};
        if (broken && i == 1) {
            // Not a map of the motion: x crosses as x, but its speed as
            // the world's height's.
            f.on_object(s, d, sg::transport::swizzle({{sg::keys::x, sg::keys::x}, {sg::keys::vx, sg::keys::vy}}));
        } else {
            f.on_object(s, d, sg::transport::swizzle({{sg::keys::x, sg::keys::x}, {sg::keys::y, sg::keys::z}, {sg::keys::z, sg::keys::y},
                                                     {sg::keys::vx, sg::keys::vx}, {sg::keys::vy, sg::keys::vz}, {sg::keys::vz, sg::keys::vy},
                                                     {sg::keys::yaw, sg::keys::yaw}}));
        }
        f.on_morphism(sg::Key{"move." + s.str()}, sg::Key{"move." + d.str()});
    }
    // A lens: the room's labels on the notes, and back.
    sg::Functor& get = g.add_functor("notes.get", "room", "notes");
    sg::Functor& put = g.add_functor("notes.put", "notes", "room");
    for (int i = 0; i < std::min(bodies, 8); ++i) {
        const sg::Key s{"b" + std::to_string(i)}, n{"n" + std::to_string(i)};
        notes.add_element(n, "note");
        get.on_object(s, n, sg::transport::only({sg::Key{"label"}, sg::Key{"lit"}}));
        put.on_object(n, s, sg::transport::only({sg::Key{"label"}, sg::Key{"lit"}}));
    }
    g.lens("notes.get", "notes.put");
    // An arrow that says nothing of what it does: run, as ever.
    room.loop("tidy", "b0", "tidy", [](sg::State&, sg::Element& e, sg::Element*, const sg::Event&) {
        e.params.set("label", std::string("tidied"));
    });
    // A registered composite of declared arrows, and one that lies.
    if (bodies >= 1) {
        const sg::Key m{"move.b0"};
        room.compose("move.b0.twice", m, m, room.step_event());
    }
    g.set_initial("room");
}

sg::LawOptions probe() {
    sg::LawOptions o;
    o.args.set(sg::keys::dt, 0.25);
    o.max_triples = 64;
    return o;
}

void test_the_same_reports() {
    for (bool broken : {false, true}) {
        sg::StateGraph g;
        build(g, 6, broken);
        const sg::LawReport plain = sg::verify(g, {}, probe());
        sg::algebra::CpuBackend cpu;
        sg::algebra::Accelerated fast(cpu);
        sg::LawOptions o = probe();
        o.accelerate = &fast;
        const sg::LawReport accelerated = sg::verify(g, {}, o);
        const auto& st = fast.stats();
        check(same(plain, accelerated), std::string(broken ? "a broken graph" : "a lawful graph") +
                                            ": the accelerated report is the verifier's (" + std::to_string(st.taken) + " of " +
                                            std::to_string(st.offered) + " equations compiled, " + std::to_string(st.broken) +
                                            " found broken and checked again)");
        check(st.taken > 0 && st.refused > 0, "some equations compile, and those that say nothing are run as before");
        if (broken) check(!plain.ok() && st.broken > 0, "and what is broken is found broken by both");
        // Again: from the cache.
        fast.reset_stats();
        const sg::LawReport again = sg::verify(g, {}, o);
        check(same(plain, again) && fast.stats().reused > 0 && fast.stats().compiled == 0,
              "verified again, the programs are the ones compiled before");
    }
    // Moved - the values, not the make-up: the same programs, new answers.
    sg::StateGraph g;
    build(g, 4, false);
    sg::algebra::CpuBackend cpu;
    sg::algebra::Accelerated fast(cpu);
    sg::LawOptions o = probe();
    o.accelerate = &fast;
    (void)sg::verify(g, {}, o);
    g.state("room").element("b2").params.set(sg::keys::vx, 9.0);
    fast.reset_stats();
    check(same(sg::verify(g, {}, probe()), sg::verify(g, {}, o)) && fast.stats().reused > 0,
          "a value changed: the compiled programs stand, run on the new values");
}

// Sides that are different sums of the same values: equal as numbers, or
// not - the batch computes them, and says which (and the verifier, where
// they part).
void test_the_batch_computes() {
    sg::StateGraph g;
    auto& s = g.add<sg::State>("s");
    s.add_element("p", "point").params.set("x", 1.5).set("v", 0.25);
    const sg::Affine::Term x{sg::Key{"x"}, 1.0, sg::Key{}, true};
    s.affine("step", "p", "tick", sg::Affine{}.set("x", {x, sg::Affine::Term{sg::Key{"v"}, 1.0, sg::keys::dt, true}}));
    s.affine("leap", "p", "tick", sg::Affine{}.set("x", {x, sg::Affine::Term{sg::Key{"v"}, 2.0, sg::keys::dt, true}}));
    s.affine("lurch", "p", "tick", sg::Affine{}.set("x", {x, sg::Affine::Term{sg::Key{"v"}, 2.5, sg::keys::dt, true}}));
    g.set_initial("s");
    sg::Diagram d("stride");
    d.commutes(sg::Path("s", "p").arrow("step").arrow("step"), sg::Path("s", "p").arrow("leap"), sg::Params{}.set(sg::keys::dt, 0.1));
    d.commutes(sg::Path("s", "p").arrow("step").arrow("step"), sg::Path("s", "p").arrow("lurch"), sg::Params{}.set(sg::keys::dt, 0.1));
    auto all = sg::gpu::backends();
    all.push_back(std::make_unique<HostKernelBackend>());
    for (const auto& b : all) {
        sg::algebra::Accelerated fast(*b);
        sg::LawOptions o;
        o.accelerate = &fast;
        const sg::LawReport plain = sg::verify(g, {d});
        const sg::LawReport accelerated = sg::verify(g, {d}, o);
        check(same(plain, accelerated) && plain.violations.size() == 1 && fast.stats().broken == 1,
              b->name() + ": two steps are one leap - computed, and held the same; not a lurch - computed, and found apart");
    }
}

// Every backend says what the reference says.
void test_backends() {
    sg::StateGraph g;
    build(g, 5, true);
    sg::algebra::CpuBackend cpu;
    auto all = sg::gpu::backends();
    all.push_back(std::make_unique<HostKernelBackend>());
    std::string seen;
    for (const auto& b : all) {
        seen += (seen.empty() ? "" : ", ") + b->name() + (b->available() ? "" : " (not here)");
        sg::algebra::Accelerated fast(*b);
        sg::LawOptions o = probe();
        o.accelerate = &fast;
        check(same(sg::verify(g, {}, probe()), sg::verify(g, {}, o)), "the " + b->name() + " backend's report is the verifier's");
    }
    check(!all.empty(), "backends: " + seen);
    check(sg::gpu::best()->available(), "and the best one here is one that runs: " + sg::gpu::best()->name());
}

void test_speed() {
    sg::StateGraph g;
    build(g, 400, false);
    sg::LawOptions plain_o = probe();
    plain_o.max_triples = 4096;
    const auto t0 = std::chrono::steady_clock::now();
    const sg::LawReport plain = sg::verify(g, {}, plain_o);
    const double plain_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    auto backend = sg::gpu::best();
    sg::algebra::Accelerated fast(*backend);
    sg::LawOptions o = plain_o;
    o.accelerate = &fast;
    const auto t1 = std::chrono::steady_clock::now();
    const sg::LawReport first = sg::verify(g, {}, o);
    const double first_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
    const auto t2 = std::chrono::steady_clock::now();
    const sg::LawReport again = sg::verify(g, {}, o);
    const double again_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t2).count();
    const auto& st = fast.stats();
    std::printf("        400 bodies: plain %.1f ms; accelerated (%s) %.1f ms first, %.1f ms again (%zu/%zu compiled, %zu held by what they are; "
                "compile %.1f, run %.1f ms)\n",
                plain_ms, backend->name().c_str(), first_ms, again_ms, st.taken / 2, st.offered / 2, st.proved / 2, st.compile_ms, st.run_ms);
    check(same(plain, first) && same(plain, again), "at 400 bodies, the same report");
    check(again_ms < plain_ms, "and faster, once compiled");
}

}  // namespace

int main() {
    test_operators();
    test_the_same_reports();
    test_the_batch_computes();
    test_backends();
    test_speed();
    std::printf("\n%s\n", failures == 0 ? "the compiled laws say what the laws say" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
