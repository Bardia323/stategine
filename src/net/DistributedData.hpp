#pragma once
#include "sg/net/Exchange.hpp"

namespace sg::net::detail {
double number(const Params& p, Key key, double fallback);
std::int64_t integer(const Params& p, Key key, std::int64_t fallback);
bool flag(const Params& p, Key key, bool fallback);
Key field(const char* base, std::uint32_t dimension, std::uint32_t coordinate);
bool same_step(const Params& p, const char* prefix, const Step& step);
std::vector<double> projection(const Params& p, const char* prefix, std::uint32_t dimension);
void projection(Params& p, const char* prefix, const std::vector<double>& values);
}
