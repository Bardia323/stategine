// Length-prefixed, bounded, big-endian protocol encoding.
#pragma once
#include "sg/net/Integrity.hpp"

namespace sg::net::detail {
class Writer {
public:
    explicit Writer(const std::string& domain);
    void integer(std::uint64_t value);
    void real(double value);
    void bytes(const Bytes& value);
    void text(const std::string& value);
    void digest(const Digest& value);
    void signature(const Signature& value);
    const Bytes& data() const;
private:
    Bytes bytes_;
};
class Reader {
public:
    explicit Reader(const Bytes& bytes, const std::string& domain);
    std::uint64_t integer();
    double real();
    Bytes bytes(std::size_t maximum = 16777216);
    std::string text();
    Digest digest();
    Signature signature();
    void end() const;
private:
    const Bytes& bytes_;
    std::size_t offset_ = 0;
    void need(std::size_t count) const;
};
void observation(Writer& writer, const Observation& observation);
Observation observation(Reader& reader);
}
