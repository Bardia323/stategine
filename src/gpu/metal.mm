// Stategine - the algebra's batches on an Apple device (Metal).
//
// Apple's GPUs have no doubles, so every value is carried as two floats - a
// float and what it leaves over - and summed and multiplied without losing
// what the second float holds (fast math off): about 48 bits, not 53. So the
// device says "the same" or "apart" only where that cannot matter, and "not
// sure" everywhere else: near the laws' tolerance, beyond the magnitudes two
// floats hold exactly, for any value a float cannot hold. Not sure is asked
// of the verifier (Compile.hpp), as apart is: nothing it says is taken on
// trust that 48 bits could get wrong.
#import <Metal/Metal.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "sg/gpu/AlgebraBackend.hpp"

namespace sg::gpu::metal {

namespace {

const char* kSource = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct df { float hi; float lo; };

static df quick(float a, float b) { float s = a + b; return df{s, b - (s - a)}; }
static df two_sum(float a, float b) { float s = a + b; float v = s - a; return df{s, (a - (s - v)) + (b - v)}; }
static df add(df a, df b) { df s = two_sum(a.hi, b.hi); return quick(s.hi, s.lo + a.lo + b.lo); }
static df mul(df a, df b) { float p = a.hi * b.hi; float e = fma(a.hi, b.hi, -p); e += a.hi * b.lo + a.lo * b.hi; return quick(p, e); }
static df neg(df a) { return df{-a.hi, -a.lo}; }
static float mag(df a) { return fabs(a.hi + a.lo); }

// 1 the same, 0 apart, 2 not sure (see sg::algebra::judge, and above).
static uint judge(uint how, df a, df b) {
    if (!isfinite(a.hi) || !isfinite(b.hi)) return 2;
    const float big = fmax(mag(a), mag(b));
    if (how == 0u) {
        if (big >= 140737488355328.0f) return 2;  // 2^47: past what two floats hold exactly
        return (a.hi == b.hi && a.lo == b.lo) ? 1u : 0u;
    }
    if (big > 1.0e6f) return 2;
    df d = add(a, neg(b));
    if (how == 2u) {
        const df turn = df{6.2831855f, -1.7484555e-07f};
        const float k = rint((d.hi + d.lo) / 6.2831855f);
        d = add(d, neg(mul(df{k, 0.0f}, turn)));
    }
    const float x = mag(d);
    const float err = 1.0e-12f * (1.0f + big);
    if (x + err < 1.0e-6f * 0.999f) return 1u;
    if (x - err > 1.0e-6f * 1.001f) return 0u;
    return 2u;
}

kernel void run_jobs(device const uint* jobs [[buffer(0)]], device const uint* row_slot [[buffer(1)]],
                     device const uint* row_first [[buffer(2)]], device const uint* row_count [[buffer(3)]],
                     device const df* row_bias [[buffer(4)]], device const uint* term_col [[buffer(5)]],
                     device const df* term_k [[buffer(6)]], device const df* start [[buffer(7)]],
                     device const uint* cmp_slot [[buffer(8)]], device const uint* cmp_how [[buffer(9)]],
                     device const uint* scratch_first [[buffer(10)]], device df* scratch [[buffer(11)]],
                     device uint* agree [[buffer(12)]], device uint* where_ [[buffer(13)]],
                     constant uint& n [[buffer(14)]], uint j [[thread_position_in_grid]]) {
    if (j >= n) return;
    const uint slots = jobs[8 * j + 5];
    const uint l = scratch_first[j], r = l + slots;
    for (uint i = 0; i < slots; ++i) scratch[l + i] = scratch[r + i] = start[jobs[8 * j + 4] + i];
    for (uint side = 0; side < 2; ++side) {
        const uint first = jobs[8 * j + side * 2], count = jobs[8 * j + side * 2 + 1];
        const uint y = side == 0 ? l : r;
        for (uint k = first; k < first + count; ++k) {
            df v = row_bias[k];
            for (uint t = row_first[k]; t < row_first[k] + row_count[k]; ++t) v = add(v, mul(term_k[t], scratch[y + term_col[t]]));
            scratch[y + row_slot[k]] = v;
        }
    }
    uint verdict = 1, at = 0;
    for (uint c = jobs[8 * j + 6]; c < jobs[8 * j + 6] + jobs[8 * j + 7]; ++c) {
        const uint s = cmp_slot[c];
        const uint v = judge(cmp_how[c], scratch[l + s], scratch[r + s]);
        if (v == 0u) { verdict = 0; at = s; break; }
        if (v == 2u && verdict == 1u) { verdict = 2; at = s; }
    }
    agree[j] = verdict;
    where_[j] = at;
}
)MSL";

struct DF {
    float hi, lo;
};
DF split(double x) {
    const float hi = static_cast<float>(x);
    return DF{hi, static_cast<float>(x - static_cast<double>(hi))};
}

struct Device {
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> queue = nil;
    id<MTLComputePipelineState> pipeline = nil;
    bool ok = false;
    Device() {
        @autoreleasepool {
            device = MTLCreateSystemDefaultDevice();
            if (!device) return;
            MTLCompileOptions* options = [MTLCompileOptions new];
            options.fastMathEnabled = NO;  // two floats are exact only without it
            NSError* error = nil;
            id<MTLLibrary> lib = [device newLibraryWithSource:[NSString stringWithUTF8String:kSource] options:options error:&error];
            if (!lib) return;
            id<MTLFunction> fn = [lib newFunctionWithName:@"run_jobs"];
            if (!fn) return;
            pipeline = [device newComputePipelineStateWithFunction:fn error:&error];
            if (!pipeline) return;
            queue = [device newCommandQueue];
            ok = queue != nil;
        }
    }
};

Device& device() {
    static Device d;
    return d;
}

template <typename T>
id<MTLBuffer> buffer(id<MTLDevice> dev, const std::vector<T>& v) {
    const std::size_t bytes = std::max<std::size_t>(16, v.size() * sizeof(T));
    id<MTLBuffer> b = [dev newBufferWithLength:bytes options:MTLResourceStorageModeShared];
    if (b && !v.empty()) std::memcpy([b contents], v.data(), v.size() * sizeof(T));
    return b;
}
std::vector<DF> split_all(const std::vector<double>& v) {
    std::vector<DF> out(v.size());
    for (std::size_t i = 0; i < v.size(); ++i) out[i] = split(v[i]);
    return out;
}

}  // namespace

