# Stub headers

Minimal, deliberately fake stand-ins for `Arduino.h`, `FastLED.h`, `WiFi.h`,
and `esp_system.h`. They declare just enough of each API — signatures only, no
implementations — for `g++ -fsyntax-only` to type-check the sketch on a machine
with no Arduino toolchain.

They are **not** a compatibility layer and nothing here is ever linked or run.
If you start using an API these stubs don't declare, the check will fail with
an "unknown identifier" error — add the declaration here, matching the real
signature.

Kept intentionally small: the moment these grow into a partial reimplementation
they stop being trustworthy, because a check that passes against a wrong stub
is worse than no check.
