# stategine

<p align="center"><img src="docs/images/as-above.svg" width="700" alt="An inverted triangle: the graph of states above, one state below, the same laws at every level"></p>

A metaphysics engine. It models what things are, how they are joined, and what
must hold when they are. Its one idea is the **state**: a small category whose
elements are objects and whose events are arrows. States form a **graph**; data
crosses between them through **functors**, and a state can be **embedded** in an
element of another, so an interface in one domain edits the world in another.
The engine holds all of it to its laws - at compile time where it can, on live
data where it must.

Physics and a renderer come with it: bodies, ropes, light, GL and ASCII. Each
is a state like any other, met through the graph. What you build on it - a
game, a desktop, a painting you walk into - is a world of states.

C++17, no dependencies. Two libraries: `stategine::stategine` (the core, the
domains, physics, the compiled laws) and `stategine::render` (GL and the ASCII
renderer); `sg/gl/Window.hpp` is a header over your own GLFW. The OpenGL example
fetches GLFW on demand.

Building on it, or changing it? [AGENTS.md](AGENTS.md) is the short version of
how: standalone states, joined only through interfaces the graph declares and
the laws check - and the engine watching that they are.

![The room, lit by one lamp, with the map on the far wall](docs/images/room.png)

## The idea, in four frames

A map hangs on the wall of a 3D room. It is not a texture: it is a **2D state
embedded in the room**, joined to it by a pair of functors. Open it and push the
yellow token three cells right - the crate it stands for slides across the real
floor, in the same frame:

| Walk up to it | Open it | Before | After |
| --- | --- | --- | --- |
| ![Approaching the map](docs/images/approach.png) | ![The map in use](docs/images/open.png) | ![The crate near the camera](docs/images/before.png) | ![The crate moved with its token](docs/images/after.png) |

The renderer knows nothing about this. It is one functor, `stamp`, running every
frame because the portal is `Live`:

```cpp
graph.add_lens("collapse", "stamp", "room", "wallmap", objects,
               /* room -> map */ swizzle_scaled({{x, x}, {y, z}}, world_to_cell),
               /* map -> room */ swizzle_scaled({{x, x}, {z, y}}, cell_to_world));
graph.embed("wall_map", "room", "wall_map", "wallmap", "collapse", "stamp",
            sg::EmbedSync::Live);
```

## Two rooms, and a map that moves the doorway

Neither room has a position. The only thing relating them is a **doorway**: a
portal element in each, the same doorway seen from either side. The renderer
roots the world at whichever room you stand in and composes the doorway to place
the other.

The second room's map has one token: the doorway itself. Moving it walks the
doorway around the hall - from inside the annex the hall swings round, from the
hall the annex moves. The engine stores neither reading.

| Through the doorway | Inside the annex | Door on the east wall | south | north |
| --- | --- | --- | --- | --- |
| ![The annex seen through the doorway](docs/images/doorway.png) | ![The annex](docs/images/annex.png) | ![East](docs/images/east.png) | ![South](docs/images/south.png) | ![North](docs/images/north.png) |

The door map is a plan of the hall on purpose: a doorway has twelve stations,
three to a side, and the border of a 5x5 board is a ring of exactly twelve cells.
An interface shaped unlike what it edits offers moves its subject cannot make.

Both maps are the same construction. The difference is the `subject` - where an
interface is *mounted* and what it *acts on* are separate questions:

```cpp
graph.embed("crate_map", "hall", "crate_map", "cratemap",   // mounted in the hall, edits the hall
            "crates_to_map", "map_to_crates", sg::EmbedSync::Live);
graph.embed("door_map", "annex", "door_map", "doormap",     // mounted in the annex, edits the hall
            "door_to_map", "map_to_door", sg::EmbedSync::Live, /*subject=*/"hall");
```

## Time

Time is a state too. A `sg::Temporal` holds timelines - an element each, with a
`time` and a `frame`, moved by its own `advance` arrow. A state that changes
with time says so with a drive, which gives it a line on the clock, and its
arrows do the changing:

```cpp
auto& clock = graph.add<sg::Temporal>("clock");
pond.loop("spread", "ripple", "tick", [](sg::State&, sg::Element& e, sg::Element*, const sg::Event& ev) {
    e.params.set("r", e.params.num("r") + 2.0 * ev.args.num("dt"));
});
sg::drive(graph, clock, "pond", "tick", /*additive=*/true);   // the drive "clock>pond"
```

Each frame the pond steps, its line on the clock advances by `dt` and the
pond's `tick` arrows run with `{dt, time, frame}`, sent by the clock. A line
keeps one state's time and moves only when that state steps, so `time` is
always the sum of the steps the state has taken: paused under a menu and
resumed, it finds no time missing and no jump. One clock can keep a whole
world's time, a line per state (`validate` refuses a line kept for two).
Within a frame, states step in the order the graph declares: the active state,
then its open embeddings' guests, in the order they were embedded - so which of
two driven states moves first is never left to chance. Time is a monoid of durations and a drive is its
action, so the laws hold it to one: `step(0)` is the identity, and a drive that
claims `additive` keeps `step(a) ; step(b) == step(a + b)` - which an exact
flow does and a compounding or Euler step does not. An additive drive is not
handed `frame` - a step count cannot keep that claim, so there is none to read:

