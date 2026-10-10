# Materials

What a surface is made of, how a thing says so, and how to see it before
using it. Everything a material is reaches the renderer through four doors,
and only through them: **a thing's params, its `surface` kind, a `Texture`
it wears, and the look of the state it is in.** A project may build its own
way of making materials on top (a texture that paints itself by strokes, a
library of named materials) - what it makes still arrives through these four.

Look at a material with `sgmat` before you use it (*Seeing one* below). It
draws it the same way every time, beside references, and says by number how
bright it came out.

## 1. A thing's params

Any `mesh` or `wall` element of a `Spatial3D` (a fixture is a mesh with no
arrow) is drawn by what its params say. Colours are **linear**, 0 to 1: what
fraction of the light a surface gives back, not a colour picked on a screen
(a screen's mid grey, 128, is about 0.22 linear).

| Param | What it is | Unless it says |
| --- | --- | --- |
| `r` `g` `b` | albedo: the fraction of each colour of light it scatters | mesh 0.8 0.5 0.25 (orange), wall 0.52 0.50 0.48 |
| `roughness` | 0.05 a polished mirror .. 1 chalk (kept to 0.05..1) | mesh 0.6, wall 0.9 |
| `metal` | 0..1, how metal: 1 reflects in its own colour (`r g b` its tint: gold 1.0 0.78 0.34, copper 0.95 0.64 0.54, iron 0.56 0.57 0.58) and scatters none; 0 a dielectric (paint, stone, wood) reflecting 4% white. Real things are 0 or 1; between is for a dusty or painted-over metal | 0 |
| `surface` | which procedural surface it is (section 2) | **mesh 3 (crate)**, wall 2 (plaster) |
| `emissive` | light of its own, added to what it reflects | 0 |
| `glass` | 0..1, how clear: drawn after the rest of its room, blended over what is behind, more reflective edge on | 0 (opaque) |
| `reflects` | 0..1, how much of the room it mirrors on its broad face (across its thinnest side, facing the eye): a polished floor 0.2, a mirror 1, less as it roughens; `Quality::reflections` | 0 |
| `mirror` | how much of the real sky it reflects instead of light from all round: still water, glossy stone, under a sky | 0 |
| `shape` | `box`, `sphere`, `cylinder`, or a `model` by name | box |
| `bevel` | rounded edges of a box, metres | 0 |
| `depth_layer` | parts of one size that coincide: which shows | 0 |
| `skin` + `tile` | a picture of its state's tiled over the world, `tile` metres to a picture | - |

A mesh that says nothing is an orange crate on purpose: always say `surface`
(0 for plain) and a colour.

A room with no walls of its own is a box drawn from the room's params:
`floor_r/g/b`, `floor_surface` (1 tiles), `floor_roughness`, `floor_reflects`;
`wall_r/g/b` (plaster); `ceiling_r/g/b`, `ceiling_surface`.

A metal is only as good as what it has to reflect: a bare metal in a room
lit only by its look's sky and ground goes dark. A room's probes (bounce,
on by default) and its lamps' highlights give it something; under an open
sky, `mirror` adds the sky itself. `surface` 5 (brushed) is at least 35%
metal whatever `metal` says.

`metal` is drawn by the native renderer (GL); the browser's (WebGPU) does
not read it yet.

## 2. `surface`: the procedural kinds

A number naming a pattern the scene shader computes (`surface_albedo`,
`room_material` in `src/gl/Shaders.cpp`). Each multiplies the colour by its
pattern and may nudge the roughness. Kinds below 10 are laid in the thing's
own frame and scale with it; 10 and above are laid in the room's metres, so
a floor's planks keep their size however big the floor.

| | In the thing's frame | | In the room's metres |
| --- | --- | --- | --- |
| 0 | plain: the colour, nothing more | 10 | planks |
| 1 | floor tiles with grout | 11 | concrete: mottled, pour lines |
| 2 | plaster, faintly graded up the wall | 12 | checker, half-metre squares |
| 3 | crate: planks and a dark bevel | 13 | brick, running bond |
| 4 | wood: grain and rings along the thing | 14 | carpet |
| 5 | brushed metal (35% metal, smoother) | 15 | metal plate: seams, diamond tread |
| 6 | moulded plastic: speckle, dark edges | 16 | grass |
| 7 | fabric: a weave, averaged away when fine | 17 | marble: slabs, wandering veins |
| 8 | sand rippled by wind, rock where steep | 18 | water (waves in its normal) |
| 9 | sky: not lit, not fogged | 19 | ground of a land (`sg::terrain`'s layers) |
| | | 20 | earth: clods, stones |

A new kind is an engine change: a branch in the shader's `surface_albedo` or
`room_material`, keyed on the next free number, a row here, and an
`sgmat` picture of it in the commit. Never a shader of a game's own.

## 3. A `Texture` it wears

`sg::Texture` (`sg/domains/Texture.hpp`) is a picture as a state of its
own: six cells, the thing seen from each way of each axis (+x, -x, +z on
the top row; -z, +y, -y below), so any shape wears it with no unwrap. A thing
wears one by being embedded in it - `embed room.crate -> stone name
wears.crate` - and the program that draws gives the renderer its pixels
for what the graph declares (`GLWorldView::bind_surface(thing, texture)`;
derive it from the embeddings, never bind what the graph does not say).

**A worn texture is the albedo.** It replaces `r g b` and the pattern of
`surface`; `roughness`, `metal`, `emissive`, `reflects` and the rest still
hold - unless it has a surface map (below), which says roughness and metal
point by point. Colour the texture with its tint instead.

Its one element, `map`:

| Param | What it is |
| --- | --- |
| `generator` | what paints it, by name: `plain`, `noise` (`scale` cells across, `seed`; four octaves, 0.55..1), `checks` (`count`), or a material: `rust` (below) |
| `seed` | whatever a generator reads for its variety |
| `tint_r` `tint_g` `tint_b` | multiplied over its colour - the material's colour |
| `layer`, `layer_mix` | a picture file painted over it, followed as it changes; read by the program's `Texture::set_reader` - the engine reads no image format; `sg::pictures::read` (`stategine::pictures`) reads PNG, JPEG, TGA, BMP and PPM |
| `surface_layer` | a picture file of the surface map (red occlusion, green roughness, blue metal - glTF's ORM order), over what the material says; read as `layer` is |
| `normals` | 1: a normal map made from its height (below) |
| `normal_layer` | a picture file of a normal map, OpenGL's way (green up), over the made one; `normal_dx` 1 for DirectX's (green down) |
| `normal_strength` | how strongly its normal map bends the surface: 1 as made or drawn, 2 twice as steep | 
| `per_cell` | 1: each layer file is one tile, laid whole in every cell - a photographed, tileable material; 0 (unless said), stretched over all six cells - a painting over the projection guide |
| `tile` | metres of the thing one cell covers, its pattern going on round it from face to face; 0, each face the whole cell |
| `blend` | 0 each face its own cell; 1 curves shade softly between the cells of the ways they face |
| `relief` | metres its paint stands at full height: the map's alpha is height, and the surface is bent by it so raised paint catches the light |

**Colour spaces.** A texture's colours are as seen on a screen (sRGB), as a
photograph's or anything picked by eye are: the renderer decodes them
before lighting (`set_srgb`, on for every `Texture`). So a generator's 0.46
is a screen's 0.46, about 0.18 linear - unlike a thing's `r g b`, which are
linear. A texture of linear values (a measurement, data) says
`set_srgb(false)`. The surface map is always linear.

### The surface map: roughness, metal, occlusion, point by point

Beside its colour a texture can say what its surface is at every point -
a second picture of the same six cells, worn the same way:

| Channel | What it is |
| --- | --- |
| red: occlusion | how open its hollows are to the light from all round: 1 open, 0 shut. It dims what comes from the look, the probes and bounce - the lamps' own light still reaches where they shine |
| green: roughness | in place of the thing's `roughness` |
| blue: metal | in place of the thing's `metal` |

It is made only when something says it: the generator is a material, or
there is a `surface_layer`. Without one, the thing's own `roughness` and
`metal` hold. This is what makes a surface read as real: painted steel
whose paint is satin, whose bare chips shine as metal, whose rust is rough
and dull - on one thing, point by point.

### The normal map: which way the surface faces, point by point

A third picture of the same cells says which way the surface faces at every
point: x along a cell's u, y up its v, z out of it (OpenGL's way, as most
tools and Poly Haven write them). A thing wearing it is lit as bent by it -
grain, flakes, scratches, weave, stitching, rivets - finer and smoother than
`relief` alone, which the screen can only tell from how the height steps
between pixels (it comes out blocky close up).

- **Made from the height** (`normals` = 1): from the generator's own height,
  at full precision - not the picture's 256 steps - `relief` metres high at
  full, `tile` metres a cell (a cell a metre where it does not tile). Every
  material with a height should say it; `relief` alone is for a painting.
- **Read from a file** (`normal_layer`): a photographed material's normal
  map, with `per_cell` 1 to lay its one tile in every cell. `normal_dx` 1
  turns a DirectX one (green down) the right way up - a map whose bumps look
  like dents is the other way.
- `normal_strength` makes it steeper or flatter.

There are no tangents and none are needed: each normal is turned into how
fast the height climbs across the cell, carried onto the screen by how the
cell's place moves there, and the three ways a point half faces weighed as
their colours are (Mikkelsen's surface gradient, `skin_height_steps` in the
scene shader). So it holds on any shape, any face, either way up. The map is
linear and its own texture on the card, made only when something says it.

### Writing a generator or a material

A generator is a pure function of the map's params, the cell and a point
(u, v) in it, giving the colour and height: `Texture::define("bark", [](const
Params& map, int cell, double u, double v) { ... return std::array<double,
4>{r, g, b, height}; })`. A **material** gives every channel at once -
colour, height, occlusion, roughness, metal - and paints the surface map
too: `Texture::define_material("rust", [](const Params& map, int cell, double
u, double v) { Texture::Channels c; ...; return c; })`. Write a material
whenever roughness or metal vary across the surface; it costs nothing more
where they do not.

Both must tile where the texture does (`tile` > 0: what is at u = 1 is
what is at u = 0) and give the same for the same params. Register them where
the module that owns them starts (never inside an arrow). A texture is remade
only when its map's params change, so they may be slow.

`rust` (built in, `src/domains/Texture.cpp`) is the example to copy: steel,
painted, the paint flaked to bright metal round patches of rust. Its params:
`rust` (how much, 0..1, 0.45), `scale` (patches across a cell, 3), `paint_r/g/b`
(the paint, as picked by eye), `seed`. It shows the pattern every material
follows: masks from tiling noise (where it is rusted, where bare), and each
channel blended between the kinds of surface by those masks.

## 4. The look of the state

How the whole state is seen - fog, lit air, exposure, grade, glow - is the
state's `LookState` (README *Looks*, *Air, grade and glow*), never a
material's. A material is right when it is right under a plain look; a look
then gives the mood. Tune a material under `sgmat`'s presets, not under a
look that grades it.

## How it is lit

The scene shader is physically based (metal/roughness, as glTF): GGX
highlights, Schlick's Fresnel from F0 (0.04 white for every dielectric; the
albedo for metal, which scatters none), Karis's split-sum fit for light from
all round, energy kept (a rough metal goes darker), the surface bent by a
texture's normal map or, without one, its height (`relief`).

Light from all round comes from the look (`uAmbient`, `uSky`, `uGround`) and
from the room's light probes (bounce, relit as lamps change; README *Light
probes*). So an albedo above about 0.9 or below 0.02 looks wrong in any
light: almost nothing real is either.

