#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include "IceFaults.hpp"
#include "sg/net/Transport.hpp"
#include <map>
#include <vector>
#include <stdexcept>
#include <sstream>
#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket invalid = INVALID_SOCKET;
void close_socket(Socket s) { closesocket(s); }
#else
using Socket = int;
constexpr Socket invalid = -1;
void close_socket(Socket s) { close(s); }
#endif
struct IceFaults::Impl {
    struct Route { Socket upstream; sockaddr_in client; };
    struct Frame { Socket socket; sockaddr_in to; sg::net::Bytes bytes; std::uint64_t due; };
    struct Allocation { Socket front; sockaddr_in target{}; std::uint16_t port; std::map<std::uint16_t,Route> clients; };
    Socket front;
    sockaddr_in server{};
    std::uint16_t bound = 0;
    std::map<std::uint16_t,Route> routes;
    std::vector<Frame> queue;
    std::map<std::string,Allocation> allocations;
    std::uint64_t turn = 0, lost = 0, duplicated = 0, forwarded = 0;
    std::uint32_t random = 17;
    bool enabled = false;
    explicit Impl(std::uint16_t port) {
#ifdef _WIN32
        WSADATA data; if (WSAStartup(MAKEWORD(2,2),&data)) throw std::runtime_error("fault proxy WSA failed");
#endif
        front = socket(); sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::bind(front,reinterpret_cast<sockaddr*>(&address),sizeof(address))) throw std::runtime_error("fault proxy bind failed");
#ifdef _WIN32
        int size = sizeof(address);
#else
        socklen_t size = sizeof(address);
#endif
        getsockname(front,reinterpret_cast<sockaddr*>(&address),&size); bound = ntohs(address.sin_port);
        server.sin_family = AF_INET; server.sin_addr.s_addr = htonl(INADDR_LOOPBACK); server.sin_port = htons(port);
    }
    ~Impl() {
        close_socket(front); for (const auto& [id,route] : routes) { (void)id; close_socket(route.upstream); }
        for (const auto& [id,a] : allocations) { (void)id; close_socket(a.front); for (const auto& [port,route] : a.clients) { (void)port; close_socket(route.upstream); } }
#ifdef _WIN32
        WSACleanup();
#endif
    }
    static Socket socket() {
        const auto s = ::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP); if (s == invalid) throw std::runtime_error("fault proxy socket failed");
        // Keep burst loss controlled by the fixture, rather than Windows'
        // small default UDP receive buffer silently dropping an SCTP window.
        const int buffer = 4*1024*1024;
        if (setsockopt(s,SOL_SOCKET,SO_RCVBUF,reinterpret_cast<const char*>(&buffer),sizeof(buffer)) || setsockopt(s,SOL_SOCKET,SO_SNDBUF,reinterpret_cast<const char*>(&buffer),sizeof(buffer))) throw std::runtime_error("fault proxy buffer configuration failed");
#ifdef _WIN32
        u_long mode = 1; ioctlsocket(s,FIONBIO,&mode);
#else
        fcntl(s,F_SETFL,O_NONBLOCK);
