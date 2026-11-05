#pragma once

#include <Arduino.h>
#include <FastLED.h>
#include <panel.h>
#include <variation.h>
#include <effects.h>
#include <time_definitions.h>

// ---------- CONFIG ----------
#define DATA_PIN 26  // change to your pin
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB
#define BRIGHTNESS 50  // global max (0-255)
#define MODE_CROSSFADE_MS 1800000UL  // 30 minutes
// Share of a transition spent staggering rows down the panel; the remainder is
// each row's own fade, so the last row still lands exactly on the full
// duration rather than overrunning it.
#define TRANSITION_CASCADE_PERCENT 15
// Test mode runs on its own, much faster clock: at the real 30-minute
// crossfades a full pass would take two and a half hours.
#define TEST_CROSSFADE_MS 8000
#define TEST_MODE_DWELL_MS 12000
// How much faster the effect stack drifts in test mode.
#define TEST_EFFECTS_SPEEDUP 40
// ----------------------------

CRGB targetBuf[NUM_LEDS];
CRGB prevBuf[NUM_LEDS];
// The crossfade's own output, before dim waves are laid over it.
CRGB baseBuf[NUM_LEDS];
uint32_t lastTransitionMillis = 0;
uint32_t transitionDuration = MODE_CROSSFADE_MS;
// Which end of the panel the fade starts from, rolled per transition.
bool cascadeFromTop = true;

// ---------- OVERLAY COMPOSITING ----------
//
// A look is not painted directly. It is stacked out of overlays from a shared
// palette, each applied at its own opacity, and then passed through filters
// that reshape the finished pixels - dimming a region, restricting the light
// to one part of the board, or switching the rest off entirely.
//
// Building the modes this way means a new look is a recipe rather than new
// drawing code, and it is where day-to-day variation comes from: opacities
// carry a jitter, regions can drift, and optional elements carry odds - all
// rolled fresh each time a transition into that mode starts.

// How a value is shaped across a region. Used both for an overlay's alpha and
// for the scale a filter applies.
enum AlphaShape : uint8_t {
  SHAPE_FLAT,      // the peak value everywhere in the region
  SHAPE_GRADIENT,  // ramps peak -> edge across the columns
  SHAPE_FALLOFF,   // peak at the column center, edge at both ends, quadratic
  SHAPE_RADIAL,    // peak at the region center in both axes - a soft spot
};

enum BlendOp : uint8_t {
  BLEND_OVER,  // alpha-over: fades what is under it toward this overlay
  BLEND_ADD,   // adds on top, saturating - for boosts and highlights
};

// One entry in the palette. `level` is how bright the color sits when the
// overlay is fully applied, so a recipe can talk in opacities without every
// color needing to be pre-dimmed.
struct Overlay {
  CRGB color;
  uint8_t level;
  uint8_t col0, col1;
  uint8_t row0, row1;
  AlphaShape shape;
  uint8_t maskPeak;  // alpha at the center / start of the region
  uint8_t maskEdge;  // alpha at its edge, for the shaped forms
};

enum OverlayId : uint8_t {
  OV_BLUE_SKY,
  OV_SUN_CORE,
  OV_DAY_WHITE,
  OV_DAY_CROWN,
  OV_SUN_SPOT,
  OV_COOL_EDGE,
  OV_WARM_WASH,
  OV_AMBER_EDGE,
  OV_DEEP_WARM,
  OV_MOONLIGHT,
  OV_COUNT
};

