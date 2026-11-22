#!/bin/sh
# Builds the web mockup's engine: web/braindance.wasm from sample.cpp and display.cpp, unchanged.
set -e
cd "$(dirname "$0")"
SDK=${OPEN_XY:-../../open-xy}/sdk/include
CLANG=${CLANG:-clang++}   # needs wasm32 and wasm-ld: on macOS, Homebrew llvm and lld
"$CLANG" --target=wasm32 -std=c++11 -O2 -ffreestanding -fno-exceptions -fno-rtti -nostdlib \
  -I"$SDK" -I.. -Wl,--no-entry $(for f in bd_params bd_out bd_start bd_render bd_lane bd_rank bd_strikes bd_display bd_page bd_frame bd_frame_nodes bd_node_bytes bd_nodes_offset bd_texts_offset bd_text_bytes bd_paths_offset bd_sample_data bd_sample_capacity bd_load bd_harvest bd_overview bd_shift; do printf -- "-Wl,--export=%s " $f; done) \
  -o braindance.wasm bridge.cpp
ls -l braindance.wasm
