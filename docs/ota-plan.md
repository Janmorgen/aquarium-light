# Plan: wireless (OTA) flashing

**Status:** proposed, not implemented. Nothing in this document has been built
or tested on hardware.

**Goal:** flash new firmware to the aquarium light from the Raspberry Pi
without physically retrieving the board and connecting USB.

Verdict: feasible, but it is an architecture change rather than a library
addition, and it does not remove the need for a compiler.

## Why this isn't just adding a library

### 1. The radio is deliberately off

`aquarium-light.ino:32` calls `disconnectWiFi()` immediately after the boot NTP
sync, and `wifi_helpers.h:34` takes the radio all the way to `WIFI_OFF`. It
returns for a few seconds during the daily midnight re-sync
(`aquarium-light.ino:62-64`) and at no other time.

An OTA listener is therefore unreachable ~99.99% of the time. Making the device
reachable is the substance of this work; wiring up the OTA library itself is
the easy part.

### 2. OTA transports a binary, it does not produce one

There is no ESP32 core installed on the Pi — the current Espressif 3.x core
needs ~7-8 GB and does not fit on this SD card (see README, "A note on disk
space"). `tools/typecheck/check.sh` deliberately cannot emit a binary.

So something must still compile the firmware. See "What the Pi needs" below.

### 3. One more USB flash is unavoidable

The first OTA-capable image has to arrive over the wire. Plan on retrieving the
board once more.

## Open decisions

Both need answering before implementation starts.

### Decision 1: when is the radio up?

| Option | Cost | Flashing workflow |
| --- | --- | --- |
| **Boot window** (recommended) | One power cycle per flash | Power-cycle the light, push within ~5 min |
| **Always on** | ~80 mA continuous, permanently changes the power profile of a device sitting over water | Flash anytime |
| **Night window** | None — the panel is already off | Flash only between 21:00 and 07:00 |

The boot window preserves the current power behaviour and the author's original
intent in turning the radio off, at the cost of a power cycle. Always-on is the
most convenient and also makes the daily re-sync logic redundant, but it is the
biggest behavioural change.

### Decision 2: does the Pi build, or only flash?

| Approach | Disk | Capability |
| --- | --- | --- |
| Install `arduino:esp32@2.0.18-arduino.5` | ~1 GB of the 5.0 GB free | Build **and** flash from the Pi |
| Keep only `espota.py` | ~10 KB | Flash only; build elsewhere |

`espota.py` is a standalone, dependency-free Python script, so push-only OTA is
essentially free in disk terms. Building on the Pi requires the core.

Core 2.0.18 is IDF 4.4-based. This sketch uses only WiFi, NTP and FastLED, none
of which differ meaningfully from 3.x here.

## Implementation

Assuming the boot window and a local core, in order:

1. **Add OTA credentials to secrets.** New `OTA_PASSWORD` and `OTA_HOSTNAME` in
   `secrets.h.example`, mirrored into the local `secrets.h`. The device sits on
   the LAN; an unauthenticated OTA endpoint is not acceptable.

2. **New `ota.h`**, following the existing header conventions (`#pragma once`,
   `<Arduino.h>`, no include guards missing). Wraps `ArduinoOTA` — which ships
   with the ESP32 core, so no extra library dependency:
   - `ota_begin()` — set hostname, set password, register handlers, `begin()`
   - `ota_handle()` — cheap, safe to call every frame
   - Serial logging on start/end/error, matching the existing log style

3. **Add an OTA window to the sketch.** Keep Wi-Fi up after the boot sync
   rather than calling `disconnectWiFi()` immediately; run `ota_handle()` from
   `loop()`; drop the radio once the window expires. The window duration
   belongs next to the other tunables in `graphics.h`, or in a new config
   header if that feels like the wrong home.

   The window timer must use `uint32_t` — see the `millis()` overflow class of
   bug already fixed in this repo.

4. **Static IP rather than mDNS.** This Pi runs DNS for the network, and mDNS
   discovery is the least reliable part of an ESP32 OTA setup. A static lease
   or static IP in `secrets.h` avoids an entire category of "why can't I find
   the board" debugging.

5. **Confirm the image fits.** The default classic-ESP32 partition scheme
   provides two app slots, so OTA works without repartitioning, but the image
   must fit the ~1.2 MB slot. FastLED + Wi-Fi + OTA is expected to land well
   under that. **Unverified** — needs a real compiler to measure.

6. **Update the README** with the OTA flashing procedure and the window
   behaviour, and extend `tools/typecheck/stubs/` with `ArduinoOTA.h` so the
   type-check keeps working without a toolchain.

## Risks

- **A bad image soft-bricks the light.** Recovery is a USB flash. Keep the
  board physically accessible; do not seal the enclosure. ESP32 rollback is
  worth investigating but is not a substitute for physical access.
- **Flashing mid-animation.** OTA during an active crossfade leaves the panel
  in whatever state it was in until reboot. Cosmetic only.
- **The window is a small attack surface on the LAN.** Password-protected and
  time-boxed, but it is a real listener. Another reason to prefer the boot
  window over always-on.
- **Power draw during OTA.** Wi-Fi TX current spikes on top of 256 LEDs. The
  5 A budget in `graphics_init()` should absorb it, but a brownout mid-write is
  the worst possible time for one. Consider dimming the panel while an OTA is
  in progress.

## Verification

Nothing here is proven. In order:

1. `tools/typecheck/check.sh` passes with the OTA code in place
2. Sketch compiles for `esp32:esp32:esp32da`, image fits the app slot
3. USB flash of the OTA-capable firmware succeeds
4. Board is reachable at its static IP during the window, and unreachable after
5. An OTA push succeeds and the light comes back up in the correct mode
6. A deliberately interrupted OTA does not brick the board

## Prior art

The original code carried commented-out `ESPAsyncWebServer` and `ElegantOTA`
includes and a partial "keep Wi-Fi up when minute >= 30" block, so this was
attempted before. Those comments were removed when the sketch was rewritten;
they are recoverable from `~/aquarium-light-original-history.bundle`.

ElegantOTA gives a browser upload UI but pulls in a web server and costs more
flash and RAM. `ArduinoOTA` is push-based, ships with the core, and matches
"flash it from the Pi" better. Worth revisiting only if a browser UI is wanted.
