# 3. Human priors, as distributions to draw characters from

A contestant is a draw from a population. This file gathers the
distributions and effect sizes the literature gives, rounded, with sources,
and says how each becomes a parameter. Psychology's replication crisis
applies: effects that failed to replicate are marked **(failed)**, contested
ones **(contested)**, numbers from memory to check **(verify)**.

General rule for drawing: traits are **continuous and correlated**. Draw a
vector from a multivariate normal on z-scores with the correlation matrix
below, then map each to the parameters that use it. Never draw types
("the villain") directly - draw traits, then read the archetype off them.

## 3.1 Personality

### Big Five (OCEAN)

Openness, Conscientiousness, Extraversion, Agreeableness, Neuroticism.
Roughly normal in the population; the best-replicated personality model.
Stable across adulthood (rank-order correlations ~0.6-0.7 over years,
Roberts & DelVecchio 2000).

- **Inter-correlations** are not zero: the "higher-order" pattern
  (Digman 1997) - A, C and low N correlate (~0.2-0.3, "stability"); E and O
  correlate (~0.2-0.3, "plasticity") (verify exact values per inventory).
- **Sex differences** (Costa et al 2001; Schmitt et al 2008): women higher
  in N (d ~ 0.4-0.5) and A (d ~ 0.3-0.5); small elsewhere; larger in
  wealthier, more gender-equal countries.
- **Age**: A and C rise, N falls from young adulthood ("maturity principle").
  Reality casts skew 20-30: higher N and E, lower C than the population mean.

What each does in a mind:

| Trait | Drives |
| --- | --- |
| E (extraversion) | social approach rate, talkativeness, reward sensitivity (BAS), attention-seeking, gossip volume |
| A (agreeableness) | warmth in interactions, forgiveness rate, conflict avoidance, cooperation in games |
| C (conscientiousness) | impulse control (resisting temptation), promise-keeping, planning horizon |
| N (neuroticism) | threat sensitivity (BIS), fear gain, rumination (memory rehearsal of negatives), jealousy, mood volatility |
| O (openness) | novelty-seeking, susceptibility to new partners and ideas, belief-update rate |

### HEXACO: honesty-humility

Ashton & Lee add **H (honesty-humility)**: sincerity, fairness, greed
avoidance, modesty. Low H is the best single predictor of cheating,
exploitation and lying in economic games (Hilbig & Zettler 2009). For a
show about love *or money*, H is the most important trait to draw.

### The dark traits

Narcissism, Machiavellianism, psychopathy (Paulhus & Williams 2002), plus
sadism ("dark tetrad"). Continuous traits; overlap is captured largely by
low H and low A. Clinical psychopathy is about 1% of the general
population (verify; higher in prisons). Narcissists are liked *at first*
and less over time (Paulhus 1998; Back et al 2010) - a perfect arc
generator: the charming arrival who sours.

### The interpersonal circumplex

Every social act has a position on two axes: **dominance** (agency) and
**warmth** (communion) (Leary 1957; Wiggins 1979). The **complementarity**
principle (Kiesler 1983): warmth invites warmth, hostility invites
hostility; dominance invites submission, submission invites dominance.
When complementarity fails (two dominants), friction follows. Cheap: each
act is a 2-vector, each agent has a preferred region and a reaction rule.

## 3.2 Motives, values, and the fear/hope axis

### Approach and avoidance

- **BIS/BAS** (Gray; Carver & White 1994): a behavioural activation system
  (reward, approach) and inhibition system (threat, avoidance), separate
  sensitivities. Maps to E (BAS) and N (BIS).
- **Regulatory focus** (Higgins 1997): *promotion* focus (gains, ideals,
  eagerness) vs *prevention* focus (losses, oughts, vigilance). This *is*
  the hope/fear vector: promotion-focused agents feel hope and dejection,
  prevention-focused feel fear and relief.
- **Hope theory** (Snyder 1991): hope = agency (I can) + pathways (there
  is a way). Low pathways makes despair.
