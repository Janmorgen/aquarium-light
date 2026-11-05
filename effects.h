#pragma once

#include <Arduino.h>
#include <FastLED.h>
#include <panel.h>
#include <variation.h>
#include <time_definitions.h>

// Live effects, laid over the composed frame every rendered frame. The looks
// in graphics.h decide what color the tank is; effects decide how it moves.
//
// Effects stack - each one runs in turn over the same frame buffer. What keeps
// that from becoming noise is that none of them is ever simply on or off:
//
//   - every effect owns a strength that eases toward where it is headed, at a
//     rate slow enough that a change takes minutes to arrive
//   - each rolls a new target level on its own randomized schedule, so no two
//     ever turn over at the same moment
//   - each carries a per-mode ceiling, so the mix that suits mid-day is not
//     the mix that suits evening - and because the ceiling feeds the same slew,
//     a mode change slides the effects across rather than switching them
//
// The result is a panel whose movement is always drifting somewhere, with
// nothing in it that arrives all at once.


// ---------- THE CLOCK ----------
// Effects run against their own clock rather than millis(), so test mode can
// wind them forward. At the real rate a bench sitting would show nothing at
// all: a full swing takes the better part of half an hour, and the focus
// rectangle sits still for minutes.
uint32_t effectsClock = 0;
uint8_t effectsSpeed = 1;

// 0 at the start of the lit day, 255 at the end of it. Set from the wall clock
// each frame; the sun rides on it.
uint8_t effectsDayProgress = 0;

void effects_set_speed(uint8_t speed) {
  effectsSpeed = (speed < 1) ? 1 : speed;
}


// ---------- PARAMETERS ----------
//
// Each effect owns a couple of knobs with sane defaults, adjustable at runtime
// over the API. They are plain 0-255 bytes so the whole set is one small table
// and one endpoint; what each means is the effect's own business.

#define MAX_EFFECT_PARAMS 3

struct ParamSpec {
  const char* name;
  uint8_t def;
};

// Indices into EFFECTS; the order must match the table further down.
enum EffectIndex : uint8_t {
  FX_WAVES, FX_CAUSTICS, FX_SWELL, FX_GLINTS, FX_SUN, FX_FOCUS, FX_COUNT
};

const ParamSpec EFFECT_PARAMS[FX_COUNT][MAX_EFFECT_PARAMS] = {
  /* waves    */ { { "depth", 210 }, { "width", 6 },   { "rate", 7 } },
  /* caustics */ { { "depth", 175 }, { "scale", 11 },  { "speed", 6 } },
  /* swell    */ { { "depth", 95 },  { "period", 90 }, { nullptr, 0 } },
  /* glints   */ { { "rate", 140 },  { "spread", 1 },  { "peak", 120 } },
  /* sun      */ { { "peak", 130 },  { "size", 7 },    { nullptr, 0 } },
  /* focus    */ { { "lift", 85 },   { "contrast", 255 }, { nullptr, 0 } },
};

uint8_t effectParam[FX_COUNT][MAX_EFFECT_PARAMS];

#define FXP(fx, n) (effectParam[fx][n])

void params_init() {
  for (uint8_t f = 0; f < FX_COUNT; ++f)
    for (uint8_t n = 0; n < MAX_EFFECT_PARAMS; ++n)
      effectParam[f][n] = EFFECT_PARAMS[f][n].def;
}

bool params_set(uint8_t fx, uint8_t n, uint8_t value) {
  if (fx >= FX_COUNT || n >= MAX_EFFECT_PARAMS) return false;
  if (EFFECT_PARAMS[fx][n].name == nullptr) return false;
  effectParam[fx][n] = value;
  return true;
}


// ---------- THE EFFECTS ----------
// Each takes a strength of 0-255 and scales its own magnitude by it, so 0 is a
// no-op and any effect can be faded all the way in or out.

// Soft dark bands drifting across the columns: light rippling through water.
struct DimWave {
  float center;  // current center column
  float speed;   // columns per frame
  bool active;
};

const int MAX_WAVES = 3;
DimWave dimWaves[MAX_WAVES] = {};

