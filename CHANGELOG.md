# Changelog

Releases are tagged `vMAJOR.MINOR.PATCH`; the version lives in
`include/sg/Version.hpp`. Projects pin a tag, so every entry that can break a
project says so.

While the major version is 0, a minor bump may break the API.

## v0.3.0 (unreleased)

- **`Keeps::WhileEntered`: a state whose time goes on only while it is where one is** - the active state itself, not a guest open in another (a world shown in a painting stays still on the wall, and goes on once someone is in it).
- **A feed shown small holds still, and can be paint.** A world's feed is made again at every smaller size after it is drawn (`RenderTarget::mipmap`), so a screen seen from across a room - or neared - no longer shimmers with the fine detail of its picture. A feed panel with `untone` = 0 shows its picture as colour lit by the room (a painting), not as light taken back through the tone curve (a screen).
- **A header says what a thing is; its .cpp what it does.** The engine is compiled: every header keeps its declarations, its types, its templates and constexpr, its one-line bodies and what every frame reaches for (`State::find` / `element`, `Params`, `Key`); every other body is in the header's own `.cpp` (`src/<dir>/<Name>.cpp`), compiled once, on its own and all at once, and the linker brings them together. A change to a body recompiles its own file, not every file that includes its header. A 20-line law check that took 13 s to compile takes 3; the dev room's builds went from about 2 minutes a change to seconds. Frame and law times are as they were.
  - A game's modules are built the same way: `stategine_module(<name> USES ...)` makes a module its own library, and `stategine_check_modules()` adds one test that holds every module to the rules - it includes only itself, the engine and what it uses, and its headers keep only short bodies, templates and what a comment says why (`// inline: ...`). The engine holds its own headers to the same (`sg_shape`).
  - **Breaking:** `stategine::stategine` is a static library, not an include path. What draws is its own library: link `stategine::render` for `sg/gl/Renderer.hpp`, `Shaders.hpp`, `Math.hpp`, `sg/render/GLWorld.hpp` and `Ascii.hpp`. `sg/gl/Window.hpp` and `GL.hpp` stay headers (GLFW is the project's).
- **Copies of a state cost a pointer per element.** `Params` share their entries between copies until one writes (copy-on-write): a snapshot, a law's trial, a default kept no longer copies every name and string. Checking the dev room's laws went from about 2 s to about 0.5 s. `Params::text(key)` reads a text where it is (no copy), `Params::is(key, "text")` compares it.
- **States meet only through the graph - now at run time too.**
  - What a state `says` (`State::says(event)`) the engine hands to the graph's transitions: an arrow that `emit`s a said event takes the transition on it next frame, with no listener in between. A said event no transition takes is dropped, not handed to another state. What a state has said and the engine not yet taken (`said_out()`) is part of its data: kept by snapshots, versioned, compared by the laws (`<said>`), and a trial's is undone.
  - Events the engine routes into a state (from `fire`, from a drive) arrive by `State::hear`, never taken for something the state said.
  - **Breaking:** `State::engine()` is `const Engine*`: a state cannot fire, switch or push; it says. `switch_to` / `push_state` / `pop_state` stay for whoever holds the engine.
  - `EventBus::subscribe` is for watching from outside; while a law's trial runs, no listener is called (a listener cannot be undone).
  - `Engine::set_watch_hooks(true)` (`set_watch_updates` is the same switch) watches every hook - `on_update`, `on_event`, `on_render`, and `on_enter`, `on_exit`, `on_pause`, `on_resume` as the engine runs them - and reports one that writes the state's data; `State::wrote_in()` names it.
- **Subscribers may observe the world; only the world may change it.**
  - **Breaking:** while a bus listener runs, `Engine::fire`, `switch_to`, `push_state`, `pop_state`, `open_embed`, `close_embed`, `sync_embed`, `focus_embed`, `State::emit` / `hear` and any change to the graph or to what a state is made of throw `sg::ObserverError`. `sg::set_observers(Observers::Report, fn)` says each once and lets it through instead, for a project moving its listeners into the graph. The check is one thread-local read where it is made.
  - A said event crosses an open embedding whose `in` or `out` functor names it (`Functor::on_event`), relabelled, to the state on the other side (`Functor::maps_event`, `carried`); the engine indexes these by state when the graph changes.
- **Edits: the world rewrites itself only where the graph says.** `graph.edit(state, event, fn)` declares that what `state` says on `event` (it must `says` it) rewrites the graph by `fn(graph, asked) -> Params`. The engine applies edits at the start of the next frame, before transitions, in the order asked; the answer is heard back by the state as `<event>.done` (`Edit::reply`). Never inside an arrow, a listener or a law's trial. Validated: an edit no state says is named.
  - An embedding's `in` functor carries events said by its host as well as its subject.
  - A functor maps events too, so a registered functor out of a state that names a said event (`on_event`) carries it, relabelled, to its target, whose arrows run on it at once - a declared wire between two states neither of which holds the other. An embedding's functor carries only while it is open; a transition's only when taken. A functor that carries events reaches its target, for `validate`. A functor carries its objects with the event, as a transition's functor does - what moved and that it moved arrive together - whenever the state that said it is the functor's source (an embedding's `in`, carrying what its host says, carries only the event).
- **Ports: the world outside reaches a state only where the graph says.** `graph.port(state, event)` declares that `event` may come in to `state` from outside (a program's output, a sensor); `Engine::send(state, event)` delivers it at the start of the next frame, and refuses one the graph does not declare (and one sent from a listener).
- `StateGraph::lens` declared again as it was changes nothing (the graph's revision does not move).
- `StateGraph::lens(get, put)`: two functors declared a lens on their own, with no embedding - a window showing a board it can also draw on - held to the lens laws (put-get, settles, put-put) by `sg::verify` like an embedding's pair.
- `Surface2D::stale()`: a surface says its picture no longer matches its own data (a copy carried in by a functor), and `raster()` paints again - a picture memoised on its data, never told from outside.
- **Where time and input go, the graph says.**
  - `Keeps::WhileShown` (time goes on while some embedding shows the state open, wherever it is) and `Keeps::WhileFocused` (only while it has the input: its drive does not fire otherwise).
  - A host opens, closes and focuses its own portals by saying `portal.open`, `portal.close {commit}`, `portal.focus {on}` with `{portal}`: whatever the graph embeds there, done at once. Saying what already is moves nothing (`focus_embed` is idempotent).
  - Input goes to the deepest focused embedding that is open in a live host - the current state, or open in one that is - not merely the one focused last: a game focused in a computer nobody sits at hears nothing, and one in a focused computer comes before it.
  - What any state said in a frame is heard by the end of it, whether the engine stepped it or not (one pass over the states, in place: `StateGraph::each_state`).
- **A camera is a state of its own** (`sg/domains/Camera.hpp`, in `sg.hpp`). `sg::Camera`: one object, its lens (a pose, a field of view, the picture's size), aimed and zoomed by its own arrows (`camera.aim`, `camera.zoom`); it needs no world to be. `sg::film(graph, camera, world, rig)` has it see a world: the world embedded in its lens, taking no input, and - with a `rig` - the rig's world pose carried onto the lens by the embedding's `out` functor. A screen shows the picture by `shows` = the filming's name, so a camera can film the room its screen stands in and, pointed at the screen, see itself seeing.
  - The renderer draws an open embedding in an `eye` portal from the portal's own pose. Feeds are kept by the view on the screen and drawn there, the deepest first, for every screen in view or standing in a world another feed shows; the views that draw feeds and far rooms show those same pictures. Each feed has two pictures, one drawn while the other is shown, so a feed that sees its own screen sees the picture it made the frame before.
  - An embedding joins its two states both ways for `validate`, as a seam does: a camera is reached from the room it films.
  - A state stepped on its own - the active one, or one that keeps time always - is carried Live into whatever it is open in (`out`), as a guest stepped inside its host is.
  - A feed is seen through the same doorways as the view on the screen: a camera stood inside a door shows the room beyond it. Views drawn for another carry doorways from their own eye, and draw own-look doorways as any other, so views never nest for ever.
- **An embedding can follow its portal** (`StateGraph::set_follows`): open exactly while the portal's `open` says, done by the engine at the end of each frame and when it starts. The host decides by its own arrows - a set showing a picture while it is on, a deck its output while a tape plays - and the engine only follows; `open_embed` / `close_embed` leave such a portal's `open` alone. A camera's filming follows its lens: it runs while the lens is `open` (`camera.roll {on}`).
- **The laws, compiled** (`sg/algebra`, `sg/gpu`). What a step does can be said (`sg/core/Declared.hpp`): `State::affine` makes an arrow from an `Affine` (each parameter it sets a sum of parameters times numbers, perhaps times an event argument, plus a number), and `sg::transport::only`, `swizzle`, `copy_all` and `affine` are `transport::Declared` - the function is made from what is said. `Functor::for_each_declared` gives each mapping's; composites (of arrows, of functors) say what their parts say. The integrator of every `Spatial` body is declared.
  - A `laws::Accelerator` set in `LawOptions::accelerate` is offered each equation first; `algebra::Accelerated` compiles those whose every step is declared (`try_compile`) into two programs over the live data, kept per graph and compiled again only when what they read changed; sides that leave a parameter the very same expression hold it the same, and are not run; the rest run in a batch on a backend (`algebra::CpuBackend`; `gpu::CudaBackend`, `RocmBackend`, `VulkanBackend`, `MetalBackend`, built in where their toolchains are found - cmake/gpu.cmake - and running on the CPU otherwise). An equation the batch finds apart, or too near the tolerance to say, is checked again by the verifier, whose counterexample is the one reported. Everything else is run as before. The laws stay the truth.
  - **Breaking (small):** `transport::copy_all`, `only`, `swizzle` are `transport::Declared` values, not functions returning `Transport`; they convert to `Transport` where one is wanted. `Morphism` has a `declared` member (aggregate initialisers that name every member need one more).
- **Arriving is heard.** A state the engine enters - by a transition (`StateGraph::cross`, so the laws take it the same way) or by hand (`switch_to`, `push_state`, `pop_state`) - hears `state.entered {from, by}` (`StateGraph::entered_event`), for its own arrows to answer: a room that says it was entered, and whoever keeps its ways in closes one.
- **Kept functors** (`StateGraph::keep`): a functor whose target follows its source, carried by the engine at the start of each frame, after the edits (so a target that asked for something is answered before it follows again), with a memo - nothing changed, nothing carried. A kept functor reaches its target for `validate`; dropped, it is kept no more. A model on a table kept to the room it is of, with no portal between them.
- **The view crosses seams.** Each frame the engine carries the viewer's eye across every seam of the state it is in, by the seam's own travel (`Engine::look_across`): the room beyond a doorway is aimed from where it is seen, its sky and ground with it. The first seam onto a room is the one it is seen through. A project that carried cameras by hand can stop.
- **Depth is counted, not guessed.** `Engine::depth` (and `live`) follows open embeddings to any depth, each state looked at once, so a chain of any length counts and embeddings that go round end the search - no limit of eight. **Breaking:** `depth` takes no `limit`. `world_pose` follows a parent chain to its end, stopping only where it would go round; it takes no `max_depth`.
- **How a room is drawn is the renderer's.** `GLWorld::spill(light, surface, most, on)`: a lamp whose colour and strength follow a picture (a screen's glow on the room), eased frame by frame by the renderer, never written into the world. `GLWorld::rays(room, at, strength, colour)`: daylight's shafts, aimed from whatever camera the room is drawn with.
- A functor carrying on an event carries whole (its transport may read more than its two elements); it runs only when its source says so.
- **One name, one thing.**
  - **Breaking:** an arrow's name is unique in its state (`add_morphism` refuses a second); a transition's in its graph (`connect` refuses a name given twice, and numbers a made-up one, `from-trigger->to#2`, for a guarded alternative on the same event).
  - **Breaking:** a functor a graph holds keeps its name: `rename` to another name throws, and assigning one under another name throws (use `set_functor`); assigning one under its own name is counted as a rewiring.
- **A transition means one thing.** `StateGraph::cross` - action, then the functor carrying the event - is how the engine and a law's `Path::transition` both take a transition. In a law, the step is taken only if the engine would take it (its guard passes and nothing it prefers does - the error names what it would take instead), and runs the lifecycle as the engine does; a pop returns to where the path last pushed from.
- Names are indexed: `State::morphism(name)`, `StateGraph::transition(name)` and the refusal of a duplicate are constant time (declaring 20k arrows and 5k transitions: 1.5 s -> 26 ms). A transition carries its event without allocating.
- `Functor::compose` maps every event: one the first functor leaves as itself is mapped by the second (`(G . F)(x) = G(x)`), and an identity's own event map is kept.
- README: the battle `says("victory")` instead of a listener that fires the engine; the functor example uses `connect(from, trigger, to, functor)`.

- **Time is a state.** `sg::Temporal` (`sg/core/Temporal.hpp`) keeps timelines: an element each with `time` and `frame`, moved by its own `advance.<line>` arrow. `sg::drive(graph, clock, state, trigger, additive)` gives the state its line and declares the drive; one clock can keep a whole world's time. `graph.drive(name, clock, state, trigger, additive)` declares that a state changes with a clock's time. Each frame a driven state steps, the engine advances its clock by `dt` and fires `trigger {dt, time, frame}` from the clock. A line keeps one state's time (a line kept for two drives is refused) and moves only when that state steps, so time and motion agree after a pause. An additive drive is handed no `frame`. The engine and the laws build the event the same way (`sg::drive_event`). A driven state reaches its clock; a clock alone reaches nothing.
  - New law, `drive` (in `sg::verify`): `step(0) == id`, and for an `additive` drive `step(a) ; step(b) == step(a + b)` (`LawOptions::drive_dt`).
  - `Keeps::Always` (`sg::drive(..., additive, Keeps::Always)`): a state that keeps its time while not active. The engine steps it once a frame if nothing else did, with what is open in it, and carries its open Live embeddings out - what a game loop used to do by calling `step` itself.
  - Paths gain `event(trigger, args)` - fire an event at a state as a frame does, with what it sets in motion - and `arrow(name, args)`, a step with arguments of its own.
  - `Engine::set_watch_updates(true)` reports a state whose `on_update` changes its data rather than emitting for an arrow. Off by default; `on_update` is unchanged.

- **The laws see all of what they compare.**
  - Queued events are compared whole - name, sender and arguments - when two paths are checked against each other; before, only names were, so `damage(5)` and `damage(500)` passed as the same result. A counterexample names the arguments.
  - `State::content_version()` versions each queued event (name, sender, arguments), not only how many there are, so a `LawCache` no longer answers from a queue that has changed under the same count.
  - `sg::is_lossless` counts an object or a parameter the round trip drops as lost; before, only changed values were. `identity_defects` takes `whole = true` for the same.
  - **Breaking:** `Cover::sections` checks descent first and glues nothing for a cover that fails it; an optional `std::vector<std::string>* seams` says why.
  - Queued events are compared in the order queued, as they are dispatched: `a, b` is not `b, a`.
  - **Breaking:** `sg::enforce` throws when an equation could not be checked (`LawReport::holds()`), not only on a counterexample. `LawReport::bounded` / `complete()` report a search stopped at its `LawOptions` budget (associativity's `max_triples`), which before was silent.
  - **Breaking:** descent is checked on every loop of a cover (one per overlap outside a spanning tree), not loops up to four; `Cover::descent_defects`, `cocycle_defects` and `sg::descent_defects(atlas, g)` lose `max_cycle`. `Cover::sections(g, root, seams)` loses `max_depth` and reaches every piece joined to the root.
  - `Tick::time` is simulated time, the sum of every `dt` (`Engine::simulated_time()`), not the wall clock: a `run_fixed` run is the same every run.
  - Docs say what is so: a law trial refuses structural change inside a state as well (a `StateGraph` comment said otherwise); `sg::verify` runs the laws the graph owns, not descent or adjunctions; a `Functor` is partial; functoriality is the per-arrow square; `add_lens` declares, `verify` checks.

- **Driven bodies, friction by the part, and a doorway's edge lit softly.**
  - `World::drive(body, x, r)`: moved by the game to where it should be by the end of the next step, at the speed that takes - furniture hauled, a board pulled by its stand, a lift. For that step it is as heavy as the room: what it meets is pushed out of its way, what lies on it goes with it by friction, and what sleeps against it wakes at once. It ends the step still.
  - `Hull::friction`: a part's own friction, where it differs from its body's (a castor rolls; the tray above it grips).
  - A thing standing through a doorway - a door ajar - is lit from all round as one thing: near the opening, and only in it, the light from all round is blended halfway to the other side's (`uDoor*`, `around_at` in `scene_fs()`); a shut door lets none of it in. Before, the part of a door swung past the opening was cut off in the other side's light.
- **Rigid bodies: through nothing, casts, sensors and walkers.**
  - Nothing is let through a thin wall: a body that went far this step for its size is swept along its way, turning as it went, against what does not move, and stopped where it first met it (conservative advancement by `apart`, the widest gap along any axis that could part two hulls). How far ahead pairs are looked for is the step's own length, not a sixtieth of a second.
  - `World::cast`: a hull carried along a line - what it meets first, how far along, and which way that faces.
  - Sensors (`Body::sensor`): nothing bumps into them; `World::inside`, `entered`, `left` say what is in each, and what came and went this step (what lies still in one stays in it).
  - Walkers (`sg::rigid::Walker`, `World::walk`): someone on their feet - sliding along walls, up and down stairs up to `step` high, standing on slopes up to `slope`, falling off edges, carried by what they stand on as it moves and turns, and shoving what is light aside.
- **Rigid bodies: pairs by sweeping, joints, and nothing flung on waking.**
  - Pairs are found by sweep and prune along x (the order kept from step to step, put back by insertion; a thing at rest never compared with another at rest) - the same pairs as every-against-every, and the same motion to the last bit; 2002 bodies: 119 ms a step to 28 (unoptimised). `World::sweep` (on) compares with the old way; `World::touching()` lists the pairs touching.
  - The solver meets contacts in one order, by pair and hull, whatever found them - before, the order a hash table grew in decided it, and two runs from the same start could part.
  - A contact's impulses found while one of its two bodies slept (pushing against it as against a wall) are not handed to it when it wakes: a marker on a tray pulled from under it was flung off. The same for joints.
  - Joints (`World::ball`, `hinge`, `spring`; `angle`, `unjoin`, `joints`), to another body or to the room: a ball holds two at a point; a hinge also lets them turn only about an axis, with limits, a motor (speed, most torque) and a spring to an angle (a door closer); a spring draws two points to a length. Solved with the contacts, softly (`joint_hertz`), warm-started; what is joined is one island, and a hinge still being driven or drawn does not sleep.
- **Light through a doorway comes in as the door opens.** A doorway is passed over only when one thing lies flat in it and fills it; before, a door's panels lying on its leaf were counted over again, and a door half open counted as shut until it was nearly wide open. `tests/doorway_light_gl.cpp` swings a panelled door open.
- **Things of one shape are drawn in one call.** Meshes and walls are batched by their shape and drawn instanced - each with its own place and material (albedo, roughness, surface, emissive, mirror) as per-instance attributes - in the scene and in every shadow layer; a thing wearing a skin, or pointed at, is drawn on its own as before. `GLQuality::instancing` (on) turns it off to compare; `FrameTimes::draws`, `instanced`, `shadow_maps`, `signature`. Where a thing is, its box, its mesh and its instance record are kept from frame to frame and worked out again only when its parameters (or its anchor's) change - by their stamps - and shadow maps are drawn again only when something that casts has moved by a tenth of a millimetre or more; each view keeps its own maps (two windows onto one world no longer draw over each other's). 3600 things: 16.0 ms a frame to 3.6-4.4; the dev room's scene 3.1 ms to 1.6, its shadows 1.1 ms to 0.1. The pictures are the same (`tests/instancing_gl.cpp` holds them to it, pixel for pixel). A look with its own scene or depth shader takes `iLocal`/`iMat0`/`iMat1`, `uInstanced` and `uFrame` from `scene_vs()`/`depth_vs()`, and reads the material through `mAlbedo`... as `scene_fs()` does - **Breaking** for such a look.
- **Light goes through a doorway.** A room is lit by its own lamps and by those of each world its doorways open onto (the strongest three, and a glow of that world's sky just beyond the opening), carried into its frame by the doorway's own travel and let in through the opening only - both ways, so a realm's sun falls across the floor of the room glued to it and the room's lamp lights the terrace outside. Such light casts real shadows: each gets a shadow map drawn from where it comes in, with only what stands on this side of the opening as casters, so a door ajar throws its own shape across the floor (past the maps there are, what stands in the opening keeps out as much as it covers). A portal with `light` = 0 lets none through, for a room that lights an opening itself. Shadow maps are one array of layers (`gl::ShadowArray`, grown as wanted, up to 8: a room's own four strongest lights, then its doorways'), light space worked out per fragment: `uShadowMaps` (a `sampler2DArrayShadow`), `uShadowVP[i]`, `uShadowBias[i]` replace `uShadowMap0..3`, `uLightViewProj0..3` and the `vec4 uShadowBias`; the depth pass takes `uCasterSide`. The scene shader takes up to 24 lights (was 16), `uLightGate`/`uLightGateAxis` per light and `uShadowCount`; **Breaking** for a look with its own scene shader: take `through_gate()`, `shadow_factor()` and the shadow uniforms from `scene_fs()`, or light from beyond a doorway reaches everything in the room. `tests/doorway_light_gl.cpp`.
- **Pay when the structure changes, not while it stands.** Work the engine keeps is derived from the one graph and thrown away when what it was found on changes; nothing about what a game means changes.
  - Stamps: `Params::stamp()` (a new one per change; setting an equal value is no change), `State::structure()`, `State::content_version()`, `Functor::stamp()`, `sg::next_stamp()` / `last_stamp()`.
  - Portals carry only what changed: `Embedding::propagate` (`Propagation::OnChange` by default, `Continuous`, `OnEvent`, `Manual`), `Engine::sync_embed(name)`, `Functor::apply(src, dst, memo)`. **Breaking** for a Live or View functor whose transport reads anything but the two elements' params (the clock, another element): declare that embedding `Continuous`.
  - Routes (an embedding's states and functors) resolved once per graph topology; one guest open in several places is found by an index, not a scan of every embedding.
  - `validate()` reuses what it found for states and functors whose structure did not change, and reachability until the interfaces change (`validate(false)` checks everything). Reachability follows an index: O(states + interfaces).
  - `LawCache` and `verify(graph, cache, ...)`: answers kept with the versions they were found on; a side of an equation that did not change reused; `Adaptive` (checks directly what is cheaper to run than keep), `Incremental`, `Direct`. Plain `verify` is faster too: sides that left the same stamps agree at once, and a side's end state is taken rather than copied (the dev room's laws: 3 s to 0.75 s).
  - A composite functor's middle element is a kept buffer (no allocation per object once warm). Removing an element renumbers only those after it.
  - `tests/test_incremental.cpp` holds each kept way to the plain way's answer; `tests/bench_scale.cpp` (`sg_bench_scale`) measures them at 1K..1M objects.
- **Who may change what.** The engine's authority is the graph it models, and the compiler holds it:
  - A const `StateGraph` gives const states, functors and embeddings (`find`, `state`); the graph's owner keeps mutable access. **Breaking** for code that changed a state through a const graph.
  - `Engine::current()`, `focused()` and `graph()` are const; act by firing events. `Engine::stack()` returns the states as `const State*` (by value). **Breaking** for a game loop that wrote into `engine.current()`.
  - Declared, then read: `embed` returns `const Embedding&`, `embedding(name)` `const Embedding*`, `embeddings()` a const list; `connect`/`push`/`pop` return `const Transition&`, `add_seam` `const Seam&`, `add_morphism`/`arrow`/`loop`/`compose`/`add_integrator` `const Morphism&`, `wear` `const Embedding&`. New: `set_focus(name, on)` (not counted), `set_sync`, `set_propagation`, `set_carry(transition, functor)`, `connect(from, trigger, to, functor)`. **Breaking**: `graph.embed(...).focus = false` becomes `graph.set_focus(graph.embed(...).name, false)`; `graph.connect(...).functor = f` becomes `graph.connect(from, trigger, to, f)`; a guard is set on a `Transition` before it is connected.
  - Structure is counted where it changes: states, elements and arrows added or taken away, a functor's maps (`on_object`, ... on a functor the graph holds), embeddings, seams, transitions, the initial state - one add each (`revision()`; the interfaces alone, `topology()`). Values never count. The engine validates at its next frame after the count moved, and not otherwise.
  - Checks do not rewrite: while a law's trial runs, nothing structural is let happen - not what joins the states, and not what is in one (an element or an arrow added or taken away). It throws `RewriteRefused` before anything changes, the trial's data is put back, and the equation is reported as **unchecked** (`LawReport::unchecked`, `Violation::refused`, `all_checked()`), apart from counterexamples: neither broken nor shown to hold. Nothing structural is ever rolled back, because nothing structural happens. **Breaking**: a functor whose target elements do not exist yet can no longer be checked on trial (it would create them) - give the target its elements, or read it as unchecked.
  - An element's identity is fixed: `Element::id` and `kind` are `ElementKey`s - read like a `Key` everywhere, set only when the element is made. `State::elements()` on a mutable state is an `ElementRange`: each element to act on, never the list (to add or take away is `add_element` / `remove_element`, counted). **Breaking** for code that renamed an element in place, or pushed into the list; `cond ? e.id : Key{}` needs `e.id.key()`.
  - The renderer draws only what can be seen: a mesh or wall wholly outside the view is passed over (the dev room leaned in to its screen: scene CPU 8-21 ms to about 1 ms). A sun's shadow map is drawn again when the sun has moved about a fifth of a degree, not every frame of the day. `FrameTimes::feeds` / `feed_views` time screens showing whole worlds.
  - The renderer only looks: `GLWorldView` takes `const Spatial3D`; `PlacedRoom::room` is `const Spatial3D*`; `bind_world`/`bind_feed` take const. **Breaking** for code that moved a camera through `PlacedRoom`.
  - `examples/room3d.cpp` walks, looks, slides the lamp, moves map tokens and sounds the alarm by events and arrows instead of writing into the room from the loop.
  - `tests/test_authority.cpp`: what watching may not do (static_asserts), what the legitimate ways still do - an embedding acting on a subject that is not its host, Live, Commit and View - which operations are counted and which are not, and a check that tries to rewrite the graph.

- **Physics** (`sg/physics`): solvers a state steps in its own arrows, on plain data it keeps in its params, so the laws can run them again from any moment.
  - `sg/physics/Rigid.hpp` (`sg::rigid`): rigid bodies - convex hulls, contacts found by separating axes and kept from step to step, a soft-step impulse solver in substeps, friction, bounce, islands that sleep, and a hand that holds a body by a spring at a point of it. Moved here from stategine-lab, where the dev room's loose things have used it; its tests come with it (`sg_rigid`). `outline(parts)` picks which of a thing's many parts make its shape: its big ones, and every small one at its outside (every castor, a chair's top rail) - so a thing turned over lies on what sticks out, not half through the floor.
  - `sg/physics/Rope.hpp` (`sg::rope`): a rope of equal links between two ends - damped by the air and by itself (a pull runs along it once and dies, no ripples), resisting bending evenly, never stretching (each joint within the rope's length of either end), led in substeps when an end moves far, lying on the floor and over blocks and lumps and never passing through them (a link leaving a top leaves at its edge), gripped where it lies. `Rope::laid` lays one out at its whole length; `Rope::within` keeps a thing on its end within reach. Tests: `sg_rope`.

- **Rooms of any finish.** A room's floor and ceiling take `floor_surface` and `ceiling_surface` (and `ceiling_r/g/b`); a wall element takes `surface`. New materials laid in the room's own metres, so they keep their size on any surface: 10 planks, 11 concrete, 12 checker, 13 brick, 14 carpet, 15 metal plate, 16 grass.
- **The attended state's look.** `GLWorldView::attend(state)` lays the active look of the state the viewer is attending to (an interface they sit at) over every room's own, blended by the look fader like any look. `uDim` in a scene look dims everything but a CRT screen's picture - eyes adjusting to a screen held close.

- **Faster frames.** Shadow maps are kept per world drawn and drawn again only when their lamp or anything that casts has moved - most frames, none are. Each element's pose and box matrix is worked out once a frame, not once per pass. A `Key` made from a string literal is found by the literal's address (checked against its characters), with no string built or hashed; a `Key` from a `std::string` no longer copies it when it is interned already. Uniform locations are found by the name's address the same way. Nothing in the API changes.
- **Better light, same cost.** The scene shader is energy-conserving Cook-Torrance: GGX with Smith masking and Schlick Fresnel, the diffuse losing what is reflected, and the ambient reflecting the sky and the floor by angle and roughness (Karis's environment fit). The brushed-metal surface (5) is partly metallic. Shadows filter over a Vogel disc turned per pixel (16 taps, down from 25): smooth penumbrae, finely dithered. The bloom adds a wide glow from a mip chain (`wide` on the blur pass, 0..1, default 0.5 - how much spreads far rather than near). Occlusion now darkens only light from all round - the ambient, and lights with `indirect` = 1, which stand in for bounced light, cast no highlight and sort after every real lamp for shadow maps - so a corner in lamplight stays lit; the scene's alpha carries that share. **Looks with their own composite:** `gl::film_glsl()` gives `tonemap(hdr)` (the ACES curve, half per channel and half on luminance, so bright colours keep their hue) and `film(c, grain, time)` (encoding, soft moving grain weighted to the mid tones, dither). The standard composite uses it; `uGrain` now means grain in display light, where before it was added in linear light.
- `Vec3d` has `+`, `-` and `* double` in the engine, so no two worlds write them out and disagree. **Breaking** for a project that defines its own for `sg::Vec3d`: take yours out.
- **The graph is watched.** States are distinct and meet only through what the graph declares - transitions, embeddings, functors, seams - and the engine now keeps checking that they do: after any tick in which what the graph is made of changed (`StateGraph::revision()` counts every change), it runs `validate()` again (at most every `set_watch_interval` seconds) and reports each new problem to `Engine::on_problem`, else stderr; `set_strict(true)` throws instead; `check_graph()` checks now. A state added with no interface to it is caught the moment it appears. Reachability now also follows seams: a room glued by a doorway is reached through it.
- **Every state can go back to how it started.** The engine keeps each state's default as it starts (`StateGraph::keep_defaults`, `keep_default(id)` for one made later or to make how it is now its default); `restore_default(id, guests)` puts it back - with `guests`, whatever lives in its portals too. `State::on_restored()` lets a state refresh what follows from its data (a surface repaints).
- `Engine::focus_embed(name, on)`: give an open embedding focus, or take it away - input goes to its guest while it has it. `StateGraph::drop_embedding(name)`.
- The renderer shows what the graph declares: a portal with `feed` = 1 that an open embedding puts a 3D state in shows it as a feed (`feed_w`, `feed_h`, `live`), with no `bind_feed` needed.
- `Spatial3D::fixture(...)`: a mesh that stays where it is put - no velocity, so no arrow of its own. A world of thousands of them (architecture, leaves) costs its laws and its frames nothing; checking the identity law on one went from 35 s to nothing.
- **A state as text** (`sg/core/Text.hpp`): `to_text(state)` writes its data - its params, and each element's id, kind, whether it is there, and params - as the state's own source; `from_text(state, text)` reads it back (`exact` to drop what the text lacks). Every value round-trips exactly.
- `to_text` can leave out what is not part of what a state is (`keep_element`, `keep_param`: the camera, a clock), and writes numbers in their shortest exact form - five times faster.
- **Texts in files** (`sg/core/Store.hpp`): a `TextStore` keeps texts under keys, each bound to a file - read once, served from memory, written only when changed, and read again only when `poll` sees the file's time stamp move (a few files a call, each at most every `interval`). Line ends are read the same however the file was saved. For content that lives apart from what shows it, edited while the game runs.
- **A world on a screen.** `GLWorldView::bind_feed(panel, world, w, h, live)` shows another 3D state on a panel as a picture: drawn whole from its own camera, in its own look (its composite too), at w x h, only while the panel is in view; not `live`, it holds its last picture. The picture is laid on the panel as a surface is, so a `crt` panel shows it through its glass, and it is taken back through the tone curve first so developing it again with the room does not flatten it. The look belongs to the world, the glass to the screen.
- A camera can tip its head: `roll` (radians) about the line of sight.
- **Skies with clouds.** `uClouds` (how much of the sky, 0 none), `uCloudColor` and `uCloudShade` in the scene pass: a drifting layer, lit towards the sun and gold at its rim.
- Materials 17 marble (slabs, soft veins) and 18 water (travelling swells and chop, in its normal). `mirror` (0..1) on a mesh reflects the real sky - clouds, the sun's glint - instead of the light from all round, as glossy stone and still water do under an open sky.
- A sun's shadow fades out towards the edge of its box instead of stopping on a line.
- A lamp can fall off as real light does: `falloff` (0..1) on a light blends the soft falloff (0, as before) into the inverse square (1). A screen's glow lights the desk, not the far wall.
- **Light that is the same in every world** (`sg/domains/Light.hpp`, in `sg.hpp`; GL-free). `sg::daylight(hour, ground)` gives the sky at an hour - the sun's (or the moon's) way, colour and strength, the sky's colours, the light from all round, exposure and stars - and `sg::show_daylight(look, d)` puts it into a look. `sg::aim_rays(look, camera, dir, strength, colour)` aims light shafts, now drawn by the standard composite too (`gl::godrays_glsl()`; `uRays` 0 by default, so nothing changes until aimed). `sg::spill(lamp, surface, most)` makes a lamp stand in for a glowing screen, taking its picture's colour and brightness. `gl::fxaa_glsl()` gives the composite's edge smoothing to looks with their own.
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
