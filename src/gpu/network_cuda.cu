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
// Small initial compactor: bounded O(n), deterministic coordinate order.
// Only changed solved values cross back to the host; this can later become
// a parallel scan without changing any numerical or semantic interface.
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
