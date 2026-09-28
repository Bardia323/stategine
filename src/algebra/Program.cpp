#include "sg/algebra/Program.hpp"

namespace sg::algebra {

auto Program::then(const Program& next) -> Program& {
    if (next.slots > slots) slots = next.slots;
    op.then(next.op);
    return *this;
}

std::vector<double> Program::apply(std::vector<double> x) const {
    if (x.size() < slots) x.resize(slots, 0.0);
    op.apply(x);
    return x;
}

std::vector<uint32_t> Program::compare(const Program& other, const std::vector<double>& x, const std::vector<Slot>& which) const {
    const std::vector<double> l = apply(x), r = other.apply(x);
    std::vector<uint32_t> out;
    for (const Slot& s : which)
        if (!same(s.how, l[s.slot], r[s.slot])) out.push_back(s.slot);
    return out;
}

std::vector<double> Program::adjoint() const {
    const std::vector<double> a = op.dense(slots);
    const uint32_t m = slots + 1;
    std::vector<double> t(static_cast<std::size_t>(slots) * slots, 0.0);
    for (uint32_t i = 0; i < slots; ++i)
        for (uint32_t j = 0; j < slots; ++j) t[j * slots + i] = a[i * m + j];
    return t;
}

std::size_t Batch::add(const Program& lhs, const Program& rhs, const std::vector<double>& x, const std::vector<Program::Slot>& which) {
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

uint32_t Batch::rows(const Operator& op) {
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

Verdict run_job(const Batch& b, std::size_t j, double* lhs, double* rhs) {
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
