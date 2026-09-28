#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
build="${1:-build-linux}"
test_binary="$(mktemp)"
trap 'rm -f "$test_binary"' EXIT
c++ -std=c++17 -DRELEASE=1 -DSMTG_RENAME_ASSERT=1 \
  -I src -I "$build/_deps/vst3sdk-src" tests/tweak_regression.cpp \
  "$build"/src/CMakeFiles/PsycleSynthLoader.dir/loader/*.cpp.o \
  -L "$build/lib/Release" -Wl,--start-group \
  -lsdk -lsdk_hosting -lsdk_common -lbase -lpluginterfaces \
  -Wl,--end-group -lpthread -ldl -o "$test_binary"
"$test_binary"