// `level` sits a little above the brightness each overlay is meant to show at,
// and the recipes below run it at roughly 90% opacity. That leaves headroom
// for a roll to land above the nominal look as well as below it - pinning
// opacity at 255 would let jitter dim the panel but never lift it.
const Overlay OVERLAYS[OV_COUNT] = {
  // color                level  col0 col1  row0 row1  shape           peak edge
  { CRGB(60, 100, 255),   57,    0,   31,   0,   7,    SHAPE_FLAT,     255, 255 },  // OV_BLUE_SKY
  { CRGB(220, 210, 200),  100,   10,  21,   0,   7,    SHAPE_FALLOFF,  255, 0   },  // OV_SUN_CORE
  { CRGB(255, 245, 230),  255,   0,   31,   0,   7,    SHAPE_FLAT,     255, 255 },  // OV_DAY_WHITE
  { CRGB(255, 245, 230),  45,    8,   23,   0,   7,    SHAPE_FALLOFF,  255, 0   },  // OV_DAY_CROWN
  { CRGB(255, 250, 240),  60,    11,  20,   1,   6,    SHAPE_RADIAL,   255, 0   },  // OV_SUN_SPOT (spare)
  { CRGB(80, 120, 255),   70,    0,   8,    0,   7,    SHAPE_GRADIENT, 255, 0   },  // OV_COOL_EDGE
  { CRGB(255, 230, 200),  157,   0,   31,   0,   7,    SHAPE_FLAT,     255, 255 },  // OV_WARM_WASH
  { CRGB(255, 140, 80),   70,    0,   8,    0,   7,    SHAPE_GRADIENT, 255, 0   },  // OV_AMBER_EDGE
  { CRGB(255, 120, 80),   72,    0,   31,   0,   7,    SHAPE_FLAT,     255, 255 },  // OV_DEEP_WARM
  { CRGB(10, 20, 60),     45,    0,   31,   0,   7,    SHAPE_FLAT,     255, 255 },  // OV_MOONLIGHT
};

// One overlay in a recipe.
//   jitter  +/- range the opacity is rolled within
//   chance  odds out of 255 that the layer is used at all; 255 is always
//   drift   +/- columns the overlay's region may slide, rolled per transition
struct Layer {
  OverlayId overlay;
  uint8_t opacity;
  uint8_t jitter;
  BlendOp op;
  uint8_t chance;
  int8_t drift;
};

// A filter scales pixels that are already composed. Its region is the shape it
// acts on: `inside` applies within it, shaped by `shape`, and `outside`
// applies to everything else. That second field is what restricts a look to
// part of the board - set it to 0 and only the region stays lit.
//
//   whole-panel dim     region = the panel,  shape FLAT,    inside 200
//   vignette            region = the panel,  shape FALLOFF, inside 255, insideEnd 190
//   focus on the middle region = the middle, shape FLAT,    inside 255, outside 90
//   middle only         region = the middle, shape FLAT,    inside 255, outside 0
//   soft spot           region = a block,    shape RADIAL,  inside 255, insideEnd 0, outside 60
//   off                 inside 0 and outside 0
struct Filter {
  AlphaShape shape;
  uint8_t col0, col1;
  uint8_t row0, row1;
  uint8_t inside;     // scale within the region: peak for the shaped forms
  uint8_t insideEnd;  // scale at the region's edge, for the shaped forms
  uint8_t outside;    // scale everywhere else; 255 leaves it untouched
  uint8_t jitter;     // +/- range `inside` is rolled within
  uint8_t chance;     // odds out of 255 that the filter runs at all
};

struct Look {
  const Layer* layers;
  uint8_t layerCount;
  const Filter* filters;
  uint8_t filterCount;
};

// Linear interpolation between two 0-255 endpoints, at `t` (0-255) along.
uint8_t rampAt(uint8_t from, uint8_t to, uint8_t t) {
  return (uint8_t)((int)from + (((int)to - (int)from) * (int)t) / 255);
}

// How far along a region a column sits, 0-255.
uint8_t regionProgress(int col0, int col1, int col) {
  int span = col1 - col0;
  if (span <= 0) return 0;
  int t = ((col - col0) * 255) / span;
  return (uint8_t)constrain(t, 0, 255);
}

// `peak` at the center, `edge` from the rim outward, quadratic between.
uint8_t quadFade(uint8_t peak, uint8_t edge, float dist) {
  if (dist >= 1.0f) return edge;
  float k = 1.0f - dist * dist;
  return (uint8_t)((float)edge + ((float)peak - (float)edge) * k);
}

