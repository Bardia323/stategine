#include "sg/net/Backend.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace sg::net {
namespace {
void offsets(const std::vector<std::uint32_t>& v, std::size_t end) {
    if (v.empty() || v.front() != 0 || v.back() != end || !std::is_sorted(v.begin(), v.end())) throw std::invalid_argument("net: invalid numerical offsets");
}
void values(const std::vector<double>& v, bool nonnegative = false) {
    for (const auto x : v)
        if (!std::isfinite(x) || (nonnegative && x < 0)) throw std::invalid_argument("net: invalid numerical value or weight");
}
void coordinates(const Layout& l, const std::vector<double>& x) {
    if (l.stalk_offsets.empty() || x.size() != l.stalk_offsets.back()) throw std::invalid_argument("net: wrong coordinate count");
}
void transpose(const Layout& l, const std::vector<double>& edge, std::vector<double>& out) {
    out.assign(l.stalk_offsets.back(), 0);
    for (std::size_t c = 0; c < out.size(); ++c)
        for (auto j = l.column_offsets[c]; j < l.column_offsets[c + 1]; ++j) out[c] += l.restrictions[l.transpose_entries[j]] * edge[l.rows[j]];
}
void with_workspace(const LinearSystem& s, const std::vector<double>& x, std::vector<double>& edge, std::vector<double>& out) {
    if (s.lambda == 0) out.assign(x.size(), 0);
    else {
        edge.assign(s.overlap_weights.size(), 0);
        for (std::size_t r = 0; r < edge.size(); ++r) {
            if (s.overlap_weights[r] == 0) continue;
            for (auto j = s.layout.row_offsets[r]; j < s.layout.row_offsets[r + 1]; ++j)
                edge[r] += s.layout.restrictions[j] * x[s.layout.columns[j]];
            edge[r] *= s.overlap_weights[r];
        }
        transpose(s.layout, edge, out);
    }
    for (std::size_t c = 0; c < x.size(); ++c) out[c] = s.confidence[c] * x[c] + s.lambda * out[c];
}
}
bool Layout::operator==(const Layout& l) const {
    return stalk_offsets == l.stalk_offsets && overlap_offsets == l.overlap_offsets && row_offsets == l.row_offsets && columns == l.columns &&
        restrictions == l.restrictions && column_offsets == l.column_offsets && rows == l.rows && transpose_entries == l.transpose_entries;
}
void validate(const LinearSystem& s) {
    const auto& l = s.layout;
    const auto n = s.observations.size(), m = s.overlap_weights.size(), nnz = l.restrictions.size();
    offsets(l.stalk_offsets, n); offsets(l.overlap_offsets, m); offsets(l.row_offsets, nnz); offsets(l.column_offsets, nnz);
    if (l.row_offsets.size() != m + 1 || l.column_offsets.size() != n + 1 || l.columns.size() != nnz || l.rows.size() != nnz ||
        l.transpose_entries.size() != nnz || s.confidence.size() != n || s.fixed.size() != n || s.pins.size() != n)
        throw std::invalid_argument("net: inconsistent numerical buffer sizes");
    for (const auto c : l.columns) if (c >= n) throw std::invalid_argument("net: invalid restriction column");
    std::vector<std::uint8_t> seen(nnz, 0);
    for (std::size_t c = 0; c < n; ++c)
        for (auto j = l.column_offsets[c]; j < l.column_offsets[c + 1]; ++j) {
            const auto r = l.rows[j], entry = l.transpose_entries[j];
            if (r >= m || entry >= nnz || l.columns[entry] != c || entry < l.row_offsets[r] || entry >= l.row_offsets[r + 1] || seen[entry]++)
                throw std::invalid_argument("net: invalid restriction transpose");
        }
    for (const auto p : s.fixed) if (p > 1) throw std::invalid_argument("net: invalid fixed-variable mask");
    values(l.restrictions); values(s.observations); values(s.pins); values(s.confidence, true); values(s.overlap_weights, true);
    if (!std::isfinite(s.lambda) || s.lambda < 0 || !std::isfinite(s.relaxation) || s.relaxation <= 0 || s.relaxation > 1 || s.iterations > 65536)
        throw std::invalid_argument("net: invalid solver controls");
}
void coboundary(const Layout& l, const std::vector<double>& x, std::vector<double>& out) {
    coordinates(l, x); out.assign(l.overlap_offsets.back(), 0);
    for (std::size_t r = 0; r < out.size(); ++r)
        for (auto j = l.row_offsets[r]; j < l.row_offsets[r + 1]; ++j) out[r] += l.restrictions[j] * x[l.columns[j]];
}
void laplacian(const LinearSystem& s, const std::vector<double>& x, std::vector<double>& out) {
    std::vector<double> edge;
    coboundary(s.layout, x, edge);
    for (std::size_t r = 0; r < edge.size(); ++r) edge[r] = s.overlap_weights[r] == 0 ? 0 : edge[r] * s.overlap_weights[r];
    transpose(s.layout, edge, out);
}
void apply(const LinearSystem& s, const std::vector<double>& x, std::vector<double>& out) {
    coordinates(s.layout, x);
    std::vector<double> edge;
    with_workspace(s, x, edge, out);
}
double step_size(const LinearSystem& s) {
    const auto& l = s.layout;
    std::vector<double> sums(s.overlap_weights.size(), 0);
    for (std::size_t r = 0; r < sums.size(); ++r)
        if (s.lambda != 0 && s.overlap_weights[r] != 0)
            for (auto j = l.row_offsets[r]; j < l.row_offsets[r + 1]; ++j) sums[r] += std::fabs(l.restrictions[j]);
    double bound = 0;
    for (std::size_t c = 0; c < s.observations.size(); ++c) {
        if (s.fixed[c]) continue;
        double row_sum = s.confidence[c];
        if (s.lambda != 0)
            for (auto j = l.column_offsets[c]; j < l.column_offsets[c + 1]; ++j)
                if (s.overlap_weights[l.rows[j]] != 0)
                    row_sum += s.lambda * std::fabs(l.restrictions[l.transpose_entries[j]]) * s.overlap_weights[l.rows[j]] * sums[l.rows[j]];
        if (!std::isfinite(row_sum)) throw std::overflow_error("net: operator bound overflow");
        bound = std::max(bound, row_sum);
    }
    const double alpha = bound == 0 ? 0 : s.relaxation / bound;
    if (!std::isfinite(alpha)) throw std::overflow_error("net: step size overflow");
    return alpha;
}
Residual measure(const LinearSystem& s, const std::vector<double>& x) {
    validate(s); coordinates(s.layout, x); values(x);
    std::vector<double> edge, ax;
    coboundary(s.layout, x, edge); apply(s, x, ax);
    Residual out;
    for (std::size_t r = 0; r < edge.size(); ++r)
        if (s.overlap_weights[r] != 0) out.disagreement = std::hypot(out.disagreement, std::sqrt(s.overlap_weights[r]) * edge[r]);
    for (std::size_t c = 0; c < x.size(); ++c)
        if (!s.fixed[c]) {
            const double equation = ax[c] - s.confidence[c] * s.observations[c];
            if (!std::isfinite(equation)) throw std::overflow_error("net: equation residual overflow");
            out.equation = std::max(out.equation, std::fabs(equation));
        }
    if (!std::isfinite(out.disagreement) || !std::isfinite(out.equation)) throw std::overflow_error("net: residual overflow");
    return out;
}
std::vector<double> CpuBackend::solve(const LinearSystem& s) const {
    validate(s);
    auto x = s.observations;
    for (std::size_t c = 0; c < x.size(); ++c) if (s.fixed[c]) x[c] = s.pins[c];
    const double alpha = step_size(s);
    std::vector<double> ax, edge;
    for (std::uint32_t iteration = 0; iteration < s.iterations; ++iteration) {
        with_workspace(s, x, edge, ax);
        for (std::size_t c = 0; c < x.size(); ++c)
            if (!s.fixed[c]) x[c] -= alpha * (ax[c] - s.confidence[c] * s.observations[c]);
    }
    values(x);
    return x;
}
Residual CpuBackend::residual(const LinearSystem& s, const std::vector<double>& x) const { return measure(s, x); }
#ifndef SG_NET_WITH_CUDA
struct CudaBackend::Impl {};
CudaBackend::CudaBackend() : impl_(std::make_unique<Impl>()) {}
CudaBackend::~CudaBackend() = default;
bool CudaBackend::available() const { return false; }
std::vector<double> CudaBackend::solve(const LinearSystem&) const { throw std::runtime_error("net: CUDA backend was not built"); }
Residual CudaBackend::residual(const LinearSystem& s, const std::vector<double>& x) const { return measure(s, x); }
TransferStats CudaBackend::transfers() const { return {}; }
#endif
} // namespace sg::net