```
drive @ drive clock>pond: pond.money.v was 100; pond [!tick(dt=0.5)@clock ; !tick(dt=0.5)@clock] leaves 225, pond [!tick(dt=1)@clock] leaves 200
```

A drive keeps time `WhileActive` (the default: the room you are in and what is
open in it), `Keeps::WhileShown` (while any embedding shows it open - a game on
a set), `Keeps::WhileFocused` (only while it has the input - a game on a
computer, which waits while another window is in front) or `Keeps::Always`: a record still turning, a door still swinging in
a room you stepped out of. The engine steps such a state once a frame if the
active state did not, with what is open in it, and carries its Live embeddings
back out - so nothing needs stepping by hand from the game loop.

`on_update` still runs. `engine.set_watch_hooks(true)` reports any state whose
hook - `on_update`, `on_event`, `on_render`, `on_enter`, `on_exit`, `on_pause`,
`on_resume` - writes its data instead of emitting an event for an arrow.

## Gluing

Rooms glued by doorways are one case of local pieces glued into a whole.
`sg/core/Sheaf.hpp` holds the general version and knows nothing about rooms:

* a **`Cover`** is a set of states with declared overlaps, each carrying its
  transition as a pair of functors;
* **descent** lets them glue: across an overlap and back is the identity
  (*separatedness*), and around every loop the composite is the identity
  (*cocycle*). A loop that does not close is a seam;
* **`sections(root)`** is the glued result, every piece joined to the root in
  one chosen chart, however far. It asks descent first: a cover with a seam
  glues to nothing (pass a `std::vector<std::string>*` to hear why). Descent
  is checked on every loop, not up to a length: a tree over the cover leaves
  one closing overlap per independent loop, and those are checked.

```cpp
for (const auto& seam : sg::descent_defects(atlas, graph)) std::cout << "seam: " << seam << "\n";
```

A doorway's transition is *derived* from its two portal elements each time it is
asked for, so it cannot drift from the geometry it describes.

Glued rooms are **adjacent, never overlapping**. A doorway's plane is where one
room ends and the next begins: each room's solids stay on its own side, and the
renderer draws each room only on its own side. A wall centred on the plane
would put half of itself in the neighbour, and each room's look would bleed
into the other's wall. `descent_defects` names it:

```
seam: hall.e_a reaches 0.150000 m past doorway doorway, into the room on the other side
```

Every image here is reproducible:
`./build/sg_room3d 50 out.ppm <room|approach|open|before|after|dim|doorway|annex|east|south|north|alert|crossing>`.
Shots advance fades by a fixed 1/60 s per frame, so a mid-fade frame is the same
on every run (`crossing` steps through the doorway at frame 30).

## The laws

An invalid structure is refused at the earliest point that can see it:

1. **Compile time** (`sg/core/Typed.hpp`). State and element names become C++
   tags, so composing arrows or functors whose ends do not meet, a lens whose
   halves do not mirror, or claiming non-parallel arrows equal does not build:
   ```
   error: static assertion failed: sg: g * f needs cod(f) == dom(g); these arrows do not meet
   ```
2. **Structure** (`graph.validate()`). Dangling arrows, unknown endpoints,
   unreachable states, functors whose image arrows do not line up, portals wired
   to the wrong state.
3. **Live data** (`sg/core/Laws.hpp`). `sg::verify(graph)` runs every law the
   graph owns (identity, associativity, composition, functoriality, the lens
   laws, seams, and any diagrams you pass) on the states' current contents,
   then puts everything back. Descent on a `Cover`, an `Adjunction`'s unit and
   counit, and `interface_defects` are checked apart, where they are declared.
   A broken law is a counterexample:
   ```
   functoriality @ functor post on restock: ledger.book.stock was <unset>;
       shop.shelf [restock ; post] leaves 42, shop.shelf [post ; order_wrong] leaves 40
   ```

Every data law says *these two paths, run on the same data, leave the same
result*:

| Law | The two paths |
| --- | --- |
| Identity | `id ; f` and `f ; id` against `f`, for arrows and functors |
| Associativity | `(f ; g) ; h` against `f ; (g ; h)` |
| Composition | a registered composite against the chain it was built from |
| Functoriality | `f` then `F` against `F` then `F(f)` - the functor carries the arrow's *action* |
| Put-get | write a view back, read it again: you see what you wrote |
| Put-put | writing the same view twice is writing it once |
| Settles | `(get ; put)` twice against once - a lossy view must still settle |
| Commutes | any two paths you declare equal |
| Descent | separatedness and cocycle on a `Cover` (`descent_defects`; not part of `verify`) |

