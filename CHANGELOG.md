# Changelog

Releases are tagged `vMAJOR.MINOR.PATCH`; the version lives in
`include/sg/Version.hpp`. Projects pin a tag, so every entry that can break a
project says so.

While the major version is 0, a minor bump may break the API.

## v0.3.0 (unreleased)

- **Rooms of any finish.** A room's floor and ceiling take `floor_surface` and `ceiling_surface` (and `ceiling_r/g/b`); a wall element takes `surface`. New materials laid in the room's own metres, so they keep their size on any surface: 10 planks, 11 concrete, 12 checker, 13 brick, 14 carpet, 15 metal plate, 16 grass.
- **The attended state's look.** `GLWorldView::attend(state)` lays the active look of the state the viewer is attending to (an interface they sit at) over every room's own, blended by the look fader like any look. `uDim` in a scene look dims everything but a CRT screen's picture - eyes adjusting to a screen held close.

- **Faster frames.** Shadow maps are kept per world drawn and drawn again only when their lamp or anything that casts has moved - most frames, none are. Each element's pose and box matrix is worked out once a frame, not once per pass. A `Key` made from a string literal is found by the literal's address (checked against its characters), with no string built or hashed; a `Key` from a `std::string` no longer copies it when it is interned already. Uniform locations are found by the name's address the same way. Nothing in the API changes.
- A CRT screen can be seen flat: `flat` (0..1) on a `crt` panel straightens its bulge, squares its corners, fills the glass with the picture edge to edge and takes the grey out of its margin and bezel, keeping its scanlines, grille and glow - someone with their face up to the tube. `crt_picture` takes the same `flat` (default 0, as before), and `crt_shape(flat)` gives the glass at any flatness.
- A CRT screen can glow: `halo` on a `crt` panel spreads the phosphor's light into the glass round it (halation), in the shader - so what is painted onto the screen can stay flat and cheap.

- **A world portal is seen from a camera of its own.** `GLWorldView::bind_world`
  takes an optional `carry` - how the viewer's camera crosses the portal (the
  seam's own travel, handed in by the game) - and an optional `back`, the far
  side's portal to leave out of the view. With a carry, the guest's own camera
  is not used, so a door and a window can open onto the same room by
  different gluings, and a room can open onto itself. Without one, nothing
  changes. `unbind_world` makes a portal a plain opening again.
- **Screens.** A world portal with `screen` = 1 is a projection: it is cut by
  no plane, it is not there when seen through another portal, and screens that
  show the same world from the same eye share one view. A room walled in
  screens can look like it goes on for ever.
- `Surface2D::resize(cols, rows)`: a surface can change size; the renderer
  makes its texture again to match (`gl::Texture::create` frees the old one).
- `StateGraph::drop_seam(name)`: two states are no longer glued.
- `State::remove_with_arrows(id)`: an element taken away with every arrow on
  it. `remove_element` still leaves them for `validate()` to name.

- **`Adjunction` is an adjunction.** It used to call F -| G whatever made the
  round trips come home, which is an isomorphism. Now a unit arrow
  `a -> G(F(a))` and a counit arrow `F(G(b)) -> b` are declared per object
  (`unit`, `counit`; `identity` names a no-op loop that stands for id), and
  `check` / `holds` verify unit and counit naturality and both triangle
  identities on the arrows, composites unfolded and identities dropped.
  `laws::adjunction` runs the same equations on live data. `unit_defects`,
  `counit_defects`, `data_defects` and `is_isomorphism` are unchanged and now
  documented as what they are: the test for an isomorphism.
- **Seams: the law every interface between like states owes.** An interface
  is a boundary in each domain and a gluing between the boundaries. A `Seam`
  (`StateGraph::add_seam`) names both boundaries (`boundary_a`, `boundary_b` -
  a doorway, a door hanging in it), glue functors both ways between them, and
  travel functors both ways for what crosses. `laws::seams`, part of `verify`,
  checks that between two states of the same kind nothing crosses one way
  (every transition or one-sided window functor must travel along a seam);
  that each glue is a bijection of the boundaries - defined on all of its
  boundary and nothing else, onto all of the other, the two glues inverse;
  that travel round trips are the identity and travel never touches a
  boundary; and that both sides *agree* - the boundary carried across is the
  boundary already there, a door swung on one side is swung on the other. That
  last is the gluing condition of a sheaf, and it catches doorways that exist
  in one room only.
  **Breaking:** a graph with a one-way window or transition between two states
  of the same (non-plain) kind no longer verifies; glue them.
- A guest open Live in several hosts is one state: when the engine steps it
  through one embedding, it writes back through every open Live embedding of
  it, so a door both rooms embed is swung in both at once.
