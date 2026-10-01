#include "Pong.hpp"
#include "Secure.hpp"
#include "../Signed.hpp"
#include "../Udp.hpp"
#include "sg/core/Engine.hpp"
#include "sg/core/Laws.hpp"
#include "sg/dsl/Runtime.hpp"
#ifdef SG_PONG_GL
#include "View.hpp"
#endif
#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <thread>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace sgen { void build_pong(sg::StateGraph&, const sg::dsl::Natives&, sg::dsl::Bindings&); }
namespace {
// OS scheduling precision for external socket polling, not a world clock.
struct Scheduling {
#ifdef _WIN32
    HANDLE timer = nullptr;
#endif
    Scheduling() {
#ifdef _WIN32
        // High-resolution waits also work when Windows occludes a headless
        // test's window and ignores its requested periodic timer resolution.
        timer = CreateWaitableTimerExW(nullptr,nullptr,0x2,TIMER_ALL_ACCESS);
        if (!timer) throw std::runtime_error("high-resolution socket scheduler unavailable");
#endif
    }
    ~Scheduling() {
#ifdef _WIN32
        CloseHandle(timer);
#endif
    }
    void pause() const {
#ifdef _WIN32
        LARGE_INTEGER due; due.QuadPart = -10000;
        if (!SetWaitableTimer(timer,&due,0,nullptr,nullptr,FALSE) || WaitForSingleObject(timer,INFINITE) != WAIT_OBJECT_0) throw std::runtime_error("socket scheduler wait failed");
#else
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
    }
};
struct Options {
    int player = -1, port = 49270, steps = 0, delay = 0;
    bool headless = false, scripted = false;
    std::string backend = "auto";
    std::string hosts = "127.0.0.1,127.0.0.1";
    std::filesystem::path out, shot, credentials;
};
Options options(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        const auto value = [&]() { if (++i == argc) throw std::invalid_argument("missing value for "+arg); return std::string(argv[i]); };
        if (arg == "--player") o.player = std::stoi(value());
        else if (arg == "--port") o.port = std::stoi(value());
        else if (arg == "--steps") o.steps = std::stoi(value());
        else if (arg == "--delay-ms") o.delay = std::stoi(value());
        else if (arg == "--backend") o.backend = value();
        else if (arg == "--hosts") o.hosts = value();
        else if (arg == "--credentials") o.credentials = value();
        else if (arg == "--out") o.out = value();
        else if (arg == "--shot") o.shot = value();
        else if (arg == "--headless") o.headless = true;
        else if (arg == "--scripted") o.scripted = true;
        else throw std::invalid_argument("unknown Pong option: "+arg);
    }
    if (o.player < 0 || o.player > 1 || o.port < 1024 || o.port > 65534 || o.steps < 0 || o.delay < 0 || o.delay > 1000) throw std::invalid_argument("usage: sg_net_pong --player 0|1 [--backend auto|cpu|cuda] [--port 49270] [--headless --steps 600 --out report.json]");
    if (o.backend != "auto" && o.backend != "cpu" && o.backend != "cuda") throw std::invalid_argument("Pong backend must be auto, cpu, or cuda");
    if (o.headless && o.steps == 0) o.steps = 600;
    if (o.headless) o.scripted = true;
    if (o.credentials.empty()) throw std::invalid_argument("Pong requires --credentials folder (generate with sg_net_keys folder 2 2 0 pong)");
    return o;
}
void report(const std::filesystem::path& path, const sg::State& game, const sg::State& network, std::size_t sent, std::size_t received, std::uint64_t lead, std::uint64_t replayed, std::size_t rejected) {
    if (path.empty()) return;
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path); out << std::setprecision(17) << "{\n";
    for (const char* key : {"epoch", "left_score", "right_score", "hits"}) out << '"' << key << "\": " << game.params().num(key) << ",\n";
    for (const char* name : {"ball", "left", "right"}) for (const char* key : {"x", "y", "vx", "vy"}) out << '"' << name << '_' << key << "\": " << game.element(name).params.num(key) << ",\n";
    const auto remote = network.params().get_or<std::string>("peer", "p0") == "p0" ? "p1" : "p0";
    out << "\"remote_observations\": " << (network.element(remote).params.has("observation_0") ? "true" : "false")
        << ",\n\"sent\": " << sent << ",\n\"received\": " << received << ",\n\"maximum_prediction_lead\": " << lead
        << ",\n\"replayed\": " << replayed << ",\n\"rejected\": " << rejected << ",\n\"receipt\": \"" << network.params().get_or<std::string>("previous",{}) << "\"\n}\n";
    if (!out) throw std::runtime_error("Pong report failed");
}
}
int main(int argc, char** argv) {
    try {
        const auto o = options(argc, argv);
        const Scheduling scheduling;
        std::unique_ptr<sg::net::Backend> backend;
        std::string mode = o.backend;
        if (mode != "cpu") {
            backend = std::make_unique<sg::net::CudaBackend>();
            if (backend->available()) mode = "cuda";
            else if (mode == "cuda") { std::cout << "SKIP CUDA Pong: unavailable\n"; return 77; }
            else mode = "cpu";
        }
        if (mode == "cpu") backend = std::make_unique<sg::net::CpuBackend>();
        const auto committee = sg::examples::read_committee(o.credentials);
        const auto peer = "p"+std::to_string(o.player);
        if (committee.members.size() != 2 || committee.quorum != 2 || !committee.key("p0") || !committee.key("p1")) throw std::invalid_argument("Pong requires the two-player regional committee");
        const auto key = sg::examples::read_signing_key(o.credentials,peer);
        sg::examples::Journal journal(o.credentials,peer);
        const auto comma = o.hosts.find(',');
        if (comma == std::string::npos || comma == 0 || comma+1 == o.hosts.size()) throw std::invalid_argument("--hosts requires the addresses of p0,p1");
        sg::examples::Udp transport(o.player, {{"p0",o.hosts.substr(0,comma)},{"p1",o.hosts.substr(comma+1)}}, o.port);
        sg::StateGraph graph; sg::dsl::Bindings bindings;
        auto solver = std::make_shared<sg::net::Distributed>();
        sgen::build_pong(graph, sg::examples::pong::natives(backend.get(),solver), bindings);
        sg::set_observers(sg::Observers::Strict);
        std::size_t sent = 0, received = 0;
        std::optional<std::uint64_t> requested;
        std::deque<sg::Params> forecasts;
        graph.state("network").bus().subscribe("network.observation", [&](const sg::Event& e) { requested = static_cast<std::uint64_t>(e.args.num("epoch")); });
        graph.state("network").bus().subscribe("network.forecast", [&](const sg::Event& e) { forecasts.push_back(e.args); });
        graph.state("network").bus().subscribe("network.outgoing", [&](const sg::Event& e) {
            transport.send_to(e.args.get_or<std::string>("to",{}),sg::net::unhex(e.args.get_or<std::string>("packet",{})),e.args.get_or<std::string>("slot",{})); ++sent;
        });
        sg::Engine engine(graph); engine.set_strict(true); engine.start();
        auto config = sg::examples::pong::configuration(o.player);
        config.set("secured",true).set("committee",sg::net::hex(committee.id())).set("world","pong:"+sg::net::hex(committee.id()));
        engine.send("game", {"game.configure",config}); engine.send("network", {"network.configure",config});
        engine.send("game", {"game.publish",{}}); engine.tick(0);
        const auto rules = sg::examples::pong::forecast(graph.state("game"));
        sg::net::Cellular topology(graph.state("network"));
        sg::net::Prediction prediction(rules,12,128,{1,1,1,1,1,1,1,1,0,0,0});
        prediction.reset(0,sg::examples::pong::frame(graph.state("game")));
        sg::net::Agreement agreement(committee,{},0,[&](const sg::net::Bytes& snapshot) { journal.store(snapshot); }); sg::net::Integrity integrity(committee);
        std::unique_ptr<sg::net::Protocol> protocol;
        sg::net::DiscreteInputs predicted_inputs{0,0};
        std::uint64_t maximum_lead = 0, replayed = 0;
        std::size_t rejected = 0;
        double temporal_time = 0;
        bool submitted_final = false;
#ifdef SG_PONG_GL
        std::unique_ptr<sg::examples::pong::View> view;
        if (!o.headless) view = std::make_unique<sg::examples::pong::View>(o.player,mode);
#else
        if (!o.headless) throw std::runtime_error("This build has no GLFW; use --headless or build with SG_BUILD_GL=ON");
#endif
        using Clock = std::chrono::steady_clock;
        auto next_step = Clock::now(), next_picture = next_step, last_progress = next_step;
        const auto deadline = next_step + std::chrono::seconds(120);
        std::int64_t previous_epoch = 0;
        struct Delivery { Clock::time_point due; sg::net::Bytes bytes; };
        std::deque<Delivery> delivery;
        bool open = true;
        while (open && (o.steps == 0 || graph.state("game").params().num("epoch") < o.steps)) {
            const auto now = Clock::now();
            sg::Params input;
            if (o.scripted) input = sg::examples::pong::scripted_input(graph.state("game"),o.player);
#ifdef SG_PONG_GL
            else if (view) { open = view->poll(); input.set("move",view->input()); }
            if (view && o.scripted) open = view->poll();
#endif
            engine.send("game", {"game.input",input});
            sg::net::Bytes bytes;
            while (transport.try_receive(bytes)) {
                // A test can delay and duplicate packets without touching the world.
                delivery.push_back({now+std::chrono::milliseconds(o.delay),bytes}); ++received;
            }
            const bool interval = o.headless || now >= next_step;
            engine.tick(interval ? 1.0/60.0 : 0.0);
            if (interval) next_step = now+std::chrono::microseconds(16667);
            const auto advanced = graph.state("game").params().get_or<std::int64_t>("epoch",0);
            if (advanced != previous_epoch) {
                const auto actual = sg::examples::pong::frame(graph.state("game"));
                if (!protocol || !protocol->accepted() || sg::examples::pong::frame_bytes(actual) != protocol->accepted()->checkpoint()) throw std::runtime_error("Pong's declared motion did not produce the independently verified checkpoint");
                replayed += prediction.reconcile(advanced,actual,temporal_time).replayed;
                predicted_inputs = sg::examples::pong::controls(protocol->accepted()->epoch().ordered_inputs);
                rejected += protocol->rejected(); protocol.reset(); submitted_final = false;
                previous_epoch = advanced; last_progress = now;
            }
            if (requested && *requested == static_cast<std::uint64_t>(advanced) && !protocol) {
                topology.update(graph.state("network"));
                auto work = sg::examples::pong::work(graph.state("network"),topology,rules,committee,agreement);
                protocol = std::make_unique<sg::net::Protocol>(committee,peer,*key,*backend,work.context,work.slots,work.build,work.verify,agreement,integrity);
                protocol->submit(std::move(work.local)); requested.reset();
            }
            while (protocol && !delivery.empty() && delivery.front().due <= now) {
                std::optional<sg::net::Message> message;
                try {
                    message = sg::net::decode_message(delivery.front().bytes);
                } catch (const std::exception&) { ++rejected; }
                // Decode errors are untrusted input. Execution/storage errors
                // must reach the caller instead of masquerading as bad packets.
                if (message) { protocol->receive(*message); if (o.delay) protocol->receive(*message); }
                delivery.pop_front();
            }
            if (delivery.size() > 512) { delivery.pop_front(); ++rejected; }
            // A forecast may use authenticated pending controls, never unsigned packets.
            if (protocol) for (const auto& observation : protocol->observations()) if (observation.discrete) {
                const auto known = sg::examples::pong::controls({observation});
                const auto player = observation.peer == "p0" ? 0 : 1;
                predicted_inputs[player] = known[player];
                prediction.amend(observation.tick+1,predicted_inputs,temporal_time);
            }
            while (!forecasts.empty()) {
                const auto args = forecasts.front(); forecasts.pop_front(); temporal_time = args.num("time");
                predicted_inputs[o.player] = static_cast<std::int64_t>(args.num("input"));
                prediction.advance(predicted_inputs);
                maximum_lead = std::max(maximum_lead,prediction.predicted_tick()-prediction.finalized_tick());
            }
            if (protocol) {
                for (const auto& message : protocol->outgoing()) {
                    sg::Params dispatch; dispatch.set("to",message.to).set("slot",std::to_string(static_cast<int>(message.kind))).set("packet",sg::net::hex(sg::net::encode(message)));
                    engine.send("network",{"network.dispatch",dispatch});
                }
                if (protocol->accepted() && !submitted_final) {
                    // This is the only remote-origin world input. Signatures,
                    // a complete reference solve and quorum have all passed.
                    engine.send("network",{"network.finalized",sg::examples::pong::finalization(*protocol)});
                    submitted_final = true;
                }
            }
            transport.retry();
#ifdef SG_PONG_GL
            if (view && now >= next_picture) { const auto picture = prediction.display(temporal_time); view->draw(graph.state("game"),graph.state("network"),&picture,prediction.predicted_tick()-prediction.finalized_tick()); next_picture = now+std::chrono::microseconds(16667); }
#endif
            if (o.steps && (now > deadline || now-last_progress > std::chrono::seconds(5))) {
                throw std::runtime_error("Pong verified neighbor timeout: epoch="+std::to_string(advanced)+" sent="+std::to_string(sent)+" received="+std::to_string(received)+" rejected="+std::to_string(rejected+(protocol ? protocol->rejected() : 0)));
            }
            scheduling.pause();
        }
        if (o.steps) {
            const auto flush = Clock::now()+std::chrono::milliseconds(400+2*o.delay);
            while (Clock::now() < flush) { transport.retry(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        }
        sg::LawOptions probes; probes.args_for["network.solve"].set("dt",1.0/60.0); probes.args_for["game.motion"].set("duration",1.0/60.0);
        const auto laws = sg::verify(graph,{},probes);
        if (!graph.validate().empty() || !laws.holds()) { std::cerr << laws.str() << '\n'; throw std::runtime_error("Pong failed strict graph validation or its laws"); }
        report(o.out,graph.state("game"),graph.state("network"),sent,received,maximum_lead,replayed,rejected);
#ifdef SG_PONG_GL
        if (view && !o.shot.empty()) { view->draw(graph.state("game"),graph.state("network")); view->shot(o.shot); }
#endif
        std::cout << "Pong p" << o.player << ' ' << mode << " step " << graph.state("game").params().num("epoch") << "; strict validation and sg::verify pass\n";
        engine.stop(); return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
