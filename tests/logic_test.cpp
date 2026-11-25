#include "RainLogic.h"
#include <cassert>
#include <iostream>

int main() {
  assert(!rain::contactClosed(false,false));assert(rain::contactClosed(false,true));
  assert(rain::contactClosed(true,false));assert(!rain::contactClosed(true,true));
  assert(rain::relayOutputHigh(false,true)&&!rain::relayOutputHigh(true,true)); // LOW-trigger
  assert(!rain::relayOutputHigh(false,false)&&rain::relayOutputHigh(true,false)); // HIGH-trigger
  assert(rain::quality(-80,10,false,10,10)==-1);
  assert(rain::quality(-80,10,true,10,10)>rain::quality(-120,-10,true,2,10));
  rain::SensorInput input;
  assert(!input.manual && !input.wet(false));
  assert(!input.select(rain::MANUAL_WET,true,false));
  assert(!input.select(rain::MANUAL,false,false));
  assert(input.select(rain::MANUAL,true,false));
  assert(input.manual && input.pending && !input.wet(true)); // ignore even disconnected AO/DO
  const auto oldRevision=input.revision;
  assert(input.select(rain::MANUAL_WET,true,false));
  assert(input.wet(false));
  assert(!input.acknowledge(oldRevision,false) && input.pending); // previous frame ACK
  assert(!input.acknowledge(input.revision,true) && input.pending); // rejected != confirmed
  assert(input.acknowledge(input.revision,false) && !input.pending);
  for(int i=0;i<5;++i){
    auto revision=input.revision;
    assert(input.select(rain::MANUAL_WET,true,false));
    assert(input.revision!=revision && input.pending && input.wet(false));
    assert(input.acknowledge(input.revision,false));
    assert(input.select(rain::MANUAL_DRY,true,true) && !input.wet(true));
    assert(input.acknowledge(input.revision,false));
  }
  assert(input.select(rain::PHYSICAL,true,false));
  assert(!input.manual && input.wet(true) && !input.wet(false));
  assert(!input.select(255,true,true));
  assert(rain::stateAgeValue(3600000)==3600);
  assert(rain::stateAgeValue(3600001)==60);
  assert(rain::stateAgeValue(86400000)==1440);
  assert(rain::stateAgeValue(86400001)==24);
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

  for (bool active : {false,true}) for (bool polarity : {false,true}) for (bool previous : {false,true}) {
    // OTA gate overrides every state, including configured link-loss actions.
    for (uint8_t loss : {0,1,2}) {
      assert(!rain::outputCoil(true,true,true,false,loss,active,polarity,previous));
      assert(!rain::outputCoil(true,true,true,true,loss,active,polarity,previous));
      assert(!rain::outputCoil(false,false,true,true,loss,active,polarity,previous));
      assert(!rain::outputCoil(false,true,false,true,loss,active,polarity,previous));
    }
    assert(rain::outputCoil(false,true,true,true,rain::HOLD_OUTPUT,active,polarity,previous)==previous);
    assert(rain::outputCoil(false,true,true,true,rain::FORCE_ACTIVE,active,polarity,previous)==polarity);
    assert(rain::outputCoil(false,true,true,true,rain::FORCE_INACTIVE,active,polarity,previous)==!polarity);
    assert(rain::outputCoil(false,true,true,false,0,active,polarity,previous)==(active?polarity:!polarity));
  }
  rain::PendingRain delivery;
  delivery.raise(42); assert(delivery.pending && delivery.id==42);
  delivery.raise(43); assert(delivery.id==42); // dry/new wet cannot erase an undelivered episode
  assert(!delivery.acknowledge(43) && delivery.pending);
  auto rebootedDelivery=delivery; // serialized to NVS by TX, independent of radio session
  assert(rebootedDelivery.pending && rebootedDelivery.acknowledge(42));
  assert(!rebootedDelivery.pending);
  assert(!rebootedDelivery.acknowledge(42));
  rebootedDelivery.raise(44); assert(rebootedDelivery.id==44 && rebootedDelivery.pending);
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
  rain::Airtime reserved;
  assert(reserved.spend(0,10000));
  assert(reserved.spend(1000,1000,12000));
  assert(reserved.spend(2000,1000,12000));
  assert(!reserved.spend(3000,1000,12000)); // leave capacity for ACK and state
  assert(reserved.spend(3000,1000)); // priority frame uses full 30 s budget
  assert(reserved.wait(3000,1000,12000)==3597000); // no background leak while over cap
  rain::Airtime full;
  for (int i=0; i<256; ++i) assert(full.spend(i, 1));
  assert(!full.spend(300, 1)); // full ring must not overwrite recent entries
  std::cout << "Control, duration, detector and rolling airtime tests passed\n";
}
