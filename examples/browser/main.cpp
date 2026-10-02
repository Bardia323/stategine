// The existing Pong world, driven by a small browser application shell.
#include "../IceSignals.hpp"
#include "../Signed.hpp"
#include "../pong/Pong.hpp"
#include "../pong/Secure.hpp"
#include "sg/core/Engine.hpp"
#include "sg/core/Laws.hpp"
#include "sg/dsl/Facts.hpp"
#include "sg/web/Input.hpp"
#include "sg/web/WebGPU.hpp"
#include "sg/web/WebRtc.hpp"
#include <deque>
#include <emscripten.h>
#include <emscripten/val.h>
#include <map>
#include <set>
#include <sstream>
namespace sgen {
void build_pong(sg::StateGraph &, const sg::dsl::Natives &, sg::dsl::Bindings &);
}
namespace {
using namespace sg;
using emscripten::val;
struct Application {
    StateGraph graph;
    dsl::Bindings bindings;
    net::CpuBackend cpu;
    std::shared_ptr<net::Distributed> solver = std::make_shared<net::Distributed>();
    net::Committee committee;
    std::string peer, evidence;
    int player;
    std::unique_ptr<net::SigningKey> key;
    std::unique_ptr<net::Agreement> agreement;
    std::unique_ptr<net::Integrity> integrity;
    std::unique_ptr<net::Protocol> protocol;
    std::unique_ptr<net::Cellular> topology;
    std::unique_ptr<Engine> engine;
    std::unique_ptr<web::WebRtcTransport> transport;
    std::unique_ptr<web::Input> input;
    std::unique_ptr<web::WebGPUView> view;
    val socket = val::null();
    net::Prediction::Step rules;
    std::optional<std::uint64_t> requested;
    std::deque<net::Bytes> incoming;
#ifdef SG_WEB_TESTS
    std::deque<net::Inbound> probes;
#endif
    std::map<net::Digest, net::Bytes> signaling;
    std::set<net::Digest> seen;
    net::OutboundQueue pending;
    std::map<std::tuple<std::string, std::string, std::string>, net::Outbound> frontier;
    bool submitted = false;
    std::int64_t previous_epoch = 0;
    double signal_retry = 0;
    std::string error;
    Application(int who, const std::string &folder, const std::string &url)
        : committee(examples::read_committee(folder)), peer("p" + std::to_string(who)), player(who),
          key(examples::read_signing_key(folder, peer)) {
        if (who < 0 || who > 1 || committee.members.size() != 2 || committee.quorum != 2 || !committee.key(peer) ||
            *committee.key(peer) != key->public_key())
            throw std::invalid_argument("browser Pong requires the same pinned two-player committee");
        evidence = "sg.pong.journal:" + net::hex(committee.id()) + ":" + peer;
        if (val::global("sgEvidence").call<bool>("exists", evidence))
            throw std::runtime_error(
                "used identity requires verified recovery or a fresh committee; refusing to vote from tick zero");
        agreement = std::make_unique<net::Agreement>(committee, net::Digest{}, 0, [this](const net::Bytes &bytes) {
            val::global("sgEvidence").call<void>("store", evidence, net::hex(bytes));
        });
        integrity = std::make_unique<net::Integrity>(committee);
        sgen::build_pong(graph, examples::pong::natives(&cpu, solver), bindings);
        set_observers(Observers::Strict);
        graph.state("network").bus().subscribe("network.observation", [this](const Event &e) {
            requested = static_cast<std::uint64_t>(e.args.num("epoch"));
        });
        graph.state("network").bus().subscribe("network.outgoing", [this](const Event &e) {
            net::Outbound m{e.args.get_or<std::string>("to", {}), "signed", net::DeliveryClass::Reliable,
                            e.args.get_or<std::string>("slot", {}),
                            net::unhex(e.args.get_or<std::string>("packet", {}))};
            const auto slot = std::make_tuple(m.peer, m.channel, m.slot);
            if (!frontier.count(slot) && frontier.size() >= 128)
                throw std::length_error("browser protocol frontier full");
            if (pending.push(m) != net::SendResult::Accepted)
                throw std::runtime_error("browser reliable backpressure");
            frontier[slot] = std::move(m);
        });
        engine = std::make_unique<Engine>(graph);
        engine->set_strict(true);
        engine->start();
        auto c = examples::pong::configuration(who);
        c.set("secured", true)
            .set("committee", net::hex(committee.id()))
            .set("world", "pong:" + net::hex(committee.id()));
        engine->send("game", {"game.configure", c});
        engine->send("network", {"network.configure", c});
        engine->send("game", {"game.publish", {}});
        engine->tick(0);
        topology = std::make_unique<net::Cellular>(graph.state("network"));
        rules = examples::pong::forecast(graph.state("game"));
        transport = std::make_unique<web::WebRtcTransport>(net::IceConfig{peer, {}});
        transport->connect(who == 0 ? "p1" : "p0");
        socket = val::global("sgSocket").call<val>("create", url);
        input = std::make_unique<web::Input>("#canvas");
        view = std::make_unique<web::WebGPUView>("#canvas");
        const auto problems = view->prepare(graph);
        if (!problems.empty())
            throw std::runtime_error(problems.front());
    }
    ~Application() {
        if (!socket.isNull())
            socket.call<void>("destroy");
    }
    void external(double now) {
        socket.call<void>("poll");
        for (auto bytes = socket.call<val>("receive"); !bytes.isNull(); bytes = socket.call<val>("receive")) {
            net::Bytes data(bytes["length"].as<unsigned>());
            val(emscripten::typed_memory_view(data.size(), data.data())).call<void>("set", bytes);
            try {
                auto accepted = examples::verify_signal(committee, peer, data);
                if (accepted) {
                    const auto id = net::hash(data);
                    if (!seen.count(id)) {
                        if (seen.size() >= 4096)
                            throw std::length_error("signaling deduplication full");
                        transport->signal(accepted->from, accepted->signal);
                        seen.insert(id);
                    }
                }
            } catch (const std::invalid_argument &) {
            }
        }
        for (const auto &signal : transport->signals()) {
            auto bytes = examples::sign_signal(committee, peer, signal, *key);
            if (signaling.size() >= 256)
                throw std::length_error("signaling frontier full");
            signaling.emplace(net::hash(bytes), std::move(bytes));
        }
        if (now >= signal_retry) {
            for (const auto &item : signaling)
                if (!socket.call<bool>("send",
                                       val(emscripten::typed_memory_view(item.second.size(), item.second.data()))))
                    break;
            signal_retry = now + 500;
        }
        transport->poll();
        for (const auto &event : transport->events())
            if (event.kind == net::TransportEventKind::DeliveryUncertain)
                for (const auto &item : frontier)
                    if (pending.push(item.second) != net::SendResult::Accepted)
                        throw std::runtime_error("reconnection replay blocked");
        while (const auto *head = pending.front(net::DeliveryClass::Reliable)) {
            const auto result = transport->send(*head);
            if (result == net::SendResult::Blocked)
                break;
            if (result != net::SendResult::Accepted)
                throw std::runtime_error("browser message refused");
            pending.pop(net::DeliveryClass::Reliable);
        }
        net::Inbound packet;
        while (transport->try_receive(packet)) {
            if (packet.channel != "signed") {
#ifdef SG_WEB_TESTS
                if (probes.size() >= 512)
                    throw std::length_error("browser probe queue full");
                probes.push_back(std::move(packet));
#endif
                continue;
            }
            if (incoming.size() >= 512)
                throw std::length_error("browser protocol receive full");
            incoming.push_back(std::move(packet.bytes));
        }
    }
    void tick(double dt, bool scripted) {
        input->poll(*engine, bindings);
        if (scripted)
            engine->send("game", {"game.input", examples::pong::scripted_input(graph.state("game"), player)});
        engine->tick(dt);
        const auto epoch = graph.state("game").params().get_or<std::int64_t>("epoch", 0);
        if (epoch != previous_epoch) {
            if (!protocol || !protocol->accepted() ||
                examples::pong::frame_bytes(examples::pong::frame(graph.state("game"))) !=
                    protocol->accepted()->checkpoint())
                throw std::runtime_error("browser motion checkpoint differs from canonical verification");
            protocol.reset();
            submitted = false;
            previous_epoch = epoch;
        }
        if (requested && *requested == static_cast<std::uint64_t>(epoch) && !protocol) {
            topology->update(graph.state("network"));
            auto work = examples::pong::work(graph.state("network"), *topology, rules, committee, *agreement);
            protocol = std::make_unique<net::Protocol>(committee, peer, *key, cpu, work.context, work.slots, work.build,
                                                       work.verify, *agreement, *integrity);
            protocol->submit(std::move(work.local));
            requested.reset();
        }
        while (protocol && !incoming.empty()) {
            std::optional<net::Message> message;
            try {
                message = net::decode_message(incoming.front());
            } catch (const std::invalid_argument &) {
            }
            if (message)
                protocol->receive(*message);
            incoming.pop_front();
        }
        if (protocol) {
            for (const auto &message : protocol->outgoing()) {
                Params dispatch;
                dispatch.set("to", message.to)
                    .set("slot", std::to_string(static_cast<int>(message.kind)))
                    .set("packet", net::hex(net::encode(message)));
                engine->send("network", {"network.dispatch", dispatch});
            }
            if (protocol->accepted() && !submitted) {
                engine->send("network", {"network.finalized", examples::pong::finalization(*protocol)});
                submitted = true;
            }
        }
    }
    std::string status() const {
        std::ostringstream s;
        s << "{\"epoch\":" << graph.state("game").params().num("epoch") << ",\"receipt\":\""
          << graph.state("network").params().get_or<std::string>("previous", {})
          << "\",\"ready\":" << (view->ready() ? "true" : "false")
          << ",\"connected\":" << (transport->telemetry(player == 0 ? "p1" : "p0").connected ? "true" : "false")
          << ",\"revision\":" << graph.revision() << ",\"facts\":\"";
        net::Bytes bytes;
        for (const auto &fact : dsl::facts(graph)) {
            bytes.insert(bytes.end(), fact.begin(), fact.end());
            bytes.push_back('\n');
        }
        s << net::hex(net::hash(bytes)) << "\"}";
        return s.str();
    }
};
std::unique_ptr<Application> application;
std::string result;
} // namespace
extern "C" {
EMSCRIPTEN_KEEPALIVE int sg_start(int player, const char *folder, const char *url) {
    try {
        application = std::make_unique<Application>(player, folder, url);
        return 1;
    } catch (const std::exception &e) {
        result = e.what();
        return 0;
    }
}
EMSCRIPTEN_KEEPALIVE const char *sg_error() { return result.c_str(); }
EMSCRIPTEN_KEEPALIVE const char *sg_status() {
    try {
        result = application ? application->status() : "{}";
        return result.c_str();
    } catch (const std::exception &e) {
        result = e.what();
        return "{}";
    }
}
EMSCRIPTEN_KEEPALIVE int sg_step(double dt, int scripted) {
    try {
        if (!application)
            return 0;
        application->tick(dt, scripted);
        return 1;
    } catch (const std::exception &e) {
        result = e.what();
        return 0;
    }
}
EMSCRIPTEN_KEEPALIVE int sg_poll(double schedule_ms) {
    try {
        if (!application)
            return 0;
        application->external(schedule_ms);
        return 1;
    } catch (const std::exception &e) {
        result = e.what();
        return 0;
    }
}
EMSCRIPTEN_KEEPALIVE int sg_draw(int w, int h, double dt) {
    try {
        if (!application)
            return 0;
        application->view->render(application->graph, application->graph.state("game"), w, h, dt);
        const auto errors = application->view->diagnostics();
        if (!errors.empty())
            throw std::runtime_error(errors.front());
        return 1;
    } catch (const std::exception &e) {
        result = e.what();
        return 0;
    }
}
EMSCRIPTEN_KEEPALIVE void sg_recreate_gpu() {
    if (application)
        application->view->recreate();
}
EMSCRIPTEN_KEEPALIVE void sg_reconnect() {
    if (application)
        application->transport->reconnect(application->player == 0 ? "p1" : "p0");
}
EMSCRIPTEN_KEEPALIVE void sg_disconnect() {
    if (application)
        application->transport->disconnect(application->player == 0 ? "p1" : "p0");
}
EMSCRIPTEN_KEEPALIVE int sg_verify() {
    if (!application)
        return 0;
    LawOptions probes;
    probes.args_for["network.solve"].set("dt", 1.0 / 60);
    probes.args_for["game.motion"].set("duration", 1.0 / 60);
    return application->graph.validate().empty() && sg::verify(application->graph, {}, probes).holds();
}
#ifdef SG_WEB_TESTS
EMSCRIPTEN_KEEPALIVE double sg_input_value() {
    return application->graph.state("game")
        .element("p" + std::to_string(application->player))
        .params.num("value_" + std::to_string(application->player));
}
EMSCRIPTEN_KEEPALIVE int sg_probe(const char *channel, const char *slot, const char *bytes, int latest) {
    try {
        return static_cast<int>(application->transport->send(
            {application->player == 0 ? "p1" : "p0", channel,
             latest ? net::DeliveryClass::Latest : net::DeliveryClass::Reliable, slot, net::unhex(bytes)}));
    } catch (const std::exception &e) {
        result = e.what();
        return -1;
    }
}
EMSCRIPTEN_KEEPALIVE const char *sg_probe_receive() {
    if (application->probes.empty())
        return "";
    result = application->probes.front().channel + ":" + net::hex(application->probes.front().bytes);
    application->probes.pop_front();
    return result.c_str();
}
#endif
}
int main() { return 0; }
