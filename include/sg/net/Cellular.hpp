// A cellular topology derived from ordinary state data, not another world.
#pragma once
#include "sg/core/State.hpp"
#include "sg/net/LinearSystem.hpp"

namespace sg::net {
struct Stalk {
    Key element;
    std::uint32_t dimension;
    bool operator==(const Stalk& other) const;
};
struct Overlap {
    Key element;
    std::uint32_t left, right, dimension;
    std::vector<double> left_restriction, right_restriction;
    bool operator==(const Overlap& other) const;
};
// Topology is memoized on ids, dimensions, endpoints and restriction entries.
// Observations, confidence, overlap weights and pins do not compile it again.
class Cellular {
public:
    Cellular() = default;
    explicit Cellular(const State& state);
    bool derive(const State& state); // metadata only, for neighborhood compilation
    bool update(const State& state);
    LinearSystem gather(const State& state);
    const Layout& layout() const;
    const std::vector<Stalk>& stalks() const { return stalks_; }
    const std::vector<Overlap>& overlaps() const { return overlaps_; }
    std::uint64_t compilations() const { return compilations_; }
    std::uint64_t revision() const { return revision_; }
    static Key coordinate_key(Key base, std::uint32_t dimension, std::uint32_t coordinate);
private:
    std::vector<Stalk> stalks_;
    std::vector<Overlap> overlaps_;
    Layout layout_;
    std::uint64_t compilations_ = 0;
    std::uint64_t revision_ = 0;
    bool built_ = false;
    bool compiled_ = false;
};
} // namespace sg::net
