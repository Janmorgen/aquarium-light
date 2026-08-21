#pragma once

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