// `depth` is how dark the trough of a wave gets.
uint8_t waveDimScale(float center, int col, uint8_t depth) {
  float dist = abs((float)col - center);
  float radius = (float)FXP(FX_WAVES, 1);  // how wide the dip is
  if (radius < 1.0f) radius = 1.0f;
  if (dist >= radius) return 255;

  float t = dist / radius;      // 0 at center, 1 at edge
  float dip = 1.0f - (t * t);   // quadratic falloff, 1.0 at center
  uint8_t floorScale = 255 - depth;
  return (uint8_t)(floorScale + (255 - floorScale) * (1.0f - dip));
}

void fxDimWaves(uint8_t strength) {
  if (strength == 0) return;

  uint8_t depth = scale8(FXP(FX_WAVES, 0), strength);

  // Occasionally spawn a new wave from the left
  if (random(100) < FXP(FX_WAVES, 2)) {
    for (int i = 0; i < MAX_WAVES; i++) {
      if (!dimWaves[i].active) {
        dimWaves[i].center = -6.0f;                        // just off the left edge
        dimWaves[i].speed = 0.1f + random(0, 40) / 100.0f;  // 0.1 - 0.5 cols/frame
        dimWaves[i].active = true;
        break;
      }
    }
  }

  // Composite all active waves - take the darkest scale per column
  uint8_t colScale[COLS];
  for (int c = 0; c < COLS; c++) colScale[c] = 255;

  for (int i = 0; i < MAX_WAVES; i++) {
    if (!dimWaves[i].active) continue;
    dimWaves[i].center += dimWaves[i].speed;
    if (dimWaves[i].center > 38.0f) {  // gone past the right edge
      dimWaves[i].active = false;
      continue;
    }
    for (int c = 0; c < COLS; c++) {
      uint8_t s = waveDimScale(dimWaves[i].center, c, depth);
      if (s < colScale[c]) colScale[c] = s;  // darkest wins
    }
  }

  for (int c = 0; c < COLS; c++) {
    if (colScale[c] < 255) scaleRegion(c, c, 0, ROWS - 1, colScale[c]);
  }
}

// Two slow sine fields interfering: the wobbling net of light that lands on
// the bottom of a pool.
void fxCaustics(uint8_t strength) {
  if (strength == 0) return;

  uint32_t now = millis();
  uint16_t rate = 260 - (uint16_t)FXP(FX_CAUSTICS, 2) * 36;  // higher param, faster
  if (rate < 12) rate = 12;
  uint8_t phaseA = (uint8_t)(now / rate);
  uint8_t phaseB = (uint8_t)(now / (rate + rate / 2));
  uint8_t depth = scale8(FXP(FX_CAUSTICS, 0), strength);
  uint8_t freq = FXP(FX_CAUSTICS, 1);

  for (int col = 0; col < COLS; ++col) {
    for (int row = 0; row < ROWS; ++row) {
      uint8_t a = sin8((uint8_t)(col * freq + row * 5 + phaseA));
      uint8_t b = sin8((uint8_t)(col * (freq / 2) - row * 13 + phaseB));
      uint8_t net = scale8(a, b);  // brightest where both crests meet

      uint8_t scale = 255 - scale8(255 - net, depth);
      int i = ledIndex(col, row);
      leds[i] = CRGB(
        scale8(leds[i].r, scale),
        scale8(leds[i].g, scale),
        scale8(leds[i].b, scale));
    }
  }
}

// The whole panel breathing, about a minute and a half to the cycle.
void fxSwell(uint8_t strength) {
  if (strength == 0) return;

  // period is in seconds; sin8 wraps every 256 phase steps
  uint16_t periodSec = FXP(FX_SWELL, 1) ? FXP(FX_SWELL, 1) : 90;
  uint8_t wave = sin8((uint8_t)(millis() / ((uint32_t)periodSec * 1000 / 256)));
  uint8_t depth = scale8(FXP(FX_SWELL, 0), strength);
  scaleRegion(0, COLS - 1, 0, ROWS - 1, 255 - scale8(255 - wave, depth));
}

// Occasional bright flecks that fade out: the surface catching the light.
struct Glint {
  uint8_t col, row;
  uint8_t life;  // 255 at birth, 0 when spent
};

const int MAX_GLINTS = 16;
Glint glints[MAX_GLINTS] = {};

