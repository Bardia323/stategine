// Stategine - one job of a batch, on a device (CUDA, HIP).
//
// Exactly what sg::algebra::run_job does (include/sg/algebra/Program.hpp),
// on the batch's flat arrays: the two sides on their own scratch, then the
// slots compared - 1 the same, 0 apart (and where), 2 not sure. Included by
// a device's source with SG_DEVICE and SG_GLOBAL defined for it.
#include <cstdint>

// The laws' tolerance (sg::algebra::judge).
SG_DEVICE inline int sg_judge(uint8_t how, double a, double b) {
    if (a == b) return 1;
    if (how == 0) return 0;  // Exact
    double d = a - b;
    if (how == 2) {          // Angle
        const double turn = 6.283185307179586;
        d = fmod(d, turn);
        if (d > turn * 0.5) d -= turn;
        if (d < -turn * 0.5) d += turn;
    }
    d = fabs(d);
    if (!(d == d)) return 0;
    if (d < 1e-6 * 0.999) return 1;
    if (d > 1e-6 * 1.001) return 0;
    return 2;
}

// jobs: 8 per job - lhs_first, lhs_count, rhs_first, rhs_count, x_first, slots, cmp_first, cmp_count.
SG_GLOBAL void sg_run_jobs(uint32_t n, const uint32_t* jobs, const uint32_t* row_slot, const uint32_t* row_first,
                           const uint32_t* row_count, const double* row_bias, const uint32_t* term_col, const double* term_k,
                           const double* start, const uint32_t* cmp_slot, const uint8_t* cmp_how, const uint32_t* scratch_first,
                           double* scratch, uint8_t* agree, uint32_t* where) {
    const uint32_t j = SG_THREAD_INDEX;
    if (j >= n) return;
    const uint32_t* job = jobs + 8 * j;
    const uint32_t slots = job[5];
    double* lhs = scratch + scratch_first[j];
    double* rhs = lhs + slots;
    for (uint32_t i = 0; i < slots; ++i) lhs[i] = rhs[i] = start[job[4] + i];
    for (int side = 0; side < 2; ++side) {
        const uint32_t first = job[side * 2], count = job[side * 2 + 1];
        double* y = side ? rhs : lhs;
        for (uint32_t r = first; r < first + count; ++r) {
            double v = row_bias[r];
            for (uint32_t t = row_first[r]; t < row_first[r] + row_count[r]; ++t) v += term_k[t] * y[term_col[t]];
            y[row_slot[r]] = v;
        }
    }
    uint8_t verdict = 1;
    uint32_t at = 0;
    for (uint32_t c = job[6]; c < job[6] + job[7]; ++c) {
        const uint32_t s = cmp_slot[c];
        const int v = sg_judge(cmp_how[c], lhs[s], rhs[s]);
        if (v == 0) {
            verdict = 0, at = s;
            break;
        }
        if (v == 2 && verdict == 1) verdict = 2, at = s;
    }
    agree[j] = verdict;
    where[j] = at;
}