"The same result" means every element's parameters and whether it is alive,
and every queued event - name, sender and arguments, in the order queued (the
queue is dispatched in that order). Two paths that queue `damage(5)` and
`damage(500)` do not agree, nor do `a, b` and `b, a`.

A report keeps apart what it found, what it could not check and where it
stopped looking: `ok()` is no counterexample; `holds()` is that and every
equation run (`unchecked` empty); `complete()` is no search cut short by a
`LawOptions` budget (`bounded` empty). `sg::enforce` throws unless `holds()`.

Functoriality here is the square `f ; F == F ; F(f)` for each arrow `F` maps:
what the functor does to data agrees with what the arrows do. It is not by
itself the textbook `F(id) = id`, `F(g . f) = F(g) . F(f)`; identities hold by
construction, and a composite is held to the square only where `F` maps it.

```cpp
sg::Diagram d("shelf work");
d.commutes(sg::Path("shop", "shelf").arrow("restock").arrow("halve"),
           sg::Path("shop", "shelf").arrow("halve").arrow("restock"));
sg::verify(graph, {d});   // shop.shelf.stock was 30; [restock ; halve] leaves 21, [halve ; restock] leaves 27

// typed, the same claim cannot be written with arrows that do not share both ends
sg::typed::commutes(d, refund * sell, sg::typed::id<Shelf>());   // Shelf -> Shelf, both sides
sg::typed::commutes(d, sell, refund);                            // does not compile
```

`sg::enforce(graph)` throws a `LawError` instead of reporting. Arrows that read
event arguments are probed through `LawOptions` - an integrator that does nothing
at `dt = 0` passes every law vacuously, so give it a `dt`. Only elements, state
parameters and queued events are undone after a check; side effects a handler
has elsewhere are not.

Turning the laws on found three core bugs every earlier check had passed: the
identity functor dropped objects created after it was built, a composite ending
in a loop was registered with the wrong type, and a rebuilt part could leave its
composite behind unnoticed. It also found the demo's 2D/3D "isomorphism" held
only on scratch data.

### The laws, compiled

Running every equation is slow on a big graph. Some can be answered without
running anything: every step on them says what it does. An arrow made with
`State::affine`, or a transport from `sg::transport::only` / `swizzle` /
`copy_all` / `affine`, carries its description (`sg/core/Declared.hpp`: each
parameter it sets is a sum of parameters it reads, times numbers - perhaps
times an event argument such as `dt` - plus a number). Its handler is made
from that description, so the two are one thing, not two truths. The
integrator of every `Spatial` body is such an arrow.

```
Equation {lhs, rhs} --try_compile--> representable? --yes--> algebra::Program --> a backend's batch --> the report
                                                    --no---> the verifier runs it, as before  ------------^
```

`sg::algebra` (`Operator.hpp`, `Program.hpp`, `Backend.hpp`, `Compile.hpp`)
compiles an equation into two programs over the live data. Two sides that
leave a parameter the very same expression hold it the same, and are not run
at all. What is left runs in a batch on a backend: the CPU (`CpuBackend`), or a
GPU (`sg/gpu/AlgebraBackend.hpp`: CUDA, ROCm, Vulkan, Metal, each built in where
its toolchain is found). An equation the batch finds apart, or too near the
tolerance to say, is run again by the verifier, and the verifier's
counterexample is what is reported. The laws stay the truth, and a step that
says nothing is run as ever.

```cpp
auto backend = sg::gpu::best();                 // a GPU if there is one here, the CPU if not
sg::algebra::Accelerated fast(*backend);        // compiled programs are kept, per graph
sg::LawOptions o;
o.accelerate = &fast;
sg::LawReport r = sg::verify(graph, {}, o);     // the same report, faster
```

`./build/sg_algebra` holds every backend to the verifier's report, lawful and
broken. At 400 bodies: plain 0.9 s, compiled 0.3 s the first time and 35 ms
after.

## Using stategine in a project

Pin a release; none of the engine's examples, tests or downloads come along.

```cmake
include(FetchContent)
FetchContent_Declare(stategine
  GIT_REPOSITORY https://github.com/Bardia323/stategine.git
  GIT_TAG        v0.2.0)
FetchContent_MakeAvailable(stategine)

target_link_libraries(my_game PRIVATE stategine::stategine
                      stategine::warnings          # optional: -Wall -Wextra / /W4
                      stategine::static_runtime)   # optional: self-contained MinGW exes
```

