// The Pong protocol adapter reads only its declared publication.
#pragma once
#include "Motion.hpp"
#include "sg/net/Protocol.hpp"
#include "sg/net/Cellular.hpp"
namespace sg::examples::pong {
struct Work {
    net::EpochContext context;
    std::vector<net::InputSlot> slots;
    net::Protocol::Builder build;
    net::Verify verify;
    std::vector<net::Observation> local;
};
Work work(const State& network, const net::Cellular& cellular, net::Prediction::Step rules, const net::Committee& committee,
          const net::Agreement& agreement);
net::DiscreteInputs controls(const std::vector<net::Observation>& inputs);
Params finalization(const net::Protocol& protocol);
}
