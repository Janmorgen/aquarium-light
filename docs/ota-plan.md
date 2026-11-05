# Wireless (OTA) flashing — implemented

**Status:** built and verified on hardware, 2026-08-26. An image was compiled,
pushed over the network to a board that was never touched, and came back up
serving its control page. The procedure lives in the README; this file records
what the plan got right, what it got wrong, and the decisions taken.

## The two open decisions

### Decision 1: when is the radio up? — **always on**

The plan recommended a boot window and called always-on "the biggest
behavioural change". That recommendation was overtaken: the light also has to
be controllable and monitorable over Wi-Fi, and neither works through a window
that closes. A boot window would have meant power-cycling the tank light to
change its brightness.

So the radio stays up, at roughly 80 mA continuous. What the plan listed as the
cost of always-on — that the daily re-sync logic becomes redundant — turned out
to be a simplification rather than a loss: `syncTime()` no longer has to bring
the radio back, it just asks.

The plan's other worry, that always-on is a permanent listener on the LAN, is
real and unchanged. Both listeners are password-protected.

### Decision 2: does the Pi build, or only flash? — **unchanged, still open**

Nothing here forces it. `espota.py` is ~10 KB and pushes an image built
elsewhere; a local core is ~1 GB and builds too. The device end is identical
either way.

## What the plan got wrong

- **Step 5 said the image "is expected to land well under" the ~1.2 MB slot,
  marked unverified.** It does fit — 1.13 MB, 86% — but 86% is not "well
  under", and that is with the default scheme's 1.2 MB slots.

  The scheme is now pinned to `no_fs` (2 MB app x2, no filesystem) in
  `sketch.yaml`, which puts the same image at 55%. **This decision cannot be
  deferred.** The partition table is written at `0x8000` by esptool and OTA only
  ever writes the inactive app slot, so the scheme is the one thing a wireless
  push can never change. There is exactly one mandatory USB flash — that is
  when to get it right.

- **Step 4 preferred a static IP over mDNS, on reliability grounds.** Static
  addressing is supported (`NET_STATIC_IP` in `secrets.h`, commented out) and is
  still the recommendation. But `ArduinoOTA.begin()` starts mDNS regardless —
  it registers its own service — so avoiding mDNS was never actually on the
  table. Verified on DHCP.

- **"Consider dimming the panel while an OTA is in progress"** was listed under
  Risks as optional. It is not really optional and it is not really a choice:
  `ArduinoOTA.handle()` blocks until the upload finishes, so the render loop
  stops on its own. `ota.h` draws a progress bar from the progress callback,
  which is the only code still running.

## What the plan got right

- Wiring up the library was the easy part; making the device reachable was the
  substance.
- One USB flash was unavoidable.
- The window timer had to be `uint32_t` — moot now that there is no window.
- Extending `tools/typecheck/stubs/` kept the type-check working. It needed
  `ArduinoOTA.h`, `WebServer.h`, a `String`, and additions to `WiFi.h`.

## Verification performed

1. `tools/typecheck/check.sh` passes — yes
2. Compiles for `esp32:esp32:esp32da`, image fits — yes, 55% of a 2 MB slot
3. USB flash of the OTA-capable firmware — yes
4. Board reachable at its address — yes, and it stays reachable, which is now
   the point rather than something to avoid
5. An OTA push succeeds and the light comes back in the correct mode — yes;
   uptime reset to 0, clock re-synced, mode restored
6. A deliberately interrupted OTA does not brick the board — **not tested.**
   The two app slots make this survivable by design (a failed write leaves the
   running slot untouched), but it has not been proven on this board.
