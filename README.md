# aquarium-light

An ESP32-driven aquarium light. A WS2812B panel is mounted over the tank and
shifts color temperature through the day — cool and blue in the morning, bright
at midday, warm through the afternoon and evening, off at night. Time comes
from NTP, so the schedule follows the real clock including DST.

## Hardware

| Part | Notes |
| --- | --- |
| ESP32 | Developed against an ESP32-WROOM-DA |
| WS2812B panel | 8 rows x 32 columns, 256 LEDs |
| Data pin | GPIO 26 (`DATA_PIN` in `graphics.h`) |
| VBUS sense | GPIO 19 (`VBUS_SENSE_PIN` in `power.h`) |
| Supply | 5 V, budgeted at 5 A via `setMaxPowerInVoltsAndMilliamps` |

The panel is wired as a **column-major serpentine**: column 0 runs top to
bottom, column 1 bottom to top, and so on, 8 pixels per column.

## Setup

1. Install the [FastLED](https://github.com/FastLED/FastLED) library and the
   ESP32 board support package.
2. Create your credentials file:

   ```sh
   cp secrets.h.example secrets.h
   ```

   Fill in `WIFI_SSID`, `WIFI_PASSWORD`, and `TZ_INFO` (a POSIX TZ string with
   DST rules). `secrets.h` is gitignored.
3. Open `aquarium-light.ino` in the Arduino IDE and flash, or build from the
   command line:

   ```sh
   arduino-cli compile --fqbn esp32:esp32:esp32da .
   arduino-cli upload  --fqbn esp32:esp32:esp32da -p /dev/ttyUSB0 .
   ```

### A note on disk space

The current Espressif core (`esp32:esp32@3.3.11`) installs toolchains and
precompiled libraries for *every* ESP32 variant — C3, C5, C6, H2, P4, S2, S3 —
with no way to select only the classic ESP32. That is roughly 7-8 GB extracted,
which will not fit on a typical 16 GB Raspberry Pi SD card.

If you are tight on space, `arduino:esp32@2.0.18-arduino.5` is about 1 GB and
builds this sketch identically — it only uses WiFi, NTP, and FastLED, none of
which changed between core 2.x and 3.x.

## Checking changes without a toolchain

`tools/typecheck/check.sh` compiles the sketch with the host `g++` against stub
headers, so you can catch syntax and type errors with no Arduino toolchain
installed:

```sh
./tools/typecheck/check.sh
```

This is a type-check, not a build. It produces no binary and uses fake
`Arduino.h` / `FastLED.h` headers, so it cannot catch anything hardware- or
library-specific — but it does catch what any C++ compiler catches, and it runs
in about a second. See `tools/typecheck/stubs/README.md`.

**A clean type-check is not a working light.** Anything touching pixel
addressing or timing still needs a flash to the real panel.

## How it works

On boot the sketch connects to Wi-Fi, syncs the clock over NTP, then drops the
radio — Wi-Fi stays off during normal operation and comes back for one
re-sync per day at midnight.

`timeMode()` maps the current hour onto one of five modes:

| Mode | Hours | Look |
| --- | --- | --- |
| 1 | 07:00-11:59 | Morning — blue accents, warm center band |
| 2 | 12:00-16:59 | Mid-day — bright white, cool boost on the left |
| 3 | 17:00-19:59 | Afternoon — warm white, amber boost on the left |
| 4 | 20:00-20:59 | Evening — deep warm, dimmed |
| 5 | 21:00-06:59 | Night — off |

Mode changes crossfade over `MODE_CROSSFADE_MS` (60 s). The fade is not a plain
blend: color channels move in sequence (blue, then green, then red) and rows
are staggered so the change cascades down the panel. On top of that,
`updateDimWaves()` drifts soft dark bands across the columns to suggest light
rippling through water.

## Files

| File | Purpose |
| --- | --- |
| `aquarium-light.ino` | Setup, main loop, daily NTP re-sync |
| `graphics.h` | Panel addressing, the five modes, transitions, dim waves |
| `time_definitions.h` | Hour-to-mode mapping |
| `wifi_helpers.h` | Wi-Fi connect/disconnect, NTP, clock reads |
| `power.h` | USB VBUS detection |
| `pixels.h` | Per-pixel color easing helper (not currently used) |
| `regulator.h` | Non-blocking interval timer (not currently used) |
| `controls.h` | Placeholder for physical controls |
| `secrets.h` | Your credentials — gitignored |

### Addressing pixels

`ledIndex(col, row)` is the only correct way to turn coordinates into a strip
index; it handles the serpentine reversal. Every region helper takes its
arguments in `(col0, col1, row0, row1)` order to match.

```c
fillRegionScaled(0, 5, 0, ROWS - 1, CRGB(255, 245, 230), 153);  // leftmost 6 columns
```

## Testing without waiting for the clock

`graphics_setup_test()` / `graphics_loop_test()` cycle through every mode on a
timer instead of following real time. Swap them in for `graphics_setup()` /
`graphics_loop()` in the sketch. `graphics_init_test()` also caps the power
budget at 500 mA, which is safe for bench testing over USB.
