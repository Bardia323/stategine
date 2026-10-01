// Pure execution planning over a verified finalized view of the declared world.
#pragma once
#include "sg/net/Cellular.hpp"
#include "sg/net/Integrity.hpp"
#include "sg/net/Peer.hpp"
#include <functional>
#include <map>

namespace sg::net {
struct FinalizedPlacementView {
    const State& state; // caller binds this read-only view to the verified checkpoint
    Digest receipt;
    std::uint64_t epoch = 0, generation = 0, last_placement_epoch = 0;
};
struct PeerCapacity { Peer peer; double capacity = 1; bool available = true; };
struct StalkCost { Key stalk; double execution = 1, migration = 1; };
struct EdgeCost { Key overlap; double traffic = 1; };
struct PlacementMetrics {
    std::vector<PeerCapacity> peers;
    std::vector<StalkCost> stalks;
    std::vector<EdgeCost> overlaps;
    double cut_penalty = 0.1, migration_penalty = 0.05, minimum_improvement = 0.05;
    std::uint64_t hold_epochs = 3;
};
enum class InterestAction { Keep, Add, Remove };
struct InterestChange {
    InterestAction action = InterestAction::Keep;
    Key overlap;
    Params constraint; // ordinary declared constraint params, applied only by an edit
    double traffic_cost = 1;
};
struct ParticipantView { Key stalk; const Params* finalized; };
using InterestPolicy = std::function<std::vector<InterestChange>(const std::vector<ParticipantView>&, const std::vector<Overlap>&)>;
struct Assignment { Key stalk; std::string peer; };
struct Migration { Key stalk; std::string from, to; };
struct PlacementPlan {
    std::vector<Assignment> assignments;
    std::vector<Migration> migrations;
    std::vector<InterestChange> interest;
    std::map<std::string,double> estimated_load;
    std::uint64_t generation = 0;
    double cut_cost = 0, objective = 0;
    bool feasible = false, changed = false;
};
class Placement {
public:
    static PlacementPlan plan(const FinalizedPlacementView& finalized, const Cellular& cellular,
                              const PlacementMetrics& metrics, const InterestPolicy& interest = {});
};
}
