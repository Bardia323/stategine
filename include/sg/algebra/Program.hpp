// Stategine - algebra: programs, and batches of them.
//
// A program is an operator on a vector of `slots` values (Operator.hpp):
// what a path does to the data it reads, compiled. Programs compose (one
// after another), apply (to a vector), compare (two of them on the same
// vector: where they leave different values), and have an adjoint (the
// transpose of what they do, as a matrix). Still neutral: slots are numbers,
// not parameters - the compiler (Compile.hpp) keeps which is which.
//
// A job is an equation of two programs on one vector, and the slots to
// compare with how; a batch is many jobs, laid out flat - plain arrays and
// offsets into them - so that any backend (Backend.hpp) can run it as it is:
// one job to a thread, every row a dot product. `run_job` is the one
// definition of what running a job means; every backend does exactly that.
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "sg/algebra/Operator.hpp"

namespace sg::algebra {

struct Program {
    uint32_t slots = 0;
    Operator op;

    Program& then(const Program& next);
    static Program compose(Program a, const Program& b) { return a.then(b); }

    std::vector<double> apply(std::vector<double> x) const;

    // Where this and `other`, run on `x`, leave `slots` different.
    struct Slot {
        uint32_t slot;
        Compare how;
    };
    std::vector<uint32_t> compare(const Program& other, const std::vector<double>& x, const std::vector<Slot>& which) const;

    // The transpose of its linear part, as a matrix (slots square,
    // row-major): the program run backwards on covectors.
    std::vector<double> adjoint() const;
};

// Many equations, flat.
struct Batch {
    // Every job's rows and terms, and the vectors they start from, end to end.
    std::vector<uint32_t> row_slot, row_first, row_count;
    std::vector<double> row_bias;
    std::vector<uint32_t> term_col;
    std::vector<double> term_k;
    std::vector<double> start;  // each job's vector
    std::vector<uint32_t> cmp_slot;
    std::vector<uint8_t> cmp_how;
    // Per job: [lhs rows), [rhs rows), its vector, the slots it compares.
    struct Job {
        uint32_t lhs_first, lhs_count, rhs_first, rhs_count;
        uint32_t x_first, slots;
        uint32_t cmp_first, cmp_count;
    };
    std::vector<Job> jobs;

    std::size_t size() const { return jobs.size(); }
    bool empty() const { return jobs.empty(); }
    void clear() { *this = Batch{}; }

    // One job: `lhs` and `rhs` on `x`, compared at `which`. Its index.
    std::size_t add(const Program& lhs, const Program& rhs, const std::vector<double>& x,
                    const std::vector<Program::Slot>& which);

private:
    uint32_t rows(const Operator& op);
};

// What a job found: its two sides agree on every slot compared (1), part
// somewhere (0: the first slot where they do), or cannot be told apart for
// certain (2) - asked again of the verifier, as a part is.
struct Verdict {
    uint8_t agree = 1;
    uint32_t slot = 0;
};

// Running one job, as every backend runs it: the two sides, each on its own
// copy of the job's vector (`lhs`, `rhs`: scratch of `slots` each), then the
// slots compared. Written for any hardware: plain loops over flat arrays.
Verdict run_job(const Batch& b, std::size_t j, double* lhs, double* rhs);

}  // namespace sg::algebra
