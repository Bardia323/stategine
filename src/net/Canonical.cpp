#include "Canonical.hpp"
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace sg::net::detail {
Writer::Writer(const std::string& domain) { text(domain); }
void Writer::integer(std::uint64_t v) { for (int s = 56; s >= 0; s -= 8) bytes_.push_back(static_cast<std::uint8_t>(v>>s)); }
void Writer::real(double v) {
    if (!std::isfinite(v)) throw std::invalid_argument("net: nonfinite canonical number");
    if (v == 0) v = 0; // one encoding for signed zero
    std::uint64_t bits; static_assert(sizeof(bits) == sizeof(v)); std::memcpy(&bits,&v,sizeof(v)); integer(bits);
}
void Writer::bytes(const Bytes& v) { if (v.size() > 16777216) throw std::length_error("net: canonical buffer too large"); integer(v.size()); bytes_.insert(bytes_.end(),v.begin(),v.end()); }
void Writer::text(const std::string& v) { if (v.size() > 4096) throw std::length_error("net: canonical name too long"); bytes(Bytes(v.begin(),v.end())); }
void Writer::digest(const Digest& v) { bytes_.insert(bytes_.end(),v.begin(),v.end()); }
void Writer::signature(const Signature& v) { bytes_.insert(bytes_.end(),v.begin(),v.end()); }
const Bytes& Writer::data() const { return bytes_; }
Reader::Reader(const Bytes& bytes, const std::string& domain) : bytes_(bytes) {
    if (bytes.size() > 16777216 || text() != domain) throw std::invalid_argument("net: wrong protocol domain or message size");
}
void Reader::need(std::size_t n) const { if (n > bytes_.size()-offset_) throw std::invalid_argument("net: truncated canonical message"); }
std::uint64_t Reader::integer() { need(8); std::uint64_t v = 0; for (int i = 0; i < 8; ++i) v = (v<<8)|bytes_[offset_++]; return v; }
double Reader::real() { const auto bits = integer(); double v; std::memcpy(&v,&bits,sizeof(v)); if (!std::isfinite(v) || (v == 0 && std::signbit(v))) throw std::invalid_argument("net: noncanonical number"); return v; }
Bytes Reader::bytes(std::size_t maximum) { const auto n = integer(); if (n > maximum) throw std::length_error("net: oversized canonical field"); need(static_cast<std::size_t>(n)); Bytes v(bytes_.begin()+offset_,bytes_.begin()+offset_+n); offset_ += n; return v; }
std::string Reader::text() { const auto v = bytes(4096); return std::string(v.begin(),v.end()); }
Digest Reader::digest() { need(32); Digest v; std::memcpy(v.data(),bytes_.data()+offset_,32); offset_ += 32; return v; }
Signature Reader::signature() { need(64); Signature v; std::memcpy(v.data(),bytes_.data()+offset_,64); offset_ += 64; return v; }
void Reader::end() const { if (offset_ != bytes_.size()) throw std::invalid_argument("net: trailing canonical data"); }
void observation(Writer& w, const Observation& o) {
    w.text(o.peer); w.digest(o.context); w.integer(o.tick); w.integer(o.sequence); w.text(o.object); w.text(o.parameter);
    w.integer(o.discrete); w.text(o.conflict); w.bytes(o.payload); w.signature(o.signature);
}
Observation observation(Reader& r) {
    Observation o; o.peer = r.text(); o.context = r.digest(); o.tick = r.integer(); o.sequence = r.integer(); o.object = r.text(); o.parameter = r.text();
    const auto discrete = r.integer(); if (discrete > 1) throw std::invalid_argument("net: invalid observation kind");
    o.discrete = discrete != 0; o.conflict = r.text(); o.payload = r.bytes(); o.signature = r.signature(); return o;
}
}
