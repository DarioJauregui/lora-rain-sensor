// ARCHIVO UNICO - Receptor - Sensor lluvia / paro bombas

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <RadioLib.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <Preferences.h>
#include <mbedtls/md.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <stddef.h>

// Version shared by the two devices; Arduino-ESP32 core 3.3.12, RadioLib 7.8.1.
static const char *FW_VERSION = "1.0.0-rc";
static const uint32_t PROTOCOL_MAGIC = 0x52414E39UL;
static const uint8_t PROTOCOL_VERSION = 1;
static const uint32_t TX_ID = 0x54583031UL;
static const uint32_t RX_ID = 0x52583031UL;
static const uint8_t FRAME_HELLO = 1;
static const uint8_t FRAME_CHALLENGE = 2;
static const uint8_t FRAME_STATE = 3;
static const uint8_t FRAME_ACK = 4;
static const uint8_t FRAME_MAINT = 5;
static const uint8_t ACK_APPLIED_GPIO = 0x01;
static const uint8_t ACK_RELAY_ARMED = 0x02;
static const uint8_t FRAME_FLAG_SLEEP = 0x04;
static const uint8_t FRAME_FLAG_DO_WAKE = 0x08;

// Clave HMAC: archivo privado local, no versionado.
#include "pair_key.h"

// Verified Heltec WiFi LoRa 32 V3 pins.
static constexpr uint8_t RADIO_NSS = 8;
static constexpr uint8_t RADIO_SCK = 9;
static constexpr uint8_t RADIO_MOSI = 10;
static constexpr uint8_t RADIO_MISO = 11;
static constexpr uint8_t RADIO_RST = 12;
static constexpr uint8_t RADIO_BUSY = 13;
static constexpr uint8_t RADIO_DIO1 = 14;
static constexpr uint8_t OLED_SDA = 17;
static constexpr uint8_t OLED_SCL = 18;
static constexpr uint8_t OLED_RST = 21;
static constexpr uint8_t VEXT_CTRL = 36;

static constexpr float LORA_FREQUENCY_MHZ = 868.3f;
static constexpr float LORA_BW_KHZ = 125.0f;
static constexpr uint8_t LORA_SF = 9;
static constexpr uint8_t LORA_CR = 5;
static constexpr int8_t LORA_POWER_DBM = 10;
static constexpr uint8_t LORA_PREAMBLE = 8;
static constexpr uint8_t LORA_SYNC_WORD = 0x12;
static constexpr float LORA_TCXO_V = 1.8f;

// Authenticated point-to-point frames; weather data is NOT encrypted.
struct __attribute__((packed)) RainFrame {
  uint32_t magic;
  uint8_t version;
  uint8_t type;
  uint8_t wet;
  uint8_t flags;
  uint32_t sender;
  uint32_t boot;
  uint32_t seq;
  uint64_t session;
  uint16_t ao;
  uint16_t batteryMv;
  uint32_t ackSeq;
  uint8_t tag[16];
};
static_assert(sizeof(RainFrame) == 52, "Protocol layout changed");

SX1262 radio = new Module(RADIO_NSS, RADIO_DIO1, RADIO_RST, RADIO_BUSY);
U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, OLED_RST, OLED_SCL, OLED_SDA);
WebServer web(80);

String coreRole;
String coreApSsid;
String coreApPassword;
String coreCsrfToken;
bool coreApActive = false;
bool coreApPermanent = false;
bool coreOtaInProgress = false;
bool coreOtaAuthenticated = false;
bool coreOtaOk = false;
uint32_t coreApStartMs = 0;
uint32_t coreRebootAtMs = 0;
bool coreRoutesReady = false;
bool coreOledReady = false;
bool coreOledOn = false;
uint32_t coreScreenUntilMs = 0;
constexpr uint32_t PORTAL_WINDOW_MS = 180000;
// An estimated diagnostic indicator, not a calibrated coverage percentage.
int signalQuality(float rssi, float snr, bool fresh, float deliveryPct = 100.0f) {
  if (!fresh) return -1;
  float r = constrain((rssi + 135.0f) * 100.0f / 70.0f, 0.0f, 100.0f);
  float n = constrain((snr + 15.0f) * 100.0f / 27.0f, 0.0f, 100.0f);
  return (int)roundf(0.35f * r + 0.30f * n +
                     0.35f * constrain(deliveryPct, 0.0f, 100.0f));
}
// EU868 conservative airtime cap: 30 seconds TX per local 1h window
// (<1% of 3600 seconds). SX1262 SF9/BW125/52B approx 329 ms.
// This is an engineering guard, not an RF conformity certificate.
uint32_t coreDutyWindowMs = 0;
uint32_t coreDutyUsedMs = 0;
static constexpr uint32_t ESTIMATED_FRAME_AIRTIME_MS = 345;
static constexpr uint32_t MAX_HOURLY_TX_MS = 30000;

