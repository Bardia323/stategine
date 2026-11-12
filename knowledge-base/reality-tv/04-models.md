# 4. Cheap minds: the agent architecture

Goal: psychologically rich agents whose interesting behaviour emerges, at a
cost of microseconds per agent per step, with no language model. The trick
is to choose representations whose updates are **closed-form** - conjugate
priors, linear decays, fixed-size tables - and to keep the number of
mechanisms small but **mutually feeding**.

## 4.1 Prior art to borrow from

| System | What to take |
| --- | --- |
| **The Sims** (Wright 2000) | needs (motives) decaying over time; actions advertise how much they satisfy each need; utility = sum of weighted need deltas with a curve that makes urgent needs dominate |
| **Utility AI** (Dave Mark 2009, *Behavioral Mathematics*) | response curves over considerations, multiplied; cheap, tunable, explainable |
| **BDI** (Rao & Georgeff 1995) | beliefs, desires, intentions - commit to a plan until reconsidered |
| **Comme il Faut / Prom Week** (McCoy et al 2013) | *social physics*: volitions toward each other computed from rules over the social state; *social exchanges* (flirt, insult, ask out) with preconditions and effects; a *cultural knowledge base* of what is cool and lame; a *social facts* log the rules can read ("A was embarrassed by B last week") |
| **Versu** (Evans & Short 2014) | *social practices* (a dinner, a conversation, a quarrel) as objects with roles, that offer each participant actions; characters pick by desire; practices run concurrently; norms and evaluations attached |
| **Talk of the Town** (Ryan et al 2015) | knowledge as *mental models* of others with sources and confidence; knowledge propagates by conversation, **mutates** (memory errors), lies are told; characters can be wrong |
| **Crusader Kings** | traits with opinion modifiers between traits (two greedy rulers dislike each other); opinion as a sum of decaying modifiers - simple and wildly productive of drama |
| **Dwarf Fortress** | personality facets, values, needs, memories that recur and change mood; emergent histories |
| **RimWorld** | mood as sum of thoughts with durations; mental breaks at thresholds - "meltdowns" |
| **Façade** (Mateas & Stern 2005) | drama manager sequencing *beats* by a tension curve |
| **Generative Agents** (Park et al 2023) | memory stream with recency x importance x relevance retrieval, reflection - the retrieval score is cheap and worth copying; the LLM is not |
| **BayesACT** (Hoey, Schröder & Alhothali 2016) | affect control theory as a POMDP: agents act to keep the feeling of a situation consistent with identities (see [05-cybernetics.md](05-cybernetics.md)) |

## 4.2 The architecture at a glance

Each agent has, as plain numbers:

```
traits        OCEAN+H, circumplex (dom, warm), attachment (anx, avo), SVO,
              lambda, k, goal weights g[goal], moral foundations    -- fixed
body state    fatigue, arousal, hunger, intoxication, position       -- fast
affect        emotions e[22 OCC types] -> mood (valence, arousal, dominance)
needs         n[goal] in 0..1, decaying toward unmet               -- drives
prospects     for each goal-relevant future event: P(event), value  -- hope/fear
about each other agent j:
   trust      opinion (belief, disbelief, uncertainty, base rate)   -- subjective logic
   liking     warmth toward j, a sum of decaying modifiers
   attraction lust / romance / attachment toward j (Fisher's three)
   respect    perceived competence / status of j
   model of j estimated traits of j (Normal mean, variance) and j's goals
   2nd order  what i thinks j feels about k  (for k in i's concern set)
facts         a fixed-size memory of propositions with activation
practices     which social practice / ritual i is in, and its role
```

Decisions: each step, each agent scores a short list of **affordances**
(actions the current place, practice and people offer) by expected utility
under its beliefs, picks by softmax with a temperature from impulsivity,
and commits (BDI-style) for a few steps unless an emotion spike interrupts.

## 4.3 Hope and fear vectors

From OCC: **hope** and **fear** are emotions about *prospects* - possible
future events, weighted by how much they matter and how uncertain they are.

