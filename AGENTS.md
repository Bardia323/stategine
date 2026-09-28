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

| Interface | Declared with | What it is |
| --- | --- | --- |
| Transition | `graph.connect` / `push` / `pop`, on what a state `says` | the active state changes |
| Functor / lens | `add_functor`, `add_lens` | data carried across, by object and arrow - written by hand, or found by `sg::kan` (a Kan extension, compiled to an ordinary functor; not another kind of thing) |
| Embedding | `graph.embed` | a state lives in a portal of another (with `in`/`out` functors, a `subject`, a sync, focus) |
| Seam | `add_seam`, `glue_doorway` | two states glued along a boundary - a doorway, a door hanging in it |
| Drive | `graph.drive` | a state changes with the time a clock (`Temporal`) keeps: its arrows on a trigger fire with `{dt, time, frame}` |
| Adjunction | `Adjunction` | a pair of functors with unit and counit, checked |
| Port | `graph.port(state, event)` | the world outside (a program, a device) speaks to a state: `engine.send`, next frame |
| Edit | `graph.edit(state, event, fn)` | what a state says rewrites the graph: applied by the engine at the start of the next frame, answered back to the state |

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

A state's time is its line on the clock and nothing else: what shows or plays
it takes that time too - a world that keeps its own time says so
(`own_time`), and the renderer moves its shaders by it, not by its own
clock. Nothing that presents a state runs a clock of its own for it.

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
keeps its default as made). Build states so this works: what a state is must be
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

The build keeps the same rules. A machine is a state too, and the compiler
is the interface to it: a header is what a thing is - its declarations, its
types, its templates, its one-line bodies, and what every frame reaches for;
its `.cpp` is what it does. Each module (and the engine: `stategine`,
`stategine_render`) is a library; the compiler takes each file on its own
and all at once, and the linker is the one pass that brings them together.
A changed body recompiles its own file, never every file that includes it.
A body goes in a header only if it is a line, a template, or hot - and a hot
one says so in a comment above it (`// inline: every frame reaches for it`).
`sg_shape` (the engine's) and `stategine_check_modules()` (a project's) fail
on a module that includes what it does not use, or a header with a body that
has no reason to be there.

## Adding a state, a room, an interface - checklist

1. Its own header (and module) - what it is - and the header's own `.cpp` - what it does. Includes: the engine, modules under it. Nothing above. In a project, the module is `stategine_module(<name> USES ...)`, and `stategine_check_modules()` holds every module to this (see `cmake/StategineModules.cmake`).
2. Its data in elements and params; its behaviour as arrows on them; no hidden state.
3. Its look (if it is seen) worn by it.
4. How it is reached: which interface, from which state, at which portal. Declare it in the graph.
5. How it is acted on from outside: an event, through a focused embedding - not a method call from the game loop.
6. Its default holds (build it fully in its constructor or before `engine.start`; `keep_default` after deliberate setup).
7. Its content (if any) in a `TextStore` file, apart from its form.
8. A test that builds its graph **alone** and runs `sg::verify`, plus one that it is reached through its interface in the whole.
9. `CHANGELOG.md`: what changed, in plain words; **Breaking** where a project must change.

## Before you finish

```sh
cmake --build build && ctest --test-dir build        # engine tests: laws, graph watch, defaults, text
```
- `graph.validate()` empty and `sg::verify(graph)` ok on every graph you touched.
- No new state without an interface to it; no game-loop code writing into a state it does not own.
- No module including what uses it.
- Frame time and law-check time not worse (see *Performance* in README.md).

## Voice

Comments and docs say what a thing *is* and *why*, in plain words, as the rest
of the code does - "a doorway goes both ways", not "iterates seams". Keep to it.
