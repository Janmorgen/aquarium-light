#pragma once

#include <Arduino.h>
#include <FastLED.h>
#include <time_definitions.h>

// ---------- CONFIG ----------
#define ROWS 8
#define COLS 32
#define NUM_LEDS (ROWS * COLS)
#define DATA_PIN 26  // change to your pin
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB
#define BRIGHTNESS 50  // global max (0-255)
#define MODE_CROSSFADE_MS 60000
// ----------------------------

CRGB leds[NUM_LEDS];
CRGB targetBuf[NUM_LEDS];
CRGB prevBuf[NUM_LEDS];
uint32_t lastTransitionMillis = 0;
uint32_t transitionDuration = MODE_CROSSFADE_MS;

// The panel is wired as a column-major serpentine: column 0 runs top to
// bottom, column 1 bottom to top, and so on, ROWS pixels per column.
//
// ledIndex() is the ONLY correct way to address a pixel. Every helper below
// takes coordinates in (column, row) order to match it.
inline int ledIndex(int col, int row) {
  col = constrain(col, 0, COLS - 1);
  row = constrain(row, 0, ROWS - 1);

  if (col % 2 == 0) {
    return ROWS * col + row;
  }
  return ROWS * col + (ROWS - 1 - row);
}

// Adds `color` (scaled by `scale`) on top of whatever is already in targetBuf.
void addRegionScaled(int col0, int col1, int row0, int row1, CRGB color, uint8_t scale) {
  for (int col = col0; col <= col1; ++col)
    for (int row = row0; row <= row1; ++row) {
      int i = ledIndex(col, row);
      targetBuf[i] = CRGB(
        qadd8(targetBuf[i].r, scale8(color.r, scale)),
        qadd8(targetBuf[i].g, scale8(color.g, scale)),
        qadd8(targetBuf[i].b, scale8(color.b, scale)));
    }
}

// Overwrites the region with `color` scaled by `scale`.
void fillRegionScaled(int col0, int col1, int row0, int row1, CRGB color, uint8_t scale) {
  for (int col = col0; col <= col1; ++col)
    for (int row = row0; row <= row1; ++row) {
      int i = ledIndex(col, row);
      targetBuf[i] = CRGB(
        scale8(color.r, scale),
        scale8(color.g, scale),
        scale8(color.b, scale));
    }
}

// Dims the live frame buffer in place.
void scaleRegion(int col0, int col1, int row0, int row1, uint8_t scale) {
  for (int col = col0; col <= col1; ++col)
    for (int row = row0; row <= row1; ++row) {
      int i = ledIndex(col, row);
      leds[i] = CRGB(
        scale8(leds[i].r, scale),
        scale8(leds[i].g, scale),
        scale8(leds[i].b, scale));
    }
}


void modeMorning() {
  CRGB base = CRGB(220, 210, 200);
  CRGB blueAccent = CRGB(60, 100, 255);

  fillRegionScaled(0, 11, 0, ROWS - 1, blueAccent, 51);
  fillRegionScaled(12, 19, 0, ROWS - 1, base, 89);
  fillRegionScaled(20, 31, 0, ROWS - 1, blueAccent, 51);
}

void modeMidday() {
  CRGB dayWhite = CRGB(255, 245, 230);
  CRGB blueBoost = CRGB(80, 120, 255);

  fillRegionScaled(0, 5, 0, ROWS - 1, dayWhite, 153);
  fillRegionScaled(6, 11, 0, ROWS - 1, dayWhite, 151);
  fillRegionScaled(12, 19, 0, ROWS - 1, dayWhite, 178);
  fillRegionScaled(20, 25, 0, ROWS - 1, dayWhite, 151);
  fillRegionScaled(26, 31, 0, ROWS - 1, dayWhite, 153);

  // Cool boost along the left edge
  addRegionScaled(0, 6, 0, ROWS - 1, blueBoost, 63);
}

void modeAfternoon() {
  CRGB warm = CRGB(255, 230, 200);
  CRGB amber = CRGB(255, 140, 80);

  fillRegionScaled(0, 5, 0, ROWS - 1, warm, 115);
  fillRegionScaled(6, 11, 0, ROWS - 1, warm, 153);
  fillRegionScaled(12, 19, 0, ROWS - 1, warm, 140);
  fillRegionScaled(20, 25, 0, ROWS - 1, warm, 153);
  fillRegionScaled(26, 31, 0, ROWS - 1, warm, 115);

  // Warm boost along the left edge
  addRegionScaled(0, 6, 0, ROWS - 1, amber, 63);
}

void modeEvening() {
  CRGB deepWarm = CRGB(255, 120, 80);

  fillRegionScaled(0, COLS - 1, 0, ROWS - 1, deepWarm, 64);
}