For each goal `g`, an agent keeps a few prospective events `x` (e.g. "B
recouples with me", "I am eliminated Friday", "I win the money") with a
believed probability `p_x` and a goal value `v_x` (positive desired,
negative feared). Then:

```
hope[g] = sum_{x in g, v_x > 0} w_g * v_x * f(p_x)
fear[g] = sum_{x in g, v_x < 0} w_g * -v_x * f(p_x)
f(p)    = 4 p (1 - p) * intensity(p)   # strongest when uncertain; OCC's
                                       # likelihood, shaped so certainty
                                       # becomes satisfaction or dread
```

with gains on fear from N, attachment anxiety and prevention focus; gains
on hope from E, promotion focus, Snyder's agency. When the event resolves,
hope becomes **satisfaction** or **disappointment**, fear becomes
**relief** or **fears-confirmed** - each with intensity proportional to
the prior hope/fear. These are the *spikes* the audience sees.

The vectors `hope[g]`, `fear[g]` over goals (love, money, status, safety,
belonging...) **are the character's inner drama**: which goal they hope
about, which they fear for, and the conflict when an action raises hope in
one goal and fear in another (kiss the bombshell: hope in love, fear in
safety and reputation). Expose them to the audience as the character's
"stakes".

Action value under fear/hope, prospect-theoretic:

```
U(a) = sum_g w_g * [ v_gain(dHope_g(a)) - lambda * v_loss(dFear_g(a)) ]
       + social terms (SVO-weighted others' outcomes)
       - effort, - norm violation cost * (1 - low H)
```

## 4.4 Beliefs: conjugate and cheap

- **Binary facts** ("A kissed C"): Beta(alpha, beta); evidence for adds to
  alpha, against to beta, each weighted by source reliability. Mean
  `alpha/(alpha+beta)`; uncertainty shrinks with total count.
- **Categorical** ("who is the traitor", "who will B pick"): Dirichlet;
  same additive updates.
- **Continuous** (another's warmth, trait estimates): Normal with known
  noise - Kalman-style: precision adds, mean is precision-weighted.
- **Forgetting / nonstationarity**: multiply counts by `gamma < 1` per day
  (exponential forgetting) so beliefs stay responsive - people change.
- **Bias**: confirmation bias as asymmetric weights (evidence for a
  favoured belief weighted >1); motivated reasoning about one's partner
  (discount bad news about them by attachment strength).

Probabilistic graphical models where structure matters:

- A small **Bayesian network per kind of inference**, with fixed structure
  and conditional tables shared by all agents (so it costs one table, not
  one per agent). E.g. *is my partner cheating*: `partner_interest_in_X`,
  `opportunity`, `partner_commitment` -> `cheated` -> evidence nodes
  `seen_together_late`, `gossip_says`, `partner_cold`. Exact inference by
  enumeration over a handful of binary nodes is microseconds.
- A **dynamic Bayesian network** (or HMM) for hidden states of others that
  change: "B's interest in me" as a 3-state chain (rising, steady, falling)
  filtered from B's acts.
- **Factor graphs** for joint house-level inference (Are You the One's
  matching, Traitors' role assignment): loopy belief propagation or a few
  hundred samples per agent per day, cached.

## 4.5 Trust as subjective logic (Jøsang)

Trust in a source needs *uncertainty*, not one number. A subjective-logic
**opinion** `w = (b, d, u, a)`, `b + d + u = 1`, is a Beta distribution
with evidence counts `r, s`: `b = r/(r+s+2)`, `d = s/(r+s+2)`,
`u = 2/(r+s+2)`; `a` is the base rate (prior, from the agent's general
trust and A). Operators in closed form:

- **Discounting** (trust transitivity): if i trusts j with `w_ij` and j says
  X with `w_jX`, then i's opinion of X via j:
  `b = b_ij * b_jX`, `d = b_ij * d_jX`, `u = d_ij + u_ij + b_ij * u_jX`.
  Gossip through a chain loses certainty at each link - automatically.
- **Fusion** (two independent sources): cumulative fusion adds evidence.
  Hearing the same rumour from two friends is stronger than from one -
  **unless they are not independent** (both heard it from C): track the
  source chain to avoid double counting, or deliberately *don't* and get
  the illusory-truth effect for free.
- **Updating trust in a source**: when a told fact is confirmed or refuted
  by direct observation, add `r` or `s` to the teller's opinion. Lies
  caught cost a lot (weight by negativity bias, ~2-5x).

This is where **exploiting trust** becomes mechanical: a Machiavellian
agent tells mostly true things to build `b`, then spends it on one big lie
when the payoff is high (a trust-building then defection strategy). The
victim's discounting makes the lie believed in proportion to the trust
earned. The audience, which saw the truth, watches it happen.

## 4.6 Memory: facts with activation

A fixed-size store per agent (say 64-256 propositions). Each proposition:

```
(subject, verb, object, place, time, valence, importance,
 source: seen | told_by j | inferred, confidence opinion,
 template_id + slots for verbal description)
```

Activation by **ACT-R base-level learning** (Anderson):
`B = ln( sum_k t_k^-d )`, `d = 0.5`, over the times `t_k` since each use
(observation, retelling, rehearsal). Add importance and emotional
intensity. Retrieval = highest activation among those matching a cue
(person, place, topic). Lowest activation is overwritten when full -
forgetting is the eviction policy. Cheap approximation: keep the last few
use times or a running "strength" that decays by `t^-0.5`.

Rumination (high N): negative memories rehearsed spontaneously, keeping
them active and keeping a grudge alive. Forgiveness (high A): grudge memories
decay faster and are not rehearsed.

**Memory errors** (Talk of the Town): on retrieval, with probability rising
with age and low activation, a slot is swapped (wrong person, wrong place) -
an honest source of false rumours.

## 4.7 Gossip: propositions that travel

No LLM: an event in the world produces a **proposition** plus a **verbal
template** ("{A} kissed {B} by the pool", "{A} said {B} is fake"). The
template is filled with names to show the audience (subtitles, speech
bubbles, confessional lines); the agents exchange the *proposition*.

When i talks with j, i picks what to share. Score each memory m:

```
tell(m, j) = juiciness(m) * trust(i->j) * relevance(m, j)
           + strategic(m, j)            # does telling help my goals?
           - risk(m)                    # harm to me if traced back
juiciness  ~ importance * ambiguity * |valence| * novelty_to_j   # Allport & Postman
strategic  e.g. telling j that k insulted j lowers j's liking of k (my rival)
risk       ~ P(subject learns i told) * subject's power over i
```

Retelling **distorts** with small probabilities (Allport & Postman):
*levelling* (drop the place or the time), *sharpening* (push valence
further: "flirted" becomes "kissed"), *assimilation* (swap the subject
toward someone the teller already dislikes). A **lie** is a proposition
the teller does not believe, chosen for its strategic value, told with
confidence.

The hearer updates by subjective-logic discounting through the teller,
fuses with what it already holds, and the proposition enters its memory
with `source = told_by i`. Detection: if the hearer has contradicting
direct evidence, the teller's trust takes a hit; otherwise detection rate
~54% at best (Bond & DePaulo 2006), biased to truth.

Consequences that emerge: rumours cascading through trust networks; echo
chambers (coalitions hear their own side); reputations built and wrecked
offstage; the teller of a big lie found out three days later; **the
audience watching misinformation travel**.

Information spread at the house level follows SIR-like rumour dynamics
(Daley & Kendall 1964): ignorants, spreaders, stiflers (who know and stop
telling when they meet someone who knows).

## 4.8 Media as evidence

A screen in the house (an ad, a "message from home", a news clip, a video
of an earlier night) is rendered for the audience as pictures. The agents
never see pictures. The media item carries **semantic content**:

```
claims: [proposition...]       e.g. "B was seen with X before the show"
topic, valence, source (who made it), production (vivid, repeated, framed)
```

A watching agent who is attending gets each claim as evidence, discounted
by its trust in the source. Persuasion research gives the modulators:

- **Elaboration likelihood** (Petty & Cacioppo 1986): high-involvement
  agents (the claim is about their partner) weigh argument quality;
  low-involvement agents weigh peripheral cues (source attractiveness,
  repetition, how many others seem to believe it).
- **Illusory truth**: each repetition adds a little belief even from a low
  trust source.
- **Sleeper effect** (Hovland): the source discount decays faster than the
  content - a discredited claim gains belief over days (separate decay for
  the source tag).
- **Priming of goals**: an ad for luxury raises the weight of money
  temporarily; a romantic film raises love; footage of a fight raises fear.
  Media updates *priors* and *goal weights*, not just facts.

This is the channel through which the audience reshapes the world: they
buy an ad slot, they choose a clip, they send a letter - and the content
enters minds only as evidence, by the same rules as gossip.

## 4.9 Love triangles: why they emerge

Three mechanisms, each small, together near-certain to make triangles:

1. **Fisher's three systems** (lust, romance, attachment) as separate
   per-person numbers. Attachment builds slowly with time together;
   lust responds to attractiveness and novelty; romance to reciprocity and
   uncertainty. They can point at different people. Tension = an agent
   whose top attachment and top lust differ.
2. **Mimetic desire** (Girard 1961; mate-choice copying evidence): i's
   attraction to k rises with how much i sees others (especially rivals or
   admired others) desire k: `attr_ik += mu * sum_j respect_ij * attr_jk`.
   Desire concentrates on a few, and rivals become rivals *because* they
   want the same person - Girard's model of conflict.
3. **Heider balance** (1946): a triad i, j, k is stable when the product of
   the three signed ties is positive (friend of friend is friend; enemy of
   friend is enemy). Imbalance creates pressure to flip one tie. Two who
   love the same person and are friends are an unbalanced triad - something
   gives: the friendship, or one love. Cartwright & Harary (1956): signed
   graphs relax toward two opposing camps - **coalitions** fall out of the
   same rule.

Add **investment model** commitment (`satisfaction - alternatives +
investment`) and **opportunity** (privacy in the layout, night, alcohol),
and **cheating** is a decision with legible causes: low commitment, a
high-quality alternative present, a private moment, low C or low H, and an
expected detection probability below the agent's risk tolerance. Getting
caught is then a gossip cascade.

## 4.10 Coalitions

- **Threat** of elimination raises cooperation value (Sherif's
  superordinate goals; minimal groups).
- **Coalition formation** as a hedonic game: each agent's utility for a
  group = sum of liking and trust of members + expected protection from
  the votes it holds. Agents join, leave or merge when it raises their
  utility; stable partitions emerge (core-stable when they exist).
- **Minimum winning coalitions** (Riker 1962): coalitions grow only as large
  as needed to win, then turn on the weakest member - Survivor's core
  dynamic. Agents who estimate vote counts (Dirichlet over others' votes)
  find this themselves.
- **Balance** (above) polarises the house into blocs; **gossip** inside a
  bloc reinforces it (echo chambers, bounded-confidence opinion dynamics:
  Deffuant 2000, Hegselmann & Krause 2002 - agents only listen to those
  whose opinions are close; clusters form).
- **Betrayal timing**: the value of defecting from a coalition rises as
  the end nears (finite horizon unravels cooperation); a jury of the
  eliminated pushes it back down.

## 4.11 Rituals and norms

Two senses:

- **Format rituals** - recoupling, elimination, round table. Each is a
  **social practice** (Versu): a fixed sequence of phases, roles (host,
  chooser, nominee, voters), and per-phase affordances. Agents enter the
  practice, act in role, and leave changed. Fixed form, emergent content.
- **Emergent rituals** - a morning coffee group, a nightly "debrief" on
  the terrace, a handshake, a nickname, a toast before ceremonies. From
  Collins' interaction ritual chains: co-presence + shared focus + shared
  mood -> emotional energy -> desire to repeat with the same people at the
  same place/time. Model: agents remember (place, time, people, mood
  outcome) of gatherings; if positive, the place-time gains utility for
  those people; repetition makes a **habit**, then a **norm** (others expect
  it; absence is noticed and appraised as a slight). Norms as conventions
  emerge from repeated coordination games (Young 1993; Bicchieri 2006).
  Objects can become **sacred symbols** (Durkheim): a necklace given in a
  bonding moment carries the bond's value; losing or regifting it is an
  event.

## 4.12 Interaction: one exchange

When two (or more) agents meet in a practice, an exchange runs:

1. Each picks an **act** from what the practice affords (greet, joke,
   flirt, confide, ask, gossip, compliment, tease, criticise, accuse,
   apologise, withdraw, propose an alliance...). Each act has a circumplex
   vector (dominance, warmth), an intimacy level, and an affect-control
   EPA (see 05).
2. The receiver appraises it (OCC: desirability for own goals, praiseworthy
   by own standards), updates liking/trust/attraction, and responds -
   complementarity and attachment style shape the response.
3. Both write a memory; the scene gets a verbal template for the audience.
4. Spectators (anyone who could see or hear) get the event as an
   observation, discounted by distance and attention.

Escalation (an argument) is a loop of steps 1-2 with schismogenesis
dynamics; de-escalation by apology, a third party, or exhaustion.

## 4.13 Cost

For `N = 12` agents:

| Item | Size |
| --- | --- |
| traits, needs, affect, prospects | ~100 floats / agent |
| pairwise (trust 4, liking, attraction 3, respect, model of j ~10) | ~20 floats x 11 others = 220 / agent |
| second order, capped at 4 concern targets x 11 | ~44 / agent |
| memory, 128 propositions x ~16 numbers | ~2k / agent |
| **total** | **~2.5k floats / agent, ~30k for the house (~120 kB)** |

Per step, per agent: decay (linear passes), score ~20 affordances (a few
hundred multiplies each), one or two belief updates (additions). Well under
10 microseconds on one core. A day of simulated time at 1 step/second is
~1 second of compute for the whole house. Rollouts for the suspense
estimate (02) - 50 rollouts x a few hours of sim time - run in a fraction
of a second, in parallel.

Everything is a fixed-size table, so the whole mind can be saved, restored,
replayed and checked deterministically - no hidden state.
