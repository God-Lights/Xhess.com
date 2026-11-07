#!/usr/bin/env bash
# Builds the browser engine: a single self-contained file (WebAssembly embedded) that runs as a Web Worker.
#   engine/wasm/build.sh [output-dir]      requires Emscripten (em++) on PATH
set -euo pipefail
cd "$(dirname "$0")/.."            # engine/
OUT="${1:-wasm/dist}"
mkdir -p "$OUT"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

SOURCES=(
  src/core/attacks.cpp src/core/bitboard.cpp src/core/init.cpp src/core/move.cpp src/core/movegen.cpp
  src/core/perft.cpp src/core/position.cpp src/core/zobrist.cpp
  src/eval/classical.cpp
  src/search/movepick.cpp src/search/search.cpp src/search/see.cpp src/search/timeman.cpp src/search/tt.cpp
  src/uci/uci.cpp
  src/wasm/wasm_main.cpp
)

# ALBATROSS_NO_THREADS: no std::thread in the browser; the driver stops searches by restarting the worker.
# ALBATROSS_MAX_HASH_MB: the Hash option is limited by WebAssembly memory (see MAXIMUM_MEMORY).
em++ -std=c++20 -O3 -DNDEBUG -DALBATROSS_NO_THREADS -DALBATROSS_MAX_HASH_MB=256 -Isrc "${SOURCES[@]}" \
  -sMODULARIZE=1 -sEXPORT_NAME=AlbatrossModule -sENVIRONMENT=web,worker,node -sSINGLE_FILE=1 \
  -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=67108864 -sMAXIMUM_MEMORY=1073741824 -sTOTAL_STACK=4194304 \
  -sEXPORTED_FUNCTIONS=_albatross_init,_albatross_command,_main -sEXPORTED_RUNTIME_METHODS=ccall \
  -o "$TMP/glue.js"

# banner (license, usage) + Emscripten glue (with the WebAssembly inlined) + worker driver.
# The driver must live at the top level of the file, so it is appended instead of using --post-js.
cat wasm/banner.js "$TMP/glue.js" wasm/driver.js > "$OUT/albatross.js"
cp wasm/albatross-loader.js "$OUT/albatross-loader.js"
echo "built $OUT/albatross.js ($(wc -c < "$OUT/albatross.js") bytes)"
