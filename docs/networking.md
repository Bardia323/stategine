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

## Building and checking

The CPU backend always builds. `SG_NET_CUDA=ON` (default) adds CUDA when CMake
finds the toolkit's NVRTC and driver libraries; `-DSG_NET_CUDA=OFF` gives a
CPU-only build. A build-time helper compiles `src/gpu/network_cuda.cu` to PTX
using NVRTC and embeds it in the library. The host uses the C CUDA driver API
and the project's own compiler, so MinGW does not need an MSVC-compatible C++
ABI. PTX compilation is not a frame operation. The initial module targets
compute capability 6.0 or later. Other GPU implementations, PCG, multigrid and
distributed consensus are not part of this pass.

`sg_net` runs the same DSL graph and reconciliation arrow on CPU and available
CUDA. `sg_net_numerics` checks analytic solutions, zero residual on agreement,
disagreement reduction, a compatible cycle, scalar and coordinate pins,
rectangular restrictions, weighted energy, CPU/GPU equivalence, changed-only
transfers, cache restoration, membership edits and topology reuse. Both run
strict validation and `sg::verify`. GPU checks explicitly report a skip where
there is no usable device.

`Transport` remains external byte machinery. Observers may serialize what the
network says and send it; incoming bytes reach only a declared port through
`Engine::send`. Cellular restrictions may be noninvertible. They do not
repurpose Cover's exact invertible descent semantics.
