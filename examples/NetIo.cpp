#include "NetIo.hpp"
#include "Udp.hpp"
#ifdef SG_EXAMPLE_ICE
#include "sg/net/IceTransport.hpp"
#include "IceSignals.hpp"
#include <rtc/rtc.hpp>
#include <chrono>
#include <algorithm>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <tuple>
#endif
#include <stdexcept>
namespace sg::examples {
struct NetIo::Impl : std::enable_shared_from_this<Impl> {
    NetIoConfig config;
    std::unique_ptr<net::Transport> transport;
#ifdef SG_EXAMPLE_ICE
    net::Committee committee;
    const net::SigningKey& key;
    std::string peer;
    std::shared_ptr<rtc::WebSocket> socket;
    std::mutex mutex;
    std::deque<net::Bytes> incoming;
    std::map<net::Digest,net::Bytes> signaling;
    std::set<net::Digest> seen;
    std::map<std::tuple<std::string,std::string,std::string>,net::Outbound> frontier;
    net::OutboundQueue pending;
    std::deque<net::Outbound> replay;
    std::vector<net::TransportEvent> notices;
    std::chrono::steady_clock::time_point retry{};
    bool overflow = false;
    explicit Impl(NetIoConfig c,const net::Committee& members,const net::SigningKey& signing)
        : config(std::move(c)),committee(members),key(signing),peer("p"+std::to_string(config.rank)) {
        if (!config.signaling.empty()) {
            if (config.signaling.substr(0,6) != "wss://" && config.signaling.substr(0,15) != "ws://127.0.0.1:" && config.signaling.substr(0,15) != "ws://localhost:") throw std::invalid_argument("Internet signaling requires wss:// (ws:// is loopback test only)");
            if (!committee.key(peer) || *committee.key(peer) != key.public_key()) throw std::invalid_argument("signaling identity must match the pinned committee");
            auto ice = std::make_unique<net::IceTransport>(net::IceConfig{peer,config.ice_servers,config.relay_only});
            for (const auto& p : committee.members) if (p.peer != peer) ice->connect(p.peer);
            transport = std::move(ice); return;
        }
        transport = std::make_unique<Udp>(config.rank,config.peers,config.port);
    }
    void open() {
        if (socket) { socket->resetCallbacks(); socket->forceClose(); }
        rtc::WebSocketConfiguration c; c.maxMessageSize = 128*1024; if (!config.ca_file.empty()) c.caCertificatePemFile = config.ca_file;
        socket = std::make_shared<rtc::WebSocket>(c); const std::weak_ptr<Impl> weak = shared_from_this();
        socket->onMessage([weak](rtc::message_variant message){
            if (auto self = weak.lock()) { std::lock_guard<std::mutex> lock(self->mutex);
                const auto* b = std::get_if<rtc::binary>(&message); if (!b || b->size() > 128*1024) return;
                if (self->incoming.size() >= 256) { self->overflow = true; return; }
                net::Bytes bytes(b->size()); for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<std::uint8_t>((*b)[i]); self->incoming.push_back(std::move(bytes));
            }
        });
        socket->open(config.signaling); retry = std::chrono::steady_clock::now()+std::chrono::seconds(1);
    }
    void receive_signal(const net::Bytes& bytes,net::IceTransport& ice) {
        const auto accepted = verify_signal(committee,peer,bytes); if (!accepted) return;
        const auto id = net::hash(bytes); if (seen.count(id)) return;
        if (seen.size() >= 4096) throw std::length_error("signaling deduplication bound exceeded; start a fresh external session");
        ice.signal(accepted->from,accepted->signal); seen.insert(id);
    }
    void poll_ice(net::IceTransport& ice) {
        const auto now = std::chrono::steady_clock::now();
        if (!socket || (socket->isClosed() && now >= retry)) open();
        std::deque<net::Bytes> input;
        { std::lock_guard<std::mutex> lock(mutex); input.swap(incoming); if (overflow) throw std::length_error("external signaling receive backpressure"); }
        for (const auto& bytes : input) { try { receive_signal(bytes,ice); } catch (const std::invalid_argument&) {} }
        for (const auto& s : ice.signals()) {
            auto bytes = sign_signal(committee,peer,s,key);
            if (signaling.size() >= 256) throw std::length_error("external signaling frontier full"); signaling.emplace(net::hash(bytes),std::move(bytes));
        }
        if (socket->isOpen() && now >= retry) {
            for (const auto& [id,bytes] : signaling) { (void)id; if (socket->bufferedAmount() > 1024*1024) break; socket->send(reinterpret_cast<const rtc::byte*>(bytes.data()),bytes.size()); }
            retry = now+std::chrono::milliseconds(500); // external rendezvous retries only
        }
        ice.poll();
        const auto notices = ice.events();
        for (const auto& notice : notices)
            if (std::none_of(this->notices.begin(),this->notices.end(),[&](const auto& n){return n.peer == notice.peer && n.channel == notice.channel && n.kind == notice.kind;})) this->notices.push_back(notice);
        bool uncertain = false; for (const auto& notice : notices) uncertain |= notice.kind == net::TransportEventKind::DeliveryUncertain;
        if (uncertain) { replay.clear(); for (const auto& [slot,message] : frontier) { (void)slot; replay.push_back(message); } }
        while (!replay.empty()) { if (pending.push(replay.front()) != net::SendResult::Accepted) break; replay.pop_front(); }
        for (auto delivery : {net::DeliveryClass::Reliable,net::DeliveryClass::Latest})
            while (const auto* head = pending.front(delivery)) {
                const auto result = ice.send(*head); if (result == net::SendResult::Blocked) break;
                if (result != net::SendResult::Accepted) throw std::runtime_error("ICE example cannot deliver queued message"); pending.pop(delivery);
            }
    }
#else
    explicit Impl(NetIoConfig c,const net::Committee&,const net::SigningKey&) : config(std::move(c)) {
        if (!config.signaling.empty()) throw std::invalid_argument("Build SG_NET_ICE=ON for Internet signaling");
        transport = std::make_unique<Udp>(config.rank,config.peers,config.port);
    }
#endif
};
NetIo::NetIo(NetIoConfig c,const net::Committee& members,const net::SigningKey& key) : impl_(std::make_shared<Impl>(std::move(c),members,key)) {}
NetIo::~NetIo() {
#ifdef SG_EXAMPLE_ICE
    if (impl_->socket) { impl_->socket->resetCallbacks(); impl_->socket->forceClose(); }
#endif
}
net::SendResult NetIo::send(net::Outbound m) {
#ifdef SG_EXAMPLE_ICE
    if (!impl_->config.signaling.empty()) {
        if (m.bytes.size() > 4*1024*1024) return net::SendResult::TooLarge;
        const auto slot = std::make_tuple(m.peer,m.channel,m.slot);
        if (m.delivery == net::DeliveryClass::Reliable) {
            if (!impl_->frontier.count(slot) && impl_->frontier.size() >= 128) return net::SendResult::Blocked;
            std::size_t total = 0; for (const auto& [id,old] : impl_->frontier) if (id != slot) total += old.bytes.size();
            if (m.bytes.size() > 8*1024*1024-total) return net::SendResult::Blocked;
        }
        const auto result = impl_->pending.push(m); if (result == net::SendResult::Accepted && m.delivery == net::DeliveryClass::Reliable) impl_->frontier[slot] = std::move(m); return result;
    }
#endif
    return impl_->transport->send(std::move(m));
}
bool NetIo::try_receive(net::Inbound& m) { return impl_->transport->try_receive(m); }
void NetIo::poll() {
#ifdef SG_EXAMPLE_ICE
    if (auto* ice = dynamic_cast<net::IceTransport*>(impl_->transport.get())) { impl_->poll_ice(*ice); return; }
#endif
    static_cast<Udp&>(*impl_->transport).retry();
}
std::vector<net::TransportEvent> NetIo::events() {
#ifdef SG_EXAMPLE_ICE
    if (!impl_->config.signaling.empty()) { std::vector<net::TransportEvent> out; out.swap(impl_->notices); return out; }
#endif
    return impl_->transport->events();
}
}
