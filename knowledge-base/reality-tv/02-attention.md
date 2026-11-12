# 2. Entertainment, made computable

The simulation has one objective: hold an audience. That needs a model of
*why attention is held* that the simulation can compute from its own state,
every few seconds, to steer the producer and the edit. The literature gives
one surprisingly precise answer, with a few supporting ones.

## 2.1 Suspense and surprise (Ely, Frankel & Kamenica 2015)

The cleanest formal theory. An audience holds beliefs `mu_t` about a
question with outcomes `w` (who leaves tonight, will A and B couple, will C
steal). Over the show beliefs move toward the truth.

- **Suspense** at time `t` = the expected variance of the next belief:
  `S_t = E[ || mu_{t+1} - mu_t ||^2 ]`. High when something is about to be
  decided and the audience does not know which way.
- **Surprise** at time `t` = how far beliefs just moved:
  `U_t = || mu_t - mu_{t-1} ||`.

They show that suspense-optimal revelation concentrates uncertainty toward
the end and keeps the outcome a coin flip until late - the shape of every
elimination ceremony ("...and the person leaving tonight is..."). Surprise
optimal policies have plot twists: beliefs held confidently, then reversed.

*Why this matters here:* the simulation knows every agent's probabilities
of acting. It can compute the audience's beliefs (from what the audience
has been *shown*) and their next-step variance exactly or by sampling a
few rollouts. That gives a scalar to maximise, per open question.

## 2.2 Bayesian surprise attracts the eye (Itti & Baldi 2009)

Human gaze goes to where a Bayesian observer's beliefs change most (the KL
divergence between posterior and prior), better than to where things are
merely novel or salient. Same quantity at the level of pixels: cut to the
camera whose scene is moving beliefs most.

## 2.3 Curiosity: the information gap (Loewenstein 1994)

Curiosity rises when a person is aware of a gap between what they know and
what they want to know, and is greatest when the gap is *small enough to
close*. Teasers, cliffhangers and partial reveals all open gaps. Kang et al
(2009): curiosity peaks at intermediate confidence, inverted-U.

*Model:* each open question has an audience confidence; curiosity is highest
near `p ~ 0.5`-`0.7` and when the question is salient. Too many open
questions = confusion (cognitive load); too few = boredom.

## 2.4 Narrative questions (Carroll 1984)

Noël Carroll's *erotetic* model: a story holds us by raising questions
("will she find out?") and answering them; a scene is engaging when it
raises, sustains or answers a question we care about. This gives a data
structure: **the open questions** - a small set, each with stakes (who it
matters to), audience belief, time raised, and how it will be answered (a
ceremony, a reveal, an act).

## 2.5 Caring: affective disposition theory (Zillmann)

Enjoyment of drama depends on dispositions toward characters: we like
those we judge moral or sympathetic, and we enjoy their success and the
punishment of those we dislike. Suspense is felt *for* someone: it scales
with how much the audience cares about the person whose fate is at stake.

*Model:* the audience has a disposition `d_i` toward each character (from
their shown acts - kindness, loyalty, cruelty, humour - and from looks);
stakes of a question = sum over affected characters of `|d_i|` times the
size of the outcome for them. Weighted suspense `sum_q stakes_q * S_q`.

Parasocial attachment (Horton & Wohl 1956) adds: characters seen often,
seen vulnerable (confessional), and seen consistently form ties that make
viewers come back. *Consistency matters:* a character must stay
recognisably themselves (stable traits, catchphrases, accessories), while
their situation changes.

## 2.6 Arousal and the inverted U (Berlyne 1971)

Berlyne's "collative variables" - novelty, complexity, conflict,
uncertainty - raise arousal; liking follows an inverted U: too little is
boring, too much aversive. Habituation lowers the effect of repeated
stimuli. Pask's Musicolour ([05-cybernetics.md](05-cybernetics.md)) made a
machine of it: it got bored of repetition and forced novelty.

