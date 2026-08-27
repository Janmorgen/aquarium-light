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
| Test switch | GPIO 32 to GND (`TEST_SWITCH_PIN` in `controls.h`) |
| Supply | 5 V, budgeted at 5 A normally, 500 mA in test mode |

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
| `power.h` | Current budgets for the two modes |
| `pixels.h` | Per-pixel color easing helper (not currently used) |
| `regulator.h` | Non-blocking interval timer (not currently used) |
| `controls.h` | Test mode switch, debounced |
| `secrets.h` | Your credentials — gitignored |

### Addressing pixels

`ledIndex(col, row)` is the only correct way to turn coordinates into a strip
index; it handles the serpentine reversal. Every region helper takes its
arguments in `(col0, col1, row0, row1)` order to match.

```c
fillRegionScaled(0, 5, 0, ROWS - 1, CRGB(255, 245, 230), 153);  // leftmost 6 columns
```

## Test mode

A switch or jumper shorting GPIO 32 to GND puts the light in test mode:

```
GPIO 32 ---o/ o--- GND        closed = test mode
```

| Switch | Mode | Current budget |
| --- | --- | --- |
| Open | Clock schedule, NTP synced | 5 A |
| Closed | Cycles every mode on a timer | 500 mA |

Close it when the light is on a bench running off the USB cable: the panel
drops to a current a USB port can actually deliver and starts cycling the
modes. Open it and the schedule comes back. The switch is read at boot and
polled while running, so throwing it either way takes effect immediately with
no reboot - re-budgeting calls `setMaxPowerInVoltsAndMilliamps()` again rather
than re-running `addLeds()`, which must only ever happen once.

Test mode never touches Wi-Fi, so a bench run works with no network in reach.
If the switch is opened on a board that booted into test mode, it syncs the
clock at that point before handing over to the schedule.

Test mode runs on its own timings (`TEST_CROSSFADE_MS`, `TEST_MODE_DWELL_MS`
in `graphics.h`, 8 s and 12 s) so a full pass through the five modes takes
about a minute instead of the ten the real 60 s crossfades would take.

| Define | Default | Meaning |
| --- | --- | --- |
| `TEST_SWITCH_PIN` | `32` | Pin the switch pulls |
| `TEST_SWITCH_ACTIVE_LEVEL` | `LOW` | Level meaning test mode |
| `TEST_POWER_BUDGET_MA` | `500` | Cap in test mode |
| `NORMAL_POWER_BUDGET_MA` | `5000` | Cap on the schedule |

GPIO 32 has an internal pull-up and is not a strapping pin, so it is safe to
hold either way at boot, and an unwired pin reads as "not test mode". Pins
34-39 would not work here - they have no internal pull-ups. Each reading is a
majority vote of 9 samples and a change has to hold for 100 ms, so switch
bounce and a long run beside the LED data line cannot flip the panel back and
forth.

### Why a switch and not USB detection

The board cannot tell what is powering it. The classic ESP32 has no USB
peripheral, and this DevKit does not route VBUS to a GPIO - probed with USB
connected, all 34 usable pins simply follow their internal pull. (An earlier
version of `power.h` read GPIO 19 as a VBUS sense; that never worked.)

Sensing the 5 V rail instead does not help either, because USB VBUS and the
strip's supply arrive on the same 5 V pin - the rail reads 5 V whichever one is
up. Separating them would take a Schottky in the feed to the ESP32, at which
point a divider on the strip-side rail would work. A switch costs less and
never guesses wrong.
