#include "sg/net/Reconcile.hpp"
#include "sg/net/Transport.hpp"
#include "sg/dsl/Runtime.hpp"
#include "sg/dsl/Natives.hpp"
#include "sg/dsl/Facts.hpp"
#include "sg/core/Engine.hpp"
#include "sg/core/Laws.hpp"
#include "sg/core/Text.hpp"
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace sgen { void build_network(sg::StateGraph&, const sg::dsl::Natives&, sg::dsl::Bindings&); }

namespace {
// Only a numerical adapter inside the DSL-declared arrow. It receives its
// own state, computes derived results, writes them and says they are ready.
sg::dsl::Natives natives(const sg::net::Backend* backend) {
    sg::dsl::Natives n;
    auto solver = std::make_shared<sg::net::Reconcile>();
    n.arrow("reconcile", [solver, backend](sg::State& state, sg::Element&, sg::Element*, const sg::Event& event) {
        if (event.args.num("dt") <= 0) return; // no elapsed time is the identity
        const auto result = solver->evaluate(state, backend);
        sg::Params message;
        for (const auto& reading : result.readings) {
            auto& params = state.element(reading.element).params;
            params.set(sg::net::Cellular::coordinate_key("result", reading.dimension, reading.coordinate), reading.value)
                  .set(sg::net::Cellular::coordinate_key("correction", reading.dimension, reading.coordinate), reading.correction);
            message.set(reading.element, reading.value);
        }
        state.params().set("residual", result.residual).set("equation_residual", result.equation_residual);
        state.emit({"network.agreed", std::move(message)});
    });
    return n;
}

// A byte sink used solely by an external observer, with no world capability.
class Sink final : public sg::net::Transport {
public:
    sg::net::Bytes sent;
    sg::net::SendResult send(sg::net::Outbound message) override { sent = std::move(message.bytes); return sg::net::SendResult::Accepted; }
    bool try_receive(sg::net::Inbound&) override { return false; }
};

bool near(double a, double b) { return std::fabs(a - b) < 1e-10; }
}

int scenario(const sg::net::Backend* executor) {
    int failures = 0;
    const auto check = [&](bool ok, const char* name) {
        std::printf("%s %s\n", ok ? "ok" : "FAIL", name);
        failures += !ok;
    };
    sg::StateGraph graph;
    sg::dsl::Bindings bindings;
    sgen::build_network(graph, natives(executor), bindings);
    check(graph.validate().empty(), "the DSL graph reaches both games, the network and its clock");
    sg::LawOptions probes;
    probes.args_for["network.reconcile"].set("dt", 0.1);
    probes.args_for["network.receive"].set("sample", 14.0);
    const auto laws = sg::verify(graph, {}, probes);
    if (!laws.ok()) std::printf("%s\n", laws.str().c_str());
    check(laws.holds(), "sg::verify passes without unchecked equations");

    sg::Engine engine(graph);
    engine.set_strict(true);
    sg::set_observers(sg::Observers::Strict);
    engine.start();
    const auto facts = sg::dsl::facts(graph, sg::dsl::Scope::Relations);
    const auto revision = graph.revision();
    auto& network = graph.state("network");
    Sink sink;
    network.bus().subscribe("network.agreed", [&](const sg::Event& event) {
        // The codec is outside the world; this observer serializes said data.
        const auto text = sg::to_string(event.args.get("a"));
        sink.send({"peer","result",sg::net::DeliveryClass::Reliable,{},sg::net::Bytes(text.begin(), text.end())});
    });

    engine.tick(0);
    check(near(network.element("a").params.num("observation"), 2) &&
          near(network.element("b").params.num("observation"), 10),
          "kept declared functors publish both incompatible local views");
    check(near(network.element("a").params.num("result"), 0) && sink.sent.empty(),
          "zero elapsed time neither reconciles nor sends traffic");
    const auto before = sg::to_text(network);
    check(sg::verify(graph, {}, probes).holds() && sg::to_text(network) == before && sink.sent.empty(),
          "laws exercise incompatible observations without changing reality or sending bytes");
    sg::net::Reconcile solver;
    const auto derived = solver.evaluate(network);
    const auto again = solver.evaluate(network);
    sg::net::CpuBackend backend;
    const auto other = solver.evaluate(network, &backend);
    check(derived.readings.size() == 2 && near(derived.readings[0].value, 14.0/3) &&
          near(derived.readings[1].value, 22.0/3) && derived.residual < 8 && derived.equation_residual < 1e-10,
          "CPU reconciliation solves the declared confidence and disagreement objective");
    check(sg::to_text(network) == before && derived.readings[0].value == again.readings[0].value &&
          derived.readings[0].value == other.readings[0].value,
          "reconciliation is repeatable, read-only and backend replaceable");

    engine.tick(0.1);
    check(near(network.element("a").params.num("result"), 14.0/3) &&
          near(network.element("b").params.num("result"), 22.0/3) &&
          near(network.element("a").params.num("correction"), 8.0/3) &&
          near(network.element("b").params.num("correction"), -8.0/3),
          "the Temporal-driven arrow stores results and corrections in its own state");
    const auto value = [&](const char* id) { return graph.state(id).element("view").params.num("value"); };
    check(near(value("game.a"), 14.0/3) && near(value("game.b"), 22.0/3),
          "the said event carries solved values through declared functors to both games");
    check(!sink.sent.empty(), "an external observer sends bytes from what the network says");
    for (int i = 0; i < 24; ++i) engine.tick(0.1);
    check(near(value("game.a"), 6) && near(value("game.b"), 6), "republication and reconciliation approach agreement");

    bool refused = false;
    try { engine.send("network", sg::Event{"network.undeclared"}); }
    catch (const std::runtime_error&) { refused = true; }
    check(refused, "remote input cannot bypass a declared port");
    engine.send("network", {"network.receive", sg::Params{}.set("sample", 14.0)});
    check(near(network.element("b").params.num("observation"), 6),
          "remote input is queued without mutating the state immediately");
    engine.tick(0.1);
    check(near(network.element("b").params.num("observation"), 14) &&
          near(value("game.a"), 26.0/3) && near(value("game.b"), 34.0/3),
          "Engine::send enters the declared arrow and the solver reduces disagreement again");
    check(graph.revision() == revision && sg::dsl::facts(graph, sg::dsl::Scope::Relations) == facts && graph.seams().empty(),
          "reconciliation changes no participants, graph structure or exact seams");
    for (int i = 0; i < 24; ++i) engine.tick(0.1);
    check(near(value("game.a"), 10) && near(value("game.b"), 10), "agreement stays fixed when published again");
    engine.stop();
    check(graph.validate().empty() && sg::verify(graph, {}, probes).holds(), "strict validation and the laws hold after agreement");
    return failures ? 1 : 0;
}
int main() {
    const sg::net::CpuBackend cpu;
    std::printf("CPU reconciliation through the declared arrow\n");
    int failed = scenario(&cpu);
    const sg::net::CudaBackend gpu;
    if (gpu.available()) {
        std::printf("CUDA reconciliation through the same declared arrow\n");
        failed += scenario(&gpu);
    } else std::printf("SKIP CUDA arrow: backend or device unavailable\n");
    return failed ? 1 : 0;
}
