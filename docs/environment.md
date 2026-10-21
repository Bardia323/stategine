# Building an environment

How a place is made to look as if it has been there - by the rules that made
it, not by placing every detail by hand. Nothing here is required: the
engine draws a stylised world as readily as a photographed one, and every
rule below is a tool to reach for when a place asks for it. A photographic
place uses nearly all of them; a painted or toy-like one, few.

The order of work, and what of the engine each step uses:

1. **References first.** Unless the brief brings them, gather them before
   anything is placed: photographs of places like it (openly licensed -
   Wikimedia Commons is searchable by its API), its light at the hour asked
   for, its materials close up. Look for what the place's photographs share
   - their colours, their big masses, what is *not* there - and write it
   down. A place built without them is built from what a model of the world
   guesses, and looks it.
2. **Real data where it is known; plausibility where it is not; the story
   where the place is someone's.** How things are spread on land has been
   measured: take the *shape* of the law from the literature and choose the
   numbers for this place (`sg::Distribution`, below). An artist's studio is
   not spread by any law: what is in it, and where, is that person's, and
   comes from their story. The two never mix: a drawn law for the papers on
   one person's desk reads as noise.
3. **Masses, then middle, then fine** (detail by frequency). Large shapes and
   the light first - land, walls, beams, the sun's way through them; then the
   things the eye goes to (beds, windows, boulders, trees); then what is felt
   more than seen (grain, gravel, flakes, stains), as material data, not
   geometry. Give the hero of a shot all three; give the distance its big
   shapes only. Uniform detail is noise: leave quiet space (below).
4. **Wear as consequence.** Say the causes (`age`, leaks - `sg/domains/Wear.hpp`)
   and let every surface answer them (`dirt_layer`, `damp_layer`); never paint
   a stain where no water went.
5. **Leave space empty on purpose.** Negative space gives scale, rest and
   contrast to what is dense; an absence (a cleared floor, an empty bed) tells
   a story. Count what is in a shot, and take things out.
6. **Measure, then look.** Render, read the frame, compare it beside the
   references; time it (README, *Performance*).

## Real assets, at real size