Not yet: an emission map, clearcoat, sheen, subsurface and anisotropy.

## Real albedos and roughness

Approximate linear albedos, to start from (measured tables:
physicallybased.info):

| Material | Albedo | Roughness |
| --- | --- | --- |
| fresh snow | 0.80-0.90 | 0.6-0.8 |
| white paint, plaster | 0.70-0.85 | 0.5-0.9 |
| dry sand | 0.35-0.45 | 0.8-1.0 |
| concrete | 0.25-0.40 | 0.8-0.95 |
| light wood (pine) | 0.35-0.50 | 0.5-0.8 |
| dark wood (walnut) | 0.10-0.20 | 0.4-0.7 |
| red brick | 0.20-0.35, reddish | 0.8-0.95 |
| grass, leaves | 0.10-0.25, green | 0.6-0.9 |
| bare earth | 0.10-0.20 | 0.9-1.0 |
| rust | 0.10-0.25, orange-brown | 0.7-0.95 |
| asphalt | 0.04-0.12 | 0.8-1.0 |
| charcoal, soot | 0.02-0.04 | 0.9-1.0 |
| polished stone | (its colour) | 0.1-0.3 |

Metals (`metal = 1`; the colour is what they reflect, linear):

| Metal | `r g b` | Roughness |
| --- | --- | --- |
| silver | 0.97 0.96 0.91 | 0.05-0.3 |
| aluminium | 0.91 0.92 0.92 | 0.2-0.5 |
| gold | 1.00 0.78 0.34 | 0.1-0.4 |
| copper | 0.95 0.64 0.54 | 0.15-0.5 |
| iron, steel | 0.56 0.57 0.58 | 0.2-0.6 (brushed 0.3, cast 0.6) |
| brass | 0.91 0.78 0.42 | 0.15-0.45 |