// The shaped value at one pixel of a region. Shared by overlays, which use it
// as an alpha, and filters, which use it as a scale.
uint8_t shapeMask(AlphaShape shape, uint8_t peak, uint8_t edge,
                  int col0, int col1, int row0, int row1, int col, int row) {
  switch (shape) {
    case SHAPE_GRADIENT:
      return rampAt(peak, edge, regionProgress(col0, col1, col));

    case SHAPE_FALLOFF: {
      float center = (col0 + col1) / 2.0f;
      float radius = (col1 - col0) / 2.0f + 0.5f;
      float dist = (radius > 0.0f) ? abs((float)col - center) / radius : 0.0f;
      return quadFade(peak, edge, dist);
    }

    case SHAPE_RADIAL: {
      float colCenter = (col0 + col1) / 2.0f;
      float rowCenter = (row0 + row1) / 2.0f;
      float colRadius = (col1 - col0) / 2.0f + 0.5f;
      float rowRadius = (row1 - row0) / 2.0f + 0.5f;
      float dc = (colRadius > 0.0f) ? abs((float)col - colCenter) / colRadius : 0.0f;
      float dr = (rowRadius > 0.0f) ? abs((float)row - rowCenter) / rowRadius : 0.0f;
      return quadFade(peak, edge, sqrtf(dc * dc + dr * dr));
    }

    case SHAPE_FLAT:
    default:
      return peak;
  }
}

void applyLayer(const Layer& layer) {
  if (!rollChance(layer.chance)) return;

  const Overlay& ov = OVERLAYS[layer.overlay];

  uint8_t opacity = jittered(layer.opacity, layer.jitter);
  if (opacity == 0) return;

  // Slide the region a few columns, so a feature like the sun core does not
  // sit in exactly the same place every day.
  int shift = (layer.drift > 0)
                ? (int)random(-(long)layer.drift, (long)layer.drift + 1)
                : 0;
  int col0 = constrain((int)ov.col0 + shift, 0, COLS - 1);
  int col1 = constrain((int)ov.col1 + shift, 0, COLS - 1);

  // The overlay's color as it looks at full strength; opacity works from here.
  CRGB tint = CRGB(scale8(ov.color.r, ov.level),
                   scale8(ov.color.g, ov.level),
                   scale8(ov.color.b, ov.level));

  for (int col = col0; col <= col1; ++col) {
    for (int row = ov.row0; row <= ov.row1; ++row) {
      uint8_t alpha = scale8(
        shapeMask(ov.shape, ov.maskPeak, ov.maskEdge,
                  col0, col1, ov.row0, ov.row1, col, row),
        opacity);
      if (alpha == 0) continue;

      int i = ledIndex(col, row);
      if (layer.op == BLEND_ADD) {
        targetBuf[i] = CRGB(
          qadd8(targetBuf[i].r, scale8(tint.r, alpha)),
          qadd8(targetBuf[i].g, scale8(tint.g, alpha)),
          qadd8(targetBuf[i].b, scale8(tint.b, alpha)));
      } else {
        targetBuf[i] = blend(targetBuf[i], tint, alpha);
      }
    }
  }
}

void applyFilter(const Filter& filter) {
  if (!rollChance(filter.chance)) return;

  uint8_t inside = jittered(filter.inside, filter.jitter);

  for (int col = 0; col < COLS; ++col) {
    bool inCols = (col >= filter.col0 && col <= filter.col1);

    for (int row = 0; row < ROWS; ++row) {
      bool within = inCols && row >= filter.row0 && row <= filter.row1;

      uint8_t scale = within
        ? shapeMask(filter.shape, inside, filter.insideEnd,
                    filter.col0, filter.col1, filter.row0, filter.row1, col, row)
        : filter.outside;
      if (scale == 255) continue;

      int i = ledIndex(col, row);
      targetBuf[i] = CRGB(
        scale8(targetBuf[i].r, scale),
        scale8(targetBuf[i].g, scale),
        scale8(targetBuf[i].b, scale));
    }
  }
}

// Builds a look into targetBuf: clear, stack the layers, then filter.
//
// Everything here is settled once, when a transition into the mode starts, and
// then holds still for the length of that mode. Nothing in a look moves - all
// movement on the panel comes from the effect stack in effects.h.
void composeLook(const Look& look) {
  for (int i = 0; i < NUM_LEDS; i++) targetBuf[i] = CRGB::Black;
  for (uint8_t i = 0; i < look.layerCount; ++i) applyLayer(look.layers[i]);
  for (uint8_t i = 0; i < look.filterCount; ++i) applyFilter(look.filters[i]);
}


// ---------- THE LOOKS ----------
// Recipes only. To change how a time of day reads, change an opacity, swap an
// overlay, or add a filter - no pixel code involved.

