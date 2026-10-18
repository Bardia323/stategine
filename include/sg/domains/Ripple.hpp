// Stategine - Ripple: rings spreading on water from where something met it.
//
// A stone dropped, a foot set down in a stream, a bow pushing through a lake
// each half metre: a ring of crests leaves the place at a walking pace and
// dies as it spreads. A ripple is kept by the water it is on (any thing
// drawn as water, `surface` 18), as data in its params - where it began (x,
// z, the frame the water is in), when (`now`: its world's time, the time its
// waves move with - semantic_time), how strong (0..1) - the last kRipples of
// them, the oldest given up for the newest. Nothing moves it on: what it is
// at any time is a function of that time, so the water at a world time is
// the same however it was come to, and a world paused holds its rings where
// they are. A renderer draws each, where its world's time says it has
// spread to, until it has died away (6 s).
#pragma once

#include "sg/core/Core.hpp"

namespace sg {

constexpr int kRipples = 16;

// A ripple on `water`, begun at (x, z) at `now`, as strong as `strength`.
void ripple(Element& water, double x, double z, double strength, double now);

}  // namespace sg
