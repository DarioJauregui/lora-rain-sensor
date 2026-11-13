#include "RainLogic.h"
#include <cassert>
#include <iostream>

int main() {
  uint64_t n;
  assert(rain::decimal("18446744073709551615", n) && n == UINT64_MAX);
  assert(!rain::decimal("18446744073709551616", n));
  for (auto invalid : {"", "-1", "1x", "1.5", " 2", "+1"}) assert(!rain::decimal(invalid, n));
  assert(rain::duration(1000, 59, 59, 999, n) && n == 3603599999ULL);
  assert(rain::duration(0, 0, 0, 1, n) && n == 1);
  assert(rain::duration(0, 0, 0, 0, n) && n == 0);
  assert(!rain::duration(UINT64_MAX, 0, 0, 0, n));
  assert(!rain::duration(0, 60, 0, 0, n));
  assert(!rain::duration(0, 0, 0, 1000, n));

  rain::Detector sensor;
  assert(!sensor.sample(3400, 3500, 3900, 0, 200));
  assert(!sensor.sample(4000, 3500, 3900, 100, 200));
  assert(!sensor.wet);
  assert(!sensor.sample(3400, 3500, 3900, 200, 200));
  assert(sensor.sample(3400, 3500, 3900, 400, 200) && sensor.wet);
  assert(!sensor.sample(3700, 3500, 3900, 800, 200) && sensor.wet);
  assert(sensor.sample(4000, 3500, 3900, 1000, 0) && !sensor.wet);

  rain::Pulse pulse;
  pulse.durationMs = 1;
  pulse.observe(true, true, 0); assert(pulse.active);
  pulse.tick(1, true); assert(!pulse.active);
  pulse.observe(true, false, 2); assert(!pulse.active); // no retrigger while still wet
  pulse.observe(false, false, 3);
  pulse.observe(true, true, 4); assert(pulse.active);
  pulse.tick(5, true); assert(!pulse.active);
  pulse.observe(false, true, 6); assert(pulse.active); // wet episode delivered after sensor dried

  rain::Pulse extend;
  extend.durationMs = 100; extend.policy = rain::EXTEND_WHILE_WET;
  extend.observe(true, true, 0); extend.tick(150, true); assert(extend.active);
  extend.observe(false, false, 160); extend.tick(250, true); assert(!extend.active);
  rain::Pulse repeat;
  repeat.durationMs = 100; repeat.policy = rain::REPEAT_WHILE_WET;
  repeat.observe(true, true, 0); repeat.tick(100, true);
  assert(!repeat.active);
  repeat.observe(true, false, 110); assert(repeat.active && repeat.since == 110);
  repeat.tick(210, false); assert(!repeat.active);
  repeat.active = true; repeat.restored = true;
  repeat.tick(UINT64_MAX, true); assert(repeat.active); // reboot requires explicit rearm
  repeat.observe(false, false, UINT64_MAX); assert(repeat.active);
  rain::Pulse zero;
  zero.durationMs = 0; zero.observe(true, true, 0); assert(!zero.active);
  rain::Pulse longPulse;
  longPulse.durationMs = 359999999999ULL;
  longPulse.observe(true, true, 5000);
  longPulse.tick(4294967296ULL, true); assert(longPulse.active); // beyond millis() wrap

  rain::Airtime budget;
  for (int i = 0; i < 60; ++i) assert(budget.spend(i * 1000, 500));
  assert(!budget.spend(60000, 500));
  assert(budget.wait(60000, 500) == 3540000);
  auto afterSleep = budget; // RTC RAM survives a deep-sleep restart
  assert(!afterSleep.spend(120000, 500));
  assert(afterSleep.spend(3600000, 500));
  assert(afterSleep.used == 30000);
  afterSleep.expire(7200000); assert(afterSleep.used == 0);
  rain::Airtime full;
  for (int i=0; i<256; ++i) assert(full.spend(i, 1));
  assert(!full.spend(300, 1)); // full ring must not overwrite recent entries
  std::cout << "Control, duration, detector and rolling airtime tests passed\n";
}