// Dim blue across the panel with the sun burning a soft warm hole in the
// middle of it, sitting a few columns either way from day to day.
const Layer MORNING_LAYERS[] = {
  // overlay      opacity jitter op          chance drift
  { OV_BLUE_SKY,  230,    35,    BLEND_OVER, 255,   0 },
  { OV_SUN_CORE,  228,    35,    BLEND_OVER, 255,   4 },
};

const Filter MORNING_FILTERS[] = {
  // Some mornings the light gathers toward the middle and the ends fall away.
  { SHAPE_FALLOFF, 0, 31, 0, 7, 255, 175, 255, 25, 110 },
};

// Bright and even, lifted in the center, cool along the left edge.
const Layer MIDDAY_LAYERS[] = {
  { OV_DAY_WHITE, 228,    30,    BLEND_OVER, 255,   0 },
  { OV_DAY_CROWN, 227,    60,    BLEND_ADD,  255,   3 },
  { OV_COOL_EDGE, 230,    55,    BLEND_ADD,  255,   0 },
};

// The same shape gone warm, with the ends pulled down so the light reads as
// low and angled.
const Layer AFTERNOON_LAYERS[] = {
  { OV_WARM_WASH,  228,   30,    BLEND_OVER, 255,   0 },
  { OV_AMBER_EDGE, 230,   55,    BLEND_ADD,  255,   0 },
};

const Filter AFTERNOON_FILTERS[] = {
  { SHAPE_GRADIENT, 0,  5,  0, 7, 210, 255, 255, 20, 255 },
  { SHAPE_GRADIENT, 26, 31, 0, 7, 255, 210, 255, 20, 255 },
};

const Layer EVENING_LAYERS[] = {
  { OV_DEEP_WARM, 227,    45,    BLEND_OVER, 255,   0 },
};

const Filter EVENING_FILTERS[] = {
  // The last of the light draws into the middle of the tank, the ends going
  // down to a low glow. Roughly one evening in three.
  { SHAPE_FLAT, 9, 22, 0, 7, 255, 255, 95, 20, 85 },
};

// Moonlight is composed and then switched off. Delete the filter to run it.
const Layer NIGHT_LAYERS[] = {
  { OV_MOONLIGHT, 255,    0,     BLEND_OVER, 255,   0 },
};

const Filter NIGHT_FILTERS[] = {
  { SHAPE_FLAT, 0, 31, 0, 7, 0, 0, 0, 0, 255 },
};

const Look LOOKS[NUM_MODES] = {
  { NIGHT_LAYERS,     ARRAY_LEN(NIGHT_LAYERS),     NIGHT_FILTERS,     ARRAY_LEN(NIGHT_FILTERS)     },  // 0: unused
  { MORNING_LAYERS,   ARRAY_LEN(MORNING_LAYERS),   MORNING_FILTERS,   ARRAY_LEN(MORNING_FILTERS)   },
  { MIDDAY_LAYERS,    ARRAY_LEN(MIDDAY_LAYERS),    nullptr,           0                            },
  { AFTERNOON_LAYERS, ARRAY_LEN(AFTERNOON_LAYERS), AFTERNOON_FILTERS, ARRAY_LEN(AFTERNOON_FILTERS) },
  { EVENING_LAYERS,   ARRAY_LEN(EVENING_LAYERS),   EVENING_FILTERS,   ARRAY_LEN(EVENING_FILTERS)   },
  { NIGHT_LAYERS,     ARRAY_LEN(NIGHT_LAYERS),     NIGHT_FILTERS,     ARRAY_LEN(NIGHT_FILTERS)     },
};


// Snapshots the current frame as the crossfade's starting point.
void startModeTransition(uint32_t duration_ms) {
  transitionDuration = duration_ms;
  lastTransitionMillis = millis();
  // Half the time the change sweeps up the panel instead of down.
  cascadeFromTop = (random(2) == 0);
  // Snapshot the crossfade's own output rather than the visible frame: leds
  // carries the dim waves too, and freezing one of those into prevBuf would
  // park a dark band on the panel for the length of the fade - half an hour,
  // now that transitions run that long.
  for (int i = 0; i < NUM_LEDS; i++) prevBuf[i] = baseBuf[i];
}

