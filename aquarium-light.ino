#include <FastLED.h>
#include <esp_system.h>
#include <time.h>

#include <pixels.h>
#include <wifi_helpers.h>
#include <graphics.h>
#include <controls.h>
#include <power.h>

int hour = 0;
int minute = 0;
uint32_t lastTimeUpdate = 0;

// True once the clock has been re-synced during the current 00:00 minute.
bool syncedToday = false;

// Test mode cycles the modes on a timer at the USB current budget instead of
// following the clock at the full one. It is selected by the switch in
// controls.h, which is what you close when the light is on a bench running off
// the USB cable rather than mounted over the tank on its supply.
bool testMode = false;

// Only set once NTP has actually answered. Test mode skips Wi-Fi entirely, so
// a board that booted into test mode reaches clock mode with no idea what time
// it is.
bool clockSynced = false;

void syncClock() {
  connectWiFi();
  syncTime();
  disconnectWiFi();

  // syncTime() blocks until NTP succeeds, so allow a generous timeout here.
  if (!getClock(hour, minute, 2000)) {
    Serial.println(F("Failed to read clock after NTP sync"));
    return;
  }
  clockSynced = true;
}

void setup() {
  Serial.begin(9600);
  setup_controls();

  testMode = testSwitchOn();
  Serial.println();
  if (testMode) {
    Serial.println(F("Test switch closed - cycling modes at the USB budget"));
  } else {
    Serial.println(F("Test switch open - following the clock"));
  }

  graphics_init(powerBudgetMa(testMode));

  // esp_random() is a hardware RNG; analogRead() on a strapping pin is not a
  // safe entropy source on the ESP32.
  randomSeed(esp_random());

  if (testMode) {
    graphics_setup_test();
    return;
  }

  syncClock();
  lastTimeUpdate = millis();

  // Avoid an immediate second sync if we happened to boot at midnight.
  syncedToday = (hour == 0 && minute == 0);

  graphics_setup(hour, minute);
}


void loop() {
  // Switch thrown while running: re-budget the panel and swap which loop
  // drives it. No reboot needed in either direction.
  if (updateTestSwitch()) {
    testMode = testSwitchOn();
    graphics_set_power_budget(powerBudgetMa(testMode));

    if (testMode) {
      Serial.println(F("Test switch closed - switching to test mode"));
      graphics_setup_test();
    } else {
      Serial.println(F("Test switch open - returning to the clock"));
      // A board that booted into test mode never synced, so do it now rather
      // than running the schedule off a clock that reads 1970.
      if (!clockSynced) {
        syncClock();
      }
      lastTimeUpdate = millis();
    }
  }

  if (testMode) {
    graphics_loop_test();
    return;
  }

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
