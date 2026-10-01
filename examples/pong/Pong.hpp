// Pong's computations. The world and every interface are declared in pong.sg.
#pragma once
#include "sg/dsl/Natives.hpp"
#include "sg/net/Distributed.hpp"

namespace sg::examples::pong {
dsl::Natives natives(const net::Backend* backend, std::shared_ptr<net::Distributed> solver);
Params configuration(int player);
Params scripted_input(const State& game, int player);
}
