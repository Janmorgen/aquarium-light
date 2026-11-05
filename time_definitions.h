#pragma once

// Modes 1-5 below, plus slot 0 so a stray mode index still lands on something.
#define NUM_MODES 6

// The lit part of the day, which the sun effect crosses.
#define DAY_START_HOUR 7
#define DAY_END_HOUR 21

// Maps wall-clock time onto a lighting mode.
//   1 morning   07:00 - 11:59
//   2 mid-day   12:00 - 16:59
//   3 afternoon 17:00 - 19:59
//   4 evening   20:00 - 20:59
//   5 night     21:00 - 06:59
int timeMode(int hour, int minute) {
  if (hour >= 7 && hour <= 11) {
    return 1;
  } else if (hour > 11 && hour <= 16) {
    return 2;
  } else if (hour > 16 && hour <= 19) {
    return 3;
  } else if (hour > 19 && hour < 21) {
    return 4;
  }
  return 5;
}

// Where the clock sits in the lit day: 0 at DAY_START_HOUR, 255 at
// DAY_END_HOUR, clamped either side. The sun effect rides on this.
uint8_t dayProgress(int hour, int minute) {
  long minutes = (long)hour * 60 + minute;
  long start = (long)DAY_START_HOUR * 60;
  long end = (long)DAY_END_HOUR * 60;

  if (minutes <= start) return 0;
  if (minutes >= end) return 255;
  return (uint8_t)(((minutes - start) * 255) / (end - start));
}