void modeNight() {
  // Intentionally dark — setMode() clears targetBuf to black before calling
  // the mode function, so night leaves the panel off.
  //
  // Moonlight / starfield experiment, kept for reference:
  // CRGB moon = CRGB(10,20,60);
  // for(int c=0;c<COLS;++c) targetBuf[ledIndex(c,3)] = moon;
  // random16_add_entropy(micros());
  // for(int i=0;i<NUM_LEDS;i++){
  //   if(random8() < 6){
  //     uint8_t g = random8(0,8);
  //     uint8_t b = random8(10,40);
  //     targetBuf[i] = CRGB(0,g,b);
  //   }
  // }
}

// Maps a 0-100 progress value onto 0-`range` with a quadratic ease-in.
int easeInQuad(int progress, int range) {
  float progressQuad = progress / 100.0f;
  return (int)(progressQuad * progressQuad * range);
}

struct DimWave {
  float center;      // current center column (0.0 - 31.0)
  float speed;       // columns per frame
  bool active;
};

const int MAX_WAVES = 3;
DimWave dimWaves[MAX_WAVES] = {};

uint8_t waveDimScale(float center, int col) {
  float dist = abs((float)col - center);
  float radius = 6.0;  // how wide the dip is
  if (dist >= radius) return 255;
  float t = dist / radius;               // 0 at center, 1 at edge
  float dip = 1.0 - (t * t);            // quadratic falloff, 1.0 at center
  uint8_t minScale = 80;                 // darkest point of the wave
  return (uint8_t)(minScale + (255 - minScale) * (1.0 - dip));
}

void updateDimWaves() {
  // Occasionally spawn a new wave from the left
  if (random(100) < 5) {
    for (int i = 0; i < MAX_WAVES; i++) {
      if (!dimWaves[i].active) {
        dimWaves[i].center = -6.0;       // start just off the left edge
        dimWaves[i].speed = 0.1 + random(0, 40) / 100.0;  // 0.1 - 0.5 cols/frame
        dimWaves[i].active = true;
        break;
      }
    }
  }

  // Composite all active waves — take the darkest scale per column
  uint8_t colScale[COLS];
  for (int c = 0; c < COLS; c++) colScale[c] = 255;

  for (int i = 0; i < MAX_WAVES; i++) {
    if (!dimWaves[i].active) continue;
    dimWaves[i].center += dimWaves[i].speed;
    if (dimWaves[i].center > 38.0) {     // gone past right edge
      dimWaves[i].active = false;
      continue;
    }
    for (int c = 0; c < COLS; c++) {
      uint8_t s = waveDimScale(dimWaves[i].center, c);
      if (s < colScale[c]) colScale[c] = s;  // darkest wins
    }
  }

  // Apply per-column scales
  for (int c = 0; c < COLS; c++) {
    if (colScale[c] < 255) {
      scaleRegion(c, c, 0, ROWS - 1, colScale[c]);
    }
  }
}

void alternateRows(int currMinute, int intervalMin) {
  bool alternate = ((currMinute / intervalMin) % 2) == 0;

  for (int r = 0; r < ROWS; r++) {
    if (alternate && r % 2 == 0) {
      scaleRegion(0, COLS - 1, r, r, 0);
    }
    if (!alternate && r % 2 != 0) {
      scaleRegion(0, COLS - 1, r, r, 0);
    }
  }
}

// Snapshots the current frame as the crossfade's starting point.
void startModeTransition(uint32_t duration_ms) {
  transitionDuration = duration_ms;
  lastTransitionMillis = millis();
  for (int i = 0; i < NUM_LEDS; i++) prevBuf[i] = leds[i];
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
  const uint16_t maxT = 765;          // total transition range (3 x 255)
  const uint16_t rowDelaySteps = 40;  // t-steps offset between adjacent rows (controls cascade spacing)
  // Convert rowDelaySteps into milliseconds of delay per row so the step units map into time:
  // rowDelayMs = rowDelaySteps / maxT * transitionDuration
  uint32_t rowDelayMs = ((uint32_t)rowDelaySteps * (uint32_t)transitionDuration) / (uint32_t)maxT;

  for (int col = 0; col < COLS; col++) {
    for (int row = 0; row < ROWS; row++) {
      int idx = ledIndex(col, row);
      // per-row elapsed with delay so row 0 starts at 0, row 1 starts at rowDelayMs, etc.
      int64_t rowElapsed = (int64_t)globalElapsed - (int64_t)(row * rowDelayMs);
      if (rowElapsed <= 0) {
        // not started yet: show prevBuf
        leds[idx] = prevBuf[idx];
        continue;
      } else if ((uint32_t)rowElapsed >= transitionDuration) {
        // finished: show target
        leds[idx] = targetBuf[idx];
        continue;
      }

      // map rowElapsed (0..transitionDuration) to t (0..maxT)
      uint16_t t = (uint16_t)(((uint32_t)rowElapsed * (uint32_t)maxT) / (uint32_t)transitionDuration);

      uint8_t tPhase1 = (t < 255) ? t : 255;
      uint8_t tPhase2 = (t < 255) ? 0 : min((int)t - 255, 255);
      uint8_t tPhase3 = (t < 510) ? 0 : min((int)t - 510, 255);

      leds[idx].b = scale8(prevBuf[idx].b, 255 - tPhase1) + scale8(targetBuf[idx].b, tPhase1);
      leds[idx].g = scale8(prevBuf[idx].g, 255 - tPhase2) + scale8(targetBuf[idx].g, tPhase2);
      leds[idx].r = scale8(prevBuf[idx].r, 255 - tPhase3) + scale8(targetBuf[idx].r, tPhase3);
    }
  }
}

