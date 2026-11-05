#pragma once

#include <Arduino.h>

// The two rolls that give the light its variation, shared by the looks in
// graphics.h and the effect stack in effects.h.
//
// Between them they cover both kinds of "not the same every time": a value
// that lands near its nominal but never exactly on it, and an element that
// only shows up some of the time at all.

// Rolls a value within +/- jitter of nominal, clamped to 0-255.
uint8_t jittered(uint8_t nominal, uint8_t jitter) {
  if (jitter == 0) return nominal;

  int lo = (int)nominal - (int)jitter;
  int hi = (int)nominal + (int)jitter;
  if (lo < 0) lo = 0;
  if (hi > 255) hi = 255;
  return (uint8_t)random(lo, hi + 1);
}

// Odds out of 255: 255 is always, 0 is never.
bool rollChance(uint8_t chance) {
  if (chance >= 255) return true;
  if (chance == 0) return false;
  return random(255) < (long)chance;
}