- **Terror management** (Greenberg, Pyszczynski & Solomon 1986): reminders
  of mortality raise clinging to worldview and in-group. **(contested;
  large replication 2022 found weak effects)** - use lightly: elimination
  threat raising in-group loyalty is the safe version.

### What people want (the goal vector)

A small vector of goal weights, drawn per character, normalised:

| Goal | Evidence it is basic |
| --- | --- |
| **love / attachment** | Baumeister & Leary 1995 (need to belong); attachment theory |
| **sex / attraction** | sexual strategies theory (Buss & Schmitt 1993) |
| **money / resources** | instrumental for everything; the prize |
| **status** | Anderson, Hildreth & Howland 2015: desire for status is a fundamental motive |
| **safety** | not being eliminated; physical comfort |
| **belonging / friendship** | Baumeister & Leary 1995 |
| **autonomy / competence** | self-determination theory (Deci & Ryan 2000) |
| **novelty / fun** | sensation seeking (Zuckerman), O and E |
| **fairness / morality** | moral foundations (Haidt & Graham 2007): care, fairness, loyalty, authority, purity, liberty |

Schwartz's value circumplex (1992) gives the correlations: neighbours on
the circle go together, opposites conflict - self-enhancement (power,
achievement) vs self-transcendence (benevolence, universalism); openness to
change vs conservation. Draw values on the circle (a von Mises angle and a
strength) and read goal weights from it; conflict between goals then comes
for free.

### Social value orientation

