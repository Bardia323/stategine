// Derived reconciliation. Only the owning state's declared arrow writes back.
#pragma once
#include "sg/net/Cellular.hpp"
#include "sg/net/Backend.hpp"

namespace sg::net {
struct Reading {
    Key element;
    std::uint32_t coordinate, dimension;
    double value, correction;
};
struct Result {
    std::vector<Reading> readings;
    double residual = 0;
    double equation_residual = 0;
};
class Reconcile {
public:
    Result evaluate(const State& state, const Backend* backend = nullptr);
    const Cellular& cellular() const { return cellular_; }
private:
    Cellular cellular_; // a cache, restored by gathering the state's data again
};
} // namespace sg::net
