# A network is a state

`sg/net` is derived numerical machinery above core, linked as `stategine::net`.
The network remains an ordinary state. Its publication, input, reconciliation
drive, outgoing events, corrections and membership edits use the existing
graph. State, Functor, Engine, Cover and the algebra are unchanged.

`tests/dsl/network.sg` is the two-game construction. The reconciliation arrow's
native adapter owns a disposable `Reconcile` cache, gathers its own state,
evaluates on either backend, writes its own results and says they are ready.
Functor transports carry them back. Zero elapsed time is the identity. No
backend writes a state, owns a clock or warm-starts from an earlier solution.

## State data and the derived layout

| Element kind | Params | Meaning |
| --- | --- | --- |
| `participant` | `dim` (default 1), `observation`, `weight` (default 1) | Stalk dimension, observed coordinates and diagonal confidence M |
| `participant` | `pinned` (default false), `pin` | Fixed coordinates; an omitted pin value uses that coordinate's observation |
| `constraint` | `left`, `right`, `dim` (default 1), `weight` (default 1) | Endpoint ids, comparison dimension and diagonal overlap weight W |
| `constraint` | `left_scale`, `right_scale` (default 1), `left_r_c`, `right_r_c` | Rectangular restrictions from each stalk to the comparison space |

Scalar coordinate params keep their original names. For vector stalks or
comparison spaces use suffixes: `observation_0`, `observation_1`, `weight_0`,
`pinned_0`, `pin_0`, and so on. A scalar `weight` or `pinned` supplies the default
for every coordinate. Each restriction entry defaults to its side's scale on
the rectangular diagonal and zero elsewhere; explicit `left_0_1`, for example,
overrides one entry. Result readings carry the element id, coordinate, dimension,
value and correction, for the owning arrow to store as `result[_i]` and
`correction[_i]` through the same naming rule.

Dimensions are positive integers. Numeric data must be finite. Confidence and
overlap weights may be zero but cannot be negative. Freshness, if wanted, is
computed by the state's own arrows on Temporal and supplied as confidence;
the solver keeps no aging clock or authority flag. Hard pins are explicit
boundary conditions, not very large confidence weights.

`Cellular::update` derives the stalks, overlaps and restriction blocks in stable
element-id order. It recompiles CSR rows of delta and CSC columns of its
transpose only when ids, dimensions, endpoints or restrictions change.
Observation, confidence, overlap-weight, pin and solver-control changes leave
that layout intact. `compilations()` is an execution diagnostic, never state
data or an input to the solution.

`LinearSystem.hpp` contains only flat numeric buffers and a reference to that
derived layout: offsets, signed restriction coefficients, sparse indices,
observations, weights and pins. The layout view remains valid until the owning
`Cellular` compiles another topology. `Cellular::gather` always reads current
state data, including restored data; no cached value is another source of truth.
Join and leave change elements and their interfaces through `graph.edit`.
The numerical test binds DSL-authored element declarations to their existing
owner inside an edit; dynamic removal uses the graph's existing rewrite API.

## Objective, residuals and bounded execution

For each overlap, `delta(x) = right_restriction*x[right] - left_restriction*x[left]`.
Both backends apply `delta`, W, and the transpose directly. They never form
the full Laplacian. The objective is

```text
1/2 (x-y)* M (x-y) + lambda/2 delta(x)* W delta(x)
A = M + lambda delta* W delta
```

State params select `lambda` (default 1, nonnegative), `iterations` (default 128,
0 through 65536), and `relaxation` (default 0.9, in (0,1]). The reference method
performs exactly that many steps `x -= alpha*(A*x - M*y)`. Alpha is relaxation
divided by a matrix-free absolute-row-sum upper bound on A. Fixed variables
start at their boundary values and are excluded from every update; their
contributions still reach neighboring free variables through delta.

Every solve starts from the supplied observations and boundaries. Identical
inputs produce identical results regardless of previous solves. Positive
confidence makes the free system positive definite. Zero confidence can leave
a kernel; deterministic initialization chooses its component from y. This
supports pure compatibility smoothing, including the tested three-stalk cycle.
All-zero operators preserve the supplied free observations.

Finite lambda and conflicting positive-confidence observations generally give
a reconciled section with nonzero overlap disagreement. Exact compatibility
is not promised by a finite penalty. `residual` is `sqrt(delta(x)* W delta(x))`;
`equation_residual` is the maximum free-coordinate `|A*x - M*y|`. The latter
reports convergence to the requested objective, not exact compatibility.
Fixed iteration count bounds work, not convergence for every condition number.
Consumers can inspect both residuals; no hidden stopping threshold changes cost.

`Backend.hpp` declares `solve`, `residual` and `available`, all over numerical
buffers. `CpuBackend` is the reference. `CudaBackend` uses the same double
precision method, row order, starting values and iteration count. One thread
owns each sparse row or column, avoiding nondeterministic atomic reductions.
Unsupported builds or devices report unavailable; no CPU fallback is reported
as a successful GPU test. Residual diagnostics use the downloaded numerical
section and the reference matrix-free operators.

CUDA keeps restrictions, indices, coordinate workspaces and inputs resident.
It compares incoming data with its disposable upload cache, transfers only
changed observations/weights/pins, and compacts changed solutions on the device.
Only the changed coordinate indices and values are downloaded, plus their count.
An unchanged solve downloads no solved values. Topology changes refresh the
resident layout. `transfers()` reports numerical execution counters only.
The initial changed-value compactor is a deterministic O(n) device scan; a
parallel scan or another solver can replace it behind the same backend.

