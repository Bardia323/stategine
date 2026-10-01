// Host execution machinery uses the CUDA C driver API. The device module is
// PTX, so no C++ ABI is shared with nvcc's host compiler.
#include "sg/net/Backend.hpp"
#include "network_ptx.h"
#include <cuda.h>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <stdexcept>
#include <string>
#include <tuple>

namespace sg::net {
namespace {
void cuda(CUresult result) {
    if (result == CUDA_SUCCESS) return;
    const char* name = nullptr;
    cuGetErrorName(result, &name);
    throw std::runtime_error(std::string("net CUDA: ") + (name ? name : "driver error"));
}
class Context {
public:
    explicit Context(CUcontext context) { cuda(cuCtxPushCurrent(context)); }
    ~Context() { CUcontext previous; cuCtxPopCurrent(&previous); }
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
};
template<class T> struct Buffer {
    CUdeviceptr ptr = 0;
    std::size_t size = 0;
    std::vector<T> cached;
    ~Buffer() { release(); }
    void release() {
        if (ptr) cuMemFree(ptr);
        ptr = 0; size = 0; cached.clear();
    }
    void reserve(std::size_t n) {
        if (ptr && size == n) return;
        release();
        cuda(cuMemAlloc(&ptr, std::max<std::size_t>(1, n) * sizeof(T)));
        size = n;
    }
    void upload(const std::vector<T>& values) {
        reserve(values.size());
        if (!values.empty()) cuda(cuMemcpyHtoD(ptr, values.data(), values.size() * sizeof(T)));
    }
    std::uint64_t changes(const std::vector<T>& values) {
        reserve(values.size());
        std::uint64_t count = 0;
        if (cached.size() != values.size()) {
            if (!values.empty()) cuda(cuMemcpyHtoD(ptr, values.data(), values.size() * sizeof(T)));
            count = values.size();
        } else {
            // Coalesce adjacent changed values; unchanged cells stay resident.
            for (std::size_t i = 0; i < values.size();) {
                if (values[i] == cached[i]) { ++i; continue; }
                const auto first = i++;
                while (i < values.size() && values[i] != cached[i]) ++i;
                cuda(cuMemcpyHtoD(ptr + first * sizeof(T), values.data() + first, (i - first) * sizeof(T)));
                count += i - first;
            }
        }
        cached = values;
        return count;
    }
};
void launch(CUfunction function, std::uint32_t work, void** args) {
    if (!work) return;
    cuda(cuLaunchKernel(function, (work - 1) / 128 + 1, 1, 1, 128, 1, 1, 0, nullptr, args, nullptr));
}
bool device(CUdevice& selected) {
    int count = 0;
    if (cuInit(0) != CUDA_SUCCESS || cuDeviceGetCount(&count) != CUDA_SUCCESS) return false;
    for (int i = 0; i < count; ++i) {
        CUdevice candidate; int major = 0;
        if (cuDeviceGet(&candidate, i) == CUDA_SUCCESS &&
            cuDeviceGetAttribute(&major, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, candidate) == CUDA_SUCCESS && major >= 6) {
            selected = candidate;
            return true;
        }
    }
    return false;
}
}
struct CudaBackend::Impl {
    mutable std::mutex mutex;
    CUcontext context = nullptr;
    CUmodule module = nullptr;
    CUfunction initial = nullptr, delta = nullptr, step = nullptr, changes = nullptr;
    Buffer<std::uint32_t> row_offsets, columns, column_offsets, rows, entries;
    Buffer<double> restrictions, observations, confidence, weights, pins;
    Buffer<std::uint8_t> fixed;
    Buffer<double> x, next, edge, previous, changed_values;
    Buffer<std::uint32_t> changed_count, changed_indices;
    Layout layout;
    bool topology_valid = false, result_valid = false;
    std::vector<double> result;
    TransferStats stats;