// Adds a warm lift to one pixel, clipped to the panel.
void glintPixel(int col, int row, uint8_t lift) {
  if (col < 0 || col >= COLS || row < 0 || row >= ROWS || lift == 0) return;
  int i = ledIndex(col, row);
  leds[i] = CRGB(qadd8(leds[i].r, lift),
                 qadd8(leds[i].g, lift),
                 qadd8(leds[i].b, scale8(lift, 200)));
}

void fxGlints(uint8_t strength) {
  if (strength == 0) return;

  if (random(1000) < 1 + scale8(FXP(FX_GLINTS, 0), strength)) {
    for (int i = 0; i < MAX_GLINTS; ++i) {
      if (glints[i].life == 0) {
        glints[i].col = (uint8_t)random(COLS);
        glints[i].row = (uint8_t)random(ROWS);
        glints[i].life = 255;
        break;
      }
    }
  }

  uint8_t peak = scale8(FXP(FX_GLINTS, 2), strength);
  uint8_t spread = FXP(FX_GLINTS, 1);

  for (int i = 0; i < MAX_GLINTS; ++i) {
    if (glints[i].life == 0) continue;

    uint8_t lift = scale8(peak, glints[i].life);
    int c = glints[i].col, r = glints[i].row;

    glintPixel(c, r, lift);
    if (spread >= 1) {
      uint8_t side = scale8(lift, 140);
      glintPixel(c - 1, r, side);
      glintPixel(c + 1, r, side);
      glintPixel(c, r - 1, side);
      glintPixel(c, r + 1, side);
    }
    if (spread >= 2) {
      uint8_t corner = scale8(lift, 70);
      glintPixel(c - 1, r - 1, corner);
      glintPixel(c + 1, r - 1, corner);
      glintPixel(c - 1, r + 1, corner);
      glintPixel(c + 1, r + 1, corner);
    }

    glints[i].life = (glints[i].life > 6) ? (uint8_t)(glints[i].life - 6) : 0;
  }
}

// A sun that crosses the panel with the day - just off the left edge at dawn,
// off the right at dusk - riding highest around the middle of the day. Its
// position comes from the wall clock, not from a timer, so it is where the
// real sun is rather than wherever the last roll left it.
void fxSun(uint8_t strength) {
  if (strength == 0) return;

  float t = effectsDayProgress / 255.0f;
  float sunCol = t * (COLS + 8.0f) - 4.0f;

  // 0 at both ends of the day, 1 in the middle of it.
  float arc = 1.0f - 4.0f * (t - 0.5f) * (t - 0.5f);
  float sunRow = (ROWS - 1) * (1.0f - arc) * 0.75f;  // row 0 is the top

  uint8_t peak = scale8(FXP(FX_SUN, 0), strength);
  float radiusCol = (float)FXP(FX_SUN, 1);
  if (radiusCol < 1.0f) radiusCol = 1.0f;   // an API-set 0 must not divide
  const float radiusRow = radiusCol * 0.55f;

  for (int col = 0; col < COLS; ++col) {
    for (int row = 0; row < ROWS; ++row) {
      float dc = ((float)col - sunCol) / radiusCol;
      float dr = ((float)row - sunRow) / radiusRow;
      float dist = sqrtf(dc * dc + dr * dr);
      if (dist >= 1.0f) continue;

      uint8_t lift = (uint8_t)(peak * (1.0f - dist * dist));
      int i = ledIndex(col, row);
      leds[i] = CRGB(
        qadd8(leds[i].r, lift),
        qadd8(leds[i].g, scale8(lift, 235)),
        qadd8(leds[i].b, scale8(lift, 180)));
    }
  }
}

// A bright rectangle that rests at one end of the board or the other, with
// every other pixel outside it turned down. It holds a position for minutes at
// a time and only travels between them - the movement exists to keep the
// change off the board from being a jump, not to be watched.
const uint8_t FOCUS_STOPS[][2] = { { 0, 9 }, { 11, 20 }, { 22, 31 } };
#define FOCUS_STOP_COUNT ARRAY_LEN(FOCUS_STOPS)

const uint16_t FOCUS_DWELL_MIN_SEC = 900;   // 15 minutes
const uint16_t FOCUS_DWELL_MAX_SEC = 1800;  // 30 minutes
const uint16_t FOCUS_TRAVEL_MIN_SEC = 60;
const uint16_t FOCUS_TRAVEL_MAX_SEC = 120;

