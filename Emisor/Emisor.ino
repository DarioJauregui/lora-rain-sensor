#include <Arduino.h>
#include "pair_key.h"
#include <RainCore.h>
#include <Controls.h>
#include <LittleFS.h>
#include <esp_sleep.h>

constexpr uint8_t PIN_RAIN_AO=4, PIN_RAIN_DO=2;
constexpr uint32_t ACK_TIMEOUT_MS=3000;
RTC_DATA_ATTR uint32_t savedBoot=0, savedSeq=0, savedSignature=0;
RTC_DATA_ATTR uint64_t savedSession=0;
RTC_DATA_ATTR bool savedWet=false;
constexpr uint32_t RTC_SIGNATURE=0x52414932;
uint32_t senderBoot=0, txSequence=0;
uint64_t sessionId=0, helloNonce=0;
volatile bool rxInterrupt=false;
rain::Detector detector;
uint16_t analogRain=4095, rainOnThreshold=3500, rainOffThreshold=3900, batteryMv=0;
int doLevel=-1, simulation=-1, batteryGate=0;
uint32_t testSampleMs=50, confirmMs=0, heartbeatSeconds=30, sleepSeconds=120;
bool doWakeEnabled=false, resumed=false;
rain::PendingRain rainEvent;
uint64_t lastSample=0, lastTx=0, lastAck=0, nextSend=0, ackStart=0, lastScreen=0;
uint32_t helloBackoff=15000, attempts=0, sentStates=0, ackedStates=0, retries=0, ackTimeouts=0, authRejects=0;
float lastRssi=-150, lastSnr=-20, remoteRssi=-150, remoteSnr=-20;
uint64_t lastRtt=0, txStarted=0;
RainFrame pendingFrame{}, receiver{};
bool pending=false, awaitingAck=false, rxKnown=false;
bool configPending=false, resetPending=false;
uint32_t requestedConfig=64, requestedLossSeconds=180;
uint64_t requestedPulseMs=1000;
bool fsReady=false, recording=false;
File recordFile;
uint64_t recordStarted=0, recordElapsed=0, lastRecordSnapshot=0;
uint32_t recordRows=0;
String recordError;
String recordStartIso;
String commandResult;
constexpr size_t RECORD_LIMIT=1024*1024;

