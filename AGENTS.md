# AGENTS.md - working on stategine

Read this before changing the engine or building anything on it. It is short
because the idea is: **everything is a state, states are independent, and
they meet only through interfaces the graph declares and the laws check.**
Every rule below follows from that.

## The basic rules

1. **Anything that is a state is separate**, and encapsulated: what it is,
   it holds; what it does, its own arrows do.
2. **States are linked only through arrows** - and the interfaces the graph
   declares from them (below).
3. **Time is a state of its own; so is physics.** Neither is part of any
   other state, and neither owns one.
4. **Each state is driven by a clock of its own**: its own line on the
   `Temporal`, private to it, moved only as that state steps - its time is
   the sum of its own steps. No state's time is another's, and no line
   governs another. A copy of its time kept in its params would be a second
   clock for the same state, free to disagree with the first.
5. **There is no authority state** - not time, and not the state graph,
   whose only job is to link the states.

## The cornerstone

A state is a small category - elements are its objects, events on them its
arrows. A room, a computer's desktop, a sheet of paper, a whiteboard, a realm
seen on a television: each is a **state of its own**, and could exist with
none of the others. States are connected **only** by the graph:

| Interface | Declared with | What it is | Avoid |
| --- | --- | --- | --- |
| Transition | `graph.connect` / `push` / `pop`, on what a state `says` | directed change of active state | flags one state sets and another polls |
| Functor / lens | `add_functor`, `add_lens` | object/arrow/data transport; `sg::kan::left` / `right` can derive an ordinary functor, not a new kind of thing | copying params in a game loop or another state's arrow |
| Embedding | `graph.embed` | guest in a host portal, with `in`/`out`, subject, sync and focus; whether it is open and focused is the portal's to say (`open_key`, `focus_key`), and the engine follows | holding a guest pointer as the interface; an open flag kept outside the host |
| Seam | `add_seam`, `glue_doorway` | bidirectional boundary identification, including a doorway and its door | hand-placed global room coordinates |
| Drive | `graph.drive` / `sg::drive` | a line of the state's own on a `Temporal` fires its arrows with `{dt, time, frame}`; additive drives omit `frame` | one clock other states must follow; a state reading another state's time; a second clock beside a state's own line (a sum of `dt` in its params); wall-clock time from outside any state |
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

