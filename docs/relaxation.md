# A relation at once, its contents in time

An experiment: can the engine say, with what it already is, that a relation
between states holds *at once* while the states' contents come to agree with
it *in time*? It can. This page says how, what it measured, and where the
existing ontology was enough and where a reading of it had to be written.

The graph is in `tests/dsl/relax.sg` (the local half), `relax_product.sg` and
`relax_bond.sg` (two global states); the test is `tests/test_relax.cpp`
(`sg_relax`); the readings are `include/sg/core/Relax.hpp`.

## The idea, in the engine's words

- What can be observed of a state (A) is its elements, their params and its
  arrows: what a functor can read and the laws re-run.
- A valuation (omega) is the state's data now, as functors read it.
- A functor between states is the relation, and it is instantaneous: whatever
  g is, the restriction `ra : g -> a` says at once what a should hold. It is
  structure - it says what correspondence must hold - and it never makes the
  target exist at once.
- Its answer y* = ra(omega_g) is the target. Inside the target state, its own
  arrow, driven on the clock, runs y <- D(y, y*) - and only that arrow writes
  y. The relation defines coherence; Temporal realises it.

## How it is built

```
            ra (only x, y)                aim_a = ra ; aim_slot_a   (kept)
      g  ───────────────►  a          g ─────────────────────────► a.aim_x, a.aim_y
         ◄───────────────
            ia (lens)                  time ── a.relax(dt) ──► a.x, a.y   (a's own arrow)
```

- `ra`, `rb` are the restrictions; `ia`, `ib` their way back, declared lenses
  (`lens ra <-> ia`), so `sg::verify` holds them to put-get, put-put and
  settles. g, a and b with these overlaps are a `Cover`.
- The target crosses by a declared functor and lands only in the target's
  aim: `aim_a` is the composite of the restriction and an endofunctor of a
  that puts x, y into aim_x, aim_y (`swizzle`). It is kept (`keep aim_a`), so
  the engine carries it at the start of every frame. The relation is never
  applied to a's own x, y.
- a relaxes by its own arrow `a.relax(dt)`, driven on `time`, declared
  `additive`:

      y - y*  <-  e^(-rate dt) R(spin dt) (y - y*)

  rate and spin are a's own params. No time is no change; two steps are one
  step as long as both - the laws check both (`laws::drives`).
- g is perturbed by a declared port (`g.kick`). Nothing writes a state but its
  own arrow; nothing moves between states but a functor.

