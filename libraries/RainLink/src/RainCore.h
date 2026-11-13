#pragma once
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
#include <atomic>
#include <esp_timer.h>
#include <sys/time.h>
#include "RainLogic.h"
#include "Portal.h"

// Version shared by the two devices; Arduino-ESP32 core 3.3.2, RadioLib 7.8.1.
static const char *FW_VERSION = "1.1.0-prototype";
static const uint32_t PROTOCOL_MAGIC = 0x52414E39UL;
static const uint8_t PROTOCOL_VERSION = 2;
static const uint32_t TX_ID = 0x54583031UL;
static const uint32_t RX_ID = 0x52583031UL;
static const uint8_t FRAME_HELLO = 1;
static const uint8_t FRAME_CHALLENGE = 2;
static const uint8_t FRAME_STATE = 3;
static const uint8_t FRAME_ACK = 4;
static const uint8_t FRAME_MAINT = 5;
static const uint8_t FRAME_CONFIG = 6;
static const uint8_t ACK_COIL_ON = 0x04;
static const uint8_t ACK_ACTIVE = 0x08;
static const uint8_t ACK_MAINTENANCE = 0x10;
static const uint8_t ACK_CONFIG = 0x20;
static const uint8_t ACK_REJECTED = 0x40;
static const uint8_t STATE_SIMULATED = 0x10;
static const uint8_t ACK_RELAY_ARMED = 0x02;

// Clave HMAC: archivo privado local, no versionado.
// PAIR_KEY is supplied by the sketch before including this header.
static_assert(sizeof(PAIR_KEY)==32,"PAIR_KEY must contain 32 bytes");

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
static constexpr uint8_t LORA_SF = 7;
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
  uint32_t event;
  uint64_t echo;
  uint64_t pulseMs;
  uint64_t remainingMs;
  uint32_t config;
  int16_t peerRssi10;
  int16_t peerSnr10;
  uint8_t tag[16];
};
static_assert(sizeof(RainFrame) == 88, "Protocol layout changed");

SX1262 radio = new Module(RADIO_NSS, RADIO_DIO1, RADIO_RST, RADIO_BUSY);
U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, OLED_RST, OLED_SCL, OLED_SDA);
WebServer web(80);

String coreRole;
String coreApSsid;
String coreApPassword;
String coreCsrfToken;
bool corePrototype = true;
bool coreDisplayEnabled = true;
std::atomic<bool> coreMaintenance{false};
bool coreApActive = false;
uint32_t coreCloseAtMs = 0;
uint32_t coreTxCount = 0, coreTxErrors = 0, coreDutyDeferrals = 0;
uint32_t coreFrameAirtimeMs = 650;
uint64_t uptimeMs() { return (uint64_t)esp_timer_get_time() / 1000; }
uint64_t rtcTimeMs() { timeval t; gettimeofday(&t, nullptr); return (uint64_t)t.tv_sec * 1000 + t.tv_usec / 1000; }
String u64(uint64_t n) { char b[24]; snprintf(b, sizeof(b), "%llu", (unsigned long long)n); return String(b); }
RTC_DATA_ATTR rain::Airtime coreAirtime;
uint64_t coreDutyWaitMs() { return coreAirtime.wait(rtcTimeMs(), coreFrameAirtimeMs); }
void recordEvent(const char *event);
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
// Implemented in each sketch.
String getStatusJson();
String roleControlsHtml();
void configureRoleRoutes();
void beforeOta();
void coreSaveDevice() {
  Preferences p; p.begin("raindevice", false);
  p.putBool("prototype", corePrototype); p.putBool("display", coreDisplayEnabled); p.end();
}
void coreLoadDevice() {
  Preferences p; p.begin("raindevice", true);
  corePrototype=p.getBool("prototype",true); coreDisplayEnabled=p.getBool("display",true); p.end();
}
bool corePostAllowed() {
  if (coreMaintenance) { web.send(423,"text/plain","Mantenimiento activo. Reiniciar para recuperar."); return false; }
  if (!web.authenticate("admin",coreApPassword.c_str()) || web.arg("token")!=coreCsrfToken) {
    web.send(403,"text/plain","Acceso denegado"); return false;
  }
  return true;
}
bool readUnsigned(const char *key, uint64_t &n) { return rain::decimal(web.arg(key).c_str(),n); }
bool readDuration(uint64_t &n) {
  uint64_t h,m,s,ms;
  return readUnsigned("hours",h) && readUnsigned("minutes",m) && readUnsigned("seconds",s) &&
         readUnsigned("milliseconds",ms) && rain::duration(h,m,s,ms,n);
}

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
  coreFrameAirtimeMs = (radio.getTimeOnAir(sizeof(RainFrame)) + 999) / 1000 + 5;
  radio.setPacketReceivedAction(onRx);
  rc = radio.startReceive();
  Serial.printf("Radio RX: %d, SF%u, %.1f MHz\n", rc, LORA_SF, LORA_FREQUENCY_MHZ);
  return rc == RADIOLIB_ERR_NONE;
}

