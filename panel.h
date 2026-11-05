#pragma once

#include <Arduino.h>
#include <FastLED.h>

// The physical panel: its geometry, its frame buffer, and how to address it.
// Split out from graphics.h so effects.h can reach the pixels without the two
// having to include each other.

// ---------- PANEL ----------
#define ROWS 8
#define COLS 32
#define NUM_LEDS (ROWS * COLS)
// ----------------------------

#define ARRAY_LEN(a) ((uint8_t)(sizeof(a) / sizeof((a)[0])))

// The live frame buffer, the one FastLED.show() puts on the panel.
CRGB leds[NUM_LEDS];

// Rendered frames per second, averaged over the last second. Worth having on
// the status page: the effect ramps are driven by elapsed time, so a loop that
// has quietly got slow shows up here before it shows up as timings that do not
// match what the tables say.
uint8_t panelFps = 0;
uint16_t panelFrameCount = 0;
uint32_t panelFpsWindow = 0;

// Call once per rendered frame.
void panelCountFrame() {
  panelFrameCount++;
  uint32_t now = millis();
  if ((now - panelFpsWindow) < 1000) return;
  panelFps = (uint8_t)((panelFrameCount * 1000UL) / (now - panelFpsWindow));
  panelFrameCount = 0;
  panelFpsWindow = now;
}

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

// ---------- PIXEL OVERRIDES ----------
//
// A layer above everything else: individual pixels pinned to a color, applied
// after the looks and the effects have had their say. Only the pixels that
// were set are held - the rest keep animating underneath, so a single pinned
// pixel reads against the live frame rather than replacing it.

CRGB overrideBuf[NUM_LEDS];
bool overrideSet[NUM_LEDS];
uint16_t overrideCount = 0;

void panel_set_pixel(int col, int row, CRGB color) {
  int i = ledIndex(col, row);
  if (!overrideSet[i]) overrideCount++;
  overrideSet[i] = true;
  overrideBuf[i] = color;
}

void panel_release_pixel(int col, int row) {
  int i = ledIndex(col, row);
  if (overrideSet[i]) overrideCount--;
  overrideSet[i] = false;
}

void panel_clear_overrides() {
  for (int i = 0; i < NUM_LEDS; ++i) overrideSet[i] = false;
  overrideCount = 0;
}

uint16_t panel_override_count() {
  return overrideCount;
}

// Call once per frame, after everything else and before FastLED.show().
void panel_apply_overrides() {
  if (overrideCount == 0) return;
  for (int i = 0; i < NUM_LEDS; ++i) {
    if (overrideSet[i]) leds[i] = overrideBuf[i];
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