## Neighboring execution partitions

`Peer.hpp` contains identity and connection metadata only. A participant's
`solver` string selects the peer computing that stalk; an omitted solver uses
the local `peer`. This is execution placement, not ownership of reality or
authority over another game. `Partition` derives owned stalks, internal edges
and touching boundary edges from the same declared cellular topology. It
uses `Cellular::derive` for topology metadata without compiling a global
coboundary, then compiles only that numerical neighborhood, reusing it until the selection or
restrictions change. Remote observations are not read or required.

For a crossing edge the outgoing value is the local restriction projection
`rho_local*x_local` in the edge's comparison space. The local numerical layout
holds the received projection as a fixed boundary variable with an identity
restriction. Neither remote interior coordinates nor whole-world state are
sent. Each peer needs the declared topology and restriction metadata; this
pass does not discover topology over the transport.

`Distributed::evaluate` and `receive` read a const state and return derived
readings, parameter changes and packets. Only their DSL-declared native arrows
apply those changes to their own network state. Observers serialize said
packets; an external receive loop queues `Engine::send("network", event)` at
the declared receive port. No backend, socket or observer writes a world.

Each world step has an integer `epoch`; `generation` identifies its execution
assignment/topology. Packet `tick` is the sender's local progress sequence,
while each projection's `basis` identifies its last changed value. They are
distinct: an unchanged value can have an old basis and a new sender sequence.
`round` stores local progress in ordinary params, advanced only by the
Temporal-driven arrow. Neither value is a wall clock.

The synchronous fallback sends numeric values for changed projections. An unchanged projection sends
a progress record naming the tick of its last changed value (`basis`); it
still participates in the round boundary. A progress record arriving before
its basis waits for that basis. Receive and send records live in the owning
constraint's ordinary `remote_*`, `next_*` and `sent_*` params. Restoring the
state restores protocol progress too; the numerical classes keep no packet
history, clocks or hidden solution trajectory. It retains matching rounds and
two receive slots, so zero-confidence/kernel fixtures keep their original
initial component and arithmetic.

Set `async = true` and an integer `max_staleness = B` (1 through 65536) through
the declared configure port to request bounded asynchronous relaxation.
The default without a staleness declaration remains synchronous. Async uses
the latest accepted touching-neighbor projection, immediately eligible on the
next Temporal solve; a missing boundary or `round - remote_tick > B` stalls
only that execution neighborhood. A faster neighbor has age zero. A smaller
basis cannot overwrite a larger one; a repeated basis is idempotent, and a
conflicting payload for the same basis is refused. Old sender progress cannot
replace newer progress either. Epoch/generation mismatches are ignored.
Each async packet carries a self-contained value, including unchanged values,
so losing a basis packet does not make its successors depend on retransmitting
an implicit stale value. Transport may coalesce unsent projections by overlap.

The initial proof certificate is deliberately sufficient rather than complete.
For the fixed free operator A, derive diagonal `d_i` and a conservative absolute
off-diagonal row bound `o_i` from published restrictions, M, W and pins. Async
requires `d_i > o_i` for **every** free coordinate, with a roundoff margin.
For the locally owned rows let `R = max(d_i + o_i)`; require
`0 < diffusion_step <= 1 / ((B+1)*R)`, as well as the existing backend and
neighborhood bounds. An excessive step fails closed. No Laplacian is formed.
With this certificate `I - alpha*A` is a strict infinity-norm contraction;
bounded stale reads, fair repeated updates and eventual boundary delivery
converge to the unique minimizer of the original objective. Coercivity alone
does not prove this particular asynchronous iteration: a system failing the
certificate, including unanchored kernels, retains the synchronous algorithm.
Disconnected regions can progress independently. Inputs/weights/pins and
restrictions must stay fixed during an epoch. A partition that groups unrelated
components shares its own local progress; use connected placement regions.

With a crossing boundary the local backend takes one bounded gradient step:

```text
x_I[r+1] = x_I[r] - alpha * (M_I*(x_I[r]-y_I) + lambda*(delta* W delta x[r])_I)
```

Synchronous peers use the same positive `diffusion_step` (default 0.05). Each validates
it against its original neighborhood bound and local numerical backend bound.
Choose a step safe for every partition; excessive steps are refused. Because
rounds match, this is the global gradient evaluated by neighborhoods. It also
preserves the initial kernel component in zero-confidence problems, unlike
independent local minimization with stale neighbors. Confidence, pins,
overlap weights, lambda and original observations remain fixed during an
epoch; changing the problem starts a new epoch through declared arrows/ports.

The existing CPU and CUDA implementations are unchanged. To let them start at
the explicit current `result` while retaining the original `M*y`, the derived
layout represents confidence as additional numerical restrictions to fixed y.
These rows and variables are flat numerical buffers, not new state elements.
A neighborhood without crossing edges uses the original backend solve
directly, including its original iteration count. One peer therefore produces
exactly the previous single-machine result. Multiple peers approach the same
objective within numerical tolerance; finite round counts do not guarantee
convergence for every condition number. Local residuals refer to that peer's
neighborhood and the received round, not a hidden global convergence test.

