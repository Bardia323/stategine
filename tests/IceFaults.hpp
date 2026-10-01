// Opaque UDP fault injection around the mature test TURN server, never a world.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
class IceFaults {
public:
    explicit IceFaults(std::uint16_t server);
    ~IceFaults();
    std::uint16_t port() const;
    std::string candidate(const std::string& value);
    void poll();
    void enable();
    std::uint64_t losses() const;
    std::uint64_t duplicates() const;
    std::uint64_t forwarded_bytes() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
