// A name and a route for computation, never authority over a state.
#pragma once
#include <cstdint>
#include <string>

namespace sg::net {
struct Peer {
    std::string id, endpoint;
};
struct Step {
    std::int64_t epoch = 0, generation = 0, tick = 0;
};
} // namespace sg::net
