#pragma once

#include <Arduino.h>

// Calls a function on a fixed interval without blocking.
//
// Two modes, pick one per instance:
//   callEveryInterval() fires fn() once each time the interval elapses.
//   callCycle()         fires progressFn(0-100) every call, restarting at the
//                       end of each interval.
class Regulator {
public:
  using Func = void (*)();
  using ProgressFunc = void (*)(int);

  explicit Regulator(int intervalMs, Func f = nullptr)
    : fn(f), progressFn(nullptr), interval(intervalMs), lastCall(0) {}

  void callEveryInterval() {
    if (!fn) return;

    uint32_t currentTime = millis();
    if ((currentTime - lastCall) >= (uint32_t)interval) {
      lastCall = currentTime;
      fn();
    }
  }

  void callCycle() {
    if (!progressFn) return;

    uint32_t currentTime = millis();
    uint32_t elapsed = currentTime - lastCall;
    int progress = (interval > 0)
                     ? (int)((elapsed * 100UL) / (uint32_t)interval)
                     : 100;
    if (progress > 100) progress = 100;

    progressFn(progress);
    if (progress >= 100) {
      lastCall = currentTime;
    }
  }

  void set(Func f) { fn = f; }
  void setProgressFn(ProgressFunc f) { progressFn = f; }

  bool valid() const { return fn != nullptr || progressFn != nullptr; }

  void setInterval(int intervalMs) { interval = intervalMs; }
  int getInterval() const { return interval; }

private:
  Func fn;
  ProgressFunc progressFn;
  int interval;
  uint32_t lastCall;
};
