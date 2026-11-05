#pragma once

// Modes 1-5 below, plus slot 0 so a stray mode index still lands on something.
#define NUM_MODES 6

// The lit part of the day, in minutes after midnight: from the start of the
// morning fade-in to the start of the fade to night. The sun effect crosses it.
#define DAY_START_MINUTE (8 * 60 + 30)  // 08:30
#define DAY_END_MINUTE   (20 * 60)      // 20:00

// Maps wall-clock time onto a lighting mode. Each boundary starts a 30-minute
// crossfade, so the panel is dark from about 20:30 until 08:30.
//   1 morning   08:30 - 10:59
//   2 mid-day   11:00 - 16:59   the plant-growing window, at full white
//   3 afternoon 17:00 - 18:59
//   4 evening   19:00 - 19:59
//   5 night     20:00 - 08:29
//
// About 12 hours lit, 6 of them at full white, and 12 dark: enough light for
// low-to-medium-light plants, and a long night for the snails, the shrimp and
// the pencilfish.
int timeMode(int hour, int minute) {
  int m = hour * 60 + minute;
  if (m < DAY_START_MINUTE) return 5;
  if (m < 11 * 60) return 1;
  if (m < 17 * 60) return 2;
  if (m < 19 * 60) return 3;
  if (m < DAY_END_MINUTE) return 4;
  return 5;
}

// Where the clock sits in the lit day: 0 at DAY_START_MINUTE, 255 at
// DAY_END_MINUTE, clamped either side. The sun effect rides on this.
uint8_t dayProgress(int hour, int minute) {
  long minutes = (long)hour * 60 + minute;
  long start = DAY_START_MINUTE;
  long end = DAY_END_MINUTE;

  if (minutes <= start) return 0;
  if (minutes >= end) return 255;
  return (uint8_t)(((minutes - start) * 255) / (end - start));
}