Split and merge change solver assignments and the derived partition layout.
`examples/distributed.sg` requests them through `graph.edit`; its edit changes
only execution params, increments generation and restarts protocol rounds.
At the cut, moving work must receive its existing observation/confidence/pin
data and current section through declared ports before computation resumes.
The example accepts `handoff_<stalk>` values in the repartition request for
its scalar fixture. This transfer is limited to moving work and is separate
from ordinary boundary traffic. It does not reset game states or republish
corrected game values as new observations. Peers must agree on the new
epoch/partition definition. The signed regional acceptance layer below binds
this execution definition and verifies its handoff; the unsigned diffusion
fixture itself remains a numerical/transport reference.

`sg_net_peer` runs independent copies of the DSL world over an external UDP
transport on Windows or POSIX. Its arguments are rank, peer count, round count,
output path, optional `cpu|cuda`, base port and comma-separated IPv4 hostnames.
For example, on four hosts using the same host list and port range:

```sh
build/sg_net_peer 0 4 400 build/out/network/p0.txt cuda 49000 host0,host1,host2,host3
# Other hosts use ranks 1, 2 and 3 with their own output paths.
```

The demo assigns contiguous regions of its six-stalk chain to equal peers.
There is no server. Its transport retries the last two said packets per
neighbor; wall time paces socket delivery only. UDP datagrams are limited to
64 KB in this example. Larger boundaries need an external framing/reliable
transport. A lost connection stalls touching work; recovery, automatic world
step negotiation remain application responsibilities. The optional Internet
adapter and pure placement planner below replace those execution limitations
without changing this unsigned UDP reference fixture.

## Derived placement and declared interest

`Placement::plan` reads a `FinalizedPlacementView`, the current Cellular
topology, available peer capacities, measured stalk execution/migration costs,
boundary traffic costs and current `solver` assignments. It returns sorted
assignments, generation, loads, cut cost, migrations and optional interest
proposals. It cannot mutate a State. The view names a verified receipt and
finalized epoch/generation; the application must bind the read-only snapshot to
that exact checkpoint, rather than prediction. A nonzero digest alone cannot
prove that binding to this generic numerical planner.

The deterministic connected greedy heuristic minimizes approximately maximum
load/capacity plus `cut_penalty * cut_cost + migration_penalty * migration_cost`.
Stable peer/stalk/overlap IDs break ties. Connected moves and adjacent-region
merges improve the candidate without crossing declared capacity limits.
`hold_epochs` and `minimum_improvement` retain a valid current assignment near
a threshold; unavailable/overloaded peers require immediate feasible repair.
History is supplied as the last placement's finalized epoch, never remembered
in a planner clock. An infeasible greedy result is explicit; this small
heuristic is not a guarantee to find every feasible or optimal partition.

A pure application `InterestPolicy` sees read-only published participant params
and the actual Cellular overlaps. It returns add/remove/keep proposals with
ordinary constraint params and traffic costs; sg/net has no spatial-distance
rule and no invisible interest graph. Placement uses the existing graph in
this pass; proposed edges affect a subsequent plan only after being declared.
`examples::placement_arguments` authors expressible additions/updates as DSL,
and the existing `network.repartition` arrow requests the existing graph edit.
That edit applies/removes actual `constraint` elements, changes assignment and
increments generation. Cellular and Partition then re-derive their layouts.
Planning and requesting an edit alone create no overlap.

Applications verify the existing `Agreement::Handoff` and checkpoint before
applying migrations or replacing a committee, and feed moving values through
the existing handoff params/ports. The unsigned scalar fixture remains an edit
adapter, not a substitute for signed handoff verification. Hot splits, cold
merges, peer replacement and interest changes are execution/declared structural
work; no game-level server-transfer event is added.

## Opaque external transport

`Transport::send(Outbound)` takes explicit peer, channel, delivery class,
supersession slot and opaque bytes. `Inbound` supplies peer, channel and bytes.
Routing never decodes an Exchange or signed protocol payload. `Latest` is
unordered and permits loss/supersession; same peer/channel/slot replaces the
older **unsent** value. `Reliable` preserves ordered control messages within
its logical channel. Bounded queues report `Blocked`, `TooLarge` or
`Unsupported` explicitly. A caller keeps blocked work outside the world and
retries; `Accepted` is queue admission, not proof of peer application receipt.
Separate budgets prevent boundary traffic consuming the control queue.

`examples::Udp` is the small IPv4 reference adapter: opaque datagrams, bounded
latest-value retry slots, explicit routes and a datagram-size limit. It reports
`Reliable` as unsupported. Existing UDP signed demos deliberately repeat a
bounded application frontier; they do not claim reliable Internet sessions.
Socket retries and latency measurements are external wall-time machinery,
never simulation progress or a reason to change finalized values.

Build `-DSG_NET_ICE=ON` for optional `stategine::net_ice` / `IceTransport`.
Its public header contains no vendor types. Pinned upstream libdatachannel
provides ICE, DTLS encryption and SCTP framing/reliability, with STUN/TURN
server URIs and `relay_only` to advertise only relay candidates. ICE can still
discover a peer-reflexive shortcut on a shared LAN; this option is not a privacy
guarantee that every subsequent packet crosses TURN. Each peer has separate
control and latest-value associations, avoiding agreement head-of-line blocking
behind large boundary messages. Logical control channels are reliable/ordered;
latest channels are unordered with zero retransmissions. Defaults bound
messages to 4 MB, the application receive queue to 16 MB/512 messages and
logical streams to 64. SCTP socket buffers and libdatachannel's bounded
per-channel receive queues are additional buffers, not part of that 16 MB
application limit. The fetched target bounds each vendor data channel to four
messages (16 MB per physical lane at the default maximum message size), plus
SCTP's socket buffers. Installed packages retain their vendor queue limits;
deployment must account for those separately.
The fetched TLS library enables upstream thread safety for RTC workers.

