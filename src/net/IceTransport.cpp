#include "sg/net/IceTransport.hpp"
#include <rtc/rtc.hpp>
#include <algorithm>
#include <array>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <tuple>

namespace sg::net {
struct IceTransport::Impl : std::enable_shared_from_this<IceTransport::Impl> {
    struct Stream {
        std::string peer, channel;
        DeliveryClass lane;
        OutboundQueue queue;
        bool sent = false;
        Stream(std::string p, std::string c, DeliveryClass d, QueueLimits limits) : peer(std::move(p)),channel(std::move(c)),lane(d),queue(limits) {}
    };
    struct Session {
        std::uint64_t id = 0;
        std::shared_ptr<rtc::PeerConnection> pc;
        std::shared_ptr<rtc::DataChannel> negotiation;
        std::vector<rtc::Candidate> candidates;
        bool failed = false, paused = false, description = false;
    };
    using Route = std::tuple<std::string,std::string,DeliveryClass>;
    IceConfig config;
    mutable std::mutex mutex;
    std::map<std::string,std::array<std::shared_ptr<Session>,2>> peers;
    std::map<Route,std::shared_ptr<Stream>> streams;
    std::vector<IceSignal> signaling;
    std::vector<TransportEvent> notices;
    std::deque<Inbound> received;
    std::size_t receive_bytes = 0;
    bool closing = false;
    explicit Impl(IceConfig c) : config(std::move(c)) {
        if (config.peer.empty() || config.maximum_message == 0 || config.maximum_message > 16*1024*1024 || config.receive_bytes < config.maximum_message || config.receive_messages == 0 || config.channels == 0 || config.channels > 1024) throw std::invalid_argument("net: invalid ICE transport bounds");
    }
    static std::size_t lane(DeliveryClass d) { return d == DeliveryClass::Reliable ? 0 : 1; }
    void notify(const std::string& peer, const std::string& channel, TransportEventKind kind) {
        // Coalesce notices too; neither an outage nor a hostile remote may
        // grow callback queues without bound. All application IO is polling.
        if (std::none_of(notices.begin(),notices.end(),[&](const auto& n){return n.peer == peer && n.channel == channel && n.kind == kind;})) notices.push_back({peer,channel,kind});
    }
    std::shared_ptr<Session> current(const std::string& peer, DeliveryClass d, std::uint64_t id) {
        const auto p = peers.find(peer); if (p == peers.end()) return {};
        const auto s = p->second[lane(d)]; return s && s->id == id && !s->paused && !closing ? s : nullptr;
    }
    void emit(IceSignal s) {
        std::lock_guard<std::mutex> lock(mutex);
        const auto session = current(s.peer,s.lane,s.session); if (!session) return;
        if (signaling.size() >= config.channels*16) { session->failed = true; notify(s.peer,{},TransportEventKind::Disconnected); return; }
        signaling.push_back(std::move(s));
    }
    void attach(const std::string& peer, DeliveryClass d, std::uint64_t id, const std::shared_ptr<rtc::DataChannel>& dc) {
        std::lock_guard<std::mutex> lock(mutex);
        const auto s = current(peer,d,id); if (!s) return;
        if (dc->label() != "_sg" || s->negotiation) { s->failed = true; return; }
        s->negotiation = dc;
    }
    void install(const std::string& peer, DeliveryClass d, std::uint64_t id, bool paused = false) {
        std::shared_ptr<Session> old;
        auto s = std::make_shared<Session>(); s->id = id; s->paused = paused;
        {
            std::lock_guard<std::mutex> lock(mutex);
            old = peers[peer][lane(d)]; peers[peer][lane(d)] = s;
            for (auto& [route,stream] : streams) { (void)route; if (stream->peer == peer && stream->lane == d) {
                if (stream->sent && d == DeliveryClass::Reliable) notify(peer,stream->channel,TransportEventKind::DeliveryUncertain);
                stream->sent = false;
            } }
            if (old) notify(peer,{},TransportEventKind::Disconnected);
        }
        if (old && old->negotiation) old->negotiation->resetCallbacks();
        if (old && old->pc) { old->pc->resetCallbacks(); old->pc->close(); }
        if (paused) return;
        rtc::Configuration rtc_config;
        for (const auto& server : config.ice_servers) rtc_config.iceServers.emplace_back(server);
        rtc_config.iceTransportPolicy = config.relay_only ? rtc::TransportPolicy::Relay : rtc::TransportPolicy::All;
        rtc_config.maxMessageSize = config.maximum_message+129;
        auto pc = std::make_shared<rtc::PeerConnection>(rtc_config); s->pc = pc;
        const std::weak_ptr<Impl> weak = shared_from_this();
        pc->onLocalDescription([weak,peer,d,id](rtc::Description description) { if (auto self = weak.lock()) self->emit({peer,d,id,IceSignalKind::Description,std::string(description),description.typeString()}); });
        pc->onLocalCandidate([weak,peer,d,id](rtc::Candidate candidate) { if (auto self = weak.lock()) self->emit({peer,d,id,IceSignalKind::Candidate,candidate.candidate(),candidate.mid()}); });
        pc->onDataChannel([weak,peer,d,id](auto dc) { if (auto self = weak.lock()) self->attach(peer,d,id,dc); });
        pc->onStateChange([weak,peer,d,id](rtc::PeerConnection::State state) {
            if (auto self = weak.lock()) {
                std::lock_guard<std::mutex> lock(self->mutex); const auto s = self->current(peer,d,id); if (!s) return;
                if (state == rtc::PeerConnection::State::Connected) self->notify(peer,{},TransportEventKind::Reconnected);
                if (state == rtc::PeerConnection::State::Disconnected || state == rtc::PeerConnection::State::Failed || state == rtc::PeerConnection::State::Closed) self->notify(peer,{},TransportEventKind::Disconnected);
                if (state == rtc::PeerConnection::State::Failed || state == rtc::PeerConnection::State::Closed) s->failed = true;
            }
        });
        if (config.peer < peer) {
            rtc::DataChannelInit init; init.reliability.unordered = d == DeliveryClass::Latest;
            if (d == DeliveryClass::Latest) init.reliability.maxRetransmits = 0;
            s->negotiation = pc->createDataChannel("_sg",init); // stable offerer, no game authority
        }
    }
    void stop() {
        std::vector<std::shared_ptr<rtc::PeerConnection>> pcs;
        std::vector<std::shared_ptr<rtc::DataChannel>> dcs;
        {
            std::lock_guard<std::mutex> lock(mutex); closing = true;
            for (const auto& [peer,sessions] : peers) { (void)peer; for (const auto& s : sessions) if (s && s->pc) { pcs.push_back(s->pc); if (s->negotiation) dcs.push_back(s->negotiation); } }
        }
        for (auto& dc : dcs) dc->resetCallbacks();
        for (auto& pc : pcs) { pc->resetCallbacks(); pc->close(); }
    }
};
IceTransport::IceTransport(IceConfig c) : impl_(std::make_shared<Impl>(std::move(c))) {}
IceTransport::~IceTransport() { impl_->stop(); }
void IceTransport::connect(const std::string& peer) {
    if (peer.empty() || peer == impl_->config.peer || peer.size() > 4096) throw std::invalid_argument("net: invalid ICE peer");
    { std::lock_guard<std::mutex> lock(impl_->mutex); if (impl_->peers.count(peer)) return; if (impl_->peers.size() >= impl_->config.channels) throw std::length_error("net: too many ICE peers"); }
    impl_->install(peer,DeliveryClass::Reliable,0); impl_->install(peer,DeliveryClass::Latest,0);
}
void IceTransport::disconnect(const std::string& peer) {
    for (auto d : {DeliveryClass::Reliable,DeliveryClass::Latest}) {
        std::uint64_t id; { std::lock_guard<std::mutex> lock(impl_->mutex); id = impl_->peers.at(peer)[Impl::lane(d)]->id; }
        if (id == UINT64_MAX) throw std::overflow_error("net: ICE session overflow");
        impl_->install(peer,d,id+1,true);
    }
}
void IceTransport::reconnect(const std::string& peer) {
    for (auto d : {DeliveryClass::Reliable,DeliveryClass::Latest}) {
        std::uint64_t id; { std::lock_guard<std::mutex> lock(impl_->mutex); id = impl_->peers.at(peer)[Impl::lane(d)]->id; }
        if (id == UINT64_MAX) throw std::overflow_error("net: ICE session overflow");
        impl_->install(peer,d,id+1); impl_->emit({peer,d,id+1,IceSignalKind::Restart,{},{}});
    }
}
std::vector<IceSignal> IceTransport::signals() { std::lock_guard<std::mutex> lock(impl_->mutex); std::vector<IceSignal> out; out.swap(impl_->signaling); return out; }
void IceTransport::signal(const std::string& peer, const IceSignal& signal) {
    if (signal.value.size() > 65536 || signal.type.size() > 128) throw std::length_error("net: oversized ICE signal");
    std::shared_ptr<Impl::Session> s;
    { std::lock_guard<std::mutex> lock(impl_->mutex); const auto p = impl_->peers.find(peer); if (p == impl_->peers.end()) throw std::invalid_argument("net: unknown signaling identity"); s = p->second[Impl::lane(signal.lane)]; }
    if (signal.session < s->id || s->paused) return;
    if (signal.session > s->id) { impl_->install(peer,signal.lane,signal.session); std::lock_guard<std::mutex> lock(impl_->mutex); s = impl_->peers.at(peer)[Impl::lane(signal.lane)]; }
    if (signal.kind == IceSignalKind::Description) {
        s->pc->setRemoteDescription(rtc::Description(signal.value,signal.type)); s->description = true;
        for (const auto& candidate : s->candidates) s->pc->addRemoteCandidate(candidate);
        s->candidates.clear();
    } else if (signal.kind == IceSignalKind::Candidate) {
        rtc::Candidate candidate(signal.value,signal.type);
        if (s->description) s->pc->addRemoteCandidate(candidate);
        else { if (s->candidates.size() >= 256) throw std::length_error("net: excessive pending ICE candidates"); s->candidates.push_back(candidate); }
    }
}
SendResult IceTransport::send(Outbound m) {
    const auto d = m.delivery;
    if (m.bytes.size() > impl_->config.maximum_message) return SendResult::TooLarge;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->peers.count(m.peer)) throw std::invalid_argument("net: connect the peer before sending");
    const Impl::Route route{m.peer,m.channel,d}; auto found = impl_->streams.find(route);
    if (found == impl_->streams.end()) {
        if (impl_->streams.size() >= impl_->config.channels) return SendResult::Blocked;
        found = impl_->streams.emplace(route,std::make_shared<Impl::Stream>(m.peer,m.channel,d,impl_->config.queues)).first;
    }
    std::size_t bytes = 0, count = 0;
    for (const auto& [r,s] : impl_->streams) { (void)r; bytes += s->queue.bytes(d); count += s->queue.size(d); }
    // Queue coalescing remains exact locally; use a conservative global bound
    // for new slots. Existing latest slots may replace their unsent payload.
    const auto cap = d == DeliveryClass::Reliable ? impl_->config.queues.reliable_bytes : impl_->config.queues.latest_bytes;
    const auto slots = d == DeliveryClass::Reliable ? impl_->config.queues.reliable_messages : impl_->config.queues.latest_slots;
    const auto old_bytes = found->second->queue.bytes(d), old_count = found->second->queue.size(d);
    auto trial = found->second->queue; const auto result = trial.push(std::move(m));
    if (result != SendResult::Accepted) return result;
    if (bytes-old_bytes+trial.bytes(d) > cap || count-old_count+trial.size(d) > slots) return SendResult::Blocked;
    found->second->queue = std::move(trial); return SendResult::Accepted;
}
void IceTransport::poll() {
    std::vector<std::pair<std::string,DeliveryClass>> failed;
    std::vector<std::shared_ptr<Impl::Stream>> streams;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        for (const auto& [peer,sessions] : impl_->peers) for (auto d : {DeliveryClass::Reliable,DeliveryClass::Latest}) {
            const auto& s = sessions[Impl::lane(d)]; if (s && s->failed && !s->paused) failed.emplace_back(peer,d);
        }
        for (const auto& [route,s] : impl_->streams) { (void)route; streams.push_back(s); }
    }
    for (const auto& [peer,d] : failed) {
        std::uint64_t id; { std::lock_guard<std::mutex> lock(impl_->mutex); id = impl_->peers.at(peer)[Impl::lane(d)]->id; }
        if (id == UINT64_MAX) throw std::overflow_error("net: ICE session overflow");
        impl_->install(peer,d,id+1); impl_->emit({peer,d,id+1,IceSignalKind::Restart,{},{}});
    }
    std::vector<std::tuple<std::string,DeliveryClass,std::shared_ptr<rtc::DataChannel>>> channels;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        for (const auto& [peer,sessions] : impl_->peers) for (auto d : {DeliveryClass::Reliable,DeliveryClass::Latest}) {
            const auto& session = sessions[Impl::lane(d)];
            if (session && !session->paused && !session->failed)
                if (session->negotiation) channels.emplace_back(peer,d,session->negotiation);
        }
    }
    for (const auto& [peer,d,dc] : channels) {
        for (std::size_t batch = 0; batch < 16; ++batch) {
            const auto next = dc->peek(); if (!next) break;
            const auto* binary = std::get_if<rtc::binary>(&*next);
            if (!binary || binary->empty() || binary->size() > impl_->config.maximum_message+129) throw std::invalid_argument("net: invalid ICE application frame");
            const auto size = static_cast<std::uint8_t>((*binary)[0]);
            if (size == 0 || size > 128 || binary->size() < 1u+size) throw std::invalid_argument("net: invalid ICE channel framing");
            std::string channel; channel.reserve(size);
            for (std::size_t i = 1; i <= size; ++i) channel.push_back(static_cast<char>((*binary)[i]));
            std::lock_guard<std::mutex> lock(impl_->mutex);
            const auto payload_size = binary->size()-1-size;
            const bool full = impl_->received.size() >= impl_->config.receive_messages || payload_size > impl_->config.receive_bytes-impl_->receive_bytes;
            if (full && d == DeliveryClass::Reliable) { impl_->notify(peer,channel,TransportEventKind::ReceiveBlocked); break; }
            const auto message = dc->receive(); if (!message) break;
            if (full) continue; // Latest explicitly permits loss
            const auto& data = std::get<rtc::binary>(*message);
            Bytes bytes(data.size()-1-size); std::transform(data.begin()+1+size,data.end(),bytes.begin(),[](auto b){return static_cast<std::uint8_t>(b);});
            impl_->receive_bytes += bytes.size(); impl_->received.push_back({peer,channel,std::move(bytes)});
        }
    }
    // Separate ICE/SCTP associations give boundary traffic its own congestion
    // and ordering domain. Each logical Reliable channel is ordered by SCTP.
    for (const auto& s : streams) {
        std::shared_ptr<Impl::Session> session; std::shared_ptr<rtc::DataChannel> dc;
        bool inactive;
        { std::lock_guard<std::mutex> lock(impl_->mutex); session = impl_->peers.at(s->peer)[Impl::lane(s->lane)]; dc = session->negotiation; inactive = session->paused || session->failed; }
        if (inactive || !session->pc || session->pc->state() != rtc::PeerConnection::State::Connected) continue;
        if (!dc || !dc->isOpen()) continue;
        for (std::size_t batch = 0; batch < 16; ++batch) {
            Outbound message;
            {
                std::lock_guard<std::mutex> lock(impl_->mutex); const auto* head = s->queue.front(s->lane); if (!head) break;
                std::size_t buffered = 0;
                for (const auto& [peer,sessions] : impl_->peers) { (void)peer; const auto& other = sessions[Impl::lane(s->lane)]; if (other && other->negotiation) buffered += other->negotiation->bufferedAmount(); }
                const auto limit = s->lane == DeliveryClass::Reliable ? impl_->config.queues.reliable_bytes : impl_->config.queues.latest_bytes;
                if (head->bytes.size() > limit || buffered > limit-head->bytes.size()) break;
                message = *head;
            }
            if (message.bytes.size()+1+message.channel.size() > dc->maxMessageSize()) throw std::length_error("net: message exceeds the negotiated ICE framing limit");
            try {
                // Routing is transport framing; application payload stays opaque.
                Bytes frame; frame.reserve(message.bytes.size()+1+message.channel.size());
                frame.push_back(static_cast<std::uint8_t>(message.channel.size()));
                frame.insert(frame.end(),message.channel.begin(),message.channel.end());
                frame.insert(frame.end(),message.bytes.begin(),message.bytes.end());
                dc->send(reinterpret_cast<const rtc::byte*>(frame.data()),frame.size()); // false means buffered, still accepted by SCTP
                std::lock_guard<std::mutex> lock(impl_->mutex); s->queue.pop(s->lane); s->sent = true;
            } catch (const std::exception&) { std::lock_guard<std::mutex> lock(impl_->mutex); session->failed = true; break; }
        }
    }
}
bool IceTransport::try_receive(Inbound& m) {
    std::lock_guard<std::mutex> lock(impl_->mutex); if (impl_->received.empty()) return false;
    m = std::move(impl_->received.front()); impl_->received.pop_front(); impl_->receive_bytes -= m.bytes.size(); return true;
}
std::vector<TransportEvent> IceTransport::events() { std::lock_guard<std::mutex> lock(impl_->mutex); std::vector<TransportEvent> out; out.swap(impl_->notices); return out; }
IceTelemetry IceTransport::telemetry(const std::string& peer) const {
    IceTelemetry out;
    std::vector<std::shared_ptr<rtc::DataChannel>> channels;
    std::array<std::shared_ptr<rtc::PeerConnection>,2> connections;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        for (const auto& [route,s] : impl_->streams) { (void)route; if (s->peer != peer) continue; out.queued_reliable += s->queue.bytes(DeliveryClass::Reliable); out.queued_latest += s->queue.bytes(DeliveryClass::Latest); }
        const auto p = impl_->peers.find(peer); if (p == impl_->peers.end()) return out;
        for (std::size_t i = 0; i < connections.size(); ++i) if (p->second[i]) { connections[i] = p->second[i]->pc; if (p->second[i]->negotiation) channels.push_back(p->second[i]->negotiation); }
    }
    // Vendor calls may acquire the ICE registry mutex while a vendor callback
    // is waiting for ours. Query retained handles after releasing our mutex.
    for (const auto& dc : channels) out.buffered += dc->bufferedAmount();
    out.connected = true;
    for (const auto& pc : connections) {
        if (!pc) { out.connected = false; continue; }
        out.connected &= pc->state() == rtc::PeerConnection::State::Connected;
        out.sent_bytes += pc->bytesSent(); out.received_bytes += pc->bytesReceived();
        if (const auto rtt = pc->rtt()) out.rtt_ms = std::max(out.rtt_ms,static_cast<double>(rtt->count()));
        rtc::Candidate local,remote;
        if (pc->getSelectedCandidatePair(&local,&remote)) out.relayed |= local.type() == rtc::Candidate::Type::Relayed || remote.type() == rtc::Candidate::Type::Relayed;
    }
    return out;
}
}
