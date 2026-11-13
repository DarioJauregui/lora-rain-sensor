#include <Arduino.h>
#include "pair_key.h"
#include <RainCore.h>
#include <Controls.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

constexpr uint8_t PIN_RELAY_NPN=5;
SemaphoreHandle_t controlMutex=nullptr;
struct ControlGuard {
  ControlGuard(){xSemaphoreTake(controlMutex,portMAX_DELAY);}
  ~ControlGuard(){xSemaphoreGive(controlMutex);}
};
rain::Pulse pulse;
bool controlArmed=false, activeCoil=true, coilOn=false, haveState=false, lastRain=false, simulated=false;
bool sessionReady=false, commsLost=true;
std::atomic<bool> persistDirty{false};
uint8_t lossPolicy=rain::HOLD_OUTPUT;
uint64_t lossTimeoutMs=180000, lastFresh=0, receiverSession=0, senderNonce=0;
uint32_t ownBoot=0, senderBoot=0, ownSeq=0, highestSeq=0, lastEvent=0;
uint32_t rxCount=0, duplicates=0, authRejects=0;
uint16_t lastAO=0, lastBatteryMv=0;
float lastRssi=-150, lastSnr=-20;
volatile bool rxInterrupt=false;

void IRAM_ATTR onRadioReceive(){rxInterrupt=true;}
void recordEvent(const char *event){Serial.printf("%llu %s\n",(unsigned long long)uptimeMs(),event);}
void setPhysicalCoil(bool on){coilOn=on;digitalWrite(PIN_RELAY_NPN,on?HIGH:LOW);}
// Caller holds controlMutex. All paths, including web and OTA, share this gate.
void applyControl(){
  setPhysicalCoil(rain::outputCoil(coreMaintenance,controlArmed,haveState,commsLost,
                                 lossPolicy,pulse.active,activeCoil,coilOn));
}
void controlTask(void *){
  for(;;){
    {ControlGuard lock;
      uint64_t now=uptimeMs();commsLost=!haveState||now-lastFresh>lossTimeoutMs;
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
bool relayActive(){return controlArmed&&haveState&&!coreMaintenance&&(coilOn==activeCoil);}
bool persistSettings(){
  ReceiverSettings saved{};
  {ControlGuard lock;saved.magic=0x52585332;saved.duration=pulse.durationMs;saved.event=lastEvent;saved.policy=pulse.policy;
    saved.loss=lossPolicy;saved.armed=controlArmed;saved.active=pulse.active;saved.polarity=activeCoil;saved.timeout=lossTimeoutMs;persistDirty=false;}
  Preferences p;if(!p.begin("rainrx2",false))return false;
  bool ok=p.putBytes("settings",&saved,sizeof(saved))==sizeof(saved);p.end();return ok;
}
void beforeOta(){ControlGuard lock;setPhysicalCoil(false);recordEvent("maintenance_latched");}
void loadSettings(){
  ReceiverSettings saved{};Preferences p;
  if(!p.begin("rainrx2",true))return;
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
      if((config&~0x5fUL)||((config>>1)&3)>2||((config>>3)&3)>2||timeout<1000||timeout>UINT32_MAX)return false;
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
String roleControlsHtml(){return "<section><h2>Control del receptor</h2><p class='hint'>La salida arranca desarmada en una instalación nueva. Una activación pendiente tras un reinicio requiere rearme manual.</p></section>"+pulseControls();}
void configureRoleRoutes(){
  web.on("/receiver",HTTP_POST,[](){
    if(!corePostAllowed())return;uint64_t armed,policy,loss,polarity,seconds,duration;
    if(!readUnsigned("armed",armed)||!readUnsigned("policy",policy)||!readUnsigned("loss",loss)||!readUnsigned("polarity",polarity)||
       !readUnsigned("loss_seconds",seconds)||!readDuration(duration)||armed>1||policy>2||loss>2||polarity>1||
       seconds<1||seconds>UINT32_MAX/1000||(armed&&web.arg("confirm")!="ARMAR")){
      web.send(400,"text/plain","Configuracion invalida o falta ARMAR");return;}
    bool ok=configureReceiver(armed|(policy<<1)|(loss<<3)|(polarity<<6),duration,seconds*1000,false);
    web.send(ok?200:500,"text/plain",ok?"Configuracion guardada":"Fallo al guardar; salida en mantenimiento");
  });
  web.on("/reset-activation",HTTP_POST,[](){if(!corePostAllowed())return;
    bool ok=configureReceiver(0,0,0,true);web.send(ok?200:409,"text/plain",ok?"Activacion rearmada":"Rearme bloqueado: requiere sensor seco, enlace vigente y memoria disponible");});
}
uint32_t configWord(){return (controlArmed?1:0)|(pulse.policy<<1)|(lossPolicy<<3)|(pulse.restored?32:0)|(activeCoil?64:0);}
String getStatusJson(){
  ControlGuard lock;
  return "{\"role\":\"RX\",\"version\":\""+String(FW_VERSION)+"\",\"wet\":"+String(lastRain?"true":"false")+
    ",\"simulated\":"+String(simulated?"true":"false")+",\"ao\":"+String(lastAO)+",\"battery_mv\":"+String(lastBatteryMv)+
    ",\"link_ok\":"+String(haveState&&!commsLost?"true":"false")+",\"ack_age_ms\":"+(haveState?u64(uptimeMs()-lastFresh):String("null"))+
    ",\"rssi_dbm\":"+String(lastRssi,1)+",\"snr_db\":"+String(lastSnr,1)+",\"rx_known\":true,\"rx_armed\":"+String(controlArmed?"true":"false")+
    ",\"rx_coil\":"+String(coilOn?"true":"false")+",\"rx_active\":"+String(relayActive()?"true":"false")+
    ",\"rx_restored\":"+String(pulse.restored?"true":"false")+",\"rx_maintenance\":"+String(coreMaintenance?"true":"false")+
    ",\"remaining_ms\":\""+u64(pulse.remaining(uptimeMs()))+"\",\"pulse_ms\":\""+u64(pulse.durationMs)+"\""+
    ",\"policy\":"+String(pulse.policy)+",\"loss\":"+String(lossPolicy)+",\"polarity\":"+String(activeCoil?1:0)+",\"loss_seconds\":"+u64(lossTimeoutMs/1000)+
    ",\"rx_packets\":"+String(rxCount)+",\"duplicates\":"+String(duplicates)+",\"auth_rejects\":"+String(authRejects)+
    ",\"sf\":"+String(LORA_SF)+",\"frequency_mhz\":"+String(LORA_FREQUENCY_MHZ,1)+coreStatusFields()+"}";
}
void sendChallenge(){
  auto f=newFrame(FRAME_CHALLENGE,RX_ID,ownBoot,++ownSeq,receiverSession);f.echo=senderNonce;
  delay(65);transmitFrame(f,onRadioReceive);
}
void sendAck(uint32_t seq,bool config=false,bool rejected=false){
  auto f=newFrame(FRAME_ACK,RX_ID,ownBoot,++ownSeq,receiverSession);f.ackSeq=seq;
  {ControlGuard lock;f.event=lastEvent;f.config=configWord();f.pulseMs=pulse.durationMs;f.remainingMs=pulse.remaining(uptimeMs());f.echo=lossTimeoutMs;
    if(controlArmed)f.flags|=ACK_RELAY_ARMED;if(coilOn)f.flags|=ACK_COIL_ON;if(relayActive())f.flags|=ACK_ACTIVE;
    if(coreMaintenance)f.flags|=ACK_MAINTENANCE;if(config)f.flags|=ACK_CONFIG;if(rejected)f.flags|=ACK_REJECTED;
    f.peerRssi10=roundf(lastRssi*10);f.peerSnr10=roundf(lastSnr*10);}
  delay(65);transmitFrame(f,onRadioReceive);
}
void processReceived(){
  if(!rxInterrupt)return;rxInterrupt=false;RainFrame f{};
  if(!readFrame(f)){++authRejects;radio.startReceive();return;}
  if(f.sender!=TX_ID){radio.startReceive();return;}
  if(f.type==FRAME_HELLO&&f.session){
    // Fresh unpredictable challenge authenticates states; boot counters need not survive a flash erase.
    if(!sessionReady||senderBoot!=f.boot||senderNonce!=f.session){
      senderBoot=f.boot;senderNonce=f.session;receiverSession=random64();if(!receiverSession)receiverSession=1;
      sessionReady=true;highestSeq=0;
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
    highestSeq=f.seq;lastFresh=uptimeMs();haveState=true;commsLost=false;lastRain=f.wet;simulated=f.flags&STATE_SIMULATED;
    lastAO=f.ao;lastBatteryMv=f.batteryMv;lastRssi=radio.getRSSI();lastSnr=radio.getSNR();++rxCount;
    bool event=f.event&&f.event!=lastEvent;bool wasActive=pulse.active;
    pulse.observe(lastRain,event,lastFresh);pulse.tick(lastFresh,true);if(event){lastEvent=f.event;persistDirty=true;}
    if(pulse.active!=wasActive)persistDirty=true;applyControl();
  }
  if(persistDirty&&!persistSettings()){coreMaintenance=true;ControlGuard lock;setPhysicalCoil(false);}
  sendAck(f.seq);
}
void setup(){
  digitalWrite(PIN_RELAY_NPN,LOW);pinMode(PIN_RELAY_NPN,OUTPUT);
  Serial.begin(115200);coreRole="RX";coreLoadDevice();loadSettings();
  controlMutex=xSemaphoreCreateMutex();if(!controlMutex)while(true)delay(1000);
  if(xTaskCreate(controlTask,"relay",4096,nullptr,2,nullptr)!=pdPASS){coreMaintenance=true;while(true)delay(1000);}
  coreInitDisplay();ownBoot=esp_random();
  if(!initRadio(onRadioReceive)){coreScreen("ERROR RADIO","Salida deshabilitada","","","","");coreMaintenance=true;while(true)delay(1000);}
  coreStartPortal(false);
}
void loop(){
  coreTickPortal();processReceived();
  if(persistDirty&&!persistSettings()){coreMaintenance=true;ControlGuard lock;setPhysicalCoil(false);}
  static uint64_t lastScreen=0;uint64_t now=uptimeMs();
  if(now-lastScreen>=1000){lastScreen=now;
    if(coreApActive&&now<30000)coreDisplayPortalIfNeeded();else {
      ControlGuard lock;coreScreen("RAIN LINK / RX",pulse.active?"RELE ACTIVO":"RELE INACTIVO",lastRain?"MOJADO":"SECO",commsLost?"SIN ENLACE":"ENLACE OK",
        "BOBINA "+String(coilOn?"ON":"OFF"),controlArmed?"ARMADO":"DESARMADO");}}
  delay(1);
}
