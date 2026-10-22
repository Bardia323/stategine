# Workflows

How work is done here, step by step, as found by doing it. Each section is
one kind of work; what is general about building a place is in
[environment.md](environment.md), and this file is what was learned
reaching for it.

## A photographic interior

Learned making Memory Leak's dev room (`tools/devroom.cpp` in the game)
against a room built in Blender and Unreal (Lumen) - the target: a frame
that could be taken for footage of a real room. Not a copy of that room:
its level of realism, in a place of the game's own.

### 1. References, kept and measured

Keep the reference frames beside the place (the game's
`reference/devroom/`), and measure them as well as look at them - the
picture's mean, its 10th, 50th and 90th percentiles of brightness, the
brightness of its left third and bottom third, its saturation:

| | mean | p10 | p50 | p90 | saturation |
| --- | --- | --- | --- | --- | --- |
| a photographic interior, day, window in shot | 75 - 110 | 6 - 32 | 49 - 88 | 180 - 208 | 0.06 - 0.13 |

Render our frame framed as theirs is (a shot of its own: the dev room's
`ref`), put the two side by side in one picture, and read the numbers
beside them. Eyes alone drift; the numbers said at once that our wall was
twice as bright as theirs and our highlights clipped where theirs did not.

### 2. Look from every side

A world is walked, not photographed (AGENTS.md *Look from every side
before it counts*). Every placement is rendered from nine eyes at once -
straight on, each side, above, low, across the room, and at every scale:
close as a hand holds it, closer, and far off - in one sheet
(`tools/devroom_views.py <x> <y> <z> <facing> <out.png>`). One shot hid,
here, in turn: props standing 5-10 cm off the desk, a lamp clamped inside a
fan with its arm through the blinds, a screen's glass sunk into its case at
the bottom and standing off it at the top, a futon on four little feet.

### 3. Light: the window is the room's light

What made the room read as "rendered" more than anything:

- **The window is a light the size of the window.** A room lit by day is
  lit by the sky seen through its window - a large, soft source - not by a
  sun stripe across one wall and probes for the rest. Hang spot lamps from
  the glass (`light_from` the pane), a little outside it, as wide as the
  opening (`size` ~1.5 m for a 2.5 m window), aimed **down** onto the desk
  and the floor (direction about (0, -0.75, 1) into the room). Aimed level,
  they throw the blinds' slats as hard stripes across the far wall.
- **Things in the opening must say what they cover.** A window frame or a
  blind made as one model has a box the size of the whole opening, and the
  renderer took that as the window shut: say `covers` 0 on frames,
  lattices and blinds; their own shadows keep out what they keep out.
- **The sun weak, soft, or none.** A hazy afternoon: `strength` ~0.2, its
  disc wide (`size` 2.5). The sun's job is a few soft bars on the desk, not
  the room's light. Check what makes a stripe before tuning the sun:
  moving the sun here changed nothing in the picture - the stripes were
  the window lamps'.
- **The street blown out.** Outside a window seen from a dim room is near
  white: bright haze (`density` ~0.05, fog a light grey), a pale sky. A
  sharp, saturated street behind the blinds says "game".
- **Bounce traced, not boxed.** The probes' boxes are about 2.5 m across:
  a small room gets four, and every shadowed surface the same flat light.
  Ask for traced light from all round (`trace.gi`, docs/trace.md): one
  bounce a pixel, the window a light to the rays, contact darkening where
  things meet. And the screen-space occlusion (`ao` 1, `ao.radius` 0.25)
  for what is nearer than a ray's grain.
- **Light levels by measurement.** The window lamps' lumens, the exposure
  and the walls' albedo were each set by reading the frame's percentiles
  against the reference, one change at a time. Two orders of magnitude
  were wrong at first (9000 lm blew everything out; 100 was right with
  exposure 1.1).

### 4. The camera is a camera

- **A film's grade, not a game's:** saturation ~0.78, no added contrast,
  white balance neutral for daylight (tint ~0.94 1 1.04), a tungsten one
  at night (0.74 1 1.42), bloom ~0.5 so the window glows into the frame, a
  vignette ~0.4, a little grain.
- **A lens:** `fstop` 2.8, focused on what is in the middle of the
  picture; a shutter open half the frame (`shutter` 0.5) for motion blur.
- **A hand:** the eye drifts by millimetres and fractions of a degree,
  never still (`den::hold`).
- **Framed as a photographer would:** from a corner, at chest height,
  looking into the busiest corner with the window in it.

### 5. Materials

- **Real albedo first.** Measure a photographed set's mean in linear light
  and tint it to what the thing is (beige paper ~0.5, white paint ~0.75,
  black leather ~0.03). A scan straight from a library was half as bright
  as the wall it stood for.
- **Dirt from photographs, where it gathers:** `dirt_layer` from a real
  dirty surface, gathered into corners, along the floor and the ceiling;
  `edge` wear on furniture; never noise.
- **Roughness varies.** A generated model's surface map often has one
  roughness everywhere (Trellis gave the leather chair 0.42 flat): it then
  reads as grey plastic. Make the roughness from the model's own maps -
  glossier where the surface is stretched flat, rougher in its creases
  (from the normal map's bend) and where it is rubbed pale (from its
  colour). The chair took highlights at once.
- **Never a picture standing in for a screen.** A screen shows a state of
  the world (the terminal embedded in a portal on the glass), and a tube
  is drawn as a tube (`crt`, `bulge`, `halo`) - measured to the model's
  bezel, tipped as the case is.

### 6. Assets: three roads, in this order

Each thing on the first road that gives it photoreal, no further:

1. **A photographed or modelled CC0 asset found online** (Poly Haven first,
   then other CC0 libraries) - real, sized, worn - if it looks photoreal.
