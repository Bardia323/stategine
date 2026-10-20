// Stategine - Wear: what time has done to a place, as its causes.
//
// A place's wear is not painted thing by thing: it is the consequence of a
// few causes the place says, which every surface in it answers - so a stain
// on the ceiling, the streak down the wall under it and the damp along the
// floor beneath all agree, being one leak's.
//
//   `age`   (a world's param, 0 new .. 1 long neglected; 1 unless it says)
//           how far every process has gone: one number for the whole place.
//   a leak  (an element of kind `leak`): where water gets in - a point
//           (x, y, z: a crack in the ceiling, a window's sill, a pipe's
//           joint), `reach` metres round it that its water spreads (1).
//           Below it, water runs down whatever wall it meets and stains
//           it in streaks that widen as they fall; on the ceiling round it,
//           a blot; on the floor under it, where the water lands, a tide
//           mark.
//
// What each surface does with them is its material's (docs/materials.md):
// a photograph of its dirt (`dirt_layer`) gathered where dirt gathers - into
// corners, along the floor and the ceiling, settled on whatever faces up -
// and a photograph of its damp (`damp_layer`) where water has been - under
// the leaks, rising from the floor (`damp_rise` metres). A renderer reads the
// causes as data (the GL view: the eight leaks of a room nearest the eye).
//
// The laws (wear_faults, a Spatial3D's faults): a leak's numbers are
// numbers, its reach is more than none; a world's age is between 0 and 1.
#pragma once

#include <string>
#include <vector>

#include "sg/domains/Spatial.hpp"

namespace sg {

namespace kinds {
inline const Key leak{"leak"};
}

// A leak in a world: where water gets in, and how far round it spreads.
Element& add_leak(Spatial3D& world, Key id, Vec3d at, double reach = 1.0);
std::vector<const Element*> leaks_of(const State& world);

// What is wrong with the wear of `world` (none: nothing).
std::vector<std::string> wear_faults(const State& world);

}  // namespace sg