To change the engine and a project together, build against a local checkout,
uncommitted edits included: `-DFETCHCONTENT_SOURCE_DIR_STATEGINE=../stategine`.
[stategine-template](https://github.com/Bardia323/stategine-template) (private)
sets this up, with a ctest that runs every law on the game's world. What each
release breaks is in `CHANGELOG.md`.

## Writing a world

| Concept | In the engine | Category theory |
| --- | --- | --- |
| Element | `Element` in a state | object |
| Event morphism | `state.arrow(name, from, to, trigger, fn)` | arrow |
| Composite | `state.compose("gf", "f", "g", trigger)` | `g . f`, needs `cod(f) == dom(g)` |
| State | a `State` subclass | a small category |
| Transition | `graph.connect(from, trigger, to)` | arrow in the state graph |
| Data transport | `Functor` on a transition or portal | functor `A -> B`, partial: defined on the objects and arrows it maps |
| View + edit pair | `graph.add_lens(...)` | a functor pair |
| Adjoint pair | `Adjunction` | `F -| G` by unit, counit and triangle laws; isomorphism when both are identities |
| Extension along a functor | `sg::kan::left`, `sg::kan::right` | `Lan_K F`, `Ran_K F`: pointwise (co)limits searched among what `C` already has, compiled to an ordinary `Functor` |
| Nested interface | `graph.embed(...)` | a state inside an object of another |
| Cover, gluing | `Cover`, `Atlas`, `sections(root)` | descent |

### A state

```cpp
class Battle : public sg::State {
public:
    Battle() : sg::State("battle") {
        add_element("hero", "unit").params.set("hp", int64_t{20});
        add_element("slime", "unit").params.set("hp", int64_t{8});

        arrow("strike", "hero", "slime", "attack",          // hero --attack--> slime
              [](sg::State& s, sg::Element&, sg::Element* slime, const sg::Event& ev) {
                  const int64_t hp = slime->params.get_or<int64_t>("hp", 0) -
                                     ev.args.get_or<int64_t>("dmg", 1);
                  slime->params.set("hp", hp);
                  if (hp <= 0) s.emit("victory");   // said outward: the graph takes it from here
              });
        says("victory");   // declared: what the battle tells the world
    }
};
```

### A graph

```cpp
sg::StateGraph graph;
auto& battle = graph.add<Battle>();
graph.add<sg::ConsoleState>("menu");
graph.connect("battle", "victory", "menu");   // switch
graph.push("battle", "pause", "menu");        // stack on top
graph.pop("menu", "back");                    // and back off
graph.set_initial("battle");

sg::Engine engine(graph);
engine.start();
engine.fire(sg::Event{"attack", sg::Params{}.set("dmg", int64_t{8})});
engine.run(60.0);
```

A state's events stay in the state, except those it `says`: the engine hands
those to the graph's transitions (and to nothing else - one no transition takes
is dropped). Nothing in between: no listener, no `engine.fire` from inside an
arrow. A state sees its engine const, so it cannot move the stack itself; what
it said is part of what an arrow did, and a law's trial keeps it to itself.
**Subscribers may observe the world; only the world may change it.** A
listener (`bus().subscribe`) reads the event and does what is outside the
world - draws, prints, plays a sound, logs. While it runs, firing the engine,
moving the stack or an embedding, sending a state an event and rewriting the
graph are refused with `sg::ObserverError` (`sg::set_observers(Observers::Report)`
says each once and lets it through, for a project moving its listeners into the
graph). No listener runs during a law's trial. An observer that must change the
world is part of it: a state, whose causes are arrows, functors, transitions.

What a state says may also be an **edit**: a request that rewrites the graph -
a room added, a doorway glued. The graph declares it, and the engine applies it
at the start of the next frame, before anything else moves; the answer comes
back to the state as `<event>.done`, for its own arrows to show:

```cpp
editor.says("cmd");
graph.edit("editor", "cmd", [&](sg::StateGraph& g, const sg::Event& asked) {
    g.add<sg::State>(sg::Key{asked.args.get_or<std::string>("name", "")});
    return sg::Params{}.set("text", std::string("made"));
});
```

A functor maps events as well as objects and arrows, so what a state says also
crosses each functor out of it that names the event (`Functor::on_event`), to
the state it goes to, whose arrows run on it at once - relabelled, as a
transition's functor carries its event. An embedding's functors carry only
while it is open (its `in` from the host too); a transition's only when taken:

```cpp
graph.add_functor("desk.to.board", "desk", "board").on_event("chalk", "write");
graph.embed("board", "desk", "board_portal", "board", "desk.to.board", {}, sg::EmbedSync::Commit);
desk.says("chalk");   // an arrow of the desk emits chalk; the board's arrows on write run
```

Transitions take an optional `guard`, an `action` (fills the `Params` handed to
`on_enter`) and a `functor`. `"*"` as the source matches any state. A
transition's name is its identity: a name given twice is refused, and a made-up
one (`from-trigger->to`) gets `#2`, `#3` for alternatives on the same event; an
arrow's name is unique in its state. In a law, `Path::transition(name)` is the
transition the engine would take - on its trigger, only if the engine would
choose it there (guard and precedence), running its action, carrying the event,
entering where it goes; a pop returns to where the path last pushed from.

