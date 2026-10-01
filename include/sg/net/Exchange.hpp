// Boundary projections and protocol progress are ordinary NetworkState data.
#pragma once
#include "sg/net/Partition.hpp"
#include "sg/net/Transport.hpp"

namespace sg::net {
struct BoundaryValue {
    std::string overlap;
    std::int64_t basis = 0; // monotone version of this projection, distinct from sender progress
    std::vector<double> values; // empty means that projection is unchanged
};
struct Packet {
    std::string from, to;
    Step step;
    std::vector<BoundaryValue> boundaries;
};
struct BoundaryChange {
    Key element;
    Params params;
};
class Exchange {
public:
    static Bytes encode(const Packet& packet);
    static Packet decode(const Bytes& bytes);
    static Params arguments(const Bytes& bytes);
    static Bytes bytes(const Params& arguments);
    // Pure receipt: the declared receive arrow applies these parameter changes.
    static std::vector<BoundaryChange> receive(const State& state, const Partition& partition, const Packet& packet, bool asynchronous = false);
    static Step step(const State& state);
    static Step step(const Params& params);
};
} // namespace sg::net
