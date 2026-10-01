// Cryptographic evidence and delivery checks outside the world.
#pragma once
#include "sg/net/Transport.hpp"
#include <array>
#include <map>
#include <memory>
#include <string>
#include <tuple>

namespace sg::net {
using Digest = std::array<std::uint8_t,32>;
using PublicKey = std::array<std::uint8_t,32>;
using Seed = std::array<std::uint8_t,32>;
using Signature = std::array<std::uint8_t,64>;

Digest hash(const Bytes& bytes);
std::string hex(const Bytes& bytes);
Bytes unhex(const std::string& text);
std::string hex(const Digest& digest);
Digest digest_from_hex(const std::string& text);
Seed random_seed();
void wipe(Seed& seed);
bool check_signature(const PublicKey& key, const Bytes& message, const Signature& signature);

class SigningKey {
public:
    explicit SigningKey(Seed seed);
    ~SigningKey();
    SigningKey(const SigningKey&) = delete;
    SigningKey& operator=(const SigningKey&) = delete;
    const PublicKey& public_key() const;
    Signature sign(const Bytes& message) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
struct Member { std::string peer; PublicKey key; };
struct Committee {
    std::string region;
    std::uint64_t generation = 0;
    std::vector<Member> members;
    std::uint32_t quorum = 0, faults = 0;
    void validate() const;
    Digest id() const;
    const PublicKey* key(const std::string& peer) const;
};
struct Observation {
    std::string peer;
    Digest context{};
    std::uint64_t tick = 0, sequence = 0;
    std::string object, parameter;
    bool discrete = false;
    std::string conflict;
    Bytes payload;
    Signature signature{};
    Bytes statement() const;
    Digest id() const;
    void sign(const SigningKey& key);
    bool authentic(const Committee& committee) const;
};
struct Equivocation { Observation first, second; };
enum class Delivery { Accepted, Duplicate, Replay, Equivocation, Invalid };
class Integrity {
public:
    explicit Integrity(Committee committee, std::size_t capacity = 65536);
    Delivery receive(const Observation& observation, const Digest& context, std::uint64_t tick);
    void retire_before(std::uint64_t tick);
    const std::vector<Equivocation>& evidence() const;
private:
    Committee committee_;
    std::size_t capacity_;
    std::uint64_t floor_ = 0;
    std::map<std::tuple<std::string,Digest,std::uint64_t>,Observation> seen_;
    std::vector<Equivocation> evidence_;
};
}
