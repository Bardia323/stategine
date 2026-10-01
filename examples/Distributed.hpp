// Native computations for the DSL example; no graph construction here.
#pragma once
#include "sg/net/Distributed.hpp"
#include "sg/net/Placement.hpp"
#include "sg/dsl/Natives.hpp"

namespace sg::examples {
sg::dsl::Natives distributed_natives(const net::Backend* backend, std::shared_ptr<net::Distributed> solver);
Params placement_arguments(const net::PlacementPlan& plan, Params handoff = {});
}
