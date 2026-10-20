// Stategine - Grounds: natural surfaces made, not photographed.
//
// A ground is what has come to lie on it, layer over layer: soil, then
// pebbles, then moss, then the grass that fell, then twigs - each layer
// things of one kind, strewn by a law of its own (how many, how big, how
// they gather), each lying on what is under it. So a ground is a sum of
// strewings - each a point process (Poisson's, thinned by a field where its
// things gather: a Cox process, as vegetation is measured to stand) whose
// things' sizes run by a Distribution's value law (sg::draw: a power law for
// stones, a lognormal for cushions and blades) - superimposed by height:
// whatever lies higher shows. Its colour, height, roughness and how open its
// hollows are to the sky come out of the same strewing, so they agree.
//
// The materials, by name, for any Texture (`generator`) once define() has
// registered them - each tiles every `tile` metres (its params say how much
// of each layer):
//   meadow        soil; pebbles; moss cushions gathered where the moss
//                 took (`moss` 0..1); grass blades, living and dead (`dry`
//                 0..1 of them dead); twigs (`twigs`)
//   lichen_rock   stone (`tone` its grey), its grains, its cracks; lichen in
//                 rosettes of every size, many small and few large
//                 (`lichen` 0..1); moss in its cracks and hollows (`moss`)
//   gravel        packed stones by a power law of size on fines; a little
//                 grass come through (`grass` 0..1)
//   mud           silt, wet (`wet` 0..1: the low of it under a film of
//                 water, smooth); pebbles and what washed up
// Each reads `seed`, and `depth` - metres from its lowest to its highest
// (its height channel 0..1 over that).
//
// tile_maps: a material laid out as the four pictures a ground's bed is
// (albedo, normal, surface: occlusion roughness metal, height) - what a
// photographed set gives, made from its laws instead.
#pragma once

#include <string>
#include <vector>

#include "sg/core/State.hpp"

namespace sg::grounds {

// Registers the materials with sg::Texture (once; again does nothing).
void define();

// A material's four maps, one tile of `px` square: RGBA rows each. Albedo
// as seen (sRGB); normal OpenGL's way (green up); surface red occlusion,
// green roughness, blue metal (linear); height grey 0..1 over its `depth`.
struct TileMaps {
    int px = 0;
    std::vector<unsigned char> albedo, normal, surface, height;
};
TileMaps tile_maps(const std::string& material, const Params& params, int px);

}  // namespace sg::grounds
