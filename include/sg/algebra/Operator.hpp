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
    Operator& set(uint32_t row, const std::vector<Term>& terms, double bias = 0.0);
    // y[to] = y[from]: a value copied, whatever it stands for.
    Operator& copy(uint32_t from, uint32_t to) { return set(to, {Term{from, 1.0}}); }

    // This, then `next`.
    Operator& then(const Operator& next);

    void apply(std::vector<double>& y) const;

    bool empty() const { return rows_.empty(); }
    const std::vector<Row>& rows() const { return rows_; }
    const std::vector<Term>& terms() const { return terms_; }

    // As one matrix, n + 1 square (the last row and column are the constant
    // 1), row-major: the product of every row update, in order.
    std::vector<double> dense(uint32_t n) const;

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
double apart(Compare c, double a, double b);
bool same(Compare c, double a, double b);

// The same, said with care: 1 the same, 0 not, 2 not sure - apart by so
// nearly the tolerance that a sum taken in another order (as a compiled
// program takes it) could fall the other side. Not sure is asked again of
// the verifier.
int judge(Compare c, double a, double b);

}  // namespace sg::algebra