bool available() { return device().ok; }

bool run(const algebra::Batch& b, std::vector<algebra::Verdict>& out) {
    Device& d = device();
    if (!d.ok) return false;
    @autoreleasepool {
        const uint32_t n = static_cast<uint32_t>(b.size());
        std::vector<uint32_t> jobs, scratch_first, how(b.cmp_how.begin(), b.cmp_how.end());
        uint64_t scratch = 0;
        for (const algebra::Batch::Job& j : b.jobs) {
            const uint32_t f[8] = {j.lhs_first, j.lhs_count, j.rhs_first, j.rhs_count, j.x_first, j.slots, j.cmp_first, j.cmp_count};
            jobs.insert(jobs.end(), f, f + 8);
            scratch_first.push_back(static_cast<uint32_t>(scratch));
            scratch += 2ull * j.slots;
        }
        if (scratch > 0xffffffffull) return false;
        id<MTLBuffer> bufs[15] = {
            buffer(d.device, jobs),
            buffer(d.device, b.row_slot),
            buffer(d.device, b.row_first),
            buffer(d.device, b.row_count),
            buffer(d.device, split_all(b.row_bias)),
            buffer(d.device, b.term_col),
            buffer(d.device, split_all(b.term_k)),
            buffer(d.device, split_all(b.start)),
            buffer(d.device, b.cmp_slot),
            buffer(d.device, how),
            buffer(d.device, scratch_first),
            buffer(d.device, std::vector<DF>(static_cast<std::size_t>(scratch))),
            buffer(d.device, std::vector<uint32_t>(n, 1)),
            buffer(d.device, std::vector<uint32_t>(n, 0)),
            buffer(d.device, std::vector<uint32_t>{n}),
        };
        for (id<MTLBuffer> x : bufs)
            if (!x) return false;
        id<MTLCommandBuffer> cmd = [d.queue commandBuffer];
        id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
        [enc setComputePipelineState:d.pipeline];
        for (NSUInteger i = 0; i < 15; ++i) [enc setBuffer:bufs[i] offset:0 atIndex:i];
        const NSUInteger width = std::min<NSUInteger>(64, d.pipeline.maxTotalThreadsPerThreadgroup);
        [enc dispatchThreads:MTLSizeMake(n, 1, 1) threadsPerThreadgroup:MTLSizeMake(width, 1, 1)];
        [enc endEncoding];
        [cmd commit];
        [cmd waitUntilCompleted];
        if (cmd.status != MTLCommandBufferStatusCompleted) return false;
        const uint32_t* agree = static_cast<const uint32_t*>([bufs[12] contents]);
        const uint32_t* where = static_cast<const uint32_t*>([bufs[13] contents]);
        out.resize(n);
        for (uint32_t i = 0; i < n; ++i) out[i] = algebra::Verdict{static_cast<uint8_t>(agree[i]), where[i]};
        return true;
    }
}

}  // namespace sg::gpu::metal
