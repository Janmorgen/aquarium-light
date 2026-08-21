#!/usr/bin/env bash
#
# Type-checks the sketch with the host g++ against the stub headers in
# stubs/, so syntax and type errors can be caught without an Arduino
# toolchain installed.
#
# This is NOT a build. It does not produce a binary, it does not use the real
# FastLED or ESP32 headers, and it cannot catch anything hardware-specific.
# It does reliably catch what a normal C++ compiler catches: syntax errors,
# bad signatures, wrong argument counts, type mismatches, unused variables.
#
# Usage: tools/typecheck/check.sh

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
sketch="$(cd "$here/../.." && pwd)"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

# wifi_helpers.h includes "secrets.h", which is gitignored. Fall back to the
# example so a fresh clone can still be checked.
if [ ! -f "$sketch/secrets.h" ]; then
  cp "$sketch/secrets.h.example" "$tmp/secrets.h"
fi

# Headers not reached from the .ino are pulled in explicitly so they get
# checked too.
cat > "$tmp/tu.cpp" <<'EOF'
#include "aquarium-light.ino"
#include <regulator.h>
#include <controls.h>
EOF

g++ -fsyntax-only -std=gnu++17 \
    -Wall -Wextra -Wno-unused-parameter \
    -I"$here/stubs" -I"$sketch" -I"$tmp" \
    "$tmp/tu.cpp"

echo "type-check clean"