The application routes opaque `signals()` through an **authenticated external
rendezvous/signaling service**, and supplies the authenticated sender to
`signal(peer, signal)`. Authentication protects exchanged DTLS fingerprints;
application signatures still establish observation/committee identity and
agreement. Neither a rendezvous nor TURN relay becomes a world participant.
Public transport methods are serialized on the application's IO thread;
vendor callbacks fill bounded external queues only. `poll` restarts failed
sessions; `disconnect` pauses and `reconnect` explicitly resumes them. Stale
session signals cannot reset a newer session. Unsent reliable work survives
replacement; already handed-off traffic can be uncertain after an association
break, reported as `DeliveryUncertain` (and receive overflow as `ReceiveBlocked`).
The application must replay its idempotent signed frontier/checkpoint on that
notice. This does not promise exactly-once application delivery across a new
connection and does not implement a second retransmission protocol.

`telemetry` reports RTT, bytes, queue depth and selected direct/relay path only
as execution metrics. It may inform Placement; it cannot update world data.
Offline or package-managed builds may set `SG_NET_ICE_FETCH=OFF` and supply
LibDataChannel 0.24 or newer, or point FetchContent at the pinned sources.
Internet traversal depends on reachable signaling/STUN/TURN services and local
firewall policy; these are supplied by the application, never discovered as
world truth. No public service or secret credential is hard-coded.

The fetched target uses pinned libdatachannel 0.24.6 and Mbed TLS 3.6.7, with
small checked patches for callback replacement, raw SCTP callback retirement,
concurrent SCTP initialization and an explicit standard-library include.
It also bounds vendor receive queues and wakes them on close; no wire protocol
or world behavior is implemented in these patches.
Windows MinGW GCC 16/UCRT is **refused for this optional target**: repeated
release and debug teardown tests reproduce a queued task using an already
freed SCTP socket. Disabling optimization, static linking and new standard
library fast paths did not eliminate it. Native Clang/libc++ and Linux builds
run the same fixtures. This is an unresolved vendor/toolchain compatibility
limitation, not a claim that the upstream lifetime defect has been repaired.
The ordinary CPU/CUDA/UDP targets continue to build with GCC 16. Fetched Mbed
TLS uses pthreads; MSVC builds must supply a native LibDataChannel package
with `SG_NET_ICE_FETCH=OFF`.

The examples select ICE with `--signaling wss://...`. `NetIo` signs each SDP,
candidate and restart signal with the application's pinned committee key;
the rendezvous broadcasts opaque bounded signaling frames. TLS certificate
verification stays enabled. `--ca-file` supplies a private deployment CA,
repeated `--ice-server` options supply STUN/TURN URIs, and `--relay-only`
restricts advertised candidates. Loopback `ws://` is allowed for local tests;
Internet signaling requires WSS. The example retains a bounded idempotent
protocol frontier for explicit delivery-uncertainty recovery. Full process
replacement still requires the existing verified handoff/checkpoint.

```sh
# Install Python websockets to run this example-only rendezvous.
python examples/ice_rendezvous.py --host 0.0.0.0 --cert tls.pem --key tls.key
build/sg_net_pong --player 0 --credentials session --backend cuda --signaling wss://rendezvous.example:49280 --ice-server stun:stun.example:3478 --ice-server turn:user:password@turn.example:3478
```

Supply your own reachable endpoints and credentials. The fetched libjuice
backend supports TURN over UDP; TURN TCP/TLS-only or UDP-blocked networks
need a suitable installed mature backend/package or route. No rendezvous or
relay deployment is created by the engine.

## Signed regional epochs and independently verified agreement

`Epoch.hpp`, `Integrity.hpp`, `Verify.hpp`, `Agreement.hpp` and `Protocol.hpp`
are execution and evidence machinery outside the world. They own no semantic
objects and have no world-write capability. A Temporal-driven arrow says it
requests reconciliation. An external executor builds the derived regional
problem, receives signed observations and proposes results. Only a verified
quorum finalization is delivered through `Engine::send` at the declared port;
the owning network arrow stores results and its declared functors carry them.
Receiving a proposal, vote, forgery or duplicate never updates NetworkState.
Local output requests use the declared identity `network.dispatch` arrow,
which says `network.outgoing` without storing the proposal/evidence in state
data. A strict external observer serializes that said packet; retransmission
reuses it outside the world. Remote messages stay in Protocol until verified
finalization. There is no observer callback into the world.

`EpochContext` binds world, partition, tick, execution generation, topology,
constraints, solver specification, pinned committee and finalized predecessor.
`Epoch` adds the canonical input-set hash. `Problem` owns disposable flat
numerical buffers plus signed observations and opaque declared rule/topology
descriptions. `topology_description(Cellular)` includes stable stalk and
overlap names, endpoints, dimensions and restrictions; include it when making
the problem so renamed relations cannot alias the same numeric matrix.
Neighborhood applications provide the corresponding boundary/coordinate
mapping description. Constraints bind M, W, exact pins, lambda and additional
declared game constraints. Input hashes bind ordered observation statements
and the numerical y gathered from them. Builder code must derive y from those
authenticated inputs and validate them against the declared game rules.

