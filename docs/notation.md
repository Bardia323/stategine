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
| `transition a -[e]-> b [carry f] [name n] [push] [with k = v] [when <comparison>]` | `graph.connect`; `* ` for any source; `-[e]-> pop`; `with` is `Transition::enter`, constants the entered state is told (data, not a lambda); `when` is `Transition::guard`: comparisons (`== != < <= > >=`, joined by `and`, `or`, `not`, brackets) of `from` (the state left's id), `from.<param>`, `from[<element>].<param>` and `arg.<name>` against numbers and words in quotes - a pure function of the state left and the event, which keeps its text (`sg/dsl/Guard.hpp`) |
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

There is no syntax for, and the compiler says why about, a second clock (a
state's time is its own line on a `Temporal`; a timer in its params would be
another), a write from one state into
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
state and elements, a transport's two elements.

**Natives are trusted extensions with restricted declared interfaces, not a
security sandbox.** The signatures keep ordinary code from being handed the
graph, the engine, a clock or an unrelated state; but a C++ closure can still
capture an external capability, and nothing here prevents that. What contains a
native is the engine's own checking (the laws re-run it on restored data, and
the graph is sealed while they do), the review of what a host registers, and
the rule that a source can only *name* what the host registered. The name
`native foo` is kept in the graph (`Morphism::native`, `Functor::native_of`,
`Edit::native`) so a comparison sees it, but two C++ functions called `foo` are
not proven equivalent. An edit is the only way a native rewrites the graph,
applied by the engine at the start of a frame.

## The world compiling itself

`src/dsl/compiler.sg` is a source document (`source`) and a compiler
(`compiler`) as ordinary states; the compiler's arrows are natives
(`sg/dsl/Compiler.hpp`). A source says `source.compile`; a functor carries the
text and the event to the compiler; the compiler reads and checks the text and
says `compiler.change`; the graph's `edit` (declared in the same source) makes
the plan on the running graph at the start of the next frame, and the answer
returns as `compiler.change.done`. A program compiled from inside does no IO
(`file(...)` is refused) and may name only the natives the host registered.

**Applying a plan is all or nothing.** `sg::dsl::apply` refuses at once what
names alone can tell (a name taken, a state or native that is not there, a
graph that is being checked). Then it takes a `StateGraph::Checkpoint`, keeps
the states the plan touches inside (a host that gains a look slot, a clock that
gains a timeline), makes the plan, and asks `graph.validate()` whether the
graph it made is as valid as the graph it found. If any step throws, or the
graph has a problem it did not have, `rollback` takes away what was added and
puts back what was replaced, and the touched states are restored - the same
graph, its states not copied or replaced. Nothing runs between the first step
and the last (an edit is applied at the start of a frame; no arrow or listener
is called), so no one sees a half-made ontology. A self-compiling world obeys
the same: a bad source, whether it fails to compile or compiles into a graph
that would not validate, leaves the graph as it was and the compiler says why.
What this does not do: it does not run `sg::verify` (the laws are the tests'
and the engine's watch), and out-of-memory in the middle is not made atomic.

**Reloading a source is one edit that replaces only what changed.**
`sg::dsl::reload(before, after, graph, natives)` (`compile_reload`, the
compiler's edit, asked with `text` and `before`) holds each declaration of the
source - a state with what it holds, a functor with its maps, a transition, an
embedding, a seam, a drive, a port, an edit - by its facts to what the source
said before and to what the graph has. What is the same is not touched; a
source reloaded unchanged changes nothing, down to a param's stamp and a
state's time (`sg_dsl_reload` holds it to that). A changed state is kept in
place - the same object, so its line on the Temporal goes on, an embedding
open on it stays open and focus stays where it is - its params carried (what
it has stays; what the source newly says is added) and its elements and
arrows made as the source says. A changed relation is taken away and declared
again by its name. A state's kind never changes, and a state, port or lens
is not taken from a running graph: such a reload is refused. All or nothing,
as `apply`: what it took away is declared again, what it made is taken away,
and the states it touched are as they were.

## Holding a declaration beside the C++ it replaces

`sg::dsl::facts(graph)` is one line for each thing the graph is made of,
sorted; `facts(plan)` the same for what a program declares, written by the
same functions. `missing(plan, graph)` is what the program declares that the
graph does not have. Port a declaration in this order: write it, compile it,
check `missing` is empty while the C++ is still there, run the laws and tests,
remove the C++, and check the graph's facts are as they were.

Facts are faithful to everything the engine holds as data. A transition's
constants are `Transition::enter`, so `with person = 1` and `with person = 2`
are different facts (`enter=[person=d:1]`); an arrow's affine rows, a
transport's stages, a composite's chain, an embedding's every field, a drive's
line and keeps, and an edit's reply are all in them. A native computation is in
its fact by name (`body=native:exit_won`, `object f x -> y native:copy_pose`,
`apply=native:compile_apply`), so `native foo` against `native bar` fails. C++
built by hand names no native: a plan fact with a native name is met by the
graph's unnamed one and reported by `unverified(plan, graph)` - not an error,
because a lambda cannot be told more, but not proven either. What canonical
lines cannot say, because a set has no order (the order embedded guests tick in,
the order time is kept in), is tested separately: the lab's golden file carries
`order drives`, `order embeddings` and `order transitions`. A transition's
guard is in its fact by the comparison it is (`guard=when(from.stock > 0)`);
a lambda's is `guard=opaque`, which meets a source's comparison as an unnamed
native meets a named one: `unverified`, not missing. What a doorway's
glue bakes in from the portals' poses at declaration is in the portals' own
params, which are facts too.
