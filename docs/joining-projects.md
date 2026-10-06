# Joining two projects

Two programs built on stategine - a game and a sandbox, two games, a game
and a tool - are each states and how they meet. Joining them is bringing one
project's states into the other's graph and declaring where they meet: a
seam, a transition, an embedding. Nothing new is invented for it; what goes
wrong is always one of the things below. Go through them in order, before
building, and the join takes an hour rather than an afternoon.

The host is the program that keeps running (its loop, its window, its start);
the guest is the project brought in.

## 1. One graph: bring states, not a graph

An engine has one graph. Seams, transitions, embeddings, functors and drives
exist only inside it; there is no bridge between two graphs. So the guest's
graph is not kept and joined - its states are made in the host's graph, and
its declarations declared there.

- **Modules:** the guest's modules (`stategine_module`) are built by the host
  from the guest's sources - copied in, or by `ROOT` from beside it - with
  their `USES` unchanged. A module of the guest's that uses the host's
  modules (or the other way round) makes the two depend on each other: copy
  rather than reach across, and say which copy a change belongs to.
- **Construction:** the guest's world-building function (which states, with
  which names, in what order) becomes a function of the host's, called where
  what it joins is made. Make the states as the guest made them.
- **Notation:** the guest's `.sg` becomes one of the host's, compiled and
  declared after the states it names exist. In it:
  - its clock is the host's: `extern state time : temporal` (one line of time
    - a second clock is a second timeline, and the engine forbids it);
  - no `initial` - the host says where it starts;
  - its natives (`native foo`) registered with the host's `Natives`.
- **Its program stays behind:** the guest's `main` (window, sound, game loop,
  compositor) is not a state and does not come. What of it the host needs is
  in section 6.

## 2. One namespace

A graph's names are all one namespace: state ids, element ids, the names of
embeddings, transitions, functors, seams, drives' triggers. Two projects
written apart reuse the obvious ones (`tv`, `away`, `door`, `play`, `hall`).
A clash is refused at construction (`duplicate embedding tv`) - but only the
first, one at a time. List the guest's names and look for each in the host
first:

```sh
grep -ohE "^\s+name [a-z0-9._]+|^functor [a-z0-9._]+|^(extern )?state [a-z0-9._]+" guest/*.sg | sort -u
```

Rename on the guest's side, with a prefix (`hotel.tv`). Event names shared by
kind (`game.pad` in two games) are fine: events are routed per state.

## 3. Globals belong to the host

A few things in the engine are set once per program, not per state - and a
guest written as its own program set them for itself:

| global | set by |
| --- | --- |
| the modeller's file reader | `sg::Modeler::set_files` |
| the texture's picture reader | `sg::Texture::set_reader` |
| the disk cache's folder | `sg::cache::set_folder` |

In the joined program only the host sets them, once, before anything is built
that reads through them. A guest module must not set them (two writers of
one global: whichever ran last wins, silently): give it a way to leave them
to its host (`open(assets, /*reader=*/false)`), and have it name its files by
whole path, so the host's reader finds them. Its assets go in a folder of
their own under the host's (`<assets>/<guest>/`).

## 4. Caches

Derived data is kept on disk by a digest of what made it (`sg::cache`). The
engine keeps only what was made whole: a model that failed - an `import`
asked for before the reader was set - is made again next time, not read back
failed. A cache is never a source of truth: if a fixed thing still comes out
wrong, delete its entries (or the folder) and it is made again.

## 5. The join is a seam, and the seam is held to the laws

Where the two meet is an overlap like any other, and the overlap law
(`laws::overlaps`) holds both sides of it to one account:

- **Placement:** a portal is placed by its middle, not its foot - a doorway
  `h` high stands at `h / 2`, or its foot is underground (`the ground is
  -1.2 m below the opening's foot`).
- **The eye:** both sides carry the eye as high (`height`, 1.65 m unless a
  walker says). A guest written with another height matches the host's, or
  the seam says `differs eye` - knowingly.
- **The floor:** the ground at the threshold at one height on both sides.
- **Reachability:** every state stays reachable from the start
  (`graph.validate()`): if the join takes the place of something the host
  had (a door on a wall that has no room for two), give that thing another
  way in - stand its doorway in the guest's place - rather than orphan it.
- **Every seam both ways:** a host that walks its seams in a test
  (`sg::render::check_crossings`) walks the guest's own seams too - which the
  guest may never have walked both ways. What it finds is a bug of the
  guest's: fix it there (a door shut and solid on one side only).

## 6. What the guest's loop did, the host's loop must be told

The guest's program turned keys into its states' events, bound its surfaces
to the renderer, and drew its prompts. None of that is a state; none of it
comes. The host's loop does it, by the graph's own means:

- **Surfaces:** `bind_surface(portal, state)` for each 2D state the guest
  shows on a thing. (Textures embedded in things bind themselves.)
- **Input:** fire the guest's events - its places' own (`use`, `esc`, a
  step) - never write its params. A guest that takes focus gets its keys
  while `engine.focused()` is it, and gives way back by its own event. Keys
  the host also uses (Esc releasing the mouse) yield while the guest has
  them.
- **Prompts:** in the host's way (a highlight on what can be used, a line of
  text) - and cleared when nothing is aimed at or the guest is left.
- **What only works in the guest's own program** (a compositor, a sound
  engine, a 2D game's own loop) stays there: show what can be shown, and take
  out the ways in that would strand whoever takes them, rather than leave
  them dangling.

A guest that wants to be joinable says what it is used by in its states
(events for use and leaving, what is aimed at) rather than only in its loop.

## 7. Presence: who is in a place

A program that has one place current at a time can leave its walker's body
standing in a place it has left - and seen from another place (through a
doorway, on a screen), that is a stranger standing there, or their shadow in
the lit air. Hide the body when its walker leaves (`on_exit`, which an
embedding closing also calls - they are not there in either case) and show it
again from its own step arrow, which runs only where it walks - never from
`on_enter`, which an embedding opening also calls.

## 8. Check it as the host checks itself

- Start the host and read every line it prints beginning `!` or `[sg]`:
  unreachable states, seams that disagree, looks that did not build.
- `sg::verify(graph)` / the host's law check: nothing unreachable, every law.
- The host's crossings test: every seam, both ways, with no jump - dump the
  frames of one that fails and look at the frame before and after.
- The host's facts golden (`sg::dsl::facts`): rewrite it only for the change
  that was meant, and read the difference.
- The guest's own tests, in its own program, still pass - its copy and the
  host's are the same, or the difference is said.

## A worked example

The Hotel Flamingo (a game) joined to stategine-lab's dev room (a sandbox):
its four modules copied in, its world made by the lab's
`World::hang_hotel`, its `world.sg` declared as `hotel.sg` on the dev room's
clock; one seam from the dev room's wall into its void, the church's door
(which had stood there) moved into the void. Every item above was met on the
way: a clashing `tv` embedding, a kit that set the modeller's reader, a
failed import cached on disk, a doorway placed by its foot, a 1.62 m eye
against 1.65 m, its hotel doors shut and solid on one side, its walker's
shadow standing in the void. The lab's `docs/porting-a-game.md` gives the
lab's own particulars.
