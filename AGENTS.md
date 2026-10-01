# AGENTS.md - working on stategine

Read this before changing the engine or building anything on it. It is short
because the idea is: **everything is a state, states are independent, and
they meet only through interfaces the graph declares and the laws check.**
Every rule below follows from that.

## The cornerstone

A state is a small category - elements are its objects, events on them its
arrows. A room, a computer's desktop, a sheet of paper, a whiteboard, a realm
seen on a television: each is a **state of its own**, and could exist with
none of the others. States are connected **only** by the graph:

| Interface | Declared with | What it is | Avoid |
| --- | --- | --- | --- |
| Transition | `graph.connect` / `push` / `pop`, on what a state `says` | directed change of active state | flags one state sets and another polls |
| Functor / lens | `add_functor`, `add_lens` | object/arrow/data transport; `sg::kan::left` / `right` can derive an ordinary functor, not a new kind of thing | copying params in a game loop or another state's arrow |
| Embedding | `graph.embed` | guest in a host portal, with `in`/`out`, subject, sync and focus | holding a guest pointer as the interface |
| Seam | `add_seam`, `glue_doorway` | bidirectional boundary identification, including a doorway and its door | hand-placed global room coordinates |
| Drive | `graph.drive` / `sg::drive` | a `Temporal` timeline fires arrows with `{dt, time, frame}`; additive drives omit `frame` | private timers, ticks or accumulated `dt` |
| Adjunction | `Adjunction` | paired functors with checked unit and counit | treating a lossy adjunction as an isomorphism |
| Port | `graph.port(state, event)` | external program/device input through `engine.send`, next frame | threads/callbacks writing into a state |
| Edit | `graph.edit(state, event, fn)` | a state's request rewrites the graph next frame and receives a reply | in-world graph rewrites from the game loop |

Anything else that moves data or control between two states is a bug, however
convenient.

## Rules

**1. A state stands alone.** Its code is its own (its own header, and in a
project its own module/directory and namespace). It may know the engine and
the modules *under* it - never a state that uses it. A realm does not know the
room whose television shows it; the desert does not know the dev room; a
computer's desktop does not know the desk it sits on. If you need a name from
above, you have the dependency backwards: add an interface instead.

**2. It is reached only through the graph.** Game-loop code must not reach
into a state to change it or to show it.
- To *act* on a state from outside, fire an event (`engine.fire`); the engine
  routes it to the guest of the innermost focused embedding
  (`Engine::focus_embed`). The state's own arrow does the work.
- To *show* a state, declare it: an embedding in a portal (the renderer draws
  an open embedding of a 3D state in a `feed` portal as a picture; a surface
  bound to a portal as a panel), a seam for a doorway. What the renderer draws
  is what the graph declares.
- To *tell the world* something, a state `says` it (`State::says`) and emits
  it; the graph's transitions take it, and so does an open embedding whose
  functor names the event (`on_event`). A state sees its engine const.
- **Subscribers may observe the world; only the world may change it.** A bus
  listener draws, prints, sounds, logs. One that fires the engine, sends a
  state an event or rewrites the graph is refused (`sg::ObserverError`). If
  an observer must change the world, make it a state and its causes arrows,
  functors, transitions.
- To *carry data*, a functor or lens. Where it runs is decided by the
  embedding's sync (`Live`, `Commit`, `View`).

