# Terrain

`sg::terrain` (`sg/domains/Terrain.hpp`) makes land from a recipe, the way the
modeller ([modeler.md](modeler.md)) makes things: text, one statement a line,
and what it makes is a function of the text alone, kept on disk by what made
it (`sg::cache`). A land is any size, at any fineness: a tabletop at 10 cm, a
province at 10 m.

What it makes is a `Land`: heights on a grid, the maps its rules read
(wetness, distance from road and water), what covers it (up to four layers,
each a share at every point), its roads' and water's surfaces, and the things
strewn on it. `terrain::lay(world, name, land)` puts all of it in a 3D state:

- the ground is the state's own (`Spatial3D::terrain`): a pure height function
  the walker stands on (`sg::walk`), rays stop at (`sg::ray`) and the renderer
  draws - resampled round the eye, fine near and coarse far, however far it
  goes - covered by its layers (surface 19, its shares the picture
  `<name>.cover`);
- roads and water are models (asphalt with painted lines; water shallow at the
  shore, dark where deep, ruffled or still);
- each thing's variants are made by the modeller (`sculpt::made`, kept on
  disk), and each placing is a fixture per part, coloured by its material.

Laid again, what was laid by that name is taken away first. The world should
be open to the sky (`sky` = 1); its air (fog, `scatter`) and its light are
its look's, as anywhere ([README](../README.md), *Air, grade and glow*).

## The language

`#` to the end of a line is a comment; any number may be an expression
(`$w/2`, `sin(30)*4`); `let name value` names one. Statements run in order,
each on the land as it is then; water, wetness, covering, road surfaces and
scattering are settled once every height is made.

| statement | what it does |
| --- | --- |
| `size w d [cell=m] [at=x,z]` | the extent, w by d metres about at, a height every cell metres (1 unless said) |
| `seed n` | where every random rule starts (each statement also takes `seed=`) |
| `beyond h [over=m]` | past the edge, the land goes to h over m metres (else on as its edge is) |
| `base h` | level, everywhere |
| `noise amp scale [oct= gain= lac= ridged=1 billow=1 warp= seed=]` | octaves of smooth noise added, `scale` metres across the largest: rolling ground, ridged peaks (`ridged`), puffy hills (`billow`), twisted by `warp` |
| `hill x z r h [shape=dome\|cone\|mesa]` | a hill, or with h < 0 a hollow |
| `ridge h w x,z x,z ...` | a raised line w wide along the points (h < 0: a ditch, a valley) |
| `flatten x z r [h=] [soft=]` | a level pad (h: as high as the land is there) - a castle's, a house's |
| `plateau h [k=]` | above h, the land kept k as steep (flat tops) |
| `terrace step [sharp=]` | steps every `step` metres |
| `curve p` | heights redistributed (p > 1 deeper valleys, sharper peaks) |
| `tilt dx dz` `clamp lo hi` `scale k` `smooth n` | the obvious |
| `erode [drops=] [strength=] [seed=]` | rain: drops run downhill carrying earth and laying it where they slow - gullies, fans, worn valleys |
| `thermal n [talus=deg]` | slopes past `talus` slump |
| `road w x,z x,z ... [bank= sink= surface= colour= lines= smooth= name=]` | a road along the curve through the points: the land graded to its profile (averaged over `smooth` metres), level across, banked back; a ribbon of asphalt with lines over it |
| `path w x,z ...` | a worn path: graded, no ribbon (cover it with a layer by `road=`) |
| `river w depth x,z ... [name= colour= deep=]` | a channel cut downhill along the curve, and its water |
| `lake x z [level=] [name= colour= deep=]` | the water filling the hollow at x,z to its level - or to where it would spill |
| `sea level` | water over everything below the level |
| `swamp x z r [level=] [pools=]` | low, hummocky ground near its level, pools in its hollows, murky |
| `layer name surface [colour=] [height=] [slope=] [wet=] [road=] [water=] [noise=] [patch=] [cover=] [soft=]` | what covers the land where its ranges say. The first is everywhere; each after is laid over those before; four at most |
| `thing name [variants=n] : recipe / recipe ...` | a thing to strew: a modeller recipe (lines split by ` / `), `$v` its variant 1..n |
| `scatter name [density=] [count=] [height= slope= wet= road= water=] [scale=a,b] [spacing=] [sink=] [seed=] [most=]` | the thing strewn where the ranges say: `density` to 100 square metres on a jittered grid (never closer than `spacing`), sized at random in `scale`, sunk `sink` |

