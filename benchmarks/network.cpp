// Opt-in execution measurements. Virtual delivery time is outside the world.
#include "sg/net/Distributed.hpp"
#include "sg/net/Protocol.hpp"
#include "sg/dsl/Compile.hpp"
#include "sg/dsl/Apply.hpp"
#include "sg/core/Engine.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <new>
#include <sstream>
#include <stdexcept>

namespace { std::atomic<std::uint64_t> allocations{0}; }
#ifdef _MSC_VER
#define BENCH_NOINLINE __declspec(noinline)
#else
#define BENCH_NOINLINE __attribute__((noinline))
#endif
BENCH_NOINLINE void* operator new(std::size_t n) { if (auto* p = std::malloc(n ? n : 1)) { ++allocations; return p; } throw std::bad_alloc(); }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
BENCH_NOINLINE void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {
using namespace sg; using namespace sg::net;
using Clock = std::chrono::steady_clock;
double us(Clock::time_point start) { return std::chrono::duration<double,std::micro>(Clock::now()-start).count(); }
std::string id(unsigned i) { return "v"+std::to_string(100000+i); }
template<class T> auto evaluate(T& solver,const State& state,DistributedResult& out,const Backend* backend,int) -> decltype(solver.evaluate(state,out,backend),void()) { solver.evaluate(state,out,backend); }
template<class T> void evaluate(T& solver,const State& state,DistributedResult& out,const Backend* backend,...) { out = solver.evaluate(state,backend); }
template<class T> auto growth(const T& solver,int) -> decltype(solver.diagnostics().workspace_growths,std::uint64_t{}) { return solver.diagnostics().workspace_growths; }
template<class T> std::uint64_t growth(const T&,...) { return 0; }
template<class T> auto references(const T& verifier,int) -> decltype(verifier.diagnostics().reference_solves,std::int64_t{}) { return static_cast<std::int64_t>(verifier.diagnostics().reference_solves); }
template<class T> std::int64_t references(const T&,...) { return -1; }
template<class T> auto protocol_references(const T& protocol,int) -> decltype(protocol.verification_diagnostics().reference_solves,std::int64_t{}) { return static_cast<std::int64_t>(protocol.verification_diagnostics().reference_solves); }
template<class T> std::int64_t protocol_references(const T&,...) { return -1; }
std::string source(unsigned n, unsigned rank, unsigned peers, bool async) {
    std::ostringstream s;
    s << "state time : temporal\ninitial network\nstate network {\npeer = \"p" << rank << "\"\nepoch = 0\ngeneration = 0\nround = 0\niterations = 128\nlambda = 1.0\nrelaxation = 0.9\ndiffusion_step = 0.01\nasync = " << (async ? "true" : "false") << "\nmax_staleness = 8\nelement control\n";
    for (unsigned i = 0; i < n; ++i) s << "element " << id(i) << " : participant { observation = " << (i%7) << ".0 solver = \"p" << std::min(peers-1,i*peers/n) << "\" }\n";
    for (unsigned i = 1; i < n; ++i) s << "element e" << i << " : constraint { left = \"" << id(i-1) << "\" right = \"" << id(i) << "\" }\n";
    s << "control -> control : solve(dt) on network.solve native solve\ncontrol -> control : receive on network.receive native receive\nsay network.boundary\n}\nport network.receive\ndrive time -> network.solve keeps always\n";
    return s.str();
}
struct Machine {
    StateGraph graph; dsl::Bindings bindings; Distributed solver; const Backend& backend;
    std::unique_ptr<Engine> engine; std::vector<Packet> outgoing; DistributedResult result; double compute = 0;
    Machine(unsigned n,unsigned rank,unsigned peers,bool async,const Backend& b) : backend(b) {
        dsl::Natives natives;
        natives.arrow("solve",[this](State& state,Element&,Element*,const Event& e){
            if (e.args.num("dt") <= 0) return;
            const auto start = Clock::now(); evaluate(solver,state,result,&backend,0); compute += us(start);
            state.params() = result.params;
            for (const auto& r : result.section.readings) state.element(r.element).params.set("result",r.value);
            for (const auto& c : result.boundaries) state.element(c.element).params = c.params;
            for (const auto& p : result.outgoing) state.emit({"network.boundary",Exchange::arguments(Exchange::encode(p))});
        });
        natives.arrow("receive",[this](State& state,Element&,Element*,const Event& e){
            if (!e.args.has("packet")) return;
            const auto start = Clock::now(); const auto changes = solver.receive(state,Exchange::decode(Exchange::bytes(e.args))); compute += us(start);
            for (const auto& c : changes) state.element(c.element).params = c.params;
        });
        const auto plan = dsl::compile_source(source(n,rank,peers,async),"<network benchmark>");
        if (!plan.ok()) throw std::runtime_error(plan.report());
        const auto applied = dsl::apply(plan.plan,graph,natives);
        if (!applied.ok) throw std::runtime_error(applied.why);
        graph.state("network").bus().subscribe("network.boundary",[this](const Event& e){outgoing.push_back(Exchange::decode(Exchange::bytes(e.args)));});
        engine = std::make_unique<Engine>(graph); engine->set_strict(true); engine->start();
    }
    std::int64_t round() const { return graph.state("network").params().get_or<std::int64_t>("round",0); }
};
struct Frame { unsigned due,target; Bytes bytes; };
void distributed(unsigned n,unsigned peers,unsigned delay,bool async,const Backend& backend,const char* name,unsigned rounds) {
    std::vector<std::unique_ptr<Machine>> machines;
    for (unsigned p = 0; p < peers; ++p) machines.push_back(std::make_unique<Machine>(n,p,peers,async,backend));
    std::vector<Frame> queue; std::uint64_t bytes = 0;
    Cellular cellular; auto system = cellular.gather(machines[0]->graph.state("network"));
    std::vector<double> x(n); int rounds_to_target = -1; std::int64_t measured_round = -1;
    const auto residual = [&] {
        for (unsigned i = 0; i < n; ++i) {
            const auto owner = std::min(peers-1,i*peers/n); const auto& p = machines[owner]->graph.state("network").element(Key{id(i)}).params;
            x[i] = p.num("result",p.num("observation"));
        }
        return measure(system,x).equation;
    };
    auto tick = [&](unsigned frame) {
        for (auto it = queue.begin(); it != queue.end();) {
            if (it->due > frame) { ++it; continue; }
            machines[it->target]->engine->send("network",{"network.receive",Exchange::arguments(it->bytes)}); it = queue.erase(it);
        }
        for (auto& m : machines) m->engine->tick(0.001);
        for (auto& m : machines) { for (const auto& p : m->outgoing) { auto b = Exchange::encode(p); bytes += b.size(); queue.push_back({frame+delay,static_cast<unsigned>(std::stoul(p.to.substr(1))),std::move(b)}); } m->outgoing.clear(); }
    };
    tick(0); double cold = 0; for (auto& m : machines) { cold += m->compute; m->compute = 0; }
    const auto first_alloc = allocations.load(); unsigned frame = 0; std::uint64_t warm_growth = 0; bool warmed = false;
    while (frame++ < (rounds+2)*(delay+2)*2) {
        tick(frame); bool done = true; auto minimum = machines[0]->round();
        for (const auto& m : machines) { done &= m->round() >= rounds; minimum = std::min(minimum,m->round()); }
        if (minimum != measured_round) {
            measured_round = minimum;
            if (rounds_to_target < 0 && residual() <= 1e-6) rounds_to_target = static_cast<int>(minimum);
        }
        if (!warmed && minimum >= 2) { for (const auto& m : machines) warm_growth += growth(m->solver,0); warmed = true; }
        if (done) break;
    }
    const auto warm_alloc = allocations.load()-first_alloc;
    double total = 0; unsigned count = 0; std::uint64_t final_growth = 0;
    for (const auto& m : machines) { total += m->compute; count += static_cast<unsigned>(m->round()); final_growth += growth(m->solver,0); }
    std::cout << "distributed," << name << ',' << (async ? "async" : "sync") << ',' << n << ',' << system.layout.columns.size() << ',' << n-1 << ',' << peers << ',' << delay << ',' << cold << ',' << total/std::max(1u,count) << ',' << count/peers << ',' << residual() << ',' << n*1e6*count/peers/std::max(1.0,total) << ',' << system.layout.columns.size()*1e6*count/peers/std::max(1.0,total) << ',' << bytes << ',' << warm_alloc << ',' << frame << ',' << rounds_to_target << ',' << (warmed ? final_growth-warm_growth : final_growth) << '\n';
}
struct Fixture {
    std::vector<std::unique_ptr<SigningKey>> keys; Committee committee; Problem problem;
    Fixture(const LinearSystem& s,unsigned count) {
        committee.region = "bench"; committee.quorum = count; committee.faults = 0;
        for (unsigned i = 0; i < count; ++i) { Seed seed{}; seed[0] = static_cast<std::uint8_t>(i+1); keys.push_back(std::make_unique<SigningKey>(seed)); committee.members.push_back({"p"+std::to_string(i),keys.back()->public_key()}); }
        SolverSpec spec; spec.iterations = s.iterations; spec.residual_tolerance = 100; spec.equation_tolerance = 1e-6;
        EpochContext context{"bench","bench",0,0,{},{},{},committee.id(),{}};
        problem = Problem::make(context,s,spec,{});
    }
};
void verified(const LinearSystem& s,unsigned peers,unsigned delay,const Backend& backend,const char* name,unsigned samples) {
    Fixture fixture(s,peers); Verify verify;
    const auto begin = Clock::now(); const auto proposal = verify.propose(fixture.problem,fixture.committee,"p0",*fixture.keys[0],backend); const auto cold = us(begin);
    const auto a = allocations.load(); const auto warm = Clock::now();
    for (unsigned i = 0; i < samples; ++i) if (!verify.proposal(fixture.problem,fixture.committee,proposal)) throw std::runtime_error("benchmark proposal rejected");
    std::cout << "verify," << name << ',' << s.observations.size() << ',' << peers << ',' << cold << ',' << us(warm)/samples << ',' << allocations.load()-a << ',' << hex(proposal.certificate.result) << ',' << references(verify,0) << '\n';
    std::vector<std::unique_ptr<Agreement>> agreements; std::vector<std::unique_ptr<Integrity>> integrity; std::vector<std::unique_ptr<Protocol>> protocols;
    std::vector<InputSlot> slots; for (unsigned i = 0; i < peers; ++i) slots.push_back({"p"+std::to_string(i),"input","value",0,false});
    auto context = fixture.problem.epoch.context;
    auto builder = [&](const auto& inputs){ return Problem::make(context,s,fixture.problem.solver,inputs); };
    for (unsigned i = 0; i < peers; ++i) {
        agreements.push_back(std::make_unique<Agreement>(fixture.committee)); integrity.push_back(std::make_unique<Integrity>(fixture.committee));
        protocols.push_back(std::make_unique<Protocol>(fixture.committee,"p"+std::to_string(i),*fixture.keys[i],backend,context,slots,builder,Verify{},*agreements.back(),*integrity.back()));
    }
    double compute = 0; std::uint64_t bytes = 0; std::vector<Frame> queue;
    for (unsigned i = 0; i < peers; ++i) { const auto t = Clock::now(); protocols[i]->submit({Observation{"p"+std::to_string(i),{},0,0,"input","value",false,{},Bytes{1},{}}}); compute += us(t); }
    unsigned frame = 0;
    for (; frame < delay*20+20; ++frame) {
        for (auto& p : protocols) for (const auto& m : p->outgoing()) { auto b = encode(m); bytes += b.size(); queue.push_back({frame+delay,static_cast<unsigned>(std::stoul(m.to.substr(1))),std::move(b)}); }
        for (auto it = queue.begin(); it != queue.end();) { if (it->due > frame) { ++it; continue; } const auto t = Clock::now(); protocols[it->target]->receive(decode_message(it->bytes)); compute += us(t); it = queue.erase(it); }
        bool done = true; for (auto& p : protocols) done &= p->accepted() != nullptr; if (done) break;
    }
    for (auto& p : protocols) if (!p->accepted()) throw std::runtime_error("benchmark finalization stalled");
    const auto receipt = protocols[0]->finalized()->id(); for (auto& p : protocols) if (p->finalized()->id() != receipt) throw std::runtime_error("benchmark receipt mismatch");
    std::int64_t solves = 0;
    for (const auto& p : protocols) { const auto count = protocol_references(*p,0); if (count < 0) { solves = -1; break; } solves += count; }
    std::cout << "protocol," << name << ',' << s.observations.size() << ',' << peers << ',' << delay << ',' << compute << ',' << bytes << ',' << frame << ',' << hex(receipt) << ',' << solves << '\n';
}
void compaction() {
    CudaBackend cuda; if (!cuda.available()) { std::cout << "SKIP CUDA compaction\n"; return; }
    for (unsigned n : {32u,128u,1024u,16385u,65536u,262145u}) {
        Layout l; l.stalk_offsets = {0,n}; l.overlap_offsets = {0}; l.row_offsets = {0}; l.column_offsets.assign(n+1,0);
        LinearSystem s{l,{},{},{},{},{}}; s.observations.assign(n,1); s.confidence.assign(n,0); s.fixed.resize(n); s.pins.assign(n,1); s.iterations = 0;
        for (unsigned c = 0; c < n; ++c) s.fixed[c] = c%3 != 0;
        auto start = Clock::now(); cuda.solve(s); const auto cold = us(start); const auto before = cuda.transfers();
        start = Clock::now(); for (int i = 0; i < 10; ++i) cuda.solve(s); const auto unchanged = us(start)/10;
        start = Clock::now(); for (int i = 0; i < 10; ++i) { for (auto& value : s.observations) value += 1; cuda.solve(s); }
        const auto changed = us(start)/10; const auto after = cuda.transfers();
        std::cout << "compaction," << n << ',' << cold << ',' << unchanged << ',' << changed << ',' << after.downloaded_values-before.downloaded_values << '\n';
    }
}
}
int main(int argc,char** argv) {
    try {
        if (argc > 1 && std::string(argv[1]) == "--compaction") { compaction(); return 0; }
        const unsigned n = argc > 1 ? static_cast<unsigned>(std::stoul(argv[1])) : 64;
        const unsigned rounds = argc > 2 ? static_cast<unsigned>(std::stoul(argv[2])) : 128;
        if (n < 16 || n > 65536 || rounds == 0) throw std::invalid_argument("benchmark needs 16..65536 coordinates and positive rounds");
        std::cout << std::fixed << std::setprecision(3);
        std::cout << "# distributed: backend,method,coordinates,nonzeros,overlaps,partitions,virtual_delay_ms,cold_us,warm_us/relaxation,rounds,residual,coordinates/sec,nonzeros/sec,bytes,allocations,virtual_elapsed_ms,rounds_to_1e-6(-1=budget),workspace_growth_after_warmup\n";
        std::cout << "# single: backend,coordinates,nonzeros,cold_us,warm_us/solve,allocations,coordinates/sec,nonzeros/sec,overlaps,participants,partitions\n";
        std::cout << "# verify: backend,coordinates,peers,cold_us,warm_us/verification,allocations,canonical_hash,reference_solves(-1=legacy)\n";
        std::cout << "# protocol: backend,coordinates,peers,virtual_delay_ms,compute_us,bytes,virtual_elapsed_ms,receipt,reference_solves(-1=legacy)\n";
        CpuBackend cpu; CudaBackend cuda;
        for (const auto* backend : {static_cast<const Backend*>(&cpu),static_cast<const Backend*>(&cuda)}) {
            const char* name = backend == &cpu ? "cpu" : "cuda";
            if (argc > 3 && std::string(argv[3]) != name) continue;
            if (!backend->available()) { std::cout << "SKIP " << name << '\n'; continue; }
            Machine single(n,0,1,false,*backend); Cellular cellular; const auto s = cellular.gather(single.graph.state("network"));
            auto start = Clock::now(); backend->solve(s); const double cold = us(start); const auto a = allocations.load(); start = Clock::now();
            for (unsigned i = 0; i < 10; ++i) backend->solve(s);
            const auto warm = us(start)/10;
            std::cout << "single," << name << ',' << n << ',' << s.layout.columns.size() << ',' << cold << ',' << warm << ',' << allocations.load()-a << ',' << n*1e6/warm << ',' << s.layout.columns.size()*1e6/warm << ',' << n-1 << ',' << n << ",1\n";
            start = Clock::now(); cpu.solve(s); std::cout << "reference_us," << name << ',' << us(start) << '\n';
            for (unsigned peers : {2u,4u,8u,16u}) for (unsigned delay : {1u,10u,50u,100u}) {
                distributed(n,peers,delay,false,*backend,name,rounds); distributed(n,peers,delay,true,*backend,name,rounds);
                verified(s,peers,delay,*backend,name,10);
            }
            if (backend == &cuda) { const auto t = cuda.transfers(); std::cout << "cuda_transfers," << t.topology_uploads << ',' << t.observation_uploads << ',' << t.weight_uploads << ',' << t.pin_uploads << ',' << t.downloaded_values << '\n'; }
        }
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
