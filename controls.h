#pragma once

#include <Arduino.h>

// Test mode switch.
//
// A switch or jumper shorting TEST_SWITCH_PIN to GND selects test mode: the
// panel cycles the modes on a timer at a current a USB port can supply,
// instead of following the clock at the full 5 A budget.
//
//   GPIO 32 ---o/ o--- GND        closed = test mode
//
// This is a switch rather than automatic USB detection because the board
// cannot sense the difference. The classic ESP32 has no USB peripheral, this
// DevKit routes VBUS to no GPIO (probed: with USB connected, all 34 usable
// pins just follow their internal pull), and USB VBUS and the strip's supply
// arrive on the same 5 V pin, so no sense point on the rail can tell them
// apart either.
//
// GPIO 32 has an internal pull-up and is not a strapping pin, so it is safe to
// hold either way at boot. Pins 34-39 would not work here - they have no
// internal pull-ups.

#ifndef TEST_SWITCH_PIN
#define TEST_SWITCH_PIN 32
#endif

// Level the pin reads while test mode is selected. LOW suits a switch to GND
// against the internal pull-up; flip this for a switch that pulls to 3V3.
#ifndef TEST_SWITCH_ACTIVE_LEVEL
#define TEST_SWITCH_ACTIVE_LEVEL LOW
#endif

// The switch may sit on a long run next to the LED data line, so each reading
// is a majority vote, and a change has to hold for DEBOUNCE_MS before the
// panel acts on it.
static const uint8_t TEST_SWITCH_SAMPLES = 9;
static const uint32_t TEST_SWITCH_DEBOUNCE_MS = 100;
static const uint32_t TEST_SWITCH_POLL_MS = 50;

static bool testSwitchState = false;      // last reported (debounced) state
static bool testSwitchCandidate = false;  // reading waiting out the debounce
static uint32_t testSwitchCandidateSince = 0;
static uint32_t testSwitchLastPoll = 0;

// One majority-vote read of the switch. Not debounced.
bool readTestSwitchRaw() {
  uint8_t votes = 0;
  for (uint8_t i = 0; i < TEST_SWITCH_SAMPLES; ++i) {
    if (digitalRead(TEST_SWITCH_PIN) == TEST_SWITCH_ACTIVE_LEVEL) ++votes;
    delayMicroseconds(200);
  }
  return votes > (TEST_SWITCH_SAMPLES / 2);
}

// Initialize physical controls (idempotent)
void setup_controls() {
  // Pull toward the inactive level, so an unwired pin means "not test mode"
  // rather than floating between the two.
  pinMode(TEST_SWITCH_PIN,
          (TEST_SWITCH_ACTIVE_LEVEL == LOW) ? INPUT_PULLUP : INPUT_PULLDOWN);
  delay(2);  // let the pull settle before the first read

  testSwitchState = readTestSwitchRaw();
  testSwitchCandidate = testSwitchState;
  testSwitchCandidateSince = millis();
  testSwitchLastPoll = testSwitchCandidateSince;
}

// True while the switch selects test mode.
bool testSwitchOn() {
  return testSwitchState;
}

// Re-reads the switch and updates the debounced state. Call from loop().
// Returns true only on the call where the state actually flips.
bool updateTestSwitch() {
  uint32_t now = millis();
  if ((now - testSwitchLastPoll) < TEST_SWITCH_POLL_MS) return false;
  testSwitchLastPoll = now;

  bool raw = readTestSwitchRaw();

  // Reading changed: restart the debounce window.
  if (raw != testSwitchCandidate) {
    testSwitchCandidate = raw;
    testSwitchCandidateSince = now;
    return false;
  }
  if (raw == testSwitchState) return false;
  if ((now - testSwitchCandidateSince) < TEST_SWITCH_DEBOUNCE_MS) return false;

  testSwitchState = raw;
  return true;
}
