#include "sg/algebra/Operator.hpp"

namespace sg::algebra {

auto Operator::set(uint32_t row, const std::vector<Term>& terms, double bias) -> Operator& {
    rows_.push_back(Row{row, static_cast<uint32_t>(terms_.size()), static_cast<uint32_t>(terms.size()), bias});
    terms_.insert(terms_.end(), terms.begin(), terms.end());
    return *this;
}

auto Operator::then(const Operator& next) -> Operator& {
    for (const Row& r : next.rows_) {
        std::vector<Term> t(next.terms_.begin() + r.first, next.terms_.begin() + r.first + r.count);
        set(r.row, t, r.bias);
    }
    return *this;
}

void Operator::apply(std::vector<double>& y) const {
    for (const Row& r : rows_) {
        double v = r.bias;
        for (uint32_t i = r.first; i < r.first + r.count; ++i) v += terms_[i].k * y[terms_[i].col];
        y[r.row] = v;
    }
}

std::vector<double> Operator::dense(uint32_t n) const {
    const uint32_t m = n + 1;
    std::vector<double> a(static_cast<std::size_t>(m) * m, 0.0);
    for (uint32_t i = 0; i < m; ++i) a[i * m + i] = 1.0;
    for (const Row& r : rows_) {
        // Row r of the product becomes k_i * (row col_i) + bias * (row n).
        std::vector<double> next(m, 0.0);
        for (uint32_t t = r.first; t < r.first + r.count; ++t)
            for (uint32_t j = 0; j < m; ++j) next[j] += terms_[t].k * a[terms_[t].col * m + j];
        for (uint32_t j = 0; j < m; ++j) next[j] += r.bias * a[n * m + j];
        for (uint32_t j = 0; j < m; ++j) a[r.row * m + j] = next[j];
    }
    return a;
}

double apart(Compare c, double a, double b) {
    double d = a - b;
    if (c == Compare::Angle) {
        const double turn = 6.283185307179586;
        d = std::fmod(d, turn);
        if (d > turn * 0.5) d -= turn;
        if (d < -turn * 0.5) d += turn;
    }
    return std::fabs(d);
}

bool same(Compare c, double a, double b) {
    if (a == b) return true;
    if (c == Compare::Exact) return false;
    return apart(c, a, b) < 1e-6;
}

int judge(Compare c, double a, double b) {
    if (a == b) return 1;
    if (c == Compare::Exact) return 0;
    const double d = apart(c, a, b);
    if (!(d == d)) return 0;       // NaN: not the same
    if (d < 1e-6 * 0.999) return 1;
    if (d > 1e-6 * 1.001) return 0;
    return 2;
}

}  // namespace sg::algebra
