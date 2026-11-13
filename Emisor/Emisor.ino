// ARCHIVO UNICO - Emisor - Sensor lluvia / paro bombas

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


#include <esp_sleep.h>

// --------- HARDWARE: Heltec V3 + MH-RD powered at 3V3 ----------
constexpr uint8_t PIN_RAIN_AO = 4;      // ADC1, NOT GPIO34 (ESP32-S3).
constexpr uint8_t PIN_RAIN_DO = 2;      // Optional diagnostics only.
// Modes: TEST = frequent ADC sampling while awake; PRODUCTION = periodic deep sleep.
constexpr uint32_t MIN_SLEEP_SECONDS = 60;
constexpr uint32_t MAX_SLEEP_SECONDS = 3600;
constexpr uint32_t SCREEN_REFRESH_MS = 1000;
constexpr uint32_t TEST_HEARTBEAT_MS = 60000;
constexpr uint32_t HELLO_RETRY_MS = 15000;
constexpr uint32_t ACK_TIMEOUT_MS = 3000;
constexpr uint32_t ACK_RETRY_GAP_MS = 4000;
constexpr uint8_t MAX_ATTEMPTS = 3;

// RTC variables survive deep sleep: avoid writing flash on every timer wake.
RTC_DATA_ATTR uint32_t rtcSignature = 0;
RTC_DATA_ATTR uint32_t rtcSenderBoot = 0;
RTC_DATA_ATTR uint32_t rtcSeq = 0;
RTC_DATA_ATTR uint64_t rtcSession = 0;
RTC_DATA_ATTR uint8_t rtcLastWet = 0;
constexpr uint32_t RTC_SIGNATURE = 0xC1A01025;

bool productionMode = false;  // Power-saving mode DISABLED until calibrated.
uint32_t testSampleMs = 200;  // continuous monitoring interval
uint32_t sleepSeconds = 120;  // timer deep sleep in production
bool doWakeEnabled = false;   // only after connecting and calibrating DO
bool resumeFromSleep = false;
int doLevel = -1;
uint32_t bootAtMs = 0;
volatile bool rxInterrupt = false;
uint32_t senderBoot = 0;
uint32_t txSequence = 0;
uint64_t sessionId = 0;
uint16_t analogRain = 4095;
uint16_t rainOnThreshold = 3500;
uint16_t rainOffThreshold = 3900;
uint16_t batteryMv = 0;
int8_t batteryGate = 0; // 0 = not identified; +1 active high (V3.2); -1 active low.
bool wet = false;
bool lastAckApplied = false;
bool lastAckArmed = false;
bool awaitingAck = false;
bool pending = false;
bool lastStateKnown = false;
bool radioHealthy = false;
uint32_t nextHelloMs = 0;
uint32_t lastSampleMs = 0;
uint32_t lastTxMs = 0;
uint32_t lastAckMs = 0;
uint32_t lastScreenMs = 0;
uint32_t ackWaitMs = 0;
uint32_t nextRetryMs = 0;
uint32_t pendingSeq = 0;
uint8_t attempts = 0;
float lastAckRssi = -150.0f;
float lastAckSnr = -20.0f;
uint32_t failedAckCount = 0;
uint32_t authRejectCount = 0;

void IRAM_ATTR onRadioReceive() { rxInterrupt = true; }
void beforeOta() {
  // Alert receiver: move to the previously configured fail-safe state.
  if (sessionId) {
    RainFrame maintenance = newFrame(FRAME_MAINT, TX_ID, senderBoot,
                                     ++txSequence, sessionId);
    maintenance.wet = wet ? 1 : 0;
    int16_t rc = transmitFrame(maintenance, onRadioReceive);
    Serial.printf("MAINT safety frame status=%d\n", rc);
  }
  Serial.println("OTA TX: paused LoRa state updates during maintenance");
}

uint16_t readRain() {
  uint32_t sum = 0;
  for (int i = 0; i < 8; ++i) sum += analogRead(PIN_RAIN_AO);
  return static_cast<uint16_t>(sum / 8);
}

// Battery divider is gated on GPIO37; gate polarity differs by V3 board revision.
uint16_t sampleBatteryWithGate(bool high) {
  pinMode(37, OUTPUT);
  digitalWrite(37, high ? HIGH : LOW);
  delay(3);
  uint32_t v = 0;
  for (uint8_t i = 0; i < 6; i++) v += analogReadMilliVolts(1);
  return static_cast<uint16_t>((v / 6) * 4.9f);
}

