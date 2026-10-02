// Vendor-neutral ICE lanes, signaling and opaque channel framing.
#pragma once
#include "sg/net/Transport.hpp"
namespace sg::net {
enum class IceSignalKind { Description, Candidate, Restart };
struct IceSignal {
    std::string peer;
    DeliveryClass lane = DeliveryClass::Reliable;
    std::uint64_t session = 0;
    IceSignalKind kind = IceSignalKind::Restart;
    std::string value, type;
};
struct IceConfig {
    std::string peer;
    std::vector<std::string> ice_servers;
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
void validate_ice(const IceConfig& config);
Bytes ice_frame(const std::string& channel,const Bytes& payload);
Inbound ice_unframe(const std::string& peer,const Bytes& frame,std::size_t maximum);
} // namespace sg::net
