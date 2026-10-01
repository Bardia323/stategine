#include "sg/net/Exchange.hpp"
#include "DistributedData.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>

namespace sg::net::detail {
double number(const Params& p, Key key, double fallback) {
    if (!p.has(key)) return fallback;
    if (!std::holds_alternative<double>(p.get(key)) && !std::holds_alternative<std::int64_t>(p.get(key))) throw std::invalid_argument("net: expected numeric " + key.str());
    const double value = p.num(key);
    if (!std::isfinite(value)) throw std::invalid_argument("net: nonfinite " + key.str());
    return value;
}
std::int64_t integer(const Params& p, Key key, std::int64_t fallback) {
    if (!p.has(key)) return fallback;
    if (const auto* n = std::get_if<std::int64_t>(&p.get(key))) return *n;
    if (const auto* n = std::get_if<double>(&p.get(key)); n && std::isfinite(*n) && *n == std::floor(*n) && std::fabs(*n) <= 9007199254740992.0)
        return static_cast<std::int64_t>(*n);
    throw std::invalid_argument("net: expected exact integer " + key.str());
}
bool flag(const Params& p, Key key, bool fallback) {
    if (!p.has(key)) return fallback;
    if (!std::holds_alternative<bool>(p.get(key))) throw std::invalid_argument("net: expected boolean " + key.str());
    return p.get_or<bool>(key, fallback);
}
Key field(const char* base, std::uint32_t dimension, std::uint32_t coordinate) { return Cellular::coordinate_key(Key{base}, dimension, coordinate); }
bool same_step(const Params& p, const char* prefix, const Step& s) {
    return integer(p, Key{std::string(prefix) + "_epoch"}, -1) == s.epoch && integer(p, Key{std::string(prefix) + "_generation"}, -1) == s.generation;
}
std::vector<double> projection(const Params& p, const char* prefix, std::uint32_t dimension) {
    std::vector<double> values;
    for (std::uint32_t c = 0; c < dimension; ++c) {
        const auto key = field(prefix, dimension, c);
        if (!p.has(key)) throw std::invalid_argument("net: missing boundary projection");
        values.push_back(number(p, key, 0));
    }
    return values;
}
void projection(Params& p, const char* prefix, const std::vector<double>& values) {
    for (std::size_t c = 0; c < values.size(); ++c) p.set(field(prefix, static_cast<std::uint32_t>(values.size()), static_cast<std::uint32_t>(c)), values[c]);
}
}
namespace sg::net {
namespace {
constexpr std::size_t limit = 16 * 1024 * 1024;
static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559, "boundary wire values require IEEE binary64");
class Writer {
public:
    Bytes bytes;
    void word(std::uint64_t n, int size) { for (int i = size - 1; i >= 0; --i) bytes.push_back(static_cast<std::uint8_t>(n >> (8*i))); }
    void text(const std::string& s) {
        if (s.empty() || s.size() > 4096) throw std::invalid_argument("net: invalid packet name");
        word(s.size(), 4); bytes.insert(bytes.end(), s.begin(), s.end());
    }
};
class Reader {
public:
    const Bytes& bytes;
    std::size_t pos = 0;
    std::uint64_t word(int size) {
        if (bytes.size() - pos < static_cast<std::size_t>(size)) throw std::invalid_argument("net: truncated boundary packet");
        std::uint64_t out = 0;
        for (int i = 0; i < size; ++i) out = (out << 8) | bytes[pos++];
        return out;
    }
    std::string text() {
        const auto n = word(4);
        if (n == 0 || n > 4096 || n > bytes.size() - pos) throw std::invalid_argument("net: invalid packet name");
        std::string out(bytes.begin() + pos, bytes.begin() + pos + n); pos += n; return out;
    }
    std::int64_t tick() {
        const auto n = word(8);
        if (n > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) throw std::invalid_argument("net: invalid packet step");
        return static_cast<std::int64_t>(n);
    }
};
void check(const Packet& p) {
    if (p.step.epoch < 0 || p.step.generation < 0 || p.step.tick < 0 || p.boundaries.size() > 65536) throw std::invalid_argument("net: invalid boundary step or count");
    std::set<std::string> ids;
    for (const auto& b : p.boundaries) {
        if (!ids.insert(b.overlap).second || b.basis < 0 || b.basis > p.step.tick || (!b.values.empty() && b.basis != p.step.tick) || b.values.size() > 1000000)
            throw std::invalid_argument("net: invalid boundary basis or duplicate overlap");
        for (auto v : b.values) if (!std::isfinite(v)) throw std::invalid_argument("net: nonfinite boundary value");
    }
}
}
Bytes Exchange::encode(const Packet& p) {
    check(p);
    Writer w; w.word(0x53474e01, 4); w.text(p.from); w.text(p.to);
    w.word(p.step.epoch, 8); w.word(p.step.generation, 8); w.word(p.step.tick, 8); w.word(p.boundaries.size(), 4);
    for (const auto& b : p.boundaries) {
        w.text(b.overlap); w.word(b.basis, 8); w.word(b.values.size(), 4);
        for (const auto v : b.values) { std::uint64_t bits; std::memcpy(&bits, &v, 8); w.word(bits, 8); }
        if (w.bytes.size() > limit) throw std::length_error("net: boundary packet too large");
    }
    return w.bytes;
}
Packet Exchange::decode(const Bytes& bytes) {
    if (bytes.size() > limit) throw std::length_error("net: boundary packet too large");
    Reader r{bytes};
    if (r.word(4) != 0x53474e01) throw std::invalid_argument("net: unsupported boundary packet");
    Packet p; p.from = r.text(); p.to = r.text(); p.step = {r.tick(), r.tick(), r.tick()};
    const auto count = r.word(4);
    if (count > 65536) throw std::invalid_argument("net: too many boundary overlaps");
    for (std::uint64_t i = 0; i < count; ++i) {
        BoundaryValue b; b.overlap = r.text(); b.basis = r.tick(); const auto n = r.word(4);
        if (n > 1000000 || n > (bytes.size() - r.pos)/8) throw std::invalid_argument("net: invalid boundary dimension");
        for (std::uint64_t c = 0; c < n; ++c) { const auto bits = r.word(8); double v; std::memcpy(&v, &bits, 8); b.values.push_back(v); }
        p.boundaries.push_back(std::move(b));
    }
    if (r.pos != bytes.size()) throw std::invalid_argument("net: trailing boundary bytes");
    check(p); return p;
}
Params Exchange::arguments(const Bytes& bytes) {
    if (bytes.size() > limit) throw std::length_error("net: boundary packet too large");
    static constexpr char digits[] = "0123456789abcdef";
    std::string text; text.reserve(bytes.size()*2);
    for (auto b : bytes) { text.push_back(digits[b >> 4]); text.push_back(digits[b & 15]); }
    Params p; p.set("packet", std::move(text)); return p;
}
Bytes Exchange::bytes(const Params& p) {
    const auto* text = p.text("packet");
    if (!text || text->size()%2 || text->size()/2 > limit) throw std::invalid_argument("net: invalid packet argument");
    const auto nibble = [](char c) -> std::uint8_t {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        throw std::invalid_argument("net: invalid packet encoding");
    };
    Bytes out;
    for (std::size_t i = 0; i < text->size(); i += 2) out.push_back((nibble((*text)[i]) << 4) | nibble((*text)[i+1]));
    return out;
}
Step Exchange::step(const State& state) {
    return step(state.params());
}
Step Exchange::step(const Params& p) {
    Step s{detail::integer(p, "epoch", 0), detail::integer(p, "generation", 0), detail::integer(p, "round", 0)};
    if (s.epoch < 0 || s.generation < 0 || s.tick < 0) throw std::invalid_argument("net: negative distributed step");
    return s;
}
std::vector<BoundaryChange> Exchange::receive(const State& state, const Partition& partition, const Packet& packet) {
    check(packet);
    const auto step = Exchange::step(state);
    if (packet.to != partition.peer().id || packet.step.epoch != step.epoch || packet.step.generation != step.generation || packet.step.tick < step.tick ||
        (packet.step.tick > step.tick && packet.step.tick - step.tick > 1)) return {};
    std::vector<BoundaryChange> changes;
    for (const auto& b : packet.boundaries) {
        const auto match = std::find_if(partition.boundaries().begin(), partition.boundaries().end(), [&](const Boundary& edge) { return edge.overlap.str() == b.overlap && edge.neighbor == packet.from; });
        if (match == partition.boundaries().end()) return {};
        if (!b.values.empty() && b.values.size() != match->dimension) throw std::invalid_argument("net: wrong boundary dimension");
        auto p = state.element(match->overlap).params;
        if (!detail::same_step(p, "remote", step))
            p.set("remote_epoch", step.epoch).set("remote_generation", step.generation).set("remote_tick", std::int64_t{-1}).set("remote_basis", std::int64_t{-1}).set("next_tick", std::int64_t{-1}).set("next_ready", false);
        const bool next = packet.step.tick != step.tick;
        const Key tick_key = next ? Key{"next_tick"} : Key{"remote_tick"};
        if (detail::integer(p, tick_key, -1) == packet.step.tick) continue;
        std::vector<double> values = b.values;
        if (values.empty() && detail::integer(p, "remote_basis", -1) == b.basis) values = detail::projection(p, "remote", match->dimension);
        p.set(tick_key, packet.step.tick).set(next ? Key{"next_basis"} : Key{"remote_basis"}, b.basis).set(next ? Key{"next_ready"} : Key{"remote_ready"}, !values.empty());
        if (!values.empty()) detail::projection(p, next ? "next_remote" : "remote", values);
        if (!next && !values.empty() && detail::integer(p, "next_tick", -1) > step.tick && !detail::flag(p, "next_ready", false) && detail::integer(p, "next_basis", -1) == b.basis) {
            detail::projection(p, "next_remote", values); p.set("next_ready", true);
        }
        changes.push_back({match->overlap, std::move(p)});
    }
    return changes;
}
} // namespace sg::net
