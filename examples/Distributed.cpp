#include "Distributed.hpp"
#include "sg/core/StateGraph.hpp"
#include "sg/dsl/Compile.hpp"
#include "sg/dsl/Apply.hpp"
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <set>
#include <stdexcept>

namespace sg::examples {
namespace {
void assign(State& state, const Params& args, bool initial) {
    for (const auto key : {Key{"async"}, Key{"max_staleness"}, Key{"diffusion_step"}})
        if (args.has(key)) state.params().set(key, args.get(key));
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
Params placement_arguments(const net::PlacementPlan& plan, Params args) {
    if (!plan.feasible || plan.generation > static_cast<std::uint64_t>(INT64_MAX)) throw std::invalid_argument("net: cannot apply an infeasible placement");
    args.set("generation",static_cast<std::int64_t>(plan.generation));
    for (const auto& assignment : plan.assignments) args.set(assignment.stalk,assignment.peer);
    std::ostringstream source; source << std::setprecision(17) << "state network {\n";
    for (const auto& i : plan.interest) {
        if (i.action == net::InterestAction::Remove) { args.set(Key{"remove_overlap_"+i.overlap.str()},true); continue; }
        if (i.action == net::InterestAction::Keep && i.constraint.empty()) continue;
        source << "element " << std::quoted(i.overlap.str()) << " : constraint {\n";
        auto params = i.constraint.all(); std::sort(params.begin(),params.end(),[](const auto& a,const auto& b){return a.first < b.first;});
        for (const auto& [key,value] : params) {
            source << std::quoted(key.str()) << " = ";
            if (const auto* text = std::get_if<std::string>(&value)) source << std::quoted(*text);
            else if (const auto* number = std::get_if<double>(&value); number && std::isfinite(*number)) source << *number;
            else if (const auto* integer = std::get_if<std::int64_t>(&value)) source << "int(" << *integer << ')';
            else if (const auto* flag = std::get_if<bool>(&value)) source << (*flag ? "true" : "false");
            else throw std::invalid_argument("net: unsupported constraint notation value");
            source << '\n';
        }
        source << "}\n";
    }
    source << "}\n"; args.set("interest_source",source.str()); return args;
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
    n.arrow("configure", [](State& s, Element&, Element*, const Event& e) { if (e.args.text("peer") || e.args.has("async")) assign(s, e.args, true); });
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
        auto previous = state.snapshot();
        try {
        if (const auto* source = e.args.text("interest_source")) {
            auto compiled = dsl::compile_source(*source,"<declared interest edit>");
            if (!compiled.ok()) throw std::invalid_argument(compiled.report());
            std::set<Key> constraints;
            for (const auto& element : state.elements()) if (element.kind == Key{"constraint"}) constraints.insert(element.id);
            for (auto& step : compiled.plan.steps) {
                if (const auto* owner = std::get_if<dsl::plan::State>(&step); owner && owner->id == Key{"network"}) step = dsl::plan::Extern{owner->id,"state"};
                else if (auto* element = std::get_if<dsl::plan::Element>(&step); element && element->state == Key{"network"} && element->kind == Key{"constraint"}) {
                    const auto* existing = state.find(element->id);
                    if (existing && existing->kind != Key{"constraint"}) throw std::invalid_argument("net: an interest edit cannot replace a participant");
                    element->own = existing != nullptr; constraints.insert(element->id);
                }
                else if (const auto* param = std::get_if<dsl::plan::Param>(&step); param && param->state == Key{"network"} && constraints.count(param->element)) {}
                else throw std::invalid_argument("net: an interest edit declares only network constraints");
            }
            const auto applied = dsl::apply(compiled.plan,g,{});
            if (!applied.ok) throw std::invalid_argument(applied.why);
        }
        std::vector<Key> removed;
        for (const auto& element : state.elements()) if (element.kind == Key{"constraint"} && e.args.get_or<bool>(Key{"remove_overlap_"+element.id.str()},false)) removed.push_back(element.id);
        for (auto id : removed) state.remove_element(id); // dynamic removal, only through this edit
        net::Cellular validate; validate.derive(state);
        assign(state, e.args, false); state.params().set("generation", generation); return Params{};
        } catch (...) { state.restore(std::move(previous)); throw; }
    });
    return n;
}
} // namespace sg::examples
