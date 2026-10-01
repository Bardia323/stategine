#include "Distributed.hpp"
#include "sg/core/StateGraph.hpp"
#include <stdexcept>

namespace sg::examples {
namespace {
void assign(State& state, const Params& args, bool initial) {
    if (const auto* peer = args.text("peer")) state.params().set("peer", *peer);
    if (const auto* endpoint = args.text("endpoint")) state.params().set("endpoint", *endpoint);
    const auto peer = state.params().get_or<std::string>("peer", {});
    for (auto& element : state.elements()) {
        if (element.kind != Key{"participant"}) continue;
        if (const auto* solver = args.text(element.id)) element.params.set("solver", *solver);
        const bool computes = element.params.get_or<std::string>("solver", peer) == peer;
        element.params.set("computes", computes);
        if (const auto key = Key{"handoff_" + element.id.str()}; computes && args.has(key)) element.params.set("result", args.get(key));
        if (initial && !computes && args.get_or<bool>("omit_remote", false)) element.params.erase("observation");
    }
    state.params().set("round", std::int64_t{0});
}
}
sg::dsl::Natives distributed_natives(const net::Backend* backend, std::shared_ptr<net::Distributed> solver) {
    sg::dsl::Natives n;
    n.transport("publish", [](const Element& from, Element& to) {
        if (from.params.has("value")) to.params.set("observation", from.params.get("value"));
    });
    n.transport("correct", [](const Element& from, Element& to) {
        if (from.params.get_or<bool>("computes", true) && from.params.has("result")) to.params.set("value", from.params.get("result"));
    });
    n.arrow("publish_request", [](State& s, Element&, Element*, const Event&) { s.emit("game.published"); });
    n.arrow("configure", [](State& s, Element&, Element*, const Event& e) { if (e.args.text("peer")) assign(s, e.args, true); });
    n.arrow("receive", [solver](State& s, Element&, Element*, const Event& e) {
        if (!e.args.has("packet")) return; // identity event used by structural probes
        const auto packet = net::Exchange::decode(net::Exchange::bytes(e.args));
        const auto changes = solver->receive(s, packet);
        for (const auto& change : changes) s.element(change.element).params = change.params;
    });
    n.arrow("distributed", [solver, backend](State& s, Element&, Element*, const Event& e) {
        if (e.args.num("dt") <= 0) return;
        const auto result = solver->evaluate(s, backend);
        s.params() = result.params;
        for (const auto& change : result.boundaries) s.element(change.element).params = change.params;
        if (result.advanced) {
            for (const auto& reading : result.section.readings) {
                auto& p = s.element(reading.element).params;
                p.set(net::Cellular::coordinate_key("result", reading.dimension, reading.coordinate), reading.value)
                 .set(net::Cellular::coordinate_key("correction", reading.dimension, reading.coordinate), reading.correction);
            }
            s.params().set("residual", result.section.residual).set("equation_residual", result.section.equation_residual);
            s.emit("network.solved");
        }
        for (const auto& packet : result.outgoing) s.emit({"network.boundary", net::Exchange::arguments(net::Exchange::encode(packet))});
    });
    n.arrow("begin", [](State& s, Element&, Element*, const Event& e) {
        const auto epoch = net::Exchange::step(e.args).epoch;
        if (epoch <= net::Exchange::step(s).epoch) return;
        s.params().set("epoch", epoch).set("round", std::int64_t{0});
        for (auto& element : s.elements())
            if (element.kind == Key{"participant"} && element.params.get_or<bool>("computes", true)) element.params.set("result", element.params.get("observation"));
    });
    n.arrow("repartition_request", [](State& s, Element&, Element*, const Event& e) { s.emit({"network.repartition.edit", e.args}); });
    n.edit("repartition", [](StateGraph& g, const Event& e) {
        auto& state = g.state("network");
        const auto generation = net::Exchange::step(e.args).generation;
        if (generation <= net::Exchange::step(state).generation) return Params{};
        assign(state, e.args, false); state.params().set("generation", generation); return Params{};
    });
    return n;
}
} // namespace sg::examples