void readBattery() {
  if (batteryGate == 0) {
    uint16_t hi = sampleBatteryWithGate(true);
    uint16_t lo = sampleBatteryWithGate(false);
    if (hi >= 2800 && hi <= 4400 && (lo < 2000 || lo > 4500)) batteryGate = +1;
    else if (lo >= 2800 && lo <= 4400 && (hi < 2000 || hi > 4500)) batteryGate = -1;
    else {
      batteryMv = 0;  // Unknown: never invent battery values.
      return;
    }
  }
  batteryMv = sampleBatteryWithGate(batteryGate == +1);
  digitalWrite(37, batteryGate == +1 ? LOW : HIGH); // Disable divider when idle.
}

bool saveBootCounter() {
  Preferences p;
  if (!p.begin("rainboot", false)) return false;
  uint32_t previous = p.getUInt("n", 0);
  if (previous == UINT32_MAX) { p.end(); return false; }
  senderBoot = previous + 1;
  bool ok = p.putUInt("n", senderBoot) == sizeof(uint32_t);
  p.end();
  return ok;
}

void loadThresholds() {
  Preferences p;
  p.begin("rainsensor", true);
  rainOnThreshold = p.getUShort("on", 3500);
  rainOffThreshold = p.getUShort("off", 3900);
  p.end();
  if (rainOnThreshold < 100 || rainOffThreshold > 4095 ||
      rainOffThreshold < rainOnThreshold + 150) {
    rainOnThreshold = 3500;
    rainOffThreshold = 3900;
  }
}

void loadModeSettings() {
  Preferences p;
  p.begin("rainmode", true);
  productionMode = p.getBool("prod", false);
  testSampleMs = p.getUInt("sample", 200);
  sleepSeconds = p.getUInt("sleep", 120);
  doWakeEnabled = p.getBool("dowake", false);
  p.end();
  testSampleMs = constrain(testSampleMs, 50UL, 60000UL);
  sleepSeconds = constrain(sleepSeconds, MIN_SLEEP_SECONDS, MAX_SLEEP_SECONDS);
}

String roleControlsHtml() {
  String h = F("<h2>Sensor</h2><form method='POST' action='/settings?token=");
  h += coreCsrfToken;
  h += F("'><label>LLUVIA si AO &lt;= <input name='on' type='number' min='100' max='3900' value='");
  h += rainOnThreshold;
  h += F("'></label><label>SECO si AO &gt;= <input name='off' type='number' min='200' max='4095' value='");
  h += rainOffThreshold;
  h += F("'></label><h2>Funcionamiento</h2><label>Modo <select name='prod'>");
  h += String("<option value='0'") + (productionMode ? "" : " selected") + ">Prueba continua</option>";
  h += String("<option value='1'") + (productionMode ? " selected" : "") + ">Produccion con sleep</option></select></label>";
  h += F("<label>Lectura en pruebas (ms) <input type='number' name='sample' min='50' max='60000' value='");
  h += testSampleMs;
  h += F("'></label><label>Intervalo produccion (s) <input type='number' name='sleep' min='60' max='3600' value='");
  h += sleepSeconds;
  h += F("'></label><label><input type='checkbox' name='dowake' value='1'");
  if (doWakeEnabled) h += " checked";
  h += F("> Despertar por DO=LOW (solo si probado)</label>"
         "<small>En produccion la deteccion periodicada puede demorarse hasta el intervalo seleccionado. "
         "La entrada DO permite despertar antes si esta correctamente calibrada y alimentada.</small>"
         "<p><button>Guardar configuracion</button></p></form>");
  return h;
}