// Implemented in each sketch.
String getStatusJson();
String roleControlsHtml();
void configureRoleRoutes();
void beforeOta();

void signFrame(RainFrame &f) {
  uint8_t digest[32];
  memset(f.tag, 0, sizeof(f.tag));
  const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  mbedtls_md_hmac(md, PAIR_KEY, sizeof(PAIR_KEY),
                  reinterpret_cast<const uint8_t *>(&f),
                  offsetof(RainFrame, tag), digest);
  memcpy(f.tag, digest, sizeof(f.tag));
  memset(digest, 0, sizeof(digest));
}

bool validFrame(const RainFrame &f) {
  if (f.magic != PROTOCOL_MAGIC || f.version != PROTOCOL_VERSION) return false;
  RainFrame check = f;
  signFrame(check);
  uint8_t diff = 0;
  for (uint8_t i = 0; i < sizeof(f.tag); i++) diff |= f.tag[i] ^ check.tag[i];
  return diff == 0;
}

RainFrame newFrame(uint8_t type, uint32_t sender, uint32_t boot,
                   uint32_t seq, uint64_t session = 0) {
  RainFrame f{};
  f.magic = PROTOCOL_MAGIC;
  f.version = PROTOCOL_VERSION;
  f.type = type;
  f.sender = sender;
  f.boot = boot;
  f.seq = seq;
  f.session = session;
  return f;
}

uint64_t random64() {
  return (static_cast<uint64_t>(esp_random()) << 32) | esp_random();
}

bool initRadio(void (*onRx)(void)) {
  SPI.begin(RADIO_SCK, RADIO_MISO, RADIO_MOSI, RADIO_NSS);
  int16_t rc = radio.begin(LORA_FREQUENCY_MHZ, LORA_BW_KHZ, LORA_SF,
                           LORA_CR, LORA_SYNC_WORD, LORA_POWER_DBM,
                           LORA_PREAMBLE, LORA_TCXO_V);
  if (rc != RADIOLIB_ERR_NONE) {
    Serial.printf("RADIO INIT ERROR %d\n", rc);
    return false;
  }
  rc = radio.setDio2AsRfSwitch(true);
  if (rc != RADIOLIB_ERR_NONE) Serial.printf("RF SWITCH WARNING %d\n", rc);
  radio.setCRC(2);  // LoRa CRC-16, in addition to HMAC authentication.
  radio.setPacketReceivedAction(onRx);
  rc = radio.startReceive();
  Serial.printf("Radio RX: %d, SF%u, %.1f MHz\n", rc, LORA_SF, LORA_FREQUENCY_MHZ);
  return rc == RADIOLIB_ERR_NONE;
}

int16_t transmitFrame(RainFrame &f, void (*onRx)(void)) {
  const uint32_t now = millis();
  if (now - coreDutyWindowMs >= 3600000UL) {
    coreDutyWindowMs = now;
    coreDutyUsedMs = 0;
  }
  if (coreDutyUsedMs + ESTIMATED_FRAME_AIRTIME_MS > MAX_HOURLY_TX_MS) {
    Serial.println("DUTY LIMIT: packet not transmitted");
    return -32000;
  }
  coreDutyUsedMs += ESTIMATED_FRAME_AIRTIME_MS;
  signFrame(f);
  radio.clearPacketReceivedAction();
  int16_t rc = radio.transmit(reinterpret_cast<uint8_t *>(&f), sizeof(f));
  radio.setPacketReceivedAction(onRx);
  int16_t armRc = radio.startReceive();
  if (armRc != RADIOLIB_ERR_NONE) Serial.printf("RX REARM ERROR %d\n", armRc);
  return rc;
}

bool readFrame(RainFrame &f) {
  size_t length = radio.getPacketLength();
  if (length != sizeof(RainFrame)) {
    uint8_t discard[128];
    if (length && length <= sizeof(discard)) radio.readData(discard, length);
    else radio.standby();
    return false;
  }
  if (radio.readData(reinterpret_cast<uint8_t *>(&f), sizeof(f)) != RADIOLIB_ERR_NONE) return false;
  return validFrame(f);
}

