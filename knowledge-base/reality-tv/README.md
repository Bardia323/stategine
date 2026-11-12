# The House - a knowledge base for a simulated reality show

The goal: a reality-TV simulation whose one objective is to be **as
entertaining as possible to an audience watching it**, peopled by agents
cheap enough to run by the dozen with no language model, whose love,
betrayal, coalitions, feuds and rituals are not scripted but **emerge** from
a small architecture. Minds, bodies, the house, media and the audience are
kept apart: what a character knows is never what is drawn.

This is the research phase. Nothing here is built yet; it is what we know,
and where it comes from. It is independent of any engine or implementation.

## The files

| File | What is in it |
| --- | --- |
| [01-formats.md](01-formats.md) | Reality formats taken apart: the mechanics that make them work (confinement, scarcity, elimination, hidden roles, recoupling, the confessional, the twist), rituals, casting, the producers' craft, what reality TV gets wrong and how a simulation can do better |
| [02-attention.md](02-attention.md) | Entertainment as something computable: suspense and surprise (Ely-Frankel-Kamenica), curiosity, disposition theory, narrative questions, pacing directors, story sifting, the habituating audience - an objective the show can measure every frame |
| [03-priors.md](03-priors.md) | Human priors with numbers: personality (Big Five, HEXACO, dark traits), attachment, values, loss aversion, cooperation in economic games and on real game shows, attraction, love, infidelity, jealousy, gossip, conformity, status, hope and fear - each as a distribution to draw a contestant from |
| [04-models.md](04-models.md) | Cheap minds: the agent architecture - needs, fear and hope vectors, appraisal (OCC), beliefs as Beta/Dirichlet, trust as subjective logic, nested beliefs to depth two, memory by activation, gossip as propositions that travel and distort, media as evidence, balance and mimetic desire for triangles, investment model for cheating, coalition and ritual formation - with costs |
| [05-cybernetics.md](05-cybernetics.md) | Cybernetics, including what was buried: Ashby's homeostat and requisite variety, Beer's algedonic meters (Cyberfolk - audience voting fifty years early), Pask's bored machine, Bateson's schismogenesis, Maruyama's deviation-amplification, Powers' perceptual control, von Foerster's non-trivial machines, Lefebvre's reflexive control and algebra of conscience, Moreno's sociometry, affect control theory |
| [06-architecture.md](06-architecture.md) | The simulation's architecture, independent of any engine: what must stay separate (mind, body, house, media, audience, producer), what flows between them, clocks, determinism, save/restore, a cost budget |
| [07-show.md](07-show.md) | The show itself: format proposal, characters (cute, distinct, readable), audience powers that stay fun, the drama loop, metrics, experiments, open questions |
| [references.md](references.md) | Bibliography, by topic |

## The thesis, in one page

1. **Drama is conflict between goals under uncertainty, seen by someone who
   cares.** Every piece follows: agents need *goals that collide* (love,
   money, status, safety, belonging), *uncertainty* about each other (hidden
   feelings, hidden roles, lies), and an *audience model* that knows more
   than any one agent but not everything (dramatic irony).

2. **Emergence needs few mechanisms that feed back.** Love triangles fall
   out of mimetic desire + Heider balance + an investment model; coalitions
   out of threat + reciprocity + reputation by gossip; feuds out of
   schismogenesis (each one's reaction the other's cause); rituals out of
   repeated co-presence at fixed times and social practices that hold
   roles. None of these needs to be scripted - each is a few lines of
   arithmetic on pairwise numbers.

3. **Minds are cheap if beliefs are conjugate.** Beta and Dirichlet
   beliefs update by addition; trust as subjective-logic opinions discounts
   and fuses in closed form; memory as ACT-R activation is a sum of decays.
   Twelve contestants with full pairwise state and second-order beliefs is a
   few thousand floats and microseconds a step. No LLM: events carry
   **verbal descriptions from templates**, chosen by what happened, and
   gossip is a proposition with a teller, not prose.

4. **What is seen is not what is known.** The renderer draws bodies,
   screens, ads; a mind receives only *what it perceives* - a functor from
   world events to percepts, filtered by attention, line of sight and
   credibility. A screen shows pixels to the audience and evidence to the
   contestant. Mental simulation and rendering are separate systems joined
   only by explicit, one-way channels.

5. **The audience is a regulator with an algedonic channel.** Beer's
   pain/pleasure signals, a viewer's vote or payment, enter the world only
   only as things *in* the world - a letter, an ad on the
   house TV, a bombshell arrival, a rule change - never a write into a mind.
   The world stays causal, the audience stays a participant, and the
   characters stay themselves.

6. **The producer is a drama manager, not a god.** It watches (const) and
   measures suspense; it acts only through the same declared levers the
   format gives it (twists, tasks, arrivals, the edit). It is one state
   among others, with no special access to minds.

7. **Entertainment is measurable.** Suspense = the expected variance of the
   audience's next beliefs about the questions it cares about; surprise =
   how far beliefs moved; both computable from the simulation's own
   probabilities. A prediction market among viewers is the same quantity,
   sampled. The show can be tuned against it.

## Status of the numbers

Priors in [03-priors.md](03-priors.md) are from the literature, rounded,
with sources. Psychology has a replication problem: effects that failed to
replicate are marked, and every number is a *starting distribution* to draw
characters from, not a law of nature. Where a number is from memory and
should be checked against the source before it is relied on, it says
**(verify)**.
