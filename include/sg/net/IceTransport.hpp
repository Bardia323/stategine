// Optional ICE/DTLS/SCTP adapter. The public interface contains no vendor types.
#pragma once
#include "sg/net/IceSession.hpp"
#include <memory>
namespace sg::net {
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
