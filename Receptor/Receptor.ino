#include <Arduino.h>
#include <RainCore.h>
#include <Controls.h>
#include <Management.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#ifndef RAIN_CONTACT_NC
#define RAIN_CONTACT_NC 0
#endif
static_assert(RAIN_CONTACT_NC==0||RAIN_CONTACT_NC==1,"Contact must be NO (0) or NC (1)");
constexpr bool DEFAULT_CONTACT_NC=RAIN_CONTACT_NC;
// Match this compile-time pin to the physically wired control input.
#ifndef RAIN_RELAY_GPIO
#define RAIN_RELAY_GPIO 5
#endif
#ifndef RAIN_RELAY_ACTIVE_HIGH
#define RAIN_RELAY_ACTIVE_HIGH 1
#endif
static_assert(RAIN_RELAY_GPIO>=0&&RAIN_RELAY_GPIO<=48,"Relay GPIO invalid");
static_assert(RAIN_RELAY_ACTIVE_HIGH==0||RAIN_RELAY_ACTIVE_HIGH==1,"Relay output polarity invalid");
uint8_t relayPin=RAIN_RELAY_GPIO;
bool relayInputLow=true; // Perfil de pruebas: entrada activa LOW.
bool contactNc=DEFAULT_CONTACT_NC;
SemaphoreHandle_t controlMutex=nullptr;
struct ControlGuard {
  ControlGuard(){xSemaphoreTake(controlMutex,portMAX_DELAY);}
  ~ControlGuard(){xSemaphoreGive(controlMutex);}
};
rain::Pulse pulse;
bool controlArmed=false, activeCoil=true, coilOn=false, haveState=false, lastRain=false, simulated=false;
bool lastRelayActive=false;
uint64_t relayTestUntilMs=0, lastRelaySelfTestMs=0, lastRadioProbeMs=0;
bool relayTestRunning=false, loggedLinkLost=true, loggedRelayActive=false;
RainFrame queuedReply{};
bool replyPending=false;
uint64_t replyReadyMs=0;
uint32_t replyDeferred=0,replySent=0,replySendErrors=0;
bool sessionReady=false, commsLost=true;
std::atomic<bool> persistDirty{false};
uint8_t lossPolicy=rain::HOLD_OUTPUT;
uint64_t lossTimeoutMs=180000, lastFresh=0, receiverSession=0, senderNonce=0;
uint32_t peerHeartbeatMs=30000;
uint64_t relayStateSinceMs=0, wetStateSinceMs=0;
uint32_t ownBoot=0, senderBoot=0, ownSeq=0, highestSeq=0, lastEvent=0;
uint32_t rxCount=0, duplicates=0, authRejects=0;
uint16_t lastAO=0, lastBatteryMv=0;
float lastRssi=-150, lastSnr=-20;
volatile bool rxInterrupt=false;