A state's time is its line on the `Temporal` and nothing else (the table
below): its own, private, moved only as it steps. Its behaviour, its shader
animation (`own_time`) and its sound follow that one time of its own, so they
never drift apart; no state's time is another's, and none is the authority. Choose `Keeps` explicitly: `WhileActive` includes open active
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
The renderer keeps the same rule for every room, so a new one is cheap without
tuning: what a frame needs from a world (lights, casters, its visibility index,
a look's uniforms, a portal's declaration, a feed's picture) is kept by what it
is made from - data versions, the graph's revision, poses - and made again only
when that moves. A still world's frame does none of it (`FrameTimes` counts,
`sg_culling_gl` holds it); never key a cache on where something is when it can
be keyed on what it is (a moving doorway once made new shadow maps every frame).

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
| `Temporal` (`core/Temporal.hpp`) | time, a state of its own: a line for each state it drives, `time` and `frame` as params, one arrow that advances a line by `dt` - no line governs another | one clock every state must follow, a state reading another's time, a second clock beside its own line (a timer or a sum of `dt` in its params), `std::chrono`, `on_update`'s `dt` - a state that changes with time is **driven** (`sg::drive(graph, clock, state, trigger)`) and reads `{dt, time, frame}` from its own line |
| `Spatial2D` / `Spatial3D` (`domains/Spatial.hpp`) | things with a pose that integrate; a 3D room, its `fixture`s and `mesh`es, `model`s | your own position / velocity / integrator |
| `walk`, `fields_of`, `ray` (`domains/Walk.hpp`) | a walker standing on whatever ground its space's fields say is down - walls, solid boxes and balls stop it, doorways let it through, a jetpack flies it; a state's `field` elements as sources; a ray against what stops | a walker, gravity or collision of your own; y-up assumptions |
| `Pose`, `standing` (`domains/Spatial.hpp`) | a whole turn (yaw, pitch, roll), composed through anchors and doorways; a camera's own ground | headings carried by hand; parts of a turned thing re-posed by hand |
| `Seam::wraps`, `nests`, `period_x/y/z` | a space glued to itself (torus, portal pair, genus two); worlds inside worlds by a ball; a space that wraps in an axis | rings excused by hand; planets in one coordinate frame; tiled copies placed by hand |
| `Room` (`domains/Room.hpp`) | a room: a floor plan of any shape (`sg::plan`), openings as data on its walls, walls laid round them by one rule (shell, face, skirting, filler), and what hangs on an opening going with it | walls, openings or floor plans of your own; cutting walls round doors by hand |
| `Surface2D` (`domains/Surface.hpp`) | a 2D state that hands over its pixels (`raster()`), repainted only when changed | a private bitmap or texture; anything 2D shown inside another domain |
| `Texture` (`domains/Texture.hpp`) | a picture things wear, a state of its own: six cells, the thing seen from each way of each axis (no unwrap); generated (`define`), photographed or painted over (`layer`, followed); worn by being embedded in a mesh or a thing of parts; `projection_guide` is the map to paint over | UVs, a texture path or colour in a mesh's params, a picture per part, an unwrap |
| `LookState` (`domains/Look.hpp`) | how a state is shown: passes and uniforms, worn by `sg::wear`, faded by `fade` | shader params, tint, fog, grade kept in a state's own params |
| coinciding surfaces | never fight: the scene's vertex stage draws a thing a few depth steps nearer the smaller it is, so where two coincide the smaller is seen, steadily (`sg_coplanar_gl`); things of one size that must still part (a model's parts) say `depth_layer` | offsetting a face by hand to stop flicker, a polygon offset of your own, a depth bias per game |
| glass (`glass` on a thing, 0..1: how clear) | drawn after the rest of its room, furthest first, blended over what is behind it, more of it seen edge on (Fresnel), writing no depth | an opaque pane, a portal or a picture standing in for glass, a transparency pass of your own |
| a look's air, grade and glow (`gl::air_fs`, `film_glsl`, `fog_bloom_glsl`; README *Air, grade and glow*) | lamps' light scattered by the air (`scatter`, a light's own `scatter`), shadowed, gathered per view only when it moves; a scene-light grade and curve (`uGrade*`, `uTonemap`); glow that spreads in thick air (`uFogBloom`); the eye adjusting to the light it sees (`exposure.auto`, eased, settled on a shot's first frame) | a fog or god-ray pass of your own, a grade in a game's composite, a volume texture kept beside a world, an exposure tuned by hand per room or kept in a state |
| `Camera` (`domains/Camera.hpp`) | a lens that sees a world by filming it (`sg::film`), its feed shown by a screen | a hand-made eye, a second view matrix, a render-to-texture written by hand |
| `ConsoleState` (`domains/Console.hpp`) | scrollback, an input line, `submit` and `clear` | your own log buffer or command line |
| `Atlas` / `Cover` (`domains/Atlas.hpp`, `core/Sheaf.hpp`) | charts glued by doorways; local pieces that must agree to glue | rooms placed by absolute coordinates; agreement checked by hand |
| `TextStore` (`core/Store.hpp`) | texts kept in files, read once, re-read only when the stamp moves | file reads and writes of your own |
| `Assets` (`core/Assets.hpp`) | files in `<root>/<owner>/`, with owner/path checks and legacy adoption | loose files or private asset paths; runs use `<build>/out/<state>/` |
| `cache` (`core/Cache.hpp`) | derived data kept on disk by a digest of what made it (inputs and code), written atomically, damaged files a miss; never a source of truth | remaking costly pure data every start; a cache keyed on a hand-kept version alone |
| `spatial` (`spatial/Math.hpp`, `Geometry.hpp`, `Index.hpp`) | pure transforms, bounds, rays, convex volumes, finite-surface projection and BVH | duplicated geometry or semantic ownership in a query cache |
| `rigid::World`, `rope` (`physics/Rigid.hpp`, `Rope.hpp`) | bodies and cords, stepped as a state's cache, pure in restored params | hand-rolled collision or cords |
| `field` (`physics/Field.hpp`) | named scalar/vector sources, receivers and pure query solver; directional/radial/plane or specialized const backend | private gravity/field logic, another clock or mutable captured state |
| `net::Cellular`, `net::Reconcile` (`net/Cellular.hpp`, `Reconcile.hpp`) | stalks, overlaps, restrictions and CPU/CUDA reconciliation derived from an ordinary state's data | network entities, a second world, private reconciliation ticks or GPU-owned reality |
| `Daylight`, `Shapes` (`domains/Light.hpp`, `Shapes.hpp`) | the sky at an hour, sun light, spill; extruded and lathed models | lighting maths or mesh code inside a game |
| `Modeler` (`domains/Modeler.hpp`, code in `src/domains/modeler/`) | a shape from a recipe, as text: primitives, cuts, arrays, macros, an imported `.obj` - its mesh a pure function of the recipe, closed, sharp where it should be; what Paint is to a texture (`docs/modeler.md`). Architecture is its libraries (`use mould orders pointed girih structure city param arch`): real construction - orders by module, arches by their centres, vaults, girih, frames, whole cities by seed; a style a point in continuous axes. What moves is a `moves` block - a turn (a hinge), a slide (a runner), a track ridden by two points (a garage door's panel, a shutter's slat); `with=` couples one to another, `step=` pushes it on round and round (a revolving door): `Model::joints`, `sculpt::pose`, `sculpt::moves_of` for a world that moves the parts itself. Doors, drawers and cabinets are `use doors` (`ModelerDoors.cpp`, docs/modeler.md *Doors*): one `door` of choices - surround, head, leaf body and cells, how it moves, hardware - every choice a variable of the library, a kind of door (`door.hotel`, `door.persian`, `cabinet.kitchen` ...) a set of them; `wall=` stands one in a wall cut round its surround | boxes placed by hand, a mesh built inside a game, a model loader of your own, a facade standing in for a building, a style's numbers written twice, a class per kind of door, a door or drawer of its own, hinge or track maths of its own |
| `terrain` (`domains/Terrain.hpp`, code in `src/domains/terrain/`, `docs/terrain.md`) | land from a recipe, any size: noise, hills, erosion, roads graded along curves, rivers, lakes filled to their spill, sea, swamp; layers of covering by height, slope, wetness, nearness to road and water; things strewn by the same rules (`grow`n plants: `use trees`). `lay` puts it in any Spatial3D as the state's own ground (`Spatial3D::terrain`: walked, hit by rays, drawn round the eye). `sgterrain` (map) and `sgland` (the renderer's view) to look | a heightmap, ground mesh or scatter of your own; trees placed by hand; a walker's own ground |
| `Save`, `SaveFiles` (`domains/Save.hpp`) | a save game: the states it keeps written out, those it replays put back to their start, and what follows from the kept brought back through the graph's own links on loading (the left Kan extension along the kept, `kan::inverse` for links undone); its file kept by a device at its port | a save system, a serializer of a world, flags copied between states by hand on loading |
| `Being` (`domains/Being.hpp`) | a creature: skeleton (joints), body (parts or a skin read from glTF), spirit (one driven arrow - clips blended, joints held, IK goals, each joint relaxing to its target at its stiffness), its own tempo from its scale; clips blended by a point (`blend`, `steer`); one body's motion carried onto another of the same skeleton by a kept functor (`retarget`, followed by `lead`) | an animation player, a skeleton or IK of your own, a retargeter or blend tree of your own, a creature's private clock |
| `fit`, `Being::resize/face/lift/height/facing` (`domains/Being.hpp`) | a model made anywhere (any units, up, facing, holder node) put where a reference body stands; glTF holders, skin pictures (`Files::decode`) read in | scaling, turning or grounding an imported character by hand, per game |
| `Ragdoll` (`domains/Ragdoll.hpp`) | a being's body with weight: a rigid body a bone, balls with cones, muscles turning each toward the pose the being means (`aq`); hit, held and pulled at its ports; asleep until disturbed; leads the being back through `lead`; meets a host's solids (`collide_with`) | a ragdoll, a hit reaction or an active-ragdoll controller of your own; joint motors written outside `sg::rigid` |

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

