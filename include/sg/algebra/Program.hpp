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

    Program& then(const Program& next) {
        if (next.slots > slots) slots = next.slots;
        op.then(next.op);
        return *this;
    }
    static Program compose(Program a, const Program& b) { return a.then(b); }

    std::vector<double> apply(std::vector<double> x) const {
        if (x.size() < slots) x.resize(slots, 0.0);
        op.apply(x);
        return x;
    }

    // Where this and `other`, run on `x`, leave `slots` different.
    struct Slot {
        uint32_t slot;
        Compare how;
    };
    std::vector<uint32_t> compare(const Program& other, const std::vector<double>& x, const std::vector<Slot>& which) const {
        const std::vector<double> l = apply(x), r = other.apply(x);
        std::vector<uint32_t> out;
        for (const Slot& s : which)
            if (!same(s.how, l[s.slot], r[s.slot])) out.push_back(s.slot);
        return out;
    }

    // The transpose of its linear part, as a matrix (slots square,
    // row-major): the program run backwards on covectors.
    std::vector<double> adjoint() const {
        const std::vector<double> a = op.dense(slots);
        const uint32_t m = slots + 1;
        std::vector<double> t(static_cast<std::size_t>(slots) * slots, 0.0);
        for (uint32_t i = 0; i < slots; ++i)
            for (uint32_t j = 0; j < slots; ++j) t[j * slots + i] = a[i * m + j];
        return t;
    }
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
                    const std::vector<Program::Slot>& which) {
        Job j{};
        j.lhs_first = static_cast<uint32_t>(row_slot.size());
        j.lhs_count = rows(lhs.op);
        j.rhs_first = static_cast<uint32_t>(row_slot.size());
        j.rhs_count = rows(rhs.op);
        j.x_first = static_cast<uint32_t>(start.size());
        j.slots = std::max<uint32_t>(std::max(lhs.slots, rhs.slots), static_cast<uint32_t>(x.size()));
        start.insert(start.end(), x.begin(), x.end());
        start.resize(j.x_first + j.slots, 0.0);
        j.cmp_first = static_cast<uint32_t>(cmp_slot.size());
        j.cmp_count = static_cast<uint32_t>(which.size());
        for (const Program::Slot& s : which) {
            cmp_slot.push_back(s.slot);
            cmp_how.push_back(static_cast<uint8_t>(s.how));
        }
        jobs.push_back(j);
        return jobs.size() - 1;
    }

private:
    uint32_t rows(const Operator& op) {
        for (const Row& r : op.rows()) {
            row_slot.push_back(r.row);
            row_first.push_back(static_cast<uint32_t>(term_col.size()));
            row_count.push_back(r.count);
            row_bias.push_back(r.bias);
            for (uint32_t t = r.first; t < r.first + r.count; ++t) {
                term_col.push_back(op.terms()[t].col);
                term_k.push_back(op.terms()[t].k);
            }
        }
        return static_cast<uint32_t>(op.rows().size());
    }
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
inline Verdict run_job(const Batch& b, std::size_t j, double* lhs, double* rhs) {
    const Batch::Job& job = b.jobs[j];
    for (uint32_t i = 0; i < job.slots; ++i) lhs[i] = rhs[i] = b.start[job.x_first + i];
    const auto side = [&](uint32_t first, uint32_t count, double* y) {
        for (uint32_t r = first; r < first + count; ++r) {
            double v = b.row_bias[r];
            for (uint32_t t = b.row_first[r]; t < b.row_first[r] + b.row_count[r]; ++t) v += b.term_k[t] * y[b.term_col[t]];
            y[b.row_slot[r]] = v;
        }
    };
    side(job.lhs_first, job.lhs_count, lhs);
    side(job.rhs_first, job.rhs_count, rhs);
    Verdict v;
    for (uint32_t c = job.cmp_first; c < job.cmp_first + job.cmp_count; ++c) {
        const uint32_t s = b.cmp_slot[c];
        const int j = judge(static_cast<Compare>(b.cmp_how[c]), lhs[s], rhs[s]);
        if (j == 0) return Verdict{0, s};
        if (j == 2 && v.agree == 1) v = Verdict{2, s};
    }
    return v;
}

}  // namespace sg::algebra