### Functors

Transports talk about parameter names, not 2D or 3D, so "the map's y is the
world's z" is one call. Built in: `copy_all`, `only({...})`,
`swizzle({{dst, src}, ...})`, `swizzle_scaled(pairs, fn)`, `then(a, b)`.

```cpp
sg::Functor& lift = graph.add_functor("lift", "world2d", "world3d");
lift.on_object("player", "player", sg::transport::copy_all)
    .on_morphism("move.player", "move.player")
    .on_event(flat.step_event(), deep.step_event());
graph.connect("world2d", "toggle", "world3d", "lift");   // a switch that carries by lift
graph.compose_functors("roundtrip", {"lift", "flatten"});   // g * f, checked at the seam
```

`Adjunction` is `F -| G` witnessed properly: declare a unit arrow
`a -> G(F(a))` and a counit arrow `F(G(b)) -> b` per object (`unit`,
`counit`, with `identity` marking no-op loops that stand for identities), and
`check` verifies both naturality squares and both triangle identities, on the
arrows. `laws::adjunction(graph, adj)` runs the same equations on live data.
The round trip need not come home - the order `0 < 1` collapsed onto a point
is adjoint to picking out `1`, with unit `0 -> 1` - so an adjunction is not an
isomorphism. What a round trip loses is still reported (`unit_defects`,
`counit_defects`, `data_defects`, `is_isomorphism`): those measure whether the
pair is an isomorphism, not whether it is adjoint.

A **Kan extension** is a functor found, not a thing added. `sg::kan` is a
compiler: given `K : A -> B` (a known piece `A` of a world `B`) and
`F : A -> C`, it works out what `F` must do on all of `B` that the piece
forces, and hands back an ordinary `Functor`:

```cpp
sg::kan::Result r = sg::kan::left(graph, "K", "F", "extended");   // or kan::right
if (r.ok()) graph.add_functor(std::move(*r.functor));             // nothing can tell where it came from
else std::puts(r.str().c_str());                                  // defects, holes
```

At each object `b` it searches `C`'s own elements and arrows for the colimit
of `F(a)` over every `K(a) -> b` (for `right`, the limit over every
`b -> K(a)`) - `Lan_K F (b) = colim (K | b)`, `Ran_K F (b) = lim (b | K)` -
an element every other cocone passes through by exactly one arrow. Nothing is
invented: no colimit object, no new arrow, no guessed code. A result is one
of three, and says which:

- **it exists**: `functor`, an ordinary `Functor`;
- **it cannot exist**: `defects` - `C` has no (co)limit at some `b`, and the
  search there looked at everything; or `K` or `F` is not a functor;
- **not derivable (yet)**: `holes`, each a `Hole` with its `kind` and where
  it is - `Arrow` (the universal property sends an arrow to one `C` does not
  name: a composite or an identity loop to add), `Transport` (a transport
  not provably invertible, or one reading what `K` drops), `Budget` (a
  search stopped at `Options::max_path` / `max_cones`), `Unsupported` (what
  cannot be made executable: an opaque transport, `K` not an inclusion).

It keeps to: a functor only with `complete` and no hole or defect; a defect
only with `complete`; with `complete == false`, neither existence nor
nonexistence. `complete` is about search alone - an `Unsupported` hole may
stand with it true.

A transport is `F . K^-1` from declared stages only. At `b = K(a)`, the leg
there the identity, `K`'s transport at `a` is undone where its stages prove
it invertible - a whole copy (the identity), or a one to one renaming with
`F` reading only what it carries - and then `F`'s runs. A rename with a
collision, a parameter dropped that `F` reads, arithmetic, a copy with
renames on top: `Transport` holes. `F` the identity gives the identity
transport only where `K` carries `a` whole - the same representation. Or a
transport is supplied (`kan::Options::supply(b, transport)`). Never a copy
of everything, never guessed code. An edit adds what is missing, and the
compiler runs again.

A state is read as the category its arrows generate: a composite is the word
of its parts, a loop that does nothing is an identity. For now `K` is an
inclusion, and the extension covers what `K`'s image reaches (for `right`,
what reaches it). Hom-sets and cones are searched within `Options` budgets,
and a search cut short says so (`Hole::Budget`, `complete`).

The cost is paid once. The result is materialised; `r.current(graph)` says
whether the structure it read - the three states, the two functors' maps - is
as it was. Data moving under the same structure leaves it current; compile
again only when it is not.

### Embeddings

A transition replaces the active state; an embedding nests one inside an element
(the portal) of a running host.

* `EmbedSync::Live` - `out` runs every frame: the map moves the crate at once.
* `EmbedSync::Commit` - `out` runs on close; `close_embed(name, false)` cancels.
* `EmbedSync::View` - `in` runs every frame, nothing comes back.

"Every frame" is as the embedding's `Propagation` says (`graph.set_propagation`):