No participant is intrinsically authoritative. `Peer`, `Partition`, `Exchange`
and `Distributed` select and coordinate numerical work over the same ordinary
world. Participant `solver` params select execution; exchanged values are
restriction projections on touching overlaps. Epoch, partition generation,
round, received boundaries and sent-value records live in NetworkState params.
Only a Temporal-driven arrow advances local progress. Explicit epoch/generation
identify the problem, sender sequence identifies progress, and boundary basis
identifies a projection version. An older basis never overwrites a newer one;
duplicates are idempotent. Certified strictly dominant free systems can use
latest-neighbor bounded-staleness relaxation; validate the step against the
actual operator and declared staleness. Singular/unproven systems retain the
matching-round algorithm and canonical kernel component. Transport delays
never become a private solver clock or invented boundary.
Keep numerical weights, pins, restrictions and controls fixed within an epoch;
verified Problems also bind their immutable observations/input set. A
split/merge hands off moving work through declared input, changes execution
params through an edit and refreshes the derived layout, without new game
states. Socket retries remain external machinery. See [networking.md](docs/networking.md)
for the data schema, numerical contract, backend build options and limitations.

`Distributed` may retain one private disposable numerical workspace, never a
second Cellular/Partition or protocol progress. Refill changing values from
State on every evaluation. Memoize dominance certificates, operator bounds and
dense indices only against epoch/generation, retained layout revisions and
every weight/pin/restriction/control that determines the plan. Fail closed on
changes within that problem; a declared reconfiguration must advance its
epoch/generation. Validate layout on layout changes, constants on plan changes
and dynamic values each use. Prepared backend views borrow validated buffers
for that call only; public solver/residual entry points remain fully checked.

