#!/bin/bash
# The engine stays portable to the browser (docs/browser.md): built with
# Emscripten into its own folder and its tests run there. Any change that
# will not build or pass as WebAssembly fails this (ctest -C full:
# sg_wasm_portable). Usage: wasm_check.sh <source> <native build>
#   EMSDK (or ~/emsdk) must hold an activated Emscripten SDK; without one the
#   check says so and fails - a portability guard that cannot run is not one.
set -u
src=${1:?source}; native=${2:?native build}
out="$native/wasm-check"
emsdk=${EMSDK:-$HOME/emsdk}
if [ ! -f "$emsdk/emsdk_env.sh" ]; then echo "wasm_check: no Emscripten SDK at $emsdk (set EMSDK)"; exit 1; fi
source "$emsdk/emsdk_env.sh" >/dev/null 2>&1
sgc="$native/sgc"; shape="$native/laws/stategine_shape"
[ -x "$sgc" ] || { echo "wasm_check: no native sgc in $native"; exit 1; }
emcmake cmake -S "$src" -B "$out" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSTATEGINE_SGC_EXECUTABLE="$sgc" -DSTATEGINE_SHAPE_EXECUTABLE="$shape" > "$out.configure.log" 2>&1 \
  || { echo "wasm_check: configure failed ($out.configure.log)"; tail -20 "$out.configure.log"; exit 1; }
cmake --build "$out" -j"$(nproc)" -- -k 0 > "$out.build.log" 2>&1 \
  || { echo "wasm_check: the engine does not build as WebAssembly ($out.build.log):"; grep -E "^FAILED|error:" "$out.build.log" | head -30; exit 1; }
ctest --test-dir "$out" -j6 --timeout 300 --output-on-failure > "$out.test.log" 2>&1 \
  || { echo "wasm_check: tests fail as WebAssembly ($out.test.log):"; grep -E "\*\*\*|FAIL" "$out.test.log" | head -30; exit 1; }
echo "wasm_check: builds and passes as WebAssembly"
