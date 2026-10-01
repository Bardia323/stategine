// Optional ICE/DTLS/SCTP adapter. The public interface contains no vendor types.
#pragma once
#include "sg/net/Transport.hpp"
#include <memory>
namespace sg::net {
enum class IceSignalKind { Description, Candidate, Restart };
struct IceSignal {
    std::string peer; // outbound recipient; incoming sender is supplied separately
    DeliveryClass lane = DeliveryClass::Reliable;
    std::uint64_t session = 0;
    IceSignalKind kind = IceSignalKind::Restart;
    std::string value, type; // SDP/type or candidate/mid, opaque external signaling
};
struct IceConfig {
    std::string peer;
    std::vector<std::string> ice_servers; // STUN/TURN URIs, including external credentials
    bool relay_only = false;
    QueueLimits queues;
    std::size_t maximum_message = 4*1024*1024, receive_bytes = 16*1024*1024;
    std::size_t receive_messages = 512, channels = 64;
};
struct IceTelemetry {
    std::size_t queued_reliable = 0, queued_latest = 0, buffered = 0;
    std::uint64_t sent_bytes = 0, received_bytes = 0;
    double rtt_ms = 0;
    bool connected = false, relayed = false;
};
class IceTransport final : public Transport {
public:
    explicit IceTransport(IceConfig config);
    ~IceTransport() override;
    SendResult send(Outbound message) override;
    bool try_receive(Inbound& message) override;
    void poll() override;
    std::vector<TransportEvent> events() override;
    void connect(const std::string& peer);
    void disconnect(const std::string& peer);
    void reconnect(const std::string& peer);
    std::vector<IceSignal> signals();
    void signal(const std::string& authenticated_peer, const IceSignal& signal);
    IceTelemetry telemetry(const std::string& peer) const;
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
}
