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
#include <chrono>
#include <deque>
#include <map>
#include <tuple>
#include <algorithm>
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
struct Udp::Impl {
public:
    Impl(int rank, const std::vector<sg::net::Peer>& peers, int base) {
#ifdef _WIN32
        WSADATA data;
        if (WSAStartup(MAKEWORD(2,2), &data)) throw std::runtime_error("UDP initialization failed");
#endif
        try {
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
            const auto route = *reinterpret_cast<sockaddr_in*>(found->ai_addr); freeaddrinfo(found);
            routes_[peers[i].id] = route;
        }
        } catch (...) {
            // A failed constructor has no Impl destructor. Release the socket
            // and startup reference before a caller retries external setup.
            if (socket_ != invalid) close_socket(socket_);
            socket_ = invalid;
#ifdef _WIN32
            WSACleanup();
#endif
            throw;
        }
    }
    ~Impl() {
        if (socket_ != invalid) close_socket(socket_);
#ifdef _WIN32
        WSACleanup();
#endif
    }
    net::SendResult send(net::Outbound m) {
        if (m.delivery == net::DeliveryClass::Reliable) return net::SendResult::Unsupported;
        if (!routes_.count(m.peer) || m.channel.empty() || m.channel.size() > 128 || m.slot.empty()) throw std::invalid_argument("UDP routing is invalid");
        if (m.bytes.size()+m.channel.size()+1 > 64000) return net::SendResult::TooLarge;
        const auto key = std::make_tuple(m.peer,m.channel,m.slot);
        const auto found = recent_.find(key);
        const auto old = found == recent_.end() ? 0 : found->second.bytes.size();
        if ((found == recent_.end() && recent_.size() >= 64) || bytes_-old+m.bytes.size() > 4*1024*1024) return net::SendResult::Blocked;
        bytes_ = bytes_-old+m.bytes.size(); recent_[key] = m; transmit(m); return net::SendResult::Accepted;
    }
    bool try_receive(net::Inbound& m) {
        for (;;) {
            char buffer[65536]; sockaddr_in from{};
#ifdef _WIN32
            int length = sizeof(from);
#else
            socklen_t length = sizeof(from);
#endif
            const auto n = recvfrom(socket_, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr*>(&from), &length);
            if (n <= 0) return false;
            const auto route = std::find_if(routes_.begin(),routes_.end(),[&](const auto& p){return p.second.sin_addr.s_addr == from.sin_addr.s_addr && p.second.sin_port == from.sin_port;});
            const auto size = static_cast<unsigned char>(buffer[0]);
            if (route == routes_.end() || size == 0 || size > 128 || n < size+1) continue;
            m = {route->first,std::string(buffer+1,buffer+1+size),net::Bytes(buffer+1+size,buffer+n)}; return true;
        }
    }
    void retry() {
        // Wall time paces external retransmission only, never world behavior.
        const auto now = std::chrono::steady_clock::now();
        if (now - last_ < std::chrono::milliseconds(2)) return;
        last_ = now;
        for (const auto& [key, m] : recent_) { (void)key; transmit(m); }
    }
private:
    void transmit(const net::Outbound& m) {
        net::Bytes bytes{static_cast<std::uint8_t>(m.channel.size())};
        bytes.insert(bytes.end(),m.channel.begin(),m.channel.end()); bytes.insert(bytes.end(),m.bytes.begin(),m.bytes.end());
        const auto& route = routes_.at(m.peer);
        const auto n = sendto(socket_, reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()), 0, reinterpret_cast<const sockaddr*>(&route), sizeof(route));
        if (n < 0 || static_cast<std::size_t>(n) != bytes.size()) throw std::runtime_error("UDP boundary send failed");
    }
    Socket socket_ = invalid;
    std::map<std::string, sockaddr_in> routes_;
    std::map<std::tuple<std::string,std::string,std::string>,net::Outbound> recent_;
    std::size_t bytes_ = 0;
    std::chrono::steady_clock::time_point last_{};
};
Udp::Udp(int rank, const std::vector<net::Peer>& peers, int base) : impl_(std::make_unique<Impl>(rank, peers, base)) {}
Udp::~Udp() = default;
net::SendResult Udp::send(net::Outbound message) { return impl_->send(std::move(message)); }
bool Udp::try_receive(net::Inbound& message) { return impl_->try_receive(message); }
void Udp::retry() { impl_->retry(); }
} // namespace sg::examples
