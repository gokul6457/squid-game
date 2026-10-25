#!/bin/bash
# Builds ../public/game/game.wasm from ../../main.c  (needs: clang, lld, wasi-libc  ->  sudo apt install clang lld wasi-libc libclang-rt-18-dev-wasm32)
set -e
cd "$(dirname "$0")"
python3 make_web.py "${1:-main.c}" web_game.c
clang --target=wasm32-wasi --sysroot=/usr -O2 -mexec-model=reactor -nostartfiles \
  -Wno-missing-field-initializers -Wno-unused-function -Wno-unused-variable \
  -Wl,--no-entry -Wl,--export=_initialize -Wl,--allow-undefined -Wl,--initial-memory=33554432 -Wl,--max-memory=268435456 \
  -Wl,-z,stack-size=1048576 -Wl,--strip-all -o ../public/game/game.wasm web_game.c -lm
ls -l ../public/game/game.wasm
