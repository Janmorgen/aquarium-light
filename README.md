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
| Supply | 5 V 10 A, budgeted at 8 A normally (heat), 500 mA in test mode |

The panel is wired as a **column-major serpentine**: column 0 runs top to
bottom, column 1 bottom to top, and so on, 8 pixels per column.

## Setup

1. Install the [FastLED](https://github.com/FastLED/FastLED) library and the
   ESP32 board support package.
2. Create your credentials file:

   ```sh
   cp secrets.h.example secrets.h
   ```

   Fill in `WIFI_SSID`, `WIFI_PASSWORD` and `TZ_INFO` (a POSIX TZ string with
   DST rules), then `OTA_PASSWORD` and `WEB_PASSWORD` — the light listens on
   the LAN, so both of those are load-bearing. `secrets.h` is gitignored.
3. Open `aquarium-light.ino` in the Arduino IDE and flash, or build from the
   command line:

   ```sh
   arduino-cli compile .
   arduino-cli upload -p /dev/ttyUSB0 .
   ```

   `sketch.yaml` pins the FQBN and the partition scheme, so a bare `compile`
   is correct. **Do not override the partition scheme.** `no_fs` gives two 2 MB
   app slots, which is what makes wireless flashing possible, and it is the one
   thing an OTA push can never change afterwards — the partition table is
   written at `0x8000` by esptool, and OTA only ever writes the inactive app
   slot. Getting it wrong means retrieving the board.

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

On boot the sketch brings up Wi-Fi, syncs the clock over NTP, and leaves the
radio up. It used to go straight back to `WIFI_OFF` and return only for a
midnight re-sync; wireless flashing and the control page both need the light to
be reachable, not reachable for a few seconds a day. That costs roughly 80 mA
continuously.

Nothing in the network path blocks for long. If the router is down at boot the
light still comes up, `wifiMaintain()` keeps retrying, and OTA and the control
page start on the first loop after the link appears. If NTP never answers the
schedule has nothing to follow — the panel stays dark and the control page says
`no clock`, from where you can drive it by hand.

`timeMode()` maps the current hour onto one of five modes:

| Mode | Hours | Look |
| --- | --- | --- |
| 1 | 07:00-11:59 | Morning — blue accents, warm center band |
| 2 | 12:00-16:59 | Mid-day — bright white, cool boost on the left |
| 3 | 17:00-19:59 | Afternoon — warm white, amber boost on the left |
| 4 | 20:00-20:59 | Evening — deep warm, dimmed |
| 5 | 21:00-06:59 | Night — off |

Mode changes crossfade over `MODE_CROSSFADE_MS`, which is 30 minutes - slow
enough that the tank never visibly steps from one look to the next. The fade is
not a plain blend: color channels move in sequence (blue, then green, then red)
and rows are staggered so the change cascades down the panel. The stagger takes
`TRANSITION_CASCADE_PERCENT` (15%) of the transition and each row fades over the
remaining 85%, so the last row lands exactly on the half hour rather than
running past it. On top of that, `updateDimWaves()` drifts soft dark bands
across the columns to suggest light rippling through water.

### How a look is built

No mode paints pixels directly. Each one is a recipe: overlays from a shared
palette, stacked at their own opacities, then passed through filters that
reshape the finished pixels.

An **overlay** is a color, the brightness it shows at when fully applied, the
region it covers, and how its alpha is shaped across that region:

| Shape | Profile |
| --- | --- |
| `SHAPE_FLAT` | The peak value across the whole region |
| `SHAPE_GRADIENT` | Ramps peak to edge across the columns |
| `SHAPE_FALLOFF` | Peak at the column center, edge at both ends, quadratic |
| `SHAPE_RADIAL` | Peak at the region center in both axes - a soft spot |

A **layer** picks an overlay, an opacity, and how it lands: `BLEND_OVER` fades
what is underneath toward it, `BLEND_ADD` adds on top and saturates. So
morning is dim blue across the panel with the sun burning a soft warm hole in
the middle of it:

```c
const Layer MORNING_LAYERS[] = {
  // overlay      opacity jitter op          chance drift
  { OV_BLUE_SKY,  230,    35,    BLEND_OVER, 255,   0 },
  { OV_SUN_CORE,  228,    35,    BLEND_OVER, 255,   4 },
};
```

### Filters

A **filter** runs after every layer is down, scaling what has been composed.
Its region is the shape it acts on: `inside` applies within that region, shaped
the same four ways as an overlay, and `outside` applies to everything else.
That second field is what restricts a look to part of the board rather than
merely dimming it:

| Want | Region | Shape | Fields |
| --- | --- | --- | --- |
| Whole-panel dim | The panel | `FLAT` | `inside 200` |
| Vignette | The panel | `FALLOFF` | `inside 255, insideEnd 190` |
| Focus on the middle | The middle | `FLAT` | `inside 255, outside 90` |
| Middle only, rest off | The middle | `FLAT` | `inside 255, outside 0` |
| Both ends only | The middle | `FLAT` | `inside 0, outside 255` |
| Soft spot | A block | `RADIAL` | `inside 255, insideEnd 0, outside 60` |
| Everything off | Anything | `FLAT` | `inside 0, outside 0` |

Filters run in order, each acting on what the previous one left. Lighting both
ends is therefore one filter with the *middle* as its region, not two filters
naming each end - the first of those would zero the far end before the second
could keep it.

Evening uses this to draw the last of the light into the middle of the tank on
roughly one evening in three, the ends dropping to a low glow. Night composes
moonlight and then switches it off with a whole-panel filter - delete that one
line to run it.

To change how a time of day reads, change an opacity, swap an overlay, or add
a filter. Nothing in the recipes touches a pixel index.

### Day-to-day variation

Three knobs, all rolled fresh when a transition into a mode starts:

| Field | Effect |
| --- | --- |
| `jitter` | The +/- range an opacity or filter amount is rolled within |
| `chance` | Odds out of 255 that a layer or filter is used at all |
| `drift` | Columns an overlay's region may slide either way |

Nominal opacities sit near 90% rather than at 255, and overlay `level` values
are set a little above the brightness they are meant to show at, so a roll can
land above the nominal look as well as below it - pinned at 255, jitter could
only ever dim the panel.

Between them: mid-day grows a hot spot on about a third of days, morning's sun
sits a few columns either way and sometimes gathers toward the middle, evening
sometimes draws in. No mode carries hardcoded alternatives; the variation falls
out of the recipe.

Transitions vary too - each one sweeps from a randomly chosen end of the panel
rather than always cascading downward.

## Effects

The looks decide what color the tank is; `effects.h` decides how it moves.
Effects run over the frame buffer every rendered frame, after the composed
frame is in place, and they stack - each one runs in turn over what the last
one left.

| Effect | What it does |
| --- | --- |
| `waves` | Soft dark bands drifting across the columns |
| `caustics` | Two slow sine fields interfering - the net of light on a pool floor |
| `swell` | The whole panel breathing, about 90 s to the cycle |
| `glints` | Occasional bright flecks that fade, like the surface catching light |
| `sun` | A sun crossing the panel with the day, arcing high at midday |
| `focus` | A bright rectangle resting at one end of the board or the other - rare, but stays once it turns up |

### The cast

**At most two effects run at a time.** The rest are held at zero. A seat is
rotated every 7-18 minutes: one is emptied, and only once its occupant has
actually faded out does a different effect take the seat. Emptying before
filling is what holds the limit at two - a crossfade would briefly put three on
the panel. Handovers use a 75 s ramp rather than the idle 15-40 minute wander,
or the pair would spend most of its life mid-swap.

The replacement is drawn at random from the effects that are switched on, not
already seated, and not suppressed to nothing by the current mode. A pinned
effect is exempt from all of this: an explicit request outranks the rotation.

`effects_prime()` re-casts for the mode, so boot opens on a pair that suits the
hour rather than one chosen before the clock was known.

Every effect takes a strength of 0-255 and scales its own magnitude by it, so
0 is a no-op and any effect can be faded all the way in or out. Nothing is ever
simply switched on:

- each effect eases toward where it is heading, capped by `rampSec` - a full
  0-255 swing takes 15 to 40 minutes, so nothing moves more than about 12 points
  in a minute
- each rolls a new target level on its own randomized schedule (`minHoldSec` to
  `maxHoldSec`), so no two ever turn over at the same moment
- each carries a `modeCeiling` per mode, and because that feeds the same slew,
  a mode change slides the effects across rather than switching them
- `chance` decides whether a reroll turns the effect on at all, which is how an
  effect can be made to show up only occasionally rather than always wandering

No two effects share a ramp time either, so when the mode does change they pull
apart instead of moving as a block. Simulated over six hours with the mode
changing twice, the stack drifts like this:

```
  time   waves     caustics  swell     glints
   1h00  210 up    144 --    159 up     22 down   mode 2
   2h00  188 up     79 down  119 up     80 up     mode 2
   3h00  225 up     75 down  100 up     39 up     mode 3
   4h20  170 --     85 up    139 down   27 down   mode 3
   4h40   12 down   70 down   44 down    0 down   mode 5
   5h00    0 down    0 down    0 down    0 --     mode 5
```

**Everything that moves lives here.** A look is settled once, when a transition
into its mode starts, and then holds still for the length of that mode - the
layers and filters in `graphics.h` never change while a mode is running. The
only things that vary frame to frame are the crossfade between modes and this
stack.

#### The sun

`sun` is the one effect whose position does not come from a timer. It rides on
`dayProgress()` from `time_definitions.h`: 0 at `DAY_START_HOUR`, 255 at
`DAY_END_HOUR`. It enters just off the left edge at dawn, arcs to the top of
the panel around the middle of the day, and leaves off the right edge at dusk.

```
  09:00 |@@@@@++                         |     12:00 |     .++@@@@@@+.                |
        |@@@@++.                         |           |      .+++@@++.                 |

  15:00 |              .+@@@@@@++.       |     18:00 |                       ++@@@@@@+|
        |               .++++++.         |           |                       .++@@@@++|
```

Its strength still runs through the usual envelope, so it fades in and out with
the day rather than appearing at 07:00.

#### The focus rectangle

`focus` brightens a ten-column rectangle and turns down every other pixel
outside it. It holds a position for fifteen to thirty minutes and takes one to
two to travel, on a smoothstep, with fractional coverage at the rectangle's
edges so they do not snap column to column on the way:

```
    0m | = = = = = ##########= = = = = =|
   20m | = = = = = ##########= = = = = =|
   25m | = = = = = = = = = = =##########|
   40m | = = = = = = = = = = =##########|
   45m |########## = = = = = = = = = = =|
   70m |########## = = = = = = = = = = =|
```

The travel is there to keep the change off the board from being a jump - the
rectangle is meant to be found somewhere, not watched moving. Its state machine
advances even at zero strength, so it is never discovered somewhere unexpected
when the effect fades back in.

It is also the one rare effect: a `chance` of 40 means only about one stretch in
six turns it on, but the stretches are long and consecutive ones chain. Measured
across three simulated days, it is present for 13% of lit hours across seven
spells, the longest of them a little over two hours - while the other five
effects run continuously and simply wander.

### Parameters

Each effect carries two or three knobs, adjustable at runtime over
`POST /api/param?fx=N&p=M&v=V`. They are plain 0-255 bytes; what each means is
the effect's business:

| Effect | Parameters (defaults) |
| --- | --- |
| `waves` | `depth` 210, `width` 6, `rate` 7 |
| `caustics` | `depth` 175, `scale` 11, `speed` 6 |
| `swell` | `depth` 95, `period` 90 (seconds) |
| `glints` | `rate` 140, `spread` 1 (0 single pixel, 1 plus, 2 block), `peak` 120 |
| `sun` | `peak` 130, `size` 7 |
| `focus` | `lift` 85, `contrast` 255 |

Defaults are deliberately more assertive than the originals - with only two
effects on at a time, each one has to carry more. `glints` in particular went
from 5 concurrent single pixels to 16 that light their neighbours too, taking
its sustained coverage from 5 to about 60 of the 256 pixels.

Changes are live and unsaved: a reboot returns every parameter to its default.

Adding an effect is a function taking a strength and an entry in the `EFFECTS`
table.

At the real rate a bench sitting shows nothing at all, so test mode winds the
stack forward by `TEST_EFFECTS_SPEEDUP` (40x) and prints the current strengths
to serial every 15 s. Each loop sets its own rate every frame, so throwing the
switch back cannot strand the effects at bench speed.

## Control page

The light serves a page at `http://<its address>/` — a live view of the panel,
the mode buttons, brightness, and every effect's current strength. The panel
view is the real frame buffer, effects and power limiting included, so it shows
what the tank is actually being lit with rather than what was asked for.

| Endpoint | Method | Does |
| --- | --- | --- |
| `/` | GET | The control page |
| `/api/status` | GET | Panel, clock, network and effect state as JSON |
| `/api/frame` | GET | The live frame, 256 RGB triples as hex |
| `/api/mode?set=N` | POST | 0 follows the clock, 1-5 pin a mode |
| `/api/brightness?set=N` | POST | Master brightness, 0-255 |
| `/api/test?set=0\|1` | POST | The remote equivalent of the test switch |
| `/api/effect?i=N&set=S` | POST | Pin one effect to a strength, -1 for auto |
| `/api/effect?i=N&enabled=0\|1` | POST | Switch an effect out of the rotation entirely |
| `/api/param?fx=N&p=M&v=V` | POST | Set one effect parameter |
| `/api/pixel?col=&row=&rgb=` | POST | Pin one pixel (`release=1` to let it go) |
| `/api/pixels` | POST | Paint the whole panel from a 1536-char hex body; `?clear=1` releases all |
| `/api/resync` | POST | Ask NTP for the time now |
| `/api/reboot` | POST | Restart |

Reading takes no credentials; anything that changes the light needs
`WEB_USER` / `WEB_PASSWORD` over HTTP basic auth.

Two things are worth knowing about how commands behave:

- **A commanded change crossfades in 4 s**, not the scheduled half hour.
  Someone pressing a button should see it happen.
- **Overrides suppress, they do not fight.** A pinned mode wins over the
  schedule *and* over test mode's cycling until it is set back to auto; a
  pinned effect strength still arrives through the normal slew, so even a
  commanded change reaches the panel gradually.

## Off-device control surface

`control-surface/` is a small service for the homelab box that hosts the same
controls with something the light cannot do for itself: **it records history.**
The ESP32 has no storage and its RAM is spoken for, so it can only answer what
it is doing now — the service answers what it has been doing, including whether
it was reachable at all. It also keeps the device credentials out of the
browser, since the page is same-origin and the service attaches basic auth on
the way through.

Standard library only, no image build. See `control-surface/README.md`.

## Wireless flashing

The board runs an `ArduinoOTA` listener, so after the first USB flash it can be
updated over the network:

```sh
arduino-cli compile .
arduino-cli upload -p <its address> --protocol network \
  --upload-field password=<OTA_PASSWORD> .
```

The panel goes dark and draws a progress bar along its bottom row while the
write happens — `ArduinoOTA.handle()` does not return until the upload is done,
so the render loop is stopped anyway, and a dark panel keeps a Wi-Fi TX spike
from landing on top of 256 lit LEDs.

Find the address from the boot log, or give the board a static lease. A fixed
address can also be set in `secrets.h` (`NET_STATIC_IP` and friends, commented
out by default).

**Keep the board physically reachable.** A bad image is recovered with a USB
flash and nothing else, so do not seal the enclosure.

## Files

| File | Purpose |
| --- | --- |
| `aquarium-light.ino` | Setup, main loop, daily NTP re-sync |
| `graphics.h` | The overlay palette, the looks, transitions |
| `webui.h` | Control page, status and frame API |
| `ota.h` | Wireless flashing, and the progress bar it draws on the panel |
| `panel.h` | Panel geometry, the frame buffer, pixel addressing |
| `effects.h` | The effect stack and its drift |
| `variation.h` | The two rolls - jitter and chance - shared by looks and effects |
| `time_definitions.h` | Hour-to-mode mapping |
| `wifi_helpers.h` | Wi-Fi lifecycle and reconnection, NTP, clock reads |
| `power.h` | Current budgets for the two modes |
| `pixels.h` | Per-pixel color easing helper (not currently used) |
| `regulator.h` | Non-blocking interval timer (not currently used) |
| `controls.h` | Test mode switch, debounced |
| `secrets.h` | Your credentials — gitignored |

### Addressing pixels

`ledIndex(col, row)` is the only correct way to turn coordinates into a strip
index; it handles the serpentine reversal. Overlays, filters and `scaleRegion()`
all describe their region in `(col0, col1, row0, row1)` order to match.

## Test mode

A switch or jumper shorting GPIO 32 to GND puts the light in test mode:

```
GPIO 32 ---o/ o--- GND        closed = test mode
```

| Switch | Mode | Current budget |
| --- | --- | --- |
| Open | Clock schedule, NTP synced | 8 A |
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
| `NORMAL_POWER_BUDGET_MA` | `8000` | Cap on the schedule |

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
