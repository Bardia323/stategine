// Stategine - Fog: where a room's air is thicker than the rest of it.
//
// A look's fog is the same everywhere in a room: as thick by the floor as
// under the ceiling, as thick in the hollow as on the hill. A fog is an
// element of a Spatial3D (kind `fog`) that says where it pools: a box, posed
// as a thing is (x, y, z its base's middle, sx sy sz, yaw), as thick as its
// `density` says (a metre's depth of it, as the look's `uFogDensity`), its
// sides and top fading over `soft` metres (its base is where it lies: on the
// ground, not faded), thinning as it rises (`fall`, how much less a metre up;
// 0 alike all through), its colour its own (`r` `g` `b`) or else its look's
// fog's. Where `drift` says (0 none, 1 the most), wisps of it move - each
// `wisp` metres across - with the time of the world it is in: what moves
// them is that world's clock, never the renderer's, so a world paused holds
// its fog where it is, and one run back takes it back.
//
// A fog that says `takes` (0..1) is no air but the dark: it takes light.
// What stands in it is lit the less, the lamps' light scattered in the air
// there is gone, and whatever is seen through it is darkened as deep as the
// way through it goes - a hole in the light, seen only against what is lit,
// black on black where nothing is. It scatters nothing and glows with
// nothing; its colour is none.
// It breathes where it says `breath` (0..0.5): its edge drawn in and let
// out by so much of its width, a slow swell climbing it, `breath_rate`
// (radians a second of its world's time) how fast. Where nothing of it is
// thick (`density` 0) it is not drawn at all: a dark elsewhere costs nothing.
//
// A renderer reads it as data: thicker fog along each way from the eye where
// the way crosses it, and more of a lamp's light scattered in the air there.
//
// The laws of a fog (fog_faults, a Spatial3D's faults): its numbers are
// numbers, its box has a size, it is no thinner than none, its drift is
// between none and the most, what it takes between none and all.
#pragma once

#include <string>
#include <vector>

#include "sg/domains/Spatial.hpp"

namespace sg {

namespace kinds {
inline const Key fog{"fog"};
}

// A fog in a world: a box `size` from its base's middle `base`, `density`
// thick, `soft` metres of fade beyond its sides and top.
Element& add_fog(Spatial3D& world, Key id, Vec3d base, Vec3d size, double density = 0.3, double soft = 0.5);
std::vector<const Element*> fogs_of(const State& world);

// What is wrong with the fogs of `world` (none: nothing).
std::vector<std::string> fog_faults(const State& world);

}  // namespace sg