void coreWakeDisplay(uint32_t durationMs) {
  if (!coreOledReady) return;
  oled.setPowerSave(0);
  coreOledOn = true;
  coreScreenUntilMs = millis() + durationMs;
}

void coreScreen(const String &line1, const String &line2,
                const String &line3, const String &line4,
                const String &line5, const String &line6) {
  if (!coreOledReady || !coreOledOn) return;
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x10_tf);
  const String lines[] = {line1, line2, line3, line4, line5, line6};
  for (uint8_t i = 0; i < 6; ++i) {
    oled.drawUTF8(0, 10 + 10 * i, lines[i].substring(0, 21).c_str());
  }
  oled.sendBuffer();
}

void coreInitDisplay() {
  pinMode(VEXT_CTRL, OUTPUT);
  digitalWrite(VEXT_CTRL, LOW);  // Heltec V3 Vext enabled.
  delay(90);
  Wire.begin(OLED_SDA, OLED_SCL);
  oled.setI2CAddress(0x3C * 2);
  oled.begin();
  coreOledReady = true;
  coreWakeDisplay(120000);
  coreScreen("LLUVIA / " + coreRole, "Iniciando...", "", "", "", "");
}

String coreGeneratePassword() {
  const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
  String p;
  for (int i = 0; i < 18; i++) p += alphabet[esp_random() % (sizeof(alphabet) - 1)];
  return p;
}

String coreGenerateCsrf() {
  char buf[21];
  snprintf(buf, sizeof(buf), "%08lX%08lX", (unsigned long)esp_random(),
           (unsigned long)esp_random());
  return String(buf);
}

bool coreIsAuthenticated() {
  if (web.authenticate("admin", coreApPassword.c_str())) return true;
  web.requestAuthentication(DIGEST_AUTH, "LLUVIA", "Acceso restringido");
  return false;
}

