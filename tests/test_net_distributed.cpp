#include "../examples/Distributed.hpp"
#include "sg/core/Engine.hpp"
#include "sg/core/Laws.hpp"
#include "sg/dsl/Runtime.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <random>

namespace sgen {
void build_distributed(sg::StateGraph&, const sg::dsl::Natives&, sg::dsl::Bindings&);
void build_distributed_vectors(sg::StateGraph&, const sg::dsl::Natives&, sg::dsl::Bindings&);
}
namespace {
using Builder = void (*)(sg::StateGraph&, const sg::dsl::Natives&, sg::dsl::Bindings&);
const std::vector<std::string> scalar_ids{"a", "b", "c", "d", "e", "f"}, vector_ids{"a", "b", "c", "anchor", "free", "u", "v"};
using Values = std::map<std::pair<sg::Key, std::uint32_t>, double>;
struct Delivery { int due, target; sg::net::Bytes bytes; };
struct Machine {
    sg::StateGraph graph;
    sg::dsl::Bindings bindings;
    std::shared_ptr<sg::net::Distributed> solver = std::make_shared<sg::net::Distributed>();
    std::unique_ptr<sg::net::Backend> backend;
    std::unique_ptr<sg::Engine> engine;
    std::vector<sg::net::Bytes> sent;
    std::size_t messages = 0, coordinates = 0;
    Machine(Builder builder, bool gpu, int rank, const std::vector<std::string>& ids, const std::vector<int>& owners, bool omit) {
        if (gpu) backend = std::make_unique<sg::net::CudaBackend>(); else backend = std::make_unique<sg::net::CpuBackend>();
        builder(graph, sg::examples::distributed_natives(backend.get(), solver), bindings);
        graph.state("network").bus().subscribe("network.boundary", [this](const sg::Event& event) {
            const auto bytes = sg::net::Exchange::bytes(event.args); const auto packet = sg::net::Exchange::decode(bytes);
            for (const auto& b : packet.boundaries) coordinates += b.values.size();
            ++messages; sent.push_back(bytes);
        });
        engine = std::make_unique<sg::Engine>(graph); engine->set_strict(true); engine->start();
        sg::Params args; args.set("peer", std::string("p")+std::to_string(rank)).set("omit_remote", omit);
        for (std::size_t i = 0; i < ids.size(); ++i) args.set(sg::Key{ids[i]}, std::string("p")+std::to_string(owners[i]));
        if (builder == sgen::build_distributed) { engine->fire("game.publish"); engine->tick(0); }
        engine->send("network", {"network.configure", args}); engine->tick(0);
    }
    std::int64_t round() const { return graph.state("network").params().get_or<std::int64_t>("round", 0); }
};
struct World {
    Builder builder;
    std::vector<std::string> ids;
    std::vector<int> owners;
    std::vector<std::unique_ptr<Machine>> machines;
    std::vector<Delivery> queue;
    int frame = 0, delay;
    bool duplicates;
    bool faults = false;
    std::vector<int> speeds;
    std::map<std::pair<std::string,std::string>,sg::net::Bytes> recent;
    std::mt19937 random{42};
    World(int count, std::vector<int> assignment, bool gpu = false, int latency = 0, bool duplicate = false, bool omit = false,
          Builder build = sgen::build_distributed, std::vector<std::string> names = scalar_ids)
        : builder(build), ids(std::move(names)), owners(std::move(assignment)), delay(latency), duplicates(duplicate) {
        for (int p = 0; p < count; ++p) machines.push_back(std::make_unique<Machine>(builder, gpu, p, ids, owners, omit));
    }
    void tick() {
        ++frame;
        for (auto it = queue.begin(); it != queue.end();) {
            if (it->due > frame) { ++it; continue; }
            machines[it->target]->engine->send("network", {"network.receive", sg::net::Exchange::arguments(it->bytes)}); it = queue.erase(it);
        }
        for (std::size_t i = 0; i < machines.size(); ++i)
            if (speeds.empty() || (speeds[i] > 0 && frame%speeds[i] == 0)) machines[i]->engine->tick(0.01);
        for (auto& m : machines) {
            for (const auto& bytes : m->sent) {
                const auto packet = sg::net::Exchange::decode(bytes);
                if (faults) recent[{packet.from,packet.to}] = bytes;
                const int target = std::stoi(packet.to.substr(1));
                if (faults && random()%5 == 0) continue;
                const int lag = delay ? static_cast<int>(faults ? random()%delay : (packet.step.tick*7 + target*3)%delay) : 0;
                queue.push_back({frame + 1 + lag, target, bytes});
                if (duplicates) queue.push_back({frame + 2 + lag, target, bytes});
            }
            m->sent.clear();
        }
        // Model a Latest transport retrying its retained frontier after loss.
        if (faults && frame%7 == 0) for (const auto& [route,bytes] : recent)
            queue.push_back({frame+1, std::stoi(route.second.substr(1)),bytes});
    }
    void asynchronous(std::int64_t bound = 4, double alpha = 0.03) {
        for (auto& m : machines) {
            sg::Params args; args.set("async",true).set("max_staleness",bound).set("diffusion_step",alpha);
            m->engine->send("network", {"network.configure",args}); m->engine->tick(0);
        }
    }
    void run(int rounds) {
        const int end = frame + 30000;
        while (frame < end) {
            bool done = true; for (const auto& m : machines) if (!m->solver->partition().stalks().empty()) done &= m->round() >= rounds;
            if (done && frame != 0) return;
            tick();
        }
        throw std::runtime_error("distributed test did not make progress");
    }
    Values values() const {
        Values out;
        for (const auto& m : machines) {
            const auto& c = m->solver->cellular(); const auto& p = m->solver->partition();
            for (auto v : p.stalks()) {
                const auto& stalk = c.stalks()[v]; const auto& data = m->graph.state("network").element(stalk.element).params;
                for (std::uint32_t i = 0; i < stalk.dimension; ++i) out[{stalk.element, i}] = data.num(sg::net::Cellular::coordinate_key("result", stalk.dimension, i));
            }
        }
        return out;
    }
    bool laws() const {
        for (const auto& m : machines) {
            const auto sent = m->messages;
            sg::LawOptions probes; probes.args_for["network.solve"].set("dt", 0.1);
            const auto result = sg::verify(m->graph, {}, probes);
            if (!result.holds() || !m->graph.validate().empty() || sent != m->messages) return false;
        }
        return true;
    }
    void repartition(std::vector<int> assignment, std::int64_t generation, const std::string& remove = {}) {
        const auto handoff = values(); owners = std::move(assignment);
        for (auto& m : machines) {
            sg::Params args; args.set("generation", generation);
            if (!remove.empty()) args.set(sg::Key{"remove_overlap_"+remove},true);
            for (std::size_t i = 0; i < ids.size(); ++i) {
                args.set(sg::Key{ids[i]}, std::string("p")+std::to_string(owners[i]));
                args.set(sg::Key{"handoff_"+ids[i]}, handoff.at({sg::Key{ids[i]}, 0}));
            }
            m->engine->send("network", {"network.repartition", args});
        }
        tick(); tick();
    }
};
Values reference(Builder builder = sgen::build_distributed) {
    sg::StateGraph graph; sg::dsl::Bindings bindings;
    builder(graph, sg::examples::distributed_natives(nullptr, std::make_shared<sg::net::Distributed>()), bindings);
    sg::net::Reconcile r; const auto result = r.evaluate(graph.state("network"));
    Values out; for (const auto& reading : result.readings) out[{reading.element, reading.coordinate}] = reading.value; return out;
}
bool equivalent(const Values& a, const Values& b, double tolerance = 1e-8) {
    if (a.size() != b.size()) return false;
    for (const auto& [key, value] : a) { const auto found = b.find(key); if (found == b.end() || std::fabs(value-found->second) > tolerance) return false; }
    return true;
}
}
int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    int failures = 0;
    const auto check = [&](bool ok, const char* name) { std::printf("%s %s\n", ok ? "ok" : "FAIL", name); failures += !ok; };
    sg::set_observers(sg::Observers::Strict);
    const auto expected = reference();
    Machine receipt(sgen::build_distributed, false, 0, scalar_ids, {0,0,0,1,1,1}, false);
    sg::net::Packet future{"p1", "p0", {0,0,1}, {{"cd",0,{}}}};
    receipt.engine->send("network", {"network.receive", sg::net::Exchange::arguments(sg::net::Exchange::encode(future))}); receipt.engine->tick(0);
    check(!receipt.graph.state("network").element("cd").params.get_or<bool>("next_ready", false), "an unchanged future projection waits for its missing basis");
    sg::net::Packet basis{"p1", "p0", {0,0,0}, {{"cd",0,{8.0}}}};
    const auto basis_args = sg::net::Exchange::arguments(sg::net::Exchange::encode(basis));
    receipt.engine->send("network", {"network.receive", basis_args}); receipt.engine->tick(0);
    const auto stamp = receipt.graph.state("network").element("cd").params.stamp();
    receipt.engine->send("network", {"network.receive", basis_args}); receipt.engine->tick(0);
    check(receipt.graph.state("network").element("cd").params.get_or<bool>("next_ready", false) &&
          receipt.graph.state("network").element("cd").params.num("next_remote") == 8 &&
          receipt.graph.state("network").element("cd").params.stamp() == stamp, "basis arrival resolves reordered progress and duplicate receipt changes nothing");
    World single(1, {0,0,0,0,0,0}); single.run(1);
    check(single.values() == expected, "one partition is exactly the previous single-machine solve");
    World two(2, {0,0,0,1,1,1}); two.run(400);
    check(equivalent(two.values(), expected), "two partitions target the same confidence/disagreement objective");
    World six(6, {0,1,2,3,4,5}, false, 7, true, true); six.run(400);
    check(equivalent(six.values(), expected), "six chained partitions converge with delays, reordering and duplicates");
    bool local = true;
    for (std::size_t rank = 0; rank < six.machines.size(); ++rank) {
        const auto& m = *six.machines[rank]; const auto& p = m.solver->partition();
        local &= p.stalks().size() == 1 && p.boundaries().size() <= 2 && m.solver->cellular().compilations() == 0;
        for (const auto& b : p.boundaries()) local &= std::abs(std::stoi(b.neighbor.substr(1))-static_cast<int>(rank)) == 1;
        for (std::size_t i = 0; i < scalar_ids.size(); ++i) if (i != rank) local &= !m.graph.state("network").element(sg::Key{scalar_ids[i]}).params.has("observation");
    }
    check(local, "machines use neighboring projections, no remote observations and no global numerical matrix");
    check(two.laws() && six.laws() && single.laws(), "strict mode and sg::verify remain clean and do not send packets");
    World dynamic(2, {0,0,0,0,0,0}); dynamic.run(1);
    const auto before = dynamic.values(); const auto old_layout = dynamic.machines[0]->solver->partition().compilations();
    const auto old_states = dynamic.machines[0]->graph.ids().size(), old_elements = dynamic.machines[0]->graph.state("network").elements().size();
    dynamic.repartition({0,0,0,1,1,1}, 1); dynamic.run(400);
    check(equivalent(dynamic.values(), before) && dynamic.laws() && dynamic.machines[0]->solver->partition().compilations() > old_layout &&
          dynamic.machines[0]->graph.ids().size() == old_states && dynamic.machines[0]->graph.state("network").elements().size() == old_elements,
          "partition split recompiles execution, preserves the solution and adds no states or elements");
    dynamic.repartition({0,0,0,0,0,0}, 2); dynamic.run(1);
    check(equivalent(dynamic.values(), expected) && dynamic.laws(), "partition merge preserves the state and original single-machine solution");
    World vectors(4, {0,1,2,3,0,1,2}, false, 3, true, true, sgen::build_distributed_vectors, vector_ids); vectors.run(1800);
    check(equivalent(vectors.values(), reference(sgen::build_distributed_vectors)), "diffusion preserves a kernel, pins and rectangular restrictions across partitions");
    const sg::net::CudaBackend gpu;
    if (gpu.available()) {
        World cuda(4, {0,0,1,2,3,3}, true, 3, true, true); cuda.run(400);
        check(equivalent(cuda.values(), expected) && equivalent(cuda.values(), six.values()) && cuda.laws(), "real GPU, CPU, distributed and single-machine results agree");
    } else std::printf("SKIP distributed CUDA: backend or device unavailable\n");
    const auto& machine = *six.machines[0];
    const auto part = machine.solver->partition().compilations(); const auto cell = machine.solver->cellular().revision();
    six.tick(); check(machine.solver->partition().compilations() == part && machine.solver->cellular().revision() == cell, "value and protocol progress do not rebuild topology");
    sg::net::Packet packet{"p1", "p0", {0,0,0}, {{"ab",0,{4.0}}}};
    const auto encoded = sg::net::Exchange::encode(packet);
    check(sg::net::Exchange::encode(sg::net::Exchange::decode(encoded)) == encoded && sg::net::Exchange::bytes(sg::net::Exchange::arguments(encoded)) == encoded, "boundary byte encoding is deterministic and round trips");
    check(machine.solver->receive(machine.graph.state("network"), packet).empty(), "stale packet ticks cannot overwrite a current boundary");
    packet.step = {1,0,machine.round()}; packet.boundaries[0].basis = packet.step.tick;
    check(machine.solver->receive(machine.graph.state("network"), packet).empty(), "packets from another world epoch are ignored");
    packet.step = {0,1,machine.round()}; check(machine.solver->receive(machine.graph.state("network"), packet).empty(), "packets from a previous partition generation are ignored");
    auto truncated = encoded; truncated.pop_back(); bool refused = false;
    try { sg::net::Exchange::decode(truncated); } catch (const std::invalid_argument&) { refused = true; }
    check(refused, "truncated packets are refused before any state changes");
    for (auto& m : six.machines) { sg::Params args; args.set("epoch", 1.0); m->engine->send("network", {"network.begin", args}); }
    six.tick(); six.run(400);
    check(equivalent(six.values(), expected) && six.laws(), "a new declared world epoch resets progress and rejects in-flight prior-epoch data");
    World async(6,{0,1,2,3,4,5},false,9,true,true);
    async.asynchronous(); async.faults = true; async.speeds = {1,2,3,1,4,2}; async.run(1400);
    check(equivalent(async.values(),expected) && async.laws(),"bounded async with seeded random delay, loss, duplicates, reordering and unequal speeds targets the synchronous objective");
    bool certified = true; for (const auto& m : async.machines) certified &= m->graph.state("network").params().get_or<bool>("async_active",false);
    check(certified,"coercive strictly dominant neighborhoods use the certified async path");
    auto& fast = *async.machines[0]; const auto step = sg::net::Exchange::step(fast.graph.state("network"));
    auto newer = sg::net::Packet{"p1","p0",{step.epoch,step.generation,step.tick+10},{{"ab",step.tick+10,{123.0}}}};
    fast.engine->send("network",{"network.receive",sg::net::Exchange::arguments(sg::net::Exchange::encode(newer))}); fast.engine->tick(0);
    const auto stable = fast.graph.state("network").element("ab").params.stamp();
    auto older = newer; older.step.tick += 1; older.boundaries[0].basis -= 1; older.boundaries[0].values = {999};
    fast.engine->send("network",{"network.receive",sg::net::Exchange::arguments(sg::net::Exchange::encode(older))});
    fast.engine->send("network",{"network.receive",sg::net::Exchange::arguments(sg::net::Exchange::encode(newer))}); fast.engine->tick(0);
    check(fast.graph.state("network").element("ab").params.stamp() == stable && fast.graph.state("network").element("ab").params.num("remote") == 123,"old basis never overwrites a new projection; duplicate basis is idempotent");
    World stalled(6,{0,1,2,3,4,5}); stalled.asynchronous(2,0.04); stalled.tick(); stalled.speeds = {0,1,1,1,1,1};
    for (int i = 0; i < 30; ++i) stalled.tick();
    check(stalled.machines[1]->round() <= 3 && stalled.machines[5]->round() > stalled.machines[1]->round(),"an exhausted boundary staleness budget stalls its neighborhood without a global round barrier");
    World independent(6,{0,1,2,3,4,5}); independent.tick();
    independent.repartition({0,1,2,3,4,5},1,"cd"); independent.asynchronous(2,0.04);
    independent.tick(); independent.speeds = {0,1,1,1,1,1};
    for (int i = 0; i < 250; ++i) independent.tick();
    check(independent.machines[1]->round() <= 4 && independent.machines[3]->round() > 100 && independent.laws(),"a slow disconnected region cannot barrier independent partitions; declared constraint edits define the separation");
    World kernel(4,{0,1,2,3,0,1,2},false,3,true,true,sgen::build_distributed_vectors,vector_ids);
    kernel.asynchronous(4,0.02); kernel.run(1800);
    bool sync = true; for (const auto& m : kernel.machines) sync &= !m->graph.state("network").params().get_or<bool>("async_active",false);
    check(sync && equivalent(kernel.values(),reference(sgen::build_distributed_vectors),1e-6) && kernel.laws(),"unproven kernels retain synchronous initialization, pins and canonical component");
    World unsafe(2,{0,0,0,1,1,1}); unsafe.asynchronous(4,1.0); bool step_refused = false;
    try { unsafe.tick(); } catch (const std::invalid_argument&) { step_refused = true; }
    check(step_refused,"an async step above the actual operator/staleness bound fails closed");
    if (gpu.available()) {
        World mixed(2,{0,0,0,1,1,1}); mixed.asynchronous();
        // Natives hold the declared backend pointer; rebuild the second world
        // with CUDA, then configure it through the same port.
        mixed.machines[1] = std::make_unique<Machine>(sgen::build_distributed,true,1,scalar_ids,mixed.owners,false);
        mixed.asynchronous(); mixed.faults = true; mixed.delay = 7; mixed.speeds = {1,3}; mixed.run(1400);
        check(equivalent(mixed.values(),expected) && mixed.laws(),"CPU/CUDA mixed async execution preserves the reference solution");
    }
    return failures ? 1 : 0;
}