Everything here is in the notation except the insides of the arrows (natives:
the step D, the kicks, g's own settling), as the DSL rules ask.

## What it measured

Each run is at 4 steps a second (dt = 0.25); tolerance 1e-6.

**The laws.** Both graphs validate, `sg::verify` holds with every equation
checked (lenses, the composite functors, the kept carries, the drives'
identity and additivity), and the engine's strict watch names nothing.

**Factorisation.** Is omega_g = omega_a (x) omega_b - is g the glued section of
a and b? Descent (`Cover::descent_defects`) cannot tell: it holds for both
graphs, because it asks the pieces to agree *where they meet*, and both
globals agree with their pieces. What tells them apart is the converse - that
g is *no more than* its pieces: everything g holds is carried to some piece
and back as it was. That is the sheaf's locality, and `locality_defects`
reads it from the cover's own transitions:

| g | descent | locality |
| --- | --- | --- |
| product (ga, gb) | holds | holds: g is the glued section |
| bond (ga, gb, bond) | holds | `g: bond.total is held by no piece; g: bond.share is held by no piece` |

And by example: moving `bond.total` (`g.retotal`) changes g while both
restrictions, and so both gaps, stay exactly 0 - two different globals with
the same local sections. That is a correlation of g's own.

**The perturbation.** A kick of 1 moves g. In the frame it lands, a's gap
read through `ra` is the whole kick (1.000000) and a has not moved: the
relation's answer is there at once, a's data untouched. At the start of the
next frame the kept functor has carried the aim (aim_x = 2), before a takes
any step toward anything else - zero steps of lag - and that frame a takes
its first step (x = 1.3935 of 2).

**The three outcomes of D**, each a variant of a's rate and spin, classified
from the gaps alone (`sg::orbit`):

| rate, spin (rad/s) | outcome | gap, first -> last |
| --- | --- | --- |
| 2, 1 | converges, monotone (spirals in) | 0.607 -> 0 |
| 0, 2 pi | periodic, period 4: D^4(y) = y, y != y* | 1 -> 1 |
| 0, 1 | persists: bounded, never back | 1 -> 1 |
| -0.5, 0 | grows | 1.13 -> 2981 |
| -0.2, 1 | grows (spirals out) | 1.05 -> 24.5 |

Periodicity is read on the gap *vector*, not its size: a turn and a gap that
does not move have the same size every step, and only the vector says which.

**Time.** Three frames of no time leave a exactly as it was, to the bit.
Restored from its own data (`to_text` / `from_text` of g, a, b and the clock),
the world runs the same 30 steps again, to the bit.

**The correlation, local against global.** In the bond world (a at rate 3,
b at 0.5), g's correlated kick moves ga.x by +1 and gb.x by -1, keeping
ga.x + gb.x = total. Each side's own gap closes monotonically. But the
correlation, read on the sides (a and b carried back by their sections,
glued, against g's total), is broken on the way - a arrives long before b -
rising to 0.58 and coming back to 0 only once both have arrived: converges,
not monotone. Local propagation keeps each local relation and breaks the
global one in between.

Moving the correlation itself (`retotal` 0.5) is out of reach of both sides:
neither restriction sees it, their gaps stay under 1e-6 throughout, and the
correlation stays off by 0.5 for ever - persists, period 1 (a fixed point that
is not the target). It is reached only when g's own arrow is on: a declared
event (`g.couple`) gives g's driven `settle` a rate, g moves its own terms
toward its total, the restrictions carry that to the sides at once, and the
sides follow. The correlation converges, and both gaps go to 0.

**It can fail.** Registered as `relax`, a step that writes the aim over the
contents whatever the time (`snap`) is refused by `sg::verify`: the drive law
finds that no time changed a (x from 0 to 1 at dt = 0). Overwriting is not
relaxing, and the laws know the difference. The bond world fails locality,
as shown above, where the product passes.

## Did the abstraction hold without new ontology?

Yes. Every part is something the engine already is:

| The idea | In the engine |
| --- | --- |
| a relation between states | a functor (restriction), a lens with its way back |
| local pieces of a global | a `Cover` of g, a, b; `sections` glues it |
| the target the relation induces | the restriction composed with an endofunctor into the aim (`compose`), kept (`keep`) |
| the dynamics toward it | the target state's own arrow, driven on `Temporal` (`drive ... additive`) |
| a perturbation | an arrow on a declared port |
| zero time is the identity; restore reproduces | `laws::drives`, `to_text` / `from_text` |
| a global correlation | g's own element and arrows, which no restriction maps |

Nothing was added to State, Functor, StateGraph, Engine or the Cover.

What had to be written is three *readings* - pure functions of what the graph
already holds, in `Relax.hpp`, adding no kind of thing:

- `gap(graph, functor)`: what a functor says its target should hold, less what
  it holds - the functor run on its source into a scratch state. The engine
  had `identity_defects` (strings, for round trips that should be the
  identity); a relaxation wants a number per parameter, between two different
  states, to watch shrink.
- `orbit(gaps, tol)`: converges, periodic (with its period), persists, or
  grows; whether the size never rose; first, last and largest.
- `locality_defects(graph, cover, whole)`: descent's converse, one composite
  per overlap of the whole, read by `identity_defects` and one apply.

Each is small and generic (any functor, any cover, any parameters), and the
test exercises each both ways.

Where the ontology pushed back, and why it was right to:

- **The target lives in the target.** An arrow is a function of its own
  params and its event's args, so the laws can re-run it. A cannot read g, or
  a functor, from inside its arrow; the target has to be carried into a's own
  params first. So the local state has a slot for its aim (aim_x, aim_y) beside
  what it is (x, y). This is not a new concept - a kept functor writing a
  target's params is the usual way - but it means the codomain of the relation
  is doubled inside the target: what it is, and what it is asked to be.
- **Coupled arrows within g are natives.** `kick_pair` and `settle` write two
  elements of g at once. An affine step reads one element and writes one, so
  the notation declares them and C++ supplies the inside, reading only g's own
  elements (the native's interface: its own state).
- **Variants are built, not steered.** Each outcome is a's rate and spin set
  before the engine starts (rule 5: built fully before `start`); while it
  runs, only declared ports and arrows change anything.

## Beside monodromy

`Cover::monodromy` classifies a ring of overlaps by the *order* of its
composite functor: how many times round until it is the identity - 1, it
closes; k, it comes back after k turns; 0, not within 24. A ring that does not
close is the space's shape, not a fault.

`orbit` classifies a driven step by the order of its action on the gap: the
period after which the gap is what it was. The two are one question asked of
two maps. Monodromy asks it of a functor, structurally and exactly; orbit asks
it of a step in time, numerically, from what it was seen to do. Converging has no
counterpart in a ring: a composite functor does not get closer to the
identity, and a step that converges has no order. A periodic orbit is the dynamics' shape around a
target it never reaches, as a ring with monodromy is the space's shape around
a loop it never closes; a persistent gap of period 1 is a fixed point that is
not the target, as an automorphism that is not the identity is a ring that
comes back changed.
