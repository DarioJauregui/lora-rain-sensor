#include <Arduino.h>
#include <RainCore.h>
#include <Controls.h>
#include <Management.h>
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
int doLevel=-1, batteryGate=0;
rain::SensorInput sensorInput;
uint32_t pendingSensorRevision=0;
uint32_t testSampleMs=50, confirmMs=0, heartbeatMs=30000, sleepMs=120000;
bool doWakeEnabled=false, resumed=false;
rain::PendingRain rainEvent;
uint64_t lastSample=0, lastTx=0, lastAck=0, nextSend=0, ackStart=0, lastScreen=0;
uint32_t helloBackoff=15000, attempts=0, sentStates=0, ackedStates=0, retries=0, ackTimeouts=0, authRejects=0;
float lastRssi=-150, lastSnr=-20, remoteRssi=-150, remoteSnr=-20;
uint64_t lastRtt=0, txStarted=0;
RainFrame pendingFrame{}, receiver{};
bool pending=false, awaitingAck=false, rxKnown=false,receiverSimulated=false;
bool configPending=false, resetPending=false;
bool forceStateAfterAck=false, remoteRelayActive=false;
uint64_t sensorStateSinceMs=0, remoteContactObservedSinceMs=0;
String manualFeedback="Sin orden manual";
uint64_t lastRadioProbeMs=0;
bool previousLinkOk=false;
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
bool wetNow() { return sensorInput.wet(detector.wet); }
bool linkOk() { return lastAck && uptimeMs()-lastAck < (uint64_t)heartbeatMs*3+10000; }
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
  coreLogEvent(event);
  if (!recording || !recordFile) return;
  uint64_t now=uptimeMs(); recordElapsed=now-recordStarted;
  String line=recordStartIso+","+u64(recordElapsed)+","+event+","+String(analogRain)+","+String(doLevel)+","+String(wetNow())+","+String(sensorInput.manual)+","+
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
  p.begin("rainmode",true);testSampleMs=p.getUInt("sample",50);sleepMs=p.getUInt("sleep",120)*1000;
  heartbeatMs=p.getUInt("heartbeat",30)*1000;confirmMs=p.getUInt("confirm",0);doWakeEnabled=p.getBool("dowake",false);p.end();
  testSampleMs=constrain(testSampleMs,10UL,60000UL);sleepMs=constrain(sleepMs,1000UL,86400000UL);
  heartbeatMs=constrain(heartbeatMs,1000UL,86400000UL);confirmMs=constrain(confirmMs,0UL,60000UL);
  p.begin("rainop",true);Operational saved{};bool has=p.getBytes("settings",&saved,sizeof(saved))==sizeof(saved);p.end();
  if(has){testSampleMs=saved.sampleMs;confirmMs=saved.confirmMs;heartbeatMs=saved.heartbeatMs;sleepMs=saved.sleepMs;rainOnThreshold=saved.on;rainOffThreshold=saved.off;doWakeEnabled=saved.doWake;}
  p.begin("rainevent",true);uint64_t state=p.getULong64("state",0);rainEvent.id=state>>1;rainEvent.pending=state&1;p.end();
}
void queueState() {
  // Do not mutate an in-flight packet. The current state is queued after its ACK.
  if(pending || awaitingAck)return;
  pendingFrame=newFrame(FRAME_STATE,TX_ID,senderBoot,++txSequence,sessionId);
  pendingSensorRevision=sensorInput.revision;pendingFrame.echo=pendingSensorRevision;
  pendingFrame.wet=wetNow();pendingFrame.ao=analogRain;pendingFrame.batteryMv=batteryMv;
  pendingFrame.event=rainEvent.pending?rainEvent.id:0;
  pendingFrame.ackSeq=((corePrototype?heartbeatMs:sleepMs)+999)/1000;
  if(!corePrototype)pendingFrame.flags|=4;
  if(sensorInput.manual)pendingFrame.flags|=STATE_SIMULATED;
  pending=true;attempts=0;nextSend=uptimeMs();
}
void latchRainEvent() {
  // A single outstanding latch is sufficient for an activation; later episodes coalesce until delivery.
  if(!rainEvent.pending){uint32_t next;do{next=esp_random();}while(!next||next==rainEvent.id);rainEvent.raise(next);saveEvent();}
  recordEvent("rain_event");
}
void updateSensor(bool immediate=false,bool report=true) {
  uint64_t now=uptimeMs();if(!immediate && now-lastSample<testSampleMs)return;lastSample=now;
  bool oldWet=wetNow();analogRain=readRain();doLevel=digitalRead(PIN_RAIN_DO);
  if(sensorInput.manual)return; // La lectura física no gobierna el estado en modo manual.
  detector.sample(analogRain,rainOnThreshold,rainOffThreshold,now,corePrototype?0:confirmMs);
  if(report&&oldWet!=wetNow()) {sensorStateSinceMs=now;if(wetNow())latchRainEvent();recordEvent("sensor_change");queueState();}
}
// Shared by the local web portal and encrypted RX -> TX LoRa commands.
bool applyManualSelection(uint8_t command) {
  const bool previous=wetNow();
  if(!sensorInput.select(command,corePrototype,detector.wet))return false;
  if(command==rain::PHYSICAL) {
    // Start a fresh physical decision; a manual episode must not retain old hysteresis.
    detector={};updateSensor(true,false);
  }
  manualFeedback="Esperando ACK del receptor";
  if(previous!=wetNow())sensorStateSinceMs=uptimeMs();
  if(!previous&&wetNow())latchRainEvent();
  if(pending||awaitingAck)forceStateAfterAck=true;else queueState();
  const char *event=command==rain::MANUAL?"sensor_manual":command==rain::PHYSICAL?"sensor_physical":
    command==rain::MANUAL_WET?"manual_wet":"manual_dry";
  recordEvent(event);
  return true;
}
String roleControlsHtml() {
  String s=F("<section><h2>Experimento</h2><div class='actions'>"
    "<button class='primary' id='startRecord' data-command='/record/start'>Grabar</button>"
    "<button id='stopRecord' data-command='/record/stop'>Detener</button>"
    "<a class='button' href='/record.csv' download>Descargar CSV</a></div><p id='recordState' aria-live='polite'>Sin grabación</p>"
    "<small>Tiempo desde el inicio. Registro automático en el emisor, aunque se desconecte el móvil. "
    "Un archivo CSV, hasta 1 MiB.</small></section>"
    "<section><h2>Prueba de lluvia</h2><div class='actions'>"
    "<button class='primary' data-command='/simulate' data-value='off' id='manualEnable'>Desactivar sensor / modo manual</button>"
    "<button data-command='/simulate' data-value='wet' id='manualWet'>Enviar MOJADO</button>"
    "<button data-command='/simulate' data-value='dry' id='manualDry'>Enviar SECO</button>"
    "<button data-command='/simulate' data-value='sensor' id='manualDisable'>Usar sensor físico</button></div>"
    "<p id='manualModeState' role='status'>Leyendo modo...</p>"
    "<small>Solo en prototipo. Primero desactiva el sensor: las órdenes MOJADO/SECO se enviarán por LoRa y se confirmarán mediante ACK. "
    "El origen seleccionado se indica en la telemetría.</small></section>");
  s+=F("<dialog id='replaceRecord'><h2>Grabación anterior</h2><p>Descarga el archivo anterior antes de sustituirlo.</p>"
    "<div class='actions'><a class='button' href='/record.csv' download>Descargar anterior</a>"
    "<button id='confirmRecord'>Sustituir y grabar</button><button id='cancelRecord'>Cancelar</button></div></dialog>"
    "<details id='memorySettings'><summary>Memoria de grabación</summary><form action='/record/prepare' data-save>"
    "<p class='hint'>Solo si aparece memoria no preparada. Formatear elimina cualquier grabación de esta partición. "
    "No cambia la configuración ni la clave.</p><label>Escribe BORRAR MEMORIA<input name='confirm' required></label><button>Preparar memoria</button></form></details>");
  s+=pulseControls();
  s+=F("<details id='sensorSettings'><summary>Sensor y envío</summary><form action='/settings' data-save><div class='grid'>"
    "<label>Mojado si AO ≤<input name='on' type='number' min='0' max='3900' value='3500' required></label>"
    "<label>Seco si AO ≥<input name='off' type='number' min='1' max='4095' value='3900' required></label>"
    "<label>Lectura despierto (ms)<input name='sample' type='number' min='10' max='60000' value='50' required></label>"
    "<label>Confirmación producción (ms)<input name='confirm_ms' type='number' min='0' max='60000' value='0' required></label>"
    "<label>Telemetría prototipo (ms)<input name='heartbeat' type='number' min='1' max='86400000' value='30000' required></label>"
    "<label>Sueño producción (ms)<input name='sleep' type='number' min='1' max='86400000' value='120000' required></label>"
    "<label>Despertar DO=LOW<select name='dowake'><option value='0'>Desactivado</option><option value='1'>Activado tras validar</option></select></label></div>"
    "<p id='sensorLive' role='status'></p><details class='help'><summary>i</summary><p>AO es un ADC de 0 a 4095; DO es el comparador digital. La histéresis usa umbrales distintos para evitar cambios continuos. Ajusta observando seco y mojado. Confirmar exige mantener la condición durante el tiempo configurado; el sueño retrasa la detección.</p></details>"
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
       on>3900||off>4095||off<=on||sample<10||sample>60000||confirmation>60000||heartbeat<1000||heartbeat>86400000||sleep<1000||sleep>86400000||dowake>1){web.send(400,"text/plain","Configuracion invalida");return;}
    Operational op=getOperational();op.on=on;op.off=off;op.sampleMs=sample;op.confirmMs=confirmation;op.heartbeatMs=heartbeat;op.sleepMs=sleep;op.doWake=dowake;
    if(!applyOperational(op)){web.send(500,"text/plain","Error al guardar");return;}
    recordEvent("sensor_config");web.send(200,"text/plain","Configuracion guardada");
  });
  web.on("/simulate",HTTP_POST,[](){
    if(!corePostAllowed())return;if(!corePrototype){web.send(409,"text/plain","Solo en prototipo");return;}
    String value=web.arg("value");int command=value=="off"?0:value=="wet"?1:value=="dry"?2:value=="sensor"?3:-1;
    if(command<0){web.send(400,"text/plain","Orden invalida");return;}
    if(!applyManualSelection(command)){web.send(409,"text/plain","Activa el modo Prototipo y desactiva el sensor antes de enviar MOJADO/SECO");return;}
    web.send(202,"text/plain",sensorInput.manual?"Orden manual pendiente de ACK LoRa":"Sensor fisico habilitado");
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
    ",\"simulated\":"+String(sensorInput.manual?"true":"false")+",\"sensor_mode\":\""+String(sensorInput.manual?"manual":"physical")+"\""+
    ",\"state_age_ms\":\""+u64(sensorStateSinceMs?uptimeMs()-sensorStateSinceMs:0)+"\""+
    ",\"manual_pending\":"+String(sensorInput.pending?"true":"false")+",\"manual_feedback\":\""+manualFeedback+"\""+
    ",\"rx_state_observed_ms\":"+(rxKnown?"\""+u64(uptimeMs()-remoteContactObservedSinceMs)+"\"":String("null"))+
    ",\"ao\":"+String(analogRain)+",\"do\":"+String(doLevel)+
    ",\"battery_mv\":"+String(batteryMv)+",\"link_ok\":"+String(linkOk()?"true":"false")+
    ",\"ack_age_ms\":"+(lastAck?u64(uptimeMs()-lastAck):String("null"))+",\"rssi_dbm\":"+String(lastRssi,1)+",\"snr_db\":"+String(lastSnr,1)+
    ",\"rx_rssi_dbm\":"+String(remoteRssi,1)+",\"rx_snr_db\":"+String(remoteSnr,1)+",\"rtt_ms\":\""+u64(lastRtt)+"\""+
    ",\"acked_states\":"+String(ackedStates)+",\"sent_states\":"+String(sentStates)+",\"retries\":"+String(retries)+",\"ack_timeouts\":"+String(ackTimeouts)+
    ",\"auth_rejects\":"+String(authRejects)+",\"event_pending\":"+String(rainEvent.pending?"true":"false")+
    ",\"config_pending\":"+String(configPending||resetPending?"true":"false")+",\"rx_known\":"+String(rxKnown?"true":"false")+
    ",\"rx_simulated\":"+String(receiverSimulated?"true":"false")+",\"rx_wet\":"+String(receiver.wet?"true":"false")+
    ",\"rx_active\":"+String(receiver.flags&ACK_ACTIVE?"true":"false")+
    ",\"rx_armed\":"+String(receiver.flags&ACK_RELAY_ARMED?"true":"false")+",\"rx_coil\":"+String(receiver.flags&ACK_COIL_ON?"true":"false")+
    ",\"rx_maintenance\":"+String(receiver.flags&ACK_MAINTENANCE?"true":"false")+",\"rx_restored\":"+String(receiver.config&32?"true":"false")+
    ",\"remaining_ms\":\""+u64(receiver.remainingMs)+"\",\"pulse_ms\":\""+u64(receiver.pulseMs)+"\""+
    ",\"contact_nc\":"+String(receiver.config&128?"true":"false")+",\"polarity\":"+String(receiver.config&64?1:0)+",\"command_result\":\""+commandResult+"\""+
    ",\"policy\":"+String((receiver.config>>1)&3)+",\"loss\":"+String((receiver.config>>3)&3)+",\"loss_seconds\":"+u64(receiver.echo/1000)+
    ",\"on\":"+String(rainOnThreshold)+",\"off\":"+String(rainOffThreshold)+",\"sample\":"+String(testSampleMs)+",\"confirm_ms\":"+String(confirmMs)+
    ",\"heartbeat\":"+String(heartbeatMs)+",\"sleep\":"+String(sleepMs)+",\"dowake\":"+String(doWakeEnabled?1:0)+
    ",\"recording\":"+String(recording?"true":"false")+",\"record_elapsed_ms\":\""+u64(recording?uptimeMs()-recordStarted:recordElapsed)+"\""+
    ",\"record_start_utc\":\""+recordStartIso+"\",\"record_capacity_bytes\":"+String(RECORD_LIMIT)+",\"filesystem_ready\":"+String(fsReady?"true":"false")+
    ",\"record_rows\":"+String(recordRows)+",\"record_error\":\""+recordError+"\""+
    ",\"sf\":"+String(LORA_SF)+",\"frequency_mhz\":"+String(LORA_FREQUENCY_MHZ,1)+coreStatusFields()+"}";
  return s;
}
void processReceived() {
  if(!rxInterrupt)return;rxInterrupt=false;RainFrame f{};
  if(!readFrame(f)){++authRejects;radio.startReceive();recordEvent("invalid_frame");return;}
  if(managementProcess(f)){radio.startReceive();return;}
  if(f.sender!=RX_ID){radio.startReceive();return;}
  if(f.type==FRAME_CHALLENGE&&f.session&&f.echo==helloNonce){
    if(sessionId!=f.session){sessionId=f.session;pending=false;awaitingAck=false;queueState();recordEvent("session_ready");}
  }else if(f.type==FRAME_ACK&&sessionId&&f.session==sessionId&&f.ackSeq==pendingFrame.seq&&awaitingAck){
    lastRssi=radio.getRSSI();lastSnr=radio.getSNR();remoteRssi=f.peerRssi10/10.0f;remoteSnr=f.peerSnr10/10.0f;
    const bool firstReceiverState=!rxKnown;
    lastAck=uptimeMs();lastRtt=lastAck-txStarted;receiver=f;rxKnown=true;
    if(firstReceiverState||remoteRelayActive!=bool(f.flags&ACK_ACTIVE)){
      remoteRelayActive=f.flags&ACK_ACTIVE;
      remoteContactObservedSinceMs=lastAck;
    }
    if(pendingFrame.type==FRAME_STATE){if(!(f.flags&ACK_REJECTED)){++ackedStates;receiverSimulated=pendingFrame.flags&STATE_SIMULATED;}
      if(!(f.flags&ACK_REJECTED)&&rainEvent.acknowledge(f.event)){if(!saveEvent())rainEvent.pending=true;}
      if(pendingFrame.wet==wetNow()&&bool(pendingFrame.flags&STATE_SIMULATED)==sensorInput.manual&&sensorInput.acknowledge(pendingSensorRevision,f.flags&ACK_REJECTED)){
        manualFeedback=String("ACK confirmado: ")+(wetNow()?"MOJADO":"SECO")+(sensorInput.manual?" manual":" fisico");
        recordEvent("sensor_command_ack");
      }else if(f.flags&ACK_REJECTED){manualFeedback="Rechazada por el receptor";recordEvent("sensor_command_rejected");}

    }else if(f.flags&(ACK_CONFIG|ACK_REJECTED)){
      configPending=false;resetPending=false;commandResult=f.flags&ACK_CONFIG?"Confirmada":"Rechazada por el receptor";
      recordEvent(f.flags&ACK_CONFIG?"receiver_config_confirmed":"receiver_command_rejected");
    }
    pending=false;awaitingAck=false;helloBackoff=15000;recordEvent("ack");
    bool refreshState=forceStateAfterAck || (pendingFrame.type==FRAME_STATE &&
      (pendingFrame.wet!=wetNow()||pendingFrame.event!=(rainEvent.pending?rainEvent.id:0)||bool(pendingFrame.flags&STATE_SIMULATED)!=(sensorInput.manual)));
    forceStateAfterAck=false;
    if(refreshState)queueState();
  }
  radio.startReceive();
}
void serviceRadio() {
  if(coreMaintenance||(mgWaiting&&mgAttempts))return;uint64_t now=uptimeMs();
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
  if(!pending&&now-lastTx>=(uint64_t)heartbeatMs)queueState();
  if(!pending)return;
  if(attempts>=3){sessionId=0;helloNonce=random64();if(!helloNonce)helloNonce=1;pending=false;awaitingAck=false;nextSend=now+helloBackoff;recordEvent("session_retry");return;}
  pendingFrame.session=sessionId;txStarted=uptimeMs();int16_t rc=transmitFrame(pendingFrame,onRadioReceive);
  if(rc==-32000){nextSend=now+coreDutyWaitMs();return;}
  if(attempts++)++retries;if(pendingFrame.type==FRAME_STATE)++sentStates;
  lastTx=uptimeMs();recordEvent(rc==RADIOLIB_ERR_NONE?"tx":"tx_error");
  if(rc==RADIOLIB_ERR_NONE){awaitingAck=true;ackStart=uptimeMs();}else nextSend=now+4000;
}
void setup() {
  Serial.begin(115200);coreRole="TX";coreLoadDevice();loadSettings();coreInitDisplay();managementOnRx=onRadioReceive;managementApplyTest=applyManualSelection;
  pinMode(PIN_RAIN_AO,INPUT);pinMode(PIN_RAIN_DO,INPUT_PULLUP);analogReadResolution(12);analogSetPinAttenuation(PIN_RAIN_AO,ADC_11db);
  auto cause=esp_sleep_get_wakeup_cause();resumed=savedSignature==RTC_SIGNATURE&&(cause==ESP_SLEEP_WAKEUP_TIMER||cause==ESP_SLEEP_WAKEUP_EXT1);
  senderBoot=resumed?savedBoot:esp_random();txSequence=resumed?savedSeq:0;sessionId=resumed?savedSession:0;
  managementOwnBoot=senderBoot;helloNonce=random64();if(!helloNonce)helloNonce=1;
  detector.wet=resumed?savedWet:false;detector.candidate=detector.wet;
  if(!initRadio(onRadioReceive)){coreScreen("ERROR RADIO","Reiniciar para probar","","","","");while(true)delay(1000);}
  // Never format an existing filesystem automatically. The portal requires explicit confirmation.
  fsReady=!resumed&&LittleFS.begin(false);if(!fsReady&&!resumed)recordError="Memoria no preparada";
  if(!resumed)coreInitLog(fsReady);
  if(fsReady&&LittleFS.exists("/experiment.csv")){
    File f=LittleFS.open("/experiment.csv","r");bool header=true;while(f.available()){String row=f.readStringUntil('\n');if(header){header=false;continue;}int comma=row.indexOf(','),end=row.indexOf(',',comma+1);uint64_t elapsed;
      if(comma>0&&end>comma&&validIso(row.substring(0,comma))&&rain::decimal(row.substring(comma+1,end).c_str(),elapsed)){recordStartIso=row.substring(0,comma);recordElapsed=elapsed;++recordRows;}}f.close();
  }
  updateSensor(true);sensorStateSinceMs=uptimeMs();readBattery();
  if(!resumed)coreStartPortal(false);if(sessionId)queueState();nextSend=uptimeMs();
}
void loop() {
  coreTickPortal();coreTickDisplay(false);processReceived();managementTick();
  static bool previousPrototype=corePrototype;
  if(previousPrototype&&!corePrototype){bool old=wetNow();sensorInput.manual=false;sensorInput.pending=true;++sensorInput.revision;manualFeedback="Sensor fisico: esperando ACK";detector={};
    updateSensor(true,false);if(old!=wetNow())sensorStateSinceMs=uptimeMs();if(!old&&wetNow())latchRainEvent();
    recordEvent("sensor_physical_production");if(recording){recordEvent("record_stop_mode");recordFile.close();recording=false;}
    if(pending||awaitingAck)forceStateAfterAck=true;else queueState();}
  previousPrototype=corePrototype;
  if(!coreMaintenance)updateSensor();serviceRadio();
  uint64_t now=uptimeMs();static uint64_t lastBattery=0;
  const bool linkCurrent=linkOk();
  if(linkCurrent!=previousLinkOk){previousLinkOk=linkCurrent;
    if(lastAck)recordEvent(linkCurrent?"link_restored":"link_lost");
  }
  if(corePrototype&&corePaired&&!coreMaintenance&&!coreOtaInProgress&&
     !awaitingAck&&!mgWaiting&&!coreDutyWaitMs()&&
     ((!lastAck&&now>180000)||(lastAck&&now-lastAck>180000))&&
     now-lastRadioProbeMs>180000){
    lastRadioProbeMs=now;
    if(coreRecoverRadio(onRadioReceive,"TX without ACK")){
      sessionId=0;pending=false;awaitingAck=false;helloNonce=random64();if(!helloNonce)helloNonce=1;
      nextSend=now;helloBackoff=15000;recordEvent("session_recovery");
    }
  }
  if(!coreMaintenance&&now-lastBattery>=30000){lastBattery=now;readBattery();}
  if(recording&&now-lastRecordSnapshot>=1000){lastRecordSnapshot=now;recordEvent("sample");}
  if(now-lastScreen>=1000){lastScreen=now;coreQuality=rain::quality(lastRssi,lastSnr,linkOk(),ackedStates,sentStates);
    if(coreApActive&&now<30000)coreDisplayPortalIfNeeded();else coreScreen("LoraRainSensor TX",String(wetNow()?"MOJADO":"SECO")+"|AO "+String(analogRain),sensorInput.manual?"MANUAL":"SENSOR FISICO",
      (sensorInput.pending?"ACK pendiente":linkOk()?"ACK OK":"SIN ACK")+(lastAck?" "+compactTime(now-lastAck):String("")),
      rxKnown?String("Rel ")+(remoteRelayActive?"ON ":"OFF ")+stateAge(now-remoteContactObservedSinceMs):String("Rel --"),"");}
  if((!lastAck||now-lastAck>2000)&&!coreRebootAtMs&&!mgWaiting&&!corePrototype&&!coreApActive&&!coreMaintenance&&!recording&&!awaitingAck&&
     ((!pending&&sessionId)||(!sessionId&&now>20000)||(now>30000&&!configPending&&!resetPending))&&now>2500&&now>=confirmMs+500){
    savedSignature=RTC_SIGNATURE;savedBoot=senderBoot;savedSeq=txSequence;savedSession=sessionId;savedWet=detector.wet;
    radio.sleep();oled.setPowerSave(1);digitalWrite(VEXT_CTRL,HIGH);WiFi.mode(WIFI_OFF);
    esp_sleep_enable_timer_wakeup((uint64_t)sleepMs*1000);
    if(doWakeEnabled&&!detector.wet&&digitalRead(PIN_RAIN_DO)==HIGH)esp_sleep_enable_ext1_wakeup_io(1ULL<<PIN_RAIN_DO,ESP_EXT1_WAKEUP_ANY_LOW);
    esp_deep_sleep_start();
  }
  delay(1);
}

uint32_t managementSensorRevision(){return sensorInput.revision;}
uint64_t managementSession(){return sessionId;}
void managementRxAction(){queueState();}
void requestLink(){queueState();}
Operational getOperational(){Operational op{};op.sampleMs=testSampleMs;op.confirmMs=confirmMs;op.heartbeatMs=heartbeatMs;op.sleepMs=sleepMs;op.on=rainOnThreshold;op.off=rainOffThreshold;op.prototype=corePrototype;op.display=coreDisplayEnabled;op.doWake=doWakeEnabled;op.role=1;op.uptime=uptimeMs();op.freeHeap=ESP.getFreeHeap();op.ao=analogRain;op.battery=batteryMv;op.doLevel=doLevel;op.wet=wetNow();op.simulated=sensorInput.manual;op.reserved=(sensorInput.pending?1:0)|(!sensorInput.pending&&ackedStates?2:0);op.rxPackets=ackedStates;op.txPackets=coreTxCount;op.errors=authRejects+coreTxErrors;return op;}
bool applyOperational(const Operational &op){
  if(op.role!=1||op.sampleMs<10||op.sampleMs>60000||op.confirmMs>60000||op.heartbeatMs<1000||op.heartbeatMs>86400000||op.sleepMs<1000||op.sleepMs>86400000||op.on>3900||op.off>4095||op.off<=op.on||op.prototype>1||op.doWake>1)return false;
  // Whole operational update is one NVS entry; loaded atomically after restart.
  Preferences p;if(!p.begin("rainop",false))return false;bool ok=p.putBytes("settings",&op,sizeof(op))==sizeof(op);p.end();
  if(ok){testSampleMs=op.sampleMs;confirmMs=op.confirmMs;heartbeatMs=op.heartbeatMs;sleepMs=op.sleepMs;rainOnThreshold=op.on;rainOffThreshold=op.off;corePrototype=op.prototype;doWakeEnabled=op.doWake;coreSaveDevice();}
  return ok;
}

bool managementCanSend(){return !awaitingAck&&(mgAttempts>0||!pending);}
