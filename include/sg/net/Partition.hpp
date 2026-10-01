// Execution selection derived from participant solver params.
#pragma once
#include "sg/net/Cellular.hpp"
#include "sg/net/Peer.hpp"

namespace sg::net {
struct Boundary {
    Key overlap;
    std::string neighbor;
    std::uint32_t edge, stalk, offset, dimension;
    bool local_left;
    bool operator==(const Boundary& other) const;
};
class Partition {
public:
    bool update(const State& state, const Cellular& cellular);
    const Peer& peer() const { return peer_; }
    const Layout& layout() const { return layout_; }
    const std::vector<std::uint32_t>& stalks() const { return stalks_; }
    const std::vector<std::uint32_t>& overlaps() const { return overlaps_; }
    const std::vector<Boundary>& boundaries() const { return boundaries_; }
    std::uint32_t coordinates() const { return coordinates_; }
    std::uint64_t compilations() const { return compilations_; }
private:
    Peer peer_;
    Layout layout_;
    std::vector<std::uint32_t> stalks_, overlaps_;
    std::vector<Boundary> boundaries_;
    std::uint32_t coordinates_ = 0;
    std::uint64_t compilations_ = 0;
    std::uint64_t topology_ = 0;
    bool built_ = false;
};
} // namespace sg::net