`Verify` keeps at most two locally validated problem references. Validate a
public Problem before using its cryptographically bound cache key; never trust
a claimed digest. A verifier copy starts empty so each peer independently
solves. Cache canonical values/hash/residual and deterministic checkpoint/hash only;
the rule/checkpoint callbacks must be pure in the immutable Problem. Reuse
authentication only for the identical signed input set; authenticate new
certificate, vote and request signatures. Compact protocol controls may refer
to a locally recomputed result, but missing input/proposal data needs bounded,
authenticated request/retransmission. Unknown hashes cannot finalize, and
predecessor chains, durable locks, quorum intersection and evidence stay intact.
CUDA changed-value compaction uses stable flag/scan/scatter order, never atomic
append. Run warm-capacity, cache-safety, reordered reference recovery and
multi-level scan regressions. `SG_BUILD_NET_BENCH` is opt-in and is not a law
test; separate measured computation from virtual transport delay and report
remaining allocations honestly. Keep the bounded-gradient reference method.

`Placement` is a pure deterministic planner over Cellular and finalized data,
capacities and execution/traffic costs. Hysteresis counts finalized epochs.
It proposes assignments and application-supplied interest changes, with no
graph capability. Apply only through `network.repartition` / `graph.edit`:
actual constraint elements are the only interest topology, generation changes
refresh Cellular/Partition, and verified `Agreement::Handoff` carries moving
checkpoints. Neither predicted data nor telemetry is world authority.

External `Transport` routes opaque bytes using explicit peer/channel/delivery/
supersession metadata. Latest coalesces bounded unsent slots; Reliable channels
preserve ordered control and report bounded backpressure. The UDP reference
honestly reports Reliable as unsupported. Optional `stategine::net_ice` delegates
ICE/STUN/TURN, DTLS and SCTP to libdatachannel; callbacks fill bounded external
queues, never touch State. Authenticate external signaling and replay signed
idempotent frontiers on delivery uncertainty after reconnect. Socket timing
and RTT remain execution metrics. Relay/rendezvous services have no world role.
The optional fetched ICE target is refused with Windows MinGW GCC 16/UCRT:
its RTC teardown use-after-free remains unresolved; use a tested Clang/libc++
build for that target. MSVC uses an installed native LibDataChannel package.
Do not hide reconnection/teardown failures by skipping tests. `relay_only`
restricts advertised candidates; ICE may find a peer-reflexive LAN shortcut.
Check actual faulty-route byte flow, not only a candidate's type label.
Agreement still requires the exact finalized predecessor before attesting any
successor; asynchronous numerical substeps do not require quorum.
`tests/dsl/network.sg` and `network_numerics.sg` author the test graphs. Run
`sg_net` and `sg_net_numerics` for agreement, disagreement, cycles, pins,
CPU/GPU equivalence, topology changes and value-only reuse. Also run
`sg_net_distributed` and `sg_net_processes` for chained peers, delayed and
duplicate packets, partition handoff and real separate-process CPU/CUDA
execution. Every graph must pass strict validation and `sg::verify` without
unchecked equations. Report unavailable GPU checks explicitly.

`examples/pong/pong.sg` declares the independent two-instance Pong test world.
Keep its controls at declared ports, its publication/correction at functors,
and its motion and reconciliation on Temporal drives. A settled boundary
admits one ordinary game motion event; its protocol epoch is not a private
clock. `examples/Udp.*` and Pong's const view are external machinery. The
`sg_net_pong` test compares whole local game results, including rallies and
scores, across separate CPU/CUDA peers and delayed/duplicate delivery.

