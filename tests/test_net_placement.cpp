#include "../examples/Distributed.hpp"
#include "sg/net/Agreement.hpp"
#include "sg/core/Engine.hpp"
#include "sg/core/Laws.hpp"
#include "sg/core/Text.hpp"
#include "sg/dsl/Runtime.hpp"
#include <iostream>
#include <set>
namespace sgen { void build_distributed(sg::StateGraph&,const sg::dsl::Natives&,sg::dsl::Bindings&); }
namespace {
using namespace sg; using namespace sg::net;
int failures = 0;
void check(bool good,const char* why) { std::cout << (good ? "ok " : "FAIL ") << why << '\n'; failures += !good; }
bool connected(const Cellular& c,const PlacementPlan& plan) {
    std::map<Key,std::string> owners; std::set<std::string> peers;
    for (const auto& a : plan.assignments) { owners[a.stalk] = a.peer; peers.insert(a.peer); }
    for (const auto& peer : peers) {
        std::set<Key> all,seen; for (const auto& a : plan.assignments) if (a.peer == peer) all.insert(a.stalk);
        std::vector<Key> queue{*all.begin()}; seen.insert(queue[0]);
        for (std::size_t i = 0; i < queue.size(); ++i) for (const auto& e : c.overlaps()) {
            const auto left = c.stalks()[e.left].element,right = c.stalks()[e.right].element;
            const auto next = left == queue[i] ? right : right == queue[i] ? left : Key{};
            if (all.count(next) && seen.insert(next).second) queue.push_back(next);
        }
        if (seen != all) return false;
    }
    return true;
}
}
int main() {
    set_observers(Observers::Strict);
    StateGraph graph; dsl::Bindings bindings; auto solver = std::make_shared<Distributed>();
    sgen::build_distributed(graph,examples::distributed_natives(nullptr,solver),bindings);
    Engine engine(graph); engine.set_strict(true); engine.start(); engine.fire("game.publish"); engine.tick(0.01);
    auto& state = graph.state("network"); Cellular c; auto system = c.gather(state);
    Seed seed1{},seed2{}; seed1[0] = 1; seed2[0] = 2; SigningKey k1(seed1),k2(seed2);
    Committee committee{"region",0,{{"p0",k1.public_key()},{"p1",k2.public_key()}},2,0};
    SolverSpec spec; spec.iterations = system.iterations; spec.residual_tolerance = 100;
    auto problem = Problem::make({"world","region",0,0,{},{},{},committee.id(),{}},system,spec,{}, {},topology_description(c));
    Verify verify; const auto proposal = verify.propose(problem,committee,"p0",k1,CpuBackend{}); const auto verified = verify.proposal(problem,committee,proposal);
    Agreement agreement(committee); agreement.attest(*verified,"p0",k1); agreement.attest(*verified,"p1",k2);
    const auto final = agreement.finalize(*verified,proposal); agreement.accept(problem,verify,*final);
    FinalizedPlacementView view{state,final->id(),10,0,0};
    PlacementMetrics metrics; metrics.peers = {{{"p0",{}},6,true},{{"p1",{}},6,true}}; metrics.migration_penalty = 0;
    metrics.stalks = {{"b",4,1}};
    const auto before = to_text(state); const auto hot = Placement::plan(view,c,metrics);
    auto reordered = metrics; std::reverse(reordered.peers.begin(),reordered.peers.end());
    const auto again = Placement::plan(view,c,reordered);
    check(examples::placement_arguments(hot).all() == examples::placement_arguments(again).all() && hot.estimated_load == again.estimated_load && hot.objective == again.objective,"stable IDs and sorting produce identical deterministic placement plans");
    check(hot.feasible && hot.changed && hot.generation == 1 && connected(c,hot),"hotspot splits into connected execution regions");
    bool capacity = true; for (const auto& [peer,load] : hot.estimated_load) { (void)peer; capacity &= load <= 6; }
    check(capacity && before == to_text(state),"capacity limits are respected and planning cannot mutate the finalized state");
    Committee next = committee; next.generation = hot.generation;
    Handoff handoff{*final,verified->checkpoint(),next,{}};
    handoff.endorsements.push_back(agreement.endorse(problem,verify,handoff,"p0",k1)); handoff.endorsements.push_back(agreement.endorse(problem,verify,handoff,"p1",k2));
    check(verify_handoff(problem,committee,verify,handoff),"placement reuses the exact existing verified handoff");
    Params transfer; for (const auto& stalk : c.stalks()) transfer.set(Key{"handoff_"+stalk.element.str()},state.element(stalk.element).params.get("result"));
    const auto game_before = to_text(graph.state("game"));
    engine.send("network",{"network.repartition",examples::placement_arguments(hot,transfer)}); engine.tick(0); engine.tick(0);
    check(net::Exchange::step(state).generation == 1 && to_text(graph.state("game")) == game_before,"migration through graph.edit changes execution without changing finalized game state");
    c.derive(state); view.generation = 1; view.last_placement_epoch = 10; view.epoch = 11;
    for (auto& cost : metrics.stalks) cost.execution = 0.1;
    for (const auto& stalk : c.stalks()) if (stalk.element != Key{"b"}) metrics.stalks.push_back({stalk.element,0.1,1});
    const auto held = Placement::plan(view,c,metrics); check(!held.changed,"cold load does not migrate before finalized-epoch hysteresis expires");
    view.epoch = 14; const auto cold = Placement::plan(view,c,metrics);
    std::set<std::string> cold_peers; for (const auto& a : cold.assignments) cold_peers.insert(a.peer);
    check(cold.changed && cold_peers.size() == 1 && cold.cut_cost == 0,"cold adjacent execution regions eventually merge without a wall timer");
    auto local = metrics; local.cut_penalty = 100; local.minimum_improvement = 0; local.hold_epochs = 0;
    check(Placement::plan(view,c,local).cut_cost == 0,"boundary traffic cost prefers keeping interactions local");
    auto replacement = metrics; replacement.peers[0].available = false; replacement.peers.push_back({{"p2",{}},6,true});
    const auto replaced = Placement::plan(view,c,replacement);
    check(replaced.feasible && std::none_of(replaced.assignments.begin(),replaced.assignments.end(),[](const auto& a){return a.peer == "p0";}),"disappeared executor work is placed on replaceable peers from the finalized checkpoint");
    auto impossible = metrics; for (auto& p : impossible.peers) p.capacity = 0.1;
    check(!Placement::plan(view,c,impossible).feasible,"insufficient capacity yields an unappliable plan instead of partial ownership");
    PlacementPlan three; three.feasible = true; three.changed = true;
    three.generation = net::Exchange::step(state).generation+1;
    for (std::size_t i = 0; i < c.stalks().size(); ++i) three.assignments.push_back({c.stalks()[i].element,"p"+std::to_string(i/2)});
    engine.send("network",{"network.repartition",examples::placement_arguments(three,transfer)}); engine.tick(0); engine.tick(0);
    c.derive(state); view.generation = net::Exchange::step(state).generation; view.last_placement_epoch = view.epoch;
    auto interest_metrics = metrics; interest_metrics.peers.push_back({{"p2",{}},6,true});
    bool protected_participant = false;
    try { Placement::plan(view,c,interest_metrics,[](const auto&,const auto&){return std::vector<InterestChange>{{InterestAction::Add,"a",{},1}};}); } catch (const std::invalid_argument&) { protected_participant = true; }
    check(protected_participant,"interest cannot repurpose a published participant as an overlap");
    Partition neighborhood; neighborhood.update(state,c); const auto compiled = neighborhood.compilations();
    const auto has_neighbor = [&](const std::string& peer){return std::any_of(neighborhood.boundaries().begin(),neighborhood.boundaries().end(),[&](const auto& b){return b.neighbor == peer;});};
    check(!has_neighbor("p2"),"partition neighborhood contains only the currently declared chain overlaps");
    const auto add = Placement::plan(view,c,interest_metrics,[](const auto& data,const auto& edges){
        (void)edges; Params constraint; constraint.set("left",data.front().stalk.str()).set("right",data.back().stalk.str()).set("weight",1.0);
        return std::vector<InterestChange>{{InterestAction::Add,"interest",constraint,2}};
    });
    const auto revision = c.revision(), elements = state.elements().size();
    engine.send("network",{"network.repartition",examples::placement_arguments(add,transfer)}); engine.tick(0);
    check(!state.find("interest"),"pure interest policy and requested arrow cannot create a hidden overlap");
    engine.tick(0); c.derive(state); neighborhood.update(state,c);
    check(state.find("interest") && c.revision() > revision && state.elements().size() == elements+1,"interest appears only as an ordinary constraint through graph.edit and Cellular re-derives it");
    check(has_neighbor("p2") && neighborhood.compilations() > compiled,"a new declared overlap creates the touching execution relation and recompiles Partition");
    view.generation = net::Exchange::step(state).generation; view.last_placement_epoch = view.epoch;
    const auto remove = Placement::plan(view,c,interest_metrics,[](const auto&,const auto&){return std::vector<InterestChange>{{InterestAction::Remove,"interest",{},0}};});
    engine.send("network",{"network.repartition",examples::placement_arguments(remove,transfer)}); engine.tick(0); engine.tick(0); c.derive(state);
    neighborhood.update(state,c);
    check(!state.find("interest") && state.elements().size() == elements && c.overlaps().size() == 5,"expired interest removes the actual cellular relation through the same edit boundary");
    check(!has_neighbor("p2"),"expired overlap removes its derived neighboring execution relation");
    const auto intact = to_text(state);
    Params invalid;
    invalid.set("generation",static_cast<std::int64_t>(net::Exchange::step(state).generation+1))
        .set("interest_source",std::string("state network { element invalid : constraint { left = \"a\" right = \"missing\" weight = 1 } }"));
    bool refused = false;
    engine.send("network",{"network.repartition",invalid}); engine.tick(0);
    try { engine.tick(0); } catch (const std::invalid_argument&) { refused = true; }
    check(refused && !state.find("invalid") && to_text(state) == intact,"invalid interest topology rolls back the complete edit without changing finalized data");
    check(graph.validate().empty() && sg::verify(graph).holds(),"placement and interest edits keep strict graph validation and sg::verify clean");
    return failures ? 1 : 0;
}