uint8_t focusFrom = 0;
uint8_t focusTo = 0;
bool focusMoving = false;
uint32_t focusLegStart = 0;
uint32_t focusLegLength = 0;

// A commanded focus: park the rectangle at one stop, hold the effect on, and
// hand it back to the rolls when the time is up. The device owns the expiry so
// a dropped connection cannot leave the tank spotlit indefinitely.
bool focusCommanded = false;
uint32_t focusCommandUntil = 0;

uint32_t rollSeconds(uint16_t lo, uint16_t hi) {
  return (uint32_t)random(lo, (long)hi + 1) * 1000UL;
}

void focus_init() {
  focusFrom = (uint8_t)random(FOCUS_STOP_COUNT);
  focusTo = focusFrom;
  focusMoving = false;
  focusLegStart = effectsClock;
  focusLegLength = rollSeconds(FOCUS_DWELL_MIN_SEC, FOCUS_DWELL_MAX_SEC);
}

// Advances the dwell/travel state machine. Runs even at zero strength, so the
// rectangle is never found somewhere unexpected when the effect fades back in.
void focus_advance() {
  if ((effectsClock - focusLegStart) < focusLegLength) return;

  focusLegStart = effectsClock;
  if (focusMoving) {
    focusMoving = false;
    focusLegLength = rollSeconds(FOCUS_DWELL_MIN_SEC, FOCUS_DWELL_MAX_SEC);
    return;
  }

  focusFrom = focusTo;
  if (FOCUS_STOP_COUNT > 1) {
    while (focusTo == focusFrom) focusTo = (uint8_t)random(FOCUS_STOP_COUNT);
  }
  focusMoving = true;
  focusLegLength = rollSeconds(FOCUS_TRAVEL_MIN_SEC, FOCUS_TRAVEL_MAX_SEC);
}

void fxFocus(uint8_t strength) {
  focus_advance();
  if (strength == 0) return;

  // Smoothstep along the leg, so the rectangle leaves and arrives gently
  // rather than starting and stopping dead.
  float ease = 1.0f;
  if (focusMoving && focusLegLength > 0) {
    float p = (float)(effectsClock - focusLegStart) / (float)focusLegLength;
    p = constrain(p, 0.0f, 1.0f);
    ease = p * p * (3.0f - 2.0f * p);
  }

  float col0 = FOCUS_STOPS[focusFrom][0] + (FOCUS_STOPS[focusTo][0] - FOCUS_STOPS[focusFrom][0]) * ease;
  float col1 = FOCUS_STOPS[focusFrom][1] + (FOCUS_STOPS[focusTo][1] - FOCUS_STOPS[focusFrom][1]) * ease;

  uint8_t lift = scale8(FXP(FX_FOCUS, 0), strength);
  // what an off pixel keeps: nothing at all when contrast is at its maximum
  uint8_t keep = 255 - scale8(FXP(FX_FOCUS, 1), strength);

  for (int col = 0; col < COLS; ++col) {
    // Fractional coverage, so the edges of the rectangle do not snap from one
    // column to the next while it travels.
    float overlap = min(col1 + 0.5f, (float)col + 0.5f) - max(col0 - 0.5f, (float)col - 0.5f);
    float cov = constrain(overlap, 0.0f, 1.0f);

    for (int row = 0; row < ROWS; ++row) {
      int i = ledIndex(col, row);

      if (cov > 0.0f) {
        uint8_t inLift = (uint8_t)(lift * cov);
        leds[i] = CRGB(qadd8(leds[i].r, inLift),
                       qadd8(leds[i].g, inLift),
                       qadd8(leds[i].b, inLift));
      }

      // Outside the rectangle, every other pixel goes down.
      if (cov < 1.0f && ((col + row) & 1) == 0) {
        uint8_t scale = (uint8_t)(255 - (255 - keep) * (1.0f - cov));
        leds[i] = CRGB(scale8(leds[i].r, scale),
                       scale8(leds[i].g, scale),
                       scale8(leds[i].b, scale));
      }
    }
  }
}


// ---------- THE STACK ----------

typedef void (*EffectFn)(uint8_t strength);

