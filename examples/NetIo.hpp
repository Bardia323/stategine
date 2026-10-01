// Example-only external IO: UDP reference or optional authenticated ICE signaling.
#pragma once
#include "sg/net/Integrity.hpp"
#include "sg/net/Peer.hpp"
#include <memory>
namespace sg::examples {
struct NetIoConfig {
    int rank = 0, port = 49270;
    std::vector<net::Peer> peers;
    std::string signaling, ca_file;
    std::vector<std::string> ice_servers;
    bool relay_only = false;
};
class NetIo final : public net::Transport {
public:
    NetIo(NetIoConfig config, const net::Committee& committee, const net::SigningKey& key);
    ~NetIo() override;
    net::SendResult send(net::Outbound message) override;
    bool try_receive(net::Inbound& message) override;
    void poll() override;
    std::vector<net::TransportEvent> events() override;
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
}