*Model:* audience arousal `A` with habituation per stimulus type (a fight,
a kiss, a twist each lose punch with repetition: `effect = base * exp(-n/h)`
with `n` recent uses), and a target band for `A`.

## 2.7 Pacing: the director (Left 4 Dead, RimWorld)

- **Left 4 Dead's AI Director** (Booth 2009) tracks each player's
  "emotional intensity", and cycles **build-up -> sustain peak -> peak fade
  -> relax**, refusing to spawn threats in the relax phase. Without the
  relax, intensity stops registering.
- **RimWorld's storytellers** (Cassandra, Phoebe, Randy) pick incidents by
  a tension budget that grows with time and with how well the colony is
  doing; Randy is deliberately random.
- **Freytag's pyramid / the TV act structure**: rising action to a climax
  per episode, a cliffhanger at each act break.
- **Story arcs** (Reagan et al 2016): six basic shapes of fortune over
  time - rise, fall, fall-rise, rise-fall, rise-fall-rise, fall-rise-fall.
  The "man in a hole" (fall-rise) and "Cinderella" are the most popular.
  A character's arc is their fortune (a valence) over episodes; the
  producer can aim for arcs, not events.

## 2.8 Emergent narrative needs curation (Ryan 2018; Kreminski et al)

James Ryan's dissertation *Curating Simulated Storyworlds* is the key
warning: **simulations generate far more events than stories**; most are
noise. What makes emergent narrative work is a *curator* that finds the
stories in the event stream - **story sifting** - with patterns ("A
betrayed B, who had helped A", "a triangle formed and closed", "a promise
was broken") matched over the event log (Kreminski's *Felt*, *Winnow*). The
sifter is an observer: it changes nothing, it only chooses what the edit
shows and what the producer might nudge.

## 2.9 Dramatic irony and point of view

Hitchcock's bomb under the table: surprise is ten seconds of shock;
suspense is ten minutes, because the audience *knows* and the characters
do not. The biggest lever in a simulated show is **the information gap
between audience and characters**: show the audience a traitor's role, a
secret kiss, a lie being told - then let them watch the others not know.

*Model:* for each fact, who knows it (each agent's belief) and whether the
audience was shown it. Irony of a scene = sum over facts relevant to the
scene of `audience_knows * (1 - agents_know) * stakes`.

## 2.10 The objective

Putting it together, a per-moment score the producer and the edit maximise:

```
E(t) =   a * sum_q stakes_q * suspense_q(t)        # Ely et al, weighted by caring
       + b * sum_q stakes_q * surprise_q(t)         # twists, reversals
       + c * irony(t)                               # audience knows, they don't
       + d * curiosity(t)                           # open gaps near closable
       + e * warmth(t)                              # bonding, comedy: the relax phase
       - f * confusion(t)                           # too many open questions, unclear who is who
       - g * repetition(t)                          # habituation by event type
```

with a pacing envelope: the weights on suspense and surprise rise in
build-up and fall in relax, as Left 4 Dead's director does. Every term is a
function of the simulation's state and of what the audience was shown -
cheap to compute, and auditable.

A prediction market among viewers ("who leaves tonight") estimates the
same thing from outside: prices are the audience's beliefs, their expected
movement is suspense. Real-time markets are also engagement in themselves.

## 2.11 What the audience must be able to read

Entertainment collapses if the audience cannot tell characters apart or
read what they feel. Requirements that follow:

- **Distinct at a glance**: silhouette, colour, accessory per character
  (team-shooter "read at a glance" design: TF2, Overwatch).
- **Legible emotion**: faces and bodies show the mind's appraisal clearly
  (cartoon exaggeration; Disney's principles of animation, "staging").
- **Few strands at once**: TV soap writing keeps three or so storylines
  per episode (A, B, C plots), cutting between them.
- **Recaps and names on screen**: memory aids lower confusion.
