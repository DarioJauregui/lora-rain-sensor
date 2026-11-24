#include "RainLogic.h"
#include <cassert>
#include <string>
#include <vector>
#include <iostream>
#include <cstring>
#include "management_types.h"
using String = std::string;
constexpr uint8_t FRAME_STATE=1,STATE_SIMULATED=2,PIN_RAIN_DO=2;
constexpr uint32_t TX_ID=1,RX_ID=2;
struct RainFrame { uint8_t type,wet,flags; uint16_t ao,batteryMv; uint32_t seq,event,ackSeq,sender; uint64_t session; uint8_t payload[128]; };
RainFrame newFrame(uint8_t type,uint32_t sender,uint32_t,uint32_t seq,uint64_t session){RainFrame f{};f.seq=seq;f.type=type;f.sender=sender;f.session=session;return f;}
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
unsigned rainEvents=0;
void latchRainEvent(){++rainEvents;rainEvent.raise(42);}
bool coreDisplayEnabled=true,doWakeEnabled=false,coreMaintenance=false;
uint32_t ackedStates=0,coreTxCount=0,authRejects=0,coreTxErrors=0;
struct {uint32_t getFreeHeap(){return 1024;}} ESP;
String coreRole="TX",coreApSsid="test-TX",mgState,mgPeerSsid;
uint64_t mgSession=0,mgLastReply=0;
uint32_t mgHighest=0,mgSequence=0,managementOwnBoot=1;
bool mgHaveCache=false,mgWaiting=false,mgPeerKnown=false;
RainFrame mgCached{},mgPending{};
Operational mgPeer{};
bool (*managementApplyTest)(uint8_t)=nullptr;
uint32_t localId(){return coreRole=="TX"?TX_ID:RX_ID;}
uint64_t managementSession(){return sessionId;}
bool mgOpen(const RainFrame &f,ManagementData &d){memcpy(&d,f.payload,sizeof(d));return true;}
bool mgSeal(RainFrame &f,const ManagementData &d){memcpy(f.payload,&d,sizeof(d));return true;}
void delay(int){}
void (*managementOnRx)()=nullptr;
void transmitFrame(const RainFrame &,void(*)()){}
void managementRxAction(){}
bool saveAccess(const char *,const char *){return true;}
bool applyOperational(const Operational &){return true;}
Operational getOperational();
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
  // Merely changing origin while already wet must not create a second episode.
  assert(applyManualSelection(rain::MANUAL_WET));pending=false;
  auto episodes=rainEvents;
  adc=0;
  assert(applyManualSelection(rain::PHYSICAL));assert(rainEvents==episodes);assert(!sensorInput.manual && wetNow());
  pending=false;adc=4095;now+=50;updateSensor();assert(!wetNow());
  corePrototype=false;assert(!applyManualSelection(rain::MANUAL));
  bool manual=false,wet=false,dry=false,physical=false;
  for(const auto &event:events){manual|=event=="sensor_manual";wet|=event=="manual_wet";dry|=event=="manual_dry";physical|=event=="sensor_physical";}
  assert(manual && wet && dry && physical);
  // Exercise the actual management dispatcher in both directions, not just JSON fixtures.
  corePrototype=true;coreRole="TX";managementApplyTest=applyManualSelection;
  ManagementData command{};command.kind=4;command.bytes[0]=rain::MANUAL;
  RainFrame request=newFrame(7,RX_ID,1,1,sessionId);request.event=100;mgSeal(request,command);
  assert(managementProcess(request));
  assert(sensorInput.manual && mgHaveCache);
  ManagementData reply{};mgOpen(mgCached,reply);
  Operational applied{};memcpy(&applied,reply.bytes,sizeof(applied));
  assert(reply.kind==4 && reply.result && applied.simulated && (applied.reserved&1));
  auto revision=sensorInput.revision;
  managementProcess(request);assert(sensorInput.revision==revision); // duplicate must not apply again
  coreRole="RX";mgWaiting=true;mgPending.event=100;
  assert(managementProcess(mgCached));
  assert(mgPeerKnown && mgPeer.simulated && (mgPeer.reserved&1) && !mgWaiting && mgState=="Aplicada");
  coreRole="TX";request.seq=2;request.event=101;command.bytes[0]=rain::PHYSICAL;mgSeal(request,command);
  assert(managementProcess(request));mgOpen(mgCached,reply);memcpy(&applied,reply.bytes,sizeof(applied));
  assert(reply.result && !applied.simulated);
  coreRole="RX";mgWaiting=true;mgPending.event=101;managementProcess(mgCached);
  assert(!mgPeer.simulated && mgState=="Aplicada");
  coreRole="TX";corePrototype=false;request.seq=3;request.event=102;command.bytes[0]=rain::MANUAL;mgSeal(request,command);
  managementProcess(request);mgOpen(mgCached,reply);assert(!reply.result && !sensorInput.manual);
  std::cout << "Actual TX functions: mode, disconnected ADC, repeated states, immutable packets, ACK revision and CSV events passed\n";
}