Wet: darker (albedo times about 0.6) and smoother (roughness down by
0.3-0.5) - water fills the pores.

## Seeing one: sgmat

`sgmat` (`tools/sgmat.cpp`; its stage `tools/sgmat.sg`, `sgmat_worn.sg`)
draws a material on three specimens - a ball (how it turns from the light),
a bevelled cube (edges and flat faces), a slab leaning back (pattern scale,
relief, a broad highlight) - beside a ball of 18% grey, a chrome ball and
six patches of known colour, in a closed neutral studio, with the renderer
the game uses. The stage never moves, so two pictures are two materials.

```sh
cd build
./sgmat ../tools/materials/worn_stone.mat -o out/sgmat/stone.png
./sgmat ../tools/materials/worn_stone.mat --preset all -o out/sgmat/stone.sheet.png
./sgmat --set r=0.95 --set g=0.64 --set b=0.54 --set metal=1 --set roughness=0.3   # copper
./sgmat ../tools/materials/rusted_steel.mat --preset all                           # a material: every channel
./sgmat stone.mat -e "roughness=0.6; texture.relief=0.008"   # a change tried without editing the file
./sgmat rusted_steel.mat --view cube                         # close on one specimen: ball, cube, slab
```

A material file is `key = value`, one a line, `#` a remark. A key is set on
each specimen as said (section 1); `texture.<key>` on the map of a texture the
specimens then wear (section 3), with `texture.srgb = 0` for a texture of
linear values, and `texture.layer` / `texture.surface_layer` / `texture.normal_layer` PNG, JPEG, TGA, BMP or PPM files
beside the file. Examples to start from: `tools/materials/` (`rusted_steel`
uses every channel).