Photographed models and materials (Poly Haven, ambientCG: CC0), at their own
scale - check every model's bounds before it is placed (a kit of parts laid
side by side, a model in centimetres: both are common), and pick a part of a
kit out as a model of its own rather than placing the whole kit. A model
stands on the ground by the lowest of the ground under its footprint
(`sg::ground_under`), a little into it: no edge over a hollow, its shadow
meeting it. Procedurally grown things (the modeller's `use trees`) are for
the stylised, the far off and the silhouetted; close to a photographic eye,
a scanned asset - unless the place is to be made with none.

**Made with none.** A place can be made with no photograph and no scan and
still stand beside one (the memory-leak showcase's `wild`: its `valley`
made): the ground's beds from `sg::grounds` (a meadow, a lichened rock,
gravel, mud - strewn by laws, laid out as a photographed set's maps); its
stones the modeller's weathered fields (`remesh rough= facets=`) wearing
`lichen_rock`; its plants grown - a tussock as ninety fine blades from one
foot and the dead of last year round it, a fern as fronds of paired
leaflets, a shrub as nine stems branching, small leaves thick on its twigs;
what people left built from the modeller's shapes. What decides it is what
decides a photograph: real densities (thousands of blades a square metre,
stones packed against each other), real sizes, real albedos, the hollows
dark, and matte leaves (a smooth one throws a low sun's glint back).

**Photographed, made, or both: what each was like.** The memory-leak
showcase has a place each way (`valley` photographed and scanned, `wild`
made, `ruin` both), and what working on each was like, as found:

| | Photographed (`valley`) | Made (`wild`) | Both (`ruin`) |
| --- | --- | --- | --- |
| Where the realism comes from | the photographs and scans | each material's physics, coded: densities, sizes, albedos, how things lie | the photographs' surfaces; the engine's relationships between them (wear from each shape, spreading, falling, light) |
| First frame that held up | quickly | after about eight rounds - each material wrong until a real fact went in (blades by the thousand a square metre, stones packed) | after about three |
| A change | a `.mat` line or a law's number, a re-render (a minute) | code for a new material; its maps made again (minutes, then cached) | a `.mat` line, a re-render |
| What limited it | what the libraries have (no lichen on ambientCG); choosing the right set mattered more than any setting | plants close up (flat leaf cards; a low sun's glint off them) | the photographs' own character (a regular block wall reads as a pattern; an irregular one did not) |
| What carries to the next place | the sets fetched | the materials and weathering made; each new material is new work | the sets, and the wear from shape, which works on any model wearing a texture that asks for it |
| Licensing, fetching | per asset | none | per asset |

Seen from the work on them: photographs gave detail at every scale for
nothing; making gave control, variety without repeats and no assets, at
the cost of coding each material's physics; with both, the work went into
the relationships between surfaces rather than the surfaces.

## Spreading things: `sg::Distribution`

A state of its own holding named laws of chance as data - each spreading
by one, and one change of a law changes all it spread. Choose the law by the
process that put the things there:

| what made it | law | for |
| --- | --- | --- |
| nothing in particular: rain, litter in a draught | `poisson` (density) | the baseline; rarely right alone |
| competition - each keeps the others off | `disk` (spacing: blue noise) | trees, shrubs, beds in a ward, boulders on a plain |
| seeding, falling, gathering round a source | `thomas` / `matern` (parents, children, spread) | moss, tussocks, stains, rubble from one fall |
| a cause with a place | `bump` (at, sd, amount) | damp round a leak, litter by a door, wear round a handle |
| a cause along a way | `stroke` (from, to, sd, amount; sd_to, amount_to) | a path worn into a lawn, something dragged down a hall, a trail of drips |
| several at once | `sum` (of, weights) | superposition: intensities add, points of parts together |
| sizes: many small, few large | `power` (lo, hi, alpha) | rocks, fragments, stains, settlements |
| a size round a typical one | `lognormal` (median, spread) | plants of one kind, people |
| a way round a preferred one | `vonmises` (mean, kappa) | along a wind, down a slope, a path's direction |
| an amount between none and all | `beta` (a, b) | how worn, how full, how faded |
| one of several | `choice` (w0 w1 ...) | which model of a kit |

A field is sampled by a process of its own (`sg::sample`): `as` it is drawn
(its own clusters), `poisson` (thinning: as random as rain), `disk` (blue
noise thinned by it: dense where it is high, never nearer than a spacing -
tussocks, measured as regular under ~0.3 m and clustered past ~0.4 m, are a
Thomas field sampled as 0.3 m blue noise), `halton` (even, no clumps, no
grid). And a field is a picture (`sg::field_picture`) a material reads as
where it wears (a room's `wear_field`): what is strewn on the floor and what
stains the walls come from one field, and agree.

What has been measured, to start from (take the shape; choose the numbers):

- Fragment and boulder sizes: a cumulative power law; an index about 2.4 for
  boulders spread over a field, steeper (3.5-4) right under the cliff they
  fell from.
- Grass tussocks: regular (inhibited) under about 0.3 m, clustered beyond
  about 0.4 m.
- Trees in a natural stand: regular at the scale of a crown, clustered at
  the scale of a gap or a slope.

## Wear: causes, and every surface's answer

A world says how old it is (`age`, 0 new .. 1 long neglected) and where
water gets in (`sg::add_leak`: a point and its reach); a material says what
its dirt and its damp look like - photographs (`texture.dirt_layer`,
`texture.damp_layer`; docs/materials.md). The renderer composes them where
the causes put them: dirt gathered into corners and junctions, along the
floor and the ceiling, settled on what faces up; damp running down a wall
below a leak in streaks that widen as they fall, a blot on the ceiling round
it, a tide mark on the floor, rising from the floor up the walls. The edge of
each is its photograph's own grain - never a line, never a blur, never
noise made up. Add the room's wear field (above) for what is spread by
chance. Everything a place's history did agrees, because it is one history.

## Repetition

A tiled material shows its tile. Say `texture.scatter` (0..1) and each
hexagon of it is read from a place of its own, turned within
`texture.scatter_turn` radians (0 for what has a grain or a grid: tiles,
planks, brick courses), blended where hexagons meet. A land's beds do this
always. A model reused many times: vary its turn, its size and its company
(the Distribution's laws), not only its place.

## Light

The light from all round is the engine's probes, relit before each frame
from the place's own lights - a closed room (its own walls, or the
renderer's shell) by its lamps and what its windows let in, an open land by
its sun. Nothing about it is tuned by hand: set the lights as they are and
the exposure as a camera's, and what comes back off the walls is what it
is. Traced light (a look's `trace`) gives the sun's and the nearest lamps'
shadows as soft as their sources are big, and what smooth surfaces mirror
(docs/trace.md); a bulb says its size (`size`, metres), a sun its disc.
Light seen through haze: the lit air (`scatter`) - its cells (`scatter.cell`)
cost as the square of the screen over them.

## Checks before it is shown

- Every model's real size; nothing floating; nothing intersecting what it
  rests on more than a few centimetres.
- No repeat visible from the eye's places; no seam where a grid round the
  eye gives way.
- Where water meets land, no line (the water thins out in its last fifth of
  a metre).
- Each light's colour and strength as the thing it is (a sign gives a few
  lumens; the moon a fraction of the sun) - the picture's brightness the
  camera's exposure, the same for every way the frame is drawn.
- The frame beside the references.