// Declares the current frame finished: no crossfade is running.
//
// Boot has to do this. graphics_setup() lands straight on the composed frame,
// but applyTransition() still runs every loop, and it reads prevBuf and the
// timer - which on a fresh boot are black and zero. Left alone it fades in
// from darkness over the full crossfade, which is a half-hour sunrise at
// whatever time the light happened to start.
void graphics_settle() {
  for (int i = 0; i < NUM_LEDS; i++) prevBuf[i] = targetBuf[i];
  lastTransitionMillis = millis();
  transitionDuration = 0;
}

// Crossfades prevBuf -> targetBuf.
//
// Two effects are layered on top of a plain fade:
//   1. Channels move in sequence rather than together: blue first, then
//      green, then red. The 0..765 range is three back-to-back 0..255 ramps.
//   2. Rows are staggered, so the fade cascades down the panel.
void applyTransition() {
  uint32_t now = millis();
  uint32_t globalElapsed = now - lastTransitionMillis;
  const uint16_t maxT = 765;  // total transition range (3 x 255)

  // Rows are staggered across TRANSITION_CASCADE_PERCENT of the transition and
  // each row fades over the rest, so the last row finishes exactly at
  // transitionDuration instead of running past it.
  uint32_t cascadeMs = (transitionDuration / 100) * TRANSITION_CASCADE_PERCENT;
  uint32_t rowDelayMs = (ROWS > 1) ? (cascadeMs / (ROWS - 1)) : 0;
  uint32_t fadeDuration = transitionDuration - cascadeMs;
  if (fadeDuration == 0) fadeDuration = 1;

  for (int col = 0; col < COLS; col++) {
    for (int row = 0; row < ROWS; row++) {
      int idx = ledIndex(col, row);
      // per-row elapsed with delay so the leading row starts at 0, the next at
      // rowDelayMs, and so on down (or up) the panel.
      int step = cascadeFromTop ? row : (ROWS - 1 - row);
      int64_t rowElapsed = (int64_t)globalElapsed - (int64_t)(step * rowDelayMs);
      if (rowElapsed <= 0) {
        // not started yet: show prevBuf
        baseBuf[idx] = prevBuf[idx];
        continue;
      } else if ((uint32_t)rowElapsed >= fadeDuration) {
        // finished: show target
        baseBuf[idx] = targetBuf[idx];
        continue;
      }

      // map rowElapsed (0..fadeDuration) to t (0..maxT). 64-bit because a
      // half-hour fade times 765 overflows a uint32_t at longer durations.
      uint16_t t = (uint16_t)(((uint64_t)rowElapsed * (uint64_t)maxT) / (uint64_t)fadeDuration);

      uint8_t tPhase1 = (t < 255) ? t : 255;
      uint8_t tPhase2 = (t < 255) ? 0 : min((int)t - 255, 255);
      uint8_t tPhase3 = (t < 510) ? 0 : min((int)t - 510, 255);

      baseBuf[idx].b = scale8(prevBuf[idx].b, 255 - tPhase1) + scale8(targetBuf[idx].b, tPhase1);
      baseBuf[idx].g = scale8(prevBuf[idx].g, 255 - tPhase2) + scale8(targetBuf[idx].g, tPhase2);
      baseBuf[idx].r = scale8(prevBuf[idx].r, 255 - tPhase3) + scale8(targetBuf[idx].r, tPhase3);
    }
  }

  // Hand the result to the frame buffer; dim waves are laid over it after.
  for (int i = 0; i < NUM_LEDS; i++) leds[i] = baseBuf[i];
}

// Mode setter
void setMode(int mode, uint32_t crossfadeMs = MODE_CROSSFADE_MS) {
  startModeTransition(crossfadeMs);
  // Composed here, at the start of the transition, so the panel spends the
  // whole fade moving toward the mix it will hold for the rest of the block.
  if (mode < 0 || mode >= NUM_MODES) mode = 5;
  composeLook(LOOKS[mode]);

  // immediate first frame
  applyTransition();
}


// Setup
int currentMode = -1;

