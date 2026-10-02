#include "sg/web/WebRtc.hpp"
#include <emscripten/val.h>
#include <map>
#include <stdexcept>
#include <tuple>
namespace sg::web {
using emscripten::val;
struct WebRtcTransport::Impl {
    net::IceConfig config;
    val handle;
    using Route = std::tuple<std::string, std::string, net::DeliveryClass>;
    std::map<Route, net::OutboundQueue> streams;
    Impl(net::IceConfig c) : config(std::move(c)), handle(val::undefined()) {
        net::validate_ice(config);
        val options = val::object();
        options.set("peer", config.peer);
        options.set("maximum", config.maximum_message);
        options.set("receiveBytes", config.receive_bytes);
        options.set("receiveMessages", config.receive_messages);
        options.set("channels", config.channels);
        options.set("relayOnly", config.relay_only);
        val servers = val::array();
        for (std::size_t i = 0; i < config.ice_servers.size(); ++i)
            servers.set(i, config.ice_servers[i]);
        options.set("servers", servers);
        handle = val::global("sgRTC").call<val>("create", options);
    }
};
WebRtcTransport::WebRtcTransport(net::IceConfig c) : impl_(std::make_unique<Impl>(std::move(c))) {}
WebRtcTransport::~WebRtcTransport() { impl_->handle.call<void>("destroy"); }
void WebRtcTransport::connect(const std::string &p) { impl_->handle.call<void>("connect", p); }
void WebRtcTransport::disconnect(const std::string &p) { impl_->handle.call<void>("disconnect", p); }
void WebRtcTransport::reconnect(const std::string &p) { impl_->handle.call<void>("reconnect", p); }
net::SendResult WebRtcTransport::send(net::Outbound m) {
    if (m.bytes.size() > impl_->config.maximum_message)
        return net::SendResult::TooLarge;
    if (!impl_->handle.call<bool>("known", m.peer))
        throw std::invalid_argument("web: connect peer before sending");
    const auto lane = m.delivery;
    const Impl::Route route{m.peer, m.channel, lane};
    auto found = impl_->streams.find(route);
    if (found == impl_->streams.end()) {
        if (impl_->streams.size() >= impl_->config.channels)
            return net::SendResult::Blocked;
        found = impl_->streams.emplace(route, net::OutboundQueue{impl_->config.queues}).first;
    }
    std::size_t bytes = 0, count = 0;
    for (const auto &stream : impl_->streams) {
        bytes += stream.second.bytes(lane);
        count += stream.second.size(lane);
    }
    const auto old_bytes = found->second.bytes(lane), old_count = found->second.size(lane);
    auto trial = found->second;
    const auto result = trial.push(std::move(m));
    if (result != net::SendResult::Accepted)
        return result;
    const auto cap =
        lane == net::DeliveryClass::Reliable ? impl_->config.queues.reliable_bytes : impl_->config.queues.latest_bytes;
    const auto slots = lane == net::DeliveryClass::Reliable ? impl_->config.queues.reliable_messages
                                                            : impl_->config.queues.latest_slots;
    if (bytes - old_bytes + trial.bytes(lane) > cap || count - old_count + trial.size(lane) > slots)
        return net::SendResult::Blocked;
    found->second = std::move(trial);
    return result;
}
void WebRtcTransport::poll() {
    impl_->handle.call<void>("poll");
    for (auto &stream : impl_->streams) {
        const auto lane = std::get<2>(stream.first);
        for (unsigned i = 0; i < 16; ++i) {
            const auto *m = stream.second.front(lane);
            if (!m)
                break;
            const auto cap = lane == net::DeliveryClass::Reliable ? impl_->config.queues.reliable_bytes
                                                                  : impl_->config.queues.latest_bytes;
            const auto frame = net::ice_frame(m->channel, m->bytes);
            val data = val(emscripten::typed_memory_view(frame.size(), frame.data()));
            if (!impl_->handle.call<bool>("send", m->peer, lane == net::DeliveryClass::Latest, data, cap))
                break;
            stream.second.pop(lane);
        }
    }
}
bool WebRtcTransport::try_receive(net::Inbound &m) {
    const auto packet = impl_->handle.call<val>("receive");
    if (packet.isNull())
        return false;
    const auto data = packet["bytes"];
    net::Bytes frame(data["length"].as<unsigned>());
    val(emscripten::typed_memory_view(frame.size(), frame.data())).call<void>("set", data);
    m = net::ice_unframe(packet["peer"].as<std::string>(), frame, impl_->config.maximum_message);
    return true;
}
std::vector<net::IceSignal> WebRtcTransport::signals() {
    const auto signals = impl_->handle.call<val>("signals");
    std::vector<net::IceSignal> out;
    for (unsigned i = 0; i < signals["length"].as<unsigned>(); ++i) {
        const auto s = signals[i];
        out.push_back({s["peer"].as<std::string>(),
                       s["lane"].as<bool>() ? net::DeliveryClass::Latest : net::DeliveryClass::Reliable,
                       std::stoull(s["session"].as<std::string>()),
                       static_cast<net::IceSignalKind>(s["kind"].as<int>()), s["value"].as<std::string>(),
                       s["type"].as<std::string>()});
    }
    return out;
}
void WebRtcTransport::signal(const std::string &peer, const net::IceSignal &s) {
    if (s.value.size() > 65536 || s.type.size() > 128)
        throw std::length_error("web: excessive ICE signal");
    impl_->handle.call<void>("signal", peer, s.lane == net::DeliveryClass::Latest, std::to_string(s.session),
                             static_cast<int>(s.kind), s.value, s.type);
}
std::vector<net::TransportEvent> WebRtcTransport::events() {
    const auto events = impl_->handle.call<val>("events");
    std::vector<net::TransportEvent> out;
    for (unsigned i = 0; i < events["length"].as<unsigned>(); ++i) {
        const auto e = events[i];
        out.push_back({e["peer"].as<std::string>(), e["channel"].as<std::string>(),
                       static_cast<net::TransportEventKind>(e["kind"].as<int>())});
    }
    return out;
}
net::IceTelemetry WebRtcTransport::telemetry(const std::string &peer) const {
    auto t = impl_->handle.call<val>("telemetry", peer);
    net::IceTelemetry out;
    out.connected = t["connected"].as<bool>();
    out.buffered = t["buffered"].as<unsigned>();
    for (const auto &stream : impl_->streams)
        if (std::get<0>(stream.first) == peer) {
            out.queued_reliable += stream.second.bytes(net::DeliveryClass::Reliable);
            out.queued_latest += stream.second.bytes(net::DeliveryClass::Latest);
        }
    return out;
}
} // namespace sg::web
