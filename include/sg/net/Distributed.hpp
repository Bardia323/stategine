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
class Distributed {
public:
    DistributedResult evaluate(const State& state, const Backend* backend = nullptr);
    std::vector<BoundaryChange> receive(const State& state, const Packet& packet);
    const Partition& partition() const { return partition_; }
    const Cellular& cellular() const { return cellular_; }
private:
    Cellular cellular_;
    Partition partition_;
};
} // namespace sg::net
