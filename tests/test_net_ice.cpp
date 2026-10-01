#include "sg/net/IceTransport.hpp"
#include "sg/net/Protocol.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>
#include <memory>
#include <sstream>
#ifdef SG_TEST_WORLD
#include "sg/core/Engine.hpp"
#include "sg/core/Laws.hpp"
#include "sg/dsl/Runtime.hpp"
#include "sg/dsl/Natives.hpp"
namespace sgen { void build_verified(sg::StateGraph&,const sg::dsl::Natives&,sg::dsl::Bindings&); }
#endif
#ifdef SG_TEST_TURN
#include <juice/juice.h>
#include "IceFaults.hpp"
#endif
namespace {
using namespace sg::net;
int failures = 0;
void check(bool good, const char* why) { std::cout << (good ? "ok " : "FAIL ") << why << std::endl; failures += !good; }
#ifdef SG_TEST_WORLD
struct World {
    sg::StateGraph graph;
    sg::dsl::Bindings bindings;
    std::unique_ptr<sg::Engine> engine;
    World() {
        sg::dsl::Natives n;
        n.arrow("request",[](sg::State&,sg::Element&,sg::Element*,const sg::Event&){});
        n.arrow("dispatch",[](sg::State& s,sg::Element&,sg::Element*,const sg::Event& e){s.emit({"network.outgoing",e.args});});
        n.arrow("finalized",[](sg::State& s,sg::Element&,sg::Element*,const sg::Event& e){
            if (!e.args.has("p0") || !e.args.has("p1")) return;
            s.element("p0").params.set("result",e.args.get("p0")); s.element("p1").params.set("result",e.args.get("p1"));
            s.params().set("receipt",e.args.get("receipt")); s.emit("network.committed");
        });
        sgen::build_verified(graph,n,bindings); engine = std::make_unique<sg::Engine>(graph); engine->set_strict(true); engine->start();
    }
    bool commit(const Protocol& p) {
        const auto* r = p.accepted(); if (!r) return false;
        engine->send("network",{"network.finalized",sg::Params{}.set("p0",r->values()[0]).set("p1",r->values()[1]).set("receipt",hex(p.finalized()->id()))}); engine->tick(0);
        return graph.state("a").element("value").params.num("x") == 6 && graph.state("b").element("value").params.num("x") == 6 && graph.validate().empty() && sg::verify(graph).holds();
    }
};
#endif
struct Pair {
    IceTransport a, b;
    std::function<void()> faults;
    std::function<std::string(const std::string&)> candidate;
    explicit Pair(std::vector<std::string> servers = {}, bool relay = false)
        : a(IceConfig{"a",servers,relay}),b(IceConfig{"b",servers,relay}) { a.connect("b"); b.connect("a"); }
    explicit Pair(IceConfig left,IceConfig right) : a(std::move(left)),b(std::move(right)) { a.connect("b"); b.connect("a"); }
    void pump() {
        if (faults) faults();
        auto as = a.signals(), bs = b.signals();
        std::reverse(as.begin(),as.end()); std::reverse(bs.begin(),bs.end()); // candidate/description reordering
        auto route = [&](IceSignal s) {
            if (!candidate) return s;
            if (s.kind == IceSignalKind::Candidate) s.value = candidate(s.value);
            if (s.kind == IceSignalKind::Description) {
                std::istringstream input(s.value); std::ostringstream output; std::string line;
                while (std::getline(input,line)) { if (!line.empty() && line.back() == '\r') line.pop_back(); if (line.substr(0,12) == "a=candidate:") line = "a="+candidate(line.substr(2)); output << line << "\r\n"; } s.value = output.str();
            }
            return s;
        };
        for (const auto& s : as) b.signal("a",route(s));
        for (const auto& s : bs) a.signal("b",route(s));
        a.poll(); b.poll(); std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    template<class F> bool until(F test, int milliseconds = 15000) {
        const auto end = std::chrono::steady_clock::now()+std::chrono::milliseconds(milliseconds);
        do { pump(); if (test()) return true; } while (std::chrono::steady_clock::now() < end);
        return false;
    }
};
void messages(Pair& pair) {
    Bytes large(300*1024); for (std::size_t i = 0; i < large.size(); ++i) large[i] = static_cast<std::uint8_t>(i);
    check(pair.a.send({"b","checkpoint",DeliveryClass::Reliable,{},large}) == SendResult::Accepted,"large reliable message is queued explicitly");
    Inbound received;
    check(pair.until([&]{return pair.b.try_receive(received);}) && received.bytes == large && received.peer == "a" && received.channel == "checkpoint","SCTP frames a message larger than 64 KB without exposing fragments");
    pair.a.disconnect("b"); pair.b.disconnect("a");
    check(pair.a.send({"b","checkpoint",DeliveryClass::Reliable,{},Bytes{7}}) == SendResult::Accepted,"temporary disconnect retains reliable unsent work");
    bool coalesced = true;
    for (int i = 0; i < 100; ++i) coalesced &= pair.a.send({"b","boundary",DeliveryClass::Latest,"edge",Bytes{static_cast<std::uint8_t>(i)}}) == SendResult::Accepted;
    check(coalesced,"latest slot coalesces while disconnected");
    check(pair.a.telemetry("b").queued_latest == 1,"latest queues remain bounded to the newest unsent slot");
    pair.a.reconnect("b"); pair.b.reconnect("a");
    bool reliable = false, latest = false;
    check(pair.until([&]{ while (pair.b.try_receive(received)) { reliable |= received.channel == "checkpoint" && received.bytes == Bytes{7}; latest |= received.channel == "boundary" && received.bytes == Bytes{99}; } return reliable && latest; }),"reconnection delivers retained control and the newest boundary");
    const auto notices = pair.a.events();
    check(std::any_of(notices.begin(),notices.end(),[](const auto& n){return n.kind == TransportEventKind::DeliveryUncertain;}),"session breaks explicitly report uncertainty instead of silently dropping reliable traffic");
    int accepted = 0; pair.a.disconnect("b");
    while (pair.a.send({"b","control",DeliveryClass::Reliable,{},Bytes(32768,1)}) == SendResult::Accepted) ++accepted;
    check(accepted > 0 && accepted <= 256 && pair.a.telemetry("b").queued_reliable <= 8*1024*1024,"reliable backpressure is bounded and reported");
    check(pair.a.send({"b","boundary",DeliveryClass::Latest,"edge",Bytes{1}}) == SendResult::Accepted,"full control queue does not consume the boundary queue budget");
}
void receive_backpressure() {
    IceConfig left{"a"},right{"b"};
    left.maximum_message = right.maximum_message = 65536;
    right.receive_bytes = 65536; right.receive_messages = 2;
    Pair pair(left,right);
    for (int i = 0; i < 16; ++i) { Bytes bytes(32768,9); bytes[0] = static_cast<std::uint8_t>(i); if (pair.a.send({"b","control",DeliveryClass::Reliable,{},std::move(bytes)}) != SendResult::Accepted) throw std::runtime_error("receive fixture admission failed"); }
    check(pair.until([&]{ const auto events = pair.b.events(); return std::any_of(events.begin(),events.end(),[](const auto& n){return n.kind == TransportEventKind::ReceiveBlocked;}); }),"a stopped reader reports bounded receive backpressure without consuming reliable frames");
    std::vector<int> order; Inbound message;
    check(pair.until([&]{ while (pair.b.try_receive(message)) { if (message.bytes.size() != 32768 || message.bytes.back() != 9) return false; order.push_back(message.bytes[0]); } return order.size() == 16; }) && order == std::vector<int>({0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}),"all reliable frames remain ordered and resume after the receiver drains its bounded queue");
}
Digest agreement(Pair& pair) {
#ifdef SG_TEST_WORLD
    World wa,wb;
#endif
    Seed s1{}, s2{}; s1[0] = 1; s2[0] = 2; SigningKey k1(s1),k2(s2);
    Committee committee{"region",0,{{"a",k1.public_key()},{"b",k2.public_key()}},2,0};
    Layout layout{{0,1,2},{0,1},{0,2},{0,1},{-1,1},{0,1,2},{0,0},{0,1}};
    LinearSystem system{layout}; system.observations = {2,10}; system.confidence = {0,0}; system.fixed = {0,0}; system.pins = {0,0}; system.overlap_weights = {1};
    SolverSpec spec; system.iterations = spec.iterations; CpuBackend backend; Verify verify; Agreement aa(committee),ab(committee); Integrity ia(committee),ib(committee);
    EpochContext context{"world","region",0,0,topology_hash(layout),constraint_hash(system,{}),spec.id(),committee.id(),{}};
    auto builder = [&](const auto& inputs){return Problem::make(context,system,spec,inputs);};
    std::vector<InputSlot> slots{{"a","left","value",0,false},{"b","right","value",0,false}};
    Protocol pa(committee,"a",k1,backend,context,slots,builder,verify,aa,ia),pb(committee,"b",k2,backend,context,slots,builder,verify,ab,ib);
    pa.submit({Observation{"a",{},0,0,"left","value",false,{},Bytes{2},{}}});
    pb.submit({Observation{"b",{},0,0,"right","value",false,{},Bytes{10},{}}});
    auto send = [](Protocol& p,IceTransport& t){ for (const auto& m : p.outgoing()) if (t.send({m.to,"agreement",DeliveryClass::Reliable,{},encode(m)}) != SendResult::Accepted) throw std::runtime_error("agreement transport backpressure"); };
    Inbound m;
    const bool good = pair.until([&]{
        send(pa,pair.a); send(pb,pair.b);
        while (pair.a.try_receive(m)) if (m.channel == "agreement") pa.receive(decode_message(m.bytes));
        while (pair.b.try_receive(m)) if (m.channel == "agreement") pb.receive(decode_message(m.bytes));
        // Flooding latest traffic uses a separate association from control.
        for (int n = 0; n < 10; ++n) pair.a.send({"b","boundary",DeliveryClass::Latest,"edge",Bytes(1024,static_cast<std::uint8_t>(n))});
        return pa.accepted() && pb.accepted();
    });
    if (!good) std::cerr << "protocol inputs " << pa.observations().size() << ',' << pb.observations().size() << " rejected " << pa.rejected() << ',' << pb.rejected() << " connected " << pair.a.telemetry("b").connected << ',' << pair.b.telemetry("a").connected << std::endl;
    check(good && pa.accepted()->values() == std::vector<double>({6,6}) && pb.accepted()->values() == pa.accepted()->values(),"control finalizes the unchanged canonical problem while boundary traffic runs");
    if (!good) return {};
    check(pa.finalized()->id() == pb.finalized()->id(),"both independently verified executors accept the same receipt");
#ifdef SG_TEST_WORLD
    check(wa.commit(pa) && wb.commit(pb),"only verified finalization crosses declared ports; ordinary worlds, strict mode and sg::verify agree");
#endif
    return pa.finalized()->id();
}
}
int main() {
    try {
        Pair direct; const auto receipt = agreement(direct);
        { Pair framing; messages(framing); }
        receive_backpressure();
#ifdef SG_TEST_TURN
        juice_set_log_level(JUICE_LOG_LEVEL_ERROR);
        juice_server_credentials_t credentials{"test","test",16}; juice_server_config_t config{};
        config.credentials = &credentials; config.credentials_count = 1; config.max_allocations = 32; config.max_peers = 64;
        config.bind_address = "127.0.0.1"; config.external_address = "127.0.0.1"; config.realm = "stategine-test";
        auto* server = juice_server_create(&config); if (!server) throw std::runtime_error("test TURN server failed");
        {
            const auto uri = "turn:test:test@127.0.0.1:"+std::to_string(juice_server_get_port(server));
            Pair relay({uri},true); const auto relayed = agreement(relay);
            // ICE may discover a peer-reflexive shortcut on one shared LAN.
            // The opaque TURN proxy below checks the actual data path instead
            // of assuming a selected-candidate label proves relay traffic.
            check(relayed == receipt && receipt != Digest{},"direct and TURN-rendezvoused paths produce identical finalized world receipts");
        }
        {
            IceFaults proxy(juice_server_get_port(server));
            Pair faulty({"turn:test:test@127.0.0.1:"+std::to_string(proxy.port())},true); faulty.faults = [&]{proxy.poll();};
            faulty.candidate = [&](const auto& value){return proxy.candidate(value);};
            check(faulty.until([&]{return faulty.a.telemetry("b").connected && faulty.b.telemetry("a").connected;}),"TURN fault path establishes mature encrypted sessions");
            proxy.enable(); const auto relayed = agreement(faulty);
            check(relayed == receipt && proxy.losses() > 0 && proxy.duplicates() > 0,"loss, duplication, reordering and transport timing preserve the finalized receipt");
            Bytes large(200*1024,17);
            const auto traffic_before = proxy.forwarded_bytes();
            for (int i = 0; i < 8; ++i) { large[0] = static_cast<std::uint8_t>(i); faulty.a.send({"b","ordered",DeliveryClass::Reliable,{},large}); }
            std::vector<int> order; Inbound message;
            const auto delivered = faulty.until([&]{ while (faulty.b.try_receive(message)) if (message.channel == "ordered") { if (message.bytes.size() != large.size() || message.bytes.back() != 17) return false; order.push_back(message.bytes[0]); } return order.size() == 8; },80000);
            if (!delivered) { auto a = faulty.a.telemetry("b"), b = faulty.b.telemetry("a"); std::cerr << "fault frames=" << order.size() << " route bytes=" << proxy.forwarded_bytes()-traffic_before << " losses=" << proxy.losses() << " duplicates=" << proxy.duplicates() << " connected=" << a.connected << ',' << b.connected << " queued=" << a.queued_reliable << " buffered=" << a.buffered << " SCTP bytes=" << a.sent_bytes << ',' << b.received_bytes << std::endl; }
            check(delivered && order == std::vector<int>({0,1,2,3,4,5,6,7}),"fragmented reliable messages survive real faulty UDP in stream order without duplication");
            check(proxy.forwarded_bytes()-traffic_before >= 8*large.size(),"large application traffic actually crosses the faulty TURN route rather than bypassing it on the LAN");
        }
        juice_server_destroy(server);
#else
        std::cout << "SKIP TURN fixture: installed libdatachannel does not expose its test server" << std::endl;
#endif
    } catch (const std::exception& e) { std::cerr << e.what() << std::endl; return 1; }
    return failures ? 1 : 0;
}