2. **Built by hand**: by a recipe in the program (shells, panelling, doors,
   a roof's timbers, shelves, a cork board, blinds), by a script (books
   with printed spines from one atlas, binders, magazines, papers,
   crumpled paper - the modeller's weathered `remesh`), or in Blender
   (headless python or its MCP) - real dimensions, photographed PBR
   materials. Most furniture and fittings are simple solids and turned
   profiles: build them.
3. **Generated** (Flux 2 Klein picture -> Trellis 2 -> repaired in Blender:
   floaters off, real size, base at y = 0, front +z, faces to a budget)
   **only** for a hero too complex to model by hand (a tufted chesterfield,
   a figure) or a small thing no one studies (a knife on the floor). Check
   every one's bounds against the real thing (a remote came out 0.6 m long)
   and look at it close: blotches baked into a texture show at once.

Boxes of one colour stood in for books at first and read as toys; printed
spines made the shelves real.

### 6a. Surfaces are layered, never baked

A surface is its clean material, then each thing that happened to it laid
over it as a layer of its own, gathered where its cause puts it - never
painted into the material's picture:

- the material: photographed (fetched) or made by script/Flux, **clean** -
  a carpet's pattern, a paper's damask, a wood's grain, nothing more;
- the layers (docs/materials.md): `dirt_layer` (gathered into corners and
  edges, along floors and ceilings), `damp_layer` (run down walls below a
  world's leaks, pooled where its `wear_field`'s green says - blood is a
  damp layer whose picture is blood), `edge` wear and `dirt_shut` from a
  model's own shape, `vary` patches, `age`;
- where they lie is said by causes (leaks, a Distribution's laws turned
  into a `wear_field`, the shape), so no two stretches are alike and the
  same pattern of stain never repeats with the tile.

A drag mark painted into a carpet's tile repeats every tile and cannot be
moved; the same mark as a field over the floor lies once, where it was
dragged.

### 7. Dressing: a person's story, and density

- **Where he put it, not where chance did.** A person's room is not spread
  by a law: slippers stepped out of at the futon's foot, the charger run
  from the outlet by the pillow, the bin by the desk with the paper balls
  that missed it, the week's cans gathered by it, printouts slid off by the
  chair, figures on the PC, mugs along the sill. A blue-noise scatter of
  papers and cans read as noise. The laws are for what no one placed: the
  floor's wear where he walks most.
- **Density at every scale.** The reference's realism came as much from
  its count of things as from its light: every surface built up - the
  sill, the desk's ends, the top of every shelf, under the desk, the floor
  by the walls. Large things first, then the middle, then the small (tape,
  pens, bottles, chargers). Count, and leave some floor bare on purpose.

### 8. Steady before rich: what a player sees, not a still

A still frame hides what a walked one shows. Before calling a look done:

- **Time it looking every way**, on a quiet card (the minimum of a few
  runs; a shared card's numbers swing twofold): toward the window, at the
  desk, at the back. Here traced light cost nothing looking away and 18 ms
  looking at the window - a frame rate that swung with where one looked. What
  it bought (a weak sun's shadow, a few reflections) no one could tell from
  the maps' and the probes'. Ask for what is traced only where it pays.
- **Mirrored: captured before traced.** What glossy things mirror is best
  captured (`reflect.captures` 1, README *Reflections*): each probe box's
  room seen every way, read where what is mirrored stands - steady by
  making, as the probes are, and costing the frame a few texture reads. In
  the hotel at 1080 x 1920 it put the lit room into the leather, the wood
  and the pool on the floor (a level or more on a twentieth of the picture)
  for some 0.4 ms of the scene's pass; tracing what is mirrored besides
  (`trace` 1, `trace.mirror` 0.4) changed a twentieth of a level on the mean
  and cost some 4 ms. Trace only where a smooth surface must show what a
  capture cannot hold: what moves, or a thing standing far from the walls
  in front of a mirror (a capture puts all it sees on the room's box).
- **Turn fast and walk**, and look for what has no history yet: traced
  light from all round (`trace.gi`) starts each newly seen pixel from a few
  rays - blotches on every turn. The probes are steady by making; give a
  small room smaller boxes (`probe_cell` ~1.3) rather than tracing it.
- **Hold still and measure the flicker** (frame-to-frame change with
  nothing moving): film grain re-drawn each frame is noise in every shadow;
  a soft lamp's traced shadow at one ray a pixel speckles; fine geometry (a
  blind's slats) needs `msaa` beside `taa`.
- **Read input with `Window::poll()`**, and test it with the keys, not a
  scripted walk that goes round them.

### 9. The order that worked

1. Shell, window, blinds; the window as the light; the camera's grade and
   exposure - measured against the reference.
2. The big furniture and its placement, from six sides.
3. Materials at real albedo, dirt where it gathers, roughness that varies.
4. Traced bounce and occlusion.
5. Dressing, by the person's story, densest last.
6. Every change: six views, the reference frame, the numbers. Then the
   game on the PC, walked.

### 10. The settings that worked (start from these)

The dev room's, each found by measuring; a new place starts here and moves
only what its own light asks for. Those that are how the picture is made
(not its grade) are the engine's `photoreal` style: lay it under the
place's look - `sg::gl::apply_style(look, &place, "photoreal")` - and set
in the look only the grade (exposure, white balance, saturation, fog) and
what this place does differently.

| What | Where | Value |
| --- | --- | --- |
| probe boxes | the place's `probe_cell` | 1.3 m for rooms and corridors (2.5 is the house's: too coarse) |
| air | `realms::Air` / `house::Mood` | fog density 0.0015, ambient 0 (the lamps and windows light it), `adapt` 0 |
| exposure | `air.exposure` | 1.1 by day, 0.7 by night - set by the frame's percentiles (section 1) |
| grade | `air.saturation`, `air.tint` | 0.78; day (0.94 1 1.04), tungsten night (0.74 1 1.42) |
| film | `uBloomStrength`, `uGrain`, `air.vignette` | 0.5, **0** (grain re-drawn each frame is noise in every shadow), 0.42 |
| lens | composite `uBarrel`, `uFringe`, `uTonemap` | 0.06 (a wide lens's bow), 0.8, 1 |
| contrast | `uGradeContrast`, `uGradeShadows` | 0, (0.006 0.008 0.012) |
| sharpness | composite `uSharpen` | 0.6 - TAA/DLSS soften; a camera's processing gives the detail back |
| contact | composite `ao`, `ao.radius` | 1, 0.25 |
| traced light | `trace`, `trace.lamps`, `trace.gi` | all 0 in a walked place (section 8); probes and maps light it |
| reflections | `reflect.captures` (scene) | 1: what is mirrored captured from the probe boxes (section 8); 16.8 MB for 24 boxes, a second at warming for a 46 m hall and its lounge |
| antialiasing | `Quality` | `taa` + `msaa` 2; or the card's upscaler (`upscaler = dlss`/`dlaa`, Vulkan, README *The card's upscaler*) from a conf beside the program |
| walked eye | camera `fstop`, `shutter` | 0 (sharp, as an eye), 0.5 (blur as it turns); the look eased (~0.05 s) |
| a shot's eye | camera `fstop`, `shutter` | 2.8 focused on the subject, 0.5; held by a hand (millimetres) |
| a window's light | spot lamps `light_from` its pane | ~100 lm at exposure 1.1, `size` 1.6, aimed (0 -0.75 1), `range` 7 |
| the sun | `sun(...)` | strength ~0.2, `size` 2.5 - a few soft bars, not the room's light |
| a lamp's | a spot or lamp `light_from` its shade | tungsten 2700-3200 K; its shade `emissive` so the lamp reads lit |
| loose things | the place's `loose_settle` | 1 - they fall to rest at the start, never hang where placed |
| materials | `<name>.mat` beside a photographed set | `texture.tint_*` to the thing's real albedo (paint 0.75, paper 0.5, leather 0.03), `dirt_layer`s from photographs |

### 11. Errors already met (do not meet them again)

Each of these was a mistake the engine let through. Where it could, it now
makes the mistake impossible; where it cannot, it says it - a law that
fails (`sg::verify`, the engine's watch), a fault when the graph is
validated, or a line `GLWorldView::prepare` returns when a world is first
drawn (every program prints them: read them) - and a test holds the guard.
"-" is a lesson with no guard yet: keep to it by hand.

| What was seen | Why | What does it | Guarded by |
| --- | --- | --- | --- |
| a room lit as if its window were shut | a frame or blind made as one model has a box the size of the opening | `covers` 0 on frames, blinds, lattices | - |
| hard stripes across a far wall | window lamps aimed level throw the slats | aim them down; the sun was not the cause | - |
| still frames that crawl, shadows "redrawn" | grain; traced lamp shadows at a ray a pixel; TAA blending a fixed amount | grain 0, `trace.lamps` 0, the engine's age-weighted TAA, `msaa` 2 beside it | the `photoreal` style (`apply_style`) |
| blotches on every fast turn; frame time that swings with where one looks | `trace.gi`/`trace` start each newly seen pixel from a few rays | trace off, finer probes (`probe_cell`) | the `photoreal` style |
| moiré on blinds and fine slats | too few samples on thin repeating geometry | `msaa` 2 + TAA (or DLSS/DLAA); roughness ~0.55 on the slats | - |
| things hanging in the air | loose things start asleep where placed | `loose_settle` 1, and place them on what they stand on (measure the top) | - |
| props 5-10 cm off a desk, a lamp inside a fan | judged from one eye | nine eyes, every scale (section 2) | - |
| leather or plastic reading as grey plastic | a generated model's one flat roughness | roughness made from its own normal and colour maps (section 5) | - |
| a screen that reads as a sticker | a picture on the glass | the state embedded on a portal (`crt`, `bulge`, `halo`), measured to the bezel | - |
| no keys, no mouse in the program | its loop called `Window::pump()` | `Window::poll()` each frame; time from the clock, not a fixed step | - |
| the view jumps when the mouse is taken | the cursor's jump to the middle read as a turn | nothing: the frames a capture starts or ends read no movement | impossible: `Window::capture_mouse` (`sg_guards_gl` mouse) |
| "no element ..., no material ..." on the PC | the Windows build's asset paths cached as `PATH` (made relative to this server) | configure `-DMEMLEAK_ASSETS_DIR:STRING=S:/...`; the game's sync refuses an exe with server paths | the game's `devroom_sync.py` |
| missing textures on the PC | generated and fetched assets are not in git | send every file the program opens (traced with `strace -e openat`), not only the exe | the game's `devroom_sync.py` |
| Windows build stops on `setenv` | MinGW has none | keep a flag in the program, not in the environment | - |
| lamps that light nothing | the game kit's `omni` was a bounce-only fill (`indirect` 1): where the probes hold, a fill gives way to them, and they take no light from it | a bulb is a real lamp: `spot` (or `r.lamp` without `indirect`), `light_from` its fixture, the fixture `cast` 0; the kit's fill is now called `fill` | prepare: "lit by nothing" (`sg::lamp_faults`; `sg_tests` lamps that light nothing) |
| a lamp's own shade shadows it | a lamp inside a fixture that casts | the fixture (shade, globe, crystal) `cast` 0 | prepare: "stands inside ..., which casts" (`sg::lamp_faults`; `sg_tests`) |
| no textures at all in a new program | its `main` never bound the worn textures | nothing: the renderer binds what the graph says is worn (`prepare(StateGraph&)`), and again as the graph changes | impossible; a const graph prepared says "nothing bound it" (`sg_guards_gl` worn) |
| things half inside the wall | `kit::place` stands a model with its footprint's middle where it is put | `Room::stand_off_walls` (the game's `put_on_wall`): its back on the wall's face; `in_wall` 1 for what is set into it | prepare: "stands ... m into the ... wall" (`sg::into_walls`; `sg_tests` things stand clear of walls) |
| a frieze and a clock metres off their walls in a hall and a den | the living room's kit placed on walls by the living room's size (`lr::W`, `lr::D`), not the room's | the kit reads the room's own `room_w`, `room_d` | impossible (the game's `lr::Room::on_wall`, `wall_point`) |
| a picture on a thin card shows a corner of itself, or not at all | a thing's `skin` is six cells, a face each | nothing for a picture (a Surface2D): a card wears it whole on each face; a texture's painting stretched over six (`per_cell` 0) - say `per_cell` 1 | impossible for a picture (`sg_guards_gl` skin); prepare: "is a card, and wears ..." for a texture |
| a `Texture`-worn picture will not cut out | a texture's fourth channel is its height, not an alpha | a model with the picture as its map (its alpha), or the picture itself worn | prepare: "says `cutout`, but wears ... a texture" (`sg_guards_gl` cutout) |
| a pane papered with a picture, seen through | a recipe's part of material `glass` is glass by its name | `glass` 0 on what is not glass | prepare: "is glass ..., yet wears a picture / glows" (`sg_guards_gl` cutout) |
| a portal smear walking into the next room, a freeze as it is entered | two rooms drawn as views of each other, the next room first made as it is stood in | `sheaf` 1 on both doorways (one space, sg::sheafify); `view.warm(every place)` at start | - |
| a black hole over a doorway once rooms are one space | the lower room behind a taller wall: the wall over it is not drawn | a wall of the taller room's own over the opening (a gable) | - |
| flicker at the jambs of a doorway made one space; a wall standing in the next room | each room's walls are `wall_t` outside its floor: doorways on the floors' edges put each room's wall in the other | stand each doorway at the back of its wall, so the rooms meet back to back (or say `differs walls`) | law: `laws::overlaps` "the walls of its two rooms stand one inside the other" (`sg_tests` seams are seamless) |
| a joined room drawn black past a distance | the eye's room's `far` shorter than the room joined to it | nothing: a space made one is drawn to the far end of every room in it | impossible: `GLWorldView::far_of` (`sg_guards_gl` far) |
| flicker where a plug in an opening meets a door's leaf | two drawn surfaces of one size in one plane: the smaller-is-seen rule cannot part them | stand one back a few millimetres, or leave one unseen, or `depth_layer` | prepare: "have a face each in one plane" (`sg::coinciding`; `sg_tests` surfaces that fight) |
| the game ended mid-play by the hand | a port sent to a place that declares none (an exception) | declare it (`port <place> <place>.near` in the program's `.sg`) | a send to an undeclared port is refused and said, never thrown in play (`Engine::send`; `sg_laws`); the game's `Hand::missing_ports` at start and in its laws |
| paper falling as a board | a readable paper made a rigid block | a sheet: `loose_note`, or `loose_paper` (sheet and document at once) | impossible: the game's `loose_thing` makes a paper-thin thing a sheet (its laws) |
| the walker 317 m outside its hall | a number named like another (a window's `W` for the hall's) | - | fault: "its eye stands ... far outside the room" (`Spatial3D::faults`; `sg_tests` walker stands in its room) |
| a 46 m hall's light from all round coarse | the scene shader takes so many probe boxes (now 24) | a smaller `probe_cell` only where the boxes are not clamped | prepare: "coarser than asked" (`sg_guards_gl` probes) |
| blood up a wall to the ceiling | a floor's `wear_field` read on walls too (the engine now keeps it to a hand's breadth up) | causes on walls are leaks and placed marks, by story | impossible (the engine's wear field) |
| frame times that swing 2-10x between runs | the card shared (Trellis, other renders) and its memory full | measure on a quiet card: ask helpers to pause, ComfyUI `/free` | `tools/quiet_time.sh`: refuses to time on a busy card, best of three |
| a program's lines cut away by an edit | a replace whose end anchor lay past another block | after any large edit, grep that each block is still there | - |
| two rooms joined at a doorway, yet walked into a wall at it, the lens focused on the wall's line across it, the room beyond blurred | joined only for drawing (`sheaf` on doorways): each room kept its own walker, solids and rays, and the walls' filler in the opening stood for the walker | the DSL's `space <name> = sheaf <rooms>` (`sg::make_space`): the rooms made one room - one walker, physics, rays, look, clock; the doorway a hole | `sg_space` (one state, the hole walked and seen through, bad cases refused); `sg::space_faults` in `prepare` (a hole no walker passes) |
| the ceiling black between its beams | a recipe built under the name `ceiling` took over the room's own ceiling (a recipe takes up an element of its own name, to rebuild) | name recipes for what they are (`cornice`) | the lab's recipe builder refuses a part named as something another maker made, saying which |
| a silver tray on the floor a blinding white blob | the tray mirror-polished: the far bulb in it, a glint flooding the floor reflection's blur | silver in a place run down is tarnished (roughness 0.3-0.45); a reflection's blur weighs each texel by 1/(1+brightness), so a glint does not flood it (`glint_down_fs`) | the engine's reflection mip chain (`GLWorldView::glint_mips`) |
| the whole ceiling and the floor boards flickering, frame to frame | rooms made one room did not say they were closed by their own walls (`enclosed` 0): the renderer's box round the room drew a floor and a ceiling in the very planes of the room's own, the two fighting | `make_space` keeps what the rooms said of themselves (`enclosed`, pull, floor, far) | `prepare` names a room that has its own floor or ceiling where the renderer's box has one; `sg_space` |
| the ceiling glaring | fresh ivory paint (albedo 0.6) in a room lit low: it gives the lamps back | old plaster is smoke-yellowed (albedo ~0.27) and grimed | - |
| the floor flickering under the arch between rooms made one | each room's floor and ceiling run on 0.4 m past its floor under its walls (so no light leaks under them): back to back, the two lay in one plane there | `make_space` ends each room's floor and ceiling at its walls' outer faces | `prepare` names two slabs of a size in one plane that overlap (`sg_space`) |
| blood blocky after rooms were made one | their wear fields laid again as one picture, too coarse and read pixel by pixel | laid as fine each way as the finest of theirs, read between pixels | - |
| blood that reads as a reflection | wet-glossy (roughness 0.38) on a carpet runner: it mirrored the lamps | blood soaked into pile and dried is matte (0.75); a dried smear on varnish satin (0.55) | - |
| a drag trail read as two paint lines, forking | strokes drawn by hand along lanes - nothing of how a body smears | the field law `drag` (a wipe: smear, streaks by what touched the floor, depletion and reloading, drips, the dragger's prints) on a field fine enough for streaks (`field_picture` 512 x 4096 over the hall); the field given an edge so streaks are blood or none (the floor's damp layer is a threshold on its own photograph) | look at the field itself (`HOTEL_SAY_WEAR=file.png`) before the render |
| a drag trail still weak, too ordered; cut off at the arch | a smear made of noise reads as a pattern; the hall's field ended at its own floor | the smear photographed: Flux draws wipes of blood on a grey floor seen from above (`tools/hotel_drag.py`), the floor keyed out by how much redder the blood is, laid end to end down the body's way overlapping a third (`sg::strip_picture`); one way, from where it lay in the lounge, the field over both rooms; covering blood a film (`damp_cover`), so a thin smear is seen through; a pool given an edge | - |
| a start of many seconds | derived data made again every start: the reflection captures (1.75 s of the hotel's), a wear field's picture evaluated pixel by pixel on one core (1 s) | keep what is derived on disk by what made it (`sg::cache`); share a picture's rows over the hands (`sg::work`) | each phase of a start timed and written down (the hotel's `build/out/hotel_start.log`; `HOTEL_LAPS`): look before guessing |
| a wall lamp's light not from its bulb: the wall behind it dark, a half moon on its back plate | a spot 2 cm in front of the bulb, pointed out of the wall | the light at the bulb (from the model's own bulb part), every way | `lamp_faults` (said by `prepare`): a lamp more than 3 cm from the bulb of the fixture it is in (a part of material `bulb` or `lamps...`) |
| the ceiling over a chandelier black; the wall over a sconce dark | a lamp that says no cone is not all round: the renderer gives it one, down, 66 degrees - a "bulb" made with `lamp()` lit only below it | a bulb is the lab's `Realm::bulb(name, at, lumens, colour, size)`: every way, lumens as a bulb's; `lamp()` is a ceiling lamp's light, down, and says so | the API: `bulb()` and `lamp()` say what each is |
| a room's light from all round ignored two thirds of its boxes in its second bounce | the relight shader's arrays held 8 while the engine handed it 24 | one number, `gl::kShaderProbes` | `sg_shader_counts`: every shader's arrays held to it |
| two failing lamps flickering as one | the flicker seeded by where each lamp stands on its fixture - the same for every fixture alike | seeded by where it stands in the room | the game's `Place::haunt` |
| measuring a flicker found one that was not there | the frames taken in the first seconds, while the room's light from all round is taken again once it has settled (~5-6 s) | measure from 10 s on, the hand and the noise off (`HOTEL_SHAKE=0 HOTEL_NOISE=0 HOTEL_DUMP=... HOTEL_DUMP_FROM=12`) | - |
| running in a room made of rooms at 12 m/s, a sprint through walls | a realm took "no box round it and no part named `ceiling`" for open sky - a room made of rooms says `enclosed` 0 (its own floors and ceilings, no box to fight them), and its ceilings are named for its rooms | open sky is nothing over the walker, looked for (a ray up); a place's run is its own pace eased in and out (the hotel's: walk 0.9, run 2.3 m/s, over half a second) | `realms_laws`: hurrying, fast under open sky, a run under a roof of any name |
| the notes on the floor could not be taken up | the hand took only what its one ray met: a sheet flat on a dark floor, 2 m off, is a thin mark, and nothing on the screen to aim by | nothing on the ray: the loose thing nearest it within 8 degrees (the game's `Hand`) | try the hand by script before saying it works (`HOTEL_CLICKS`, `HOTEL_INSPECT`, `HOTEL_SAY_BODIES`) |
| a page held up to read, white, its ink gone | the page `undim` under a dark place's exposure: a lit sheet past white | where the exposure is for the dark, the page dims with the room as it comes close (the eye taken in by it) | look at a page read in every new place: its paper under 230 of 255 |
| a seam to another place from a room made one: "no element hall.to_room2 in state hotel" | the notation glued every seam before it made any space | a seam at a doorway a space took in (`seam hotel.hall.to_room2 <-> ...`) is planned after the space is made | `sg_dsl`: its rooms' seam before the space, the space's own after it |
| a door taken into a room made one: nameless, its events said by a room that is no more | a place named its doors by its own prefix (`hotel.` + ...), and the room that said its events was removed | a door is named by its doorway's id less the place's prefix where it has one (`hall.to_room2` in the hotel); the one room says the events (`Place::door_said`), the .sg names them `hotel.hall.to_room2.hold` | - (the hotel's walk through it) |
| nothing loose in a room made one ever moved: a bottle taken up stayed on its table | `join_bodies` carried a body's parts to the things of the place named for their room (`lounge.` to `lounge`): the one room is named `hotel` | a part goes to the thing of its own name when the place has it | said at start: "! <part>: a part of <physics> that no place's thing is carried to" |
| a set showing a place (a tape, a camera) black in the hotel | a picture of a place is shown as light at the brightness it was seen with; the hotel's lamps are five times as many lumens, for an exposure of 0.2 | a screen is a light of its own, in its place's light scale: the panel's `bright` (the hotel's set: 1 / its exposure). A window says nothing: what is beyond it is seen with the viewer's own eye | `sg_deep_views_gl`: a window in its own look shows what a view through it shows |
| a set switched off still showing its snow | its signal's embedding follows the set, opened again by the engine whatever the set said | off, the lit glass is not drawn (`unseen`): the set's own dead tube is seen (`Place::screen_shows`) | - |
| E at a deck did nothing | the deck's model was not solid: the eye's ray went through it | whatever is to be used is solid, or has a hand's pad (`reach`) - here the nightstand round the deck takes the tape too | `Place::use_faults`, said at the hotel's start; the game's laws list theirs (43, to settle) |
| shapes flat against what they stand on; folds of velvet with no depth | the ambient occlusion was hemisphere taps within 0.3 m: its buffer averaged 0.94, next to nothing in a crease | GTAO (Jimenez et al. 2016, as XeGTAO): horizons searched each way in a few slices, the cosine-weighted visibility integrated between them; noise turned each frame where frames are averaged; light bounced inside a crease (`ao.bounce`, 0.3); the styles' radius 0.6 m (`ao.radius`, `ao.slices`, `ao.steps`) | `sg_ao_gl`: a box's foot and the corners deep, the open floor open |
| an open floor greyed toward the frame's edges (10% at its corners) | GTAO's slices turned evenly on the screen: off the middle the eye's ray leans, and even on the screen is not even round it - a bias, not noise | each slice a direction square to the eye's ray, turned evenly round it, stepped along where it lands on the screen | `sg_ao_gl`: the open floor's darkest 1% over 0.92, anywhere in the frame |
| AO looked at through `ao.show` read wrong (nearly white) | the picture shown goes through the camera's film after: exposure, its adapting, the curve, the lift | read the occlusion itself: `GLWorldView::occlusion` (the hotel: `HOTEL_SAY_AO=file.png`) | - |
| most of a room's lamps lit through everything | a room's own six strongest only were given shadow maps - Room 2's thirteen | twelve, while the 24 layers last (a spot takes one) | `SG_SHADOW_PICK=1` says which lamps cast |
| what stands lit straight from above still sits on the floor | occlusion darkens only the light from all round; a downlight's own shadow falls under what it lights | - (to come: contact shadows for the lamps' light, a short march along the depth buffer toward each) | - |
| a simple room at 24 ms a frame on a 4080 (the budget is 15) | each pixel weighed every probe box (24) three times over - the light given, the light mirrored, the captures - a turn worked out for each; a floor's own mirror drawing the room again for nothing to be seen in it (8 ms); every downlight asked by every pixel (its ball, not its cone) | the boxes that reach each cell of the floor plan in a map (`probe_index`, at most four, else all): 24 to 12 ms, the same picture to a level; a spot's cells by its cone; no planar mirror where the room's captures already show what the floor would | `tools/frame_budget.py`: every eye of a place under 15 ms at 1080p, or it fails; `SG_PROBE_INDEX=0` and a diff: the map changes nothing |
| a frame's time in passes no timer named | the card's `feeds` and `views` were timed but never printed | print every pass (`HOTEL_TIMES`); profile by median over 30 s, never one reading (the same eye read 19 to 29 ms) | - |
| the light from all round wrong where a view was traced | the probes' map was given texture unit 28, the tracer's (`kSceneGi`) | unit 30, its owners named beside it | `sg_gi_gl` |
| a "faster" shader 70% slower | a list of the boxes kept per pixel in a local array, indexed as it ran: on the card, out of registers into memory | candidates from a texture, each a uniform's index; never a local array indexed by a variable in a pixel's shader | - (measure every change: `frame_budget.py`) |
| a button too small to aim at | a pad the size of the button, inside the model's own box | a pad a little larger than the button, standing a few centimetres proud of the face | - (try it by script: `HOTEL_USE`, `HOTEL_SAY_USE`) |

### 11a. Blood, dirt and story

Wear is where something happened, not everywhere it could be. A body dragged
leaves trails on the floor (Distribution `stroke` laws into the floor's
`wear_field`) and stops where it was taken; a hand put to a wall leaves one
print. Drips down every wall read as decoration, and repeat. A stain that
covers (blood, paint) says `damp_cover` 1; one that runs says `damp_runs`
1; pooled it lies flat and glossy, drying matte at its edge (`damp_rough`
~0.35-0.4). Clues the player can read are papers (books::Document, a text
file of the place's papers), lying where the story dropped them.

### 11b. A camera's look: P.T.'s (video in a dark house)

Photoreal is not one look: it is a camera. P.T. (references in the game's
`reference/hotel/pt_*.jpg`) is a cheap digital video camera walked through a
dark house, and it reads as real for that reason. What makes it, read off its
frames and from those who rebuilt it (a Unity recreation stacked AO, noise
and grain, fisheye, chromatic aberration, fog, AA, focus that follows the
gaze, flares - killscreen.com "who said pt was dead"; a reconstruction of
the Fox Engine shaders lists depth of field, motion blur, bloom, grade,
grain, flares):

| What | How (the hotel: `HOTEL_LOOK=pt`, the default) |
| --- | --- |
| the sensor's noise, not film's grain: per photosite, its colour a little coarser, strongest in the shadows, alive at 30 fps | the engine's `video` style: composite `uSensor` 0.06 0.45 1.5 (amount, colour share, colour blotch px), added to the light as exposed before the curve, its spread Poisson-Gaussian (variance = a light + b: Foi et al. 2008); `uGrain` 0 - film grain laid over the print is the tell; at 1.8 px blotches it read as a dirty overlay |
| highlights clip as video's do | `uTonemap` 0 (the harder curve), not film's long shoulder |
| the exposure finds itself | `exposure.auto` 1, key 0.05, a stop either way, 0.7 stops a second - a camera's, not an eye's: the dark stays dark |
| a wide cheap lens | `uBarrel` 0.14 (the hotel 0.2), `uFringe` 2.2, `uVignette` 0.95; nothing sharpened (`uSharpen` 0) |
| bulbs blown to white, blooming wide | bare lamps, `uBloomStrength` 0.9 (the hotel 1.3); exposure set so the wall by a lamp clips. A program that sets the bloom again (the hotel's reading hook dims it under a held page) must start from the same number as the look - one function for both - or it silently undoes the look's |
| the grade is the place's, not P.T.'s | P.T. is drained because its bulbs are white; a hotel under tungsten keeps its warmth (saturation 0.86, tungsten balance) - take the camera, not the colour; the darks dark, contrast 0.35 - but not a flat black: the sensor's floor a hair lifted (`uGradeShadows` ~0.006; 0.02 read as grey), its noise visible in it; and the darks' colour gone before the lit's (`uDarkChroma` 0.5-0.6, the `video` style's 0.6: few photons, the colour goes first - a saturated shadow reads as rendered) |
| dark between pools of light | the overhead lights out but one at the end, a table lamp on, half the sconces dead: a light at the end of a dark hall. Not too dark: at mean 6-13 (90% of the frame under 20 of 255) it read as a dark game, not footage of a dark place - the hotel now exposes at 0.2 (mean ~20-25, under 20: ~65%) |
| measured | P.T.'s frames: mean 14 (dark hall) to 104 (lit: median 99, 10% over 200), saturation 0.15-0.35, near-neutral RGB; the hotel in it: mean 49, saturation 0.25 |
| what reads as real (the 2026-10-21 look round) | Rademacher et al. 2001: soft shadows and rough surfaces raise it, more lights or objects do not; bodycam games (Unrecord): a camera's limits - blown windows and lamps, bloom, fisheye, noise; P.T.: a light that clips with a hard shadow on a glossy floor, pale walls that carry one bulb's bounce, the detail an arm's length away. Proportions read at once: a 7 m gabled roof over a 3.4 m hall read as a church, not a hotel (now a flat plaster ceiling, cased beams, at 4.6 m) |

The walk, from walking studies (the head moves at the step's rate,
Woodman & Griffin; a few centimetres up and down - 5 cm at 2 Hz is a common
model): a slow walk, 1.1 m/s in 0.65 m steps (about 1.7 a second); the head
rising and falling 3.5 cm a step, swaying 2.5 cm side to side a stride,
under half a degree of roll; its pitch held nearly still (the eyes hold the
view; Moore and Hirasaki: the head rises at the step, ~2 Hz, sways at the
stride, ~1 Hz, the gaze held by a nose-down pitch with each rise); the look
carried with weight (eased ~0.09 s); starts and stops over a
quarter second. Running is a different gait, not a faster film: longer
steps, more throw, a lean, a wider view, a longer shutter. Focus where the
eye looks (a ray to what is in the middle, eased in dioptres over a quarter
second, f/1.8), and sharp all over while reading.

And the camera's finishing, in the `video` style: a rolling shutter
(`uReadout` 0.018 s: a fast turn leans the verticals, as a phone's does),
corners softer than the middle (`uCornerSoft` 0.6), colour at half the
pixels (`uChromaHalf` 1: video's 4:2:0). The hand's own unsteadiness under
the walk: three octaves of smooth noise on yaw, pitch and roll (a body's
drift, the arms', a tremor), a few tenths of a degree, more while walking -
never one sine (a repeating wobble reads as a machine). And the phone's
sharpening halos (`uSharpenHalo` 0.6: an unsharp mask left unclamped); the
edges' smear (`uEdgeSmear` 6: each point drawn out along its line from the
middle, red outward, blue inward - the aberration a phone's eye sees, where
two shifted copies, `uFringe`, barely read on a small screen) - they
cost a little detail, and read as video. The colour a little drained besides
(saturation ~0.78): even under tungsten, a cheap camera's colour is thin.
A lamp or two flickering (`Place::flicker`, on the place's own time), the
rest dead.

### 12. A new place, in order (what to copy)

1. **A program of its own** beside the game's (the dev room: `tools/devroom.cpp`
   + `tools/devroom.sg`): the place's states made in C++, everything that
   joins them in the `.sg` (`drive time -> ... keeps always`, the loose things'
   embedding and ports, a terminal's). Its `main`: the game's
   `memleak::World` graph, `give_bodies` / `join_bodies` / `walk_among`, the
   hand; a conf beside it read with `load_quality` (and Vulkan asked for
   when it says `dlss`); `Window::poll()`; real time; a log every second.
   Shots and a fixed eye by environment variables, as the dev room's.
2. **References** in the game's `reference/<place>/`, measured (section 1).
3. **Shell and its light first**, the settings above; nine eyes and the
   numbers before anything is dressed.
4. **Materials**: photographed sets fetched (`tools/fetch_textures.py`),
   made where none fits (a printed pattern: a script or Flux), all clean;
   a `.mat` each, tinted by measurement; the wear laid as layers (6a).
5. **Props** by the three roads in their order (section 6: found CC0,
   built by hand, generated last), each checked for size and blotches.
6. **Dressing by story**, then loose things given bodies.
7. **Steady**: time it looking every way, turn fast, hold still (section 8).
8. **To the PC**: the Windows build, then the sync (exe, every asset it
   opens, the upscaler's file and the conf beside the exe).

### 13. A place from a story (what is said, what is done)

What the user gives: the story (what happened there, who was there, what
is left), the layout (rooms, their sizes, which are one space and which are
reached through a door - another place), the camera (the `photoreal` or
`video` look, section 11b) and what can be done (taken up, read, switched,
put in, opened). Everything after that is a step below, done by code where
code can do it; what needs judgement (choosing models, reading a picture,
placing by the story) is the agent's, by this list. Look at every result
(several eyes, section 2), measure it, then go on.

1. **Rooms**: a C++ builder per room (the hotel's `hall()`, `lounge()`,
   `house::room2`): walls with their openings (`add_opening`, a door's
   `door` flag), `enclose`, photographed materials (`wear_material`,
   `clothe`; a new one: a row in `tools/fetch_textures.py` and a `.mat`).
   A drawn pattern made a rug: `tools/room2_rugs.py` (its picture woven
   in a photographed carpet's pile).
2. **Joins in the `.sg`**: rooms that are one space, `space x = sheaf a b`;
   another place through a door, a `seam` (on the space's doorway if it
   was a room of one: `hotel.hall.to_room2`), its two `transition`s, the
   door's own state (`sg::Door`, its `hung`/`beyond`/`by` functors, as
   `tools/hotel.sg`), the places' drives and ports. The program follows
   the engine's current place (`engine.current()`), never one place.
3. **Props, by their roads** (section 6): Poly Haven at 2k for a hero
   (`tools/fetch_models.py <assets> --res 2k <id>`; measure it by ray in
   Blender before placing it); built by script in Blender with
   photographed finishes (`tools/gen/hotel_build.py`: a builder, its
   `info.json` saying where its working parts are - a deck's slot, a
   button - so the C++ places by numbers, not by eye); Trellis last.
4. **What can be done** - the machinery, each a few lines:
   - taken up: `house::loose_thing` (a model's parts carried by its body);
   - read: `clue` / `house::loose_paper` (a page of `assets/books/<x>.txt`);
   - a door: `door_way` + `door_leaf` on a hinge anchor, `sg::Door` in the .sg;
   - switched: `circuit(name, lamps, glows, pads)` - a pad (`reach`) per switch;
   - a screen: `Room::tv` (its signal embedded in the .sg; `bright` 1/exposure
     in a dark-exposed place), on its circuit (off: the dead tube);
   - a tape and its deck: the deck's `use` `tape:<glass>` and its slot from
     the deck's place (`tape_x/y/mouth_z/in_z/yaw`), `memleak::Deck` (hand.hpp)
     in the loop; what the tape shows: `memleak::tape_of` (a place's cameras);
   - sound for each: a CC0 recording (a row of `tools/sounds.py`'s HOTEL,
     its Freesound page checked for CC0), cued where it happens.
5. **Light and camera**: sections 3, 10, 11b; a lamp at its fixture's bulb;
   every door the player comes to lit.
6. **Try every action by script** before saying it works: `HOTEL_STAND`
   (`room2:` too), `HOTEL_STAND2` (a second look in the same run),
   `HOTEL_USE` (E at frames), `HOTEL_CLICKS`, `HOTEL_INSPECT`,
   `HOTEL_SAY_USE`, `HOTEL_SAY_BODY`, `HOTEL_SAY_SOUND`, `HOTEL_TAPE_AT`.
   Read what the program says at start (`!` lines: nothing to be used out
   of reach, no part carried nowhere, no lamp off its bulb).
   And the frame budget: `tools/frame_budget.py` at an eye in every room,
   looking along it and into its doorways - 15 ms on the 4080 at 1080p,
   each over it profiled pass by pass before anything is cut.
7. **To the PC** (section 12, 8).

## A person from a video

A real or generated orbit video of a person made into a rigged, textured
character the engine reads (a skinned glTF), and that character's face
moved by speech. The tool stands beside the engine, not in it:
`~/stategine/stategine-capture` (private). The engine reads what it makes
as it reads any rigged glTF (`sg::cast`); where a character needs more of
the engine (skin normal and ORM maps, morph targets, skin shading, a
joint-name map for retarget), that is engine machinery, never a change of
its ontology. Started 2026-10-22.

### 1. The capture asked for

One person standing still in an A-pose (arms about 30 degrees out), even
soft light from all round, a plain ground, the camera circling once at
constant speed, the whole body in frame all the way round. 9:16 (a
standing person fills it), the highest resolution offered (1080x1920
ideal, 720x1280 the least for a face worth texturing), 6-10 s at 24 fps or
more. A generated video is asked the same, with a negative prompt against
walking, talking, blinking, hair blowing, camera shake, cuts and zoom.

### 2. The models (code and licence checked first)

| Stage | Model | Licence |
| --- | --- | --- |
| body model and rig | MHR, Meta's Momentum Human Rig (`facebookresearch/MHR`, assets v1.0.1) | Apache-2.0 |
| a body from one picture | SAM 3D Body (`facebook/sam-3d-body-dinov3`, gated, approved by hand) | SAM License (commercial, no military) |
| the person cut out | BiRefNet (ComfyUI's copy) | MIT |
| masks through a video | SAM 3 (`facebook/sam3`, gated) | SAM License |
| camera poses | MapAnything, its Apache weights (`facebook/map-anything-apache`); VGGT-1B-Commercial a second road (gated) | Apache-2.0 |
| normals and other cues | Sapiens2 | Meta's own (no non-commercial clause) |
| speech to face | NVIDIA Audio2Face-3D v3.0 (diffusion, `network.onnx`) | NVIDIA Open Model License (commercial) |

Kept out of the core, opt-in only: MASt3R and Sapiens v1 (CC BY-NC),
WiLoR (CC BY-NC-ND, and MANO's licence), CoRA (non-commercial; only for a
flash-lit face). Checked and passed over for speech: UniTalker and SAiD
(2024, no tongue or eyes), ARTalk (FLAME, whose licence is
non-commercial), EmoTalk and FaceDiffuser (no clear licence).

### 3. Where things are, and the smallest footprint

- Its venv `.venv` overlays ComfyUI's `venv2` (Python 3.12, torch 2.9.1
  cu128) by a `.pth` line: only what venv2 lacks is installed into it (14
  MB). Never put numpy 2 into it - venv2's compiled modules (onnxruntime
  and others) break.
- MHR's assets: `~/stategine/downloads/mhr/assets/assets` (4.5 GB;
  `MHR_ASSETS` names another). Weights the tool fetches: `weights/`
  (gitignored), only the files used (SAM 3D Body's checkpoint and config,
  not its second copy of MHR).
- SAM 3D Body's code: `third_party/sam-3d-body`, run with our own box,
  mask and camera - no detector, no field-of-view model, no detectron2.

### 4. MHR, as the tool uses it

- Run through its TorchScript model (`mhr_model.pt`): skeleton, skinning,
  UVs, identity and expression shapes and pose correctives are all in it,
  and it runs on any torch. pymomentum is not used: its 0.1.114 wheel
  segfaults beside torch 2.9 (`Character.with_blend_shape`).
- 127 joints (`body_world`, `root`, ... fingers, twist bones, jaw, teeth,
  tongue, eyes), at most four weights a vertex (the engine's four), a real
  UV atlas (a large face square, hands, body, legs, feet), 45 identity
  shapes (20 body, 20 head, 5 hands), 72 FACS expression shapes (linear:
  glTF morph targets as they are). Seven levels of detail; LOD 1 (18,439
  vertices) the default.
- Centimetres, Y up; a joint's state is [t, q xyzw, s], a point goes to
  t + s R(q) p. Model parameters: 0-129 the pose (root and joint angles),
  130-203 the body's proportions (the `*_length/width_flexible` lengths
  and `scale_*`) - a person's, fixed for every clip of them.
- Bound in MHR's zero pose at the person's proportions, without scale on
  the joints (exact for a body whose proportions do not change; within
  1-3.5 mm of MHR's own vertices, checked by evaluating the glTF in numpy
  and through Blender's importer). Pose correctives are not exported.

### 5. The steps, in the order built

1. One picture -> MHR -> glTF: `tools/image_to_character.py photo out.glb`
   (BiRefNet mask, its box, SAM 3D Body, `capture/export.py`). Writes
   `out.json` beside it: identity, proportions, the picture's pose.
2. Clips: retarget Mixamo-rigged motion (Bruce's) onto MHR's skeleton;
   motion from a video read by SAM 3D Body frame by frame (MHR already,
   no retarget), smoothed.
3. The video fit, all in `tools/video_to_character.py work/<name>/orbit.mp4`
   (each stage kept beside the video, skipped when there): BiRefNet masks
   and SAM 3D Body on every second frame; the cameras from the body (a
   plain ground gives nothing to track: the person stands still, so how
   SAM 3D Body reads the body turned in each frame is the orbit); one body
   fitted to every silhouette (shape, proportions, joint angles shared, a
   correction per view); the face to MediaPipe's 478 landmarks; offsets
   along the normals for clothes and hair.
4. Texture: the frames projected onto MHR's 2048 atlas, each texel from the
   views that see it most squarely, unseen texels inpainted, islands padded.
5. The face from speech: Audio2Face-3D's skin motion (its own face mesh,
   24,002 vertices; also tongue, jaw, eyes) solved onto MHR's own 72
   expressions through a fitted correspondence of the two faces; jaw,
   tongue and eyes on MHR's joints. ARKit's 52 weights kept beside it.
6. Opt-in: hands (WiLoR), a flash-lit face (CoRA).

### 6. Into the engine

- A character to look at in the hotel: `HOTEL_CAST=<glb>,x,z,yaw,height`
  (the hall's coordinates, yaw in degrees, the person's real height - the
  engine fits any model to the height it is told).
- A person cast must be shown each frame by whoever draws
  (`cast::show` before `view.render`), or its skin never moves; and one
  cast where the place does not count it active needs `always`
  (`cast::person(..., why, true)`), or its clip never plays.

### 7. Errors already met

- A view's root as Euler angles wraps going round the person: smoothing
  the orbit on them tipped the body over in the back view. Correct each
  view by a small turn (axis-angle) and measure smoothness on quaternions.
- A body fitted to clothed silhouettes swells to fill them (a thick neck
  inside a hood, a jowly face on it): keep its shape near SAM 3D Body's
  reading (learnt for the body under clothes), let the offsets fill.
- Free 3D offsets fold at hair (fins, holes): push along the normal only;
  hold the face's front, hands and feet (silhouettes say nothing of a face
  seen from the front; a floor shadow in a mask is no boot); low-pass the
  scalp (hair grew two ridges otherwise).
- The face needs its own cue: MediaPipe on a crop of the head (a face 150
  px tall in the frame is too small for it whole), placed on MHR's surface
  by running MediaPipe on renders of MHR's own head.
- The atlas is drawn with MHR's v and the glTF's v is 1 - MHR's: turn the
  picture over (MHR's face is bottom-left in glTF's way). Check by reading
  the colour at a known vertex's glTF uv - and look at it in Blender before
  blaming the engine.
- With no clip the engine shows the bind pose (MHR's: arms out): export
  the captured stance as the first clip.
- ComfyUI keeps its models on the card: free it (`POST /free`) before a
  stage that needs the GPU.

