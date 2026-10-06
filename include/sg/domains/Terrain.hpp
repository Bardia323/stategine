// Stategine - Terrain: land made from a recipe, as the modeller makes things.
//
// A recipe, one statement a line, says what the land is: how big (any size,
// at any fineness), how it rises and falls (noise, hills, ridges, terraces,
// erosion by rain), where its roads, paths and rivers run, where its water
// lies (lakes, the sea, a swamp), what covers it where (grass, earth, rock,
// asphalt, snow - by height, slope, wetness, and nearness to road and water),
// and what grows and stands on it (any modeller recipe - the `trees`
// library's oaks and pines, `rock`, a lamp post - scattered by the same
// rules). What it makes is a Land: heights on a grid, the maps the rules
// read, and what lies on it - a function of the recipe alone, kept on disk
// by what made it (sg/core/Cache.hpp).
//
// The language (`#` to the end of a line is a comment; any number may be an
// expression, as the modeller's: `$w/2`, `sin(30)*4`; `let name value`):
//
//   size w d [cell=m] [at=x,z]        the land's extent, w by d metres about
//                                     at (0,0), a height every `cell` metres
//   seed n                            the seed every random rule starts from
//   beyond h [over=m]                 past the edge, the land goes to h over m
//                                     (else on as its edge is)
//   base h                            level, everywhere
//   noise amp scale [oct= gain= lac= ridged=1 billow=1 warp= seed=]
//                                     rolling ground: octaves of value noise,
//                                     `scale` metres across its largest
//   hill x z r h [shape=dome|cone|mesa]   a hill (h < 0: a hollow)
//   ridge h w x,z x,z ...             a line raised w wide (h < 0: a ditch)
//   flatten x z r [h=] [soft=]        a level pad (a castle's, a house's)
//   plateau h [k=]  terrace step [sharp=]  curve p  tilt dx dz
//   clamp lo hi  scale k  smooth n
//   erode [drops=] [strength=] [seed=]   rain running off it, carrying earth
//                                     down: gullies, fans, worn valleys
//   thermal n [talus=deg]             slopes past `talus` slumping
//   road w x,z x,z ... [bank= sink= surface= colour= lines=1 name=]
//                                     a road along the curve through the
//                                     points: the land levelled under it and
//                                     banked beside it, its surface a ribbon
//   path w x,z ...                    a worn path: levelled, earth, no ribbon
//   river w depth x,z ... [name=]     a channel cut downhill along the curve,
//                                     and the water in it
//   lake x z [level=] [name= colour= deep=]   the water that fills the hollow
//                                     at x,z to its level (else to where it
//                                     would spill)
//   sea level                         water over everything below the level
//   swamp x z r [level=] [pools=]     low wet ground, pools of standing water
//   layer name surface [colour=#rrggbb] [height=a,b] [slope=a,b] [wet=a,b]
//         [road=a,b] [water=a,b] [noise=k] [scale=m] [soft=]
//                                     what covers the land where the ranges
//                                     say (slope in degrees, road and water
//                                     in metres from them); the first is
//                                     everywhere, each after laid over it
//                                     (four at most). Surfaces: grass,
//                                     earth, mud, rock, sand, gravel,
//                                     asphalt, snow
//   thing name [variants=n] : <modeller recipe, lines split by / >
//                                     a thing to scatter; $v in its recipe
//                                     is which variant (1..n)
//   scatter name [density= | count=] [height= slope= wet= road= water=]
//         [scale=a,b] [spacing=m] [sink=m] [seed=] [most=]
//                                     the thing, strewn where the ranges say
//                                     (density: how many to 100 square metres)
//
// The ground and its covering are what the renderer draws as `terrain`
// (resampled round the eye, any size); roads, water and things are models.
// `lay` puts a Land in a 3D state: all of it, its ground the state's
// (Spatial3D::terrain), so walking, rays and drawing agree.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "sg/domains/Modeler.hpp"
#include "sg/domains/Spatial.hpp"

