#pragma once

#include <Arduino.h>

// VBUS sense GPIO for ESP32-WROOM-DA (DA core exposes VBUS_SENSE)
// If your board variant differs, change this to the correct pin.
#ifndef VBUS_SENSE_PIN
#define VBUS_SENSE_PIN GPIO_NUM_19
#endif

// Initialize USB power subsystem (idempotent)
void initUsbPwr() {
  pinMode((gpio_num_t)VBUS_SENSE_PIN, INPUT);
}

// Returns true if USB VBUS is present
bool checkUsbPower() {
  // digitalRead returns HIGH when VBUS present if board ties VBUS to pin through divider.
  int val = digitalRead((gpio_num_t)VBUS_SENSE_PIN);
  return (val == HIGH);
}
