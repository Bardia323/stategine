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
    void send(const net::Bytes& bytes) override;
    void send_to(const std::string& peer, const net::Bytes& bytes, const std::string& slot);
    bool try_receive(net::Bytes& bytes) override;
    void retry();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
