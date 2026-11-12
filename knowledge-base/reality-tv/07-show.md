# 7. The show: a format, its characters, the audience's powers

A first proposal, to argue with. Everything here follows from 01-06.

## 7.1 The format: "Love or Money" (working title)

**Premise.** Twelve characters live in a house for a season of simulated
weeks. There is a **prize** (money) and there is **coupling**. At the end:
the final couples face a **Split or Steal** for the prize - so love and
money meet at one decision, and every relationship all season is read in
its light.

**Weekly cycle** (build-up -> peak -> fallout, Left 4 Dead's director):

| Day | Event | What it does |
| --- | --- | --- |
| 1 | arrival / bombshell | raises alternatives; tests commitment |
| 2-3 | tasks in pairs or teams (sometimes frightening: misattribution of arousal) | forced proximity, shared stakes, coalitions |
| 3 | **screen night**: media on the house TV (a clip, a letter, an audience ad) | evidence into minds; dramatic irony |
| 4 | free day: privacy available, alcohol, the terrace at night | opportunity: cheating, confessions, schemes |
| 5 | **the table** (hidden-role round, some weeks) | accusation, scapegoat, trust tested |
| 6 | **recoupling**: each chooses, publicly | private states become public fact |
| 7 | **elimination** by a mix: peers nominate, audience decides between nominees | strategy + performance |

**Hidden roles some weeks** (Traitors-style): one or two characters are
secretly told they may "steal" part of the pot if never discovered. The
audience knows. Irony by construction.

**The jury**: the eliminated stay in a side house, still hearing gossip
(through visits and media), and judge the final; betrayal has a delayed
price.

**Rules as data**: coupling required on/off, touch rules with a shared
pool (Too Hot to Handle), anonymity rounds (The Circle), truth booths (Are
You the One) - switched per season or by vote.

## 7.2 The characters

**Cute.** Kindchenschema (Lorenz 1943; Glocker et al 2009): big head, big
eyes low in the face, round forms, short limbs make faces read as cute and
raise care-giving and attention. Cuteness makes the audience forgive and
care - suspense needs caring. Proven in this genre: Tomodachi Life, Animal
Crossing, Tamagotchi.

**Distinct at a glance.** Each character must be recognisable from its
silhouette and its colour alone: a unique shape (hair, ears, hat, body
proportion), a signature colour, and **one signature accessory**. Test:
the cast in black silhouette, at thumbnail size - every one nameable.

**Looks tell nothing about the mind.** Draw appearance independent of
traits (the Love Is Blind lesson), except where a character *chooses* it
(a vain one dresses up) - so the audience must learn who they are by what
they do.

**Accessories as story objects.** A gifted bracelet carries a bond; a
borrowed hoodie signals a couple; a necklace thrown in the pool is an
event. Objects are in the world, minds know who gave what, and the
audience can follow a relationship by the objects worn.

**Legible minds.** Faces and poses show the dominant emotion; a small
thought icon can show hope or fear and its object (a heart toward B, a
coin, a skull for elimination). The confessional renders the mind's top
concerns as templated lines. Personality shows in idle behaviour (the
clown juggles, the strategist watches).

**Archetypes from traits** (01 *Casting*): cast by sampling the priors
(03) many times and picking the set that maximises expected conflict and
variety: spread on the circumplex, at least one low-H schemer, one high-A
peacemaker, one anxious and one avoidant attachment, mixed SVO, values on
opposite sides of Schwartz's circle, attractiveness uncorrelated.

## 7.3 What the audience can do

Principles: (1) every power acts *through the world* (06); (2) powers are
priced by leverage (Meadows), so cheap powers nudge and rare powers reshape;
(3) no single viewer can buy the story (quadratic costs, caps, pooled
spends); (4) every intervention is visible and attributed ("the public
sent...") - an in-world cause the characters react to.

| Power | Mechanism | In-world effect | Leverage |
| --- | --- | --- | --- |
| **The dial** (Cyberfolk) | continuous like/dislike per character | aggregate shapes who the producer's levers favour; shown to contestants as "public mood" only if the format says | low, constant |
| **Vote** | at elimination, between nominees | ritual input | high, weekly |
| **Send a letter / gift** | pay; template message or object | a claim or an object arrives for a character | medium |
| **Buy the screen** | pay; choose a clip of what really happened, or an ad | media item; claims enter whoever watches | medium-high (truth told to the house) |
| **Whisper** | pay; plant a rumour with one character, marked as "an anonymous note" | a claim with low source trust | medium; can be false - and the audience sees the cascade |
| **Bombshell** | pooled vote among candidates | new agent enters | high, rare |
| **Rule change** | season-level vote | format rule toggled | very high, rare |
| **Predict** | a market on open questions | none in-world; the price is the suspense meter | none |

Revealing the truth on the screen is the strongest emotional power: the
audience has watched a lie spread, and now can pay to show the house the
footage. Price it so it is a climax, not a routine.

## 7.4 The producer and the edit

- **Open questions** (02, Carroll) maintained live: "will A stay loyal",
  "who is the traitor", "will the coalition hold". Each with audience
  belief, stakes (dispositions), and a schedule of how it resolves.
- **Pacing**: the producer keeps a tension envelope; in build-up it pulls
  levers that raise suspense (a task pairing rivals, a clip on screen
  night); at peak it lets the ritual run; in relax it schedules warm,
  funny scenes (the clown's task, a group dinner ritual).
- **Budget**: a fixed number of lever pulls a week, each logged and
  visible to an auditing viewer. Requisite variety: enough kinds of lever.
- **The edit**: the story sifter proposes threads; the edit picks the
  camera with the highest `E(t)` (02) under a variety constraint (A/B/C
  plots), shows dramatic-irony scenes (we see the lie being told), and
  cuts away at the peak of suspense before a ritual.

## 7.5 Metrics: how we know it is working

From inside the simulation (no audience needed, for tuning):

- **suspense and surprise** integrated per episode, per question;
- **irony** integrated per episode;
- **arc shapes** per character over the season (fortune over time) -
  aim for variety: at least one man-in-a-hole, one rise-and-fall;
- **thread yield**: sifter patterns matched per simulated day (betrayals,
  triangles, reconciliations, rituals formed) - a flat run has few;
- **distinctness**: can a classifier tell characters apart from behaviour
  alone? If not, they are not distinct personalities;
- **criticality**: how far a small perturbation spreads (too little: inert
  house; too much: chaos).

From a real audience: watch time, rewinds, dial variance, market volume,
chat rate at reveals.

## 7.6 Experiments for the next phase

1. **Minimal house**: 6 agents, needs + liking + attraction + balance +
   investment model only. Do triangles and cheating emerge? How often per
   week? (Ablate each mechanism: which are necessary?)
2. **Gossip**: add memory and subjective-logic trust. Measure rumour reach,
   distortion, and how often a lie is found out. Does a Machiavellian agent
   win more?
3. **Media**: inject a true clip and a false rumour; compare belief shifts
   across trust networks and attachment styles.
4. **Rituals**: free schedule; do recurring gatherings and norms form?
5. **Suspense estimator**: rollouts vs observed surprises in a held-out
   run - is the estimate calibrated?
6. **Producer on/off**: same seeds, with and without levers - does `E(t)`
   rise without the producer's hand being visible?
7. **Audience simulation**: synthetic viewers with dispositions, voting
   by Zillmann - do votes make the show better or flatten it (e.g. always
   evicting the villain kills the plot)? Design counterweights.

## 7.7 Open questions

- How much inner state to show the audience? Full minds kill some irony
  (no mystery about feelings); none kills legibility. Probably: show
  emotions always, beliefs on demand, hidden roles always, secret feelings
  only in confessional.
- Should contestants know they are watched and voted on? (Changes the game
  from social to performative.)
- Verbal layer: templates plus EPA (05) for choosing words - is it rich
  enough over a season, or do lines repeat? Pool size per act type.
- Ethics of the audience's power when the cast are agents: none in harm,
  but an audience paying to torment a cute character may make the show
  feel cruel; disposition theory says audiences want *deserved* outcomes -
  design powers that favour justice, comedy and romance over cruelty.
- Persistence: do alumni return with full memories? (Strong for long-run
  attachment; costly to keep coherent.)
