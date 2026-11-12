# 6. The simulation's architecture (engine-independent)

What must be kept separate, what flows between the parts, and the
properties the whole must keep. Deliberately free of any engine: this is
the shape any implementation must have.

## 6.1 The parts, and why each is apart

| Part | Holds | Never holds |
| --- | --- | --- |
| **Mind** (one per contestant) | traits, needs, affect, hope/fear, beliefs, trust, memory | pixels, positions it was not told, other minds' contents |
| **Body** (one per contestant) | pose, animation, face, accessories, look | beliefs - it shows what the mind tells it to |
| **House** (the world) | places, objects, who is where, what can be seen/heard from where | anyone's opinion |
| **Media** (screens, letters, ads) | items: pictures for the audience, *claims* for minds | effects on minds - those are computed by minds |
| **Rituals / practices** | phases, roles, rules of each ceremony or routine | the outcome - that is the participants' choices |
| **Format** | rules: coupling required, prize, elimination, schedule | the story |
| **Producer** | drama model, open questions, levers it may pull | direct access to change a mind |
| **Story sifter** | patterns over the event log, threads | any power to change anything (it only observes) |
| **Audience** | dispositions, beliefs about questions, attention, votes, money | direct access to change a mind |
| **Edit / camera** | which scene and which thread to show, POV | the world |

The single most important separation: **a mind receives content, never
appearance**. The body is drawn from the mind's state; the mind learns of
the world only by **perception** - a one-way mapping from world events to
percepts, filtered by:

- *presence*: in the same place, or within sight/earshot;
- *attention*: what the mind was attending to (a mind in an argument does
  not notice a whisper across the room);
- *credibility*: for told and broadcast content, discounted by trust;
- *noise*: a chance of mishearing or misreading (memory error at encoding).

## 6.2 What flows between the parts

```
            percepts                        acts (intentions)
House  ---------------->  Mind  ---------------------------->  Body
  ^  (presence/attention)  |  ^                                  |
  |                        |  | claims (via trust)               | animation, face
  |   events (acts done)   |  |                                  v
  +------------------------+  +----- Media <---- Audience     (rendered)
                                         ^          |
                         Producer -------+          | votes, payments
                            ^   (levers)            v
                            +----- Story sifter <---- event log (read only)
```

- **Mind -> Body**: the chosen act and the current emotion. The body
  animates; nothing comes back but completion ("arrived", "kiss done").
- **Body/House -> Mind**: only percepts of events.
- **Mind -> House**: acts as events in the world (said X to Y in the
  kitchen at 21:04). Every act is an event with a verbal template.
- **Media -> Mind**: claims, at the moment a mind attends a screen or
  reads a letter.
- **Audience -> world**: only through declared levers that produce
  *in-world events*: an ad bought on the house TV (a media item), a letter
  from "the public" (a media item), a vote at a ceremony (a ritual input),
  a payment that sends a gift or an arrival (an object or a new agent),
  a rule change (format). Never "set B's love for C to 0.9".
- **Producer -> world**: the same kind of levers, with a budget.
- **Sifter -> Edit**: which threads are live; it changes nothing.

Why this discipline is worth it: every change in a mind has a cause in the
world that the audience could have seen. The show stays *fair* (no
invisible hand), *explainable* (why did she do that? here is the chain),
*replayable*, and the audience's power feels real because it acts the way
the world acts.

## 6.3 Time

- Each mind runs on its own step; fast processes (affect, attention) every
  step, slow ones (mood, need decay, belief forgetting) scaled by elapsed
  time. A mind's time is its own - not a global clock other parts read.
- The house has a day schedule (morning, tasks, evening, ceremony nights).
- Rituals run their phases on their own time.
- The audience's time is broadcast time: the show may run the world faster
  than real time between highlights and slow to real time for scenes
  (time compression is an edit decision, not a world one).

## 6.4 Determinism, saving and replay

- All randomness from seeded draws keyed by (seed, agent, step, purpose),
  so any moment can be reproduced and a decision explained after the fact.
- Every part is plain fixed-size data: the whole house saves and restores
  exactly. A season can be rewound for an audience "what if" without
  touching canon (a fork is a separate world).
- Event log is append-only: the story sifter and the edit read it; the
  audience can audit it ("show me every time A lied").

## 6.5 Invariants to check

| Invariant | Why |
| --- | --- |
| probabilities in [0,1]; Dirichlets positive; opinions b+d+u=1 | no broken beliefs |
| a mind's belief about an event it never perceived or heard of is at its prior | no leaks around perception |
| every change of a mind traces to a percept, a claim, or its own step | no invisible hand |
| the audience's levers produce only in-world events | audience never writes minds |
| same seed, same inputs -> same season | replay, explanation |
| a still house (everyone asleep) computes almost nothing | cheap when idle |

## 6.6 Budget

From [04-models.md](04-models.md): ~2.5k floats per mind, microseconds per
step. Real costs are elsewhere:

- **Rollouts** for suspense estimates: tens to hundreds of short
  simulations per open question, a few times a minute - parallel, bounded.
- **The sifter**: pattern matching over a growing log; index the log by
  person and verb, match incrementally.
- **Rendering and animation**: dwarfs the minds. Keep minds independent of
  frame rate.

## 6.7 Scaling the cast

Pairwise state grows as N^2, second-order as N^2 x concerns. For N up to
~30, full pairwise is fine. Past that (a whole village, alumni), keep full
state only for each agent's Dunbar "15" and summary reputations for the
rest; promote on contact.
