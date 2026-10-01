// Execution machinery consumes numerical buffers, never a semantic world.
#pragma once
#include "sg/net/LinearSystem.hpp"
#include <memory>

namespace sg::net {
// Borrowed validated buffers and their already derived step. No solver model.
class PreparedSystem {
public:
    const LinearSystem& system() const { return *system_; }
    double step() const { return step_; }
private:
    PreparedSystem(const LinearSystem& system, double step);
    const LinearSystem* system_;
    double step_;
    friend class Backend;
    friend class Distributed;
    friend class Verify;
    friend class Reconcile;
    friend Residual measure(const LinearSystem&, const std::vector<double>&);
};
struct SolveWorkspace {
    std::vector<double> edge, ax, delta;
    std::uint64_t growths = 0;
    template<class T> void resize(std::vector<T>& buffer, std::size_t size) {
        if (size > buffer.capacity()) ++growths;
        buffer.resize(size);
    }
};
Residual measure(const PreparedSystem& system, const std::vector<double>& x, SolveWorkspace& workspace);
class Backend {
public:
    virtual ~Backend() = default;
    virtual bool available() const = 0;
    virtual std::vector<double> solve(const LinearSystem& system) const = 0;
    virtual Residual residual(const LinearSystem& system, const std::vector<double>& x) const = 0;
    virtual void solve_prepared(const PreparedSystem& system, SolveWorkspace& workspace, std::vector<double>& out) const;
    virtual Residual residual_prepared(const PreparedSystem& system, const std::vector<double>& x, SolveWorkspace& workspace) const;
protected:
    static PreparedSystem prepare(const LinearSystem& system);
};
class CpuBackend final : public Backend {
public:
    bool available() const override { return true; }
    std::vector<double> solve(const LinearSystem& system) const override;
    Residual residual(const LinearSystem& system, const std::vector<double>& x) const override;
    void solve_prepared(const PreparedSystem& system, SolveWorkspace& workspace, std::vector<double>& out) const override;
    Residual residual_prepared(const PreparedSystem& system, const std::vector<double>& x, SolveWorkspace& workspace) const override;
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
    void solve_prepared(const PreparedSystem& system, SolveWorkspace& workspace, std::vector<double>& out) const override;
    Residual residual_prepared(const PreparedSystem& system, const std::vector<double>& x, SolveWorkspace& workspace) const override;
    TransferStats transfers() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace sg::net
