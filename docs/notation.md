# The notation

A textual presentation of a construction the engine already has. It is not a
scripting language, a scene graph, a scheduler or a second state system: every
construct is one call of the engine's own API, and there is one runtime, the
engine's. `sgc` reads a source, resolves names, holds it to the ontology and
writes C++17 that makes the same graph through `StateGraph`, `State`, `Functor`.
Then `graph.validate()`, `sg::verify` and the engine's watch check the result,
as they check a graph built by hand.

```
source ─ parse ─ resolve ─ ontology ─ lower ─┬─ emit ──► C++17 ──► compiler ──► engine
                                             ├─ apply ─► a live graph (graph.edit)
                                             └─ facts ─► canonical lines (compare)
```

Every construct, and the primitive it becomes (`sg/dsl/Plan.hpp`):

| Notation | Becomes |
| --- | --- |
| `state x : kind { ... }` | `graph.add<Class>("x")`; kinds: `state`, `temporal`, `spatial2d`, `spatial3d`, `look`, `camera`, `console` |
| `extern state x [: kind]` | nothing made: the graph must hold it (built in C++); its relations are declared here |
| `key = value` in a state | `state.params().set` |
| `element e [: kind] { key = value }` | `State::add_element`, and `Element::params.set`; on an element the state's own class makes, the parameters are set on it |
| `a -> b : name(args) [on event]` | `State::arrow` (`State::loop` when `a` is `b`); the trigger is the name unless `on` says |
| `... { x = x + vx * dt }` | `State::affine`: an `Affine` (sum of parameter x number, times one event argument) - the laws can check it without running it |
| `... native name` | the arrow's handler, looked up in `sg::dsl::Natives` |
| `compose c = f ; g [on event]` (in a state) | `State::compose` |
| `say event` | `State::says` |
| `functor f : A -> B { object x -> y ... }` | `graph.add_functor`, `Functor::on_object`; transports `only(...)`, `swizzle(z = x) [rest]`, `{ y = 2 * x + 1 }` (an `Affine`), `native n`, `via alias` |
| `event a -> b`, `arrow f -> g` (in a functor) | `Functor::on_event`, `on_morphism` |
| `extern functor f : A -> B` | nothing made: the graph must hold it |
| `compose f = g ; h ; ...` | `graph.compose_functors` |
| `lens get <-> put` | `graph.lens` |
| `transition a -[e]-> b [carry f] [name n] [push] [with k = v]` | `graph.connect`; `* ` for any source; `-[e]-> pop`; `with` is `Transition::action` telling the entered state constants |
| `embed h.p -> g [in f] [out f] [subject s] [sync live/commit/view] [propagate ...] [focus b] [follows b] [name n]` | `graph.embed` |
| `seam a.p <-> b.q [also x <-> y] [name n]` | `sg::glue_doorway` (the boundaries identified both ways; its `<name>.ab` / `.ba` travel functors are named for `carry`) |
| `drive clock -> s.event` / `drive clock -> s event e [keeps ...] [additive]` | `sg::drive` on a `Temporal` |
| `port s.event` / `port s event e` | `graph.port` |
| `keep f` | `graph.keep` |
| `edit s on event native n [reply e]` | `graph.edit` |
| `initial s` | `graph.set_initial` |
| `wear h <- look` | `sg::wear` (sugar) |
| `film cam -> world [rig e]` | `sg::film` (sugar) |
| `when e1 e2` | `Functor::on_event` of a functor between the two states (sugar) |
| `bind device { key -> event(k: v) }` | a table (`sg::dsl::Bindings`) an input adapter turns into `Engine::fire` (sugar) |
| `transport alias native n` | nothing: the alias is replaced by the native where it is used (sugar) |

`state.event` names an event by the state it is named for (`desert.day` is
state `desert`, event `desert.day`); an event not named for its state takes the
long form (`drive time -> doorway event door.step`). Numbers are doubles, as a
world's parameters are; `int(5)` is an integer. `[a, b, c]` sets `key.x`, `key.y`,
`key.z`. `file("x.frag")` is the text of a file, read when the source is compiled
- not by a state, not while the world runs.

## What it will not say

There is no syntax for, and the compiler says why about, a private timer (a
state has one time, its line on a `Temporal`), a write from one state into
another, a callback, `on_update`, `emit` as orchestration, a mode that
duplicates focus, an input that writes a state, IO in a transport or an arrow.
A `when` cannot carry a constant argument (a functor relabels an event and
passes its arguments as they are). An arrow that reads `dt` must be driven.

A seam is a shared boundary and a transition a directed change of active
state; neither makes the other. Between like states a transition may carry only
the seam's own travel functor (`carry gate.ab`): the engine's seam law says so,
and `sg::verify` says it when it is broken.

## Native computations

`native name` is the inside of an arrow, transport or edit whose declaration
(ends, trigger, arguments) is written here. It is registered in C++
(`sg::dsl::Natives`) and given only what its type gives it: an arrow's own
state and elements, a transport's two elements. It is not a place for a hidden
state, a private timer or a call into another state; the laws re-run it on
restored data, the graph is sealed while they do, and an edit is the only way a
native rewrites the graph, applied by the engine at the start of a frame.

## The world compiling itself

`src/dsl/compiler.sg` is a source document (`source`) and a compiler
(`compiler`) as ordinary states; the compiler's arrows are natives
(`sg/dsl/Compiler.hpp`). A source says `source.compile`; a functor carries the
text and the event to the compiler; the compiler reads and checks the text and
says `compiler.change`; the graph's `edit` (declared in the same source) makes
the plan on the running graph at the start of the next frame, and the answer
returns as `compiler.change.done`. A program compiled from inside does no IO
(`file(...)` is refused) and may name only the natives the host registered.

## Holding a declaration beside the C++ it replaces

`sg::dsl::facts(graph)` is one line for each thing the graph is made of,
sorted; `facts(plan)` the same for what a program declares, written by the
same functions. `missing(plan, graph)` is what the program declares that the
graph does not have. Port a declaration in this order: write it, compile it,
check `missing` is empty while the C++ is still there, run the laws and tests,
remove the C++, and check the graph's facts are as they were.
