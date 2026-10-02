#include "sg/net/Integrity.hpp"
#include "Canonical.hpp"
#include <monocypher.h>
#include <monocypher-ed25519.h>
#include <algorithm>
#include <fstream>
#include <set>
#include <stdexcept>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
EM_JS(int, sg_random_bytes, (unsigned char* destination, unsigned size), {
    if (!globalThis.crypto || !globalThis.crypto.getRandomValues) return 0;
    globalThis.crypto.getRandomValues(HEAPU8.subarray(destination,destination+size));
    return 1;
});
#endif
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#endif

namespace sg::net {
namespace {
bool nontrivial_key(const PublicKey& key) {
    // Use the upstream cofactored verification equation to reject [8]A = 0.
    // R=identity, S=0, H=1 tests exactly the small-order public-key case;
    // no curve arithmetic or cryptographic primitive is implemented here.
    Signature identity{}; identity[0] = 1;
    std::array<std::uint8_t,32> one{}; one[0] = 1;
    return crypto_eddsa_check_equation(identity.data(),key.data(),one.data()) != 0;
}
}
Digest hash(const Bytes& bytes) { Digest out; crypto_blake2b(out.data(),out.size(),bytes.data(),bytes.size()); return out; }
std::string hex(const Bytes& bytes) { const char* digits = "0123456789abcdef"; std::string out; out.reserve(bytes.size()*2); for (auto c : bytes) { out += digits[c>>4]; out += digits[c&15]; } return out; }
Bytes unhex(const std::string& text) {
    if (text.size()%2 || text.size() > 33554432) throw std::invalid_argument("net: invalid hex length");
    const auto digit = [](char c) -> unsigned { if (c >= '0' && c <= '9') return c-'0'; if (c >= 'a' && c <= 'f') return c-'a'+10; throw std::invalid_argument("net: noncanonical hex"); };
    Bytes out; out.reserve(text.size()/2); for (std::size_t i = 0; i < text.size(); i += 2) out.push_back(static_cast<std::uint8_t>((digit(text[i])<<4)|digit(text[i+1]))); return out;
}
std::string hex(const Digest& digest) { return hex(Bytes(digest.begin(),digest.end())); }
Digest digest_from_hex(const std::string& text) { const auto bytes = unhex(text); if (bytes.size() != 32) throw std::invalid_argument("net: wrong digest size"); Digest out; std::copy(bytes.begin(),bytes.end(),out.begin()); return out; }
Seed random_seed() {
    Seed seed;
#ifdef __EMSCRIPTEN__
    if (!sg_random_bytes(seed.data(),seed.size())) throw std::runtime_error("net: browser cryptographic randomness unavailable");
#elif defined(_WIN32)
    if (BCryptGenRandom(nullptr,seed.data(),static_cast<ULONG>(seed.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) throw std::runtime_error("net: operating-system randomness unavailable");
#else
    std::ifstream random("/dev/urandom",std::ios::binary); random.read(reinterpret_cast<char*>(seed.data()),seed.size()); if (!random) throw std::runtime_error("net: operating-system randomness unavailable");
#endif
    return seed;
}
void wipe(Seed& seed) { crypto_wipe(seed.data(),seed.size()); }
bool check_signature(const PublicKey& key, const Bytes& message, const Signature& signature) { return nontrivial_key(key) && crypto_ed25519_check(signature.data(),key.data(),message.data(),message.size()) == 0; }
struct SigningKey::Impl { std::array<std::uint8_t,64> secret{}; PublicKey key{}; ~Impl() { crypto_wipe(secret.data(),secret.size()); } };
SigningKey::SigningKey(Seed seed) : impl_(std::make_unique<Impl>()) { crypto_ed25519_key_pair(impl_->secret.data(),impl_->key.data(),seed.data()); }
SigningKey::~SigningKey() = default;
const PublicKey& SigningKey::public_key() const { return impl_->key; }
Signature SigningKey::sign(const Bytes& message) const { Signature signature; crypto_ed25519_sign(signature.data(),impl_->secret.data(),message.data(),message.size()); return signature; }
void Committee::validate() const {
    if (region.empty() || members.empty() || members.size() > 1024 || quorum == 0 || quorum > members.size() || faults >= members.size() || quorum > members.size()-faults || 2*quorum <= members.size()+faults) throw std::invalid_argument("net: quorum must intersect by more than the declared fault budget and remain available without faulty members");
    std::set<std::string> peers; std::set<PublicKey> keys;
    for (const auto& m : members) if (m.peer.empty() || !peers.insert(m.peer).second || !keys.insert(m.key).second || m.key == PublicKey{} || !nontrivial_key(m.key)) throw std::invalid_argument("net: committee identities and public keys must be distinct and nontrivial");
}
Digest Committee::id() const {
    validate(); detail::Writer w("sg.net.committee.v1"); w.text(region); w.integer(generation); w.integer(quorum); w.integer(faults);
    auto ordered = members; std::sort(ordered.begin(),ordered.end(),[](const Member& a,const Member& b){ return a.peer < b.peer; });
    w.integer(ordered.size()); for (const auto& m : ordered) { w.text(m.peer); w.digest(m.key); } return hash(w.data());
}
const PublicKey* Committee::key(const std::string& peer) const { for (const auto& m : members) if (m.peer == peer) return &m.key; return nullptr; }
Bytes Observation::statement() const {
    if (peer.empty() || object.empty() || parameter.empty() || (discrete && conflict.empty()) || (!discrete && !conflict.empty())) throw std::invalid_argument("net: incomplete observation declaration");
    detail::Writer w("sg.net.observation.v1"); w.text(peer); w.digest(context); w.integer(tick); w.integer(sequence); w.text(object); w.text(parameter); w.integer(discrete); w.text(conflict); w.bytes(payload); return w.data();
}
Digest Observation::id() const { return hash(statement()); }
void Observation::sign(const SigningKey& key) { signature = key.sign(statement()); }
bool Observation::authentic(const Committee& committee) const { const auto* key = committee.key(peer); if (!key) return false; try { return check_signature(*key,statement(),signature); } catch (const std::exception&) { return false; } }
Integrity::Integrity(Committee committee, std::size_t capacity) : committee_(std::move(committee)),capacity_(capacity) { committee_.validate(); if (capacity == 0) throw std::invalid_argument("net: empty integrity capacity"); }
Delivery Integrity::receive(const Observation& o, const Digest& context, std::uint64_t tick) {
    const auto key = std::make_tuple(o.peer,o.context,o.sequence); const auto found = seen_.find(key);
    // Retransmission of the identical authenticated statement/signature needs
    // no second public-key operation. A changed body or signature still does.
    if (found != seen_.end() && o.signature == found->second.signature && o.id() == found->second.id()) {
        if (o.tick < floor_) return Delivery::Replay;
        return o.context == context && o.tick == tick ? Delivery::Duplicate : Delivery::Invalid;
    }
    const auto conflict = [&]() {
        const auto exists = std::any_of(evidence_.begin(),evidence_.end(),[&](const Equivocation& e){ return e.first.id() == found->second.id() && e.second.id() == o.id(); });
        if (!exists && evidence_.size() < capacity_) evidence_.push_back({found->second,o});
        return Delivery::Equivocation;
    };
    if (o.tick < floor_) {
        if (found != seen_.end() && o.authentic(committee_) && found->second.id() != o.id()) return conflict();
        return Delivery::Replay;
    }
    if (o.context != context || o.tick != tick || !o.authentic(committee_)) return Delivery::Invalid;
    if (found != seen_.end()) {
        if (found->second.id() == o.id()) return Delivery::Duplicate;
        return conflict();
    }
    if (seen_.size() >= capacity_) {
        const auto retired = std::find_if(seen_.begin(),seen_.end(),[&](const auto& item){ return item.second.tick < floor_; });
        if (retired == seen_.end()) return Delivery::Invalid;
        seen_.erase(retired);
    }
    seen_.emplace(key,o); return Delivery::Accepted;
}
void Integrity::retire_before(std::uint64_t tick) {
    floor_ = std::max(floor_,tick); const auto retain = floor_ > 64 ? floor_-64 : 0;
    for (auto it = seen_.begin(); it != seen_.end();) { if (it->second.tick < retain) it = seen_.erase(it); else ++it; }
}
const std::vector<Equivocation>& Integrity::evidence() const { return evidence_; }
}