`Epoch`, `Integrity`, `Verify`, `Agreement` and `Protocol` are external
execution/evidence machinery. A driven network arrow says which work it
requests; proposed solutions never mutate a state. Canonical epochs bind the
world step, region/generation, named topology, ordered signed inputs,
constraints, solver/tolerances, committee and finalized predecessor. Check
signatures before any remote-origin port delivery, retain signed
equivocation evidence, redundantly recompute the CPU acceptance cells, and
check equation/disagreement residuals and exact hard pins. Never hash GPU
intermediates. Only independently verified, matching regional quorum
finalizations enter the declared port through `Engine::send`.

Committees use distinct pinned public keys and intersecting quorums beyond
their explicit fault budget. Persist vote locks before broadcasting and seal
an old generation before endorsing its replacement; verify a handoff's exact
checkpoint, old quorum and new committee before continuing. Availability
still requires the epoch's declared inputs. Signatures identify suppliers;
game rules validate their claims. Discrete controls/transfers/deaths stay
deterministically ordered events, never numerical averages. Cross-region
inputs come from independently checked finalized boundaries, not a global
ledger or a privileged machine.

`Prediction` owns disposable numeric forecasts and an input replay history;
it has no world-write capability or private clock. Reuse the game's existing
declared motion/rules. Bound the lead, replay pending inputs after a finalized
correction, smooth only continuous presentation using supplied Temporal
time, and expose confirmed history separately for read-only latency queries.
Predicted scores or collision outcomes never become accepted reality. Run
`sg_net_agreement`, `sg_net_verified_processes` and `sg_net_pong`, plus the
existing authority suite, when changing this acceptance path. Demonstrate
actual available GPU checks and describe hardware limitations accurately.

## The browser adds no world

`ViewPlan`, GPU resources, WebRTC connections, DOM input and JS queues are
disposable machinery. A renderer cannot create a relation between states;
a connection cannot create a participant; a callback cannot change State;
a GPU buffer cannot own truth; a browser clock cannot become world time.
Meaning remains in StateGraph. Browser machinery only computes, moves,
verifies or draws what its declared interfaces already mean. Keep `sg/render`
backend-neutral, `sg/gl` native and `sg/web` above them. Cross builds consume
a native `sgc`; Wasm networking keeps the double-precision CPU reference.
Run the portable Wasm suite and real browser/native integration tests as well
as native laws and renderer tests. See [browser.md](docs/browser.md).

## Joining two projects

Bringing one stategine project into another (a game into a sandbox, two
games together) is bringing its states into the host's one graph and
declaring where they meet - never a bridge between graphs. Before doing it,
read [docs/joining-projects.md](docs/joining-projects.md): one graph, one
namespace (check names first), globals that only the host sets
(`Modeler::set_files`, `Texture::set_reader`, `cache::set_folder`), the
join held to the overlap law (portals by their middle, one eye height),
what the guest's loop did and the host's loop must now do, presence, and
the host's checks.

## Adding a state, a room, an interface - checklist

1. Register `stategine_module(<name> USES ...)` and `stategine_check_modules()`
   (`cmake/StategineModules.cmake`); follow the ownership/build rules above.
2. Author the construction under *The DSL* below, applying rules 2-7 for
   behaviour, reachability, appearance, input, defaults and content.