struct EffectSpec {
  const char* name;
  EffectFn apply;
  uint8_t minLevel, maxLevel;      // range a new target is rolled within
  uint8_t chance;                  // odds out of 255 a reroll turns it on at all; else it heads to zero
  uint16_t minHoldSec, maxHoldSec; // how long a target is held before rerolling
  uint16_t rampSec;                // seconds a full 0-255 swing takes; nothing moves faster
  uint8_t modeCeiling[NUM_MODES];  // per-mode cap: -, morning, mid-day, afternoon, evening, night
};

// A target level for one stretch: either a rolled level, or nothing at all if
// the effect is one of the rare ones and this stretch is not its turn.
uint8_t rollLevel(const EffectSpec& fx) {
  if (!rollChance(fx.chance)) return 0;
  return (uint8_t)random(fx.minLevel, (long)fx.maxLevel + 1);
}

// Ramps are deliberately long - a quarter of an hour or more for a full swing,
// so no effect can move more than a few points in a minute - and no two share
// one, so a mode change slides them apart instead of moving them together.
// `odds` is a leftover from when all six ran at once and rarity had to come
// from somewhere. The cast provides it now - a seat is one effect in six - so
// these are all 255: an effect that has been given a seat should use it, not
// roll a zero and leave the panel running on one.
//
// Mid-day is the plant-growing window, so the effects that work by darkening -
// waves, caustics, swell, and focus dimming everything outside its rectangle -
// are held low there or kept off, and the light reaches the plants. The sun
// only adds light and keeps its full ceiling.
const EffectSpec EFFECTS[] = {
  // name        apply        level    odds   hold (s)     ramp   ceiling per mode
  { "waves",     fxDimWaves,  110, 255, 255,   600, 1800,   900,  { 0, 200,  80, 220, 140, 0 } },
  { "caustics",  fxCaustics,   60, 230, 255,   900, 2400,  1500,  { 0, 170,  80, 200,  90, 0 } },
  { "swell",     fxSwell,      80, 255, 255,  1200, 3000,  2400,  { 0, 255, 100, 220, 255, 0 } },
  { "glints",    fxGlints,      0, 200, 255,   720, 2100,  1200,  { 0, 120, 120, 160,  60, 0 } },
  { "sun",       fxSun,       150, 255, 255,   900, 2700,  1800,  { 0, 255, 255, 220, 120, 0 } },
  { "focus",     fxFocus,     130, 200, 255,  1800, 3600,  1200,  { 0,  80,   0, 110, 150, 0 } },
};

#define EFFECT_COUNT ARRAY_LEN(EFFECTS)

struct EffectState {
  int32_t levelQ8;      // current strength, 8.8 fixed point
  uint8_t target;       // level rolled for this stretch, before the ceiling
  uint32_t nextRollMs;
  int16_t override;     // -1 to follow the rolls, else a strength to hold
  bool enabled;         // off sends it to zero regardless of rolls or override
  uint16_t rampOverride; // 0 = the effect's own ramp; else seconds for this move
};

// ---------- THE CAST ----------
//
// At most two effects run at a time. Everything else is held at zero, and the
// pair rotates: a seat is emptied, and only once its occupant has actually
// faded out does a different effect take it. Emptying before filling is what
// holds the limit at two - a crossfade would briefly put three on the panel.
//
// A pinned effect is exempt. Someone who explicitly asked for one gets it
// whether or not the rotation would have picked it; the cast governs what the
// light does on its own.

#define EFFECT_SEATS 2
#define ROTATION_MIN_SEC 420
#define ROTATION_MAX_SEC 1080
#define ROTATION_FADE_SEC 75   // handovers move faster than the idle wander

int8_t castSeat[EFFECT_SEATS];
int8_t retiringSeat = -1;      // seat waiting for its occupant to reach zero
int8_t incomingEffect = -1;    // who takes it
uint32_t nextRotationMs = 0;

// Seconds a commanded strength gets to arrive in, against the 15-40 minutes an
// autonomous one takes. The slow ramps exist so the panel never changes
// abruptly on its own; someone who just pressed a button is a different case
// and should see it happen.
#define EFFECT_COMMAND_RAMP_SEC 4

EffectState effectState[EFFECT_COUNT];
uint32_t effectsLastUpdate = 0;