void configureRoleRoutes() {
  web.on("/settings", HTTP_POST, []() {
    if (!coreIsAuthenticated()) return;
    if (web.arg("token") != coreCsrfToken) { web.send(403, "text/plain", "Token"); return; }
    int on = web.arg("on").toInt(), off = web.arg("off").toInt();
    int sample = web.arg("sample").toInt(), sleep = web.arg("sleep").toInt();
    if (on < 100 || on > 3900 || off > 4095 || off < on + 150 ||
        sample < 50 || sample > 60000 || sleep < (int)MIN_SLEEP_SECONDS ||
        sleep > (int)MAX_SLEEP_SECONDS) {
      web.send(400, "text/plain", "Configuracion invalida"); return;
    }
    rainOnThreshold = on; rainOffThreshold = off;
    productionMode = web.arg("prod") == "1";
    testSampleMs = sample; sleepSeconds = sleep;
    doWakeEnabled = web.hasArg("dowake") && web.arg("dowake") == "1";
    Preferences p;
    p.begin("rainsensor", false); p.putUShort("on",on); p.putUShort("off",off); p.end();
    p.begin("rainmode", false);
    p.putBool("prod",productionMode); p.putUInt("sample",testSampleMs);
    p.putUInt("sleep",sleepSeconds); p.putBool("dowake",doWakeEnabled); p.end();
    Serial.printf("Config TX: prod=%d sample=%lu ms sleep=%lu s doWake=%d\n",
                  productionMode,(unsigned long)testSampleMs,(unsigned long)sleepSeconds,doWakeEnabled);
    web.sendHeader("Location", "/"); web.send(303,"text/plain","OK");
  });
}

String getStatusJson() {
  bool fresh = lastAckMs && millis() - lastAckMs < 180000UL;
  int quality = signalQuality(lastAckRssi,lastAckSnr,fresh,
                             (failedAckCount == 0 ? 100.0f :
                             100.0f * (float)(txSequence) /
                             (float)(txSequence + failedAckCount)));
  String s = "{\"rol\":\"Emisor\",\"version\":\"" + String(FW_VERSION) +
    "\",\"lluvia\":" + String(wet ? "true":"false") +
    ",\"AO\":" + String(analogRain) +
    ",\"DO\":" + String(doLevel) +
    ",\"umbral_lluvia\":" + String(rainOnThreshold) +
    ",\"umbral_seco\":" + String(rainOffThreshold) +
    ",\"bateria_mV\":" + String(batteryMv) +
    ",\"modo\":\"" + String(productionMode?"Produccion":"Prueba") + "\"" +
    ",\"lectura_pruebas_ms\":" + String(testSampleMs) +
    ",\"intervalo_produccion_s\":" + String(sleepSeconds) +
    ",\"despertar_DO\":" + String(doWakeEnabled ? "true":"false") +
    ",\"calidad_estim_pct\":" + (quality < 0 ? "null":String(quality)) +
    ",\"RSSI_dBm\":" + String(lastAckRssi,1) +
    ",\"SNR_dB\":" + String(lastAckSnr,1) +
    ",\"ACK_reciente\":" + String(fresh?"true":"false") +
    ",\"ACK_pendiente\":" + String(pending?"true":"false") +
    ",\"fallos_ACK\":" + String(failedAckCount) +
    ",\"rele_remoto_armado\":" + String(lastAckArmed?"true":"false") +
    ",\"rechazos_auth\":" + String(authRejectCount) + "}";
  return s;
}

void queueState() {
  pending = true;
  awaitingAck = false;
  attempts = 0;
  pendingSeq = ++txSequence;
  nextRetryMs = millis();
}

void transmitHello() {
  RainFrame hello = newFrame(FRAME_HELLO, TX_ID, senderBoot, 0);
  int16_t rc = transmitFrame(hello, onRadioReceive);
  Serial.printf("HELLO boot=%lu status=%d\n", (unsigned long)senderBoot, rc);
  nextHelloMs = millis() + HELLO_RETRY_MS;
}

void transmitState() {
  RainFrame state = newFrame(FRAME_STATE, TX_ID, senderBoot, pendingSeq, sessionId);
  state.wet = wet ? 1 : 0;
  state.ao = analogRain;
  state.batteryMv = batteryMv;
  // In STATE frames ackSeq carries the planned status-report interval,
  // allowing RX to distinguish a scheduled sleep from a lost link.
  state.ackSeq = productionMode ? sleepSeconds : TEST_HEARTBEAT_MS / 1000;
  if (productionMode) state.flags |= FRAME_FLAG_SLEEP;
  if (doWakeEnabled) state.flags |= FRAME_FLAG_DO_WAKE;
  int16_t rc = transmitFrame(state, onRadioReceive);
  lastTxMs = millis();
  attempts++;
  if (rc != RADIOLIB_ERR_NONE) {
    Serial.printf("TX error=%d\n", rc);
    awaitingAck = false;
    nextRetryMs = millis() + ACK_RETRY_GAP_MS;
    return;
  }
  awaitingAck = true;
  ackWaitMs = millis();
  Serial.printf("STATE #%lu wet=%u AO=%u attempt=%u\n",
                (unsigned long)pendingSeq, wet, analogRain, attempts);
}

