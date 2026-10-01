// Socket IO is outside the world: observe said bytes and queue a declared port.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include "Udp.hpp"
#include "sg/net/Exchange.hpp"
#include <chrono>
#include <deque>
#include <map>
#include <stdexcept>
namespace sg::examples {
#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket invalid = INVALID_SOCKET;
void close_socket(Socket s) { closesocket(s); }
#else
using Socket = int;
constexpr Socket invalid = -1;
void close_socket(Socket s) { close(s); }
#endif
struct Udp::Impl final : public sg::net::Transport {
public:
    Impl(int rank, const std::vector<sg::net::Peer>& peers, int base) {
#ifdef _WIN32
        WSADATA data;
        if (WSAStartup(MAKEWORD(2,2), &data)) throw std::runtime_error("UDP initialization failed");
#endif
        socket_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socket_ == invalid) throw std::runtime_error("UDP socket failed");
        sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_ANY); address.sin_port = htons(static_cast<unsigned short>(base+rank));
        if (::bind(socket_, reinterpret_cast<sockaddr*>(&address), sizeof(address))) throw std::runtime_error("UDP bind failed");
#ifdef _WIN32
        u_long mode = 1; if (ioctlsocket(socket_, FIONBIO, &mode)) throw std::runtime_error("UDP nonblocking mode failed");
#else
        if (fcntl(socket_, F_SETFL, O_NONBLOCK) == -1) throw std::runtime_error("UDP nonblocking mode failed");
#endif
        for (std::size_t i = 0; i < peers.size(); ++i) {
            addrinfo hints{}; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_DGRAM;
            addrinfo* found = nullptr;
            if (getaddrinfo(peers[i].endpoint.c_str(), std::to_string(base+i).c_str(), &hints, &found)) throw std::runtime_error("UDP peer lookup failed");
            routes_[peers[i].id] = *reinterpret_cast<sockaddr_in*>(found->ai_addr); freeaddrinfo(found);
        }
    }
    ~Impl() override {
        if (socket_ != invalid) close_socket(socket_);
#ifdef _WIN32
        WSACleanup();
#endif
    }
    void send(const sg::net::Bytes& bytes) override {
        const auto packet = sg::net::Exchange::decode(bytes);
        auto& recent = recent_[packet.to]; recent.push_back(bytes); if (recent.size() > 2) recent.pop_front();
        transmit(packet.to, bytes);
    }
    bool try_receive(sg::net::Bytes& bytes) override {
        char buffer[65536]; const auto n = recvfrom(socket_, buffer, sizeof(buffer), 0, nullptr, nullptr);
        if (n <= 0) return false;
        bytes.assign(buffer, buffer+n); return true;
    }
    void retry() {
        // Wall time paces external retransmission only, never world behavior.
        const auto now = std::chrono::steady_clock::now();
        if (now - last_ < std::chrono::milliseconds(2)) return;
        last_ = now;
        for (const auto& [to, packets] : recent_) for (const auto& bytes : packets) transmit(to, bytes);
        for (const auto& [to, packets] : signed_) for (const auto& [slot, bytes] : packets) { (void)slot; transmit(to,bytes); }
    }
    void send_to(const std::string& to, const sg::net::Bytes& bytes, const std::string& slot) {
        if (slot.empty() || slot.size() > 128) throw std::invalid_argument("UDP retry slot is invalid");
        auto& packets = signed_[to]; if (!packets.count(slot) && packets.size() >= 8) throw std::length_error("UDP retry slots must remain bounded");
        packets[slot] = bytes; transmit(to,bytes);
    }
private:
    void transmit(const std::string& to, const sg::net::Bytes& bytes) {
        if (bytes.size() > 64000) throw std::length_error("UDP example requires a smaller boundary datagram");
        const auto& route = routes_.at(to);
        const auto n = sendto(socket_, reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()), 0, reinterpret_cast<const sockaddr*>(&route), sizeof(route));
        if (n < 0 || static_cast<std::size_t>(n) != bytes.size()) throw std::runtime_error("UDP boundary send failed");
    }
    Socket socket_ = invalid;
    std::map<std::string, sockaddr_in> routes_;
    std::map<std::string, std::deque<sg::net::Bytes>> recent_;
    std::map<std::string, std::map<std::string,sg::net::Bytes>> signed_;
    std::chrono::steady_clock::time_point last_{};
};
Udp::Udp(int rank, const std::vector<net::Peer>& peers, int base) : impl_(std::make_unique<Impl>(rank, peers, base)) {}
Udp::~Udp() = default;
void Udp::send(const net::Bytes& bytes) { impl_->send(bytes); }
void Udp::send_to(const std::string& peer, const net::Bytes& bytes, const std::string& slot) { impl_->send_to(peer,bytes,slot); }
bool Udp::try_receive(net::Bytes& bytes) { return impl_->try_receive(bytes); }
void Udp::retry() { impl_->retry(); }
} // namespace sg::examples