Presets (`--preset`): `studio` (a key from the front left, a fill, a rim),
`lamp` (one warm bulb in a dark room, a night interior), `soft` (overcast,
light from everywhere), `all` (each, and a sheet of them side by side). Each
is exposed so the grey ball shows about 120 of 255.

After each picture it prints what the eye measures: each thing's colour as
it shows on screen (0..255, after the tone curve) and its brightness beside
the grey ball's. The patches read about: white 200, mid 120, black 35 under
`studio`. A specimen far brighter than white or darker than black under every
preset has an albedo nothing real has. `! ...` lines are problems - a look
that did not build, a law broken - read every one.

Pictures go where it runs (`build/out/sgmat/` by convention), never beside
the material.

### The loop

1. Start from the closest example or the albedo table; write the `.mat`.
2. `sgmat <file> --preset all`; look at the sheet, then at the numbers.
3. Change one thing at a time (`-e` to try it), and look again.
4. When it is right, put it where it will be used: its params in the `.sg`
   that declares the thing, its texture as a `Texture` state the thing wears.
   The `.mat` is a sketch for `sgmat`, not a format the engine reads.

### A photographed material (Poly Haven, ambientCG, ...)

Their maps go straight in - each one tile, laid whole in every cell
(`per_cell` = 1), `tile` the metres it covers (the site says; about 1-2 m):

| Their map | Here |
| --- | --- |
| diffuse / albedo / base colour (JPG is fine) | `texture.layer` |
| normal, **OpenGL** (`nor_gl`) - PNG, not JPG | `texture.normal_layer` (a DirectX one, `nor_dx`: and `normal_dx = 1`) |
| ARM / ORM (occlusion, roughness, metal packed) - PNG | `texture.surface_layer`, as it is |
| separate AO, roughness, metal maps | pack them first: red AO, green roughness, blue metal |
| displacement / height | not read: bake its normals into the normal map instead |

`tools/materials/photographed.mat` is the template. `--cell 1024` lets a
1K map keep its detail (a cell is 256 pixels unless said). The program that
draws gives the reader once: `Texture::set_reader(sg::pictures::read)`.

A new `surface` kind or generator is tried the same way: build the engine
with it, then `sgmat --set surface=<n>` or `texture.generator=<name>` (a
generator must be registered in the program that draws; for `sgmat`, at the
top of its `main`, while it is being tried).