Ranges are `lo,hi`, either end left open (`road=6,` is six metres off a road
and further). `slope` is in degrees; `road` and `water` are metres from their
edge, negative on them; `wet` is 0..1 (where water gathers, runs and stands).
Ranges fade over `soft` (2 m of height, 5 degrees, 0.1 of wetness, half a
metre of distance unless said). `noise` makes a layer's edge ragged over
patches `patch` metres across; `cover` < 1 lays it in patches over that much
of where it may lie. Things go nowhere in the water or on a road unless their
rules say `water=` or `road=`.

Surfaces (the renderer's own, procedural): `grass`, `sand`, `earth` (`mud`,
`dirt`: give it a darker colour), `rock` (seen from every side, banded,
lichened), `gravel`/`asphalt`, `snow`, `concrete`, `tiles`, `plain`. Colours
are `rrggbb` or `r,g,b`, as they look (sRGB); the land lays them lit.

Presets (`terrain::presets()`, `sgterrain --presets`): `hills`, `mountains`,
`lake`, `swamp`, `valley road`, `island`, `moor` - whole recipes to start from
(`sgterrain --preset lake`).

## Plants: `grow` and `use trees`

The modeller grows plants from L-systems ([modeler.md](modeler.md), *Growing*):
`grow <axiom> <rule> ...`, the rules rewritten n times and the word walked by
a turtle; wood as thick as the pipe model says, leaves at every tip. `use
trees` has `tree.oak pine birch dead willow swamp palm`, `bush`,
`plant.grass`, `plant.reeds` - each by height (`h=`), seed, and `low=1` for its
low-poly self. In a land: `thing pine variants=4 : use trees / tree.pine h=12
seed=$v low=1`.

## Looking at it

```sh
sgterrain land.terrain                         # the map from above: land.png
sgterrain -p swamp -o swamp.png -s 1024        # a preset
sgterrain -e "size 200 200 ; noise 10 80"      # lines split by ' ; '
sgterrain land.terrain --obj land.obj          # all of it as one mesh
sgland land.terrain                            # as a game draws it: sky, sun, shadows, air
sgland land.terrain --eye -38,0,-290,80,-2,70 --above 1.7     # from a walker's eye
sgland -p "valley road" --fog 0.045 --sky 0.62,0.62,0.6       # thick grey air
```

`sgterrain` says what the land is (its size, heights, how much each layer
covers, its water and roads, how many things) and what the recipe got wrong;
`sgland` draws it with the engine's renderer (yaw 0 looks along +x, 90 along
+z; `--above` stands the eye that high over the ground at x,z; `--hour` the
time of day). Write, look, write again; keep the pictures under
`<build>/out/`.

## In a program

```cpp
auto land = sg::terrain::build(recipe);          // made once, kept on disk
world.params().set("sky", 1.0);
sg::terrain::lay(world, "land", land, &files);   // `files` for what things import
// land->height(x, z), land->normal(x, z), land->at(land->wet, x, z) ...
```

Any `Spatial3D` takes a land, so any realm can stand on one; its looks, light
and air are its own. `sg_terrain` (tests/test_terrain.cpp) holds the laws: the
same recipe the same land, any size, roads level across, lakes held by their
shores, things off roads and out of water, kept bytes read back the same, a
walker standing and walking on it, a ray stopping at it.

## What it does not do yet

- The ground's covering is four procedural surfaces blended per pixel; there
  are no painted (Paint) or photographed textures on the ground yet.
- Water is opaque (its colour says how deep): nothing under it is seen.
- A river's water does not flow (its surface is still, ruffled as a lake's).
- Strewn things have no low-poly stand-ins far off: each is drawn as made
  (use `low=1` trees for woods).
- The walker climbs any slope.
