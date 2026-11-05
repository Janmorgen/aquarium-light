#pragma once

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WiFi.h>
#include <panel.h>

#include "secrets.h"

// Wireless flashing.
//
// ArduinoOTA ships with the ESP32 core, so this adds no library dependency.
// Once an upload starts, ArduinoOTA::handle() does not return until it is done
// or has failed - the render loop stops for the duration. That is why the
// progress callback draws to the panel itself: it is the only thing running.

#ifndef OTA_PORT
#define OTA_PORT 3232
#endif

static bool otaRunning = false;
static uint8_t otaProgressPercent = 0;

bool otaInProgress() {
  return otaRunning;
}

uint8_t otaProgress() {
  return otaProgressPercent;
}

// Draws a progress bar across the bottom row and blanks the rest. Also serves
// the plan's other purpose: the panel is dark while the write happens, so a
// Wi-Fi TX spike does not land on top of 256 lit LEDs.
void otaShowProgress(uint8_t percent) {
  FastLED.clear();

  int lit = (COLS * (int)percent) / 100;
  for (int col = 0; col < lit; ++col) {
    leds[ledIndex(col, ROWS - 1)] = CRGB(0, 40, 10);
  }
  FastLED.show();
}

void ota_begin() {
  ArduinoOTA.setPort(OTA_PORT);
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([]() {
    otaRunning = true;
    otaProgressPercent = 0;
    Serial.println();
    Serial.print(F("OTA started: "));
    Serial.println(ArduinoOTA.getCommand() == U_FLASH ? "firmware" : "filesystem");
    otaShowProgress(0);
  });

  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
    uint8_t percent = (total > 0) ? (uint8_t)((done * 100UL) / total) : 0;
    if (percent == otaProgressPercent) return;  // only redraw when it moves
    otaProgressPercent = percent;
    otaShowProgress(percent);
    Serial.print(F("OTA "));
    Serial.print(percent);
    Serial.println(F("%"));
  });

  ArduinoOTA.onEnd([]() {
    otaRunning = false;
    Serial.println(F("OTA done, rebooting"));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    otaRunning = false;
    Serial.print(F("OTA failed: "));
    switch (error) {
      case OTA_AUTH_ERROR:    Serial.println(F("auth")); break;
      case OTA_BEGIN_ERROR:   Serial.println(F("begin - does the image fit the slot?")); break;
      case OTA_CONNECT_ERROR: Serial.println(F("connect")); break;
      case OTA_RECEIVE_ERROR: Serial.println(F("receive")); break;
      case OTA_END_ERROR:     Serial.println(F("end")); break;
      default:                Serial.println(F("unknown")); break;
    }
    // A failed write leaves the panel blank; the render loop repaints it.
  });

  ArduinoOTA.begin();

  Serial.print(F("OTA listening on "));
  Serial.print(WiFi.localIP().toString().c_str());
  Serial.print(F(":"));
  Serial.println(OTA_PORT);
}

// Cheap while idle; blocks for the length of an upload once one starts.
void ota_handle() {
  ArduinoOTA.handle();
}