3. If its data can disagree with itself (a body's pose and its bones, a
   solver's targets and its parts), say so in `State::faults`: validation
   then names it at start and `verify` refuses it, in every project.
4. Test the graph **alone** with `sg::verify`, then test its interface in the
   assembled world. Use meaningful event args; inspect `unchecked` and `bounded`
   as well as counterexamples. `ok()` is no structure problem/counterexample;
   `holds()` also requires no unchecked equation; `complete()` excludes bounded
   search. Check covers, adjunctions and interface defects where declared.
5. Write `CHANGELOG.md` in plain words, with **Breaking** for consumer changes.

## The DSL: the construction, written as notation

The engine has a textual syntax for what it already is (`sg/dsl/`, the compiler
`sgc`, `stategine_compile_dsl()` in `cmake/StategineDsl.cmake`). It is not a
scripting language and adds no runtime: `tests/dsl/scenario.sg` is a whole
vertical slice, and `sg/dsl/Compile.hpp` says how a program becomes the
engine's own calls.

- **Author Stategine structure in the DSL.** Ordinary states, elements, params, arrows, compositions, functors, lenses, embeddings, transitions, seams, drives, ports, Looks, cameras and their graph relations must be authored in the Stategine DSL whenever the DSL can express them. Do not add equivalent declarations directly in C++. C++ is reserved for engine/runtime implementation, devices and native computations attached to already-declared arrows or transports.
- **Sugar must disappear during lowering.** `wear`, `film`, `when`, input bindings and any other convenience syntax are legal only when they lower mechanically to existing Stategine primitives. No sugar construct may introduce runtime semantics of its own. Every construct names the primitive it becomes (`sg/dsl/Plan.hpp`: one step is one call of the engine's API); if you cannot name it, the syntax does not go in.
- **Seams and transitions are different.** A seam identifies two boundaries bidirectionally. A transition is a directed change of active state. A seam does not imply two-way traversal. One-way traversal across a shared boundary is expressed as a seam plus only the permitted directed transition (`seam a.door <-> b.door` and `transition a -[cross]-> b`; if the transition carries anything between like states, it carries the seam's own travel functor - the engine's seam law allows nothing else). A seam never makes a transition; two transitions never make a seam.
- **A seam is seamless.** Both sides of it say what whoever crosses meets there (`State::overlap`), and the overlap law (`laws::overlaps`, in `verify` and the engine's watch) holds the two accounts to one: walked alike, the same down, the ground at one height at the threshold, the eye carried as high, and a crossing for each side walked through (`walkway` declares it; `glue_doorway` is the bare seam). A new kind of space gives its account, or its seams are reported unchecked. What a seam means to let differ it says (`differs`); what is meant to break - a cut, a dissolve - is a transition, never a seam. Fix a disagreement where it is (a doorway's foot, a walker's eye), never by excusing it.
- **Port before removing.** For every existing C++ declaration migrated into the DSL, first reproduce it in the DSL, verify equivalence and run the laws/tests, and only then remove the old declaration. Never delete first and reconstruct afterward. Equivalence is `sg::dsl::facts` (`sg/dsl/Facts.hpp`): everything the DSL declares is in the C++-built graph's facts (`missing(plan, graph)` is empty); then the C++ goes, and the graph's facts are as they were.
- **Notation is not a state; its source document may be.** A terminal, file, sheet, book or editor can hold source as an ordinary state; its compiler is another state with native arrows. Compilation produces ordinary StateGraph structure. The compiler requests a `graph.edit`, applied next frame, rather than mutating from an arrow/callback (`sg/dsl/Compiler.hpp`, `src/dsl/compiler.sg`). Grammar, source state, compiler state and resulting graph remain distinct; there is no privileged meta-runtime.

What the compiler refuses is the ontology, as errors with the reason: a second clock (a state's time is its own line on a Temporal; a timer in its params would be another), a direct write from one state into another (target an arrow through a declared interface), IO in a transport (external effects cross a declared device or port), a callback that changes another state, `on_update`, `emit` as orchestration, a mode duplicating focus, input that writes a state. There is no syntax for any of them, and none to make migrating them easier: a migration repairs them (a `camera.params().set("fov", ...)` becomes the arrow `lens -> lens : zoom(fov)` and a functor to it).

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

Keep native programs at the build root, static libraries in `lib/`, law checks in `laws/`,
logs in `logs/`, and Wasm in `build/wasm`. Do not add nested packaging layers.

```sh
cmake --build build && ctest --test-dir build        # the fast tier: laws, graph watch, defaults, text, the DSL, every domain
ctest --test-dir build -C full                       # everything: also the networking process runs (pong, peers)
```
Run the full tier when the networking acceptance path changes and before a
release; a test that guards only a settled path goes in it (`CONFIGURATIONS
full` on its `add_test`, and its name in `_sg_full_tests`), never deleted.
Derived data a program makes again each start (meshes, paint) is kept on disk
by what made it (`sg::cache`): key it on every input and on its code's digest
(`stategine_code_digest`), so a cold cache and a warm one show the same.
- `graph.validate()` empty and `sg::verify(graph)` ok on every graph you touched.
- No new state without an interface to it; no game-loop code writing into a state it does not own.
- No module including what uses it.
- Frame time and law-check time not worse (see *Performance* in README.md).

## Voice

Comments and docs say what a thing *is* and *why*, in plain words, as the rest
of the code does - "a doorway goes both ways", not "iterates seams". Keep to it.