How one weighs another's payoff against one's own (Van Lange 1999; Murphy
& Ackermann 2014). Rough population shares: **prosocial ~50-60%,
individualist ~25-35%, competitive ~10-15%** (verify by measure). A
continuous angle (Murphy's SVO slider) is better than three types:
`u_i = cos(theta) * own + sin(theta) * other`.

## 3.3 Risk, loss and time

- **Prospect theory** (Tversky & Kahneman 1992): value of gains `x^0.88`,
  losses `-lambda * (-x)^0.88` with **loss aversion lambda ~ 2.25**;
  probability weighting overweights small probabilities (gamma ~ 0.61 for
  gains, 0.69 for losses). Individual lambda varies widely (median ~2,
  spread across 1-4).
- **Path dependence on a real game show**: *Deal or No Deal* (Post, van den
  Assem, Baltussen & Thaler 2008): contestants became *more* risk-seeking
  after losses (break-even effect) and after big gains (house money). A
  contestant's risk attitude should depend on recent fortune.
- **Discounting**: hyperbolic, `V = A / (1 + k D)`; impulsive people have
  higher k (Ainslie; Kirby). Ties to C. A kiss now vs the prize later.
- **Ego depletion** (willpower as a fuel) **(failed - large multi-lab
  replication 2016)**: do not model self-control as a depletable pool; use
  fatigue and arousal instead.

## 3.4 Cooperation and trust

| Game | Typical behaviour | Source |
| --- | --- | --- |
| Trust game | senders give ~50% of endowment; receivers return ~37% of the tripled amount | Johnson & Mislin 2011 meta-analysis |
| Ultimatum | modal offers 40-50%; offers under ~20% rejected about half the time | Camerer 2003; Güth et al 1982 |
| Dictator | mean give ~28% (wide spread; many give 0, many 50%) | Engel 2011 meta-analysis |
| Public goods | start ~40-60% contribution, decay over rounds; punishment sustains it | Fehr & Gächter 2000, 2002 |
| Prisoner's dilemma (one shot) | ~40-50% cooperate | Sally 1995 meta-analysis (verify) |
| **Split or Steal** (*Golden Balls*, real money) | ~53% split; promises to split before choosing predicted splitting | van den Assem, van Dolder & Thaler 2012 |
| *Weakest Link* voting | evidence of strategic and taste-based voting | Levitt 2004; Antonovics et al 2009 |

Further regularities:

- **Conditional cooperation**: about half of people cooperate if others do
  (Fischbacher, Gächter & Fehr 2001); ~30% free-ride always.
- **Reciprocity** (Trivers 1971; Axelrod 1984): tit-for-tat with
  forgiveness is robust.
- **Indirect reciprocity by reputation** (Nowak & Sigmund 1998): help
  those who helped others. **Gossip** can replace observation: Sommerfeld
  et al (2007) - people act on gossip even when it conflicts with what they
  saw.
- **Altruistic punishment** (Fehr & Gächter 2002): people pay to punish
  defectors - the round table.

## 3.5 Attraction and love

- **Propinquity** and **mere exposure** (Festinger et al 1950; Zajonc 1968;
  Moreland & Beach 1992: a student seen in class more often was liked more).
- **Similarity** in attitudes predicts liking (Byrne 1971); in personality
  it predicts weakly (Montoya et al 2008).
- **Reciprocity of liking**: being liked raises liking (Kenny; Eastwick et
  al 2007 - but liking *everyone* signals nothing; selective liking counts).
- **Physical attractiveness** dominates first impressions in speed dating
  for both sexes (Walster et al 1966; Eastwick & Finkel 2008). **Stated
  preferences barely predict actual choices** (Eastwick & Finkel 2008).
- **Sex differences in preferences** (Buss 1989, 37 cultures): both rank
  kindness and intelligence first; men weigh looks more, women resources
  and ambition more - moderate effects, smaller in real choices.
- **Mate-choice copying**: people rate someone more attractive when they
  see them chosen by others (Waynforth 2007; Place et al 2010) - mimetic
  desire with evidence.
- **Misattribution of arousal** (Dutton & Aron 1974, the bridge study;
  replication mixed **(contested)**): arousal from fear or exercise raises
  attraction to whoever is there.
- **Self-disclosure ladder**: the 36 questions (Aron et al 1997) - mutual,
  escalating disclosure builds closeness in 45 minutes. Social penetration
  (Altman & Taylor 1973): intimacy grows by breadth then depth.
- **Love's components**: Sternberg (1986) intimacy, passion, commitment;
  Fisher (1998) lust, romantic attraction, attachment as separate systems -
  which can point at three different people at once. That is a love
  triangle in one head.

### Attachment styles

Hazan & Shaver (1987), adult romantic attachment: **~56% secure, ~25%
avoidant, ~19% anxious** (verify). Better as two continuous dimensions
(Brennan et al 1998): **anxiety** (fear of abandonment) and **avoidance**
(discomfort with closeness). Anxious agents seek reassurance, read
ambiguity as threat, cling and protest; avoidant agents withdraw when
closeness rises. The **anxious-avoidant pair** is the classic
pursue-withdraw loop - a schismogenesis (see [05-cybernetics.md](05-cybernetics.md)).

### Relationship stability

- **Gottman**: stable couples keep a **~5:1** ratio of positive to negative
  interactions in conflict; the "four horsemen" - criticism, contempt,
  defensiveness, stonewalling - predict breakup, contempt the most.
- **Investment model** (Rusbult 1980; Le & Agnew 2003 meta):
  **commitment = satisfaction - quality of alternatives + investment**, and
  commitment predicts staying. Satisfaction is the main term; alternatives
  are what a bombshell changes.

### Infidelity

- Lifetime extramarital sex in US surveys (GSS): **~20% of men, ~13% of
  women** married (verify by year; the gap is narrowing).
- Predictors: low commitment, attractive alternatives available, opportunity
  (privacy, travel), permissive attitudes, low C and A, avoidant
  attachment, dark traits.
- **Jealousy**: Buss et al (1992) found men more upset by sexual, women by
  emotional infidelity **(contested - effect depends on method)**. Safe to
  model jealousy as threat to a valued bond, with N and attachment anxiety
  as gain.

## 3.6 Social structure, gossip and conformity

- **Dunbar's layers**: ~5 intimates, ~15 close, ~50 friends, ~150 known.
  A house of 10-16 is one "close" layer - everyone matters.
- **Gossip share**: roughly two-thirds of conversation is about social
  topics (Dunbar 1997 (verify)); most gossip is not negative but negative
  gossip spreads further.
- **Rumour's basic law** (Allport & Postman 1947): spread rate ~
  **importance x ambiguity**. Retold, rumours are **levelled** (shortened,
  details dropped), **sharpened** (a few details exaggerated) and
  **assimilated** (bent toward the teller's expectations).
- **Deception detection**: people judge lies at **~54% accuracy**, barely
  over chance (Bond & DePaulo 2006), with a truth bias.
- **Illusory truth**: repetition raises believed truth (Hasher et al 1977;
  holds even for known falsehoods, Fazio et al 2015).
- **Conformity** (Asch 1956): ~37% of answers conformed to a wrong
  majority; ~75% conformed at least once; one dissenter breaks it.
- **Obedience** (Milgram 1963) and the **Stanford Prison Experiment**
  (Zimbardo 1971): famous, but the SPE is now discredited as an experiment
  (guards were coached, Le Texier 2019) **(failed)**; do not use as a prior.
- **In-group bias** from trivial groups (Tajfel 1971, minimal groups):
  teams form identities in hours. **Superordinate goals** dissolve rival
  groups (Sherif's Robbers Cave, 1954 - methods since criticised).
- **Status: dominance vs prestige** (Henrich & Gil-White 2001): two routes
  to rank - fear (dominance) and freely given deference to skill
  (prestige). A house will grow both kinds of leader.
- **Interaction rituals** (Collins 2004): co-presence, a shared focus and
  a shared mood, rhythmically entrained, produce *emotional energy* and
  solidarity symbols - the engine of group rituals.

## 3.7 Emotion

- **Appraisal** (Lazarus 1991; Scherer 2001): emotions are evaluations of
  events against goals - relevance, congruence, agency, coping potential,
  norm compatibility.
- **OCC** (Ortony, Clore & Collins 1988): 22 emotion types derived from
  three appraisals - consequences of events (for goals), actions of agents
  (against standards), aspects of objects (against tastes). **Hope and fear
  are its prospect-based emotions**: hope = a desirable event possible but
  uncertain, fear = an undesirable one; then satisfaction/disappointment and
  relief/fears-confirmed when it resolves. This is the computational core
  of the "fear and hope vectors" asked for (see [04-models.md](04-models.md)).
- **Dimensions**: valence and arousal (Russell 1980); PAD adds dominance
  (Mehrabian 1996). Mood = slow average of emotion.
- **Negativity bias** (Baumeister et al 2001): bad is stronger than good,
  roughly by 2-5x (cf. loss aversion, Gottman's 5:1).
- **Emotional contagion** (Hatfield et al 1993): moods spread through a
  group by mimicry.

## 3.8 Drawing a cast: summary of parameters

| Parameter | Distribution |
| --- | --- |
| O C E A N, H | multivariate normal (z), correlations ~0.2-0.3 as above, sex shifts on N and A |
| dominance, warmth (circumplex) | derived: dominance ~ E - A/2, warmth ~ A + E/2 (approx) |
| attachment anxiety, avoidance | normal; anxiety correlates with N (~0.3-0.4) |
| SVO angle | mixture: prosocial ~ N(40°, 8°) 55%, individualist ~ N(5°, 8°) 30%, competitive ~ N(-15°, 8°) 15% (verify centres) |
| loss aversion lambda | lognormal, median 2, sd of log ~0.4 |
| discount k | lognormal; higher for low C |
| goal weights | Dirichlet, concentration shaped by Schwartz angle |
| moral foundations (6) | normal; liberty/care/fairness vs loyalty/authority/purity cluster |
| attractiveness | normal, independent of personality (a design choice: Love Is Blind) |
| impulsivity | from low C, high E, alcohol state |
| gossip propensity | from E, low H |
| lie propensity | from low H, Machiavellianism |