// Pins an effect to a strength, or -1 to hand it back to the rolls. The slew
// still applies, so even a commanded change arrives gradually rather than
// snapping the panel.
void effects_override(uint8_t index, int16_t level) {
  if (index >= EFFECT_COUNT) return;
  effectState[index].override = (level < 0) ? -1 : (level > 255 ? 255 : level);
  // Set for letting go as well as for pinning, so a released effect returns
  // promptly instead of drifting back over the next twenty minutes. It clears
  // when the level actually arrives - a window measured in seconds would be a
  // guess about how far the level has to travel and how fast frames are
  // coming, and both have been wrong.
  effectState[index].rampOverride = EFFECT_COMMAND_RAMP_SEC;
}

// Switching an effect off is a different statement from pinning it to 0: the
// pin is a level you chose, this is the effect not participating at all. Both
// arrive through the commanded slew, so neither snaps the panel.
void effects_set_enabled(uint8_t index, bool on) {
  if (index >= EFFECT_COUNT) return;
  if (effectState[index].enabled == on) return;
  effectState[index].enabled = on;
  effectState[index].rampOverride = EFFECT_COMMAND_RAMP_SEC;
}

bool effects_enabled(uint8_t index) {
  return (index < EFFECT_COUNT) ? effectState[index].enabled : false;
}

int16_t effects_override_of(uint8_t index) {
  return (index < EFFECT_COUNT) ? effectState[index].override : -1;
}

uint8_t effects_strength(uint8_t index) {
  return (index < EFFECT_COUNT) ? (uint8_t)(effectState[index].levelQ8 >> 8) : 0;
}

// Parks the focus rectangle at `stop` and holds it there at `level` for
// `seconds`, then releases it. Stops are 0 left, 1 middle, 2 right.
void effects_focus_at(uint8_t stop, uint16_t seconds, uint8_t level) {
  if (stop >= FOCUS_STOP_COUNT) return;

  focusFrom = stop;
  focusTo = stop;
  focusMoving = false;
  focusLegStart = effectsClock;
  // Outlast the command, so the dwell cannot expire and wander mid-spotlight.
  focusLegLength = (uint32_t)seconds * 1000UL + 60000UL;

  focusCommanded = true;
  focusCommandUntil = effectsClock + (uint32_t)seconds * 1000UL;
  effects_override(FX_FOCUS, level);

  Serial.print(F("Focus commanded: stop "));
  Serial.print(stop);
  Serial.print(F(" for "));
  Serial.print(seconds);
  Serial.println(F("s"));
}

uint8_t effects_focus_stop() {
  return focusTo;
}

bool effects_focus_commanded() {
  return focusCommanded;
}

uint8_t effects_param_count(uint8_t fx) {
  if (fx >= FX_COUNT) return 0;
  uint8_t n = 0;
  while (n < MAX_EFFECT_PARAMS && EFFECT_PARAMS[fx][n].name != nullptr) ++n;
  return n;
}

const char* effects_param_name(uint8_t fx, uint8_t n) {
  return (fx < FX_COUNT && n < MAX_EFFECT_PARAMS) ? EFFECT_PARAMS[fx][n].name : "";
}

uint8_t effects_param(uint8_t fx, uint8_t n) {
  return (fx < FX_COUNT && n < MAX_EFFECT_PARAMS) ? effectParam[fx][n] : 0;
}

uint8_t effects_count() {
  return EFFECT_COUNT;
}

const char* effects_name(uint8_t index) {
  return (index < EFFECT_COUNT) ? EFFECTS[index].name : "";
}

uint32_t rollHold(const EffectSpec& fx) {
  return (uint32_t)random(fx.minHoldSec, (long)fx.maxHoldSec + 1) * 1000UL;
}

// True while this effect is on its way out of a seat. It is still seated -
// the seat is not handed over until it has actually faded - but it is already
// headed for zero, and without this its goal stays non-zero, it never reaches
// zero, and the handover never completes.
bool effect_retiring(uint8_t i) {
  return retiringSeat >= 0 && castSeat[retiringSeat] == (int8_t)i;
}

bool effect_in_cast(uint8_t i) {
  for (uint8_t s = 0; s < EFFECT_SEATS; ++s)
    if (castSeat[s] == (int8_t)i) return true;
  return false;
}

