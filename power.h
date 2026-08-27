#pragma once

#include <Arduino.h>

// Current budgets handed to FastLED.
//
// Test mode is what runs at the bench, off the USB cable. A USB port is good
// for roughly half an amp, nowhere near enough for 256 pixels, so the panel
// has to be capped hard there. Normal operation runs off the 5 A supply.
//
// Which mode is active comes from the switch in controls.h - see the note
// there on why this is a switch and not automatic USB detection.

#ifndef TEST_POWER_BUDGET_MA
#define TEST_POWER_BUDGET_MA 500
#endif
#ifndef NORMAL_POWER_BUDGET_MA
#define NORMAL_POWER_BUDGET_MA 5000
#endif

// Supply budget for FastLED, in milliamps.
uint32_t powerBudgetMa(bool testMode) {
  return testMode ? TEST_POWER_BUDGET_MA : NORMAL_POWER_BUDGET_MA;
}
