#include <FastLED.h>
#include <esp_system.h>
#include <time.h>

#include <pixels.h>
#include <wifi_helpers.h>
#include <panel.h>
#include <controls.h>
#include <graphics.h>
#include <power.h>
#include <ota.h>
#include <webui.h>

int hour = 0;
int minute = 0;
uint32_t lastTimeUpdate = 0;
uint32_t lastEffectsLog = 0;

// True once the clock has been re-synced during the current 00:00 minute.
bool syncedToday = false;

// Test mode cycles the modes on a timer at the USB current budget instead of
// following the clock. Either the switch in controls.h or the control page can
// ask for it.
bool testMode = false;

// Only set once NTP has actually answered. Until then the schedule has nothing
// to follow, and the control page says so rather than the light silently
// sitting in night.
bool clockSynced = false;

// Set by the control page; cleared once the sync has been attempted.
bool resyncRequested = false;

// How long a commanded mode change takes, against the half hour a scheduled one
// gets. Someone standing at the tank pressing a button should see it happen.
#define MANUAL_CROSSFADE_MS 4000

// And how long a correction takes. Booting without a clock leaves the schedule
// with nothing to follow, so the panel sits in night until NTP answers. Moving
// to the right mode after that is a correction, not a sunrise - running it over
// the scheduled half hour would mean a slow dawn at whatever time the light
// happened to come up.
#define CORRECTION_CROSSFADE_MS 6000

// False until the schedule has had a real time to work from. The first mode
// decision made with a valid clock is a correction, however long it took.
bool clockEverValid = false;

// How often to retry NTP after a failure.
#define CLOCK_RETRY_INTERVAL_MS 60000
uint32_t lastClockAttempt = 0;

// Whether OTA and the web server are listening. They start in setup() when the
// link is there, and in loop() the first time it turns up if it was not.
bool servicesStarted = false;

// Tries NTP and reads the clock back. Never blocks for long: the light has to
// run whether or not the network is there.
bool syncClock() {
  lastClockAttempt = millis();

  if (!syncTime(15000)) {
    Serial.println(F("NTP did not answer"));
    return false;
  }
  if (!getClock(hour, minute, 2000)) {
    Serial.println(F("Failed to read clock after NTP sync"));
    return false;
  }

  clockSynced = true;
  Serial.print(F("Clock synced: "));
  Serial.print(hour);
  Serial.print(F(":"));
  Serial.println(minute);
  return true;
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

  // The radio now stays up for the whole run: wireless flashing and the
  // control page both need the light to be reachable, not reachable for the
  // first few minutes after a power cycle.
  wifiBegin();
  if (wifiWaitConnected(WIFI_CONNECT_TIMEOUT_MS)) {
    Serial.print(F("Wi-Fi up at "));
    Serial.println(WiFi.localIP().toString().c_str());
    syncClock();
    ota_begin();
    webui_begin();
    servicesStarted = true;
  } else {
    // Not fatal. wifiMaintain() keeps trying, and the services come up on the
    // first loop after the link does.
    Serial.println(F("Wi-Fi did not come up; retrying in the background"));
  }

  if (testMode) {
    graphics_setup_test();
    return;
  }

  lastTimeUpdate = millis();

  // Avoid an immediate second sync if we happened to boot at midnight.
  syncedToday = (hour == 0 && minute == 0);

  graphics_setup(hour, minute);
}


// Brings up OTA and the web server the first time the link is available, for
// the case where Wi-Fi was not there at boot.
void startServicesWhenReady() {
  if (servicesStarted || !isWifiConnected()) return;
  servicesStarted = true;
  ota_begin();
  webui_begin();
  if (!clockSynced) syncClock();
}


void loop() {
  wifiMaintain();
  startServicesWhenReady();

  // Blocks for the length of an upload once one starts; the panel shows a
  // progress bar from ota.h while it does.
  ota_handle();
  webui_handle();

  // The switch and the control page are two ways of asking for the same thing.
  bool wantTest = testSwitchOn() || testModeRequest;
  bool switchMoved = updateTestSwitch();

  if (wantTest != testMode) {
    testMode = wantTest;
    graphics_set_power_budget(powerBudgetMa(testMode));

    if (testMode) {
      Serial.println(switchMoved ? F("Test switch closed - switching to test mode")
                                 : F("Test mode requested - switching to test mode"));
      graphics_setup_test();
    } else {
      Serial.println(F("Leaving test mode - returning to the clock"));
      if (!clockSynced) syncClock();
      lastTimeUpdate = millis();
    }
  }

  graphics_set_brightness(masterBrightness);
  // Wound forward on the bench so a short sitting actually shows the effects
  // moving. Set every frame, so leaving test mode cannot strand them fast.
  effects_set_speed(testMode ? TEST_EFFECTS_SPEEDUP : 1);

  // Picking a mode is a stronger statement than "cycle everything", so an
  // override suppresses the cycling. The test current budget still applies -
  // that is about what the supply can deliver, not about what to show.
  if (testMode && modeOverride == 0) {
    // Bench heartbeat: the effect stack drifts far too slowly to read off the
    // panel in a short sitting, so print where it currently sits.
    if ((millis() - lastEffectsLog) >= 15000) {
      lastEffectsLog = millis();
      effects_report();
    }
    graphics_loop_test();
    return;
  }

  if ((millis() - lastTimeUpdate) >= 500) {
    // On failure the previous hour/minute are kept rather than jumping to a
    // sentinel value, which timeMode() would read as night.
    if (clockSynced && !getClock(hour, minute)) {
      Serial.println(F("Clock read failed, holding last known time"));
    }
    lastTimeUpdate = millis();
  }

  // A commanded mode wins over the schedule until it is set back to auto.
  int mode = (modeOverride > 0) ? modeOverride : timeMode(hour, minute);
  uint32_t crossfade = MODE_CROSSFADE_MS;
  if (controlsChanged) {
    controlsChanged = false;
    crossfade = MANUAL_CROSSFADE_MS;
  }

  // First frame with a real clock: land on the right mode quickly, and put the
  // effects where they belong rather than climbing to them for half an hour.
  if (clockSynced && !clockEverValid) {
    clockEverValid = true;
    crossfade = CORRECTION_CROSSFADE_MS;
    effects_prime(mode);
    Serial.print(F("Clock acquired - correcting to mode "));
    Serial.println(mode);
  }

  graphics_loop(mode, dayProgress(hour, minute), crossfade);

  // Re-sync once per day during the 00:00 minute, on request, or on a timer
  // while the clock has never been set at all.
  bool dailyDue = (hour == 0 && minute == 0 && !syncedToday);
  bool retryDue = (!clockSynced && (millis() - lastClockAttempt) >= CLOCK_RETRY_INTERVAL_MS);

  if (resyncRequested || dailyDue || retryDue) {
    resyncRequested = false;
    if (syncClock() && dailyDue) syncedToday = true;
  } else if (hour != 0 || minute != 0) {
    syncedToday = false;
  }
}
