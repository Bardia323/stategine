// A canonical regional numerical problem, never a semantic world object.
#pragma once
#include "sg/net/Integrity.hpp"
#include "sg/net/LinearSystem.hpp"

namespace sg::net {
class Cellular;
struct SolverSpec {
    std::string method = "bounded-gradient-reference-v1";
    std::string rules = "continuous-v1";
    std::uint32_t iterations = 256;
    double lambda = 1, relaxation = 0.9;
    double quantum = 1e-8, solve_tolerance = 1e-6;
    double residual_tolerance = 1e-6, equation_tolerance = 1e-6;
    void validate() const;
    Digest id() const;
};
struct EpochContext {
    std::string world, partition;
    std::uint64_t tick = 0, generation = 0;
    Digest topology{}, constraints{}, solver{}, committee{}, previous{};
    Digest id() const;
};
struct Epoch {
    EpochContext context;
    Digest inputs{};
    std::vector<Observation> ordered_inputs;
    Digest id() const;
};
// Owned numerical buffers allow a problem to outlive a disposable GPU cache.
struct Problem {
    Epoch epoch;
    Layout layout;
    std::vector<double> observations, confidence, overlap_weights, pins;
    std::vector<std::uint8_t> fixed;
    SolverSpec solver;
    Bytes declared_constraints;
    Bytes declared_topology;
    LinearSystem system() const;
    static Problem make(EpochContext context, const LinearSystem& system,
                        SolverSpec solver, std::vector<Observation> inputs,
                        Bytes declared_constraints = {}, Bytes declared_topology = {});
    void validate() const;
};
Digest topology_hash(const Layout& layout, const Bytes& declared_topology = {});
Bytes topology_description(const Cellular& cellular);
Digest constraint_hash(const LinearSystem& system, const Bytes& declared_constraints);
std::vector<Observation> order_inputs(std::vector<Observation> inputs);
Bytes canonical_result(const std::vector<double>& values, double quantum);
std::vector<double> canonical_values(const LinearSystem& system, const std::vector<double>& values, double quantum);
}
