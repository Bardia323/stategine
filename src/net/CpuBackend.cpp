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
void validate_layout(const Layout& l) {
    if (l.stalk_offsets.empty() || l.overlap_offsets.empty()) throw std::invalid_argument("net: missing numerical offsets");
    const auto n = l.stalk_offsets.back(), m = l.overlap_offsets.back();
    const auto nnz = l.restrictions.size();
    offsets(l.stalk_offsets, n); offsets(l.overlap_offsets, m); offsets(l.row_offsets, nnz); offsets(l.column_offsets, nnz);
    if (l.row_offsets.size() != static_cast<std::size_t>(m)+1 || l.column_offsets.size() != static_cast<std::size_t>(n)+1 || l.columns.size() != nnz || l.rows.size() != nnz || l.transpose_entries.size() != nnz)
        throw std::invalid_argument("net: inconsistent numerical layout sizes");
    for (const auto c : l.columns) if (c >= n) throw std::invalid_argument("net: invalid restriction column");
    std::vector<std::uint8_t> seen(nnz,0);
    for (std::size_t c = 0; c < n; ++c)
        for (auto j = l.column_offsets[c]; j < l.column_offsets[c+1]; ++j) {
            const auto r = l.rows[j], entry = l.transpose_entries[j];
            if (r >= m || entry >= nnz || l.columns[entry] != c || entry < l.row_offsets[r] || entry >= l.row_offsets[r+1] || seen[entry]++) throw std::invalid_argument("net: invalid restriction transpose");
        }
    values(l.restrictions);
}
void validate_dynamic(const LinearSystem& s) {
    if (s.layout.stalk_offsets.empty() || s.layout.overlap_offsets.empty() || s.observations.size() != s.layout.stalk_offsets.back() || s.confidence.size() != s.observations.size() || s.fixed.size() != s.observations.size() || s.pins.size() != s.observations.size() || s.overlap_weights.size() != s.layout.overlap_offsets.back()) throw std::invalid_argument("net: inconsistent numerical buffer sizes");
    values(s.observations); values(s.pins);
}
void validate_constants(const LinearSystem& s) {
    values(s.confidence,true); values(s.overlap_weights,true);
    for (auto mask : s.fixed) if (mask > 1) throw std::invalid_argument("net: invalid fixed-variable mask");
    if (!std::isfinite(s.lambda) || s.lambda < 0 || !std::isfinite(s.relaxation) || s.relaxation <= 0 || s.relaxation > 1 || s.iterations > 65536) throw std::invalid_argument("net: invalid solver controls");
}
void validate(const LinearSystem& s) { validate_layout(s.layout); validate_dynamic(s); validate_constants(s); }
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
double operator_bound(const LinearSystem& s) {
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
    return bound;
}
double step_size(const LinearSystem& s) {
    const double bound = operator_bound(s);
    const double alpha = bound == 0 ? 0 : s.relaxation / bound;
    if (!std::isfinite(alpha)) throw std::overflow_error("net: step size overflow");
    return alpha;
}
PreparedSystem::PreparedSystem(const LinearSystem& s, double step) : system_(&s),step_(step) {}
PreparedSystem Backend::prepare(const LinearSystem& s) { validate(s); return PreparedSystem(s,step_size(s)); }
void Backend::solve_prepared(const PreparedSystem& s,SolveWorkspace&,std::vector<double>& out) const { out = solve(s.system()); }
Residual Backend::residual_prepared(const PreparedSystem& s,const std::vector<double>& x,SolveWorkspace&) const { return residual(s.system(),x); }
Residual measure(const PreparedSystem& prepared,const std::vector<double>& x,SolveWorkspace& work) {
    const auto& s = prepared.system(); coordinates(s.layout,x); values(x);
    work.resize(work.delta,s.overlap_weights.size()); coboundary(s.layout,x,work.delta);
    work.resize(work.edge,s.overlap_weights.size()); work.resize(work.ax,x.size()); with_workspace(s,x,work.edge,work.ax);
    Residual out;
    for (std::size_t r = 0; r < work.delta.size(); ++r)
        if (s.overlap_weights[r] != 0) out.disagreement = std::hypot(out.disagreement,std::sqrt(s.overlap_weights[r])*work.delta[r]);
    for (std::size_t c = 0; c < x.size(); ++c) if (!s.fixed[c]) {
        const double equation = work.ax[c]-s.confidence[c]*s.observations[c];
        if (!std::isfinite(equation)) throw std::overflow_error("net: equation residual overflow");
        out.equation = std::max(out.equation,std::fabs(equation));
    }
    if (!std::isfinite(out.disagreement) || !std::isfinite(out.equation)) throw std::overflow_error("net: residual overflow");
    return out;
}
Residual measure(const LinearSystem& s,const std::vector<double>& x) {
    // Residuals need validation but do not need a step bound.
    validate(s); SolveWorkspace work;
    return measure(PreparedSystem(s,0),x,work);
}
std::vector<double> CpuBackend::solve(const LinearSystem& s) const {
    const auto prepared = Backend::prepare(s); SolveWorkspace work; std::vector<double> out;
    solve_prepared(prepared,work,out); return out;
}
void CpuBackend::solve_prepared(const PreparedSystem& prepared,SolveWorkspace& work,std::vector<double>& x) const {
    const auto& s = prepared.system(); work.resize(x,s.observations.size()); x = s.observations;
    for (std::size_t c = 0; c < x.size(); ++c) if (s.fixed[c]) x[c] = s.pins[c];
    work.resize(work.ax,x.size()); work.resize(work.edge,s.overlap_weights.size());
    for (std::uint32_t iteration = 0; iteration < s.iterations; ++iteration) {
        with_workspace(s,x,work.edge,work.ax);
        for (std::size_t c = 0; c < x.size(); ++c) if (!s.fixed[c]) x[c] -= prepared.step()*(work.ax[c]-s.confidence[c]*s.observations[c]);
    }
    values(x);
}
Residual CpuBackend::residual(const LinearSystem& s,const std::vector<double>& x) const { return measure(s,x); }
Residual CpuBackend::residual_prepared(const PreparedSystem& s,const std::vector<double>& x,SolveWorkspace& work) const { return measure(s,x,work); }
#ifndef SG_NET_WITH_CUDA
struct CudaBackend::Impl {};
CudaBackend::CudaBackend() : impl_(std::make_unique<Impl>()) {}
CudaBackend::~CudaBackend() = default;
bool CudaBackend::available() const { return false; }
std::vector<double> CudaBackend::solve(const LinearSystem&) const { throw std::runtime_error("net: CUDA backend was not built"); }
Residual CudaBackend::residual(const LinearSystem& s, const std::vector<double>& x) const { return measure(s, x); }
void CudaBackend::solve_prepared(const PreparedSystem&,SolveWorkspace&,std::vector<double>&) const { throw std::runtime_error("net: CUDA backend was not built"); }
Residual CudaBackend::residual_prepared(const PreparedSystem& s,const std::vector<double>& x,SolveWorkspace& work) const { return measure(s,x,work); }
TransferStats CudaBackend::transfers() const { return {}; }
#endif
} // namespace sg::net
