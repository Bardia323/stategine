#include "sg/net/Transport.hpp"
#include "../examples/Udp.hpp"
#include <chrono>
#include <iostream>
#include <thread>
#include <random>
#include <memory>
#include <stdexcept>
namespace {
using namespace sg::net;
int failures = 0;
void check(bool good,const char* why) { std::cout << (good ? "ok " : "FAIL ") << why << '\n'; failures += !good; }
}
int main() {
    OutboundQueue queue({8,2,4,2});
    check(queue.push({"peer","agreement",DeliveryClass::Reliable,{},Bytes{1,2,3,4}}) == SendResult::Accepted && queue.push({"peer","agreement",DeliveryClass::Reliable,{},Bytes{5,6,7,8}}) == SendResult::Accepted,"reliable messages enter a bounded explicit queue");
    check(queue.push({"peer","agreement",DeliveryClass::Reliable,{},Bytes{9}}) == SendResult::Blocked && queue.size(DeliveryClass::Reliable) == 2,"full reliable queue reports backpressure and preserves every accepted message");
    check(queue.push({"peer","boundary",DeliveryClass::Latest,"edge",Bytes{1}}) == SendResult::Accepted && queue.push({"peer","boundary",DeliveryClass::Latest,"edge",Bytes{2,3}}) == SendResult::Accepted && queue.size(DeliveryClass::Latest) == 1 && queue.front(DeliveryClass::Latest)->bytes == Bytes({2,3}),"latest replacement is scoped by explicit peer/channel/slot");
    check(queue.front(DeliveryClass::Reliable)->bytes == Bytes({1,2,3,4}),"boundary replacement cannot reorder or head-of-line block reliable control");
    queue.pop(DeliveryClass::Reliable); check(queue.front(DeliveryClass::Reliable)->bytes == Bytes({5,6,7,8}) && queue.push({"peer","agreement",DeliveryClass::Reliable,{},Bytes{9}}) == SendResult::Accepted,"reliable FIFO order and backpressure recovery are explicit");
    check(queue.push({"peer","boundary",DeliveryClass::Latest,"second",Bytes{4,5}}) == SendResult::Accepted && queue.push({"peer","boundary",DeliveryClass::Latest,"third",Bytes{6}}) == SendResult::Blocked,"latest slots and byte buffering are bounded");
    check(queue.push({"peer","agreement",DeliveryClass::Reliable,{},Bytes(9)}) == SendResult::TooLarge,"oversized messages fail visibly without entering a queue");
    // Windows/Hyper-V can reserve randomly selected ports. Retry bounded setup
    // only; no failed send/receive or correctness assertion is retried away.
    std::unique_ptr<sg::examples::Udp> first,second;
    std::mt19937 random{std::random_device{}()};
    const std::vector<Peer> peers{{"a","127.0.0.1"},{"b","127.0.0.1"}};
    for (unsigned attempt = 0; attempt < 32 && !first; ++attempt) {
        const int base = 20000+static_cast<int>(random()%25000);
        try {
            auto a = std::make_unique<sg::examples::Udp>(0,peers,base);
            auto b = std::make_unique<sg::examples::Udp>(1,peers,base);
            first = std::move(a); second = std::move(b);
        } catch (const std::runtime_error& e) {
            if (std::string(e.what()) != "UDP bind failed") throw;
        }
    }
    if (!first) { std::cerr << "FAIL no available UDP port pair after bounded setup retries\n"; return 1; }
    auto& a = *first; auto& b = *second;
    const Bytes opaque{0xff,0,17,0,254};
    check(a.send({"b","opaque",DeliveryClass::Latest,"slot",opaque}) == SendResult::Accepted,"UDP routes opaque bytes without decoding application packets");
    Inbound received; const auto end = std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while (!b.try_receive(received) && std::chrono::steady_clock::now() < end) { a.retry(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    check(received.peer == "a" && received.channel == "opaque" && received.bytes == opaque,"inbound UDP supplies explicit route and complete application payload");
    check(a.send({"b","control",DeliveryClass::Reliable,{},opaque}) == SendResult::Unsupported && a.send({"b","opaque",DeliveryClass::Latest,"slot",Bytes(65536)}) == SendResult::TooLarge,"UDP honestly reports missing reliability and datagram size limits");
    return failures ? 1 : 0;
}
