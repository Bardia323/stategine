// Native computations for the DSL example; no graph construction here.
#pragma once
#include "sg/net/Distributed.hpp"
#include "sg/dsl/Natives.hpp"

namespace sg::examples {
sg::dsl::Natives distributed_natives(const net::Backend* backend, std::shared_ptr<net::Distributed> solver);
}
