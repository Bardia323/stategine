// Stategine - running a batch on a CUDA or HIP device.
//
// Included by cuda.cu and rocm.hip, with SG_RT(x) naming the runtime's own
// calls (cudaMalloc / hipMalloc ...) and SG_NS the namespace of the backend.
#include <vector>

#include "sg/gpu/AlgebraBackend.hpp"

namespace sg::gpu::SG_NS {

namespace {
struct Buffers {
    std::vector<void*> held;
    ~Buffers() {
        for (void* p : held) SG_RT(Free)(p);
    }
    template <typename T>
    T* up(const std::vector<T>& v) {
        void* p = nullptr;
        const std::size_t bytes = std::max<std::size_t>(1, v.size()) * sizeof(T);
        if (SG_RT(Malloc)(&p, bytes) != SG_RT(Success)) return nullptr;
        held.push_back(p);
        if (!v.empty() && SG_RT(Memcpy)(p, v.data(), v.size() * sizeof(T), SG_RT(MemcpyHostToDevice)) != SG_RT(Success)) return nullptr;
        return static_cast<T*>(p);
    }
};
}  // namespace

bool available() {
    int n = 0;
    return SG_RT(GetDeviceCount)(&n) == SG_RT(Success) && n > 0;
}

bool run(const algebra::Batch& b, std::vector<algebra::Verdict>& out) {
    const uint32_t n = static_cast<uint32_t>(b.size());
    std::vector<uint32_t> jobs, scratch_first;
    uint64_t scratch = 0;
    for (const algebra::Batch::Job& j : b.jobs) {
        const uint32_t f[8] = {j.lhs_first, j.lhs_count, j.rhs_first, j.rhs_count, j.x_first, j.slots, j.cmp_first, j.cmp_count};
        jobs.insert(jobs.end(), f, f + 8);
        scratch_first.push_back(static_cast<uint32_t>(scratch));
        scratch += 2ull * j.slots;
    }
    if (scratch > 0xffffffffull) return false;
    Buffers d;
    const uint32_t* dj = d.up(jobs);
    const uint32_t* rs = d.up(b.row_slot);
    const uint32_t* rf = d.up(b.row_first);
    const uint32_t* rc = d.up(b.row_count);
    const double* rb = d.up(b.row_bias);
    const uint32_t* tc = d.up(b.term_col);
    const double* tk = d.up(b.term_k);
    const double* st = d.up(b.start);
    const uint32_t* cs = d.up(b.cmp_slot);
    const uint8_t* ch = d.up(b.cmp_how);
    const uint32_t* sf = d.up(scratch_first);
    double* sc = d.up(std::vector<double>(static_cast<std::size_t>(scratch)));
    std::vector<uint8_t> agree(n, 1);
    std::vector<uint32_t> where(n, 0);
    uint8_t* da = d.up(agree);
    uint32_t* dw = d.up(where);
    if (!dj || !rs || !rf || !rc || !rb || !tc || !tk || !st || !cs || !ch || !sf || !sc || !da || !dw) return false;
    const uint32_t threads = 128, blocks = (n + threads - 1) / threads;
    sg_run_jobs<<<blocks, threads>>>(n, dj, rs, rf, rc, rb, tc, tk, st, cs, ch, sf, sc, da, dw);
    if (SG_RT(DeviceSynchronize)() != SG_RT(Success)) return false;
    if (SG_RT(Memcpy)(agree.data(), da, n, SG_RT(MemcpyDeviceToHost)) != SG_RT(Success)) return false;
    if (SG_RT(Memcpy)(where.data(), dw, n * sizeof(uint32_t), SG_RT(MemcpyDeviceToHost)) != SG_RT(Success)) return false;
    out.resize(n);
    for (uint32_t i = 0; i < n; ++i) out[i] = algebra::Verdict{agree[i], where[i]};
    return true;
}

}  // namespace sg::gpu::SG_NS
