#include <FastLED.h>
#include <esp_system.h>
#include <time.h>

#include <pixels.h>
#include <wifi_helpers.h>
#include <graphics.h>
#include <power.h>

int hour = 0;
int minute = 0;
uint32_t lastTimeUpdate = 0;

// True once the clock has been re-synced during the current 00:00 minute.
bool syncedToday = false;

void setup() {
  Serial.begin(9600);
  initUsbPwr();

  if (checkUsbPower()) {
    Serial.println(F("Powered by USB"));
  }
  graphics_init();

  // esp_random() is a hardware RNG; analogRead() on a strapping pin is not a
  // safe entropy source on the ESP32.
  randomSeed(esp_random());

  connectWiFi();
  syncTime();
  disconnectWiFi();

  // syncTime() blocks until NTP succeeds, so allow a generous timeout here.
  if (!getClock(hour, minute, 2000)) {
    Serial.println(F("Failed to read clock after NTP sync"));
  }
  lastTimeUpdate = millis();

  // Avoid an immediate second sync if we happened to boot at midnight.
  syncedToday = (hour == 0 && minute == 0);

  graphics_setup(hour, minute);
}


void loop() {
  if ((millis() - lastTimeUpdate) >= 500) {
    // On failure the previous hour/minute are kept rather than jumping to a
    // sentinel value, which timeMode() would read as night.
    if (!getClock(hour, minute)) {
      Serial.println(F("Clock read failed, holding last known time"));
    }
    lastTimeUpdate = millis();
  }

  graphics_loop(hour, minute);

  // Re-sync once per day, during the 00:00 minute.
  if (hour == 0 && minute == 0) {
    if (!syncedToday) {
      connectWiFi();
      syncTime();
      disconnectWiFi();
      syncedToday = true;
    }
  } else {
    syncedToday = false;
  }

  delay(30);
}
