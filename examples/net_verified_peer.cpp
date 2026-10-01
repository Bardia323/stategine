// External regional executor. Only a verified quorum crosses the DSL port.
#include "Signed.hpp"
#include "Udp.hpp"
#include "sg/net/Cellular.hpp"
#include "sg/net/Protocol.hpp"
#include "sg/core/Engine.hpp"
#include "sg/core/Laws.hpp"
#include "sg/dsl/Runtime.hpp"
#include "sg/dsl/Natives.hpp"
#include "../src/net/Canonical.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
namespace sgen { void build_verified(sg::StateGraph&, const sg::dsl::Natives&, sg::dsl::Bindings&); }
namespace {
sg::net::Bytes number(double x) { sg::net::detail::Writer w("verified.observation.v1"); w.real(x); return w.data(); }
double number(const sg::net::Bytes& bytes) { sg::net::detail::Reader r(bytes,"verified.observation.v1"); const auto x = r.real(); r.end(); return x; }
}
int main(int argc,char** argv) {
    try {
        if (argc != 6) throw std::invalid_argument("usage: sg_net_verified_peer rank base-port credentials cpu|cuda output.json");
        const int rank = std::stoi(argv[1]), base = std::stoi(argv[2]); const std::filesystem::path folder(argv[3]), output(argv[5]);
        const auto committee = sg::examples::read_committee(folder); const auto peer = "p"+std::to_string(rank);
        const auto key = sg::examples::read_signing_key(folder,peer);
        sg::examples::Journal journal(folder,peer);
        std::unique_ptr<sg::net::Backend> backend;
        if (std::string(argv[4]) == "cuda") { backend = std::make_unique<sg::net::CudaBackend>(); if (!backend->available()) return 77; }
        else if (std::string(argv[4]) == "cpu") backend = std::make_unique<sg::net::CpuBackend>();
        else throw std::invalid_argument("invalid backend");
        std::vector<sg::net::Peer> peers; for (const auto& m : committee.members) peers.push_back({m.peer,"127.0.0.1"});
        sg::examples::Udp transport(rank,peers,base);
        sg::dsl::Natives natives;
        natives.arrow("dispatch",[](sg::State& s,sg::Element&,sg::Element*,const sg::Event& e) {
            if (e.args.has("packet")) s.emit({"network.outgoing",e.args});
        });
        natives.arrow("request",[](sg::State& s,sg::Element&,sg::Element*,const sg::Event& e) {
            if (e.args.num("dt") > 0 && s.params().num("requested",-1) != s.params().num("epoch")) {
                s.params().set("requested",s.params().get("epoch")); s.emit("network.request");
            }
        });
        natives.arrow("finalized",[](sg::State& s,sg::Element&,sg::Element*,const sg::Event& e) {
            if (!e.args.has("receipt") || e.args.num("epoch",-1) != s.params().num("epoch")) return;
            for (const auto* id : {"p0","p1"}) s.element(sg::Key{id}).params.set("result",e.args.num(sg::Key{id}));
            s.params().set("previous",e.args.get("receipt")).set("epoch",s.params().get_or<std::int64_t>("epoch",0)+1);
            s.emit("network.committed");
        });
        sg::StateGraph graph; sg::dsl::Bindings bindings; sgen::build_verified(graph,natives,bindings);
        sg::set_observers(sg::Observers::Strict); bool requested = false;
        graph.state("network").bus().subscribe("network.request",[&](const sg::Event&) { requested = true; });
        graph.state("network").bus().subscribe("network.outgoing",[&](const sg::Event& e) {
            transport.send_to(e.args.get_or<std::string>("to",{}),sg::net::unhex(e.args.get_or<std::string>("packet",{})),e.args.get_or<std::string>("slot",{}));
        });
        sg::Engine engine(graph); engine.set_strict(true); engine.start();
        // Ordinary game data publishes only through these declared functors.
        engine.tick(0);
        sg::net::Cellular cellular(graph.state("network"));
        sg::net::SolverSpec spec; sg::net::Verify verify; sg::net::Agreement agreement(committee,{},0,[&](const sg::net::Bytes& snapshot) { journal.store(snapshot); }); sg::net::Integrity integrity(committee);
        std::unique_ptr<sg::net::Protocol> protocol; bool delivered = false;
        std::vector<std::string> receipts,epochs;
        std::size_t forged = 0; const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds(20);
        while (graph.state("network").params().num("epoch") < 4) {
            engine.tick(1.0/60.0);
            auto& network = graph.state("network");
            if (delivered) { receipts.push_back(sg::net::hex(protocol->finalized()->id())); epochs.push_back(sg::net::hex(protocol->accepted()->epoch().id())); protocol.reset(); delivered = false; }
            if (network.params().num("epoch") >= 4) break;
            if (requested && !protocol) {
                requested = false; cellular.update(network); const auto layout = cellular.layout(); const auto topology = sg::net::topology_description(cellular);
                sg::net::LinearSystem system{layout}; system.observations = {network.element("p0").params.num("observation"),network.element("p1").params.num("observation")};
                system.confidence = {0,0}; system.fixed = {0,0}; system.pins = {0,0}; system.overlap_weights = {1}; system.iterations = spec.iterations;
                const auto tick = agreement.next_tick();
                sg::net::EpochContext context{"verified:"+sg::net::hex(committee.id()),committee.region,tick,committee.generation,sg::net::topology_hash(layout,topology),sg::net::constraint_hash(system,{}),spec.id(),committee.id(),agreement.previous()};
                auto builder = [layout,context,spec,topology](const std::vector<sg::net::Observation>& inputs) {
                    sg::net::LinearSystem s{layout}; s.observations = {0,0}; s.confidence = {0,0}; s.fixed = {0,0}; s.pins = {0,0}; s.overlap_weights = {1}; s.iterations = spec.iterations;
                    for (const auto& o : inputs) s.observations[o.peer == "p0" ? 0 : 1] = number(o.payload);
                    return sg::net::Problem::make(context,s,spec,inputs,{},topology);
                };
                protocol = std::make_unique<sg::net::Protocol>(committee,peer,*key,*backend,context,std::vector<sg::net::InputSlot>{{"p0","p0","value",tick,false},{"p1","p1","value",tick,false}},builder,verify,agreement,integrity);
                std::vector<sg::net::Observation> local;
                if (rank < 2) { sg::net::Observation o; o.peer = peer; o.object = peer; o.parameter = "value"; o.sequence = tick; o.payload = number(system.observations[rank]); local.push_back(o); }
                // Injection checks the actual port boundary: invalid inputs must
                // neither finalize nor alter the ordinary graph's data.
                const auto before = network.data_version();
                sg::net::Observation fake; fake.peer = "p0"; fake.context = context.id(); fake.tick = tick; fake.sequence = tick; fake.object = "p0"; fake.parameter = "value"; fake.payload = number(99);
                sg::net::Message injection; injection.from = "p0"; injection.to = peer; injection.inputs = {fake}; protocol->receive(injection);
                if (network.data_version() != before || protocol->accepted()) throw std::runtime_error("forged observation reached the world");
                ++forged;
                protocol->submit(local);
            }
            sg::net::Bytes bytes;
            while (transport.try_receive(bytes)) {
                std::optional<sg::net::Message> message;
                try { message = sg::net::decode_message(bytes); } catch (const std::exception&) { }
                if (protocol && message) { protocol->receive(*message); protocol->receive(*message); }
            }
            if (protocol) {
                for (const auto& m : protocol->outgoing()) {
                    sg::Params dispatch; dispatch.set("to",m.to).set("slot",std::to_string(static_cast<int>(m.kind))).set("packet",sg::net::hex(sg::net::encode(m)));
                    engine.send("network",{"network.dispatch",dispatch});
                }
                if (protocol->accepted() && !delivered) {
                    const auto& x = protocol->accepted()->values(); sg::Params args; args.set("epoch",static_cast<std::int64_t>(protocol->accepted()->epoch().context.tick)).set("p0",x[0]).set("p1",x[1]).set("receipt",sg::net::hex(protocol->finalized()->id()));
                    engine.send("network",{"network.finalized",args}); delivered = true;
                }
            }
            transport.retry(); if (std::chrono::steady_clock::now() > deadline) throw std::runtime_error("verified region did not finalize");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const auto flush = std::chrono::steady_clock::now()+std::chrono::milliseconds(200);
        while (std::chrono::steady_clock::now() < flush) { transport.retry(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        const auto laws = sg::verify(graph); if (!graph.validate().empty() || !laws.holds()) throw std::runtime_error(laws.str());
        std::ofstream out(output); out << "{\"a\":" << graph.state("a").element("value").params.num("x") << ",\"b\":" << graph.state("b").element("value").params.num("x") << ",\"forged_rejected\":" << forged << ",\"receipts\":[";
        for (std::size_t i = 0; i < receipts.size(); ++i) out << (i ? "," : "") << '"' << receipts[i] << '"';
        out << "],\"epochs\":[";
        for (std::size_t i = 0; i < epochs.size(); ++i) out << (i ? "," : "") << '"' << epochs[i] << '"';
        out << "]}\n";
        if (!out) throw std::runtime_error("report failed");
        std::cout << "four regional steps; independent verification, strict mode and laws pass\n"; engine.stop(); return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
