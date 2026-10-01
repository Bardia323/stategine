// Example byte transport; it has no access to a Stategine world.
#pragma once
#include "sg/net/Transport.hpp"
#include "sg/net/Peer.hpp"
#include <memory>

namespace sg::examples {
class Udp final : public net::Transport {
public:
    Udp(int rank, const std::vector<net::Peer>& peers, int base);
    ~Udp() override;
    net::SendResult send(net::Outbound message) override;
    bool try_receive(net::Inbound& message) override;
    void retry();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