* `OnChange` (the default) - only the objects whose source or target changed
  since the direction last ran, found by their stamps; with nothing changed it
  costs a comparison. The same result as running every frame, for a transport
  that is a function of the two elements' params.
* `Continuous` - every object, every frame: for a transport that reads anything
  else (the time, another element).
* `OnEvent` - only in a frame in which an event crossed into the guest through it.
* `Manual` - only when `engine.sync_embed(name)` says so.

An embedding is declared, then read: `embed` hands back a const view, and what
it joins changes only through the graph (`set_sync`, `set_propagation`,
`drop_embedding`), each counted. `set_focus(name, on)` says whether it takes
input when opened. The subject need not be the host - a panel hanging in one
room can act on another.

Open and close with `engine.open_embed(name)` / `close_embed(name)`, or fire
`embed.open` / `embed.close` with a `name`. A focused portal receives the
engine's events; portals nest.

## Rendering

Domain states never know about pixels, and renderers never know about a game:
anything speaking the shared vocabulary (`x/y/z`, `sx/sy/sz`, `r/g/b`,
`w/h/yaw`, in `sg::keys`) can be drawn. So one `Spatial3D` can be a lit room, a
terminal sketch and a texture on a wall at once.

`sg::render::GLWorldView` draws any `Spatial3D`: portal passes into other
states, shadow maps for the two nearest lamps, up to four spot lights with PCF
shadows, procedural materials and fog, then bloom, ACES tonemapping and FXAA.
Walls are data - a state with `wall` elements gets them drawn, one without gets a
box. Knobs are in `sg::render::GLQuality`; elements set their own look through
parameters (`r/g/b`, `roughness`, `intensity`, ...).

![Standing in the annex, looking into the hall: both rooms lit and shadowed by their own lamps](docs/images/east.png)

A camera is a state of its own (`sg::Camera`): a lens, aimed by its arrows.
It sees a world when the graph says so - `sg::film(graph, camera, world, rig)`
embeds the world in its lens, the rig's pose carried onto it - and a screen
shows what it sees by naming that embedding (`shows`). Pointed at its own
screen, it shows the room, the screen in it, and so on down: each frame's
picture holds the frame before.

### Looks

How a room is shown is a state too. A `LookState` has one element per pass
(`shadow`, `scene`, `bright`, `blur`, `composite`); a parameter starting with `u`
is a uniform of that pass, and `fs`/`vs` replace its shaders. A look only states
how it differs from the standard one. Rooms *wear* looks through an embedding,
so looks are ordinary, reachable, validated states:

```cpp
auto& alert = graph.add<sg::LookState>("hall.alert");
alert.uniform(sg::passes::composite, "uTint", 1.3, 0.62, 0.55)
     .uniform(sg::passes::scene, "uFogDensity", 0.035)
     .fade(0.35);                                  // seconds to fade in
cool.shader(sg::passes::composite, my_composite);  // a pass with its own shader

sg::wear(graph, "hall", "hall.calm");              // the first worn is active
sg::wear(graph, "hall", "hall.alert");
sg::set_look(hall, "hall.alert");                  // a parameter write; the renderer fades
```

Each room is drawn in its own look, so the annex keeps its fog when seen from
the hall. The post passes follow the room the viewer stands in.

What is on screen is a *blend* of looks, as weights. A change of look moves
weight towards the look now wanted, at the rate its `fade` sets, and never
jumps. If the change is undone half way, the blend walks back along the same
path to where it started. A third look reached mid-fade starts from the blend
on screen, not from either end. Numbers are weighted averages. The composite
pass runs every program in the blend and averages them, so a change of shader
dissolves too. The other passes use the shader of the heaviest look.

`view.prepare(graph)` compiles every look reachable in the graph before the
first frame. Looks that only change numbers share the built-in programs. It
also reports, as counterexamples, shaders that do not build (the built-in is
used instead), shaders missing a uniform the renderer sets, and looks that set
a uniform their shader does not have. `view.stats().late` counts anything
compiled mid-game because `prepare` never saw it.

| Calm | Alert (`L`) |
| --- | --- |
| ![The hall in its calm look](docs/images/room.png) | ![The same view in the alert look](docs/images/alert.png) |

## The notation

What this page has written in C++ - a state and its arrows, a functor, an
embedding, a transition, a seam, a drive - has a textual form, and the form is
only that: another syntax for the same model. A program is a *presentation* of
a graph; `sgc` reads it, holds it to the ontology, and writes the C++ that
builds the same construction through the engine's own API. There is no
interpreter, no script and no second runtime; the engine that runs the result
does not know where its declarations came from.