// An effect the rotation may seat right now: switched on, not already seated,
// and not suppressed to nothing by this mode.
bool castCandidate(uint8_t i, int mode) {
  if (!effectState[i].enabled || effect_in_cast(i)) return false;
  uint8_t ceiling = EFFECTS[i].modeCeiling[(mode >= 0 && mode < NUM_MODES) ? mode : 0];
  return ceiling > 0;
}

int8_t pickCandidate(int mode) {
  uint8_t pool[EFFECT_COUNT];
  uint8_t n = 0;
  for (uint8_t i = 0; i < EFFECT_COUNT; ++i)
    if (castCandidate(i, mode)) pool[n++] = i;
  if (n == 0) return -1;
  return (int8_t)pool[random(n)];
}

// A seat whose occupant this mode suppresses to nothing, or -1. A scheduled
// mode change does not re-cast, so without this an effect the new mode turns
// off would sit in its seat at zero until a rotation happened to pick that
// seat - leaving the panel on one effect for up to the full rotation interval.
// A pinned effect is an explicit request and is left alone.
int8_t suppressedSeat(int mode) {
  uint8_t m = (mode >= 0 && mode < NUM_MODES) ? mode : 0;
  for (uint8_t s = 0; s < EFFECT_SEATS; ++s) {
    int8_t occ = castSeat[s];
    if (occ < 0 || effectState[occ].override >= 0) continue;
    if (EFFECTS[occ].modeCeiling[m] == 0) return (int8_t)s;
  }
  return -1;
}

uint32_t rollRotation() {
  return (uint32_t)random(ROTATION_MIN_SEC, ROTATION_MAX_SEC + 1) * 1000UL;
}

void effects_init() {
  params_init();
  effectsLastUpdate = millis();
  effectsClock = 0;
  uint32_t now = effectsClock;

  for (uint8_t i = 0; i < EFFECT_COUNT; ++i) {
    // Start at zero and let the first minutes of runtime ease everything up,
    // rather than opening on a full-strength stack.
    effectState[i].levelQ8 = 0;
    effectState[i].target = rollLevel(EFFECTS[i]);
    effectState[i].override = -1;
    effectState[i].enabled = true;
    effectState[i].rampOverride = 0;
    // Holds are rolled per effect, so their turnovers are staggered from the
    // first minute onward and never resynchronize.
    effectState[i].nextRollMs = now + rollHold(EFFECTS[i]);
  }

  // Seats are filled by effects_prime(), which knows the mode.
  for (uint8_t s = 0; s < EFFECT_SEATS; ++s) castSeat[s] = -1;
  retiringSeat = -1;
  incomingEffect = -1;
  nextRotationMs = now + rollRotation();

  focus_init();
}

// Where one effect is currently headed, in 8.8 fixed point.
int32_t effectGoalQ8(uint8_t i, int mode) {
  const EffectSpec& fx = EFFECTS[i];
  const EffectState& st = effectState[i];
  if (!st.enabled) return 0;
  // A pin is an explicit request and outranks the rotation.
  if (st.override >= 0) return (int32_t)st.override << 8;
  if (!effect_in_cast(i) || effect_retiring(i)) return 0;

  uint8_t ceiling = fx.modeCeiling[(mode >= 0 && mode < NUM_MODES) ? mode : 0];
  return (int32_t)scale8(st.target, ceiling) << 8;
}

// Puts every effect straight at the strength it belongs at for `mode`, with no
// ramp. For boot and for a correction: the slow ramps exist so the panel never
// changes abruptly on its own, and there is nothing to be abrupt against when
// the panel has only just lit - climbing from zero over the next half hour
// just means the light looks wrong for that half hour.
void effects_prime(int mode) {
  // Re-cast for this mode: an effect the mode suppresses to nothing should not
  // be holding a seat, and boot should open on a pair that suits the hour.
  retiringSeat = -1;
  incomingEffect = -1;
  for (uint8_t s = 0; s < EFFECT_SEATS; ++s) castSeat[s] = -1;
  for (uint8_t s = 0; s < EFFECT_SEATS; ++s) castSeat[s] = pickCandidate(mode);
  nextRotationMs = effectsClock + rollRotation();

  for (uint8_t i = 0; i < EFFECT_COUNT; ++i) {
    effectState[i].levelQ8 = effectGoalQ8(i, mode);
    effectState[i].rampOverride = 0;
  }
}

