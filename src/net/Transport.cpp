#include "sg/net/Transport.hpp"
#include <algorithm>
#include <stdexcept>
namespace sg::net {
OutboundQueue::OutboundQueue(QueueLimits limits) : limits_(limits) {}
SendResult OutboundQueue::push(Outbound m) {
    if (m.peer.empty() || m.channel.empty() || m.channel.size() > 128 || m.peer.size() > 4096 || (m.delivery == DeliveryClass::Latest && (m.slot.empty() || m.slot.size() > 4096))) throw std::invalid_argument("net: invalid external transport routing");
    if (m.delivery == DeliveryClass::Reliable) {
        if (m.bytes.size() > limits_.reliable_bytes) return SendResult::TooLarge;
        if (reliable_.size() >= limits_.reliable_messages || m.bytes.size() > limits_.reliable_bytes-reliable_bytes_) return SendResult::Blocked;
        reliable_bytes_ += m.bytes.size(); reliable_.push_back(std::move(m));
    } else {
        if (m.bytes.size() > limits_.latest_bytes) return SendResult::TooLarge;
        const auto found = std::find_if(latest_.begin(),latest_.end(),[&](const auto& x){return x.peer == m.peer && x.channel == m.channel && x.slot == m.slot;});
        const auto old = found == latest_.end() ? 0 : found->bytes.size();
        if ((found == latest_.end() && latest_.size() >= limits_.latest_slots) || m.bytes.size() > limits_.latest_bytes-(latest_bytes_-old)) return SendResult::Blocked;
        latest_bytes_ = latest_bytes_-old+m.bytes.size();
        if (found == latest_.end()) latest_.push_back(std::move(m)); else *found = std::move(m);
    }
    return SendResult::Accepted;
}
const Outbound* OutboundQueue::front(DeliveryClass d) const { const auto& q = d == DeliveryClass::Reliable ? reliable_ : latest_; return q.empty() ? nullptr : &q.front(); }
void OutboundQueue::pop(DeliveryClass d) { auto& q = d == DeliveryClass::Reliable ? reliable_ : latest_; auto& b = d == DeliveryClass::Reliable ? reliable_bytes_ : latest_bytes_; if (q.empty()) throw std::logic_error("net: pop empty transport queue"); b -= q.front().bytes.size(); q.pop_front(); }
std::size_t OutboundQueue::bytes(DeliveryClass d) const { return d == DeliveryClass::Reliable ? reliable_bytes_ : latest_bytes_; }
std::size_t OutboundQueue::size(DeliveryClass d) const { return d == DeliveryClass::Reliable ? reliable_.size() : latest_.size(); }
}
