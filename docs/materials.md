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

**There is no metallic param.** Every surface reflects 4% head on, white
(a dielectric), except `surface` 5, which is 35% metal: it reflects in its
own colour and scatters less. A truly metallic surface would need the
renderer to gain one (`metal` beside `roughness` in `WorldBatches`/
`WorldThings` and the scene shader); until then, brushed metal is the metal.

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
`surface`; `roughness`, `emissive`, `reflects` and the rest still hold.
Colour the texture with its tint instead.

Its one element, `map`:

| Param | What it is |
| --- | --- |
| `generator` | what paints it, by name: `plain`, `noise` (`scale` cells across, `seed`; four octaves, 0.55..1), `checks` (`count`) |
| `seed` | whatever a generator reads for its variety |
| `tint_r` `tint_g` `tint_b` | multiplied over all of it - the material's colour |
| `layer`, `layer_mix` | a picture file painted over it, followed as it changes; read by the program's `Texture::set_reader` (the engine reads no image format) |
| `tile` | metres of the thing one cell covers, its pattern going on round it from face to face; 0, each face the whole cell |
| `blend` | 0 each face its own cell; 1 curves shade softly between the cells of the ways they face |
| `relief` | metres its paint stands at full height: the map's alpha is height, and the surface is bent by it so raised paint catches the light |

Its pixels are **linear** unless the texture says `set_srgb(true)` - do,
for a photograph or anything whose colours were picked by eye; otherwise it
is lit as brighter than it is and comes out washed out.

A new generator is a pure function of the map's params, the cell and a point
(u, v) in it: `Texture::define("rust", [](const Params& map, int cell, double
u, double v) { ... return {r, g, b, height}; })`. Where `tile` > 0 it must
tile: what is at u = 1 is what is at u = 0. Register it where the module that
owns it starts (never inside an arrow); a texture is remade only when its
map's params change (`Texture::stale`), so a generator may be slow but must
give the same picture for the same params.

## 4. The look of the state

How the whole state is seen - fog, lit air, exposure, grade, glow - is the
state's `LookState` (README *Looks*, *Air, grade and glow*), never a
material's. A material is right when it is right under a plain look; a look
then gives the mood. Tune a material under `sgmat`'s presets, not under a
look that grades it.

## How it is lit

The scene shader is physically based: GGX highlights, Schlick's Fresnel from
F0 (0.04 white for every dielectric; mixed toward the albedo for metal),
Karis's split-sum fit for light from all round, energy kept (a rough metal
goes darker). Light from all round comes from the look (`uAmbient`, `uSky`,
`uGround`) and from the room's light probes (bounce, relit as lamps change;
README *Light probes*). So an albedo above about 0.9 or below 0.02 looks
wrong in any light: almost nothing real is either.

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
./sgmat --set r=0.62 --set g=0.63 --set b=0.65 --set surface=5 --set roughness=0.35
./sgmat stone.mat -e "roughness=0.6; texture.relief=0.008"   # a change tried without editing the file
```

A material file is `key = value`, one a line, `#` a remark. A key is set on
each specimen as said (section 1); `texture.<key>` on the map of a texture the
specimens then wear (section 3), with `texture.srgb = 1` for a photograph and
`texture.layer` a binary PPM (P6) beside the file. Examples to start from:
`tools/materials/`.

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

A new `surface` kind or generator is tried the same way: build the engine
with it, then `sgmat --set surface=<n>` or `texture.generator=<name>` (a
generator must be registered in the program that draws; for `sgmat`, at the
top of its `main`, while it is being tried).
