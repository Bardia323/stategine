// Matrix-free deterministic kernels. Every row/column has one owner; there
// are no atomic sums and no device copy of a semantic world.
extern "C" __global__ void network_initial(unsigned n, const double* y, const unsigned char* fixed,
                                           const double* pins, double* x) {
    const unsigned i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) x[i] = fixed[i] ? pins[i] : y[i];
}
extern "C" __global__ void network_delta(unsigned m, const unsigned* offsets, const unsigned* columns,
                                         const double* restrictions, const double* x, const double* weight, double* edge) {
    const unsigned r = blockIdx.x * blockDim.x + threadIdx.x;
    if (r >= m) return;
    if (weight[r] == 0) { edge[r] = 0; return; }
    double value = 0;
    for (unsigned j = offsets[r]; j < offsets[r + 1]; ++j) value += restrictions[j] * x[columns[j]];
    edge[r] = weight[r] * value;
}
extern "C" __global__ void network_step(unsigned n, const unsigned* offsets, const unsigned* rows,
                                        const unsigned* entries, const double* restrictions, const double* edge,
                                        const double* confidence, const double* y, const unsigned char* fixed,
                                        const double* x, double* next, double lambda, double alpha) {
    const unsigned c = blockIdx.x * blockDim.x + threadIdx.x;
    if (c >= n) return;
    if (fixed[c]) { next[c] = x[c]; return; }
    double value = 0;
    if (lambda != 0)
        for (unsigned j = offsets[c]; j < offsets[c + 1]; ++j) value += restrictions[entries[j]] * edge[rows[j]];
    const double ax = confidence[c] * x[c] + lambda * value;
    next[c] = x[c] - alpha * (ax - confidence[c] * y[c]);
}
// The small-n path avoids scan launches. Both paths produce ascending indices.
extern "C" __global__ void network_changes(unsigned n, const double* x, double* previous, unsigned valid,
                                            unsigned* count, unsigned* indices, double* values) {
    if (blockIdx.x || threadIdx.x) return;
    unsigned size = 0;
    for (unsigned c = 0; c < n; ++c) {
        if (!valid || x[c] != previous[c]) { indices[size] = c; values[size++] = x[c]; }
        previous[c] = x[c];
    }
    *count = size;
}
// Integer scans are exact and scheduling independent. Every block has 128 lanes.
__device__ unsigned scan_block(unsigned value, unsigned* scratch) {
    const unsigned lane = threadIdx.x;
    scratch[lane] = value; __syncthreads();
    for (unsigned stride = 1; stride < 128; stride *= 2) {
        const unsigned add = lane >= stride ? scratch[lane-stride] : 0;
        __syncthreads(); scratch[lane] += add; __syncthreads();
    }
    return scratch[lane]-value;
}
extern "C" __global__ void network_change_flags(unsigned n,const double* x,const double* previous,unsigned valid,
                                                 unsigned* offsets,unsigned* totals) {
    __shared__ unsigned scratch[128];
    const unsigned c = blockIdx.x*128+threadIdx.x;
    const unsigned flag = c < n && (!valid || x[c] != previous[c]);
    const unsigned offset = scan_block(flag,scratch);
    if (c < n) offsets[c] = offset;
    if (threadIdx.x == 127) totals[blockIdx.x] = scratch[127];
}
extern "C" __global__ void network_scan(unsigned n,const unsigned* input,unsigned* offsets,unsigned* totals) {
    __shared__ unsigned scratch[128];
    const unsigned c = blockIdx.x*128+threadIdx.x;
    const unsigned offset = scan_block(c < n ? input[c] : 0,scratch);
    if (c < n) offsets[c] = offset;
    if (threadIdx.x == 127) totals[blockIdx.x] = scratch[127];
}
extern "C" __global__ void network_scan_add(unsigned n,unsigned* offsets,const unsigned* parent) {
    const unsigned c = blockIdx.x*128+threadIdx.x;
    if (c < n) offsets[c] += parent[blockIdx.x];
}
extern "C" __global__ void network_change_scatter(unsigned n,const double* x,double* previous,unsigned valid,
                                                   const unsigned* offsets,const unsigned* blocks,unsigned* indices,double* values) {
    const unsigned c = blockIdx.x*128+threadIdx.x;
    if (c >= n) return;
    if (!valid || x[c] != previous[c]) {
        const unsigned destination = blocks[blockIdx.x]+offsets[c];
        indices[destination] = c; values[destination] = x[c];
    }
    previous[c] = x[c];
}
