#!/usr/bin/env bash
# Run all tests for lvgl-html5-canvas.
# Used by CI and locally. Assumes linux-host preset is configured.
set -euo pipefail

cd "$(dirname "$0")/.."

PRESET="${PRESET:-linux-host}"

echo "==> configure ($PRESET)"
cmake --preset "$PRESET" >/dev/null

echo "==> build"
cmake --build --preset "$PRESET"

echo "==> ctest"
ctest --preset "$PRESET"

echo
echo "all tests passed"
