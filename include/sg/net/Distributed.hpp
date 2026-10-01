// Repeated local numerical work; its progress is kept by the owning state.
#pragma once
#include "sg/net/Exchange.hpp"
#include "sg/net/Reconcile.hpp"

namespace sg::net {
struct DistributedResult {
    Result section;
    Params params;
    std::vector<BoundaryChange> boundaries;
    std::vector<Packet> outgoing;
    bool advanced = false;
};
struct DistributedStats {
    std::uint64_t plans = 0, certificates = 0, layout_validations = 0, workspace_growths = 0;
};
class Distributed {
public:
    Distributed();
    ~Distributed();
    Distributed(const Distributed& other);
    Distributed& operator=(const Distributed& other);
    DistributedResult evaluate(const State& state, const Backend* backend = nullptr);
    void evaluate(const State& state, DistributedResult& out, const Backend* backend = nullptr);
    std::vector<BoundaryChange> receive(const State& state, const Packet& packet);
    const Partition& partition() const { return partition_; }
    const Cellular& cellular() const { return cellular_; }
    DistributedStats diagnostics() const;
private:
    Cellular cellular_;
    Partition partition_;
    struct Workspace;
    std::unique_ptr<Workspace> workspace_;
    void prepare(const State& state);
};
} // namespace sg::net
