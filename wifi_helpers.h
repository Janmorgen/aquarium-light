#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

#include "secrets.h"

// Wi-Fi, NTP, and the clock.
//
// The radio used to come up once at boot, sync the clock and go straight back
// to WIFI_OFF. It now stays up: the light is reachable for wireless flashing
// and for the control page, neither of which can work through a window that
// closes. That costs roughly 80 mA continuously - acceptable on a 5 A supply,
// and the reason the daily re-sync no longer has to bring the radio back.
//
// Nothing here blocks forever. A light that will not turn on because the router
// is rebooting is worse than a light running on yesterday's clock.

const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

const char* ntp1 = "north-america.pool.ntp.org";
const char* ntp2 = "pool.ntp.org";

const char* tzInfo = TZ_INFO;

// How long to wait for the association at boot, and how often to retry after
// the link drops.
#ifndef WIFI_CONNECT_TIMEOUT_MS
#define WIFI_CONNECT_TIMEOUT_MS 20000
#endif
#ifndef WIFI_RETRY_INTERVAL_MS
#define WIFI_RETRY_INTERVAL_MS 30000
#endif

static uint32_t wifiLastAttempt = 0;
static uint32_t wifiDropouts = 0;
static bool wifiWasConnected = false;

bool isWifiConnected() {
  return (WiFi.status() == WL_CONNECTED);
}

// Applies the static address from secrets.h, if one is defined. Without it the
// board takes a DHCP lease - give it a reservation on the router, or its
// address will move.
void applyStaticIp() {
#ifdef NET_STATIC_IP
  IPAddress ip, gateway, subnet, dns;
  ip.fromString(NET_STATIC_IP);
  gateway.fromString(NET_GATEWAY);
  subnet.fromString(NET_SUBNET);
#ifdef NET_DNS
  dns.fromString(NET_DNS);
#else
  dns = gateway;
#endif
  if (!WiFi.config(ip, gateway, subnet, dns)) {
    Serial.println(F("Static IP config rejected, falling back to DHCP"));
  }
#endif
}

// Starts the association. Does not wait for it.
void wifiBegin() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);       // OTA and the web server want a responsive radio
  WiFi.setAutoReconnect(true);
  applyStaticIp();
  WiFi.begin(ssid, password);
  wifiLastAttempt = millis();
}

// Waits up to timeoutMs for the link. Returns whether it came up.
bool wifiWaitConnected(uint32_t timeoutMs) {
  uint32_t started = millis();
  while (!isWifiConnected()) {
    if ((millis() - started) >= timeoutMs) return false;
    delay(100);
  }
  return true;
}

// Re-associates after a dropout. Call from loop(); costs nothing while the
// link is up.
void wifiMaintain() {
  if (isWifiConnected()) {
    if (!wifiWasConnected) {
      wifiWasConnected = true;
      Serial.print(F("Wi-Fi up at "));
      Serial.println(WiFi.localIP().toString().c_str());
    }
    return;
  }

  if (wifiWasConnected) {
    wifiWasConnected = false;
    wifiDropouts++;
    Serial.println(F("Wi-Fi lost, retrying in the background"));
  }

  if ((millis() - wifiLastAttempt) < WIFI_RETRY_INTERVAL_MS) return;
  wifiLastAttempt = millis();
  WiFi.disconnect();
  WiFi.begin(ssid, password);
}

uint32_t wifiDropoutCount() {
  return wifiDropouts;
}

// Asks NTP for the time and waits up to timeoutMs for an answer. Returns
// whether the clock ended up set.
//
// 1577836800 is the Unix time for Jan 1, 2020 - anything below it means the
// clock is still at the epoch and NTP has not answered.
bool syncTime(uint32_t timeoutMs) {
  if (!isWifiConnected()) return false;

  configTime(0, 0, ntp1, ntp2);
  setenv("TZ", tzInfo, 1);
  tzset();

  uint32_t started = millis();
  time_t now = 0;
  while (time(&now) < 1577836800) {
    if ((millis() - started) >= timeoutMs) return false;
    delay(200);
  }
  return true;
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
