#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

#include "secrets.h"

const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

const char* ntp1 = "north-america.pool.ntp.org";
const char* ntp2 = "pool.ntp.org";

const char* tzInfo = TZ_INFO;

bool isWifiConnected() {
  return (WiFi.status() == WL_CONNECTED);
}

void connectWiFi() {
  // Put Wi-Fi in station mode so ESP32 connects to a router
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  // Wait until Wi-Fi connection is established
  while (!isWifiConnected()) {
    delay(500);
  }
}

void disconnectWiFi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

void syncTime() {
  // Start NTP using the two servers above
  configTime(0, 0, ntp1, ntp2);

  // Set the timezone for your region
  setenv("TZ", tzInfo, 1);
  tzset();

  // Wait until a valid time is received from the NTP server
  // 1577836800 is the Unix time for Jan 1, 2020
  time_t now = 0;
  while (time(&now) < 1577836800) {
    delay(500);
  }
}

// Function that prints formatted date and time
void printDateTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 2000)) {
    Serial.println(F("Failed to obtain time"));
    return;
  }
  char formattedTime[80];  // Buffer to store the formatted string
  strftime(formattedTime, sizeof(formattedTime), "%A, %B %d %Y %H:%M:%S", &timeinfo);
  Serial.println(formattedTime);
}

// Reads the current wall-clock time into hourOut/minuteOut.
//
// Returns false and leaves both outputs untouched if the time is not
// available, so callers can keep their last known good values instead of
// falling back to a sentinel that would read as midnight.
//
// timeoutMs defaults to a short poll: once NTP has set the clock the RTC
// answers immediately, so a long timeout would only ever stall the animation.
bool getClock(int& hourOut, int& minuteOut, uint32_t timeoutMs = 10) {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, timeoutMs)) {
    return false;
  }
  hourOut = timeinfo.tm_hour;    // 0-23
  minuteOut = timeinfo.tm_min;   // 0-59
  return true;
}