- **No stall stepping into another world.** Ground is resampled in the
  background and the ground already there is drawn until it is ready, so
  walking across a grid step no longer stops the frame (it cost ~8 ms, and a
  doorway on a grid line cost it on every crossing). A height function bound
  with `bind_terrain` is now called while the game runs, so it must be safe
  to call concurrently. `GLWorldView::warm` draws given worlds once, off
  screen, and restores the fades, so nothing is first used mid-game;
  `set_timing` / `times()` report where a frame's time went.
- The doorway being looked through keeps its frame in the far view; only its
  own view is left out (the far room's door frame no longer vanishes).
- A doorway's `tunnel` is now exactly the opening's outline as seen from the
  eye, a few centimetres past the near plane, and only in the last few
  centimetres before crossing - the opening no longer jumps bigger as you
  close in. `tunnel_margin` is gone.
- `glue_doorway(g, name, a, pa, b, pb, also)` builds a doorway's seam from its
  two portals: `name.ab/.ba` carry the camera, `name.glue.ab/.ba` carry the
  doorway (`seam_carry`: the same doorway, facing back) and anything in
  `also` (`pose_carry`). `as_cover` now glues every doorway this way.

- **Surfaces paint themselves.** `Surface2D::paint()` is virtual; the default
  still draws the board of tiles and sprites (`paint_board`). A subclass that is
  a sheet of text, a photograph or a screen overrides it, uses the protected
  pixel helpers (`put`, `fill`, `box`, `pixels()`), and calls `invalidate()`
  when what it shows changes. `pixel(x, y)` reads the last raster.
- **Panels tilt.** A portal bound to a surface reads `pitch` (its face tipped
  up; `pi/2` lies face up) and `roll` (turned in its own plane). Doorways stay
  upright. `frame = 0` draws a bare sheet `thick` metres thick in its own
  `r/g/b` instead of a mounted board; `glow` and `roughness` override the
  panel's defaults.
- **sRGB surfaces.** `Surface2D::set_srgb(true)` marks painted pixels as
  sRGB; the renderer decodes them before lighting, so painted colours are not
  washed out. Off by default: existing boards look as before.
- **Surface textures are mipmapped** (with anisotropic filtering where the
  driver has it), so detailed print does not shimmer at a distance.
- **Screens.** A surface panel with `crt` > 0 is drawn as a tube, per pixel in
  the scene shader: curved glass, scanlines, an aperture grille, vignette, a
  rounded bezel. The CPU only paints what the screen shows.
- **Mesh shapes and materials.** `shape` = `cylinder` or `sphere` (same unit
  size as the box); meshes honour `pitch` and `roll` like panels; `surface`
  picks a material - 3 crate (the default), 4 wood, 5 brushed metal,
  6 moulded plastic, 7 fabric, 0 plain - and `emissive` makes one glow.
- A light with `fixture` = 0 draws no housing, for lamps modelled elsewhere.
- **Faster law checks.** Values of the same kind are compared as themselves
  rather than formatted as text, and snapshots are indexed rather than
  searched: checking a world of a few hundred elements is about 8x faster.
  Same laws, same counterexamples.
- `gl::Mat4::rotate_x` / `rotate_z`; `sg_tests` holds the panel turn to
  `forward_of`.
- **Open worlds.** A state with `sky` = 1 is drawn under a sky (gradient and
  sun, from the look's `uSkyTop` / `uSkyHorizon`) instead of a ceiling and
  walls, and `far` sets how far anything is drawn (120 m by default). A
  `terrain` element is ground from a height function bound with
  `bind_terrain`, sampled round the viewer - fine near, coarse far - on every
  core, and resampled as they move; `surface` 8 is sand turning to banded rock
  where it is steep.
- **Suns.** A light with `sun` = 1 is parallel light with no falloff; its
  shadow is an orthographic box `extent` metres round the viewer, moved in
  whole texels. Suns take the first shadow map.
- **Eight lights**, up from four. A light at intensity 0 is skipped, so a
  switched-off lamp costs nothing; a ceiling fixture's glow follows its
  intensity. Wide lamps get a shadow bias to match their width (no more acne
  stripes on walls).
- **Doorways to other worlds.** A world portal's far side is cut by the
  portal's own plane carried through (from the two cameras), not by pushing
  the near plane out, so a window beside a door shows the right slice and
  nothing behind the far doorway gets in the way. Portal views are
  multisampled, and skipped when the portal is out of view. New portal
  parameters: `inset` (where the view is drawn; 0 puts it on the plane, for
  walking through), `casing` / `depth` / `r,g,b` (the frame), `tunnel` (a
  backing quad so the last step through never shows the near plane's cut) and
  `oneway` (seen from behind, only the frame).
- **Breaking:** `portal_carry` and `through_portal` carry height as height above
  the doorway (`y - here.y + there.y`) rather than keeping `y`, so doors at
  different heights line up. Doorways at the same height behave as before.
- **The CRT's glass** is antialiased (edges spread over a pixel with
  `fwidth`), and the picture sits wholly inside the rounded glass with a dark
  margin, the way a tube's does - no corner is lost. `gl::crt_picture` maps a
  point of the glass to the picture with the shader's own numbers (`CrtGlass`),
  for pointers.
- **Softer, steadier shadows.** Four shadow maps, not two, given to the
  brightest lights rather than the nearest, so shadows do not pop as the viewer
  walks. 5x5 filtering, `uShadowSoft` (texels) wide, and `uShadowFloor` light
  left in full shadow - both look uniforms (1 and 0 by default, as before). A
  mesh with `cast` = 0 casts none: a lamp's own shade does not shadow it.
- The scene shader knows `uTime`; `uWind` drifts veils of sand over surface 8;
  `uStars` puts stars in the sky; fog brightens towards the sun.
- A framed panel's `border` sets how much board shows round it (0.15 as
  before); a world portal with `casing` = 0 has no frame at all.
- Every world portal's targets are made up front, so the first frame through a
  doorway does not stop to allocate.
- MSAA is clamped to what the driver offers (`GL_MAX_SAMPLES`); `Mesh::update`
  for meshes that change; `gl::Mat4::ortho`; `glGetIntegerv`, `glDepthMask`,
  `glScissor` in the loader.

## v0.2.0

- **Looks** (`sg/domains/Look.hpp`): how a state is shown, as a state. A
  `LookState` holds per-pass uniforms, settings and optional shader sources;
  rooms `wear` looks through embeddings and switch with `set_look`.
  `LookFader` fades between looks, GL-free, and `look_defects` checks the
  structure.
- **Renderer** (`GLWorldView`): each room is drawn in its own look, and the post
  passes follow the viewer's room. Numbers fade, and the composite pass
  dissolves between different shaders. `prepare(graph)` compiles every
  reachable look before the first frame and reports broken shaders, missing
  required uniforms and misspelt uniforms. Programs are shared by source and
  uniform locations are cached. `set_fixed_step` makes headless fades
  reproducible. `stats()` counts compiles, including late ones.
- **Shaders:** ambient (`uSky`, `uGround`, `uAmbient`) and grading (`uTint`,
  `uSaturation`, `uVignette`, `uGrain`) are uniforms now, not constants. With
  no looks declared, frames are unchanged.
- **Fades are continuous and reversible.** `LookMix` is a set of weighted looks,
  not a from/to pair. A change moves weight towards the wanted look, so undoing
  it half way walks back along the same path with no jump, and a third look
  reached mid-fade starts from the blend on screen. The composite pass averages
  every program in the blend.
- **Glued rooms are adjacent** (`adjacency_defects`, part of `descent_defects`):
  a room's solids must stay on its own side of each doorway plane.
  `PlacedRoom` carries its bounding doorways, and the scene shader clips each
  room to its own side (`room_side`), so no surface is drawn by two rooms. The
  room demo's doorway walls now stand on their own side of the plane: they were
  centred on it, and each room's look bled into the other's wall.
- `sg_looks_gl` test (needs a display); `sg_room3d` gains looks, `L` to switch
  the hall's, and `alert` / `crossing` shots.
- **Breaking:** `PlacedRoom` has a third member, `doorways`, so brace
  initialisers should give three fields. A look's custom scene vertex shader
  must write `gl_ClipDistance` from `uClip` / `uClipCount`; `prepare()` names
  one that does not. `adjacency_defects` may name walls that straddle a doorway
  plane in existing levels.

## v0.1.0

First tagged release, and the first one meant to be consumed by other projects.

- **Laws on live data** (`sg/core/Laws.hpp`): identity, associativity,
  composition, functoriality, lens put-get / put-put / settles and declared
  diagrams, each reported as a concrete counterexample. `sg::verify`,
  `sg::enforce`.
- **Typed handles** (`sg/core/Typed.hpp`): ill-typed composition of arrows,
  functors, lenses and diagrams does not compile.
- **Fixed:** the identity functor dropped objects created after it was built;
  composing an arrow with a loop registered the wrong type; two loops could not
  compose.
- **Packaging:** `stategine::stategine` alias; examples, tests and the GLFW
  download are built only when stategine is the top-level project;
  `SG_BUILD_EXAMPLES`, `SG_BUILD_TESTS`, `SG_BUILD_GL` options; `SG_VERSION_*`
  macros.
- **Breaking:** the library target no longer adds `-Wall -Wextra` / `/W4` or
  MinGW `-static` link flags to projects that use it. Link
  `stategine::warnings` and `stategine::static_runtime` to keep them.