String coreHtml() {
  String h = R"PORTAL(<!doctype html><html lang="es"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Sensor lluvia - paro bombas</title>
<style>
:root{color-scheme:light}*{box-sizing:border-box}body{font:14px system-ui,Arial;
background:#fff;color:#282828;margin:0 auto;padding:20px;max-width:680px}
h1{font-size:19px;color:#6D130E;margin:0 0 12px}h2{font-size:16px;color:#6D130E;
border-bottom:1px solid #ddd;padding-bottom:6px;margin-top:22px}
small{color:#666}.row{display:flex;justify-content:space-between;gap:12px;
padding:7px 0;border-bottom:1px solid #eee}.row span{overflow-wrap:anywhere;text-align:right}
label{display:block;margin:9px 0}input,select{font:inherit;padding:7px;max-width:100%;
border:1px solid #aaa;border-radius:4px}input[type=number]{width:115px}
button{background:#6D130E;border:0;border-radius:4px;color:white;font:inherit;
padding:9px 14px;margin:6px 0;cursor:pointer}button:active{background:#DC241F}
.err{color:#DC241F}.tag{font-size:12px;color:#6D130E}.meta{color:#666;font-size:12px}
fieldset{border:1px solid #ddd;border-radius:5px;margin:10px 0}
</style></head><body>
<h1>Sensor lluvia - paro bombas</h1><div class="meta">Rol: )PORTAL";
  h += coreRole;
  h += F(" · Firmware: "); h += FW_VERSION;
  h += R"PORTAL(</div><h2>Estado</h2><div id="status">Leyendo...</div>
<script>
async function refresh(){try{let r=await fetch('/status',{cache:'no-store'});
if(!r.ok)throw Error();let d=await r.json();document.getElementById('status').innerHTML=
Object.entries(d).map(([k,v])=>'<div class="row"><b>'+k.replaceAll('_',' ')+'</b><span>'+
((v===true)?'Sí':(v===false)?'No':String(v))+'</span></div>').join('');
}catch(e){document.getElementById('status').textContent='Sin conexión';}}
refresh();setInterval(refresh,2500);
</script>)PORTAL";
  h += roleControlsHtml();
  h += F("<h2>Firmware OTA</h2><small>Solo archivo .bin de esta placa y rol. "
         "Comprobar estado de la maniobra antes de actualizar.</small>"
         "<form method='POST' enctype='multipart/form-data' action='/update?token=");
  h += coreCsrfToken;
  h += F("'><input type='file' name='firmware' accept='.bin' required>"
         "<button type='submit'>Actualizar</button></form></body></html>");
  return h;
}

void coreHandleUpload() {
  HTTPUpload &u = web.upload();
  if (u.status == UPLOAD_FILE_START) {
    coreOtaAuthenticated = web.authenticate("admin", coreApPassword.c_str()) &&
                           web.arg("token") == coreCsrfToken &&
                           u.filename.endsWith(".bin") &&
                           u.filename.indexOf(coreRole == "TX" ? "Emisor" : "Receptor") >= 0;
    coreOtaOk = false;
    if (!coreOtaAuthenticated) {
      Serial.println("OTA rejected (auth/token/filename)");
      return;
    }
    coreOtaInProgress = true;
    Serial.printf("OTA start: %s\n", u.filename.c_str());
    if (!esp_ota_get_next_update_partition(nullptr)) {
      coreOtaAuthenticated = false;
      coreOtaInProgress = false;
      Serial.println("OTA unavailable: no second app partition");
      return;
    }
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      coreOtaAuthenticated = false;
      coreOtaInProgress = false;
      Update.printError(Serial);
    } else {
      beforeOta(); // Receiver puts output in its configured fail-safe state.
    }
  } else if (u.status == UPLOAD_FILE_WRITE && coreOtaAuthenticated) {
    if (Update.write(u.buf, u.currentSize) != u.currentSize) {
      coreOtaAuthenticated = false;
      Update.abort();
      Serial.println("OTA write error");
    }
  } else if (u.status == UPLOAD_FILE_END) {
    if (coreOtaAuthenticated) coreOtaOk = Update.end(true);
    coreOtaInProgress = false;
    Serial.println(coreOtaOk ? "OTA completed" : "OTA incomplete/invalid");
    if (!coreOtaOk) Update.printError(Serial);
  } else if (u.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    coreOtaInProgress = false;
    coreOtaOk = false;
  }
}

void coreConfigureWeb() {
  web.on("/", HTTP_GET, []() {
    if (!coreIsAuthenticated()) return;
    web.send(200, "text/html; charset=utf-8", coreHtml());
  });
  web.on("/status", HTTP_GET, []() {
    if (!coreIsAuthenticated()) return;
    web.sendHeader("Cache-Control", "no-store");
    web.send(200, "application/json", getStatusJson());
  });
  web.on("/update", HTTP_POST, []() {
    if (!coreOtaAuthenticated) {
      web.send(403, "text/plain", "OTA denied");
      return;
    }
    web.send( coreOtaOk ? 200 : 500, "text/plain",
              coreOtaOk ? "OK. Reiniciando." : "ERROR OTA. Ver consola.");
    if (coreOtaOk) coreRebootAtMs = millis() + 1500;
  }, coreHandleUpload);
  web.onNotFound([]() {
    if (!coreIsAuthenticated()) return;
    web.send(404, "text/plain", "No encontrado");
  });
}

void coreStartPortal(bool permanent) {
  if (coreApActive) return;
  coreApPermanent = permanent;
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);

  Preferences p;
  p.begin("rainwifi", false);
  coreApPassword = p.getString("password", "");
  if (coreApPassword.length() < 16) {
    coreApPassword = coreGeneratePassword();
    p.putString("password", coreApPassword);
  }
  p.end();

  char suffix[10];
  snprintf(suffix, sizeof(suffix), "%04lX", (unsigned long)(ESP.getEfuseMac() & 0xFFFFUL));
  coreApSsid = "LLUVIA-" + coreRole + "-" + suffix;
  coreCsrfToken = coreGenerateCsrf();
  if (!WiFi.softAP(coreApSsid.c_str(), coreApPassword.c_str(), 6, false, 1)) {
    Serial.println("WIFI AP ERROR");
    return;
  }
  if (!coreRoutesReady) {
    coreConfigureWeb();
    configureRoleRoutes();
    coreRoutesReady = true;
  }
  web.begin();
  coreApActive = true;
  coreApStartMs = millis();
  Serial.printf("Portal: SSID=%s PASS=%s IP=%s\n", coreApSsid.c_str(),
                coreApPassword.c_str(), WiFi.softAPIP().toString().c_str());
  coreWakeDisplay(permanent ? 30000 : PORTAL_WINDOW_MS);
}

void coreStopPortal() {
  if (!coreApActive) return;
  web.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  coreApActive = false;
  Serial.println("WiFi mantenimiento cerrado");
}

void coreTickPortal() {
  if (!coreApActive) return;
  web.handleClient();
  if (coreRebootAtMs && (int32_t)(millis() - coreRebootAtMs) >= 0) ESP.restart();
  if (!coreApPermanent && !coreOtaInProgress &&
      millis() - coreApStartMs > PORTAL_WINDOW_MS) {
    coreStopPortal();
  }
}

void coreTickDisplay(bool onMains) {
  if (!coreOledReady || !coreOledOn || (coreApActive && coreOtaInProgress)) return;
  if (!onMains && (int32_t)(millis() - coreScreenUntilMs) >= 0) {
    oled.setPowerSave(1);
    coreOledOn = false;
  }
}

void coreDisplayPortalIfNeeded() {
  if (!coreApActive || !coreOledOn || coreOtaInProgress) return;
  coreScreen("CONFIG WIFI / " + coreRole, coreApSsid, "Clave:", coreApPassword,
             "192.168.4.1", coreApPermanent ? "Portal activo" : "Temporal: 3 min");
}

String signalText(float rssi, float snr, bool connected) {
  if (!connected) return "SIN ENLACE";
  // Qualitative guideline for SF9, not a calibrated range percentage.
  if (rssi > -90.0f && snr > 5.0f) return "EXCELENTE";
  if (rssi > -110.0f && snr > -5.0f) return "BUENA";
  if (rssi > -123.0f && snr > -11.0f) return "DEBIL";
  return "MUY DEBIL";
}



// HELTEC V3: output to NPN base via 1 kOhm, NPN collector -> relay IN.
// Relay board assumed 5V active-LOW at IN. GPIO5 HIGH => NPN ON => coil ON.
// For a series STOP circuit: COM + NO; dry=coil ON=contact CLOSED,
// wet=coil OFF=contact OPEN; loss of receiver power also OPENS NO.
constexpr uint8_t PIN_RELAY_NPN = 5;
constexpr uint32_t MIN_LINK_TIMEOUT_MS = 180000; // status only, NEVER changes coil.
constexpr uint32_t SCREEN_REFRESH_MS = 1000;

// 0 = single STOP latched until manual reset; 1 = timed stop (h:m:s).
constexpr uint8_t STOP_LATCH = 0;
constexpr uint8_t STOP_TIMED = 1;

volatile bool rxInterrupt = false;
bool controlArmed = false;       // output is disabled until validated on bench
bool coilOn = false;
bool stopActive = false;
bool restoredStopNeedsManualReset = false;
bool haveState = false;
bool lastRain = false;
bool radioLinked = false;
bool commsLost = true;
bool sessionReady = false;
uint8_t stopMode = STOP_LATCH;
uint32_t durationSeconds = 60;
uint32_t stopSinceMs = 0;
uint64_t receiverSession = 0;
uint32_t ownBoot = 0, senderBoot = 0, highestSeenBoot = 0;
uint32_t highestSeq = 0, ownSeq = 0, lastFreshMs = 0, lastScreenMs = 0;
uint32_t rxCount = 0, duplicates = 0, lostSequenceGaps = 0, authReject = 0;
uint32_t senderIntervalSeconds = 60;
uint32_t linkTimeoutMs() {
  uint32_t v = senderIntervalSeconds > 3600UL ? 3600UL : senderIntervalSeconds;
  uint32_t timeout = v * 3000UL;
  return timeout < MIN_LINK_TIMEOUT_MS ? MIN_LINK_TIMEOUT_MS : timeout;
}
uint16_t lastAO = 0, lastBatteryMv = 0;
float lastRssi = -150.0f, lastSnr = -20.0f;

void IRAM_ATTR onRadioReceive() { rxInterrupt = true; }

void setPhysicalCoil(bool on) {
  coilOn = on;
  digitalWrite(PIN_RELAY_NPN, on ? HIGH : LOW);
}

void applyControl() {
  // Startup and disarmed always OFF. Valid authenticated sensor data required.
  // Radio loss does NOT call this function: physical output is held unchanged.
  setPhysicalCoil(controlArmed && haveState && !stopActive);
}

void persistStop(bool active) {
  Preferences p;
  if (p.begin("rainrx", false)) { p.putBool("stop", active); p.end(); }
}

void activateStop() {
  if (stopActive) return; // once per rain edge
  stopActive = true;
  stopSinceMs = millis();
  persistStop(true); // if MCU restarts, STOP remains latched until manual reset
  applyControl();
  Serial.printf("STOP: edge rain, mode=%u, duration=%lu s\n",
                stopMode, (unsigned long)durationSeconds);
}

void clearStop(bool automatic = false) {
  stopActive = false;
  restoredStopNeedsManualReset = false;
  persistStop(false);
  applyControl();
  Serial.println(automatic ? "STOP timeout ended" : "STOP manually reset");
}

void beforeOta() {
  // Receiver resets during OTA; de-energize relay for safe COM+NO behaviour.
  setPhysicalCoil(false);
  Serial.println("OTA: coil OFF until authenticated status after reboot");
}

void loadSettings() {
  Preferences p;
  p.begin("rainrx", true);
  controlArmed = p.getBool("armed", false);
  stopMode = p.getUChar("mode", STOP_LATCH);
  durationSeconds = p.getUInt("seconds", 60);
  stopActive = p.getBool("stop", false);
  highestSeenBoot = p.getUInt("senderboot", 0);
  ownBoot = p.getUInt("selfboot", 0);
  p.end();
  if (stopMode > 1) stopMode = STOP_LATCH;
  durationSeconds = constrain(durationSeconds, 1UL, 359999UL); // max 99h59m59s
  restoredStopNeedsManualReset = stopActive;
  // Boot writes to NVS only on real receiver reboot, not periodic sender wake.
  p.begin("rainrx", false); ownBoot++; p.putUInt("selfboot",ownBoot); p.end();
}

String roleControlsHtml() {
  uint32_t h = durationSeconds / 3600, m = (durationSeconds % 3600)/60;
  uint32_t sec = durationSeconds%60;
  String s = F("<h2>Rele</h2><small>CONTACTOS: COM+NO. "
    "Bobina con corriente = normal (contacto cerrado). "
    "Sin corriente = paro (contacto abierto). Configurar solo en banco.</small>"
    "<form method='POST' action='/control?token=");
  s += coreCsrfToken;
  s += F("'><label>Salida <select name='armed'>");
  s += String("<option value='0'") + (controlArmed ? "":" selected") + ">Desarmada</option>";
  s += String("<option value='1'") + (controlArmed ? " selected":"") + ">Armada</option></select></label>";
  s += F("<label>Orden paro <select name='mode'>");
  s += String("<option value='0'") + (stopMode==STOP_LATCH?" selected":"") + ">Una orden, hasta rearme manual</option>";
  s += String("<option value='1'") + (stopMode==STOP_TIMED?" selected":"") + ">Paro durante tiempo definido</option></select></label>";
  s += F("<label>Horas <input name='hours' type='number' min='0' max='99' value='"); s += h;
  s += F("'></label><label>Minutos <input name='minutes' type='number' min='0' max='59' value='"); s += m;
  s += F("'></label><label>Segundos <input name='seconds' type='number' min='0' max='59' value='"); s += sec;
  s += F("'></label><label>Confirmar: <input name='confirm' placeholder='BANCO' required></label>"
         "<button>Guardar</button></form>"
         "<form method='POST' action='/reset-stop?token=");
  s += coreCsrfToken;
  s += F("'><label>Rearmar paro: <input name='confirm' placeholder='REARMAR' required></label>"
         "<button>Rearmar (solo sensor SECO y enlace OK)</button></form>"
         "<small>La duracion se cuenta desde cada nuevo paso SECO a LLUVIA. "
         "Con temporizador, al finalizar puede reabrirse el circuito de marcha; "
         "requiere validacion de la maniobra para evitar arranque automatico.</small>");
  return s;
}

void configureRoleRoutes() {
  web.on("/control", HTTP_POST, []() {
    if (!coreIsAuthenticated()) return;
    if (web.arg("token")!=coreCsrfToken || web.arg("confirm")!="BANCO") {
      web.send(403,"text/plain","Confirmacion incorrecta"); return;
    }
    int h=web.arg("hours").toInt(), m=web.arg("minutes").toInt(), sec=web.arg("seconds").toInt();
    int mode=web.arg("mode").toInt();
    if (h<0 || h>99 || m<0 || m>59 || sec<0 || sec>59 || mode<0 || mode>1 ||
        h*3600+m*60+sec<1) {
      web.send(400,"text/plain","Duracion o modo invalido"); return;
    }
    controlArmed=(web.arg("armed")=="1");
    stopMode=(uint8_t)mode;
    durationSeconds=(uint32_t)h*3600U + (uint32_t)m*60U + sec;
    Preferences p; p.begin("rainrx",false);
    p.putBool("armed",controlArmed); p.putUChar("mode",stopMode);
    p.putUInt("seconds",durationSeconds); p.end();
    // Changing the mode while stopped must never release that stop later.
    if (stopActive) restoredStopNeedsManualReset=true;
    applyControl();
    Serial.printf("RX CONFIG armed=%d mode=%u duration=%lu\n",
                  controlArmed, stopMode,(unsigned long)durationSeconds);
    web.sendHeader("Location", "/"); web.send(303,"text/plain","OK");
  });
  web.on("/reset-stop", HTTP_POST, []() {
    if (!coreIsAuthenticated()) return;
    if (web.arg("token")!=coreCsrfToken || web.arg("confirm")!="REARMAR") {
      web.send(403,"text/plain","Confirmacion incorrecta"); return;
    }
    if (!haveState || commsLost || lastRain) {
      web.send(409,"text/plain","Rearme bloqueado: sin enlace fiable o sensor mojado"); return;
    }
    clearStop();
    web.sendHeader("Location","/"); web.send(303,"text/plain","OK");
  });
}

String getStatusJson() {
  bool fresh=haveState && !commsLost;
  float packetRatio=rxCount ? 100.0f*(float)rxCount/(float)(rxCount+lostSequenceGaps) : 100.0f;
  int quality=signalQuality(lastRssi,lastSnr,fresh,packetRatio);
  String s="{\"rol\":\"Receptor\",\"version\":\"" + String(FW_VERSION) +
    "\",\"calidad_estim_pct\":" + (quality<0?"null":String(quality)) +
    ",\"RSSI_dBm\":"+String(lastRssi,1)+
    ",\"SNR_dB\":"+String(lastSnr,1)+
    ",\"enlace\":\""+String(commsLost?"SIN ENLACE":"OK")+"\""+
    ",\"lluvia\":"+String(lastRain?"true":"false")+
    ",\"paro_activo\":"+String(stopActive?"true":"false")+
    ",\"parada_requiere_rearme\":"+String(restoredStopNeedsManualReset?"true":"false")+
    ",\"salida_armada\":"+String(controlArmed?"true":"false")+
    ",\"bobina_energizada_comandada\":"+String(coilOn?"true":"false")+
    ",\"contacto_NO_teorico\":\""+String(coilOn?"CERRADO":"ABIERTO")+"\""+
    ",\"modo_paro\":\""+String(stopMode==STOP_LATCH?"Unica hasta rearme":"Temporizada")+"\""+
    ",\"duracion_paro_s\":"+String(durationSeconds)+
    ",\"intervalo_emisor_s\":"+String(senderIntervalSeconds)+
    ",\"desde_ultimo_RX_s\":"+String(lastFreshMs?(millis()-lastFreshMs)/1000:0)+
    ",\"paquetes_RX\":"+String(rxCount)+
    ",\"duplicados\":"+String(duplicates)+
    ",\"perdidas_estimadas\":"+String(lostSequenceGaps)+
    ",\"rechazos_auth\":"+String(authReject)+
    ",\"AO_remoto\":"+String(lastAO)+
    ",\"bateria_remota_mV\":"+String(lastBatteryMv)+"}";
  return s;
}

void sendChallenge() {
  if (!sessionReady) return;
  RainFrame challenge=newFrame(FRAME_CHALLENGE,RX_ID,ownBoot,++ownSeq,receiverSession);
  delay(65); // TX switches from TX to RX.
  int16_t rc=transmitFrame(challenge,onRadioReceive);
  Serial.printf("CHALLENGE status=%d\n",rc);
}

void sendAck(uint32_t txSeq) {
  RainFrame ack=newFrame(FRAME_ACK,RX_ID,ownBoot,++ownSeq,receiverSession);
  ack.ackSeq=txSeq;
  if (controlArmed) ack.flags |= ACK_RELAY_ARMED | ACK_APPLIED_GPIO;
  delay(65);
  int16_t rc=transmitFrame(ack,onRadioReceive);
  if (rc!=RADIOLIB_ERR_NONE) Serial.printf("ACK ERROR=%d\n",rc);
}

void processReceived() {
  if (!rxInterrupt) return;
  rxInterrupt=false;
  RainFrame f{};
  if (!readFrame(f)) {
    authReject++; radio.startReceive(); return;
  }
  float rssi=radio.getRSSI(), snr=radio.getSNR();
  if (f.sender!=TX_ID || f.boot < highestSeenBoot) {radio.startReceive();return;}

  if (f.type==FRAME_HELLO) {
    if (!sessionReady || senderBoot!=f.boot) {
      senderBoot=f.boot;
      receiverSession=random64();
      if (!receiverSession) receiverSession=1;
      sessionReady=true;
      highestSeq=0;
      commsLost=true; // diagnostics ONLY. DO NOT change coil on LoRa loss.
      if (senderBoot>highestSeenBoot) {
        highestSeenBoot=senderBoot;
        Preferences p; p.begin("rainrx",false);
        p.putUInt("senderboot",senderBoot);p.end();
      }
      Serial.printf("New radio session for boot %lu\n",(unsigned long)senderBoot);
    }
    sendChallenge(); return;
  }

  if (f.type==FRAME_MAINT && sessionReady &&
      f.session==receiverSession && f.boot==senderBoot && f.seq>highestSeq) {
    highestSeq=f.seq;
    Serial.println("TX in maintenance - hold last relay state");
    radio.startReceive(); return;
  }

  if (f.type!=FRAME_STATE) { radio.startReceive(); return; }
  if (!sessionReady || f.session!=receiverSession || f.boot!=senderBoot) {
    if (!sessionReady && f.boot>=highestSeenBoot) {
      senderBoot=f.boot; receiverSession=random64();
      if (!receiverSession) receiverSession=1;
      sessionReady=true; highestSeq=0;
    }
    sendChallenge(); return;
  }

  if (f.seq < highestSeq) {radio.startReceive();return;}
  if (f.seq == highestSeq && highestSeq!=0) {
    duplicates++; sendAck(f.seq); return;
  }
  if (highestSeq && f.seq>highestSeq+1)
    lostSequenceGaps+=f.seq-highestSeq-1;
  highestSeq=f.seq;
  lastFreshMs=millis();
  lastAO=f.ao; lastBatteryMv=f.batteryMv;
  senderIntervalSeconds=constrain((uint32_t)f.ackSeq, 30UL, 3600UL);
  bool nowWet = f.wet!=0;
  bool rainEdge = nowWet && (!haveState || !lastRain);
  lastRain=nowWet;
  haveState=true;
  commsLost=false;
  radioLinked=true;
  rxCount++;
  lastRssi=rssi;lastSnr=snr;
  if (rainEdge) activateStop();
  // No automatic rearm when dry. Timer (if selected) handled in loop().
  applyControl();
  Serial.printf("RX #%lu wet=%d stop=%d coil=%d RSSI=%.1f SNR=%.1f\n",
         (unsigned long)highestSeq,nowWet,stopActive,coilOn,rssi,snr);
  sendAck(f.seq);
}

void drawStatus() {
  if (millis()-lastScreenMs<SCREEN_REFRESH_MS) return;
  lastScreenMs=millis();
  if (coreApActive && millis()-coreApStartMs<30000) {
    coreDisplayPortalIfNeeded(); return;
  }
  bool fresh=haveState && !commsLost;
  float packetRatio=rxCount ? 100.0f*(float)rxCount/(float)(rxCount+lostSequenceGaps) : 100.0f;
  int quality=signalQuality(lastRssi,lastSnr,fresh,packetRatio);
  String q=quality<0 ? "--%" : String(quality)+"%*";
  coreScreen("PARO BOMBAS / RX",
     stopActive ? "PARO ACTIVO" : "SIN PARO",
     "CALIDAD: " + q,
     String("ENLACE: ") + (commsLost?"SIN ENLACE":"OK"),
     "RSSI:"+String(lastRssi,0)+" SNR:"+String(lastSnr,1),
     String("RELE:")+(coilOn?" ON":" OFF") + (controlArmed?"":" TEST"));
}

void setup() {
  Serial.begin(115200);delay(350);
  coreRole="RX";
  digitalWrite(PIN_RELAY_NPN,LOW);
  pinMode(PIN_RELAY_NPN,OUTPUT);
  setPhysicalCoil(false); // NO contact OPEN on reboot/unarmed.
  loadSettings();
  coreInitDisplay();
  if (!initRadio(onRadioReceive)) {
    coreScreen("ERROR RADIO","Revisar SX1262","","","","");
    while(true) delay(1000);
  }
  // Receptor is mains-powered, but AP is OFF after 3 minutes to limit attack surface.
  coreStartPortal(false);
  Serial.printf("RX %s, armed=%d, mode=%u, savedSTOP=%d\n",
       FW_VERSION,controlArmed,stopMode,stopActive);
}

void loop() {
  coreTickPortal();
  processReceived();
  if (!commsLost && haveState && millis()-lastFreshMs>linkTimeoutMs()) {
    commsLost=true;
    Serial.println("SIN ENLACE: conservando salida fisica del rele");
    // DO NOT call applyControl(), users explicitly require hold-last on radio loss.
  }
  if (stopMode==STOP_TIMED && stopActive &&
      !restoredStopNeedsManualReset && !commsLost &&
      millis()-stopSinceMs >= durationSeconds*1000UL) {
    clearStop(true);
  }
  drawStatus();
  delay(3);
}
