#!/bin/sh
# Builds and runs every tests/test_*.cpp with the host compiler.
# Each test is its own translation unit; tests/fake holds stand-ins for
# windows.h and friends so mod code can build on Linux.
# Usage: tests/run.sh   (CXX=clang++ tests/run.sh to pick a compiler)
set -eu
cd "$(dirname "$0")"
CXX="${CXX:-g++}"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
status=0
for t in test_*.cpp; do
    [ -e "$t" ] || continue
    bin="$out/${t%.cpp}"
    if ! "$CXX" -std=c++17 -Wall -Wextra -I fake -I.. -o "$bin" "$t"; then
        echo "BUILD FAILED: $t"; status=1; continue
    fi
    "$bin" || status=1
done
exit $status