```
state time : temporal
initial void

state void : spatial3d {
    element terminal : portal { x = 0.0  y = 1.0  z = -2.0  w = 1.6  h = 1.0  open = true }
    element gate     : portal { x = 3.0  yaw = 1.5707963267948966  w = 1.0  h = 2.0 }
}
state terminal { element screen : portal { w = 1.6  h = 1.0  open = true } }

state exit {
    element idle
    element playing
    element finished
    element runner : sprite { x = 0.0  vx = 2.0 }

    idle -> playing : start on exit.start
    playing -> finished : win on exit.win native exit_won      # the inside of the arrow is C++
    runner -> runner : run(dt) on exit.tick { x = x + vx * dt } # or it says what it does (an Affine)

    say exit.out
}

embed void.terminal -> terminal  sync commit  focus true  follows true
embed terminal.screen -> exit    sync commit  focus true  follows true
drive time -> exit.tick keeps focused

seam void.gate <-> room.gate  name gate        # a shared boundary ...
transition void -[exit.out]-> room carry gate.ab   # ... and one way through it

state void.look : look { fade = 0.4  element scene : pass { uAmbient = 0.2  uSky = [0.05, 0.06, 0.08] } }
wear void <- void.look
```

Every construct is one engine primitive: `state` a `State` (of the kind's own
class: `state`, `temporal`, `spatial2d`, `spatial3d`, `look`, `camera`,
`console`), `element` and `key = value` its data, `a -> b : name` an arrow
(`loop` when `a` is `b`), `compose` composition, `functor` with `object` and
`event` maps, `compose f = g ; h` and `lens` for functors, `transition`,
`embed`, `seam` (`sg::glue_doorway`), `drive`, `port`, `keep`, `edit`,
`initial`. Sugar is only sugar: `wear` is `sg::wear`, `film` is `sg::film`,
`when e1 e2` an event mapping of a functor, `bind` a table an input adapter
turns into `Engine::fire`. `extern state` names a state built in C++, so the
two can be ported one declaration at a time. A seam and a transition are
different claims - a shared boundary, a directed change of active state - and
neither makes the other.

What the ontology forbids has no syntax, and the compiler says why: a private
timer (`elapsed`), a write from one state into another, IO in a transport, a
callback, `on_update`. C++ is for devices and for *native* computations - the
inside of an arrow, a transport or an edit that is already declared, named in
the notation and handed only its own elements (`sg::dsl::Natives`): trusted
extensions with restricted interfaces, not a sandbox.

```cmake
stategine_compile_dsl(game NAME world SOURCES src/world.sg)   # -> sgen::build_world(graph, natives, bindings)
```

The same plan can be made on a running graph: a source held by an ordinary
state, a compiler that is a state with native arrows, and the change asked of
the graph by `graph.edit` - the world rewriting itself by the one lawful way
(`sg/dsl/Compiler.hpp`, `src/dsl/compiler.sg`). `sg::dsl::facts(graph)` says
what a graph is made of in canonical lines, so what a DSL program declares can
be held beside the C++ it replaces before that C++ goes.

## A game's modules

A game built on the engine is built the way the engine is: each state (or a
few that belong together) a module, each module a library - its headers say
what a thing is, its `.cpp` files what it does - compiled on its own and all
at once, with the linker the one pass that brings them together. A change
recompiles only the files that changed.

```cmake
FetchContent_MakeAvailable(stategine)
stategine_module(sheets)                          # src/sheets/*.cpp
stategine_module(desert USES sheets)              # may include sheets/ - nothing else of yours
stategine_module(room   USES sheets desert EXCLUDE main)
add_executable(game src/room/main.cpp)
target_link_libraries(game PRIVATE room)
stategine_check_modules()                         # one test: modules their own, headers in shape
```

`stategine_check_modules()` fails on a module that includes one it does not
`USES` (never what uses it), and on a body in a header that is longer than a
line and neither a template, constexpr, nor said why (`// inline: ...`).

## Performance

Names are interned once, so hot paths compare pointers; morphisms are bucketed
by trigger; `Params` is a flat vector; queues reuse their buffers; surfaces and
portal textures redraw only when something moved. `./build/sg_bench` here
(512 bodies, -O2):

```
element lookup                      96,000 k/s
frames (512 integrator arrows)          40 k/s      ~20M arrow applications/s
functor apply (512 objects)         21,000 k/s
frames with a live portal               16 k/s      (9 k/s carrying everything every frame)
```

**Pay when the structure changes, not while it stands.** Everything the engine
works out and keeps is derived from the one graph, holds nothing of its own,
and is thrown away and found again when what it was found on changes:

* *Stamps.* Every change of an element's params gets the next number of one
  count (`Params::stamp()`; setting a value it already holds is no change). A
  stamp names content, so a copy - a snapshot, a restored default - carries it.
  `State::structure()` is stamped when elements or arrows come or go;
  `State::content_version()` folds a state's stamps into one number.
* *Portals.* An embedding's route (states, functors) is resolved once per
  change of the graph's `topology()`; its functor keeps a `Functor::Memo` of
  which element went where and what each held, and carries only what changed.
* *Validation.* `validate()` checks a state's arrows, a functor's endpoints and
  reachability again only when their structure moved; `validate(false)` checks
  everything, to compare.
