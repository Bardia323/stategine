# Changelog

Releases are tagged `vMAJOR.MINOR.PATCH`; the version lives in
`include/sg/Version.hpp`. Projects pin a tag, so every entry that can break a
project says so.

While the major version is 0, a minor bump may break the API.

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
