# The same world in a browser

StateGraph declares meaning. `sg/render/ViewPlan.hpp` derives borrowed drawing
data from its states, spatial seams, embeddings and Looks. It owns no state
and can be discarded at any time. Native GL and browser WebGPU use the same
camera, geometry, portal framing, clipping and shadow projection computations.
Resource bindings cannot create a world relationship; every use must still be
declared by the graph.

`sg/web` contains device adapters and executors. DOM callbacks queue physical
input; C++ polling resolves the existing DSL Bindings and fires their events.
WebRTC callbacks queue opaque bytes/signals only. C++ polling authenticates the
existing protocol before delivering a finalized result through its declared
port. Neither GPU device loss nor connection replacement changes the graph,
committee, participant structure or world data.

## Build

Use an activated Emscripten SDK and a native C++17 toolchain:

```sh
cmake -S . -B build/native
cmake --build build/native
ctest --test-dir build/native --output-on-failure
emcmake cmake -S . -B build/wasm \
  -DSTATEGINE_SGC_EXECUTABLE=/absolute/path/build/native/sgc
cmake --build build/wasm
ctest --test-dir build/wasm --output-on-failure
```

On Windows the host executable is `sgc.exe`. `SG_BUILD_SGC` defaults off for
cross builds; a Wasm compiler is never run as a build tool. Tests also consume
the adjacent native `stategine_shape` executable (or `STATEGINE_SHAPE_EXECUTABLE`).
Emscripten automatically excludes native GL/GLFW, CUDA, ROCm, Vulkan, Metal
and libdatachannel probing. Validation, exceptions and rollback stay enabled.
Generated JS/Wasm and preloaded filesystem data remain in `build/wasm`.
Assets and TextStore keep their existing owner/path semantics; preload files
into Emscripten's filesystem rather than adding URL-owned world data.

## Run the example

`examples/browser` uses the existing `examples/pong/pong.sg`, native computations
and signed protocol. Make fresh credentials with the native tool:

```sh
build/native/sg_net_keys credentials 2 2 0 pong
python examples/ice_rendezvous.py --port 49280
python -m http.server 8080 --directory build/wasm
```

Open `http://localhost:8080` in a WebGPU-capable browser. Select the credential
folder, choose one peer and start; a second browser or native ICE Pong uses
the other peer. Use W/S or arrows. For Internet deployment use HTTPS/WSS and
provide STUN/TURN addresses through `IceConfig`. The relay routes signed
opaque signaling and has no world or agreement role.

The shell schedules existing `Engine::tick(dt)` with requestAnimationFrame.
It does not keep gameplay data in JS. Renderer `uTime` is the state's declared
`own_time`, otherwise its declared Temporal drive line, otherwise zero. Frame
intervals move disposable Look blend weights only; they never accumulate into
shader/world time. Native `Engine::run()` is unchanged.

The sample persists Agreement's opaque journal before voting. It refuses a
used identity starting again at tick zero. Loading a verified recovery
checkpoint is application work; deleting a journal is not a recovery protocol.
Use a fresh committee for independent demonstrations.

## Drawing and custom Looks

WebGPU executes instanced geometry, depth/shadows, graph-declared portal and
feed views, Surface2D textures, HDR, AO, bloom, composite/Look blending,
lighting, doorway clipping and CRT glass. GPU resources can be recreated
without changing StateGraph. No GLSL is translated at runtime.

Attach optional `wgsl` text to the **existing Look pass element**. `prepare()`
reports a custom GLSL pass without WGSL; `MissingShader::Refuse` is the default.
Applications may explicitly choose `BuiltinFallback`; diagnostics remain
visible. Changed shader source requires a new prepare. Custom modules use
the vertex/uniform ABI in `src/web/shaders.js`: `vs_main`/`fs_main` for scene
and post, `vs_shadow`/`fs_shadow` for shadow, and the same Frame bindings.

The executor additionally supplies binding 6 with 256 packed vec4 uniforms.
Preparation generates `sg_uFoo()`/`sg_uFoo_x()` accessors from the same Look
uniform keys, with the same blended values. Those `sg_` names are reserved.
`sg_uTime()` always reads declared state time. Custom code can copy the WGSL
executor's Frame declarations; it must not declare binding 6 again. This is
a shader ABI, not another Look registry or presentation model.

## Transport and verification

`WebRtcTransport` implements `net::Transport`. The same `IceSession` contract
defines stable offerer selection, Reliable/Latest lanes, session generations,
Description/Candidate/Restart signaling, `_sg` data channels and opaque channel
framing. Reliable is ordered; Latest is unordered with zero retransmissions.
Queues are bounded and report backpressure. Delivery uncertainty after a
restart requires replay of the existing signed idempotent protocol frontier.
Transport encryption adds privacy; pinned application signatures still define
protocol identity and independently verified quorum acceptance.

Wasm uses the unchanged double `CpuBackend`, Monocypher Ed25519/BLAKE2b,
canonical encoding, Verify, Agreement and Protocol. Browser `random_seed()`
uses `crypto.getRandomValues`. WebGPU rendering does not become an f32 network
solver or weaken numerical tolerance.

Run the real integration harness with a native libdatachannel build:

```sh
python tests/browser.py --native build/native --wasm build/wasm \
  --ice-build build/ice --node node --browser /path/to/chromium
```

It uses real WebGPU and native/browser WebRTC, compares native/Wasm graph
facts, laws, numerical values, hashes, signatures and receipts, exchanges
messages larger than 64 KB, reconnects, checks bounded queues and finalizes
the same Pong epochs. GPU or browser absence is a failure of that integration
run, not an emulated GPU success. Portable CTests run under Node separately;
native process/socket/GL tests run on their actual native platform.

To include this integration run in Wasm CTest, configure
`SG_BROWSER_NATIVE_DIR`, `SG_BROWSER_ICE_DIR`, `SG_BROWSER_EXECUTABLE` and
`SG_BROWSER_NODE`. Portable tests remain separate from this real GPU/network
test; setting those paths never substitutes mocks or CPU drawing.

## Verification of this pass

On Windows, Emscripten 6.0.10 and Edge ran the same source and DSL with
36 native tests and 28 Wasm tests (27 portable tests plus real browser
integration). Native CUDA networking checks and the two optional ICE/process
tests also passed. The browser/native integration exchanged 96 KB messages,
reconnected sessions and reached the identical receipt after 40 Pong epochs.
It exercised real WebGPU portals, camera feedback, Surface2D textures,
shadows, AO, bloom, custom WGSL, refusal/fallback and resource recreation.

The sibling lab ran its 14 tests, including foley, and all six screenshot
checks against this local engine without moving its pinned revision. The room
now declares its Temporal drive for relighting and shader time. Its graph
facts changed only by that drive and drive order. Screenshot expectations
were reviewed and refreshed for declared time: the preserved pre-browser
renderer already differed from all six old goldens, including the overview's
older room layout, and its grain depended on accumulated rendering time.
The new expectations pass on repeated runs; no game rules or portal relations
were changed to fit a picture.
