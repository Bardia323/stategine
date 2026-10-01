#include "sg/net/Reconcile.hpp"
#include "sg/dsl/Apply.hpp"
#include "sg/dsl/Compile.hpp"
#include "sg/core/Engine.hpp"
#include "sg/core/Laws.hpp"
#include "sg/core/Text.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace sgen { void build_network_numerics(sg::StateGraph&, const sg::dsl::Natives&, sg::dsl::Bindings&); }
namespace {
bool near(double a, double b) { return std::fabs(a - b) < 1e-8; }
std::size_t coordinate(const sg::net::Cellular& c, sg::Key id, std::uint32_t dimension = 0) {
    for (std::size_t i = 0; i < c.stalks().size(); ++i)
        if (c.stalks()[i].element == id) return c.layout().stalk_offsets[i] + dimension;
    throw std::runtime_error("no test coordinate");
}
sg::dsl::Natives natives() {
    sg::dsl::Natives n;
    n.arrow("request_join", [](sg::State& s, sg::Element&, sg::Element*, const sg::Event&) { s.emit("samples.join.edit"); });
    n.arrow("request_leave", [](sg::State& s, sg::Element&, sg::Element*, const sg::Event&) { s.emit("samples.leave.edit"); });
    n.edit("add_sample", [](sg::StateGraph& g, const sg::Event&) {
        auto source = sg::dsl::compile_source(R"(
state samples {
    element added : participant { observation = 5.0 }
    element added.edge : constraint { left = "agree.a" right = "added" }
}
)", "<network join>");
        if (!source.ok()) throw std::runtime_error(source.report());
        // Bind the compiled element declarations to the existing owner.
        // The parser cannot give an extern state a body; this edit replaces
        // only the construction of that owner by the existing primitive.
        for (auto& step : source.plan.steps)
            if (const auto* owner = std::get_if<sg::dsl::plan::State>(&step)) {
                const auto id = owner->id;
                step = sg::dsl::plan::Extern{id, "state"};
            }
        const auto applied = sg::dsl::apply(source.plan, g, {});
        if (!applied.ok) throw std::runtime_error(applied.why);
        return sg::Params{};
    });
    n.edit("remove_sample", [](sg::StateGraph& g, const sg::Event&) {
        // Element removal is dynamic construction the notation cannot express.
        g.state("samples").remove_element("added.edge");
        g.state("samples").remove_element("added");
        return sg::Params{};
    });
    return n;
}
}
int main() {
    int failures = 0;
    const auto check = [&](bool ok, const char* name) { std::printf("%s %s\n", ok ? "ok" : "FAIL", name); failures += !ok; };
    sg::StateGraph graph; sg::dsl::Bindings bindings;
    sgen::build_network_numerics(graph, natives(), bindings);
    sg::Engine engine(graph); engine.set_strict(true); engine.start();
    auto& state = graph.state("samples");
    sg::net::Cellular cellular;
    auto system = cellular.gather(state);
    const sg::net::CpuBackend cpu;
    const auto x = cpu.solve(system);
    const auto at = [&](sg::Key id, std::uint32_t c = 0) { return x[coordinate(cellular, id, c)]; };
    check(at("agree.a") == 3 && at("agree.b") == 3, "two agreeing stalks remain a zero-residual section");
    check(near(at("disagree.a"), 14.0/3) && near(at("disagree.b"), 22.0/3), "two disagreeing stalks solve the analytic penalized system");
    check(std::fabs(at("disagree.a") - at("disagree.b")) < 8, "solve reduces overlap disagreement");
    check(near(at("cycle.a"), 6) && near(at("cycle.b"), 6) && near(at("cycle.c"), 6), "three-stalk cycle converges to a compatible section");
    check(at("pinned.a") == 7 && near(at("pinned.b"), 7), "a hard boundary is fixed throughout the solve and influences its neighbor");
    check(at("coordinate.pin") == -3 && at("coordinate.pin", 1) == 5, "coordinate pins do not freeze another coordinate in the same stalk");
    check(near(at("block.a"), 234.0/61) && near(at("block.a", 1), 107.0/61) && near(at("block.b"), 433.0/61),
          "rectangular restrictions, comparison weights and coordinate confidence match the analytic solution");
    const auto r = cpu.residual(system, x);
    check(r.equation < 1e-8 && r.disagreement < cpu.residual(system, system.observations).disagreement, "matrix-free equation and disagreement residuals are distinct and correct");
    std::vector<double> delta, lx, ax;
    sg::net::coboundary(system.layout, x, delta); sg::net::laplacian(system, x, lx); sg::net::apply(system, x, ax);
    for (std::size_t e = 0; e < cellular.overlaps().size(); ++e)
        if (cellular.overlaps()[e].element == sg::Key{"agree"}) check(delta[system.layout.overlap_offsets[e]] == 0, "the agreeing pair has exactly zero coboundary residual");
    double energy = 0, weighted = 0;
    for (std::size_t i = 0; i < x.size(); ++i) energy += x[i] * lx[i];
    for (std::size_t i = 0; i < delta.size(); ++i) weighted += system.overlap_weights[i] * delta[i] * delta[i];
    check(near(energy, weighted), "delta transpose W delta has the declared quadratic energy");
    check(cpu.solve(system) == x, "fixed-count CPU solves are deterministic");
    auto no_steps = system;
    no_steps.iterations = 0;
    const auto initial = cpu.solve(no_steps);
    bool initial_ok = true;
    for (std::size_t i = 0; i < initial.size(); ++i) initial_ok &= initial[i] == (system.fixed[i] ? system.pins[i] : system.observations[i]);
    check(initial_ok, "zero iterations return supplied observations and boundary values");
    auto no_penalty = system;
    no_penalty.lambda = 0;
    check(cpu.solve(no_penalty) == initial, "zero penalty preserves observed free coordinates");
    auto fixed = system;
    std::fill(fixed.fixed.begin(), fixed.fixed.end(), 1);
    check(cpu.solve(fixed) == fixed.pins && cpu.residual(fixed, fixed.pins).equation == 0, "a fully pinned system has no free equation to solve");
    auto invalid = system;
    invalid.confidence.front() = -1;
    bool rejected = false;
    try { cpu.solve(invalid); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "negative confidence is refused instead of making an indefinite system");
    const auto original_layout = cellular.layout();
    const auto compiled = cellular.compilations();
    sg::net::CudaBackend gpu;
    std::vector<double> gpu_reference;
    if (gpu.available()) {
        check(gpu.solve(no_steps) == initial && gpu.solve(no_penalty) == initial && gpu.solve(fixed) == fixed.pins,
              "CUDA honors zero iterations, zero penalty and fixed-only boundaries");
        const auto gx = gpu.solve(system);
        gpu_reference = gx;
        bool same = gx.size() == x.size();
        for (std::size_t i = 0; same && i < x.size(); ++i) same = near(gx[i], x[i]);
        check(same && near(gpu.residual(system, gx).disagreement, r.disagreement), "real CUDA and reference CPU solutions agree within tolerance");
        const auto transfers = gpu.transfers();
        check(gpu.solve(system) == gx && gpu.transfers().downloaded_values == transfers.downloaded_values &&
              gpu.transfers().observation_uploads == transfers.observation_uploads && gpu.transfers().topology_uploads == transfers.topology_uploads,
              "unchanged solves retain GPU buffers and download no unchanged values");
    } else std::printf("SKIP CUDA equivalence: backend or device unavailable (no CPU fallback counted)\n");
    engine.send("samples", {"samples.receive", sg::Params{}.set("sample", 4.0)}); engine.tick(0);
    auto changed = cellular.gather(state);
    check(cellular.compilations() == compiled && cellular.layout() == original_layout && changed.observations[coordinate(cellular, "disagree.a")] == 4,
          "value-only input gathers fresh observations without recompiling topology");
    if (gpu.available()) {
        const auto before = gpu.transfers();
        const auto gx = gpu.solve(changed), cx = cpu.solve(changed);
        bool same = gx.size() == cx.size();
        for (std::size_t i = 0; same && i < cx.size(); ++i) same = near(gx[i], cx[i]);
        const auto after = gpu.transfers();
        check(same && after.topology_uploads == before.topology_uploads && after.observation_uploads == before.observation_uploads + 1 &&
              after.downloaded_values - before.downloaded_values == 2, "CUDA uploads one changed observation and downloads only the two changed solutions");
        check(gpu.solve(system) == gpu_reference, "restored numerical inputs reproduce the same solution after other GPU solves");
        gpu.solve(changed);
    }
    engine.send("samples", {"samples.confidence", sg::Params{}.set("sample", 2.0)}); engine.tick(0);
    auto confidence = cellular.gather(state);
    check(cellular.compilations() == compiled, "confidence changes do not rebuild the restriction layout");
    if (gpu.available()) {
        const auto before = gpu.transfers(); gpu.solve(confidence); const auto after = gpu.transfers();
        check(after.topology_uploads == before.topology_uploads && after.weight_uploads == before.weight_uploads + 1, "CUDA updates only changed confidence weights");
    }
    engine.send("samples", sg::Event{"samples.join"}); engine.tick(0);
    check(!state.find("added"), "join requests structural change without changing elements inside an arrow");
    engine.tick(0); auto joined = cellular.gather(state);
    check(state.find("added") && cellular.compilations() == compiled + 1 && joined.observations.size() == x.size() + 1,
          "graph.edit adds a participant and recompiles the numerical layout");
    if (gpu.available()) {
        const auto before = gpu.transfers(); gpu.solve(joined);
        check(gpu.transfers().topology_uploads == before.topology_uploads + 1, "CUDA refreshes resident topology after structural change");
    }
    engine.send("samples", sg::Event{"samples.leave"}); engine.tick(0); engine.tick(0);
    cellular.gather(state);
    check(!state.find("added") && cellular.compilations() == compiled + 2 && cellular.layout() == original_layout, "leave is a graph edit and restores the original derived layout");
    engine.send("samples", {"samples.restrict", sg::Params{}.set("sample", 3.0)}); engine.tick(0);
    cellular.gather(state);
    check(cellular.compilations() == compiled + 3, "restriction changes invalidate the compiled operator");
    sg::LawOptions probes; probes.args_for["samples.receive"].set("sample", 4.0); probes.args_for["samples.confidence"].set("sample", 2.0);
    probes.args_for["samples.restrict"].set("sample", 3.0);
    const auto laws = sg::verify(graph, {}, probes);
    if (!laws.holds()) std::printf("%s\n", laws.str().c_str());
    check(graph.validate().empty() && laws.holds(), "strict validation and sg::verify hold after input, join and leave");
    engine.stop();
    return failures ? 1 : 0;
}
