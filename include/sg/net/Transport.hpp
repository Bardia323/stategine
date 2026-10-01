// Routed opaque messages outside the world. No state/graph/engine capability.
#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <deque>

namespace sg::net {

using Bytes = std::vector<std::uint8_t>;
enum class DeliveryClass { Latest, Reliable };
enum class SendResult { Accepted, Blocked, Unsupported, TooLarge };
struct Outbound {
    std::string peer, channel;
    DeliveryClass delivery = DeliveryClass::Reliable;
    std::string slot; // Latest supersession key; ignored for Reliable
    Bytes bytes;
};
struct Inbound { std::string peer, channel; Bytes bytes; };
struct QueueLimits {
    std::size_t reliable_bytes = 8*1024*1024, reliable_messages = 256;
    std::size_t latest_bytes = 1024*1024, latest_slots = 64;
};
enum class TransportEventKind { Disconnected, Reconnected, DeliveryUncertain, ReceiveBlocked };
struct TransportEvent { std::string peer, channel; TransportEventKind kind; };
// Bounded scheduling only, not a reliable wire protocol. A backend leaves a
// Reliable head here until its mature data plane accepts it exactly once.
class OutboundQueue {
public:
    explicit OutboundQueue(QueueLimits limits = {});
    SendResult push(Outbound message);
    const Outbound* front(DeliveryClass delivery) const;
    void pop(DeliveryClass delivery);
    std::size_t bytes(DeliveryClass delivery) const;
    std::size_t size(DeliveryClass delivery) const;
private:
    QueueLimits limits_;
    std::deque<Outbound> reliable_, latest_;
    std::size_t reliable_bytes_ = 0, latest_bytes_ = 0;
};

class Transport {
public:
    virtual ~Transport() = default;
    virtual SendResult send(Outbound message) = 0;
    // Nonblocking. True means one complete message was received.
    virtual bool try_receive(Inbound& message) = 0;
    virtual void poll() {}
    virtual std::vector<TransportEvent> events() { return {}; }
};

} // namespace sg::net