int16_t transmitFrame(RainFrame &f, void (*onRx)(void)) {
  if (!coreAirtime.spend(rtcTimeMs(), coreFrameAirtimeMs)) {
    ++coreDutyDeferrals; return -32000;
  }
  ++coreTxCount;
  signFrame(f);
  radio.clearPacketReceivedAction();
  int16_t rc = radio.transmit(reinterpret_cast<uint8_t *>(&f), sizeof(f));
  radio.setPacketReceivedAction(onRx);
  int16_t armRc = radio.startReceive();
  if (armRc != RADIOLIB_ERR_NONE) Serial.printf("RX REARM ERROR %d\n", armRc);
  if (rc != RADIOLIB_ERR_NONE) ++coreTxErrors;
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
  if (!coreOledReady || !coreDisplayEnabled) return;
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
  if (!coreDisplayEnabled) oled.setPowerSave(1);
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
  String h = FPSTR(PORTAL_HTML);
  h.replace("__TOKEN__", coreCsrfToken);
  h.replace("__ROLE__", coreRole);
  h.replace("__VERSION__", FW_VERSION);
  h.replace("__CONTROLS__", roleControlsHtml());
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
      coreMaintenance = true;
      recordEvent("ota_start");
      beforeOta(); // Latched until reboot, including upload failure or cancellation.
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
    coreOtaAuthenticated = false;
  }
}

void coreConfigureWeb() {
  web.on("/device", HTTP_POST, []() {
    if (!corePostAllowed()) return;
    if (web.arg("mode")!="prototype" && web.arg("mode")!="production") { web.send(400,"text/plain","Modo invalido"); return; }
    corePrototype=web.arg("mode")=="prototype";
    coreDisplayEnabled=web.arg("display")=="1";
    coreSaveDevice();
    if (coreDisplayEnabled) coreWakeDisplay(120000);
    else { oled.setPowerSave(1); coreOledOn=false; }
    if (web.arg("wifi")=="0") coreCloseAtMs=millis()+700;
    recordEvent("device_config");
    web.send(200,"text/plain","Guardado. Si apagas Wi-Fi, reinicia para reabrirlo.");
  });
  web.on("/restart", HTTP_POST, []() {
    if (!coreIsAuthenticated()) return;
    if (web.arg("token")!=coreCsrfToken) {web.send(403,"text/plain","Acceso denegado");return;}
    web.send(200,"text/plain","Reiniciando"); coreRebootAtMs=millis()+700;
  });
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
  if (coreCloseAtMs && (int32_t)(millis()-coreCloseAtMs)>=0 && !coreOtaInProgress) { coreStopPortal(); coreCloseAtMs=0; return; }
  if (!corePrototype && !coreApPermanent && !coreOtaInProgress &&
      millis() - coreApStartMs > PORTAL_WINDOW_MS) {
    coreStopPortal();
  }
}

void coreTickDisplay(bool onMains) {
  if (!coreOledReady || !coreOledOn || (coreApActive && coreOtaInProgress)) return;
  if (!corePrototype && !onMains && (int32_t)(millis() - coreScreenUntilMs) >= 0) {
    oled.setPowerSave(1);
    coreOledOn = false;
  }
}

void coreDisplayPortalIfNeeded() {
  if (!coreApActive || !coreOledOn || coreOtaInProgress) return;
  coreScreen("CONFIG WIFI / " + coreRole, coreApSsid, "Clave:", coreApPassword,
             "192.168.4.1", (corePrototype || coreApPermanent) ? "Portal activo" : "Temporal: 3 min");
}

String coreStatusFields() {
  return ",\"prototype\":" + String(corePrototype?"true":"false") +
    ",\"display_enabled\":" + String(coreDisplayEnabled?"true":"false") +
    ",\"maintenance\":" + String(coreMaintenance?"true":"false") +
    ",\"uptime_ms\":\"" + u64(uptimeMs()) + "\"" +
    ",\"airtime_ms\":" + String(coreFrameAirtimeMs) +
    ",\"airtime_used_ms\":" + String(coreAirtime.used) +
    ",\"duty_wait_ms\":\"" + u64(coreDutyWaitMs()) + "\"" +
    ",\"tx_packets\":" + String(coreTxCount) +
    ",\"tx_errors\":" + String(coreTxErrors) +
    ",\"duty_deferrals\":" + String(coreDutyDeferrals);
}
