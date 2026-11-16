#pragma once
#include "RainCore.h"
#include "RainCrypto.h"
// Management packets fit in one SX1262 packet: 216 bytes including HMAC.
// 100 encrypted bytes accommodate the maximum SSID and WPA2 password together.
struct __attribute__((packed)) Operational {
  uint32_t sampleMs, confirmMs, heartbeatMs, sleepMs;
  uint16_t on, off;
  uint8_t prototype, display, doWake, role;
  uint64_t uptime;
  uint32_t freeHeap,rxPackets,txPackets,errors;
  uint16_t ao,battery;
  int8_t doLevel;
  uint8_t wet,simulated,reserved;
};
struct __attribute__((packed)) ManagementData { uint8_t kind, result; uint8_t bytes[98]; };
static_assert(sizeof(Operational)+33<=98,"Snapshot and SSID fit in one encrypted reply");
static_assert(sizeof(ManagementData)==100,"Encrypted payload size");
bool applyOperational(const Operational &);
Operational getOperational();
uint64_t managementSession();
void managementRxAction();
bool managementCanSend();
void (*managementOnRx)()=nullptr;
uint32_t managementOwnBoot=0;
uint64_t mgCounter=0,mgLimit=0,mgSession=0,mgNext=0;
uint32_t mgSequence=0,mgHighest=0;
RainFrame mgPending{},mgCached{};
bool mgWaiting=false,mgHaveCache=false;
uint8_t mgAttempts=0;
String mgState="Sin operaciones",mgPeerSsid;
Operational mgPeer{};
bool mgPeerKnown=false,mgPeerAsleep=false;
uint64_t mgLastReply=0;
uint32_t localId(){return coreRole=="TX"?TX_ID:RX_ID;}
bool validAccess(const char *ssid,const char *pass){
  size_t a=strnlen(ssid,33),b=strnlen(pass,64);
  if(a<1||a>32||b<16||b>63)return false;
  for(size_t i=0;i<a;++i)if((uint8_t)ssid[i]<32||(uint8_t)ssid[i]>126)return false;
  bool letter=false,digit=false;
  for(size_t i=0;i<b;++i){uint8_t c=pass[i];if(c<33||c>126)return false;letter|=(c>='a'&&c<='z')||(c>='A'&&c<='Z');digit|=c>='0'&&c<='9';}
  return letter&&digit;
}
bool saveAccess(const char *ssid,const char *pass){
  if(!validAccess(ssid,pass))return false;
  struct Access {char ssid[33];char pass[64];} a{};
  strlcpy(a.ssid,ssid,33);strlcpy(a.pass,pass,64);
  Preferences p;if(!p.begin("rainaccess",false))return false;
  bool ok=p.putBytes("access",&a,sizeof(a))==sizeof(a);p.end();
  if(ok)coreRebootAtMs=millis()+20000; // ACK precedes AP restart; duplicate requests reuse cached reply.
  memset(&a,0,sizeof(a));return ok;
}
bool mgNonce(uint8_t *nonce){
  if(mgCounter==mgLimit){
    Preferences p;if(!p.begin("rainnonce",false))return false;
    uint64_t base=p.getULong64("next",0);
    bool ok=base<=UINT64_MAX-1024&&p.putULong64("next",base+1024)==8;p.end();
    if(!ok)return false;mgCounter=base;mgLimit=base+1024;
  }
  uint32_t id=localId();memcpy(nonce,&id,4);uint64_t n=mgCounter++;memcpy(nonce+4,&n,8);return true;
}
bool mgSeal(RainFrame &f,const ManagementData &data){
  uint8_t key[32];if(!corePaired||!raincrypto::derive(PAIR_KEY,key)||!mgNonce(f.payload))return false;
  bool ok=raincrypto::crypt(true,key,f.payload,reinterpret_cast<const uint8_t*>(&f),offsetof(RainFrame,payload),reinterpret_cast<const uint8_t*>(&data),f.payload+12,f.payload+112);
  memset(key,0,32);return ok;
}
bool mgOpen(const RainFrame &f,ManagementData &data){
  uint8_t key[32];if(!corePaired||!raincrypto::derive(PAIR_KEY,key))return false;
  uint8_t tag[16];memcpy(tag,f.payload+112,16);
  bool ok=raincrypto::crypt(false,key,f.payload,reinterpret_cast<const uint8_t*>(&f),offsetof(RainFrame,payload),f.payload+12,reinterpret_cast<uint8_t*>(&data),tag);
  memset(key,0,32);return ok;
}
bool managementQueue(const ManagementData &data){
  if(mgWaiting)return false;
  mgPending=newFrame(7,localId(),managementOwnBoot,++mgSequence,0);
  if(!mgSequence){mgState="Contador agotado; reiniciar";return false;}mgPending.event=mgSequence;
  memcpy(mgPending.payload,&data,sizeof(data)); // RAM only; encrypted once a session is available.
  mgWaiting=true;mgAttempts=0;mgNext=0;mgState="Pendiente de envio";return true;
}
bool managementProcess(const RainFrame &f){
  if(f.type!=7&&f.type!=8)return false;
  uint64_t session=managementSession();
  if(!session||f.session!=session||f.sender!=(localId()==TX_ID?RX_ID:TX_ID))return true;
  if(mgSession!=session){mgSession=session;mgHighest=0;mgHaveCache=false;}
  if(f.type==7&&mgHaveCache&&f.seq==mgHighest&&f.event==mgCached.event){delay(65);transmitFrame(mgCached,managementOnRx);return true;}
  if(f.type==7&&f.seq<=mgHighest)return true;
  ManagementData d{};if(!mgOpen(f,d))return true;
  if(f.type==8){
    if(!mgWaiting||f.event!=mgPending.event)return true;
    mgState=d.result?"Aplicada":"Rechazada";mgWaiting=false;mgLastReply=uptimeMs();
    if(d.kind==0&&d.result){memcpy(&mgPeer,d.bytes,sizeof(mgPeer));mgPeerKnown=true;char ssid[33]{};memcpy(ssid,d.bytes+sizeof(mgPeer),32);mgPeerSsid=ssid;}
    if(d.kind==2&&d.result){char name[33]{};memcpy(name,d.bytes,32);mgPeerSsid=name;}
    memset(&mgPending,0,sizeof(mgPending));return true;
  }
  bool ok=false;
  if(!coreMaintenance){
    if(d.kind==0){auto op=getOperational();memset(d.bytes,0,sizeof(d.bytes));memcpy(d.bytes,&op,sizeof(op));memcpy(d.bytes+sizeof(op),coreApSsid.c_str(),coreApSsid.length());ok=true;}
    if(d.kind==1){Operational op;memcpy(&op,d.bytes,sizeof(op));ok=applyOperational(op);}
    if(d.kind==2){char ssid[33]{},pass[64]{};memcpy(ssid,d.bytes,33);memcpy(pass,d.bytes+33,64);
      ok=saveAccess(ssid,pass);memset(pass,0,sizeof(pass));memset(d.bytes,0,sizeof(d.bytes));memcpy(d.bytes,ssid,32);}
    if(d.kind==3){managementRxAction();ok=true;memset(d.bytes,0,sizeof(d.bytes));}
  }
  if(!ok)memset(d.bytes,0,sizeof(d.bytes));
  if(ok&&d.kind)recordEvent("remote_change_applied");
  d.result=ok;mgCached=newFrame(8,localId(),managementOwnBoot,++mgSequence,session);mgCached.event=f.event;
  if(mgSeal(mgCached,d)){mgHighest=f.seq;mgHaveCache=true;delay(65);transmitFrame(mgCached,managementOnRx);}
  memset(&d,0,sizeof(d));return true;
}
void managementTick(){
  if(!mgWaiting||coreMaintenance)return;
  uint64_t session=managementSession(),now=uptimeMs();
  if(!session){mgState="Esperando dispositivo";return;}
  if(now<mgNext||coreDutyWaitMs()||!managementCanSend())return;
  if(coreRole=="RX"&&mgPeerAsleep&&(!mgLastReply||now-mgLastReply>2000)){mgState="Esperando dispositivo";return;}
  if(mgAttempts>=3){mgWaiting=false;mgState="Sin confirmacion";memset(&mgPending,0,sizeof(mgPending));return;}
  if(!mgAttempts){ManagementData d;memcpy(&d,mgPending.payload,sizeof(d));mgPending.session=session;
    if(!mgSeal(mgPending,d)){mgWaiting=false;mgState="Error criptografico";}memset(&d,0,sizeof(d));if(!mgWaiting)return;}
  if(mgPending.session!=session){mgWaiting=false;mgState="Sin confirmacion: sesion cambiada";return;}
  int rc=transmitFrame(mgPending,managementOnRx);
  if(rc==-32000){mgState="Pendiente: presupuesto de radio";mgNext=now+coreAirtime.wait(rtcTimeMs(),(radio.getTimeOnAir(216)+999)/1000+5);return;}++mgAttempts;mgNext=now+5000;mgState="Pendiente de confirmacion";
}
String jsonEscape(String v){v.replace("\\","\\\\");v.replace("\"","\\\"");return v;}
String managementStatus(){
  return ",\"paired\":"+String(corePaired?"true":"false")+",\"ssid\":\""+jsonEscape(coreApSsid)+"\",\"management_state\":\""+mgState+"\",\"remote_ssid\":\""+jsonEscape(mgPeerSsid)+"\",\"remote_known\":"+String(mgPeerKnown?"true":"false")+
    ",\"remote_sample\":"+String(mgPeer.sampleMs)+",\"remote_confirm_ms\":"+String(mgPeer.confirmMs)+",\"remote_heartbeat_ms\":"+String(mgPeer.heartbeatMs)+",\"remote_sleep_ms\":"+String(mgPeer.sleepMs)+
    ",\"remote_on\":"+String(mgPeer.on)+",\"remote_off\":"+String(mgPeer.off)+",\"remote_prototype\":"+String(mgPeer.prototype?1:0)+",\"remote_dowake\":"+String(mgPeer.doWake?1:0)+
    ",\"remote_uptime_ms\":\""+u64(mgPeer.uptime)+"\",\"remote_free_heap\":"+String(mgPeer.freeHeap)+",\"remote_ao\":"+String(mgPeer.ao)+",\"remote_do\":"+String(mgPeer.doLevel)+",\"remote_battery_mv\":"+String(mgPeer.battery)+",\"remote_rx_packets\":"+String(mgPeer.rxPackets)+",\"remote_tx_packets\":"+String(mgPeer.txPackets)+",\"remote_errors\":"+String(mgPeer.errors)+
    ",\"free_heap\":"+String(ESP.getFreeHeap());
}
void managementRoutes(){
  web.on("/link-refresh",HTTP_POST,[](){if(!corePostAllowed())return;requestLink();web.send(202,"text/plain","Medicion solicitada; espera una respuesta LoRa nueva");});
  web.on("/pair",HTTP_POST,[](){if(!corePostAllowed())return;String key=web.arg("key");uint8_t bytes[32];
    if(key.length()!=64){web.send(400,"text/plain","Se requieren 64 digitos hexadecimales");return;}
    for(int i=0;i<32;++i){char part[3]={key[2*i],key[2*i+1],0};char *end;bytes[i]=strtoul(part,&end,16);if(*end||!isxdigit(part[0])||!isxdigit(part[1])){web.send(400,"text/plain","Clave invalida");return;}}
    Preferences p;p.begin("raindevice",false);bool ok=p.putBytes("key",bytes,32)==32;p.end();memset(bytes,0,32);
    web.send(ok?200:500,"text/plain",ok?"Clave guardada. Reiniciando":"Error de memoria");if(ok)coreRebootAtMs=millis()+1000;
  });
  web.on("/access",HTTP_POST,[](){if(!corePostAllowed())return;String ssid=web.arg("ssid"),pass=web.arg("password");
    if(!validAccess(ssid.c_str(),pass.c_str())){web.send(400,"text/plain","SSID: 1–32 ASCII; clave: 16–63 ASCII sin espacios, letras y numeros");return;}
    bool remote=web.arg("target")=="remote",ok;
    if(remote){ManagementData d{};d.kind=2;memcpy(d.bytes,ssid.c_str(),ssid.length());memcpy(d.bytes+33,pass.c_str(),pass.length());ok=managementQueue(d);memset(&d,0,sizeof(d));}
    else ok=saveAccess(ssid.c_str(),pass.c_str());
    pass="";web.send(ok?202:409,"text/plain",ok?(remote?"Cambio cifrado pendiente de aplicacion remota":"Guardado. Reconecta al nuevo SSID tras el reinicio"):"No se pudo guardar o hay otra operacion pendiente");
  });
  web.on("/remote-read",HTTP_POST,[](){if(!corePostAllowed())return;ManagementData d{};d.kind=0;bool ok=managementQueue(d);web.send(ok?202:409,"text/plain",ok?"Consulta pendiente":"Operacion pendiente");});
  web.on("/remote-settings",HTTP_POST,[](){if(!corePostAllowed())return;
    Operational op{};uint64_t a,b,c,d,on,off,mode,wake;
    if(!readUnsigned("remote_sample",a)||!readUnsigned("remote_confirm_ms",b)||!readUnsigned("remote_heartbeat_ms",c)||!readUnsigned("remote_sleep_ms",d)||!readUnsigned("remote_on",on)||!readUnsigned("remote_off",off)||!readUnsigned("remote_prototype",mode)||!readUnsigned("remote_dowake",wake)||
       a>60000||a<10||b>60000||c<1000||c>86400000||d<1000||d>86400000||on>3900||off>4095||off<=on||mode>1||wake>1){web.send(400,"text/plain","Parametros invalidos");return;}
    op.sampleMs=a;op.confirmMs=b;op.heartbeatMs=c;op.sleepMs=d;op.on=on;op.off=off;op.prototype=mode;op.doWake=wake;op.display=1;op.role=1;
    ManagementData data{};data.kind=1;memcpy(data.bytes,&op,sizeof(op));bool ok=managementQueue(data);web.send(ok?202:409,"text/plain",ok?"Configuracion pendiente de aplicacion":"Operacion pendiente");
  });
}