// Mode setter
void setMode(int mode, uint32_t crossfadeMs = MODE_CROSSFADE_MS) {
  startModeTransition(crossfadeMs);
  for (int i = 0; i < NUM_LEDS; i++) targetBuf[i] = CRGB::Black;

  switch (mode) {
    case 1: modeMorning(); break;
    case 2: modeMidday(); break;
    case 3: modeAfternoon(); break;
    case 4: modeEvening(); break;
    case 5: modeNight(); break;
    default: modeNight(); break;
  }
  // immediate first frame
  applyTransition();
}


// Setup
int currentMode = -1;
void graphics_init() {

  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  //FastLED.setBrightness(BRIGHTNESS);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 5000);
  // FastLED.setMaxPowerInVoltsAndMilliamps(5, 500);
  FastLED.clear();
  FastLED.show();
}
void graphics_init_test() {

  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  //FastLED.setBrightness(BRIGHTNESS);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 500);
  FastLED.clear();
  FastLED.show();
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

  for (int i = 0; i < NUM_LEDS; i++) targetBuf[i] = CRGB::Black;
  switch (currentMode) {
    case 1: modeMorning(); break;
    case 2: modeMidday(); break;
    case 3: modeAfternoon(); break;
    case 4: modeEvening(); break;
    case 5: modeNight(); break;
  }
  for (int i = 0; i < NUM_LEDS; i++) { leds[i] = targetBuf[i]; }
  FastLED.show();
}

int testModeCounter = 1;
uint32_t last_season_change = 0;

void graphics_loop(int hour, int minute) {

  int m = timeMode(hour, minute);
  uint32_t currentTime = millis();

  if (m != currentMode) {
    currentMode = m;
    Serial.println();
    Serial.print(F("Switching to mode: "));
    Serial.println(currentMode);
    Serial.print(F("Hour: "));
    Serial.print(hour);
    Serial.print(F(" Minute: "));
    Serial.println(minute);
    setMode(currentMode, MODE_CROSSFADE_MS);
    last_season_change = currentTime;
  }

  applyTransition();
  updateDimWaves();
  // alternateRows(minute, 5);
  FastLED.show();
  delay(40);
}


void graphics_setup_test(int hour, int minute) {

  last_season_change = millis();
  // initialize with current mode immediately
  currentMode = timeMode(hour, minute);
  Serial.println();
  Serial.print(F("Starting with mode: "));
  Serial.println(currentMode);

  for (int i = 0; i < NUM_LEDS; i++) targetBuf[i] = CRGB::Black;
  switch (currentMode) {
    case 1: modeMorning(); break;
    case 2: modeMidday(); break;
    case 3: modeAfternoon(); break;
    case 4: modeEvening(); break;
    case 5: modeNight(); break;
  }
  for (int i = 0; i < NUM_LEDS; i++) { leds[i] = targetBuf[i]; }
  FastLED.show();
}

// Cycles through every mode on a timer instead of following the clock.
void graphics_loop_test(int hour, int minute) {
  uint32_t currentTime = millis();
  if (currentTime - last_season_change > MODE_CROSSFADE_MS * 2) {
    testModeCounter++;
    last_season_change = currentTime;
  }

  int m = testModeCounter % 6;

  if (m != currentMode) {
    currentMode = m;
    Serial.print(F("Season: "));
    Serial.println(m);
    setMode(currentMode, MODE_CROSSFADE_MS);
  }

  applyTransition();
  updateDimWaves();
  // alternateRows(minute, 5);

  FastLED.show();
  delay(40);
}
