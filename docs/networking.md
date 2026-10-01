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

Each world step has an integer `epoch`. `generation` identifies the execution
partition within that epoch. `round` is a protocol work count, not a clock:
only a Temporal drive runs the reconciliation arrow. Every boundary packet
names sender, recipient and `(epoch, generation, tick)`. Tick is the round of
the projected section. A peer advances from r to r+1 only after receiving all
neighbor projections for r and publishing its own. Two slots per crossing
edge retain r and r+1, so arrival order does not change arithmetic. Duplicates,
old ticks, other epochs/generations and non-neighbor messages are ignored.
Missing packets wait for delivery; they are never replaced by invented values.

Only changed projections carry numeric values. An unchanged projection sends
a progress record naming the tick of its last changed value (`basis`); it
still participates in the round boundary. A progress record arriving before
its basis waits for that basis. Receive and send records live in the owning
constraint's ordinary `remote_*`, `next_*` and `sent_*` params. Restoring the
state restores protocol progress too; the numerical classes keep no packet
history, clocks or hidden solution trajectory.

With a crossing boundary the local backend takes one bounded gradient step:

```text
x_I[r+1] = x_I[r] - alpha * (M_I*(x_I[r]-y_I) + lambda*(delta* W delta x[r])_I)
```

All peers use the same positive `diffusion_step` (default 0.05). Each validates
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
step negotiation and partition load balancing are not implemented here.

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
NAT traversal and larger-message framing are not implemented. No executor's
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
implements IPv4 transport, not automatic Internet connectivity. The same-PC
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
