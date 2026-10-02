#include "sg/net/IceSession.hpp"
#include <stdexcept>
namespace sg::net {
void validate_ice(const IceConfig &c) {
    if (c.peer.empty() || c.maximum_message == 0 || c.maximum_message > 16 * 1024 * 1024 ||
        c.receive_bytes < c.maximum_message || !c.receive_messages || !c.channels || c.channels > 1024)
        throw std::invalid_argument("net: invalid ICE transport bounds");
}
Bytes ice_frame(const std::string &channel, const Bytes &payload) {
    if (channel.empty() || channel.size() > 128)
        throw std::invalid_argument("net: invalid ICE channel framing");
    Bytes frame;
    frame.reserve(1 + channel.size() + payload.size());
    frame.push_back(static_cast<std::uint8_t>(channel.size()));
    frame.insert(frame.end(), channel.begin(), channel.end());
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}
Inbound ice_unframe(const std::string &peer, const Bytes &frame, std::size_t maximum) {
    if (frame.empty() || !frame[0] || frame[0] > 128 || frame.size() < 1u + frame[0] ||
        frame.size() - 1 - frame[0] > maximum)
        throw std::invalid_argument("net: invalid ICE application frame");
    return {peer, std::string(frame.begin() + 1, frame.begin() + 1 + frame[0]),
            Bytes(frame.begin() + 1 + frame[0], frame.end())};
}
} // namespace sg::net