namespace sg::terrain {

// What covers the land somewhere: a surface of the renderer's, its colour,
// and where it lies.
struct Layer {
    std::string name;
    int surface = 16;      // the renderer's procedural surface (Surfaces below)
    Vec3d colour{0.35, 0.42, 0.22};
};

// The renderer's surfaces a layer may be (uSurface): grass 16, sand 8,
// earth 20 (mud is earth, dark), asphalt 21 (gravel too), rock 22, snow 23.
int surface_named(const std::string& name);

// Water lying on the land: its surface (triangles in the land's own frame,
// eight floats a corner), the level it lies at, and how deep it is under
// every point of its box (a picture's red: 0 at the shore, 255 at `deepest`).
struct Water {
    std::string name;
    std::vector<float> corners;
    Vec3d lo, hi;
    Vec3d shallow{0.30, 0.34, 0.26}, deep{0.03, 0.07, 0.08};
    double deepest = 4;
    int map_w = 0, map_h = 0;
    std::vector<unsigned char> depth;  // RGBA, rows from lo.z
    bool flowing = false;
};

// A road's surface: a ribbon over the levelled land, its uv across (0..1)
// and along (metres).
struct Road {
    std::string name;
    double width = 6;
    std::vector<Vec3d> centre;
    std::vector<float> corners;
    Vec3d lo, hi;
    int surface = 21;
    Vec3d colour{0.16, 0.16, 0.16};
    bool lines = true;
};

struct Thing {
    std::string name;
    std::vector<std::string> recipes;  // one a variant
};
// One thing set on the land: which, which variant, where its foot is, how it
// is turned and how big.
struct Placed {
    int thing = 0, variant = 0;
    Vec3d at;
    double yaw = 0, scale = 1;
};

struct Land {
    double x0 = 0, z0 = 0, w = 0, d = 0, cell = 1;
    int nx = 0, nz = 0;  // cells; heights at (nx + 1) x (nz + 1) points
    std::vector<float> h;
    // What the rules read, a value at each point: how wet (0..1), how far
    // from a road's edge and from water (metres; large where far).
    std::vector<float> wet, road, water;
    double beyond = 0, over = 0;
    bool to_beyond = false;
    std::vector<Layer> layers;
    std::vector<unsigned char> splat;  // RGBA at each point: each layer's share
    std::vector<Road> roads;
    std::vector<Water> waters;
    std::vector<Thing> things;
    std::vector<Placed> placed;
    std::string errors;  // what the recipe got wrong, a line each

    // The height anywhere (between the points, bilinear; past the edge, as
    // `beyond` says), the way up there, and how steep it is (rise over run).
    double height(double x, double z) const;
    Vec3d normal(double x, double z) const;
    double slope(double x, double z) const;
    bool inside(double x, double z) const { return x >= x0 && z >= z0 && x <= x0 + w && z <= z0 + d; }
    float at(const std::vector<float>& map, double x, double z) const;
    std::size_t index(int i, int j) const { return std::size_t(j) * std::size_t(nx + 1) + std::size_t(i); }
};

// The land a recipe makes: made once, remembered, and kept on disk when a
// cache folder is set.
std::shared_ptr<const Land> build(const std::string& recipe);
// The same, made now and not kept (what build keeps).
Land make(const std::string& recipe);

// The words of the language, one a line, and whole recipes to start from
// by name (`presets`: hills, mountains, lake, swamp, valley road, island,
// moor), each a recipe that can be edited.
std::string words();
std::vector<std::string> presets();
std::string preset(const std::string& name);

// Put the land in a 3D state, its things named `<name>...`: its ground (the
// state's terrain `<name>`, drawn with its layers), its roads and water (as
// models), and every thing scattered on it (its parts as fixtures, each
// thing's variants made by the modeller - `files` for what they import).
// Laid again, what was laid before is taken away first. The state should be
// open to the sky (`sky` = 1) to be seen as land. Returns the things made.
std::size_t lay(Spatial3D& world, const std::string& name, std::shared_ptr<const Land> land, const sculpt::Files* files = nullptr);

// Looking at it: the land from above, shaded by a low sun, coloured by its
// layers, its water, roads and things on it - `size` pixels across its
// longer side (rgb rows; sculpt::png makes a file). And a model of all of
// it, the ground included (`ground` metres a face), for sculpt::picture,
// picture_from and to_obj.
std::vector<unsigned char> map(const Land& land, int size, int& w, int& h);
sculpt::Model model(const Land& land, double ground = 0, bool things = true);

}  // namespace sg::terrain
