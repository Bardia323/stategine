// Browser sessions are byte-routing handles, with no world capability.
#pragma once
#include "sg/net/IceSession.hpp"
#include <memory>
namespace sg::web {
class WebRtcTransport final : public net::Transport {
public:
    explicit WebRtcTransport(net::IceConfig config);
    ~WebRtcTransport() override;
    net::SendResult send(net::Outbound message) override;
    bool try_receive(net::Inbound& message) override;
    void poll() override;
    std::vector<net::TransportEvent> events() override;
    void connect(const std::string& peer);
    void disconnect(const std::string& peer);
    void reconnect(const std::string& peer);
    std::vector<net::IceSignal> signals();
    void signal(const std::string& authenticated_peer,const net::IceSignal& signal);
    net::IceTelemetry telemetry(const std::string& peer) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
