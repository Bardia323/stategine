// Stategine - the algebra's batches, on a GPU.
//
// Outside the core, and outside the algebra: a backend here runs a batch of
// compiled equations (sg/algebra/Program.hpp) on a device, one job to a
// thread, each doing exactly what `run_job` does. Which devices there are is
// decided when the engine is built: each backend's code is compiled in only
// where its toolchain is found (CMake: SG_WITH_CUDA, SG_WITH_ROCM,
// SG_WITH_VULKAN, SG_WITH_METAL, and the `stategine::gpu` target), and asked
// at run time whether its device is there.
//
//   CudaBackend    NVIDIA, CUDA            doubles
//   RocmBackend    AMD, HIP                doubles (the CUDA kernel, as HIP)
//   VulkanBackend  any Vulkan 1.1 device   doubles, where the device has them (shaderFloat64)
//   MetalBackend   Apple                   no doubles on the device: sums in two floats, and
//                                          any job it cannot be sure of is said unsure
//
// A backend that is not built in, or finds no device, says so (`available`)
// and runs its batch on the CPU instead: its answers are always right, only
// not always fast. Whatever a device says is only ever "these agree", "these
// part" or "not sure", and the last two go to the verifier (Compile.hpp):
// the laws' report is the verifier's, whatever ran the batch.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "sg/algebra/Backend.hpp"

namespace sg::gpu {

// Each device's code, where it is built in (src/gpu/*): whether a device is
// there, and running a batch on it (false: it could not).
#ifdef SG_WITH_CUDA
namespace cuda {
bool available();
bool run(const algebra::Batch& b, std::vector<algebra::Verdict>& out);
}  // namespace cuda
#endif
#ifdef SG_WITH_ROCM
namespace rocm {
bool available();
bool run(const algebra::Batch& b, std::vector<algebra::Verdict>& out);
}  // namespace rocm
#endif
#ifdef SG_WITH_VULKAN
namespace vulkan {
bool available();
bool run(const algebra::Batch& b, std::vector<algebra::Verdict>& out);
}  // namespace vulkan
#endif
#ifdef SG_WITH_METAL
namespace metal {
bool available();
bool run(const algebra::Batch& b, std::vector<algebra::Verdict>& out);
}  // namespace metal
#endif

// A device backend: its device when it is there, the CPU when it is not.
template <typename Device>
class DeviceBackend : public algebra::Backend {
public:
    std::string name() const override { return Device::name(); }
    bool available() const override { return Device::available(); }
    std::vector<algebra::Verdict> run(const algebra::Batch& b) override {
        std::vector<algebra::Verdict> out;
        if (!b.empty() && Device::available() && Device::run(b, out) && out.size() == b.size()) return out;
        return cpu_.run(b);
    }

private:
    algebra::CpuBackend cpu_;
};

struct Cuda {
    static std::string name() { return "cuda"; }
#ifdef SG_WITH_CUDA
    static bool available() { return cuda::available(); }
    static bool run(const algebra::Batch& b, std::vector<algebra::Verdict>& o) { return cuda::run(b, o); }
#else
    static bool available() { return false; }
    static bool run(const algebra::Batch&, std::vector<algebra::Verdict>&) { return false; }
#endif
};
struct Rocm {
    static std::string name() { return "rocm"; }
#ifdef SG_WITH_ROCM
    static bool available() { return rocm::available(); }
    static bool run(const algebra::Batch& b, std::vector<algebra::Verdict>& o) { return rocm::run(b, o); }
#else
    static bool available() { return false; }
    static bool run(const algebra::Batch&, std::vector<algebra::Verdict>&) { return false; }
#endif
};
struct Vulkan {
    static std::string name() { return "vulkan"; }
#ifdef SG_WITH_VULKAN
    static bool available() { return vulkan::available(); }
    static bool run(const algebra::Batch& b, std::vector<algebra::Verdict>& o) { return vulkan::run(b, o); }
#else
    static bool available() { return false; }
    static bool run(const algebra::Batch&, std::vector<algebra::Verdict>&) { return false; }
#endif
};
struct Metal {
    static std::string name() { return "metal"; }
#ifdef SG_WITH_METAL
    static bool available() { return metal::available(); }
    static bool run(const algebra::Batch& b, std::vector<algebra::Verdict>& o) { return metal::run(b, o); }
#else
    static bool available() { return false; }
    static bool run(const algebra::Batch&, std::vector<algebra::Verdict>&) { return false; }
#endif
};

using CudaBackend = DeviceBackend<Cuda>;
using RocmBackend = DeviceBackend<Rocm>;
using VulkanBackend = DeviceBackend<Vulkan>;
using MetalBackend = DeviceBackend<Metal>;

// Every backend, the CPU's first. inline: which are built in is decided
// where this is included (SG_WITH_*).
inline std::vector<std::unique_ptr<algebra::Backend>> backends() {
    std::vector<std::unique_ptr<algebra::Backend>> out;
    out.push_back(std::make_unique<algebra::CpuBackend>());
    out.push_back(std::make_unique<CudaBackend>());
    out.push_back(std::make_unique<RocmBackend>());
    out.push_back(std::make_unique<VulkanBackend>());
    out.push_back(std::make_unique<MetalBackend>());
    return out;
}

// The first device that is here - CUDA, ROCm, Vulkan, Metal - or the CPU.
// inline: as backends().
inline std::unique_ptr<algebra::Backend> best() {
    std::vector<std::unique_ptr<algebra::Backend>> all = backends();
    for (std::size_t i = 1; i < all.size(); ++i)
        if (all[i]->available()) return std::move(all[i]);
    return std::move(all[0]);
}

}  // namespace sg::gpu