* *Laws.* A `LawCache` keeps each equation's answer with the versions of what
  its paths read (`verify(graph, cache)`), reuses a side of an equation that is
  unchanged when the other is not, and checks directly an equation that is
  cheaper to run than to keep or never comes out the same twice. Without a
  cache, `verify` is still faster: a side's end state is taken, not copied,
  and sides that left the same stamps agree without a value compared.

`./build/sg_bench_scale` measures all of it at 1K..1M objects against the
plain way (`--quick`, `--big`, or case numbers). Some of what it shows: a Live
portal of 100K objects costs 0.5 ms a frame idle and 1.6 ms with 1% changing,
against 72 ms carrying everything; with every object changing, still less
than half. A graph verified again unchanged costs about 45% of a fresh check.

## Who may change what

Whoever holds the `StateGraph` - the code that builds the world, and whatever
rewrites it as it runs - may change anything in it. Whoever holds it `const`
may look at everything and change nothing: a const graph gives const states,
elements, functors. The engine shows the world const (`current()`,
`focused()`, `graph()`): a caller acts on it by firing events. A state changes
its own data in its update and its arrows (handed the state); functors carry
data into the state they land in; an embedding acts on its declared subject.

What the graph is made of - states, their elements and arrows, functors and
their maps, embeddings, seams, transitions - is counted by `revision()` (the
interfaces alone by `topology()`), one add per change and nothing else;
changing a value never counts. Embeddings, transitions, seams and arrows are
declared and then read: nothing rewrites one in place behind the graph's back
(`set_sync`, `set_propagation`, `set_carry`, `drop_embedding`, `set_functor`
do it, counted). The engine checks the graph again at its next frame after the
count moved; unchanged, it is not checked.

An element's identity is fixed when it is made: `id` and `kind` read like any
`Key`, and nothing but the element sets them. A mutable state hands out its
elements (`elements()`, an `ElementRange`) to act on, never the list itself.

A law's trial runs arrows on the live graph, then puts their data back. It
does not try to put back structure: while a trial runs, nothing structural is
let happen at all - no functor, embedding or seam, and no element or arrow
added to or taken from any state. A path that would do it throws
`RewriteRefused` before anything changes, and its equation is reported as
unchecked (`LawReport::unchecked`), apart from the counterexamples.

## Layout

```
include/sg/
  core/      the engine, domain-agnostic
    Core.hpp State.hpp Functor.hpp Adjunction.hpp Kan.hpp Embedding.hpp
    StateGraph.hpp Engine.hpp Sheaf.hpp (covers, descent)
    Laws.hpp (laws on live data)  Typed.hpp (compile-time typed handles)
    Declared.hpp (what a step does, said: affine arrows and transports)
  algebra/   the laws compiled: Operator, Program, Backend (CPU), Compile
  gpu/       the algebra's batches on a GPU: AlgebraBackend.hpp (src/gpu: CUDA, ROCm, Vulkan, Metal)
  domains/   what a state is about: Spatial, Atlas, Console, Surface, Look
  physics/   solvers a state can step in its arrows, plain data in and out:
    Rigid.hpp (sg::rigid: bodies that fall, stack, tip, roll, sleep, are held)
    Rope.hpp  (sg::rope: cords that hang, lie over edges, never pass through)
  render/    how a state is shown: Ascii, GLWorld
  gl/        the GL backend
  sg.hpp     umbrella for core + domains (renderers are opt-in)
  Version.hpp
```

## Build and run

```sh
cmake -S . -B build -G "MinGW Makefiles"   # or any generator; -DSG_BUILD_GL=OFF skips GLFW
cmake --build build -j
ctest --test-dir build                     # unit tests, laws, must-not-compile cases
./build/sg_room3d
```

| Target | What it is |
| --- | --- |
| `sg_room3d` | **the real one**: one lit OpenGL space, two rooms, two maps |
| `sg_room` | the same room and lens, in the terminal |
| `sg_demo` | console/2D/3D states, an isomorphic pair, transitions, a portal, the laws - headless |
| `sg_tests` | 178 assertions over the core, functors, portals, rooms and looks |
| `sg_laws` | every data law holding, then broken on purpose and read back |
| `sg_looks_gl` | looks on a real GL context: broken shaders named, fallbacks, late compiles counted (needs a display) |
| `compile_fail_*` | pass only if an ill-typed composition is refused with stategine's own message |
| `sg_core_only` | the core with no domain or renderer - the layering, as a build failure |
| `sg_bench` | hot-path throughput |

`sg_room3d` controls: `WASD` walk (moves the token in map mode), mouse look,
`E` use a map, `Tab` next token, `C` cancel, `Q`/`R` slide the lamp, `F` dim,
`L` switch the hall's look, `Esc` release mouse / quit. `./build/sg_room3d 120 frame.ppm` renders 120 frames
to a PPM and exits.