**3. Behaviour is arrows.** What a state does is its morphisms (`loop`,
`arrow`), triggered by events, reading their arguments (`dt`, a step, a
pointer) and the state's params. The laws re-run arrows on restored data, so an
arrow must be a function of params and event args. A cache a state keeps (a
solver's contacts, a mesh) must be pure in its params, or memoised on them -
never hidden state the laws cannot restore.

Where an arrow or a transport is affine - each parameter it sets a sum of
parameters times numbers, plus a number - say so (`State::affine`,
`sg::transport::only` / `swizzle` / `copy_all` / `affine`): its handler is made
from what it says, and the laws can check it without running it
(`sg::algebra`, `LawOptions::accelerate`). Never write a description beside a
handler that does something else: what is said is what runs.

Time comes from the `Temporal` drive in the table below. Behaviour, shader
animation (`own_time`) and sound use the same timeline; presentation keeps no
second clock. Choose `Keeps` explicitly: `WhileActive` includes open active
guests; `WhileShown` runs wherever shown; `WhileFocused` requires input;
`WhileEntered` requires the current state; `Always` runs elsewhere.

**4. Every state is reachable, and the engine keeps checking.**
`graph.validate()` refuses a state no interface reaches (seams count). The
engine re-validates whenever the graph changes (`StateGraph::revision`) and
reports what is newly wrong (`Engine::on_problem`; `set_strict(true)` throws).
In tests, run strict, and `sg::verify(graph)` / `sg::enforce(graph)` on every
graph you build. A state added "for now" with nothing linking it is caught the
frame it appears - link it, don't silence it.

**5. Every state has a start, and can go back.** The engine keeps each
state's initial conditions when it starts (`StateGraph::keep_defaults`);
`restore_default(id, guests)` puts a state - and what lives in its portals -
back; `keep_default(id)` makes how it is now its start (a room built at runtime
keeps its default as made). Build it fully in its constructor or before
`engine.start`; call `keep_default` after deliberate setup. What a state is must be
in its elements and params. Override `State::on_restored()` to refresh anything
derived (a surface repaints).

**6. What a thing says is not what it is.** Form and content are separate:
content that changes (a sheet's words, a note) lives apart from the state that
shows it - in a file kept by `sg::TextStore` (read once, re-read only when the
file's time stamp moves, written only when the text changes). A state's own data
can always be written out as its source with `sg::to_text` and read back with
`sg::from_text` - leave out what is only of the moment (the camera, a clock)
with its filters.

**7. A look belongs to the state; the glass to the interface.** A state wears
its own `LookState` (`sg::wear`) and keeps it wherever it is shown - a realm's
VHS picture is the realm's. Whatever shows it adds only what it itself is: a
CRT panel's glass is the television's. Composites share `gl::film_glsl()`,
`godrays_glsl()`, `fxaa_glsl()`; lighting that is the same in every world
lives in `sg/domains/Light.hpp`, not in a game. A feed panel is a screen
(its picture developed light, taken back through the tone curve) unless it
says `untone` = 0; a world whose feed must be seen as paint makes its own
picture ready for the room it hangs in, in its own look. A world whose time
must stand still while it is only shown is driven `Keeps::WhileEntered`
(`WhileActive` runs it while it is open in another state).

**8. Watching is const; structure is counted.** The engine and a const graph
show the world const - act by firing events, never by writing into what
`engine.current()` returns. Embeddings, transitions, seams and arrows are
declared, then read; to change one, use the graph (`set_sync`,
`set_propagation`, `set_carry`, `drop_embedding`, `set_functor`), which counts
it. Never `const_cast` your way past this. An element's `id` and `kind` are
fixed - another name is another element. An arrow that adds or removes
elements cannot be checked by the laws (a trial never changes structure): it
shows as unchecked; keep such arrows few, and their structure in their params. A Live or View functor whose
transport reads more than its two elements is declared
`Propagation::Continuous`.

**9. Cheap by construction.** States that are idle compute nothing; repaint
only what changed. Static geometry is `Spatial3D::fixture` (no arrow, so it
costs the laws and the frame nothing) - `mesh` is for things that move. Check
large states' arrows count: laws cost arrows x elements.

The engine keeps to one rule for this: **strictness in the model, checking in
the tools, dispatch compiled.** What is true of the model is refused when it is
declared (a duplicate name, a functor renamed under its graph) - once, and in
constant time, by an index. What costs to find out - the laws, watching hooks,
validating the graph - runs in tests, `verify`, and the engine's watch, never
in a frame's path. What the frame runs is the declared model looked up by
index: arrows by trigger, transitions by trigger and by name, drives and
routes by state, rebuilt only when the graph's revision moves.

The build follows the same separation: the machine is a state, the compiler
its interface. Headers hold declarations, types, templates, one-line/constexpr
bodies and justified hot bodies (`// inline: every frame reaches for it`).
Implementation belongs in `.cpp`. Modules and engine libraries (`stategine`,
`stategine_render`, `stategine_dsl`, `stategine_net`) compile files independently; the linker
joins them. A changed body recompiles only its own file.
`sg_shape` (the engine's) and `stategine_check_modules()` (a project's) fail
on a module that includes what it does not use, or a header with a body that
has no reason to be there.

## What already exists - reach for it before writing your own

Before adding a param, field, counter or helper, check this table. Carry only
what is needed through a declared interface; extend the existing implementation
or derive a functor when necessary, rather than keeping a private copy.

| State or machinery (header) | It is | Use it instead of |
| --- | --- | --- |
| `Temporal` (`core/Temporal.hpp`) | time: a timeline per element, `time` and `frame` as params, one arrow that advances by `dt` | a `dt` argument used as a clock, a tick or frame counter, a timer param, `std::chrono`, `on_update`'s `dt` - a state that changes with time is **driven** (`sg::drive(graph, clock, state, trigger)`) and reads `{dt, time, frame}` from its line on the clock |
| `Spatial2D` / `Spatial3D` (`domains/Spatial.hpp`) | things with a pose that integrate; a 3D room, its `fixture`s and `mesh`es, `model`s | your own position / velocity / integrator |
| `Surface2D` (`domains/Surface.hpp`) | a 2D state that hands over its pixels (`raster()`), repainted only when changed | a private bitmap or texture; anything 2D shown inside another domain |
| `LookState` (`domains/Look.hpp`) | how a state is shown: passes and uniforms, worn by `sg::wear`, faded by `fade` | shader params, tint, fog, grade kept in a state's own params |
| `Camera` (`domains/Camera.hpp`) | a lens that sees a world by filming it (`sg::film`), its feed shown by a screen | a hand-made eye, a second view matrix, a render-to-texture written by hand |
| `ConsoleState` (`domains/Console.hpp`) | scrollback, an input line, `submit` and `clear` | your own log buffer or command line |
| `Atlas` / `Cover` (`domains/Atlas.hpp`, `core/Sheaf.hpp`) | charts glued by doorways; local pieces that must agree to glue | rooms placed by absolute coordinates; agreement checked by hand |
| `TextStore` (`core/Store.hpp`) | texts kept in files, read once, re-read only when the stamp moves | file reads and writes of your own |
| `Assets` (`core/Assets.hpp`) | files in `<root>/<owner>/`, with owner/path checks and legacy adoption | loose files or private asset paths; runs use `<build>/out/<state>/` |
| `spatial` (`spatial/Math.hpp`, `Geometry.hpp`, `Index.hpp`) | pure transforms, bounds, rays, convex volumes, finite-surface projection and BVH | duplicated geometry or semantic ownership in a query cache |
| `rigid::World`, `rope` (`physics/Rigid.hpp`, `Rope.hpp`) | bodies and cords, stepped as a state's cache, pure in restored params | hand-rolled collision or cords |
| `field` (`physics/Field.hpp`) | named scalar/vector sources, receivers and pure query solver; directional/radial/plane or specialized const backend | private gravity/field logic, another clock or mutable captured state |
| `net::Cellular`, `net::Reconcile` (`net/Cellular.hpp`, `Reconcile.hpp`) | stalks, overlaps, restrictions and CPU/CUDA reconciliation derived from an ordinary state's data | network entities, a second world, private reconciliation ticks or GPU-owned reality |
| `Daylight`, `Shapes` (`domains/Light.hpp`, `Shapes.hpp`) | the sky at an hour, sun light, spill; extruded and lathed models | lighting maths or mesh code inside a game |

Solvers and renderers are machinery inside the owning state, not new semantic
worlds. Derive their inputs from its params; its arrows write results. Physics,
field support and render visibility keep separate derived indices. Field edits
affecting sleeping bodies must wake them through an owning arrow. See
[fields-and-views.md](docs/fields-and-views.md) and README's *Layout* for the
current APIs and layers.

## Networking is derived machinery above core

`sg/net` is linked as `stategine::net`. The network is an ordinary `State`:
its elements and params hold participants, observations, constraints and
results. Author that state and its graph interfaces in the DSL. Keep networking
out of State, Functor, StateGraph, Engine, Cover and the existing algebra API.

- Local game data reaches the network through declared functors, lenses or
  kept carries. Remote input enters a declared port through `Engine::send`.
  Outgoing traffic is what the network `says`; an external observer may
  serialize and send bytes, but cannot change the world.
- Reconciliation is the network's own arrow, driven by `Temporal`. Its native
  gathers its own state, evaluates `Reconcile`, writes its own results and says
  they are ready; declared functors/events return corrections to game states.
  Zero elapsed time is the identity. No private tick, callback or `on_update`.
- Joining and leaving change structure through `graph.edit`, never through
  the reconciliation arrow. Cellular overlaps can have noninvertible
  restrictions; they do not change Cover's exact invertible descent semantics.
- `Cellular` owns only derived layouts. Recompile when topology or restriction
  operators change; gather fresh observations, confidence, overlap weights
  and pins without rebuilding for value-only changes. Restored state data must
  reproduce the same solution regardless of previous evaluations.
- `LinearSystem` contains flat numerical buffers; `Backend` consumes those
  buffers, never semantic objects. CPU and GPU caches are disposable execution
  data. Neither warm starts nor cache history may become hidden state.
- Apply `delta`, W and `delta*` matrix-free; use bounded iteration counts.
  Hard pins are fixed variables or boundary conditions throughout the solve.
  Finite lambda minimizes the confidence/disagreement objective and can leave
  disagreement: report it separately from the free equation residual.
- Keep the CPU implementation as the reference. GPU topology and restrictions
  stay resident; upload changed inputs and download changed solutions. Report
  an unavailable GPU explicitly rather than counting a CPU fallback as GPU
  equivalence. A backend change must not change state or graph structure.

No participant is intrinsically authoritative. Distribution and distributed
consensus remain later execution work. See [networking.md](docs/networking.md)
for the data schema, numerical contract, backend build options and limitations.
`tests/dsl/network.sg` and `network_numerics.sg` author the test graphs. Run
`sg_net` and `sg_net_numerics` for agreement, disagreement, cycles, pins,
CPU/GPU equivalence, topology changes and value-only reuse; both must pass
strict graph validation and `sg::verify` without unchecked equations.

## Adding a state, a room, an interface - checklist

1. Register `stategine_module(<name> USES ...)` and `stategine_check_modules()`
   (`cmake/StategineModules.cmake`); follow the ownership/build rules above.
2. Author the construction under *The DSL* below, applying rules 2-7 for
   behaviour, reachability, appearance, input, defaults and content.
3. Test the graph **alone** with `sg::verify`, then test its interface in the
   assembled world. Use meaningful event args; inspect `unchecked` and `bounded`
   as well as counterexamples. `ok()` is no structure problem/counterexample;
   `holds()` also requires no unchecked equation; `complete()` excludes bounded
   search. Check covers, adjunctions and interface defects where declared.
4. Write `CHANGELOG.md` in plain words, with **Breaking** for consumer changes.

## The DSL: the construction, written as notation

The engine has a textual syntax for what it already is (`sg/dsl/`, the compiler
`sgc`, `stategine_compile_dsl()` in `cmake/StategineDsl.cmake`). It is not a
scripting language and adds no runtime: `tests/dsl/scenario.sg` is a whole
vertical slice, and `sg/dsl/Compile.hpp` says how a program becomes the
engine's own calls.

- **Author Stategine structure in the DSL.** Ordinary states, elements, params, arrows, compositions, functors, lenses, embeddings, transitions, seams, drives, ports, Looks, cameras and their graph relations must be authored in the Stategine DSL whenever the DSL can express them. Do not add equivalent declarations directly in C++. C++ is reserved for engine/runtime implementation, devices and native computations attached to already-declared arrows or transports.
- **Sugar must disappear during lowering.** `wear`, `film`, `when`, input bindings and any other convenience syntax are legal only when they lower mechanically to existing Stategine primitives. No sugar construct may introduce runtime semantics of its own. Every construct names the primitive it becomes (`sg/dsl/Plan.hpp`: one step is one call of the engine's API); if you cannot name it, the syntax does not go in.
- **Seams and transitions are different.** A seam identifies two boundaries bidirectionally. A transition is a directed change of active state. A seam does not imply two-way traversal. One-way traversal across a shared boundary is expressed as a seam plus only the permitted directed transition (`seam a.door <-> b.door` and `transition a -[cross]-> b`; if the transition carries anything between like states, it carries the seam's own travel functor - the engine's seam law allows nothing else). A seam never makes a transition; two transitions never make a seam.
- **Port before removing.** For every existing C++ declaration migrated into the DSL, first reproduce it in the DSL, verify equivalence and run the laws/tests, and only then remove the old declaration. Never delete first and reconstruct afterward. Equivalence is `sg::dsl::facts` (`sg/dsl/Facts.hpp`): everything the DSL declares is in the C++-built graph's facts (`missing(plan, graph)` is empty); then the C++ goes, and the graph's facts are as they were.
- **Notation is not a state; its source document may be.** A terminal, file, sheet, book or editor can hold source as an ordinary state; its compiler is another state with native arrows. Compilation produces ordinary StateGraph structure. The compiler requests a `graph.edit`, applied next frame, rather than mutating from an arrow/callback (`sg/dsl/Compiler.hpp`, `src/dsl/compiler.sg`). Grammar, source state, compiler state and resulting graph remain distinct; there is no privileged meta-runtime.

What the compiler refuses is the ontology, as errors with the reason: a private timer (use a Temporal drive), a direct write from one state into another (target an arrow through a declared interface), IO in a transport (external effects cross a declared device or port), a callback that changes another state, `on_update`, `emit` as orchestration, a mode duplicating focus, input that writes a state. There is no syntax for any of them, and none to make migrating them easier: a migration repairs them (a `camera.params().set("fov", ...)` becomes the arrow `lens -> lens : zoom(fov)` and a functor to it).

A native implements only the inside of a declared arrow, transport or edit
(`sg/dsl/Natives.hpp`). Arrows receive their own state/elements; transports their
two endpoints, never a graph, clock, engine or unrelated state. Edits receive
the graph as the declared rewrite capability. The declaration remains the
semantic source of truth. Natives are trusted C++ with restricted interfaces,
**not a sandbox**: closures can capture outside capabilities and must be reviewed.
Applying a plan is **all or nothing** (`dsl::apply`: checkpoint, make, validate,
roll back). **Facts are faithful**: transition `enter`, affine rows, transport
stages/chains, embedding/seam/drive/edit configuration and native binding names
are preserved. `person = 1` differs from `person = 2`; `native foo` from `native bar`.

## Before you finish

```sh
cmake --build build && ctest --test-dir build        # engine tests: laws, graph watch, defaults, text, the DSL
```
- `graph.validate()` empty and `sg::verify(graph)` ok on every graph you touched.
- No new state without an interface to it; no game-loop code writing into a state it does not own.
- No module including what uses it.
- Frame time and law-check time not worse (see *Performance* in README.md).

## Voice

Comments and docs say what a thing *is* and *why*, in plain words, as the rest
of the code does - "a doorway goes both ways", not "iterates seams". Keep to it.