#endif
        return s;
    }
    std::uint32_t next() { random = random*1664525+1013904223; return random; }
    std::string candidate(const std::string& value) {
        std::istringstream input(value); std::string foundation, protocol, address, typ, type, tail; unsigned component, priority, port;
        if (!(input >> foundation >> component >> protocol >> priority >> address >> port >> typ >> type) || typ != "typ" || type != "relay" || address != "127.0.0.1" || port > 65535) throw std::runtime_error("fault fixture expects the local TURN allocation");
        std::getline(input,tail);
        const auto id = address+":"+std::to_string(port); auto found = allocations.find(id);
        if (found == allocations.end()) {
            if (allocations.size() >= 32) throw std::runtime_error("fault fixture allocation bound");
            Allocation a{socket(),{},0,{}}; a.target.sin_family = AF_INET; a.target.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.target.sin_port = htons(port);
            sockaddr_in local{}; local.sin_family = AF_INET; local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            if (::bind(a.front,reinterpret_cast<sockaddr*>(&local),sizeof(local))) throw std::runtime_error("allocation proxy bind failed");
#ifdef _WIN32
            int size = sizeof(local);
#else
            socklen_t size = sizeof(local);
#endif
            getsockname(a.front,reinterpret_cast<sockaddr*>(&local),&size); a.port = ntohs(local.sin_port);
            found = allocations.emplace(id,a).first;
        }
        std::ostringstream output; output << foundation << ' ' << component << ' ' << protocol << ' ' << priority << " 127.0.0.1 " << found->second.port << " typ relay" << tail; return output.str();
    }
    void put(Socket socket,sockaddr_in to,sg::net::Bytes bytes) {
        if (queue.size() > 2048) throw std::runtime_error("fault proxy test buffer exceeded");
        // A bounded outage followed by eventual delivery exercises actual
        // SCTP retransmission. Delay/reordering/duplication continue throughout.
        if (enabled && lost < 16 && next()%13 == 0) { ++lost; return; }
        const auto due = turn+(enabled ? next()%7 : 0);
        if (enabled && next()%17 == 0) { ++duplicated; queue.push_back({socket,to,bytes,due+2}); }
        queue.push_back({socket,to,std::move(bytes),due});
    }
    void poll() {
        ++turn;
        for (;;) {
            char buffer[65536]; sockaddr_in from{};
#ifdef _WIN32
            int size = sizeof(from);
#else
            socklen_t size = sizeof(from);
#endif
            const auto n = recvfrom(front,buffer,sizeof(buffer),0,reinterpret_cast<sockaddr*>(&from),&size); if (n <= 0) break;
            auto found = routes.find(from.sin_port);
            if (found == routes.end()) {
                if (routes.size() > 64) throw std::runtime_error("fault proxy route bound exceeded");
                found = routes.emplace(from.sin_port,Route{socket(),from}).first;
            }
            put(found->second.upstream,server,sg::net::Bytes(buffer,buffer+n));
        }
        for (const auto& [id,route] : routes) {
            (void)id;
            for (;;) {
                char buffer[65536]; const auto n = recvfrom(route.upstream,buffer,sizeof(buffer),0,nullptr,nullptr); if (n <= 0) break;
                put(front,route.client,sg::net::Bytes(buffer,buffer+n));
            }
        }
        // Advertise opaque forwarding endpoints for the actual TURN
        // allocations too. Otherwise ICE can discover a direct LAN shortcut
        // and bypass this fixture's loss injection after rendezvous.
        for (auto& [id,a] : allocations) {
            (void)id;
            for (;;) {
                char buffer[65536]; sockaddr_in from{};
#ifdef _WIN32
                int size = sizeof(from);
#else
                socklen_t size = sizeof(from);
#endif
                const auto n = recvfrom(a.front,buffer,sizeof(buffer),0,reinterpret_cast<sockaddr*>(&from),&size); if (n <= 0) break;
                auto route = a.clients.find(from.sin_port);
                if (route == a.clients.end()) { if (a.clients.size() >= 16) throw std::runtime_error("allocation proxy route bound"); route = a.clients.emplace(from.sin_port,Route{socket(),from}).first; }
                put(route->second.upstream,a.target,sg::net::Bytes(buffer,buffer+n));
            }
            for (const auto& [port,route] : a.clients) { (void)port;
                for (;;) { char buffer[65536]; const auto n = recvfrom(route.upstream,buffer,sizeof(buffer),0,nullptr,nullptr); if (n <= 0) break; put(a.front,route.client,sg::net::Bytes(buffer,buffer+n)); }
            }
        }
        // Deliver by due time rather than receipt order; SCTP, ICE and DTLS
        // supply the real protocol behavior through this opaque faulty path.
        for (auto it = queue.begin(); it != queue.end();) {
            if (it->due > turn) { ++it; continue; }
            const auto sent = sendto(it->socket,reinterpret_cast<const char*>(it->bytes.data()),static_cast<int>(it->bytes.size()),0,reinterpret_cast<const sockaddr*>(&it->to),sizeof(it->to));
            if (sent > 0) forwarded += static_cast<std::uint64_t>(sent);
            it = queue.erase(it);
        }
    }
};
IceFaults::IceFaults(std::uint16_t port) : impl_(std::make_unique<Impl>(port)) {}
IceFaults::~IceFaults() = default;
std::uint16_t IceFaults::port() const { return impl_->bound; }
std::string IceFaults::candidate(const std::string& value) { return impl_->candidate(value); }
void IceFaults::poll() { impl_->poll(); }
void IceFaults::enable() { impl_->enabled = true; }
std::uint64_t IceFaults::losses() const { return impl_->lost; }
std::uint64_t IceFaults::duplicates() const { return impl_->duplicated; }
std::uint64_t IceFaults::forwarded_bytes() const { return impl_->forwarded; }