void IRAM_ATTR onRadioReceive(){rxInterrupt=true;}
void recordEvent(const char *event){Serial.printf("%llu %s\n",(unsigned long long)uptimeMs(),event);coreLogEvent(event);}
void setPhysicalCoil(bool on){
  bool active=controlArmed&&haveState&&!coreMaintenance&&!pulse.restored&&(on==activeCoil);
  if(lastRelayActive!=active)relayStateSinceMs=uptimeMs();
  lastRelayActive=active;coilOn=on;
  digitalWrite(relayPin,rain::relayOutputHigh(on,relayInputLow)?HIGH:LOW);
}
// Caller holds controlMutex. All paths, including web and OTA, share this gate.
void applyControl(){
  if(relayTestRunning)return;
  if(pulse.restored){setPhysicalCoil(false);return;}
  setPhysicalCoil(rain::outputCoil(coreMaintenance,controlArmed,haveState,commsLost,
                                 lossPolicy,pulse.active,activeCoil,coilOn));
}
void controlTask(void *){
  for(;;){
    {ControlGuard lock;
      uint64_t now=uptimeMs();commsLost=!haveState||now-lastFresh>lossTimeoutMs;
      if(relayTestRunning&&now>=relayTestUntilMs){relayTestRunning=false;setPhysicalCoil(false);}
      bool wasActive=pulse.active;
      if(!coreMaintenance&&!(commsLost&&lossPolicy==rain::HOLD_OUTPUT))pulse.tick(now,!commsLost);
      if(wasActive!=pulse.active)persistDirty=true;
      applyControl();
    }
    // Output deadlines continue while HTTP or a blocking radio transmission runs.
    vTaskDelay(1);
  }
}
struct __attribute__((packed)) ReceiverSettings {
  uint32_t magic;
  uint64_t duration, timeout;
  uint32_t event;
  uint8_t policy, loss, armed, active, polarity;
};
bool relayActive(){return controlArmed&&haveState&&!coreMaintenance&&!pulse.restored&&(coilOn==activeCoil);}
bool rxLinkOk(){
  const uint64_t threshold=corePrototype?(uint64_t)peerHeartbeatMs*3+10000:lossTimeoutMs;
  return haveState&&uptimeMs()-lastFresh<threshold;
}
bool persistSettings(){
  ReceiverSettings saved{};
  {ControlGuard lock;saved.magic=0x52585332;saved.duration=pulse.durationMs;saved.event=lastEvent;saved.policy=pulse.policy;
    saved.loss=lossPolicy;saved.armed=controlArmed;saved.active=pulse.active;saved.polarity=activeCoil;saved.timeout=lossTimeoutMs;persistDirty=false;}
  Preferences p;if(!p.begin("rainrx2",false))return false;
  bool ok=p.putBytes("settings",&saved,sizeof(saved))==sizeof(saved);
  ok=p.putBool("inputlow",relayInputLow)>0&&ok;
  ok=p.putBool("contactnc",contactNc)>0&&ok;
  ok=p.putUChar("relaypin",relayPin)==1&&ok;
  p.end();return ok;
}
void beforeOta(){ControlGuard lock;setPhysicalCoil(false);recordEvent("maintenance_latched");}
void loadSettings(){
  ReceiverSettings saved{};Preferences p;
  if(!p.begin("rainrx2",true))return;
  relayInputLow=p.getBool("inputlow",true);
  contactNc=p.getBool("contactnc",DEFAULT_CONTACT_NC);
  relayPin=p.getUChar("relaypin",RAIN_RELAY_GPIO);
  if(relayPin!=2&&relayPin!=5)relayPin=5;
  size_t n=p.getBytes("settings",&saved,sizeof(saved));p.end();
  if(n!=sizeof(saved)||saved.magic!=0x52585332||saved.policy>2||saved.loss>2||saved.armed>1||saved.active>1||saved.polarity>1||
     saved.timeout<1000||saved.timeout>UINT32_MAX)return;
  pulse.durationMs=saved.duration;pulse.policy=saved.policy;pulse.active=saved.active;pulse.restored=pulse.active;
  lossPolicy=saved.loss;controlArmed=saved.armed;activeCoil=saved.polarity;lossTimeoutMs=saved.timeout;lastEvent=saved.event;
}
bool configureReceiver(uint32_t config,uint64_t duration,uint64_t timeout,bool reset){
  {ControlGuard lock;
    if(reset){
      if(!haveState||commsLost||lastRain||coreMaintenance)return false;
      pulse.active=false;pulse.restored=false;
    }else{
      if((config&~0x3dfUL)||((config>>1)&3)>2||((config>>3)&3)>2||timeout<1000||timeout>UINT32_MAX)return false;
      const bool newNc=(config&128)!=0,newInputLow=(config&256)!=0;
      const uint8_t newPin=(config&512)?2:5;
      if((newNc!=contactNc||newInputLow!=relayInputLow||newPin!=relayPin)&&(controlArmed||(config&1)))return false;
      if(newPin!=relayPin){
        setPhysicalCoil(false);
        pinMode(relayPin,INPUT);
        relayPin=newPin;
        digitalWrite(relayPin,rain::relayOutputHigh(false,newInputLow)?HIGH:LOW);
        pinMode(relayPin,OUTPUT);
      }
      contactNc=newNc;relayInputLow=newInputLow;
      controlArmed=config&1;activeCoil=config&64;pulse.policy=(config>>1)&3;lossPolicy=(config>>3)&3;
      pulse.durationMs=duration;lossTimeoutMs=timeout;
      // Editing settings never silently clears an existing activation.
      if(pulse.active)pulse.restored=true;
    }
    applyControl();
  }
  if(!persistSettings()){coreMaintenance=true;ControlGuard lock;setPhysicalCoil(false);return false;}
  return true;
}
String roleControlsHtml(){
  return "<section><h2>Control del relé</h2><div class='grid'>"
    "<div class='metric'><span>Accionamiento lógico</span><b id='relayLocalState'>—</b><small id='relayLocalAge'>—</small></div>"
    "<div class='metric'><span>Salida GPIO</span><b id='relayElectricalState'>—</b><small id='relayPinInfo'>—</small></div>"
    "</div><div class='actions'><button id='relayTestButton' type='button'>Probar salida (2 s)</button></div>"
    "<small>Prueba local con control automático deshabilitado y carga desconectada. "
    "Requiere confirmación PROBAR. La salida indica la orden GPIO sin realimentación eléctrica.</small>"
    "<p id=\'relayTestHelp\' class=\'hint\'></p></section>"
    +pulseControls();
}
void configureRoleRoutes(){
  web.on("/relay-test",HTTP_POST,[](){
    if(!corePostAllowed())return;
    if(web.arg("confirm")!="PROBAR"){web.send(400,"text/plain","Confirmación requerida: PROBAR");return;}
    bool ok=false;
    {ControlGuard lock;
      const uint64_t now=uptimeMs();
      if(!controlArmed&&!coreMaintenance&&!relayTestRunning&&(now-lastRelaySelfTestMs>10000||!lastRelaySelfTestMs)){
        lastRelaySelfTestMs=now;relayTestRunning=true;relayTestUntilMs=now+2000;
        setPhysicalCoil(true);ok=true;
      }
    }
    if(ok)recordEvent("relay_test");
    web.send(ok?202:409,"text/plain",ok?"Prueba GPIO activa durante 2 s":"Deshabilita el control automático o espera a que termine la prueba");
  });
  web.on("/receiver",HTTP_POST,[](){
    if(!corePostAllowed())return;uint64_t armed,policy,loss,polarity,seconds,duration,contact,input_low,pin;
    if(!readUnsigned("armed",armed)||!readUnsigned("policy",policy)||!readUnsigned("loss",loss)||!readUnsigned("polarity",polarity)||
       !readUnsigned("loss_seconds",seconds)||!readUnsigned("contact",contact)||!readUnsigned("input_low",input_low)||!readUnsigned("pin",pin)||!readDuration(duration)||armed>1||policy>2||loss>2||polarity>1||contact>1||input_low>1||(pin!=2&&pin!=5)||
       seconds<1||seconds>UINT32_MAX/1000||(armed&&web.arg("confirm")!="ARMAR")){
      web.send(400,"text/plain","Configuracion invalida o falta ARMAR");return;}
    bool ok=configureReceiver(armed|(policy<<1)|(loss<<3)|(polarity<<6)|(contact<<7)|(input_low<<8)|((pin==2?1UL:0UL)<<9),duration,seconds*1000,false);
    web.send(ok?200:500,"text/plain",ok?"Configuracion guardada":"Fallo al guardar; salida en mantenimiento");
  });
  web.on("/reset-activation",HTTP_POST,[](){if(!corePostAllowed())return;
    bool ok=configureReceiver(0,0,0,true);web.send(ok?200:409,"text/plain",ok?"Activacion rearmada":"Rearme bloqueado: requiere sensor seco, enlace vigente y memoria disponible");});
}
uint32_t configWord(){return (controlArmed?1:0)|(pulse.policy<<1)|(lossPolicy<<3)|(pulse.restored?32:0)|(activeCoil?64:0)|(contactNc?128:0)|(relayInputLow?256:0)|(relayPin==2?512:0);}
String getStatusJson(){
  ControlGuard lock;
  return "{\"role\":\"RX\",\"version\":\""+String(FW_VERSION)+"\",\"wet\":"+String(lastRain?"true":"false")+
    ",\"simulated\":"+String(simulated?"true":"false")+",\"wet_state_age_ms\":\""+u64(wetStateSinceMs?uptimeMs()-wetStateSinceMs:0)+"\""+
    ",\"relay_state_age_ms\":\""+u64(relayStateSinceMs?uptimeMs()-relayStateSinceMs:0)+"\""+",\"relay_pin\":"+String(relayPin)+",\"ao\":"+String(lastAO)+",\"battery_mv\":"+String(lastBatteryMv)+
    ",\"link_ok\":"+String(rxLinkOk()?"true":"false")+",\"comms_lost\":"+String(commsLost?"true":"false")+",\"peer_heartbeat_ms\":"+String(peerHeartbeatMs)+",\"ack_age_ms\":"+(haveState?u64(uptimeMs()-lastFresh):String("null"))+
    ",\"rssi_dbm\":"+String(lastRssi,1)+",\"snr_db\":"+String(lastSnr,1)+",\"rx_known\":true,\"rx_armed\":"+String(controlArmed?"true":"false")+
    ",\"contact_nc\":"+String(contactNc?"true":"false")+",\"relay_input_low\":"+String(relayInputLow?"true":"false")+",\"rx_coil\":"+String(coilOn?"true":"false")+",\"rx_active\":"+String(relayActive()?"true":"false")+
    ",\"rx_restored\":"+String(pulse.restored?"true":"false")+",\"rx_maintenance\":"+String(coreMaintenance?"true":"false")+
    ",\"remaining_ms\":\""+u64(pulse.remaining(uptimeMs()))+"\",\"pulse_ms\":\""+u64(pulse.durationMs)+"\""+
    ",\"policy\":"+String(pulse.policy)+",\"loss\":"+String(lossPolicy)+",\"polarity\":"+String(activeCoil?1:0)+",\"loss_seconds\":"+u64(lossTimeoutMs/1000)+
    ",\"relay_test\":"+String(relayTestRunning?"true":"false")+",\"relay_output_high\":"+String(digitalRead(relayPin)==HIGH?"true":"false")+
    ",\"relay_active_high\":"+String(relayInputLow?"false":"true")+
    ",\"reply_deferred\":"+String(replyDeferred)+",\"reply_sent\":"+String(replySent)+",\"reply_errors\":"+String(replySendErrors)+
    ",\"rx_packets\":"+String(rxCount)+",\"duplicates\":"+String(duplicates)+",\"auth_rejects\":"+String(authRejects)+
    ",\"sf\":"+String(LORA_SF)+",\"frequency_mhz\":"+String(LORA_FREQUENCY_MHZ,1)+coreStatusFields()+"}";
}
// ACK y desafíos diferidos: conservamos la respuesta al agotar el tiempo de emisión.
void scheduleReply(const RainFrame &f){
  queuedReply=f;replyPending=true;replyReadyMs=uptimeMs()+75;
}
void serviceReply(){
  if(!replyPending||coreMaintenance||uptimeMs()<replyReadyMs)return;
  const int16_t rc=transmitFrame(queuedReply,onRadioReceive);
  if(rc==RADIOLIB_ERR_NONE){replyPending=false;++replySent;return;}
  if(rc==-32000){++replyDeferred;replyReadyMs=uptimeMs()+min((uint64_t)10000,coreDutyWaitMs()+250);return;}
  ++replySendErrors;replyReadyMs=uptimeMs()+500;
}
void sendChallenge(){
  auto f=newFrame(FRAME_CHALLENGE,RX_ID,ownBoot,++ownSeq,receiverSession);f.echo=senderNonce;
  scheduleReply(f);radio.startReceive();
}
void sendAck(uint32_t seq,bool config=false,bool rejected=false){
  auto f=newFrame(FRAME_ACK,RX_ID,ownBoot,++ownSeq,receiverSession);f.ackSeq=seq;
  {ControlGuard lock;f.wet=lastRain?1:0;f.event=lastEvent;f.config=configWord();f.pulseMs=pulse.durationMs;f.remainingMs=pulse.remaining(uptimeMs());f.echo=lossTimeoutMs;
    if(controlArmed)f.flags|=ACK_RELAY_ARMED;if(coilOn)f.flags|=ACK_COIL_ON;if(relayActive())f.flags|=ACK_ACTIVE;
    if(coreMaintenance)f.flags|=ACK_MAINTENANCE;if(config)f.flags|=ACK_CONFIG;if(rejected)f.flags|=ACK_REJECTED;
    f.peerRssi10=roundf(lastRssi*10);f.peerSnr10=roundf(lastSnr*10);}
  scheduleReply(f);radio.startReceive();
}
void processReceived(){
  if(!rxInterrupt)return;rxInterrupt=false;RainFrame f{};
  if(!readFrame(f)){++authRejects;radio.startReceive();return;}
  if(managementProcess(f)){radio.startReceive();return;}
  if(f.sender!=TX_ID){radio.startReceive();return;}
  if(f.type==FRAME_HELLO&&f.session){
    // Fresh unpredictable challenge authenticates states; boot counters need not survive a flash erase.
    if(!sessionReady||senderBoot!=f.boot||senderNonce!=f.session){
      if(!sessionReady||senderBoot!=f.boot){mgPeerKnown=false;mgPeer={};mgPeerRevision=0;}
      senderBoot=f.boot;senderNonce=f.session;receiverSession=random64();if(!receiverSession)receiverSession=1;
      sessionReady=true;highestSeq=0;replyPending=false;
    }
    sendChallenge();return;
  }
  if(!sessionReady||f.boot!=senderBoot||f.session!=receiverSession){radio.startReceive();return;}
  if(f.seq<highestSeq){radio.startReceive();return;}
  if(f.seq==highestSeq&&highestSeq){++duplicates;sendAck(f.seq,f.type==FRAME_CONFIG);return;}
  if(f.type==FRAME_MAINT){highestSeq=f.seq;recordEvent("sender_maintenance");radio.startReceive();return;}
  if(coreMaintenance){sendAck(f.seq,false,true);return;}
  if(f.type==FRAME_CONFIG){
    if(configureReceiver(f.config,f.pulseMs,f.echo,f.flags&1)){highestSeq=f.seq;sendAck(f.seq,true);}
    else sendAck(f.seq,false,true);return;
  }
  if(f.type!=FRAME_STATE||f.wet>1){radio.startReceive();return;}
  {ControlGuard lock;
    highestSeq=f.seq;lastFresh=uptimeMs();mgLastSeen=lastFresh;mgPeerAsleep=f.flags&4;
    if(!haveState||lastRain!=(f.wet!=0))wetStateSinceMs=lastFresh;
    haveState=true;commsLost=false;lastRain=f.wet;simulated=f.flags&STATE_SIMULATED;
    if(f.ackSeq>=1&&f.ackSeq<=86400)peerHeartbeatMs=f.ackSeq*1000;
    managementObserveState(f);
    lastAO=f.ao;lastBatteryMv=f.batteryMv;lastRssi=radio.getRSSI();lastSnr=radio.getSNR();++rxCount;
    bool event=f.event&&f.event!=lastEvent;bool wasActive=pulse.active;
    pulse.observe(lastRain,event,lastFresh);pulse.tick(lastFresh,true);if(event){lastEvent=f.event;persistDirty=true;}
    if(pulse.active!=wasActive)persistDirty=true;applyControl();
  }
  if(persistDirty&&!persistSettings()){coreMaintenance=true;ControlGuard lock;setPhysicalCoil(false);}
  sendAck(f.seq);
}
void setup(){
  Serial.begin(115200);coreRole="RX";coreLoadDevice();loadSettings();
  digitalWrite(relayPin,rain::relayOutputHigh(false,relayInputLow)?HIGH:LOW);
  pinMode(relayPin,OUTPUT);relayStateSinceMs=uptimeMs();
  coreInitLog();
  controlMutex=xSemaphoreCreateMutex();if(!controlMutex)while(true)delay(1000);
  if(xTaskCreate(controlTask,"relay",4096,nullptr,2,nullptr)!=pdPASS){coreMaintenance=true;while(true)delay(1000);}
  coreInitDisplay();ownBoot=esp_random();managementOwnBoot=ownBoot;managementOnRx=onRadioReceive;
  if(!initRadio(onRadioReceive)){coreScreen("ERROR RADIO","Salida deshabilitada","","","","");coreMaintenance=true;while(true)delay(1000);}
  coreStartPortal(false);
}
void loop(){
  coreTickPortal();processReceived();serviceReply();managementTick();
  const uint64_t radioNow=uptimeMs();
  bool hasRecent=false, lostNow=true, relayNow=false;
  {ControlGuard lock;hasRecent=haveState;lostNow=commsLost;relayNow=lastRelayActive;}
  if(hasRecent&&lostNow&&!loggedLinkLost){loggedLinkLost=true;recordEvent("link_lost");}
  if(hasRecent&&!lostNow&&loggedLinkLost){loggedLinkLost=false;recordEvent("link_restored");}
  if(relayNow!=loggedRelayActive){loggedRelayActive=relayNow;recordEvent(relayNow?"relay_on":"relay_off");}
  if(!coreMaintenance&&!coreOtaInProgress&&
     (coreRxRecoveryNeeded||(!hasRecent?radioNow>60000:!rxLinkOk()&&radioNow-lastFresh>60000))&&
     radioNow-lastRadioProbeMs>15000&&(coreRxRecoveryNeeded||!replyPending)){
    lastRadioProbeMs=radioNow;
    coreRecoverRadio(onRadioReceive,"RX without fresh LoRa state");
  }
  if(persistDirty&&!persistSettings()){coreMaintenance=true;ControlGuard lock;setPhysicalCoil(false);}
  static uint64_t lastScreen=0;uint64_t now=uptimeMs();
  if(now-lastScreen>=1000){lastScreen=now;
    if(coreApActive&&now<30000)coreDisplayPortalIfNeeded();else {
      ControlGuard lock;coreQuality=rain::quality(lastRssi,lastSnr,rxLinkOk(),rxCount,rxCount);coreTimer=pulse.active&&!pulse.restored?"Temp "+compactTime(pulse.remaining(now)):"Temp --";coreScreen("LoraRainSensor RX",String("Rel ")+(relayActive()?"ON":"OFF")+"|GPIO "+String(relayPin),String(lastRain?"MOJADO":"SECO")+(simulated?" MAN":" SENSOR"),rxLinkOk()?"ENLACE OK":"SIN ENLACE",
        String("Rel ")+(relayActive()?"ON ":"OFF ")+stateAge(now-relayStateSinceMs),coreTimer);}}
  delay(1);
}

uint32_t managementSensorRevision(){return 0;}
uint64_t managementSession(){return sessionReady?receiverSession:0;}
void managementRxAction(){}
void requestLink(){ManagementData d{};d.kind=3;managementQueue(d);}
Operational getOperational(){Operational op{};op.role=2;op.prototype=corePrototype;op.display=coreDisplayEnabled;op.uptime=uptimeMs();op.freeHeap=ESP.getFreeHeap();op.rxPackets=rxCount;op.txPackets=coreTxCount;op.errors=authRejects+coreTxErrors;op.ao=lastAO;op.battery=lastBatteryMv;op.doLevel=-1;op.wet=lastRain;op.simulated=simulated;return op;}
bool applyOperational(const Operational &){return false;}

bool managementCanSend(){return !replyPending;}