    ~Impl() {
        if (!context) return;
        // All buffers are released while their context is current. Destructors
        // subsequently see empty buffers and perform no driver operations.
        if (cuCtxPushCurrent(context) == CUDA_SUCCESS) {
            std::apply([](auto&... buffer) { (buffer.release(), ...); },
                std::tie(row_offsets, columns, column_offsets, rows, entries, restrictions, observations, confidence, weights,
                         pins, fixed, x, next, edge, previous, changed_values, changed_count, changed_indices));
            if (module) cuModuleUnload(module);
            CUcontext popped; cuCtxPopCurrent(&popped);
        }
        cuCtxDestroy(context);
    }
    void initialize() {
        if (initial && delta && step && changes) return;
        if (!context) {
            CUdevice selected;
            if (!device(selected)) throw std::runtime_error("net CUDA: no device supports the embedded PTX");
            cuda(cuCtxCreate(&context, 0, selected));
            CUcontext popped; cuda(cuCtxPopCurrent(&popped));
        }
        Context current(context);
        if (!module) cuda(cuModuleLoadData(&module, sg_network_ptx));
        cuda(cuModuleGetFunction(&initial, module, "network_initial"));
        cuda(cuModuleGetFunction(&delta, module, "network_delta"));
        cuda(cuModuleGetFunction(&step, module, "network_step"));
        cuda(cuModuleGetFunction(&changes, module, "network_changes"));
    }
    void topology(const Layout& new_layout) {
        if (topology_valid && new_layout == layout) return;
        topology_valid = false; result_valid = false;
        row_offsets.upload(new_layout.row_offsets); columns.upload(new_layout.columns);
        column_offsets.upload(new_layout.column_offsets); rows.upload(new_layout.rows); entries.upload(new_layout.transpose_entries);
        restrictions.upload(new_layout.restrictions);
        const auto n = new_layout.stalk_offsets.back(), m = new_layout.overlap_offsets.back();
        x.reserve(n); next.reserve(n); previous.reserve(n); edge.reserve(m);
        changed_count.reserve(1); changed_indices.reserve(n); changed_values.reserve(n);
        observations.cached.clear(); confidence.cached.clear(); weights.cached.clear(); pins.cached.clear(); fixed.cached.clear();
        result.assign(n, 0);
        layout = new_layout; topology_valid = true;
        ++stats.topology_uploads;
    }
    std::vector<double> solve(const LinearSystem& s) {
        initialize();
        Context current_context(context);
        topology(s.layout);
        stats.observation_uploads += observations.changes(s.observations);
        stats.weight_uploads += confidence.changes(s.confidence) + weights.changes(s.overlap_weights);
        stats.pin_uploads += pins.changes(s.pins) + fixed.changes(s.fixed);
        auto n = static_cast<std::uint32_t>(s.observations.size()), m = static_cast<std::uint32_t>(s.overlap_weights.size());
        if (!n) return {};
        double lambda = s.lambda, alpha = step_size(s);
        CUdeviceptr current = x.ptr, following = next.ptr;
        void* initial_args[] = {&n, &observations.ptr, &fixed.ptr, &pins.ptr, &current};
        launch(initial, n, initial_args);
        void* delta_args[] = {&m, &row_offsets.ptr, &columns.ptr, &restrictions.ptr, &current, &weights.ptr, &edge.ptr};
        void* step_args[] = {&n, &column_offsets.ptr, &rows.ptr, &entries.ptr, &restrictions.ptr, &edge.ptr,
                            &confidence.ptr, &observations.ptr, &fixed.ptr, &current, &following, &lambda, &alpha};
        for (std::uint32_t iteration = 0; iteration < s.iterations; ++iteration) {
            launch(delta, m, delta_args); launch(step, n, step_args);
            std::swap(current, following);
        }
        std::uint32_t valid = result_valid ? 1 : 0;
        void* change_args[] = {&n, &current, &previous.ptr, &valid, &changed_count.ptr, &changed_indices.ptr, &changed_values.ptr};
        cuda(cuLaunchKernel(changes, 1, 1, 1, 1, 1, 1, 0, nullptr, change_args, nullptr));
        cuda(cuCtxSynchronize());
        std::uint32_t count = 0;
        cuda(cuMemcpyDtoH(&count, changed_count.ptr, sizeof(count)));
        if (count > n) throw std::runtime_error("net CUDA: invalid changed-value count");
        std::vector<std::uint32_t> indices(count);
        std::vector<double> values(count);
        if (count) {
            cuda(cuMemcpyDtoH(indices.data(), changed_indices.ptr, count * sizeof(std::uint32_t)));
            cuda(cuMemcpyDtoH(values.data(), changed_values.ptr, count * sizeof(double)));
        }
        for (std::uint32_t i = 0; i < count; ++i) {
            if (indices[i] >= n || !std::isfinite(values[i])) throw std::runtime_error("net CUDA: invalid solved coordinate");
            result[indices[i]] = values[i];
        }
        result_valid = true;
        stats.downloaded_values += count;
        return result;
    }
};
CudaBackend::CudaBackend() : impl_(std::make_unique<Impl>()) {}
CudaBackend::~CudaBackend() = default;
bool CudaBackend::available() const {
    CUdevice selected;
    return device(selected);
}
std::vector<double> CudaBackend::solve(const LinearSystem& s) const {
    validate(s);
    std::lock_guard<std::mutex> lock(impl_->mutex);
    try { return impl_->solve(s); }
    catch (...) { impl_->topology_valid = false; impl_->result_valid = false; throw; }
}
Residual CudaBackend::residual(const LinearSystem& s, const std::vector<double>& x) const { return measure(s, x); }
TransferStats CudaBackend::transfers() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->stats;
}
} // namespace sg::net