void IRAM_ATTR onRadioReceive() { rxInterrupt=true; }
bool wetNow() { return simulation<0?detector.wet:simulation==1; }
bool linkOk() { return lastAck && uptimeMs()-lastAck < (uint64_t)heartbeatSeconds*3000+10000; }
bool saveEvent() {
  Preferences p;
  bool ok=p.begin("rainevent",false);
  if(ok){ok=p.putULong64("state",((uint64_t)rainEvent.id<<1)|(rainEvent.pending?1:0))==8;p.end();}
  if(!ok){coreMaintenance=true;recordError="Error al guardar evento; reiniciar";}
  return ok;
}
String recordFilename() {
  String stamp=recordStartIso.length()?recordStartIso:"hora-desconocida";stamp.replace(":","-");
  return "rain_"+stamp+"_"+u64(recordElapsed)+"ms.csv";
}
bool validIso(const String &s) {
  if(s.length()!=24)return false;
  for(size_t i=0;i<s.length();++i){char c=s[i];
    if(i==4||i==7){if(c!='-')return false;}
    else if(i==10){if(c!='T')return false;}
    else if(i==13||i==16){if(c!=':')return false;}
    else if(i==19){if(c!='.')return false;}
    else if(i==23){if(c!='Z')return false;}
    else if(c<'0'||c>'9')return false;
  }
  return true;
}
void recordEvent(const char *event) {
  if (!recording || !recordFile) return;
  uint64_t now=uptimeMs(); recordElapsed=now-recordStarted;
  String line=recordStartIso+","+u64(recordElapsed)+","+event+","+String(analogRain)+","+String(doLevel)+","+String(wetNow())+","+String(simulation>=0)+","+
    String(batteryMv)+","+(lastAck?u64(now-lastAck):String(""))+","+String(lastRssi,1)+","+String(lastSnr,1)+","+
    String(coreTxCount)+","+String(ackedStates)+","+String(retries)+","+String(ackTimeouts)+","+String(lastRtt)+","+
    String(rxKnown)+","+String(receiver.flags)+","+u64(receiver.remainingMs)+","+String(rainEvent.id)+","+String(rainEvent.pending)+","+
    String(coreAirtime.used)+","+u64(coreDutyWaitMs())+","+String(remoteRssi,1)+","+String(remoteSnr,1)+","+
    u64(receiver.pulseMs)+","+String(receiver.config)+","+String(lastAck?linkOk():false)+","+String(coreMaintenance)+"\n";
  if(recordFile.size()+line.length()>RECORD_LIMIT) {
    recordError="Memoria de grabacion llena";recordFile.flush();recordFile.close();recording=false;return;
  }
  if(recordFile.print(line)!=line.length()) {recordError="Error de escritura";recordFile.close();recording=false;return;}
  ++recordRows; recordFile.flush();
}
void beforeOta() {
  if(recording){recordEvent("record_stop_ota");recordFile.close();recording=false;}
  // Informative only: RX applies its configured link-loss policy if updates stop.
  if(sessionId) { auto f=newFrame(FRAME_MAINT,TX_ID,senderBoot,++txSequence,sessionId);transmitFrame(f,onRadioReceive); }
}
uint16_t readRain() { uint32_t sum=0;for(int i=0;i<8;++i)sum+=analogRead(PIN_RAIN_AO);return sum/8; }
uint16_t sampleBattery(bool high) {
  pinMode(37,OUTPUT);digitalWrite(37,high?HIGH:LOW);delay(3);
  uint32_t value=0;for(int i=0;i<6;++i)value+=analogReadMilliVolts(1);return (value/6)*4.9f;
}
void readBattery() {
  if(!batteryGate) {
    uint16_t hi=sampleBattery(true),lo=sampleBattery(false);
    if(hi>=2800 && hi<=4400 && (lo<2000 || lo>4500))batteryGate=1;
    else if(lo>=2800 && lo<=4400 && (hi<2000 || hi>4500))batteryGate=-1;
    else {batteryMv=0;digitalWrite(37,LOW);return;}
  }
  batteryMv=sampleBattery(batteryGate==1);digitalWrite(37,batteryGate==1?LOW:HIGH);
}
void loadSettings() {
  Preferences p;p.begin("rainsensor",true);rainOnThreshold=p.getUShort("on",3500);rainOffThreshold=p.getUShort("off",3900);p.end();
  if(rainOnThreshold>3900 || rainOffThreshold>4095 || rainOffThreshold<=rainOnThreshold){rainOnThreshold=3500;rainOffThreshold=3900;}
  p.begin("rainmode",true);testSampleMs=p.getUInt("sample",50);sleepSeconds=p.getUInt("sleep",120);
  heartbeatSeconds=p.getUInt("heartbeat",30);confirmMs=p.getUInt("confirm",0);doWakeEnabled=p.getBool("dowake",false);p.end();
  testSampleMs=constrain(testSampleMs,10UL,60000UL);sleepSeconds=constrain(sleepSeconds,1UL,86400UL);
  heartbeatSeconds=constrain(heartbeatSeconds,1UL,86400UL);confirmMs=constrain(confirmMs,0UL,60000UL);
  p.begin("rainevent",true);uint64_t state=p.getULong64("state",0);rainEvent.id=state>>1;rainEvent.pending=state&1;p.end();
}
void queueState() {
  // Do not mutate an in-flight packet. The current state is queued after its ACK.
  if(pending || awaitingAck)return;
  pendingFrame=newFrame(FRAME_STATE,TX_ID,senderBoot,++txSequence,sessionId);
  pendingFrame.wet=wetNow();pendingFrame.ao=analogRain;pendingFrame.batteryMv=batteryMv;
  pendingFrame.event=rainEvent.pending?rainEvent.id:0;
  pendingFrame.ackSeq=corePrototype?heartbeatSeconds:sleepSeconds;
  if(simulation>=0)pendingFrame.flags|=STATE_SIMULATED;
  pending=true;attempts=0;nextSend=uptimeMs();
}
void latchRainEvent() {
  // A single outstanding latch is sufficient for an activation; later episodes coalesce until delivery.
  if(!rainEvent.pending){uint32_t next;do{next=esp_random();}while(!next||next==rainEvent.id);rainEvent.raise(next);saveEvent();}
  recordEvent("rain_event");
}
void updateSensor(bool immediate=false) {
  uint64_t now=uptimeMs();if(!immediate && now-lastSample<testSampleMs)return;lastSample=now;
  bool oldWet=wetNow();analogRain=readRain();doLevel=digitalRead(PIN_RAIN_DO);
  detector.sample(analogRain,rainOnThreshold,rainOffThreshold,now,corePrototype?0:confirmMs);
  if(oldWet!=wetNow()) {if(wetNow())latchRainEvent();recordEvent("sensor_change");queueState();}
}
String roleControlsHtml() {
  String s=F("<section><h2>Experimento</h2><div class='actions'>"
    "<button class='primary' id='startRecord' data-command='/record/start'>Grabar</button>"
    "<button id='stopRecord' data-command='/record/stop'>Detener</button>"
    "<a class='button' href='/record.csv' download>Descargar CSV</a></div><p id='recordState' aria-live='polite'>Sin grabación</p>"
    "<small>Tiempo desde el inicio. Registro automático en el emisor, aunque se desconecte el móvil. "
    "Una grabación, hasta 1 MiB; se detiene al llenar la memoria. No borra datos al reiniciar.</small></section>"
    "<section><h2>Prueba de lluvia</h2><div class='actions'>"
    "<button data-command='/simulate' data-value='wet'>Simular mojado</button>"
    "<button data-command='/simulate' data-value='dry'>Simular seco</button>"
    "<button data-command='/simulate' data-value='sensor'>Usar sensor físico</button></div>"
    "<small>La simulación permanece hasta volver al sensor o reiniciar. Disponible solo en prototipo.</small></section>");
  s+=F("<dialog id='replaceRecord'><h2>Grabación anterior</h2><p>Descarga el archivo anterior antes de sustituirlo.</p>"
    "<div class='actions'><a class='button' href='/record.csv' download>Descargar anterior</a>"
    "<button id='confirmRecord'>Sustituir y grabar</button><button id='cancelRecord'>Cancelar</button></div></dialog>"
    "<details><summary>Preparar memoria de grabación</summary><form action='/record/prepare' data-save>"
    "<p class='hint'>Solo si aparece memoria no preparada. Formatear elimina cualquier grabación de esta partición. "
    "No cambia la configuración ni la clave.</p><label>Escribe BORRAR MEMORIA<input name='confirm' required></label><button>Preparar memoria</button></form></details>");
  s+=pulseControls();
  s+=F("<details><summary>Sensor y envío</summary><form action='/settings' data-save><div class='grid'>"
    "<label>Mojado si AO ≤<input name='on' type='number' min='0' max='3900' value='3500' required></label>"
    "<label>Seco si AO ≥<input name='off' type='number' min='1' max='4095' value='3900' required></label>"
    "<label>Lectura despierto (ms)<input name='sample' type='number' min='10' max='60000' value='50' required></label>"
    "<label>Confirmación producción (ms)<input name='confirm_ms' type='number' min='0' max='60000' value='0' required></label>"
    "<label>Telemetría prototipo (s)<input name='heartbeat' type='number' min='1' max='86400' value='30' required></label>"
    "<label>Sueño producción (s)<input name='sleep' type='number' min='1' max='86400' value='120' required></label>"
    "<label>Despertar DO=LOW<select name='dowake'><option value='0'>Desactivado</option><option value='1'>Activado tras validar</option></select></label></div>"
    "<p class='hint'>AO es lectura ADC, no intensidad de precipitación. Prototipo reacciona en la siguiente lectura. "
    "Producción confirma al despertar y puede retrasarse hasta el intervalo de sueño. "
    "Los envíos respetan el presupuesto de radio aunque el intervalo elegido sea menor.</p><button>Guardar</button></form></details>");
  return s;
}
void configureRoleRoutes() {
  web.on("/settings",HTTP_POST,[](){
    if(!corePostAllowed())return;uint64_t on,off,sample,confirmation,heartbeat,sleep,dowake;
    if(!readUnsigned("on",on)||!readUnsigned("off",off)||!readUnsigned("sample",sample)||!readUnsigned("confirm_ms",confirmation)||
       !readUnsigned("heartbeat",heartbeat)||!readUnsigned("sleep",sleep)||!readUnsigned("dowake",dowake)||
       on>3900||off>4095||off<=on||sample<10||sample>60000||confirmation>60000||heartbeat<1||heartbeat>86400||sleep<1||sleep>86400||dowake>1){web.send(400,"text/plain","Configuracion invalida");return;}
    rainOnThreshold=on;rainOffThreshold=off;testSampleMs=sample;confirmMs=confirmation;heartbeatSeconds=heartbeat;sleepSeconds=sleep;doWakeEnabled=dowake;
    Preferences p;p.begin("rainsensor",false);p.putUShort("on",on);p.putUShort("off",off);p.end();p.begin("rainmode",false);
    p.putUInt("sample",sample);p.putUInt("confirm",confirmation);p.putUInt("heartbeat",heartbeat);p.putUInt("sleep",sleep);p.putBool("dowake",dowake);p.end();
    recordEvent("sensor_config");web.send(200,"text/plain","Configuracion guardada");
  });
  web.on("/simulate",HTTP_POST,[](){
    if(!corePostAllowed())return;if(!corePrototype){web.send(409,"text/plain","Solo en prototipo");return;}
    String value=web.arg("value");if(value!="wet"&&value!="dry"&&value!="sensor"){web.send(400,"text/plain","Origen invalido");return;}
    bool previous=wetNow();simulation=value=="sensor"?-1:value=="wet"?1:0;
    if(!previous&&wetNow())latchRainEvent();queueState();recordEvent("simulation_change");web.send(200,"text/plain","Origen actualizado; pendiente de confirmacion LoRa");
  });
  web.on("/receiver",HTTP_POST,[](){
    if(!corePostAllowed())return;uint64_t policy,loss,armed,polarity,seconds,duration;
    if(!readUnsigned("policy",policy)||!readUnsigned("loss",loss)||!readUnsigned("armed",armed)||!readUnsigned("loss_seconds",seconds)||!readUnsigned("polarity",polarity)||
       !readDuration(duration)||policy>2||loss>2||armed>1||polarity>1||seconds<1||seconds>UINT32_MAX/1000||
       (armed&&web.arg("confirm")!="ARMAR")){web.send(400,"text/plain","Configuracion invalida o falta ARMAR");return;}
    if(configPending||resetPending){web.send(409,"text/plain","Ya hay una orden pendiente");return;}
    requestedConfig=armed|(policy<<1)|(loss<<3)|(polarity<<6);requestedPulseMs=duration;requestedLossSeconds=seconds;configPending=true;commandResult="Pendiente";
    recordEvent("receiver_config_queued");web.send(202,"text/plain","Configuracion en cola. Comprueba config_pending hasta confirmacion.");
  });
  web.on("/reset-activation",HTTP_POST,[](){if(!corePostAllowed())return;if(configPending||resetPending){web.send(409,"text/plain","Orden pendiente");return;}
    resetPending=true;commandResult="Pendiente";recordEvent("reset_queued");web.send(202,"text/plain","Rearme en cola; requiere seco y enlace vigente en receptor");});
  web.on("/record/prepare",HTTP_POST,[](){
    if(!corePostAllowed())return;
    if(recording||fsReady||web.arg("confirm")!="BORRAR MEMORIA"){web.send(409,"text/plain","Memoria disponible, ocupada o confirmacion incorrecta");return;}
    fsReady=LittleFS.format()&&LittleFS.begin(false);
    if(fsReady){recordError="";recordStartIso="";recordRows=0;recordElapsed=0;}
    web.send(fsReady?200:500,"text/plain",fsReady?"Memoria preparada":"No se pudo preparar la particion LittleFS");
  });
  web.on("/record/start",HTTP_POST,[](){
    if(!corePostAllowed())return;if(!fsReady||!corePrototype){web.send(409,"text/plain","Grabacion requiere memoria disponible y modo prototipo");return;}
    if(recording){web.send(409,"text/plain","Ya se esta grabando");return;}
    if(LittleFS.exists("/experiment.csv")&&web.arg("replace")!="1"){web.send(409,"text/plain","Descarga o confirma sustituir la grabacion anterior");return;}
    String iso=web.arg("start_iso");if(!validIso(iso)){web.send(400,"text/plain","Fecha de inicio invalida; recarga la interfaz");return;}
    recordFile=LittleFS.open("/experiment.csv","w");if(!recordFile){web.send(500,"text/plain","No se pudo crear el archivo");return;}
    recordFile.println("start_utc,elapsed_ms,event,ao,do,wet,simulated,battery_mv,ack_age_ms,rssi_dbm,snr_db,tx_packets,acked_states,retries,ack_timeouts,rtt_ms,rx_known,rx_flags,remaining_ms,event_id,event_pending,airtime_used_ms,duty_wait_ms,rx_rssi_dbm,rx_snr_db,pulse_ms,rx_config,link_ok,maintenance");
    recordStartIso=iso;recordStarted=uptimeMs();recordRows=0;recordElapsed=0;recordError="";recording=true;recordEvent("record_start");web.send(200,"text/plain","Grabando");
  });
  web.on("/record/stop",HTTP_POST,[](){if(!corePostAllowed())return;if(recording){recordEvent("record_stop");recordFile.close();recording=false;}web.send(200,"text/plain","Grabacion detenida");});
  web.on("/record.csv",HTTP_GET,[](){
    if(!coreIsAuthenticated())return;if(!fsReady||!LittleFS.exists("/experiment.csv")){web.send(404,"text/plain","No hay grabacion");return;}
    // Download a closed, consistent file; recording can be started again explicitly.
    if(recording){recordEvent("record_stop_download");recordFile.close();recording=false;}
    File f=LittleFS.open("/experiment.csv","r");web.sendHeader("Content-Disposition","attachment; filename=\""+recordFilename()+"\"");
    web.streamFile(f,"text/csv; charset=utf-8");f.close();
  });
}
String getStatusJson() {
  String s="{\"role\":\"TX\",\"version\":\""+String(FW_VERSION)+"\",\"wet\":"+String(wetNow()?"true":"false")+
    ",\"simulated\":"+String(simulation>=0?"true":"false")+",\"ao\":"+String(analogRain)+",\"do\":"+String(doLevel)+
    ",\"battery_mv\":"+String(batteryMv)+",\"link_ok\":"+String(linkOk()?"true":"false")+
    ",\"ack_age_ms\":"+(lastAck?u64(uptimeMs()-lastAck):String("null"))+",\"rssi_dbm\":"+String(lastRssi,1)+",\"snr_db\":"+String(lastSnr,1)+
    ",\"rx_rssi_dbm\":"+String(remoteRssi,1)+",\"rx_snr_db\":"+String(remoteSnr,1)+",\"rtt_ms\":\""+u64(lastRtt)+"\""+
    ",\"acked_states\":"+String(ackedStates)+",\"sent_states\":"+String(sentStates)+",\"retries\":"+String(retries)+",\"ack_timeouts\":"+String(ackTimeouts)+
    ",\"auth_rejects\":"+String(authRejects)+",\"event_pending\":"+String(rainEvent.pending?"true":"false")+
    ",\"config_pending\":"+String(configPending||resetPending?"true":"false")+",\"rx_known\":"+String(rxKnown?"true":"false")+
    ",\"rx_active\":"+String(receiver.flags&ACK_ACTIVE?"true":"false")+
    ",\"rx_armed\":"+String(receiver.flags&ACK_RELAY_ARMED?"true":"false")+",\"rx_coil\":"+String(receiver.flags&ACK_COIL_ON?"true":"false")+
    ",\"rx_maintenance\":"+String(receiver.flags&ACK_MAINTENANCE?"true":"false")+",\"rx_restored\":"+String(receiver.config&32?"true":"false")+
    ",\"remaining_ms\":\""+u64(receiver.remainingMs)+"\",\"pulse_ms\":\""+u64(receiver.pulseMs)+"\""+
    ",\"polarity\":"+String(receiver.config&64?1:0)+",\"command_result\":\""+commandResult+"\""+
    ",\"policy\":"+String((receiver.config>>1)&3)+",\"loss\":"+String((receiver.config>>3)&3)+",\"loss_seconds\":"+u64(receiver.echo/1000)+
    ",\"on\":"+String(rainOnThreshold)+",\"off\":"+String(rainOffThreshold)+",\"sample\":"+String(testSampleMs)+",\"confirm_ms\":"+String(confirmMs)+
    ",\"heartbeat\":"+String(heartbeatSeconds)+",\"sleep\":"+String(sleepSeconds)+",\"dowake\":"+String(doWakeEnabled?1:0)+
    ",\"recording\":"+String(recording?"true":"false")+",\"record_elapsed_ms\":\""+u64(recording?uptimeMs()-recordStarted:recordElapsed)+"\""+
    ",\"record_start_utc\":\""+recordStartIso+"\",\"record_capacity_bytes\":"+String(RECORD_LIMIT)+",\"filesystem_ready\":"+String(fsReady?"true":"false")+
    ",\"record_rows\":"+String(recordRows)+",\"record_error\":\""+recordError+"\""+
    ",\"sf\":"+String(LORA_SF)+",\"frequency_mhz\":"+String(LORA_FREQUENCY_MHZ,1)+coreStatusFields()+"}";
  return s;
}
void processReceived() {
  if(!rxInterrupt)return;rxInterrupt=false;RainFrame f{};
  if(!readFrame(f)){++authRejects;radio.startReceive();recordEvent("invalid_frame");return;}
  if(f.sender!=RX_ID){radio.startReceive();return;}
  if(f.type==FRAME_CHALLENGE&&f.session&&f.echo==helloNonce){
    if(sessionId!=f.session){sessionId=f.session;pending=false;awaitingAck=false;queueState();recordEvent("session_ready");}
  }else if(f.type==FRAME_ACK&&sessionId&&f.session==sessionId&&f.ackSeq==pendingFrame.seq&&awaitingAck){
    lastRssi=radio.getRSSI();lastSnr=radio.getSNR();remoteRssi=f.peerRssi10/10.0f;remoteSnr=f.peerSnr10/10.0f;
    lastAck=uptimeMs();lastRtt=lastAck-txStarted;receiver=f;rxKnown=true;
    if(pendingFrame.type==FRAME_STATE){++ackedStates;
      if(rainEvent.acknowledge(f.event)){if(!saveEvent())rainEvent.pending=true;}
    }else if(f.flags&(ACK_CONFIG|ACK_REJECTED)){
      configPending=false;resetPending=false;commandResult=f.flags&ACK_CONFIG?"Confirmada":"Rechazada por el receptor";
      recordEvent(f.flags&ACK_CONFIG?"receiver_config_confirmed":"receiver_command_rejected");
    }
    pending=false;awaitingAck=false;helloBackoff=15000;recordEvent("ack");
    if(pendingFrame.type==FRAME_STATE&&(pendingFrame.wet!=wetNow()||pendingFrame.event!=(rainEvent.pending?rainEvent.id:0)||bool(pendingFrame.flags&STATE_SIMULATED)!=(simulation>=0)))queueState();
  }
  radio.startReceive();
}
void serviceRadio() {
  if(coreMaintenance)return;uint64_t now=uptimeMs();
  if(awaitingAck&&now-ackStart>=ACK_TIMEOUT_MS){awaitingAck=false;++ackTimeouts;nextSend=now+4000;recordEvent("ack_timeout");}
  if(awaitingAck||now<nextSend)return;
  uint64_t duty=coreDutyWaitMs();if(duty){nextSend=now+duty;return;}
  if(!sessionId){
    auto f=newFrame(FRAME_HELLO,TX_ID,senderBoot,0,helloNonce);
    int16_t rc=transmitFrame(f,onRadioReceive);recordEvent(rc==RADIOLIB_ERR_NONE?"hello":"hello_error");
    nextSend=now+helloBackoff;helloBackoff=constrain(helloBackoff*2,15000UL,300000UL);return;
  }
  if(!pending&&(configPending||resetPending)){
    pendingFrame=newFrame(FRAME_CONFIG,TX_ID,senderBoot,++txSequence,sessionId);
    pendingFrame.config=requestedConfig;pendingFrame.pulseMs=requestedPulseMs;pendingFrame.echo=(uint64_t)requestedLossSeconds*1000;
    pendingFrame.flags=resetPending?1:0;pending=true;attempts=0;
  }
  if(!pending&&now-lastTx>=(uint64_t)heartbeatSeconds*1000)queueState();
  if(!pending)return;
  if(attempts>=3){sessionId=0;helloNonce=random64();if(!helloNonce)helloNonce=1;pending=false;awaitingAck=false;nextSend=now+helloBackoff;recordEvent("session_retry");return;}
  pendingFrame.session=sessionId;txStarted=uptimeMs();int16_t rc=transmitFrame(pendingFrame,onRadioReceive);
  if(rc==-32000){nextSend=now+coreDutyWaitMs();return;}
  if(attempts++)++retries;if(pendingFrame.type==FRAME_STATE)++sentStates;
  lastTx=uptimeMs();recordEvent(rc==RADIOLIB_ERR_NONE?"tx":"tx_error");
  if(rc==RADIOLIB_ERR_NONE){awaitingAck=true;ackStart=uptimeMs();}else nextSend=now+4000;
}
void setup() {
  Serial.begin(115200);coreRole="TX";coreLoadDevice();loadSettings();coreInitDisplay();
  pinMode(PIN_RAIN_AO,INPUT);pinMode(PIN_RAIN_DO,INPUT_PULLUP);analogReadResolution(12);analogSetPinAttenuation(PIN_RAIN_AO,ADC_11db);
  auto cause=esp_sleep_get_wakeup_cause();resumed=savedSignature==RTC_SIGNATURE&&(cause==ESP_SLEEP_WAKEUP_TIMER||cause==ESP_SLEEP_WAKEUP_EXT1);
  senderBoot=resumed?savedBoot:esp_random();txSequence=resumed?savedSeq:0;sessionId=resumed?savedSession:0;
  helloNonce=random64();if(!helloNonce)helloNonce=1;
  detector.wet=resumed?savedWet:false;detector.candidate=detector.wet;
  if(!initRadio(onRadioReceive)){coreScreen("ERROR RADIO","Reiniciar para probar","","","","");while(true)delay(1000);}
  // Never format an existing filesystem automatically. The portal requires explicit confirmation.
  fsReady=!resumed&&LittleFS.begin(false);if(!fsReady&&!resumed)recordError="Memoria no preparada";
  if(fsReady&&LittleFS.exists("/experiment.csv")){
    File f=LittleFS.open("/experiment.csv","r");bool header=true;while(f.available()){String row=f.readStringUntil('\n');if(header){header=false;continue;}int comma=row.indexOf(','),end=row.indexOf(',',comma+1);uint64_t elapsed;
      if(comma>0&&end>comma&&validIso(row.substring(0,comma))&&rain::decimal(row.substring(comma+1,end).c_str(),elapsed)){recordStartIso=row.substring(0,comma);recordElapsed=elapsed;++recordRows;}}f.close();
  }
  updateSensor(true);readBattery();
  if(!resumed)coreStartPortal(false);if(sessionId)queueState();nextSend=uptimeMs();
}
void loop() {
  coreTickPortal();coreTickDisplay(false);processReceived();
  static bool previousPrototype=corePrototype;
  if(previousPrototype&&!corePrototype){bool old=wetNow();simulation=-1;if(!old&&wetNow())latchRainEvent();
    if(recording){recordEvent("record_stop_mode");recordFile.close();recording=false;}queueState();}
  previousPrototype=corePrototype;
  if(!coreMaintenance)updateSensor();serviceRadio();
  uint64_t now=uptimeMs();static uint64_t lastBattery=0;
  if(!coreMaintenance&&now-lastBattery>=30000){lastBattery=now;readBattery();}
  if(recording&&now-lastRecordSnapshot>=1000){lastRecordSnapshot=now;recordEvent("sample");}
  if(now-lastScreen>=1000){lastScreen=now;
    if(coreApActive&&now<30000)coreDisplayPortalIfNeeded();else coreScreen("RAIN LINK / TX",wetNow()?"MOJADO":"SECO",simulation>=0?"SIMULACION":"SENSOR",
      linkOk()?"ACK OK":"SIN CONFIRMACION","RSSI "+String(lastRssi,0),recording?"GRABANDO":"");}
  if(!corePrototype&&!coreApActive&&!coreMaintenance&&!recording&&!awaitingAck&&
     ((!pending&&sessionId)||(!sessionId&&now>20000)||(now>30000&&!configPending&&!resetPending))&&now>2500&&now>=confirmMs+500){
    savedSignature=RTC_SIGNATURE;savedBoot=senderBoot;savedSeq=txSequence;savedSession=sessionId;savedWet=detector.wet;
    radio.sleep();oled.setPowerSave(1);digitalWrite(VEXT_CTRL,HIGH);WiFi.mode(WIFI_OFF);
    esp_sleep_enable_timer_wakeup((uint64_t)sleepSeconds*1000000);
    if(doWakeEnabled&&!detector.wet&&digitalRead(PIN_RAIN_DO)==HIGH)esp_sleep_enable_ext1_wakeup_io(1ULL<<PIN_RAIN_DO,ESP_EXT1_WAKEUP_ANY_LOW);
    esp_deep_sleep_start();
  }
  delay(1);
}