Observations contain peer, context hash, tick, sequence, object, parameter,
payload and signature. Discrete observations additionally identify an
exclusive conflict key. Accepted input order is
`(tick, peer, sequence, object, parameter)`. The input manifest states exactly
which producers/slots are required; missing data is never invented. A
signature proves the supplier's identity, not the truth of its claim.
`Integrity` detects signed changes at the same peer/context/sequence and keeps
both statements as evidence. Duplicates are idempotent. Finalized ticks reject
replay; up to 64 recent ticks of bounded delivery records also allow detecting
late equivocation without reopening the world. Export/persist the evidence
if it must outlive the executor. Old records may be evicted at capacity.

Hashing uses BLAKE2b-256, signing uses standard Ed25519, implemented by pinned
[Monocypher 4.0.3](https://monocypher.org/download/) and its
[Ed25519 module](https://monocypher.org/manual/ed25519).
The upstream archive's SHA-512 is verified by CMake. Seed generation uses the
OS random source; private keys stay outside State and are wiped on destruction.
Encoding is versioned, domain separated, length prefixed and big endian;
nonfinite numbers, negative zero, excessive fields and truncated wire data are
refused. Signatures authenticate statements but are not hashed as unique
input identities. The public committee manifest pins distinct peer keys;
small-order identity keys are rejected using the upstream verification equation.

Every executor solves with its selected CPU/GPU backend and redundantly runs
the bounded CPU reference. It checks backend/reference error, weighted overlap
disagreement, free equation residual and exact pins. SolverSpec binds method,
rules version, fixed iteration count, lambda/relaxation, quantization spacing
and all tolerances. Finite penalty solutions may have legitimate nonzero
disagreement: choose the epoch's tolerance for its actual objective; failing a
tolerance refuses finalization rather than silently increasing work.

GPU buffers are never hashed. Accepted cells use rounded integer coordinates
`round(reference_x / quantum)` with a bounded range, canonical tie handling
and exact off-grid hard pins. Recomputing the reference acceptance cells
avoids GPU perturbations on opposite sides of a quantization threshold.
Result certificates sign epoch, input, topology, constraint, solver, canonical
result and checkpoint hashes, residual diagnostics and signer identity. Every
recipient independently recomputes the reference section and the application's
deterministic checkpoint. Residual diagnostics are compared with the declared
tolerances. The final receipt binds the epoch/result/checkpoint, independently
of proposer, GPU intermediate values or which sufficient quorum subset arrived.
If an application's CPU arithmetic differs enough to change acceptance cells
or an exact checkpoint, it fails closed. Exact cross-platform simulations need
a specified deterministic arithmetic/rules implementation (for example integer
or fixed point); tolerance is not permission to average discrete game events.

`Agreement` counts distinct authenticated members only. For n members, quorum
q and declared faulty-member budget f, configuration requires
`2q > n + f` and `q <= n - f`. An honest member locks one verified problem and
decision per regional world step; it cannot sign a second input set even if
the result happens to be equal. Matching attestations finalize that exact
problem. `Agreement::Persist` must durably store the snapshot before a vote is
exposed and before accepting a finalized successor. `restore` requires an
independently verified predecessor and restores pending vote locks. The demos
use exclusive identity leases and two checksummed, alternating flushed journal
slots in a held file. A torn write leaves the preceding slot available; no
filename replacement occurs during finalization. A failed flush leaves the
live predecessor unchanged and exposes an execution error. They refuse
silently restarting a used identity at tick zero; their launchers create fresh
sessions. General crash recovery must restore the declared world through its
port/checkpoint and these execution locks together.

Numerical relaxations need no quorum. Quorum finalizes regional world steps,
not diffusion substeps. The predecessor chain remains strict: an executor may
compute a speculative successor, but may not attest it before knowing the
exact finalized predecessor. Independent regions and external input/proposal/
attestation transport can run concurrently; only finalized candidates cross
the declared world port.

Verification is regional: create a Protocol over only the affected region's
declared input slots and comparison spaces, not the whole world. A neighboring
finalization must be independently verified against that neighbor's committee,
problem and checkpoint before its boundary projection becomes an input to the
next region. No global agreement service or global ledger is required.
Inter-region routing and discovery remain application/external transport work.

Handoff contains the exact finalized proof/checkpoint, next committee and old
quorum endorsements. `Agreement::endorse` independently checks that checkpoint,
locks the replacement and seals the old generation before signing. It cannot
endorse a second roster or continue voting in the old generation. New members
use `Agreement::resume` to verify the handoff and retain the exact world id,
finalized predecessor and next tick with generation + 1. Raw recovery also
requires the independently verified world id and predecessor; the snapshot
must match both. Replacing every machine changes execution metadata,
not world meaning. Structural world membership still requires `graph.edit`.

This is a configured regional acceptance rule, not unrestricted adversarial
consensus. Key membership and fault bounds are explicit assumptions; distinct
keys do not prove physical independence. Losing a required input producer can
stall even when enough executors remain. Divergent canonical input sets refuse
finalization; automatic view changes/input-set recovery, committee discovery,
and committee recovery are not implemented by Agreement. NAT traversal and
larger-message framing belong to the optional external ICE adapter. No executor's
proposed result is intrinsically authoritative.

## Prediction and latency compensation

`Prediction.hpp` stores only disposable numeric forecasts, discrete pending
inputs and bounded history. Supply the game's existing rule computation, a
maximum lead, history bound and continuity mask. `advance` forecasts local
input immediately; `amend` replaces late authenticated pending input and
replays; `reconcile` anchors a finalized step and replays remaining inputs.
No prediction helper modifies State. Rendering may call `display(time)` with
the owning Temporal timeline to ease continuous corrections. Discrete scores,
inventory changes and finalized outcomes are excluded from smoothing.

`sample` exposes read-only forecast history; `finalized_sample` returns only
confirmed historical frames for latency queries. Skipped speculative frames
never become confirmed history. A compensated action still needs deterministic
game validation and ordered epoch agreement; a historical query is not a
license to rewind or mutate the world. Prediction has no private clock and
stops advancing at its configured lead when finalization stalls. It hides
bounded latency; it cannot eliminate quorum latency or guarantee 60 finalized
steps per second across a high-latency network. Batching/pipelining is later
execution work behind these boundaries.

## Building and checking

The independent local Pong world is `examples/pong/pong.sg`. Each process
builds the same game, network and Temporal states. Each player signs an exact
ternary paddle command and its published boundary positions. Discrete commands
are deterministically validated and ordered, never sheaf-averaged. Positions
form two 2D stalks with identity restrictions and are checked against the last
finalized game checkpoint. The original CPU/CUDA backend reconciles this local
continuous problem. Both players independently execute the next game rules,
verify the numerical result and checkpoint, and require both attestations.

The correction functor carries each computed local section into its game.
Pong's own Temporal-driven arrow then says it is ready for one motion
interval. The declared self functor routes that event to affine spatial
motion and the game's bounce/score arrow. Each world advances independently
from identical discrete commands and fixed drive intervals. The next
publication begins a new protocol epoch. Ball motion and bounce/scoring rules
retain their original meaning. Forecasts execute the same DSL affine bodies
and the same bounce helper on disposable numeric values. Lead is bounded to
12 steps; late input/finalization replays pending steps, and continuous visual
error eases over 100 ms using Temporal time. Scores are drawn from finalized
game State. A missing player stalls this two-input test world; reconnect and
mid-game joining are outside this example. Two players provide no malicious
member fault tolerance (n=2, q=2, f=0); neither can finalize alone.

On Windows, `examples/pong/run.ps1` opens both views side by side. W/S moves
left, Up/Down moves right, Esc closes the focused window. Both programs read
their own physical keys when either Pong view is foreground. Elsewhere,
GLFW input requires focusing the view for that paddle. Select `-Backend cpu`
or `-Backend cuda`; the default chooses CUDA if available. `-DelayMs 60`
delays incoming datagrams to demonstrate prediction/correction. The launcher
generates fresh per-player seeds and a pinned public committee under
`<build>/out/pong/play`. To run manually:

```sh
build/sg_net_keys build/out/pong/session 2 2 0 pong
build/sg_net_pong --player 0 --credentials build/out/pong/session --backend cpu --port 49270 --hosts host0,host1
# On the second machine, use --player 1 and the same public manifest/host list.
```

Copy `committee.txt` and only that machine's own `p0.seed` or `p1.seed` to its
credential folder over a trusted channel. Never distribute all private seeds
to other participants. Allow reachable UDP ports 49270/49271 on the respective
hosts; NAT/firewall forwarding or a relay is external transport setup. This
is the reference IPv4 UDP path. The optional authenticated ICE/WSS path above
adds Internet traversal without changing this world or its protocol. The same-PC
launcher defaults to loopback and generates both local identities.
It also runs without a display using
`--headless --steps 600 --out <build>/out/pong/p0.json` (choose each peer's
own output path). Only this region's signed boundary positions/commands,
proposals and votes are transmitted; no ball/world snapshot is sent by default.
Each verifier derives its checkpoint from its own declared publication. The
`sg_net_pong` CTest compares both local worlds after 600 steps on CPU, CUDA
and mixed backends, with delayed and duplicated delivery and bounded prediction.
Final world values and receipt chains agree within each session; transport
timing does not change the game. Explicit CUDA
selection reports an unavailable device as a skip.

The CPU backend always builds. `SG_NET_CUDA=ON` (default) adds CUDA when CMake
finds the toolkit's NVRTC and driver libraries; `-DSG_NET_CUDA=OFF` gives a
CPU-only build. A build-time helper compiles `src/gpu/network_cuda.cu` to PTX
using NVRTC and embeds it in the library. The host uses the C CUDA driver API
and the project's own compiler, so MinGW does not need an MSVC-compatible C++
ABI. PTX compilation is not a frame operation. The initial module targets
compute capability 6.0 or later. Other GPU implementations, PCG and multigrid
are not part of this pass. Cryptographic source is fetched and checksum-pinned
by `cmake/network_integrity.cmake`; offline builds may set
`FETCHCONTENT_SOURCE_DIR_MONOCYPHER` to that verified upstream source.

`sg_net` runs the same DSL graph and reconciliation arrow on CPU and available
CUDA. `sg_net_numerics` checks analytic solutions, zero residual on agreement,
disagreement reduction, a compatible cycle, scalar and coordinate pins,
rectangular restrictions, weighted energy, CPU/GPU equivalence, changed-only
transfers, cache restoration, membership edits and topology reuse. Both run
strict validation and `sg::verify`. GPU checks explicitly report a skip where
there is no usable device.

`sg_net_distributed` checks one, two and six partitions, neighboring data only,
delayed/reordered/duplicate packets, split/merge with handoff, zero-confidence
kernels, hard pins, rectangular restrictions, CPU/CUDA equivalence, restored
law trials and cached layout reuse. `sg_net_processes` (when Python is present)
launches four separate CPU peers and four CUDA peers when available, over real
UDP sockets, and compares their combined owned results with one peer. The DSL
graphs, strict validation and `sg::verify` are checked in every process.
The distributed fixture also injects randomized delay/loss, reordering,
duplicates, unequal executor speeds and disconnected neighborhoods, checks
basis monotonicity/staleness/unsafe steps, and preserves the synchronous
singular component with real mixed CPU/CUDA execution.

`sg_net_placement` checks deterministic connected plans, capacities, hotspots,
finalized-epoch hysteresis/merging, unavailable-peer replacement, verified
handoff and real interest edits/removal/rollback. `sg_net_transport` checks
opaque UDP routes, honest unsupported reliability and bounded scheduling.
With ICE enabled, `sg_net_ice` checks >64 KB framing, reconnection, retained
unsent reliable work, coalescing, backpressure, separate control/boundary
associations and actual faulty relay-route traffic. Its receipt is independently
verified and committed through the declared port, with strict laws. The optional
`sg_net_ice_processes` fixture requires Python websockets and openssl to generate
a temporary test CA; it runs an actual authenticated WSS rendezvous, independent
CPU/CUDA/mixed regional processes and delayed Pong, comparing finalized
world/receipt chains. Relay fault fixtures use a bounded packet-loss burst with
continuing duplication/reordering/delay and require eventual reliable delivery;
an indefinitely unavailable route cannot promise progress.

`sg_net_agreement` checks canonical inputs/results, real CPU/CUDA and simulated
small numerical perturbations, forgery, signed equivocation, invalid proposals,
different input sets, intersecting quorums, replay, conflicting discrete events,
durable locks, sealed handoff and full committee replacement. It also checks
prediction replay, visual correction masks, bounded lead and confirmed history.
`sg_net_verified_processes` runs three actual CPU, CUDA and mixed executors over
UDP in a four-member q=3/f=1 committee with one executor absent, checking equal
epoch/receipt hashes, unchanged state on forged input, duplicate/replayed packets,
strict validation and laws. Required data producers remain available. Different
GPU hardware is covered by the tolerance/reference contract and perturbation
test; hardware diversity depends on available devices, not CPU fallback.

`Transport` remains external byte machinery. Observers may serialize what the
network says and send it; incoming bytes reach only a declared port through
`Engine::send`. Cellular restrictions may be noninvertible. They do not
repurpose Cover's exact invertible descent semantics.

## Execution workspaces and verification reuse

`Distributed` retains a private disposable workspace alongside the existing
Cellular and Partition layouts. It caches dense stalk offsets, boundary packet
metadata, the asynchronous dominance certificate and the original operator
bound/step. Epoch, generation, layout revisions, maximum staleness, lambda,
confidence, overlap weights, fixed mask, pins, restrictions and solver controls
determine that plan. Changes to these values within the same numerical
epoch/generation fail closed. An explicit reconfiguration must advance the
problem identity; the example's own configure arrow advances its execution
generation when replacing an already evaluated plan.
An omitted pin takes its value from the observation; changing that observation
therefore changes a hard constraint and also requires a new problem identity.

Observations, current results, incoming projections and sender progress are
refilled from ordinary State params each evaluation. They do not invalidate
the certificate. A restored state produces the same numerical step regardless
of cached history. The in-place `evaluate(state, output, backend)` overload
reuses output capacity; the value-returning overload remains available.
`diagnostics()` counts plan/certificate builds, structural validation and
numeric workspace capacity growth. These counters never influence a solve.

Layout validation runs when the retained layout changes, constant validation
when the plan changes, and finite/dimension checks on dynamic values every use.
`PreparedSystem` is an execution-only borrowed view plus the original
precomputed step, valid for its call while its buffers remain unchanged. It
avoids deriving the same bound again in CPU/CUDA execution. Public `solve`,
`residual` and verification of arbitrary public Problems retain full checks,
including transpose consistency, bounds, finite numbers, masks and exact pins.
The gradient method, iteration count and floating-point operation order remain
the reference method; there is no PCG/multigrid change.

Each `Verify` stores at most two validated immutable problem references:
canonical CPU values/hash, raw/reference residuals and deterministic checkpoint/hash.
Public `Problem::validate()` succeeds before any cache lookup; a claimed epoch
digest cannot bypass it. An identical signed input set reuses authentication.
Changed input signatures require authentication even when statement hashes are
unchanged. Certificate and attestation signatures remain checked. Copies of
Verify start with empty caches so different peers independently compute.
Discrete rules and checkpoint callbacks must be deterministic functions of the
immutable Problem and canonical values. Mutable captured world data is outside
that contract. Protocol owns its validated Problem and reuses its independently
verified result for later messages and Agreement acceptance. Tests count one
CPU reference solve/input-set authentication per peer/problem.

Signed wire framing is now `sg.net.protocol.v2`: a full Proposal carries the
values and authenticated input set; later Attestation and Finalization messages
carry a compact signed certificate reference and vote/quorum signatures. They
carry neither solved vectors nor input sets. A receiver with the authenticated
Problem independently reconstructs the canonical values. Otherwise it waits
and sends a signed, recipient-bound data request for that epoch/decision;
another peer responds with the full proposal/input data. No unknown hash can
finalize, and packet ordering is irrelevant. Pending recovery is limited to
64 messages/4 MiB, with 64 request/response identities and 256 received-frame
deduplication entries per Protocol. Excess work is reported as rejected rather
than accepted or buffered without bound. Requests/responses use the external
Reliable control path, whose retained retransmissions/backpressure provide
delivery; Protocol has no retry clock. Required input availability and a live
data holder remain necessary. Epoch, decision, receipt and durable-journal hash
domains are unchanged, but all session peers must upgrade the wire framing
together; v1 frames are refused.

CUDA output above 128 coordinates uses device-only flags, hierarchical integer
prefix scans and stable scatter. Each coordinate has its ascending output
offset; scheduling cannot change order. Small systems retain the single-launch
serial path. Host downloads remain count, changed indices and changed values
only, with unchanged solves transferring no values. Tests cover partial blocks,
multiple scan levels, pins, sparse/all/zero changes and restored inputs.

## Networking benchmark

Enable `-DSG_BUILD_NET_BENCH=ON` and build `sg_net_bench`. It is an opt-in
measurement executable, not a CTest correctness test. Its worlds and ports are
declared in DSL. For example:

```sh
build/sg_net_bench 32 32
build/sg_net_bench 32 1536 cpu
build/sg_net_bench --compaction
```

The first two arguments select coordinates and the distributed relaxation
budget; an optional third selects `cpu` or `cuda`. It measures single-machine
bounded solves, synchronous/asynchronous reconciliation, canonical verification
and complete Protocol finalization independently. Logical peer counts are
2/4/8/16, with simulated 1/10/50/100 ms delivery delays. Virtual delivery time
is separate from measured computation and never enters solver/world semantics.
CSV rows include coordinates, nonzeros, overlaps, partitions, cold/warm times,
rounds and residual, coordinate/nonzero throughput, bytes, allocations, workspace
growth after warmup, CPU reference solve time and CUDA transfers. The target
equation residual is 1e-6; a reported target round of -1 means the bounded
budget did not reach it. Warm measurements include packet handling/stalled
evaluations per completed relaxation. Heap allocation counts include ordinary
State/Params and wire materialization; zero numeric workspace growth does not
claim zero total allocations. The allocation counter covers ordinary C++ `new`, excluding
driver/system allocations. Allocation totals start after the first projection
and include initial result materialization; capacity growth is sampled after
each peer has completed two relaxations. Logical CUDA peers share one serial executor in
this benchmark, so partition switches include resident layout replacement;
`--compaction` isolates a resident device layout with contiguous uploads and
one-third changed outputs. Real multi-process/backend correctness is covered
separately by the test suite.

Measured on 2026-10-01, Windows Release GCC 16/UCRT, Ryzen 5 3600 and RTX
3090 Ti, against pre-optimization main `8f66545`. The same benchmark source was
linked against an archived pre-change net library/header set and the optimized
library. The 32-coordinate chain has 31 overlaps/62 restriction nonzeros;
the short run uses 32 relaxations. Verification/finalization figures below
are medians over the four simulated delays. Computation excludes injected
delay. Results are one local measurement, not portable performance guarantees.

| Measurement | Before | After | Change |
| --- | ---: | ---: | ---: |
| CPU synchronous, 2 partitions, 1 ms, warm microseconds/relaxation | 71.94 | 31.82 | 2.26x faster |
| CPU asynchronous, same case | 86.81 | 30.81 | 2.82x faster |
| Repeated canonical verification, 16-member problem, microseconds | 973.22 | 169.35 | 5.75x faster |
| Complete 16-peer CPU finalization, milliseconds | 2297.00 | 260.57 | 8.81x faster |
| Bytes per 16-peer finalized epoch | 1,729,140 | 1,000,020 | 42.2% fewer |
| Warm synchronous whole-fixture allocations, 2 peers/32 rounds/1 ms | 28,247 | 4,700 | 83.4% fewer |
| Warm asynchronous whole-fixture allocations, same case | 38,695 | 4,649 | 88.0% fewer |
| Resident CUDA, 262,145 coordinates, changed solve microseconds | 41,441 | 3,353 | 12.4x faster |
| Resident CUDA, same size, unchanged solve microseconds | 39,026 | 2,667 | 14.6x faster |

The CUDA compaction measurement uses zero solver iterations, contiguous changed
observations and pins leaving one-third of outputs changed; both versions
download exactly 873,820 changed values over ten samples. Small serial-path
results remain approximately unchanged. Cold first-device setup is approximately
110 ms and is reported separately; 16-member CPU cold proposal/verification
was approximately 0.92 ms before and 0.51 ms after. Raw public 32-coordinate
CPU solve time is essentially unchanged (29.53 vs 29.79 microseconds), since
that API still validates arbitrary caller buffers. Its numerical solve is
already small compared with authentication/control and distributed bookkeeping
in this fixture, so there is no measured justification for changing solvers.
Cold workspace setup can cost more: the first two-partition synchronous
preparation measured 137.6 vs 223.8 microseconds, and asynchronous preparation
117.8 vs 131.1. The benefit is removing that repeated work from warm rounds.

All 32 before/after CPU/CUDA protocol cases retain identical receipt hashes and
canonical result hashes, independent of virtual delay. All 64 optimized
distributed cases report zero numeric workspace capacity growth after warmup.
The 32-round measurement budget does not converge to 1e-6; a separate 1,536-round
CPU run reaches that equation residual in all 32 combinations of partition
count, synchronous/asynchronous execution and delay, after 1,052–1,080 rounds.
For 16 partitions/100 ms, synchronous virtual completion takes 153,600 ms;
bounded async takes 17,105 ms for the same relaxation budget. These are simulated
schedule results, not WAN latency promises. Remaining work includes semantic
Params copies, frame materialization, signatures/hash validation and shared
CUDA-executor layout changes. Measurements live under `<build>/out/network/`;
the reproducible benchmark source is `benchmarks/network.cpp`.
