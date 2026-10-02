#include "sg/core/Engine.hpp"
#include "sg/core/Laws.hpp"
#include "sg/dsl/Facts.hpp"
#include "sg/dsl/Natives.hpp"
#include "sg/dsl/Runtime.hpp"
#include "sg/net/Protocol.hpp"
#include "sg/render/ViewPlan.hpp"
#include <iostream>
namespace sgen {
void build_view_plan(sg::StateGraph &, const sg::dsl::Natives &, sg::dsl::Bindings &);
}
int main() {
    try {
        using namespace sg::net;
        Seed a{}, b{};
        a[0] = 1;
        b[0] = 2;
        SigningKey ka(a), kb(b);
        Committee committee{"region", 0, {{"a", ka.public_key()}, {"b", kb.public_key()}}, 2, 0};
        Layout layout{{0, 1, 2}, {0, 1}, {0, 2}, {0, 1}, {-1, 1}, {0, 1, 2}, {0, 0}, {0, 1}};
        LinearSystem system{layout};
        system.observations = {2, 10};
        system.confidence = {0, 0};
        system.fixed = {0, 0};
        system.pins = {0, 0};
        system.overlap_weights = {1};
        SolverSpec spec;
        system.iterations = spec.iterations;
        EpochContext context{"world",   "region",       0, 0, topology_hash(layout), constraint_hash(system, {}),
                             spec.id(), committee.id(), {}};
        Observation oa{"a", context.id(), 0, 0, "a", "value", false, {}, Bytes{2}, {}},
            ob{"b", context.id(), 0, 0, "b", "value", false, {}, Bytes{10}, {}};
        oa.sign(ka);
        ob.sign(kb);
        const auto problem = Problem::make(context, system, spec, {ob, oa});
        Verify verify;
        CpuBackend cpu;
        const auto proposal = verify.propose(problem, committee, "a", ka, cpu);
        const auto checked = verify.proposal(problem, committee, proposal);
        if (!checked)
            throw std::runtime_error("canonical verification");
        Agreement agreement(committee);
        agreement.attest(*checked, "a", ka);
        agreement.attest(*checked, "b", kb);
        const auto final = agreement.finalize(*checked, proposal);
        if (!final || !agreement.accept(problem, verify, *final))
            throw std::runtime_error("quorum finalization");
        sg::StateGraph graph;
        sg::dsl::Bindings bindings;
        sgen::build_view_plan(graph, {}, bindings);
        sg::Engine engine(graph);
        engine.set_strict(true);
        engine.start();
        engine.tick(0.25);
        sg::StateGraph paced;
        sg::dsl::Bindings other;
        sgen::build_view_plan(paced, {}, other);
        sg::Engine engine2(paced);
        engine2.set_strict(true);
        engine2.start();
        engine2.tick(.25);
        for (int step = 0; step < 7; ++step) {
            const sg::Event input{"room.move", sg::Params{}.set("dx", static_cast<double>(step) * .01)};
            engine.send("room", input);
            engine2.send("room", input);
            engine.tick(.125);
            engine2.tick(.125);
            for (int i = 0; i < step * 3; ++i) {
                auto plan = sg::render::view_plan(graph, graph.state("room"));
                (void)plan;
            }
            for (int i = 0; i < 19 - step; ++i) {
                auto plan = sg::render::view_plan(paced, paced.state("room"));
                (void)plan;
            }
            if (sg::dsl::facts(graph) != sg::dsl::facts(paced))
                throw std::runtime_error("render cadence changed Temporal/input sequence");
        }
        if (!graph.validate().empty() || !sg::verify(graph).holds())
            throw std::runtime_error("graph laws");
        Bytes facts;
        for (const auto &fact : sg::dsl::facts(graph)) {
            facts.insert(facts.end(), fact.begin(), fact.end());
            facts.push_back('\n');
        }
        const auto bytes = oa.statement();
        if (!check_signature(ka.public_key(), bytes, oa.signature))
            throw std::runtime_error("signature");
        std::cout << "{\"facts\":\"" << hex(hash(facts)) << "\",\"input\":\"" << hex(hash(bytes))
                  << "\",\"signature\":\"" << hex(Bytes(oa.signature.begin(), oa.signature.end())) << "\",\"epoch\":\""
                  << hex(problem.epoch.id()) << "\",\"result\":\"" << hex(checked->result()) << "\",\"receipt\":\""
                  << hex(final->id()) << "\"}\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