void processReceived() {
  if (!rxInterrupt) return;
  rxInterrupt = false;
  RainFrame f{};
  if (!readFrame(f)) {
    authRejectCount++;
    radio.startReceive();
    return;
  }
  float rssi = radio.getRSSI();
  float snr = radio.getSNR();
  if (f.sender != RX_ID) { radio.startReceive(); return; }
  if (f.type == FRAME_CHALLENGE && f.session != 0) {
    if (sessionId != f.session) {
      sessionId = f.session;
      Serial.println("Sesion radio sincronizada");
      queueState();
    }
  } else if (f.type == FRAME_ACK && f.session == sessionId &&
             f.ackSeq == pendingSeq && awaitingAck) {
    awaitingAck = false;
    pending = false;
    radioHealthy = true;
    lastAckMs = millis();
    lastAckRssi = rssi;
    lastAckSnr = snr;
    lastAckArmed = f.flags & ACK_RELAY_ARMED;
    lastAckApplied = f.flags & ACK_APPLIED_GPIO;
    Serial.printf("ACK #%lu RSSI=%.1f SNR=%.1f armed=%d gpio=%d\n",
                  (unsigned long)pendingSeq, rssi, snr,
                  lastAckArmed, lastAckApplied);
  }
  radio.startReceive();
}

void updateSensor() {
  if (millis() - lastSampleMs < testSampleMs) return;
  lastSampleMs = millis();
  analogRain = readRain();
  doLevel = digitalRead(PIN_RAIN_DO);
  bool newWet = wet;
  if (!wet && analogRain <= rainOnThreshold) newWet = true;
  else if (wet && analogRain >= rainOffThreshold) newWet = false;
  if (!lastStateKnown || newWet != wet) {
    wet = newWet;
    lastStateKnown = true;
    queueState();
    coreWakeDisplay(45000);
    Serial.printf("RAIN EVENT wet=%u AO=%u\n", wet, analogRain);
  }
}

void drawStatus() {
  if (millis() - lastScreenMs < SCREEN_REFRESH_MS) return;
  lastScreenMs = millis();
  if (coreApActive && millis() - coreApStartMs < 30000) {
    coreDisplayPortalIfNeeded(); return;
  }
  bool fresh = lastAckMs && millis() - lastAckMs < 180000UL;
  float delivery = failedAckCount == 0 ? 100.0f :
       100.0f * (float)txSequence / (float)(txSequence + failedAckCount);
  int quality = signalQuality(lastAckRssi,lastAckSnr,fresh,delivery);
  String q = quality < 0 ? "--%" : String(quality) + "%*";
  String bat = batteryMv ? String(batteryMv/1000.0f,2)+"V" : "--";
  coreScreen("SENSOR LLUVIA / TX", wet ? "LLUVIA" : "SECO",
             "CALIDAD: " + q,
             "RSSI:" + String(lastAckRssi,0) + " SNR:" + String(lastAckSnr,1),
             "AO:" + String(analogRain) + " BAT:" + bat,
             String(productionMode?"PROD":"TEST") +
             (pending ? " TX PENDIENTE" : " ACK OK"));
}

void sleepUntilNextCheck() {
  if (!productionMode || coreApActive || coreOtaInProgress || pending || awaitingAck) return;
  if ((millis() - bootAtMs) < 2500) return;
  Serial.printf("Deep sleep: %lu s; DO=%d, wet=%d\n",
                (unsigned long)sleepSeconds, doLevel, wet);
  rtcSignature=RTC_SIGNATURE; rtcSenderBoot=senderBoot;
  rtcSeq=txSequence; rtcSession=sessionId; rtcLastWet=wet?1:0;
  radio.sleep();
  oled.setPowerSave(1);
  digitalWrite(VEXT_CTRL, HIGH); // OLED power OFF, MH-RD must use 3V3 not Vext.
  WiFi.mode(WIFI_OFF);
  esp_sleep_enable_timer_wakeup((uint64_t)sleepSeconds*1000000ULL);
  if (doWakeEnabled && !wet && digitalRead(PIN_RAIN_DO)==HIGH) {
    // GPIO2 = RTC-capable on ESP32-S3; LM393 DO must go LOW on rain.
    esp_sleep_enable_ext1_wakeup_io(1ULL << PIN_RAIN_DO, ESP_EXT1_WAKEUP_ANY_LOW);
  }
  Serial.flush();
  esp_deep_sleep_start();
}

