// The text of the modeller's built-in macros, and its libraries by name.
#pragma once

#include <string>

namespace sg::sculpt {
// Always there: what a castle, a village or a wood is made of.
const char* library();
// A library by name (`use <name>`): one a program defined (define_library),
// or one the modeller comes with - each its own file.
bool library_named(const std::string& name, std::string& text);
// What the libraries a program defined say, all of them, as one text: a
// recipe's mesh depends on them (the built-in ones are the code's own).
std::string defined_libraries();
// The ones it comes with: the architect's composer, and its styles - each
// saying the same words (ModelerArch.cpp) its own way.
const char* lib_arch();
const char* lib_classical();
const char* lib_gothic();
const char* lib_modern();
const char* lib_romanesque();
const char* lib_islamic();
const char* lib_japanese();
const char* lib_brutalist();
const char* lib_artdeco();
// And what fills a building: a church's furnishings (`use church`).
const char* lib_church();
// What buildings are really made of, each a library of composable
// constructions (docs/modeler.md, "Architecture"):
//   mould      running mouldings as swept profiles (cyma, ovolo, cavetto,
//              torus, scotia), cornices built of them, dentils, modillions,
//              balusters, panels, rustication, quoins, consoles, urns
//   orders     the five classical orders by Vignola's modules: column, base,
//              capital, entablature, pedestal, pediment, portico, aedicule
//   pointed    pointed-arch geometry by compass: two-centred arches of any
//              centre (equilateral, lancet, drop), four-centred, ogee; rib
//              vaults whose ribs meet at one crown; tracery, pinnacles,
//              buttresses, flyers, roses
//   girih      islamic geometry: star polygons, star-and-cross tilings,
//              rosettes, muqarnas, horseshoe and multifoil arches, domes on
//              drums, screens, iwans
//   structure  what holds up: footings, battered walls, piers, voussoir
//              arches, barrel, groin and rib vaults, domes, pendentives,
//              trusses, stairs (straight, dog-leg, spiral)
//   param      a style that is a point in a continuous space (arch, ornament,
//              mass, vertical, pitch, rustic, glazing, tracery, dome,
//              pattern, order), the known styles its presets, any two mixed
const char* lib_mould();
const char* lib_orders();
const char* lib_pointed();
const char* lib_girih();
const char* lib_structure();
const char* lib_param();
//   city       the modern city from the frame building's rules: skyscrapers,
//              blocks, houses, terraces, sheds, shops; lots, blocks, streets
//              and districts of them
const char* lib_city();
//   trees      plants grown from L-systems: oak, pine, birch, dead, willow,
//              swamp, palm, bush, grass, reeds - each by height and seed,
//              and its low-poly self (ModelerTrees.cpp)
const char* lib_trees();
}  // namespace sg::sculpt
