// Bytes outside the world. Encoding, peers and delivery policy belong outside
// too. Observe what a state says to send; receive through a declared port and
// Engine::send. This interface has no state, graph or engine capability.
#pragma once
#include <cstdint>
#include <vector>

namespace sg::net {

using Bytes = std::vector<std::uint8_t>;

class Transport {
public:
    virtual ~Transport() = default;
    virtual void send(const Bytes& bytes) = 0;
    // Nonblocking. True means one complete message was received.
    virtual bool try_receive(Bytes& bytes) = 0;
};

} // namespace sg::net
