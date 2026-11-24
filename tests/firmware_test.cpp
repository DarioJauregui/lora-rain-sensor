#include "RainLogic.h"
#include <cassert>
#include <string>
#include <vector>
#include <iostream>
using String = std::string;
constexpr uint8_t FRAME_STATE=1,STATE_SIMULATED=2,PIN_RAIN_DO=2;
constexpr uint32_t TX_ID=1;
struct RainFrame { uint8_t wet=0,flags=0; uint16_t ao=0,batteryMv=0; uint32_t seq=0,event=0,ackSeq=0; };
RainFrame newFrame(uint8_t,uint32_t,uint32_t,uint32_t seq,uint64_t){RainFrame f;f.seq=seq;return f;}
rain::Detector detector;
rain::SensorInput sensorInput;
rain::PendingRain rainEvent;
uint32_t pendingSensorRevision=0,senderBoot=1,txSequence=0,testSampleMs=50,confirmMs=200,heartbeatMs=30000,sleepMs=120000;
uint16_t analogRain=4095,rainOnThreshold=3500,rainOffThreshold=3900,batteryMv=0,adc=4095;
int doLevel=-1;
uint64_t sessionId=1,lastSample=0,nextSend=0,sensorStateSinceMs=0,now=100;
bool corePrototype=true,pending=false,awaitingAck=false,forceStateAfterAck=false;
unsigned attempts=0;
String manualFeedback;
RainFrame pendingFrame;
std::vector<String> events;
uint64_t uptimeMs(){return now;}
uint16_t readRain(){return adc;}
int digitalRead(uint8_t){return adc<3500?0:1;}
void recordEvent(const char *event){events.emplace_back(event);}
void latchRainEvent(){rainEvent.raise(42);}
#include "tx_functions.h"
int main(){
  updateSensor(true);
  assert(!applyManualSelection(rain::MANUAL_WET));
  assert(applyManualSelection(rain::MANUAL));
  assert(sensorInput.manual && sensorInput.pending && pending && !wetNow());
  const auto first=pendingFrame.seq,oldRevision=pendingSensorRevision;
  awaitingAck=true;
  assert(applyManualSelection(rain::MANUAL_WET));
  assert(forceStateAfterAck && wetNow() && pendingFrame.seq==first && !pendingFrame.wet);
  assert(!sensorInput.acknowledge(oldRevision,false));
  pending=false;awaitingAck=false;forceStateAfterAck=false;queueState();
  assert(pendingFrame.seq!=first && pendingFrame.wet && (pendingFrame.flags&STATE_SIMULATED));
  assert(sensorInput.acknowledge(pendingSensorRevision,false));
  pending=false;
  // Repeat identical commands: each gets a new sequence, independent of AO/DO.
  for(int i=0;i<4;++i){
    auto seq=txSequence;
    assert(applyManualSelection(rain::MANUAL_WET));
    assert(txSequence==seq+1 && pendingFrame.wet);
    adc=4095;now+=50;updateSensor();assert(wetNow());
    assert(sensorInput.acknowledge(pendingSensorRevision,false));pending=false;
    assert(applyManualSelection(rain::MANUAL_DRY));
    adc=0;now+=50;updateSensor();assert(!wetNow());
    assert(sensorInput.acknowledge(pendingSensorRevision,false));pending=false;
  }
  adc=0;
  assert(applyManualSelection(rain::PHYSICAL));assert(!sensorInput.manual && wetNow());
  pending=false;adc=4095;now+=50;updateSensor();assert(!wetNow());
  corePrototype=false;assert(!applyManualSelection(rain::MANUAL));
  bool manual=false,wet=false,dry=false,physical=false;
  for(const auto &event:events){manual|=event=="sensor_manual";wet|=event=="manual_wet";dry|=event=="manual_dry";physical|=event=="sensor_physical";}
  assert(manual && wet && dry && physical);
  std::cout << "Actual TX functions: mode, disconnected ADC, repeated states, immutable packets, ACK revision and CSV events passed\n";
}