void setup() {
  Serial.begin(115200);
  delay(350);
  bootAtMs = millis();
  coreRole = "TX";
  coreInitDisplay();
  pinMode(PIN_RAIN_AO, INPUT);
  pinMode(PIN_RAIN_DO, INPUT_PULLUP);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_RAIN_AO, ADC_11db);
  loadThresholds(); loadModeSettings();
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  resumeFromSleep = rtcSignature == RTC_SIGNATURE &&
      (cause==ESP_SLEEP_WAKEUP_TIMER || cause==ESP_SLEEP_WAKEUP_EXT1);
  if (resumeFromSleep) {
    senderBoot=rtcSenderBoot; txSequence=rtcSeq;
    sessionId=rtcSession; wet=rtcLastWet!=0;
    lastStateKnown=true;
  } else {
    if (!saveBootCounter()) {
      Serial.println("FATAL: NVS contador");
      coreScreen("ERROR NVS", "No transmite", "", "", "", "");
      while(true) delay(1000);
    }
    rtcSignature=RTC_SIGNATURE;
    sessionId=0; txSequence=0;
  }
  analogRain=readRain(); doLevel=digitalRead(PIN_RAIN_DO);
  if (!resumeFromSleep) {
    // Conservative when the first value falls into the hysteresis zone.
    wet = analogRain < rainOffThreshold;
    lastStateKnown = true;
  } else {
    if (!wet && analogRain <= rainOnThreshold) wet=true;
    if (wet && analogRain >= rainOffThreshold) wet=false;
  }
  readBattery();
  if (!initRadio(onRadioReceive)) {
    coreScreen("ERROR RADIO", "Revisar radio", "", "", "", "");
    while(true) delay(1000);
  }
  // Timer wake-ups MUST NOT reopen the WiFi AP (battery + secure 3-minute window).
  if (!resumeFromSleep) coreStartPortal(false);
  Serial.printf("TX %s, modo=%s, AO=%u, wet=%d, boot=%lu, wake=%d\n",
        FW_VERSION, productionMode?"PROD":"TEST", analogRain, wet,
        (unsigned long)senderBoot,(int)cause);
  lastSampleMs=millis()-testSampleMs;
  lastTxMs=0;
  if (sessionId) queueState();
  else nextHelloMs=0;
}

void loop() {
  coreTickPortal();
  coreTickDisplay(false);
  processReceived();
  updateSensor();

  static uint32_t lastBatteryMs=0;
  if (millis()-lastBatteryMs > 30000UL) {
    lastBatteryMs=millis(); readBattery();
  }
  if (!sessionId) {
    if (millis() >= nextHelloMs && !coreOtaInProgress) transmitHello();
  } else {
    if (!pending && millis()-lastTxMs >= TEST_HEARTBEAT_MS &&
        !productionMode) queueState();
    if (awaitingAck && millis()-ackWaitMs >= ACK_TIMEOUT_MS) {
      awaitingAck=false;
      failedAckCount++;
      nextRetryMs=millis()+ACK_RETRY_GAP_MS;
      Serial.printf("ACK timeout #%lu\n",(unsigned long)pendingSeq);
    }
    if (pending && !awaitingAck && !coreOtaInProgress && millis()>=nextRetryMs) {
      if (attempts < MAX_ATTEMPTS) transmitState();
      else {
        Serial.printf("Sin ACK #%lu; recuperando sesion\n",(unsigned long)pendingSeq);
        pending=false;
        sessionId=0;
        nextHelloMs=millis();
        lastTxMs=millis();
        radioHealthy=false;
      }
    }
  }
  // In production avoid indefinite awake on a dead receiver.
  if (productionMode && !coreApActive && !coreOtaInProgress &&
      ((sessionId && !pending && !awaitingAck) ||
       (!sessionId && millis()-bootAtMs > 18000UL))) {
    if (!sessionId) { pending=false; awaitingAck=false; }
    sleepUntilNextCheck();
  }
  drawStatus();
  delay(5);
}
