// Stategine - algebra: operators on a vector of values.
//
// Neutral: nothing here knows a state, an element or a law. A vector is a row
// of numbers (slots); an operator is a list of row updates, each run in turn:
//
//     y[row] = sum_i k_i * y[col_i] + bias
//
// read from the vector as it is when that row runs - so an operator is a
// product of elementary affine maps, and two operators compose by running one
// list after the other. That is exactly how a declared step writes an
// element's parameters, one after another (sg/core/Declared.hpp), and it is
// what lets a batch of them be run side by side on any hardware: every row
// is a small dot product.
//
// Every value is a double. What is not a number (a word, a flag) is carried
// as a number that stands for it - only ever copied, never added to - and
// compared exactly (Compare::Exact).
#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

namespace sg::algebra {

struct Term {
    uint32_t col = 0;
    double k = 1.0;
};

struct Row {
    uint32_t row = 0;
    uint32_t first = 0, count = 0;  // its terms, in the operator's `terms`
    double bias = 0.0;
};

class Operator {
public:
    // y[row] = sum k * y[col] + bias, after every row before it.
    Operator& set(uint32_t row, const std::vector<Term>& terms, double bias = 0.0) {
        rows_.push_back(Row{row, static_cast<uint32_t>(terms_.size()), static_cast<uint32_t>(terms.size()), bias});
        terms_.insert(terms_.end(), terms.begin(), terms.end());
        return *this;
    }
    // y[to] = y[from]: a value copied, whatever it stands for.
    Operator& copy(uint32_t from, uint32_t to) { return set(to, {Term{from, 1.0}}); }

    // This, then `next`.
    Operator& then(const Operator& next) {
        for (const Row& r : next.rows_) {
            std::vector<Term> t(next.terms_.begin() + r.first, next.terms_.begin() + r.first + r.count);
            set(r.row, t, r.bias);
        }
        return *this;
    }

    void apply(std::vector<double>& y) const {
        for (const Row& r : rows_) {
            double v = r.bias;
            for (uint32_t i = r.first; i < r.first + r.count; ++i) v += terms_[i].k * y[terms_[i].col];
            y[r.row] = v;
        }
    }

    bool empty() const { return rows_.empty(); }
    const std::vector<Row>& rows() const { return rows_; }
    const std::vector<Term>& terms() const { return terms_; }

    // As one matrix, n + 1 square (the last row and column are the constant
    // 1), row-major: the product of every row update, in order.
    std::vector<double> dense(uint32_t n) const {
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

private:
    std::vector<Row> rows_;
    std::vector<Term> terms_;
};

// How two values of a slot are held the same.
enum class Compare : uint8_t {
    Exact,   // the same number: a word, a flag, what a number stands for
    Number,  // a number, to within a hair (as the laws compare numbers)
    Angle    // a heading: the same on the circle
};

// The laws' own tolerance (sg::same_number): the same number, or apart by
// less than a millionth; a heading, the same modulo a full turn.
inline double apart(Compare c, double a, double b) {
    double d = a - b;
    if (c == Compare::Angle) {
        const double turn = 6.283185307179586;
        d = std::fmod(d, turn);
        if (d > turn * 0.5) d -= turn;
        if (d < -turn * 0.5) d += turn;
    }
    return std::fabs(d);
}
inline bool same(Compare c, double a, double b) {
    if (a == b) return true;
    if (c == Compare::Exact) return false;
    return apart(c, a, b) < 1e-6;
}

// The same, said with care: 1 the same, 0 not, 2 not sure - apart by so
// nearly the tolerance that a sum taken in another order (as a compiled
// program takes it) could fall the other side. Not sure is asked again of
// the verifier.
inline int judge(Compare c, double a, double b) {
    if (a == b) return 1;
    if (c == Compare::Exact) return 0;
    const double d = apart(c, a, b);
    if (!(d == d)) return 0;       // NaN: not the same
    if (d < 1e-6 * 0.999) return 1;
    if (d > 1e-6 * 1.001) return 0;
    return 2;
}

}  // namespace sg::algebra
