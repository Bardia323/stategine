// The hash the GL view keys what it keeps by - a lamp's maps, a doorway's
// cover, a room's casters - one step at a time, shared by the files that
// keep them (World*.cpp).
#pragma once

#include <cstdint>
#include <cstring>

namespace sg::render {

inline uint64_t fnv(uint64_t h, uint64_t v) { return (h ^ v) * 1099511628211ULL; }
// inline: a number's bits, taken at every step of the keys above
inline uint64_t bits_of(double v) {
    uint64_t b = 0;
    std::memcpy(&b, &v, sizeof b);
    return b;
}

}  // namespace sg::render