// Brings up the panel. maxMilliamps is the supply budget FastLED is allowed to
// draw from - see powerBudgetMa() in power.h, which picks it from whether the
// board is on USB or on the external supply.
//
// This is the only place addLeds() may be called; calling it twice registers
// the strip twice.
void graphics_init(uint32_t maxMilliamps) {

  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  //FastLED.setBrightness(BRIGHTNESS);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, (int)maxMilliamps);
  FastLED.clear();
  FastLED.show();

  effects_init();
}

// Re-budgets a running panel, for USB being plugged in or pulled out.
void graphics_set_power_budget(uint32_t maxMilliamps) {
  FastLED.setMaxPowerInVoltsAndMilliamps(5, (int)maxMilliamps);
}

// Master brightness. FastLED applies this before its power limiting, so the
// current cap still holds whatever this is set to.
void graphics_set_brightness(uint8_t value) {
  FastLED.setBrightness(value);
}

int graphics_current_mode() {
  return currentMode;
}

// How far the running crossfade has got, 0-100. 100 means nothing is moving.
uint8_t graphics_transition_progress() {
  uint32_t elapsed = millis() - lastTransitionMillis;
  if (transitionDuration == 0 || elapsed >= transitionDuration) return 100;
  return (uint8_t)((elapsed * 100UL) / transitionDuration);
}

uint32_t graphics_transition_duration() {
  return transitionDuration;
}

void graphics_setup(int hour, int minute) {

  // initialize with current mode immediately
  currentMode = timeMode(hour, minute);
  Serial.println();
  Serial.print(F("Starting with mode: "));
  Serial.println(currentMode);
  Serial.print(F("Hour: "));
  Serial.print(hour);
  Serial.print(F(" Minute: "));
  Serial.println(minute);

  composeLook(LOOKS[currentMode]);
  for (int i = 0; i < NUM_LEDS; i++) { leds[i] = targetBuf[i]; baseBuf[i] = targetBuf[i]; }
  graphics_settle();
  effects_prime(currentMode);
  FastLED.show();
}

int testModeCounter = 1;
uint32_t last_season_change = 0;

// Renders one frame of `mode`.
//
// The mode is decided by the caller rather than read off the clock here, so a
// commanded override and the schedule take exactly the same path in - and
// crossfadeMs comes in with it, because a change someone asked for should land
// in seconds while a scheduled one takes half an hour.
void graphics_loop(int mode, uint8_t progress, uint32_t crossfadeMs) {

  if (mode != currentMode) {
    currentMode = mode;
    Serial.println();
    Serial.print(F("Switching to mode: "));
    Serial.print(currentMode);
    Serial.print(F(" over "));
    Serial.print(crossfadeMs / 1000);
    Serial.println(F("s"));
    setMode(currentMode, crossfadeMs);
    last_season_change = millis();
  }

  applyTransition();
  effects_loop(currentMode, progress);
  panel_apply_overrides();
  FastLED.show();
  panelCountFrame();
  delay(40);
}


// Test mode does not read the clock - it starts at morning and steps through
// the modes on a timer, so it works with no NTP sync and no network in reach.
void graphics_setup_test() {

  testModeCounter = 0;
  currentMode = 1;
  last_season_change = millis();

  Serial.println();
  Serial.println(F("Test mode: cycling modes"));
  effects_prime(currentMode);

  // Crossfading in works from a cleared panel at boot and from whatever the
  // clock mode was showing when USB got plugged in.
  setMode(currentMode, TEST_CROSSFADE_MS);
}

// Cycles through every mode on a timer instead of following the clock.
void graphics_loop_test() {
  uint32_t currentTime = millis();
  if ((currentTime - last_season_change) > TEST_MODE_DWELL_MS) {
    testModeCounter++;
    last_season_change = currentTime;
  }

  // 1-5, so the cycle stays inside the real modes rather than passing through
  // setMode()'s default case.
  int m = 1 + (testModeCounter % 5);

  if (m != currentMode) {
    currentMode = m;
    Serial.print(F("Season: "));
    Serial.println(m);
    setMode(currentMode, TEST_CROSSFADE_MS);
  }

  applyTransition();
  // Run a whole day past the sun in about a minute.
  effects_loop(currentMode, (uint8_t)((millis() / 235) & 0xFF));

  panel_apply_overrides();
  FastLED.show();
  panelCountFrame();
  delay(40);
}
