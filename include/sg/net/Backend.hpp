// Execution machinery consumes numerical buffers, never a semantic world.
#pragma once
#include "sg/net/LinearSystem.hpp"
#include <memory>

namespace sg::net {
class Backend {
public:
    virtual ~Backend() = default;
    virtual bool available() const = 0;
    virtual std::vector<double> solve(const LinearSystem& system) const = 0;
    virtual Residual residual(const LinearSystem& system, const std::vector<double>& x) const = 0;
};
class CpuBackend final : public Backend {
public:
    bool available() const override { return true; }
    std::vector<double> solve(const LinearSystem& system) const override;
    Residual residual(const LinearSystem& system, const std::vector<double>& x) const override;
};
// Execution diagnostics only. None of these counters influences a solution.
struct TransferStats {
    std::uint64_t topology_uploads = 0;
    std::uint64_t observation_uploads = 0, weight_uploads = 0, pin_uploads = 0;
    std::uint64_t downloaded_values = 0;
};
// Resident buffers are disposable caches; every solve starts from supplied data.
class CudaBackend final : public Backend {
public:
    CudaBackend();
    ~CudaBackend() override;
    CudaBackend(const CudaBackend&) = delete;
    CudaBackend& operator=(const CudaBackend&) = delete;
    bool available() const override;
    std::vector<double> solve(const LinearSystem& system) const override;
    Residual residual(const LinearSystem& system, const std::vector<double>& x) const override;
    TransferStats transfers() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace sg::net