// Advances every effect's strength and applies the stack to the frame buffer.
// Call once per rendered frame, after the base frame is in place.
void effects_loop(int mode, uint8_t dayProgress) {
  effectsDayProgress = dayProgress;

  uint32_t real = millis();
  uint32_t dt = real - effectsLastUpdate;
  effectsLastUpdate = real;

  // A long block elsewhere - an NTP re-sync, say - must not let the strengths
  // lurch to catch up.
  if (dt > 200) dt = 200;

  dt *= effectsSpeed;
  effectsClock += dt;
  uint32_t now = effectsClock;

  // Rotation: empty a seat, then fill it once it is actually empty.
  if (retiringSeat >= 0) {
    int8_t leaving = castSeat[retiringSeat];
    if (leaving < 0 || effectState[leaving].levelQ8 == 0) {
      castSeat[retiringSeat] = incomingEffect;
      if (incomingEffect >= 0) effectState[incomingEffect].rampOverride = ROTATION_FADE_SEC;
      Serial.print(F("Effect seated: "));
      Serial.println(incomingEffect >= 0 ? EFFECTS[incomingEffect].name : "(none)");
      retiringSeat = -1;
      incomingEffect = -1;
      nextRotationMs = effectsClock + rollRotation();
    }
  } else {
    // A suppressed occupant is retired now, not at the next rotation.
    int8_t deadSeat = suppressedSeat(mode);
    bool due = (int32_t)(effectsClock - nextRotationMs) >= 0;
    int8_t next = (deadSeat >= 0 || due) ? pickCandidate(mode) : -1;
    if (next < 0) {
      if (due) nextRotationMs = effectsClock + rollRotation();   // nothing to swap in
    } else {
      retiringSeat = (deadSeat >= 0) ? deadSeat : (int8_t)random(EFFECT_SEATS);
      incomingEffect = next;
      int8_t leaving = castSeat[retiringSeat];
      if (leaving >= 0) effectState[leaving].rampOverride = ROTATION_FADE_SEC;
      Serial.print(F("Effect retiring: "));
      Serial.println(leaving >= 0 ? EFFECTS[leaving].name : "(empty seat)");
    }
  }

  // A commanded focus times out on the device's own clock.
  if (focusCommanded && (int32_t)(effectsClock - focusCommandUntil) >= 0) {
    focusCommanded = false;
    effects_override(FX_FOCUS, -1);
    focusLegStart = effectsClock;
    focusLegLength = rollSeconds(FOCUS_DWELL_MIN_SEC, FOCUS_DWELL_MAX_SEC);
    Serial.println(F("Focus released"));
  }

  for (uint8_t i = 0; i < EFFECT_COUNT; ++i) {
    const EffectSpec& fx = EFFECTS[i];
    EffectState& st = effectState[i];

    if ((int32_t)(now - st.nextRollMs) >= 0) {
      st.target = rollLevel(fx);
      st.nextRollMs = now + rollHold(fx);
    }

    // Where this effect is headed: its rolled level, capped by what this mode
    // allows. Both can change at any time; the slew below is what stops either
    // from arriving abruptly.
    int32_t goalQ8 = effectGoalQ8(i, mode);

    uint16_t rampSec = st.rampOverride ? st.rampOverride : fx.rampSec;
    int32_t maxStep = ((int32_t)255 << 8) * (int32_t)dt / ((int32_t)rampSec * 1000);
    if (maxStep < 1) maxStep = 1;

    int32_t delta = goalQ8 - st.levelQ8;
    if (delta > maxStep) delta = maxStep;
    if (delta < -maxStep) delta = -maxStep;
    st.levelQ8 += delta;

    // Arrived: hand it back to the slow rules.
    if (st.rampOverride && st.levelQ8 == goalQ8) st.rampOverride = 0;

    fx.apply((uint8_t)(st.levelQ8 >> 8));
  }
}

// Current strength of each effect, for logging.
void effects_report() {
  Serial.print(F("Effects:"));
  for (uint8_t i = 0; i < EFFECT_COUNT; ++i) {
    Serial.print(" ");
    Serial.print(EFFECTS[i].name);
    Serial.print("=");
    Serial.print((int)(effectState[i].levelQ8 >> 8));
  }
  Serial.println();
}
