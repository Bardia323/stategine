#include "Distributed.hpp"
#include "Udp.hpp"
#include "sg/core/Engine.hpp"
#include "sg/core/Laws.hpp"
#include "sg/dsl/Runtime.hpp"
#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <thread>

namespace sgen { void build_distributed(sg::StateGraph&, const sg::dsl::Natives&, sg::dsl::Bindings&); }
int main(int argc, char** argv) {
    try {
        if (argc < 5) { std::cerr << "usage: sg_net_peer rank count rounds output [cpu|cuda] [base-port] [comma-separated-hosts]\n"; return 2; }
        const int rank = std::stoi(argv[1]), count = std::stoi(argv[2]), rounds = std::stoi(argv[3]);
        const int base = argc > 6 ? std::stoi(argv[6]) : 49000;
        if (rank < 0 || rank >= count || count < 1 || count > 6 || rounds < 1 || base < 1024 || base+count > 65535) throw std::invalid_argument("invalid peer execution arguments");
        std::vector<sg::net::Peer> peers;
        std::stringstream hosts(argc > 7 ? argv[7] : "");
        for (int i = 0; i < count; ++i) { std::string host; if (!std::getline(hosts, host, ',') || host.empty()) host = "127.0.0.1"; peers.push_back({"p"+std::to_string(i), host}); }
        std::unique_ptr<sg::net::Backend> backend;
        const std::string mode = argc > 5 ? argv[5] : "cpu";
        if (mode != "cuda" && mode != "cpu") throw std::invalid_argument("backend must be cpu or cuda");
        if (mode == "cuda") {
            backend = std::make_unique<sg::net::CudaBackend>();
            if (!backend->available()) { std::cout << "SKIP CUDA peer: unavailable\n"; return 77; }
        } else backend = std::make_unique<sg::net::CpuBackend>();
        sg::examples::Udp transport(rank, peers, base);
        sg::StateGraph graph; sg::dsl::Bindings bindings;
        auto solver = std::make_shared<sg::net::Distributed>();
        sgen::build_distributed(graph, sg::examples::distributed_natives(backend.get(), solver), bindings);
        sg::set_observers(sg::Observers::Strict);
        graph.state("network").bus().subscribe("network.boundary", [&](const sg::Event& e) {
            const auto bytes = sg::net::Exchange::bytes(e.args); const auto packet = sg::net::Exchange::decode(bytes);
            if (transport.send({packet.to,"boundary",sg::net::DeliveryClass::Latest,std::to_string(packet.step.tick%2),bytes}) != sg::net::SendResult::Accepted) throw std::runtime_error("UDP boundary backpressure");
        });
        sg::Engine engine(graph); engine.set_strict(true); engine.start();
        engine.fire("game.publish"); engine.tick(0);
        sg::Params configuration; configuration.set("peer", peers[rank].id).set("omit_remote", true);
        const char* names[] = {"a","b","c","d","e","f"};
        for (int i = 0; i < 6; ++i) configuration.set(names[i], peers[i*count/6].id);
        engine.send("network", {"network.configure", configuration}); engine.tick(0);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
        while (graph.state("network").params().num("round") < rounds) {
            sg::net::Inbound received;
            while (transport.try_receive(received)) if (received.channel == "boundary") engine.send("network", {"network.receive", sg::net::Exchange::arguments(received.bytes)});
            engine.tick(0.01); transport.retry();
            if (std::chrono::steady_clock::now() > deadline) throw std::runtime_error("neighbor boundary timeout");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        // A peer can be one round ahead. Keep its last two said messages
        // available briefly so a neighbor can finish the same final round.
        const auto flush = std::chrono::steady_clock::now() + std::chrono::milliseconds(350);
        while (std::chrono::steady_clock::now() < flush) { transport.retry(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        sg::LawOptions probes; probes.args_for["network.solve"].set("dt", 0.01);
        if (!graph.validate().empty() || !sg::verify(graph, {}, probes).holds()) throw std::runtime_error("peer graph failed its laws");
        const std::filesystem::path path(argv[4]);
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path); out << std::setprecision(17);
        for (const auto v : solver->partition().stalks()) {
            const auto& stalk = solver->cellular().stalks()[v]; const auto& p = graph.state("network").element(stalk.element).params;
            for (std::uint32_t c = 0; c < stalk.dimension; ++c) out << stalk.element.str() << ' ' << c << ' ' << p.num(sg::net::Cellular::coordinate_key("result", stalk.dimension, c)) << '\n';
        }
        if (!out) throw std::runtime_error("peer result output failed");
        std::cout << peers[rank].id << " settled round " << rounds << " with " << solver->partition().stalks().size() << " local stalks; strict laws pass\n";
        engine.stop(); return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
