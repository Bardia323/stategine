// The game's own bounce rules, also evaluated in a disposable forecast.
#pragma once
#include "sg/net/Prediction.hpp"
#include "sg/net/Transport.hpp"
#include "sg/core/State.hpp"
namespace sg::examples::pong {
void bounce(Params& left, Params& right, Params& ball, Params& score);
net::PredictedValues frame(const State& game);
net::Prediction::Step forecast(const State& game);
net::Bytes frame_bytes(const net::PredictedValues& frame);
net::PredictedValues read_frame(const net::Bytes& bytes);
}
