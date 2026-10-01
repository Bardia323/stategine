// Flat derived numerical buffers. No state, element, event or graph lives here.
#pragma once
#include <cstdint>
#include <vector>

namespace sg::net {
// Signed restriction blocks compiled as CSR rows of delta and CSC columns
// of its transpose. No Laplacian is materialized.
struct Layout {
    std::vector<std::uint32_t> stalk_offsets, overlap_offsets;
    std::vector<std::uint32_t> row_offsets, columns;
    std::vector<double> restrictions;
    std::vector<std::uint32_t> column_offsets, rows, transpose_entries;
    bool operator==(const Layout& other) const;
};
struct LinearSystem {
    const Layout& layout;
    std::vector<double> observations, confidence, overlap_weights, pins;
    std::vector<std::uint8_t> fixed;
    double lambda = 1;
    double relaxation = 0.9;
    std::uint32_t iterations = 128;
};
struct Residual {
    double disagreement = 0; // sqrt(delta(x)* W delta(x))
    double equation = 0;    // maximum free-coordinate |Ax - My|
};
void validate(const LinearSystem& system);
void validate_layout(const Layout& layout);
void validate_constants(const LinearSystem& system);
void validate_dynamic(const LinearSystem& system);
void coboundary(const Layout& layout, const std::vector<double>& x, std::vector<double>& out);
void laplacian(const LinearSystem& system, const std::vector<double>& x, std::vector<double>& out);
void apply(const LinearSystem& system, const std::vector<double>& x, std::vector<double>& out);
double step_size(const LinearSystem& system);
double operator_bound(const LinearSystem& system);
Residual measure(const LinearSystem& system, const std::vector<double>& x);
} // namespace sg::net
