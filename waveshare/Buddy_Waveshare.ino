#include <Arduino.h>
#include "Arduino_GFX_Library.h"
#include "pin_config.h"
#include <Wire.h>
#include <math.h>
#include <Preferences.h>
#include "ESP_I2S.h"
#include "TouchDrvCSTXXX.hpp"
#include <BLEDevice.h>          // v19: koppeling met de Buddy-app op je telefoon
#include <BLEServer.h>
#include <BLE2902.h>
#include <BLESecurity.h>

#include <pgmspace.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>
#include <LittleFS.h>             // v24: logboek in het flashgeheugen
#include <WiFi.h>                 // v25: zelfstandig via de hotspot van je telefoon
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <time.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>
static const int FW_VERSIE = 30;
static volatile int otaPct = -1;
static uint32_t koppelPin = 0; static volatile bool gekoppeld = false; static volatile uint32_t pinTot = 0;
static volatile float oorNiveau = 0, oorBasis = 0; static volatile uint32_t muziekSinds = 0, klapT = 0, knalT = 0; static volatile bool oorOk = false;
   // voortgang van een update via wifi (-1 = geen)   // ophogen bij elke nieuwe versie (de update via wifi kijkt hiernaar)
#define FW_BASIS "https://raw.githubusercontent.com/ToxicRadiuM/GeenIdee/firmware/"
#define DATA_BASIS "https://raw.githubusercontent.com/ToxicRadiuM/GeenIdee/snelheden/"
struct JP { const char *p, *e; };   // mini-JSON-lezer (staat bovenaan i.v.m. de automatische prototypes van Arduino)
#include <FFat.h>
#include <esp_system.h>
#include <stdarg.h>

// ===================== v24: logboek (blijft bewaard, ook zonder computer) =====================
// Elke minuut een statusregel + gebeurtenissen (opstarten en waarom, rit, 0-100, GPS-fix, Bluetooth).
// Uitlezen: Serial Monitor -> typ "log" (en Enter), of in de Buddy-app via "Log van het schermpje ophalen".
static fs::FS *logFs = nullptr; static SemaphoreHandle_t logMutex = nullptr;
static const char *LOGF = "/buddy_log.txt", *LOGOUD = "/buddy_log_oud.txt";
static void logStart() {
  if (LittleFS.begin(true)) logFs = &LittleFS;
  else if (FFat.begin(true)) logFs = &FFat;
  logMutex = xSemaphoreCreateMutex();
  Serial.printf("Logboek: %s\n", logFs ? (logFs == &LittleFS ? "LittleFS klaar" : "FFat klaar") : "GEEN opslag (kies een Partition Scheme met SPIFFS of FATFS)");
}
static void logRegel(const char *fmt, ...) {
  char buf[200]; va_list ap; va_start(ap, fmt);
  uint32_t s0 = millis() / 1000; int n = snprintf(buf, sizeof buf, "[%02lu:%02lu:%02lu] ", (unsigned long)(s0 / 3600), (unsigned long)(s0 / 60 % 60), (unsigned long)(s0 % 60));
  vsnprintf(buf + n, sizeof buf - n, fmt, ap); va_end(ap);
  Serial.println(buf);
  if (!logFs || !logMutex || xSemaphoreTake(logMutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
  File f = logFs->open(LOGF, FILE_APPEND);
  if (f) { f.println(buf); size_t sz = f.size(); f.close(); if (sz > 200000) { logFs->remove(LOGOUD); logFs->rename(LOGF, LOGOUD); } }
  xSemaphoreGive(logMutex);
}
static const char *resetReden() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "stroom aan"; case ESP_RST_SW: return "herstart door software"; case ESP_RST_PANIC: return "CRASH (panic)";
    case ESP_RST_INT_WDT: return "CRASH (interrupt-waakhond)"; case ESP_RST_TASK_WDT: return "CRASH (taak-waakhond)"; case ESP_RST_WDT: return "CRASH (waakhond)";
    case ESP_RST_BROWNOUT: return "STROOM TE LAAG (brownout)"; case ESP_RST_DEEPSLEEP: return "uit diepe slaap"; case ESP_RST_EXT: return "reset-knop"; default: return "onbekend";
  }
}
static void logDump(Stream &uit) {
  if (!logFs) { uit.println("Geen logboek-opslag."); return; }
  for (const char *naam : {LOGOUD, LOGF}) { File f = logFs->open(naam, FILE_READ); if (!f) continue; uit.printf("===== %s (%u bytes) =====\n", naam, (unsigned)f.size()); while (f.available()) uit.write(f.read()); f.close(); }
  uit.println("===== einde logboek =====");
}
static volatile bool logVraag = false;

// v15: de LC76G op de -G versie praat via I2C (0x50 schrijven / 0x54 lezen) op de gedeelde bus (SDA 15, SCL 14),
// precies zoals het officiele Waveshare-voorbeeld 09_LC76G_I2C. De UART op 17/18 is volgens Waveshare optioneel
// en meestal niet doorverbonden: daarom kreeg v14 nooit data. UART blijft als reserve meelezen.
// GPS_RST loopt via TCA9554 P7. Alle I2C-gebruik (GPS, touch, audio) gaat via een mutex, want GPS draait op core 0.
static constexpr int GPS_UART_RX = 18;
static constexpr int GPS_UART_TX = 17;
static constexpr uint32_t GPS_UART_BAUD = 115200; // LC76G standaard
static HardwareSerial gpsSerial(1);
static SemaphoreHandle_t i2cMutex = nullptr; // alleen voor touchscreen op Wire
static volatile bool gpsFix = false;
static volatile bool gpsDataOk = false;
static volatile uint32_t gpsLastDataMs = 0;
static volatile uint32_t gpsNmeaBytes = 0;
static volatile int gpsSatellites = 0;
static volatile float gpsSpeedKmh = 0.0f, gpsLat = 0.0f, gpsLon = 0.0f;
static volatile uint32_t gpsLastFixMs = 0;
static volatile int gpsNearestCameraM = -1, gpsCameraLimit = 0;
static volatile bool gpsCameraAlert = false;
static char gpsNmea[160];
static volatile bool gpsNewFix = false;
static volatile uint32_t gpsI2cOk = 0, gpsI2cFail = 0;
static uint32_t gpsLastCameraAlert = 0;

static bool i2cLock(uint32_t ms = 30) { return !i2cMutex || xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(ms)) == pdTRUE; }
static void i2cUnlock() { if (i2cMutex) xSemaphoreGive(i2cMutex); }

// TCA9554 op 0x20: P7 stuurt GPS_RST op de 31264-G.
static bool releaseLc76gReset() {
  const uint8_t addr = 0x20;
  const uint8_t REG_OUTPUT = 0x01;
  const uint8_t REG_CONFIG = 0x03;
  auto readReg = [&](uint8_t reg, uint8_t &value) -> bool {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)addr, 1) != 1) return false;
    value = Wire.read();
    return true;
  };
  auto writeReg = [&](uint8_t reg, uint8_t value) -> bool {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
  };

  uint8_t config = 0xFF, output = 0xFF;
  if (!readReg(REG_CONFIG, config) || !readReg(REG_OUTPUT, output)) {
    Serial.println("GPS reset: TCA9554 niet bereikbaar");
    return false;
  }

  // P7 eerst als uitgang configureren; overige expanderpinnen behouden.
  if (!writeReg(REG_CONFIG, (uint8_t)(config & ~0x80))) return false;

  // GPS_RST is actief-laag: laag = reset, hoog = vrijgeven.
  if (!writeReg(REG_OUTPUT, (uint8_t)(output & ~0x80))) return false;
  delay(250);
  if (!writeReg(REG_OUTPUT, (uint8_t)(output | 0x80))) return false;
  delay(500);
  Serial.println("GPS reset: TCA9554 P7 resetpuls gegeven en vrijgegeven");
  return true;
}

// Nederlandse vaste flitsers uit de originele Buddy-database (544 locaties).
// Kolommen: latitude, longitude, maximumsnelheid (0 = onbekend), type.
struct BuddyCamera { float lat, lon; uint16_t limit; uint8_t type; };
static const BuddyCamera buddyCameras[] PROGMEM = {
  {50.81162f,5.91391f,0,0},
  {50.81162f,5.91527f,0,0},
  {50.83797f,5.71808f,0,0},
  {50.83869f,5.71823f,0,0},
  {50.84112f,5.71653f,0,0},
  {50.84193f,5.71694f,0,0},
  {50.84290f,5.73438f,50,0},
  {50.84399f,5.72293f,50,0},
  {50.85108f,5.66866f,50,0},
  {50.85329f,6.00413f,0,0},
  {50.85343f,6.02102f,50,0},
  {50.85823f,5.71269f,0,0},
  {50.86053f,5.71554f,0,0},
  {50.86079f,5.71633f,0,0},
  {50.86339f,5.98908f,100,0},
  {50.86739f,5.71568f,50,0},
  {50.87460f,5.67269f,70,0},
  {50.88259f,5.67316f,50,0},
  {50.88952f,5.96173f,100,0},
  {50.94353f,5.78851f,50,0},
  {50.96240f,5.83866f,50,0},
  {50.99098f,5.86914f,50,0},
  {50.99165f,5.80600f,80,0},
  {50.99201f,5.82544f,80,0},
  {50.99198f,5.80753f,80,0},
  {51.01386f,5.85015f,80,0},
  {51.18590f,5.86203f,80,0},
  {51.22648f,5.85809f,80,0},
  {51.23795f,6.04457f,50,0},
  {51.23851f,6.04520f,50,0},
  {51.24144f,5.76342f,80,0},
  {51.24952f,5.97512f,80,0},
  {51.26778f,5.71159f,70,0},
  {51.26832f,5.71309f,70,0},
  {51.29294f,3.82950f,0,0},
  {51.29541f,5.81379f,80,0},
  {51.30612f,5.84545f,80,0},
  {51.31015f,3.45727f,80,0},
  {51.31049f,5.85839f,80,0},
  {51.31139f,6.06937f,80,0},
  {51.31568f,6.07407f,0,0},
  {51.31646f,5.36263f,0,0},
  {51.33184f,3.80088f,0,0},
  {51.35348f,6.11571f,80,0},
  {51.37607f,6.20057f,50,0},
  {51.37946f,6.13703f,70,0},
  {51.37970f,6.13761f,70,0},
  {51.38366f,6.19452f,50,0},
  {51.38483f,6.19563f,50,0},
  {51.38833f,6.19899f,60,0},
  {51.40148f,5.48000f,0,0},
  {51.40227f,5.47978f,0,0},
  {51.42055f,5.53773f,0,0},
  {51.42380f,5.49309f,50,0},
  {51.42457f,5.49332f,70,0},
  {51.42972f,5.57335f,0,0},
  {51.43182f,5.50267f,0,0},
  {51.45165f,5.46364f,50,0},
  {51.45217f,5.48468f,50,1},
  {51.45252f,5.46384f,50,1},
  {51.45282f,5.48581f,0,0},
  {51.45403f,3.58668f,50,0},
  {51.46172f,5.45454f,50,1},
  {51.46342f,3.59243f,70,0},
  {51.46566f,5.58602f,0,0},
  {51.46590f,5.58727f,0,0},
  {51.47130f,5.60890f,0,0},
  {51.47132f,5.24582f,50,0},
  {51.47165f,5.70503f,0,0},
  {51.47185f,5.70658f,0,0},
  {51.47190f,5.61015f,0,0},
  {51.47266f,5.65318f,50,0},
  {51.47272f,5.65437f,0,0},
  {51.47429f,5.63252f,50,0},
  {51.47441f,3.59842f,80,0},
  {51.47496f,5.63383f,0,0},
  {51.47530f,5.04928f,0,0},
  {51.47744f,5.66000f,50,1},
  {51.47741f,5.66086f,50,0},
  {51.47773f,5.65759f,50,1},
  {51.48313f,5.43304f,70,1},
  {51.48353f,3.62543f,50,0},
  {51.48667f,5.61722f,50,0},
  {51.48795f,6.08223f,80,0},
  {51.49380f,5.71794f,80,0},
  {51.49652f,3.52023f,0,0},
  {51.50488f,3.87133f,70,0},
  {51.50603f,3.87074f,70,0},
  {51.51105f,5.94730f,80,0},
  {51.51139f,4.27709f,70,0},
  {51.53001f,3.86840f,80,0},
  {51.53817f,4.19007f,80,0},
  {51.55256f,6.10728f,80,0},
  {51.55316f,5.10799f,50,0},
  {51.56175f,4.21020f,0,0},
  {51.57654f,5.74965f,0,0},
  {51.58178f,5.78649f,80,0},
  {51.58567f,5.15931f,80,0},
  {51.58737f,3.85780f,0,0},
  {51.59545f,5.17541f,80,0},
  {51.60132f,5.67554f,0,0},
  {51.60138f,4.77764f,0,0},
  {51.60477f,5.19050f,80,0},
  {51.64482f,3.93939f,0,0},
  {51.65855f,4.06697f,80,0},
  {51.67694f,5.80985f,50,0},
  {51.68682f,5.27363f,0,0},
  {51.68690f,5.27183f,0,0},
  {51.69145f,5.86386f,50,0},
  {51.69442f,5.32195f,50,0},
  {51.69443f,5.32103f,50,0},
  {51.69846f,4.20285f,0,0},
  {51.69856f,4.20153f,0,0},
  {51.70132f,5.31977f,50,0},
  {51.70196f,3.78805f,80,0},
  {51.70661f,5.30218f,50,0},
  {51.70751f,5.30167f,50,0},
  {51.70806f,5.31953f,50,0},
  {51.70869f,5.31991f,50,0},
  {51.74434f,4.44071f,50,0},
  {51.76541f,4.44378f,50,0},
  {51.77587f,4.43892f,50,0},
  {51.79488f,4.64850f,50,0},
  {51.79535f,4.65653f,0,0},
  {51.79628f,4.66358f,50,0},
  {51.79704f,4.66669f,0,0},
  {51.79749f,4.65395f,0,0},
  {51.79874f,4.70801f,50,0},
  {51.79906f,4.65204f,50,0},
  {51.80454f,4.51988f,50,0},
  {51.80481f,4.51825f,50,0},
  {51.80961f,5.80308f,50,0},
  {51.81369f,4.49562f,50,0},
  {51.81391f,4.70754f,80,0},
  {51.81442f,4.49431f,50,0},
  {51.81562f,3.98197f,50,0},
  {51.81572f,3.98374f,50,0},
  {51.81617f,4.70773f,80,0},
  {51.81650f,4.40514f,50,0},
  {51.81663f,4.66660f,0,0},
  {51.81698f,4.70861f,50,0},
  {51.81709f,4.70472f,50,0},
  {51.81716f,5.80495f,50,0},
  {51.81821f,4.48857f,50,0},
  {51.82175f,4.41655f,50,0},
  {51.82290f,4.41704f,50,0},
  {51.82412f,5.85700f,50,0},
  {51.82583f,5.80106f,50,0},
  {51.83229f,4.70757f,80,0},
  {51.83300f,4.70028f,50,0},
  {51.83345f,4.69900f,50,0},
  {51.83498f,5.84065f,50,0},
  {51.83781f,5.84929f,0,0},
  {51.83864f,5.85081f,0,0},
  {51.84725f,4.60141f,50,0},
  {51.84740f,5.82843f,50,0},
  {51.85466f,4.59201f,50,0},
  {51.85626f,4.30264f,50,0},
  {51.85678f,4.57581f,50,0},
  {51.85705f,4.30132f,50,0},
  {51.85815f,4.40946f,0,0},
  {51.85826f,4.40783f,0,0},
  {51.85852f,4.41741f,50,0},
  {51.85876f,4.41890f,50,0},
  {51.85887f,4.26290f,50,0},
  {51.85890f,4.26150f,50,0},
  {51.85890f,4.56688f,50,0},
  {51.85898f,4.29398f,50,0},
  {51.85900f,4.29242f,50,0},
  {51.86004f,4.39473f,0,0},
  {51.86007f,4.39614f,0,0},
  {51.86043f,4.38952f,50,0},
  {51.86066f,4.23962f,50,0},
  {51.86074f,4.38822f,50,0},
  {51.86095f,4.23808f,50,0},
  {51.86116f,4.43105f,50,0},
  {51.86135f,4.38309f,50,0},
  {51.86143f,4.38436f,50,0},
  {51.86202f,4.43186f,50,0},
  {51.86233f,4.37587f,50,0},
  {51.86235f,4.37455f,50,0},
  {51.86610f,5.85154f,50,0},
  {51.86653f,4.22642f,50,0},
  {51.87503f,4.51126f,0,0},
  {51.87531f,4.51048f,0,0},
  {51.88045f,4.50141f,0,0},
  {51.88111f,4.50014f,0,0},
  {51.88569f,4.54812f,0,0},
  {51.89620f,4.54042f,0,0},
  {51.89701f,4.51799f,50,0},
  {51.90081f,4.51056f,0,0},
  {51.90893f,5.10547f,50,0},
  {51.91183f,4.47801f,50,0},
  {51.91253f,4.32750f,0,0},
  {51.91326f,4.32785f,0,0},
  {51.92590f,4.46167f,50,0},
  {51.92666f,4.56692f,0,0},
  {51.92843f,4.43162f,0,0},
  {51.92884f,4.45407f,0,0},
  {51.93094f,5.17199f,80,1},
  {51.93170f,5.17323f,80,1},
  {51.93172f,4.44066f,0,0},
  {51.93491f,4.44919f,0,0},
  {51.93505f,4.43457f,0,0},
  {51.93545f,4.43347f,0,0},
  {51.93788f,6.07195f,50,0},
  {51.93873f,4.46272f,0,0},
  {51.93983f,4.46527f,0,0},
  {51.94002f,4.46753f,0,0},
  {51.94030f,4.57658f,0,0},
  {51.94127f,4.47687f,0,0},
  {51.94230f,4.48436f,0,0},
  {51.94311f,6.08121f,0,0},
  {51.94690f,4.42050f,0,0},
  {51.94825f,5.88505f,50,0},
  {51.94944f,4.45913f,50,0},
  {51.95064f,4.56517f,0,0},
  {51.95094f,4.56411f,0,0},
  {51.95290f,4.56423f,0,0},
  {51.95439f,5.86801f,50,0},
  {51.95491f,5.89272f,50,0},
  {51.95509f,4.56181f,0,0},
  {51.95655f,4.56328f,0,0},
  {51.95896f,4.54822f,50,0},
  {51.95963f,6.02716f,0,0},
  {51.96266f,5.89796f,70,0},
  {51.96268f,5.97667f,50,0},
  {51.96333f,5.89781f,50,0},
  {51.96580f,5.89049f,50,0},
  {51.96600f,5.58001f,60,0},
  {51.96668f,6.31192f,50,0},
  {51.96689f,5.57978f,60,0},
  {51.96767f,5.90499f,50,0},
  {51.96904f,5.90534f,50,0},
  {51.97058f,5.95422f,0,0},
  {51.97774f,5.91461f,50,0},
  {51.97931f,5.96021f,50,0},
  {51.98292f,5.91438f,50,0},
  {51.98426f,5.95517f,0,0},
  {51.98433f,4.33446f,50,0},
  {51.98453f,4.33612f,50,0},
  {51.99176f,4.22276f,0,0},
  {51.99273f,4.34425f,0,0},
  {51.99301f,4.22285f,0,0},
  {51.99322f,4.38976f,0,0},
  {51.99673f,5.87000f,70,0},
  {52.00107f,4.34749f,0,0},
  {52.00356f,4.65326f,0,0},
  {52.00614f,4.96239f,80,0},
  {52.00636f,5.66634f,50,0},
  {52.00954f,4.37945f,0,0},
  {52.01306f,4.19469f,60,0},
  {52.01784f,4.33306f,0,0},
  {52.01892f,4.33134f,0,0},
  {52.01896f,4.53400f,50,0},
  {52.01987f,4.53400f,50,0},
  {52.02079f,5.64922f,0,0},
  {52.02109f,4.60653f,0,0},
  {52.02123f,4.27692f,0,0},
  {52.02196f,4.26832f,50,0},
  {52.02200f,4.35226f,0,0},
  {52.02387f,4.68326f,50,0},
  {52.02447f,4.68230f,50,0},
  {52.02478f,5.66236f,50,0},
  {52.02587f,4.36069f,0,0},
  {52.02698f,5.64711f,50,0},
  {52.02763f,5.06840f,0,0},
  {52.02809f,4.35881f,0,0},
  {52.02827f,4.71251f,0,0},
  {52.02830f,4.71376f,0,0},
  {52.03230f,4.31817f,0,0},
  {52.03269f,4.31056f,0,0},
  {52.03421f,5.10391f,70,0},
  {52.03513f,5.67153f,50,0},
  {52.03514f,5.07744f,70,0},
  {52.03636f,4.27230f,50,0},
  {52.03987f,4.40817f,0,0},
  {52.04316f,4.51272f,0,0},
  {52.04388f,5.09255f,0,0},
  {52.04522f,4.50670f,0,0},
  {52.04616f,5.66246f,50,0},
  {52.04845f,5.63811f,50,0},
  {52.04846f,4.47252f,0,0},
  {52.04896f,5.63936f,50,0},
  {52.05042f,4.44423f,0,0},
  {52.05283f,4.35446f,0,0},
  {52.05298f,4.40877f,0,0},
  {52.05431f,5.13503f,0,0},
  {52.05479f,5.13446f,0,0},
  {52.05520f,5.13215f,0,0},
  {52.05817f,5.11343f,0,0},
  {52.05897f,5.11218f,0,0},
  {52.06123f,5.08893f,0,0},
  {52.06137f,5.09312f,0,0},
  {52.06141f,4.40621f,0,0},
  {52.06161f,5.08802f,0,0},
  {52.06243f,4.40583f,0,0},
  {52.06377f,4.36912f,0,0},
  {52.06582f,4.40242f,0,0},
  {52.06617f,4.75170f,0,0},
  {52.06644f,4.40120f,0,0},
  {52.06737f,4.35889f,0,0},
  {52.06846f,4.48547f,0,0},
  {52.07015f,4.35972f,0,0},
  {52.07143f,4.39826f,0,0},
  {52.07353f,4.34156f,0,0},
  {52.07385f,5.07438f,50,0},
  {52.07612f,5.09499f,0,0},
  {52.07658f,4.90135f,0,0},
  {52.07757f,4.66267f,0,0},
  {52.07828f,4.32642f,50,0},
  {52.07882f,4.81279f,50,0},
  {52.07889f,5.09161f,0,0},
  {52.08198f,4.41024f,0,0},
  {52.08692f,5.10027f,50,0},
  {52.08776f,5.10041f,50,0},
  {52.08916f,6.13582f,0,0},
  {52.09069f,6.09553f,0,0},
  {52.09170f,4.30726f,0,0},
  {52.09297f,4.30988f,50,0},
  {52.09395f,4.33477f,0,0},
  {52.09446f,4.33559f,0,0},
  {52.09553f,5.21747f,0,0},
  {52.10104f,4.32056f,50,0},
  {52.10366f,4.31785f,50,0},
  {52.10485f,5.09183f,0,0},
  {52.10562f,5.09255f,50,0},
  {52.10565f,5.12154f,50,0},
  {52.10585f,5.11976f,50,0},
  {52.10766f,4.45673f,0,0},
  {52.10757f,5.23591f,0,0},
  {52.10853f,4.30917f,0,0},
  {52.10994f,4.64967f,100,0},
  {52.11044f,4.64813f,100,0},
  {52.11432f,5.26004f,0,0},
  {52.12048f,4.63069f,100,0},
  {52.12096f,4.67694f,0,0},
  {52.12225f,4.58276f,0,0},
  {52.12230f,4.57699f,0,0},
  {52.12352f,4.62485f,70,0},
  {52.12362f,5.75932f,50,0},
  {52.12527f,4.44309f,50,0},
  {52.12610f,5.01805f,0,0},
  {52.12810f,5.01564f,0,0},
  {52.12952f,4.39449f,0,0},
  {52.13192f,4.53426f,0,0},
  {52.13428f,4.46384f,0,0},
  {52.13442f,4.52617f,70,0},
  {52.13588f,5.74959f,50,0},
  {52.14065f,4.65682f,0,0},
  {52.14168f,4.47741f,0,0},
  {52.14209f,4.47670f,0,0},
  {52.14534f,4.65834f,0,0},
  {52.14648f,4.65835f,0,0},
  {52.14688f,4.47527f,0,0},
  {52.14798f,4.47491f,0,0},
  {52.14896f,4.39036f,0,0},
  {52.14949f,4.47442f,0,0},
  {52.15007f,4.47403f,0,0},
  {52.15246f,4.65946f,0,0},
  {52.15332f,4.65972f,0,0},
  {52.15411f,5.22676f,80,0},
  {52.15489f,5.39539f,0,0},
  {52.15741f,4.46724f,0,0},
  {52.15777f,5.40499f,50,0},
  {52.15808f,5.40644f,50,0},
  {52.15922f,5.30749f,50,0},
  {52.16237f,5.37132f,50,0},
  {52.16272f,5.36981f,50,0},
  {52.16512f,5.39914f,50,0},
  {52.16563f,5.39789f,50,0},
  {52.16843f,4.51018f,50,0},
  {52.16887f,4.51133f,50,0},
  {52.16914f,4.98678f,0,0},
  {52.16943f,4.98760f,0,0},
  {52.17522f,5.24863f,80,0},
  {52.17984f,4.98608f,0,0},
  {52.18021f,4.98679f,0,0},
  {52.18099f,5.36974f,80,1},
  {52.18216f,5.36946f,80,1},
  {52.18424f,5.41207f,70,0},
  {52.18683f,4.41827f,50,0},
  {52.18742f,4.41802f,50,0},
  {52.18952f,5.28291f,50,0},
  {52.19056f,4.47368f,50,0},
  {52.19071f,4.42285f,50,0},
  {52.19158f,5.29483f,50,0},
  {52.19402f,5.37954f,70,0},
  {52.19455f,5.38108f,70,0},
  {52.19598f,6.05985f,0,0},
  {52.19780f,4.41959f,0,0},
  {52.20592f,6.88999f,50,0},
  {52.20633f,6.88040f,50,0},
  {52.20665f,6.89558f,50,0},
  {52.20716f,5.95088f,50,0},
  {52.21095f,5.19555f,50,0},
  {52.21592f,4.88234f,0,0},
  {52.21759f,6.94002f,0,0},
  {52.21837f,6.87348f,0,0},
  {52.21893f,4.98633f,0,0},
  {52.22126f,4.98687f,0,0},
  {52.22185f,5.27443f,50,0},
  {52.22340f,4.44218f,0,0},
  {52.22473f,4.67704f,50,0},
  {52.22488f,5.01338f,0,0},
  {52.22566f,4.67576f,50,0},
  {52.22749f,5.02633f,0,0},
  {52.22770f,5.02799f,0,0},
  {52.23078f,6.86911f,50,0},
  {52.23320f,4.97806f,0,0},
  {52.23494f,5.18041f,50,0},
  {52.23568f,5.18695f,50,0},
  {52.23583f,4.43574f,0,0},
  {52.23683f,5.19500f,50,0},
  {52.23917f,5.72953f,80,0},
  {52.23986f,4.45815f,50,0},
  {52.24263f,5.22084f,0,0},
  {52.24767f,5.60839f,50,0},
  {52.25047f,4.44745f,50,0},
  {52.25162f,4.67462f,0,0},
  {52.25273f,4.67558f,0,0},
  {52.25393f,4.71108f,0,0},
  {52.25487f,4.82612f,0,0},
  {52.25499f,5.58962f,0,0},
  {52.25637f,4.56740f,50,0},
  {52.25772f,4.96572f,0,0},
  {52.25896f,4.96601f,0,0},
  {52.26034f,4.79852f,0,0},
  {52.26091f,4.79516f,0,0},
  {52.26184f,4.79484f,0,0},
  {52.26189f,4.72374f,0,0},
  {52.26539f,4.56884f,50,0},
  {52.26708f,5.61423f,50,0},
  {52.26846f,4.69878f,0,0},
  {52.28525f,4.72222f,0,0},
  {52.29014f,4.59575f,0,0},
  {52.32132f,4.66372f,0,0},
  {52.32342f,4.97647f,50,0},
  {52.33058f,4.80824f,30,0},
  {52.33666f,4.94278f,70,0},
  {52.33730f,4.63141f,0,0},
  {52.34143f,4.84152f,0,0},
  {52.34539f,4.84147f,50,0},
  {52.34544f,4.62529f,50,0},
  {52.34667f,4.91197f,30,0},
  {52.34734f,4.84147f,0,0},
  {52.34763f,4.62371f,50,0},
  {52.34881f,4.84134f,0,0},
  {52.35370f,4.84278f,0,0},
  {52.35558f,6.64244f,50,0},
  {52.35565f,4.84236f,0,0},
  {52.35786f,4.90403f,30,0},
  {52.35802f,4.89997f,50,0},
  {52.35983f,6.54780f,80,1},
  {52.35985f,6.54668f,80,1},
  {52.36069f,5.25304f,50,0},
  {52.36122f,5.25228f,50,0},
  {52.36271f,4.84235f,0,0},
  {52.36348f,4.61004f,70,1},
  {52.36351f,4.61183f,70,1},
  {52.36506f,4.70826f,100,0},
  {52.36578f,5.24808f,50,0},
  {52.36636f,5.24711f,50,0},
  {52.36812f,4.62867f,50,0},
  {52.36907f,4.84251f,0,0},
  {52.37014f,4.84193f,0,0},
  {52.37009f,4.62881f,50,0},
  {52.37015f,4.65348f,50,1},
  {52.37075f,4.65427f,50,1},
  {52.37200f,4.96293f,0,0},
  {52.37215f,4.96164f,0,0},
  {52.37294f,4.67246f,30,0},
  {52.37588f,4.84321f,0,0},
  {52.38219f,4.66902f,70,1},
  {52.38380f,4.75379f,50,0},
  {52.38383f,4.75541f,50,0},
  {52.38475f,4.83515f,0,0},
  {52.38809f,4.84443f,0,0},
  {52.39081f,4.66209f,50,1},
  {52.39170f,4.83447f,50,0},
  {52.39258f,5.57042f,0,0},
  {52.39705f,4.62588f,70,1},
  {52.39800f,4.62654f,0,1},
  {52.40198f,5.25951f,80,0},
  {52.40543f,5.59244f,0,0},
  {52.40627f,5.59381f,0,0},
  {52.40639f,4.63403f,70,0},
  {52.40643f,4.93820f,0,0},
  {52.40717f,4.63416f,70,0},
  {52.49161f,6.12894f,70,0},
  {52.49329f,6.11483f,70,0},
  {52.49421f,4.76442f,0,0},
  {52.49424f,4.76606f,0,0},
  {52.49532f,6.13973f,80,0},
  {52.49692f,4.95091f,50,0},
  {52.50039f,4.95946f,50,0},
  {52.50059f,6.09972f,70,0},
  {52.50060f,6.09832f,70,0},
  {52.50368f,6.07349f,70,0},
  {52.50387f,4.93012f,0,0},
  {52.50582f,5.97276f,70,0},
  {52.50674f,5.96638f,70,0},
  {52.51669f,6.07690f,50,0},
  {52.54869f,4.95781f,0,0},
  {52.58096f,6.19101f,0,0},
  {52.61866f,4.73210f,70,0},
  {52.61903f,4.73087f,70,0},
  {52.62487f,5.00510f,0,0},
  {52.64683f,4.73169f,70,0},
  {52.64811f,4.73259f,70,0},
  {52.65119f,6.61832f,50,0},
  {52.65399f,6.65516f,80,0},
  {52.66137f,4.81326f,50,0},
  {52.69905f,4.77244f,0,0},
  {52.73133f,6.49786f,50,0},
  {52.75681f,4.68293f,0,0},
  {52.76125f,6.87476f,50,0},
  {52.76218f,6.87501f,50,0},
  {52.78092f,4.70302f,80,0},
  {52.80452f,6.98295f,0,0},
  {52.80737f,7.01117f,0,0},
  {52.91676f,4.75417f,70,0},
  {52.93134f,6.75961f,80,0},
  {52.99431f,6.53801f,50,0},
  {53.02247f,6.22134f,0,0},
  {53.08975f,6.14178f,0,0},
  {53.12260f,6.09263f,50,0},
  {53.14109f,6.62687f,50,0},
  {53.14623f,6.45658f,80,0},
  {53.14652f,6.45519f,80,0},
  {53.14804f,6.48201f,50,0},
  {53.14896f,6.48471f,50,0},
  {53.16314f,6.37409f,50,0},
  {53.16593f,6.36589f,50,0},
  {53.18733f,6.51138f,80,0},
  {53.21311f,6.58604f,50,0},
  {53.22655f,6.53431f,70,0},
  {53.22730f,6.53365f,0,0},
  {53.24315f,6.65380f,60,0},
  {53.24347f,6.87659f,70,0},
  {53.24567f,6.87628f,70,0},
  {53.27419f,6.53612f,50,0},
  {53.29305f,6.53317f,50,0},
  {53.32778f,6.51337f,50,0},
};
static constexpr size_t BUDDY_CAMERA_COUNT = sizeof(buddyCameras)/sizeof(buddyCameras[0]);


// Waveshare ESP32-S3-Touch-AMOLED-1.75(-G), CO5300, 466x466
Arduino_DataBus *bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_CO5300 *gfx = new Arduino_CO5300(bus, LCD_RESET, 0, LCD_WIDTH, LCD_HEIGHT, 6, 0, 0, 0);
// v15: gewone 16-bit framebuffer, expliciet in PSRAM. De indexed versie van v14 zocht voor ELKE pixel de kleur op
// in een palet van 256 kleuren; dat maakte het tekenen traag en schokkerig.
class PsramCanvas : public Arduino_Canvas {
 public:
  using Arduino_Canvas::Arduino_Canvas;
  bool begin(int32_t speed = GFX_NOT_DEFINED) override {
    if (!_framebuffer) _framebuffer = (uint16_t *)heap_caps_aligned_alloc(16, (size_t)_width * _height * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!_framebuffer) return false;
    return Arduino_Canvas::begin(speed);
  }
};
PsramCanvas *frame = nullptr;            // het beeld waar nu in getekend wordt
PsramCanvas *frames[2] = {nullptr, nullptr};  // v16: twee beelden: core 1 tekent het ene terwijl core 0 het andere verstuurt
static uint8_t frameIdx = 0;
static constexpr int32_t QSPI_HZ = 80000000;  // bij strepen of flikkeren: 40000000

// Alleen het stukje scherm dat echt veranderd is wordt opnieuw verstuurd (de "dirty box").
struct Box { int16_t x0, y0, x1, y1; };
static Box curBox = {32767, 32767, -32768, -32768}, prevBox = {0, 0, 465, 465};
static Box drawnBox[2] = {{0, 0, 465, 465}, {0, 0, 465, 465}};  // wat er in elk beeld getekend staat (om te wissen)
struct FlushJob { uint16_t *fb; Box b; };
static QueueHandle_t flushQueue = nullptr;
static SemaphoreHandle_t flushIdle = nullptr;
static volatile uint32_t flushLastUs = 0;
static volatile uint8_t wilLicht = 0, isLicht = 0;   // v23: helderheid alleen vanuit de verstuur-taak (anders botsen twee kernen op de schermbus)
// v16: GPS-reset via TCA9554 P7 standaard UIT. Het officiele Waveshare GPS-voorbeeld raakt P7 niet aan;
// in v14/v15 kwam er na onze resetpuls niks meer uit de GPS. Zet op true om de oude resetpuls terug te zetten.
static constexpr bool GPS_RESET_PULSE = false;
I2SClass buddyI2S;
static bool audioReady = false;
static volatile bool audioBusy = false;
static volatile uint8_t buddyVolume = 85;
Preferences buddyPrefs;
static bool volumeNeedsSave = false;
static uint32_t lastVolumeChange = 0;
static uint32_t lastRenderUs = 0;
static uint32_t fpsWindowUs = 0;
static uint16_t fpsFrames = 0;
static uint32_t volumeUiUntil = 0;
static uint32_t lastTouchPoll = 0;
TouchDrvCST92xx buddyTouch;
bool touchReady=false;
int16_t touchX[2] = {0}, touchY[2] = {0};
static const uint32_t AUDIO_RATE = 16000;
static const uint32_t FRAME_INTERVAL_US = 33333; // stabiele 30 FPS als startpunt
static uint32_t serialHeartbeatMs = 0;
static uint32_t idleMoodUntilMs = 0;
static uint8_t idleMoodStep = 0;

static const int16_t W=240, H=240;
static const uint16_t BG=0x0000, EYE=0xD7FF, BLUSH=0xFB1F, HEART=0xF98D, RED=0xF986, GOLD=0xFEA0;
static const float PI2=6.2831853f;

enum Mood { NEUTRAL, CONTENT, RELAXED, HAPPY, LOVE, EXCITED, FOCUSED, STARTLED, WORRIED, BORED, SLEEPY, YAWN, SLEEPING, ALERT, BLOW, ANTICIPATE, MOOD_COUNT };
struct Expr { float open,lid,tilt,squint,scale,mouth,mo,blush; };
// Waarden overgenomen uit de originele Buddy P-expressietabel.
const Expr exprs[MOOD_COUNT] = {
 {1,0,0,0,1,.15f,0,0}, {.9f,0,0,.3f,1,.6f,0,.35f}, {.8f,.1f,0,.2f,1,.45f,0,.1f},
 {.95f,0,0,.6f,1,.9f,0,.6f}, {1,0,0,.78f,1.05f,1,0,1}, {1.1f,0,0,0,1.14f,.8f,.25f,.2f},
 {.78f,.28f,.6f,0,1,-.05f,0,0}, {1.25f,0,0,0,.9f,0,1,0}, {.95f,.22f,-.8f,0,1,-.6f,0,0},
 {.6f,.38f,0,0,1,-.1f,0,0}, {.38f,.45f,0,0,1,0,0,0}, {.3f,.2f,0,.5f,1,0,1,0},
 {.05f,0,0,0,1,.1f,0,0}, {1.12f,0,.25f,0,1.1f,-.05f,0,0}, {.85f,.1f,0,.5f,1,0,.55f,.3f}, {1.05f,0,0,0,1.08f,.5f,0,.15f}
};
const char* moodNames[MOOD_COUNT]={"WAKKER","TEVREDEN","ONTSPANNEN","BLIJ","VERLIEFD","OPGEWONDEN","GEFOCUST","GESCHROKKEN","BEZORGD","VERVEELD","SLAPERIG","GAAPT","SLAAPT","ALERT","BLAAST","VERWACHTINGSVOL"};
Expr cur=exprs[NEUTRAL];
float t=0, lastFrame=0, nextBlink=2, blinkStart=-1, blink=1, gx=0,gy=0,tgx=0,tgy=0,nextGaze=0;
float breathe=0, headBob=0;
int mood=NEUTRAL, lastMood=-1;
// v21: toestand van de animaties (gags), net als in de app
static int gagAct = -1, gagMood = -1; static float gagT0 = 0, nextGagT = 6; static const char *gagMsg = nullptr;
static float fdx = 0, fdy = 0, eyeL = 1, eyeR = 1, faceOx = 0, faceOy = 0;
static bool fxNoBlink = false, fxSpiral = false, fxTongue = false;
static float g_fx = 120, g_fy = 90, g_r = 0, g_d = 0, g_nn = 0; static bool g_b = false;
static volatile uint32_t flagTot = 0;   // 0-100 vlag-show
struct Particle { float x,y,vx,vy,age,life,size; uint8_t kind; } particles[24];

int16_t X(float x){return (int16_t)lroundf(x*LCD_WIDTH/240.0f);} 
int16_t Y(float y){return (int16_t)lroundf(y*LCD_HEIGHT/240.0f);} 
int16_t S(float v){return (int16_t)lroundf(v*LCD_WIDTH/240.0f);} 
static inline void boxAdd(float x0,float y0,float x1,float y1){
  int16_t a=X(fminf(x0,x1))-3,b=Y(fminf(y0,y1))-3,c=X(fmaxf(x0,x1))+3,d=Y(fmaxf(y0,y1))+3;
  if(a<curBox.x0)curBox.x0=a; if(b<curBox.y0)curBox.y0=b; if(c>curBox.x1)curBox.x1=c; if(d>curBox.y1)curBox.y1=d;
}
void line(float x1,float y1,float x2,float y2,uint16_t c,uint8_t thick=2){
  boxAdd(x1,y1,x2,y2+thick);
  // dikte in echte pixels (scherm is ~1,94x zo groot als de 240-tekenruimte)
  int16_t n=(int16_t)lroundf(thick*LCD_WIDTH/240.0f); int16_t ax=X(x1),ay=Y(y1),bx=X(x2),by=Y(y2);
  for(int i=0;i<n;i++) frame->drawLine(ax,ay+i,bx,by+i,c);
}
void circle(float x,float y,float r,uint16_t c){boxAdd(x-r,y-r,x+r,y+r);frame->fillCircle(X(x),Y(y),S(r),c);} 
void roundRect(float x,float y,float w,float h,float r,uint16_t c){boxAdd(x,y,x+w,y+h);frame->fillRoundRect(X(x),Y(y),S(w),S(h),S(r),c);} 
void heart(float x,float y,float s,uint16_t c){
  boxAdd(x-s*.8f,y-s*.6f,x+s*.8f,y+s*.95f);
  circle(x-s*.34f,y-s*.12f,s*.38f,c); circle(x+s*.34f,y-s*.12f,s*.38f,c);
  // V-shaped lower point; deliberately chunky at this display scale.
  int16_t ax=X(x-s*.68f), ay=Y(y-s*.05f), bx=X(x+s*.68f), by=Y(y-s*.05f), px=X(x), py=Y(y+s*.82f);
  for(int k=-2;k<=2;k++){frame->drawLine(ax,Y(y-s*.05f+k),px,Y(y+s*.82f+k),c);frame->drawLine(bx,Y(y-s*.05f+k),px,Y(y+s*.82f+k),c);}
}
void star(float x,float y,float r,uint16_t c){
  for(int i=0;i<4;i++){float a=i*PI2/4;line(x-cosf(a)*r*.25f,y-sinf(a)*r*.25f,x+cosf(a)*r,y+sinf(a)*r,c,3);}
}
void addParticle(uint8_t kind){
 for(auto &p:particles) if(p.life<=0){p={120+random(-75,76),82,(float)random(-12,13),(float)-random(20,42),0,kind==0?1.6f:kind==1?.9f:2.4f,(float)random(5,10),kind};return;}
}
void gazeTarget(){
 if(mood==STARTLED||mood==FOCUSED||mood==EXCITED||mood==LOVE||mood==HAPPY||mood==ALERT){tgx=0;tgy=-.05f;nextGaze=t+.6f;}
 else if(mood==BORED){tgx=(random(2)?1:-1)*.8f;tgy=random(-50,51)/100.f;nextGaze=t+random(8,19)/10.f;}
 else if(mood==ANTICIPATE){tgx=(random(2)?1:-1)*.9f;tgy=-.6f;nextGaze=t+.7f;}
 else if(mood==SLEEPY||mood==SLEEPING||mood==YAWN){tgx=random(-20,21)/100.f;tgy=.5f;nextGaze=t+2;}
 else {tgx=random(-30,31)/100.f;tgy=-.1f;nextGaze=t+2;}
}
void eye(float cx,float cy,int side){
 float w=44*cur.scale, h=fmaxf(3,62*cur.open*cur.scale*blink*(side<0?eyeL:eyeR));
 boxAdd(cx-w*.9f-4,cy-h/2-4,cx+w*.9f+4,cy+h*1.65f);   // v28: breed genoeg voor de knijp-ellips (gaf 'dode pixels')
 // Solid cyan eye, rounded exactly like the original Buddy canvas design.
 roundRect(cx-w/2,cy-h/2,w,h,fminf(w,h)*.42f,EYE);
 if(cur.lid>.01f || fabsf(cur.tilt)>.01f){
   float cov=cur.lid*h, tilt=cur.tilt*h*.38f;
   float yl=cy-h/2+cov+(side==-1?-tilt:tilt), yr=cy-h/2+cov+(side==-1?tilt:-tilt);
   // Dark upper lid, slanted for worried/focused expressions.
   int16_t x1=X(cx-w/2-3),x2=X(cx+w/2+3), ytop=Y(cy-h/2-3);
   frame->fillTriangle(x1,ytop,x2,ytop,x2,Y(yr),BG);
   frame->fillTriangle(x1,ytop,x2,Y(yr),x1,Y(yl),BG);
 }
 if(cur.squint>.01f){
   float yy=cy+h*(1.05f-cur.squint*.7f);
   frame->fillEllipse(X(cx),Y(yy),S(w*.85f),S(h*.55f),BG);
 }
}
void mouth(float oy){
 float cx=120, cy=170+oy*.5f;
 if((mood==CONTENT||mood==LOVE)&&cur.mo<.2f){
   // two tiny smile dimples
   for(int j=0;j<2;j++){float c=cx+(j?8:-8); for(int i=0;i<18;i++){float a=3.14159f+i*3.14159f/17; circle(c+cosf(a)*6,cy-2+sinf(a)*6,1.8f,EYE);}}
   return;
 }
 if(cur.mo>.05f) boxAdd(cx-12,cy-12,cx+12,cy+16);
 if(cur.mo>.05f) frame->fillEllipse(X(cx),Y(cy+2),S(6+5*cur.mo),S(3+10*cur.mo),EYE);
 float hw=12+8*fabsf(cur.mouth), prevx=cx-hw, prevy=cy;
 for(int i=1;i<=16;i++){float q=-1+2*i/16.f;float xx=cx+q*hw, yy=cy+cur.mouth*18*(1-q*q);line(prevx,prevy,xx,yy,EYE,2);prevx=xx;prevy=yy;}
}
// v21: 0-80 / 0-100 meten met de GPS, net als launchFix() in de app (tijdstip van 80/100 tussen twee metingen in)
static char gpsDate[7] = "";
static volatile float res80 = 0, res100 = 0; static volatile bool new80 = false, new100 = false;
static void launchFix(uint32_t ms, float v) {
  static bool arm = false, d80 = false, fijn = false; static uint32_t tStil = 0, t0 = 0, tPrev = 0, t1 = 0; static float vPrev = 0, v1 = 0;
  if (v < 1.0f) { arm = true; tStil = ms; t0 = 0; d80 = false; }
  else if (arm && t0 == 0) {
    float f = (vPrev < 1 && v > vPrev) ? (1 - vPrev) / (v - vPrev) : 0; f = fmaxf(0, fminf(1, f));
    t0 = tStil + (uint32_t)((ms - tStil) * f); arm = false; t1 = ms; v1 = v; fijn = false;
  } else if (t0 && !fijn && ms > t1) {   // tweede meting: terugrekenen wanneer je echt wegreed
    fijn = true; float helling = (v - v1) / (float)(ms - t1);
    if (helling > 0) { float st = t1 - v1 / helling; if (st < tStil) st = tStil; if (st < t1) t0 = (uint32_t)st; }
  }
  if (t0) {
    auto kruis = [&](float doel) -> float { return v > vPrev ? tPrev + (ms - tPrev) * (doel - vPrev) / (v - vPrev) : (float)ms; };
    if (!d80 && v >= 80 && vPrev < 80) { d80 = true; float tt = (kruis(80) - t0) / 1000.f; if (tt > 2.5f && tt < 9) { res80 = tt; new80 = true; } }
    if (v >= 100 && vPrev < 100) { float tt = (kruis(100) - t0) / 1000.f; t0 = 0; if (tt > 3.5f && tt < 10) { res100 = tt; new100 = true; } }
    else if (ms - t0 > 15000) t0 = 0;
  }
  vPrev = v; tPrev = ms;
}
static volatile float gpsKoers = 0;
static volatile uint32_t nmeaOk = 0, nmeaBad = 0; static char lastRmc[72] = "";
static volatile int gpsHH = -1, gpsMM = 0, gpsInZicht = 0; static volatile uint32_t gpsTijdMs = 0; static int inZicht[5] = {0};
// Parse NMEA coordinate (ddmm.mmmm / dddmm.mmmm) to decimal degrees.
float nmeaCoord(const char* v, const char* hemi) {
  if (!v || !*v) return 0; float raw=atof(v); int deg=(int)(raw/100.0f); float out=deg+(raw-deg*100)/60.0f;
  if (hemi && (*hemi=='S'||*hemi=='W')) out=-out; return out;
}
void parseNmeaLine(char* line) {
  if (!line || line[0]!='$') return;
  // v15: checksum controleren, anders kan een half gelezen regel een rare snelheid of plek geven
  char *ster = strrchr(line, '*');
  if (ster && strlen(ster) >= 3) { uint8_t cs = 0; for (char *q = line + 1; q < ster; ++q) cs ^= (uint8_t)*q; if (cs != (uint8_t)strtol(ster + 1, nullptr, 16)) { nmeaBad++; return; } *ster = 0; }
  nmeaOk++; if (strstr(line, "RMC")) { strncpy(lastRmc, line, sizeof lastRmc - 1); lastRmc[sizeof lastRmc - 1] = 0; }
  char *f[20]={0}; int n=0; f[n++]=line;
  for(char *p=line; *p && n<20; ++p) if(*p==','){*p=0;f[n++]=p+1;}
  if (strstr(f[0],"RMC") && n>7 && f[2][0]=='A') {
    float lat=nmeaCoord(f[3],f[4]), lon=nmeaCoord(f[5],f[6]);
    float knots=atof(f[7]); if (n>8 && f[8][0]) gpsKoers=atof(f[8]); gpsLat=lat; gpsLon=lon; gpsSpeedKmh=knots*1.852f; gpsFix=true; gpsLastFixMs=millis(); gpsNewFix=true;
    if (n>9 && strlen(f[9])==6) { memcpy(gpsDate, f[9], 6); gpsDate[6]=0; }
    if (strlen(f[1]) >= 6) { gpsHH = (f[1][0]-'0')*10 + (f[1][1]-'0'); gpsMM = (f[1][2]-'0')*10 + (f[1][3]-'0'); gpsTijdMs = millis(); }
    launchFix(millis(), gpsSpeedKmh);
  } else if (strstr(f[0], "RMC") && n > 1 && strlen(f[1]) >= 6) {   // ook zonder fix weet hij al hoe laat het is
    gpsHH = (f[1][0]-'0')*10 + (f[1][1]-'0'); gpsMM = (f[1][2]-'0')*10 + (f[1][3]-'0'); gpsTijdMs = millis();
    if (n>9 && strlen(f[9])==6) { memcpy(gpsDate, f[9], 6); gpsDate[6]=0; }
  } else if (strstr(f[0], "GSV") && n > 3) {   // satellieten in zicht (per systeem: GPS, Galileo, BeiDou, Glonass)
    int k = f[0][2] == 'P' ? 0 : f[0][2] == 'A' ? 1 : f[0][2] == 'B' ? 2 : f[0][2] == 'L' ? 3 : 4; if (k < 5) inZicht[k] = atoi(f[3]);
    gpsInZicht = inZicht[0] + inZicht[1] + inZicht[2] + inZicht[3] + inZicht[4];
  } else if(strstr(f[0],"GGA") && n>8) {
    int q=atoi(f[6]); gpsSatellites=atoi(f[7]); if(q>0 && f[2][0]) {gpsLat=nmeaCoord(f[2],f[3]);gpsLon=nmeaCoord(f[4],f[5]);gpsFix=true;gpsLastFixMs=millis();} else if(q==0) gpsFix=false;
  }
}
float cameraDistanceM(float a1,float o1,float a2,float o2) {
  const float rad=0.01745329252f; float da=(a2-a1)*rad,dl=(o2-o1)*rad;
  float h=sinf(da*.5f)*sinf(da*.5f)+cosf(a1*rad)*cosf(a2*rad)*sinf(dl*.5f)*sinf(dl*.5f);
  return 12742000.0f*asinf(sqrtf(fminf(1.0f,h)));
}
// v25: actuele lijst vaste flitsers (heel NL, OpenStreetMap) via wifi; de ingebouwde lijst is reserve
struct NetCam { float lat, lon; int16_t lim; uint8_t type; };
static NetCam *netCams = nullptr; static volatile int netCamN = 0;
static NetCam *extraCams = nullptr; static volatile int extraCamN = 0;   // v28: mobiele controles + flitsers van de groep (Worker)
void checkBuddyCameras() {
  if(!gpsFix) return; int best=99999, lim=0; float bLat = 0, bLon = 0;
  int nn = netCamN; NetCam *nc = netCams;
  if (nn > 0 && nc) {
    for (int i = 0; i < nn; i++) { if (fabsf(nc[i].lat-gpsLat)>.012f || fabsf(nc[i].lon-gpsLon)>.018f) continue;
      int dist=(int)cameraDistanceM(gpsLat,gpsLon,nc[i].lat,nc[i].lon); if(dist<best){best=dist;lim=nc[i].lim;bLat=nc[i].lat;bLon=nc[i].lon;} }
  }
  { int en = extraCamN; NetCam *ec = extraCams;
    if (en > 0 && ec) for (int i = 0; i < en; i++) { if (fabsf(ec[i].lat-gpsLat)>.012f || fabsf(ec[i].lon-gpsLon)>.018f) continue;
      int dist=(int)cameraDistanceM(gpsLat,gpsLon,ec[i].lat,ec[i].lon); if(dist<best){best=dist;lim=ec[i].lim;bLat=ec[i].lat;bLon=ec[i].lon;} } }
  if (nn > 0 && nc) {} else for(size_t i=0;i<BUDDY_CAMERA_COUNT;i++){ BuddyCamera c; memcpy_P(&c,&buddyCameras[i],sizeof(c));
    if(fabsf(c.lat-gpsLat)>.012f || fabsf(c.lon-gpsLon)>.018f) continue;
    int dist=(int)cameraDistanceM(gpsLat,gpsLon,c.lat,c.lon); if(dist<best){best=dist;lim=c.limit;bLat=c.lat;bLon=c.lon;}
  }
  // v25: voorbij = voorbij. Loopt de afstand op (je rijdt ervan weg), dan niet meer melden tot je ver weg bent.
  static float vLat = 0, vLon = 0; static int vMin = 99999, vOmhoog = 0; static bool voorbij = false;
  if (best < 99999 && (fabsf(bLat - vLat) > 1e-5f || fabsf(bLon - vLon) > 1e-5f)) { vLat = bLat; vLon = bLon; vMin = best; vOmhoog = 0; voorbij = false; }
  if (best < vMin) { vMin = best; vOmhoog = 0; } else if (best > vMin + 15) { if (++vOmhoog >= 2) voorbij = true; }
  if (voorbij && best > 1500) voorbij = false;
  if (voorbij) best = 99999;
  gpsNearestCameraM=best==99999?-1:best; gpsCameraLimit=lim;
  bool near=(best<=350); if(near && !gpsCameraAlert && millis()-gpsLastCameraAlert>25000){
    gpsCameraAlert=true; gpsLastCameraAlert=millis();  // v15: de mood zet updateMood() zelf (niet vanaf de GPS-core)
    Serial.printf("BUDDY CAMERA: %d meter | limiet %d km/u\n",best,lim);
  } else if(!near) gpsCameraAlert=false;
}
// UART-NMEA parser. Geen I2C-driver voor GPS: de LC76G gebruikt op de 31264-G
// ook de rechtstreeks gerouteerde UART-pinnen 17/18.
static char gpsLine[160];
static size_t gpsLinePos = 0;
static uint32_t gpsLastReportMs = 0;
static char gpsLastStage[24] = "UART WACHT";

static void gpsByte(int value) {
  gpsNmeaBytes++;
  gpsLastDataMs = millis();
  if (value == '\r' || value == 0) return;
  if (value == '\n') {
    gpsLine[gpsLinePos] = 0;
    if (gpsLinePos > 6 && gpsLine[0] == '$') parseNmeaLine(gpsLine);
    gpsLinePos = 0;
  } else if (value == '$') {
    gpsLine[0] = '$'; gpsLinePos = 1;  // nieuwe zin begint altijd opnieuw
  } else if (gpsLinePos < sizeof(gpsLine) - 1) {
    gpsLine[gpsLinePos++] = (char)value;
  } else {
    gpsLinePos = 0; // te lange/onvolledige regel veilig weggooien
  }
}

// Officieel LC76G I2C-protocol (Waveshare 09_LC76G_I2C): vraag eerst hoeveel bytes klaarstaan, lees ze daarna.
static uint8_t gpsI2cBuf[1024];
static volatile uint8_t gpsI2cStap = 0;
static volatile int gpsI2cN = 0;        // hoeveel bytes de GPS wel teruggaf bij de mislukte stap   // bij welke stap het misging (voor de Serial Monitor)
// v18: volgens de werkende driver van ESP32Marauder (zelfde Waveshare-bord). Belangrijk:
//  - de HELE transactie in een keer, zonder dat touch/audio er tussendoor op de bus mag (anders raakt de
//    LC76G in de war en blijft 0x54 voorgoed weigeren tot de stroom er lang af is geweest);
//  - korte pauzes (3 ms) tussen de stappen, eerst een "por" naar 0x50, en af en toe de wek-reeks.
static bool lcTouch(uint8_t a) { Wire.beginTransmission(a); Wire.write((uint8_t)0); return Wire.endTransmission() == 0; }
static uint8_t lcWriteCr(uint32_t reg, uint32_t len) {
  uint8_t b[8] = {(uint8_t)reg, (uint8_t)(reg >> 8), (uint8_t)(reg >> 16), (uint8_t)(reg >> 24),
                  (uint8_t)len, (uint8_t)(len >> 8), (uint8_t)(len >> 16), (uint8_t)(len >> 24)};
  Wire.beginTransmission(0x50); Wire.write(b, 8); return Wire.endTransmission();
}
static uint32_t lcRead32(uint32_t reg) {
  if (lcWriteCr(reg, 4) != 0) { gpsI2cStap = 1; return 0xFFFFFFFFUL; }
  delay(3);
  uint8_t b[4]; int n = Wire.requestFrom((int)0x54, 4); gpsI2cN = n;
  for (int i = 0; i < n && i < 4; i++) b[i] = Wire.read();
  if (n != 4) { gpsI2cStap = 2; return 0xFFFFFFFFUL; }
  return b[0] | (b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}
static void lcWake() {
  if (!lcTouch(0x50)) return; delay(3);
  uint32_t vrij = lcRead32(0xAA510004UL); if (vrij == 0xFFFFFFFFUL || vrij < 1) return; delay(3);
  if (lcWriteCr(0xAA531000UL, 1) != 0) return; delay(3);
  lcTouch(0x58);
}
static int lc76gReadI2C() {
  static uint8_t leeg = 0;
  if (!i2cLock(200)) return -1;            // bus vasthouden tot de transactie klaar is
  uint16_t oudTo = Wire.getTimeOut(); Wire.setTimeOut(300);
  int res = -1;
  do {
    if (!lcTouch(0x50)) { lcTouch(0x54); lcTouch(0x58); gpsI2cStap = 1; break; }  // por + herstel-duwtje
    delay(3);
    uint32_t len = lcRead32(0xAA510008UL);
    if (len == 0xFFFFFFFFUL) break;
    if (len >= 35 * 1024) { gpsI2cStap = 3; break; }
    if (len == 0) { res = 0; if (++leeg >= 8) { leeg = 0; lcWake(); } break; }
    leeg = 0;
    if (len > sizeof(gpsI2cBuf)) len = sizeof(gpsI2cBuf);   // de rest komt de volgende ronde
    delay(3);
    if (lcWriteCr(0xAA512000UL, len) != 0) { gpsI2cStap = 4; break; }
    delay(3);
    int n = Wire.requestFrom((int)0x54, (int)len), got = 0;
    while (Wire.available() && got < (int)len) gpsI2cBuf[got++] = Wire.read();
    if (got != (int)len) { gpsI2cStap = 5; gpsI2cN = got; break; }
    res = got;
  } while (0);
  Wire.setTimeOut(oudTo);
  i2cUnlock();
  if (res > 0) for (int i = 0; i < res; i++) gpsByte(gpsI2cBuf[i]);
  return res;
}

void gpsTask(void*) {
  uint32_t lastI2c = 0;
  for (;;) {
    bool received = false;
    while (gpsSerial.available() > 0) {   // reserve: als de UART toch doorverbonden is
      int value = gpsSerial.read();
      if (value < 0) break;
      received = true; gpsByte(value);
    }
    if (received) { gpsDataOk = true; strcpy(gpsLastStage, "UART NMEA DATA"); }
    if (millis() - lastI2c >= 500) {
      lastI2c = millis();
      int got = lc76gReadI2C();
      if (got > 0) { gpsDataOk = true; gpsI2cOk++; strcpy(gpsLastStage, "I2C NMEA DATA"); }
      else if (got < 0) gpsI2cFail++;
    }
    if (gpsFix && millis() - gpsLastFixMs > 3000) gpsFix = false;
    if (gpsNewFix) { gpsNewFix = false; checkBuddyCameras(); }  // alleen bij een nieuwe positie, niet 50x per seconde
    if (millis() - gpsLastReportMs >= 3000) {
      gpsLastReportMs = millis();
      if (!gpsDataOk || millis() - gpsLastDataMs > 6000) strcpy(gpsLastStage, "GEEN DATA");
      Serial.printf("GPS %s | i2c ok=%lu fout=%lu (stap %d, n=%d) | baud=%lu | fix=%s | sats=%d | %.1f km/u | %.6f, %.6f | bytes=%lu | flitser=%d m\n",
        (gpsDataOk && millis() - gpsLastDataMs < 6000) ? gpsLastStage : "GEEN DATA",
        (unsigned long)gpsI2cOk, (unsigned long)gpsI2cFail, (int)gpsI2cStap, (int)gpsI2cN, (unsigned long)GPS_UART_BAUD, gpsFix ? "JA" : "NEE", (int)gpsSatellites,
        (float)gpsSpeedKmh, (float)gpsLat, (float)gpsLon,
        (unsigned long)gpsNmeaBytes, (int)gpsNearestCameraM);
      Serial.printf("   NMEA goed=%lu fout=%lu | satellieten in zicht=%d | laatste RMC: %s\n", (unsigned long)nmeaOk, (unsigned long)nmeaBad, (int)gpsInZicht, lastRmc);
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// ===================== v19: koppeling met de telefoon (Bluetooth) =====================
// De Buddy-app stuurt elke seconde: snelheid, limiet, flitser, file, werk, zijn stemming en wat hij zegt.
// Het schermpje kan terugsturen: "MELD:cam" (flitser melden) en "MELD:mob" (mobiele controle).
#define NUS_SVC "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_RX  "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_TX  "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
struct WerkerCfg { char url[96]; char grp[33]; char van[25]; char naam[21]; };
static WerkerCfg wCfg = {"", "", "", ""}, wNieuw = {"", "", "", ""}; static volatile bool netNieuw = false, zwaaiVraag = false;
static char grpTekst[40] = ""; static volatile uint32_t grpTot = 0;
static volatile int setLicht = -1, setVol = -1; static volatile float animFactor = 1.0f; static char outfitK[16] = ""; static volatile uint32_t outfitT = 0;
static char wifiNaam[33] = "", wifiPass[65] = "", wifiNaamNieuw[33] = "", wifiPassNieuw[65] = ""; static volatile bool wifiNieuw = false, wifiHerstart = false;
struct PhoneData { int v, lim, camD, camLim, fileD, fileMin; bool werk; int mood; char txt[64]; uint32_t ms; char nav; int navD; char msi[6]; };
static PhoneData phone = {0, 0, -1, 0, -1, 0, false, -1, "", 0, 0, 0, ""};
static BLECharacteristic *bleTx = nullptr;
static volatile bool bleConnected = false;
static char bleBuf[256]; static size_t bleLen = 0;
static const char *moodKeys[MOOD_COUNT] = {"neutral","content","relaxed","happy","love","excited","focused","startled","worried","bored","sleepy","yawn","sleeping","alert","blow","anticipate"};
static bool phoneFresh() { return phone.ms && millis() - phone.ms < 3500; }
static void parsePhone(char *line) {
  if (strcmp(line, "LOG?") == 0) { logVraag = true; return; }
  if (!strncmp(line, "NET:", 4)) {   // v28: NET:<worker-url>\t<groepscode>\t<mijn id>\t<naam>
    char *f[4] = {line + 4, nullptr, nullptr, nullptr}; int n = 1; for (char *q = line + 4; *q && n < 4; q++) if (*q == '\t') { *q = 0; f[n++] = q + 1; }
    if (n == 4) { strncpy(wNieuw.url, f[0], sizeof wNieuw.url - 1); strncpy(wNieuw.grp, f[1], sizeof wNieuw.grp - 1); strncpy(wNieuw.van, f[2], sizeof wNieuw.van - 1); strncpy(wNieuw.naam, f[3], sizeof wNieuw.naam - 1); netNieuw = true; }
    return;
  }
  if (!strncmp(line, "SET:", 4)) {   // v27: instellingen uit de app, bv. SET:l=65;v=80;a=1;o=pet
    for (char *tok = strtok(line + 4, ";"); tok; tok = strtok(nullptr, ";")) {
      if (tok[0] == 'l' && tok[1] == '=') setLicht = atoi(tok + 2);
      else if (tok[0] == 'v' && tok[1] == '=') setVol = atoi(tok + 2);
      else if (tok[0] == 'a' && tok[1] == '=') { float f = atof(tok + 2); if (f > .2f && f < 5) animFactor = f; }
      else if (tok[0] == 'o' && tok[1] == '=') { strncpy(outfitK, tok + 2, sizeof outfitK - 1); outfitK[sizeof outfitK - 1] = 0; outfitT = millis(); }
    }
    return;
  }
  if (!strncmp(line, "WIFI:", 5)) { char *tab = strchr(line + 5, '\t'); if (tab) { *tab = 0; strncpy(wifiNaamNieuw, line + 5, 32); wifiNaamNieuw[32] = 0; strncpy(wifiPassNieuw, tab + 1, 64); wifiPassNieuw[64] = 0; wifiNieuw = true; } return; }
  char *f[14] = {0}; int n = 0; f[n++] = line;
  for (char *q = line; *q && n < 14; ++q) if (*q == '|') { *q = 0; f[n++] = q + 1; }
  if (n < 9 || strcmp(f[0], "B1") != 0) return;
  PhoneData d = phone;
  d.v = atoi(f[1]); d.lim = atoi(f[2]); d.camD = atoi(f[3]); d.camLim = atoi(f[4]);
  d.fileD = atoi(f[5]); d.fileMin = atoi(f[6]); d.werk = atoi(f[7]) == 1; d.mood = -1;
  for (int i = 0; i < MOOD_COUNT; i++) if (strcmp(f[8], moodKeys[i]) == 0) d.mood = i;
  strncpy(d.txt, n > 9 ? f[9] : "", sizeof(d.txt) - 1); d.txt[sizeof(d.txt) - 1] = 0;
  d.nav = (n > 10 && f[10][0]) ? f[10][0] : 0; d.navD = d.nav ? atoi(f[10] + 1) : 0;   // v29: navigatiepijl, bv. R350
  strncpy(d.msi, n > 11 ? f[11] : "", sizeof(d.msi) - 1); d.msi[sizeof(d.msi) - 1] = 0;  // v29: matrixbord: x of 70
  d.ms = millis(); phone = d;
}
class BuddySrvCb : public BLEServerCallbacks {
  void onConnect(BLEServer *) override { bleConnected = true; if (!gekoppeld) pinTot = millis() + 60000; logRegel("BLE: telefoon verbonden"); }
  void onDisconnect(BLEServer *) override { bleConnected = false; logRegel("BLE: telefoon weg"); BLEDevice::startAdvertising(); }
};
class BuddyRxCb : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    auto v = c->getValue(); if (!gekoppeld) { gekoppeld = true; pinTot = 0; }
    for (size_t i = 0; i < v.length(); i++) {
      char ch = v[i];
      if (ch == '\n') { bleBuf[bleLen] = 0; parsePhone(bleBuf); bleLen = 0; }
      else if (bleLen < sizeof(bleBuf) - 1) bleBuf[bleLen++] = ch;
    }
  }
};
static void bleStart() {
  BLEDevice::init("Buddy-scherm");
  BLEDevice::setMTU(185);
  BLEServer *srv = BLEDevice::createServer(); srv->setCallbacks(new BuddySrvCb());
  BLEService *svc = srv->createService(NUS_SVC);
  BLECharacteristic *rx = svc->createCharacteristic(NUS_RX, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  rx->setCallbacks(new BuddyRxCb());
  rx->setAccessPermissions(ESP_GATT_PERM_WRITE_ENC_MITM);   // v29: alleen na koppelen met de koppelcode
  BLESecurity *sec = new BLESecurity();
#if defined(ESP_ARDUINO_VERSION) && ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 3, 0)
  sec->setPassKey(true, koppelPin); sec->setCapability(ESP_IO_CAP_OUT); sec->setAuthenticationMode(true, true, true);   // nieuwere ESP32-versie
#else
  sec->setStaticPIN(koppelPin);
#endif
  bleTx = svc->createCharacteristic(NUS_TX, BLECharacteristic::PROPERTY_NOTIFY); bleTx->addDescriptor(new BLE2902());
  svc->start();
  BLEAdvertising *adv = BLEDevice::getAdvertising(); adv->addServiceUUID(NUS_SVC); adv->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.println("BLE: zichtbaar als 'Buddy-scherm'");
}
static void logNaarTelefoonTaak(void *) {
  // laatste ~12 KB van het logboek in kleine stukjes naar de app (die zet het in zijn eigen log -> buddy-logs)
  if (logFs && bleConnected && bleTx) {
    bleTx->setValue((uint8_t *)"LOGBEGIN", 8); bleTx->notify(); vTaskDelay(pdMS_TO_TICKS(30));
    File f = logFs->open(LOGF, FILE_READ);
    if (f) { size_t sz = f.size(); if (sz > 12000) f.seek(sz - 12000);
      uint8_t b[18]; while (bleConnected && f.available()) { int n = f.read(b, sizeof b); bleTx->setValue(b, n); bleTx->notify(); vTaskDelay(pdMS_TO_TICKS(12)); }
      f.close(); }
    bleTx->setValue((uint8_t *)"LOGEND", 6); bleTx->notify();
  }
  vTaskDelete(NULL);
}
static void bleSend(const char *t) { if (bleTx && bleConnected) { bleTx->setValue((uint8_t *)t, strlen(t)); bleTx->notify(); } }

// ===================== v19: meehangen in bochten (QMI8658) =====================
// v20: de QMI8658 rechtstreeks via registers (de SensorLib-versie liet v19 crashen bij het opstarten)
static bool imuReady = false;
static bool qmiW(uint8_t r, uint8_t v) { Wire.beginTransmission(0x6B); Wire.write(r); Wire.write(v); return Wire.endTransmission() == 0; }
static bool qmiInit() {
  Wire.beginTransmission(0x6B); Wire.write(0x00); if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)0x6B, 1) != 1) return false;
  uint8_t who = Wire.read(); if (who != 0x05) { Serial.printf("Buddy gyro: onbekend WHO_AM_I 0x%02X\n", who); return false; }
  qmiW(0x60, 0xB0); delay(30);   // v26: zachte reset (ook om de 'wakker bij beweging'-stand van de slaap te wissen)
  qmiW(0x02, 0x40);   // CTRL1: registers automatisch ophogen (v26: was 0x60 = big endian, daardoor klopte het meehangen niet)
  qmiW(0x03, 0x15);   // CTRL2: versnelling +-4 G, ~250 Hz
  qmiW(0x08, 0x01);   // CTRL7: versnellingsmeter aan
  delay(20); return true;
}
static float gX = 0, gY = 0, gZ = 1, leanX = 0, pushZ = 0, hop = 0; static bool gInit = false;
static constexpr float LEAN_RICHTING = 1.0f;  // hangt hij de verkeerde kant op? Zet dit op -1.0f
static uint32_t schrikTot = 0; static volatile uint32_t laatsteBeweging = 0, laatsteDeur = 0;
static void imuStep(float dt) {
  if (!imuReady || !i2cLock(2)) return;
  float ax, ay, az; uint8_t d[6]; bool ok = false;
  Wire.beginTransmission(0x6B); Wire.write(0x35);
  if (Wire.endTransmission(false) == 0 && Wire.requestFrom((int)0x6B, 6) == 6) { for (int i = 0; i < 6; i++) d[i] = Wire.read(); ok = true; }
  i2cUnlock(); if (!ok) return;
  ax = (int16_t)(d[0] | (d[1] << 8)) / 8192.0f; ay = (int16_t)(d[2] | (d[3] << 8)) / 8192.0f; az = (int16_t)(d[4] | (d[5] << 8)) / 8192.0f;
  if (!gInit) { gX = ax; gY = ay; gZ = az; gInit = true; }
  float k = 1 - expf(-dt / 1.6f);            // zwaartekracht = traag gemiddelde
  gX += (ax - gX) * k; gY += (ay - gY) * k; gZ += (az - gZ) * k;
  float dx = ax - gX, dy = ay - gY, dz = az - gZ;
  float kl = 1 - expf(-dt / .18f);
  leanX += (dx - leanX) * kl; pushZ += (dz - pushZ) * kl;
  if (fabsf(dy) > .45f && hop < .3f) { hop = 1; }   // drempel of gat
  hop = fmaxf(0, hop - dt * 3.2f);
  if (fabsf(pushZ) > .45f && millis() > schrikTot) schrikTot = millis() + 1400;  // hard remmen / optrekken
  // v26: schokjes voor in- en uitstappen
  float schok = sqrtf(dx * dx + dy * dy + dz * dz);
  if (schok > .035f) laatsteBeweging = millis();
  if (schok > .22f) laatsteDeur = millis();
}

// ===================== v19: rond menu (lang drukken) =====================
static bool menuOpen = false; static uint32_t menuTot = 0, infoTot = 0, aaiTot = 0;
// v30: vensters over alles heen (0 = geen, 1 = info, 2 = volume, 3 = helderheid); sluiten met TERUG
static uint8_t venster = 0; static uint32_t vensterTot = 0; static bool lichtOpslaan = false;
static uint8_t helderheid = 160;
static const char *menuTxt[5] = {"VOLUME", "HELDER", "FLITSER", "INFO", "ZWAAI"};   // v28: zwaaien naar de groep
static void drawMenu() {
  boxAdd(0, 0, 240, 240);
  frame->fillCircle(X(120), Y(120), S(118), 0x0841);
  for (int i = 0; i < 5; i++) {
    float a = -PI2 / 4 + i * PI2 / 5; float cx = 120 + cosf(a) * 76, cy = 120 + sinf(a) * 76;
    frame->fillCircle(X(cx), Y(cy), S(31), i == 2 ? 0x8000 : (i == 4 ? 0x0451 : 0x18E3));
    frame->drawCircle(X(cx), Y(cy), S(31), EYE);
    frame->setTextColor(0xFFFF); frame->setTextSize(2);
    int tw = strlen(menuTxt[i]) * 12; frame->setCursor(X(cx) - tw / 2, Y(cy) - 7); frame->print(menuTxt[i]);
  }
  frame->fillCircle(X(120), Y(120), S(26), 0x2124); frame->drawCircle(X(120), Y(120), S(26), EYE);
  frame->setTextColor(EYE); frame->setTextSize(2); frame->setCursor(X(120) - 30, Y(120) - 7); frame->print("TERUG");
}
// v21: records en ritjes (blijven bewaard in het geheugen van het bordje)
static float rec80 = 0, rec100 = 0, recG = 0, totKm = 0; static uint32_t totRit = 0; static char dat80[9] = "", dat100[9] = "";
static const char *netStatus = "wifi uit";
static void infoRegel(int y, const char *r) { frame->setCursor(X(120) - (int)strlen(r) * 6, Y(y)); frame->print(r); }
static void drawInfo() {
  frame->setTextColor(EYE); frame->setTextSize(2);
  char r[48]; int y = 46;
  snprintf(r, sizeof r, "Buddy v%d %s", FW_VERSIE, netStatus); infoRegel(y, r); y += 16;
  snprintf(r, sizeof r, "GPS %s %d sat", gpsFix ? "FIX" : (gpsDataOk ? "zoekt" : "geen data"), (int)gpsSatellites); infoRegel(y, r); y += 16;
  snprintf(r, sizeof r, "Tel %s code %06lu", bleConnected ? (phoneFresh() ? "OK" : "stil") : "niet", (unsigned long)koppelPin); infoRegel(y, r); y += 16;
  snprintf(r, sizeof r, "Volume %d%% Licht %d%%", buddyVolume, helderheid * 100 / 255); infoRegel(y, r); y += 16;
  snprintf(r, sizeof r, "Bocht %.2f G", fabsf(leanX)); infoRegel(y, r); y += 16;
  frame->setTextColor(GOLD);
  if (rec100 > 0) snprintf(r, sizeof r, "0-100 %.1fs %s", rec100, dat100); else snprintf(r, sizeof r, "0-100 nog niet"); infoRegel(y, r); y += 16;
  if (rec80 > 0) snprintf(r, sizeof r, "0-80 %.1fs %s", rec80, dat80); else snprintf(r, sizeof r, "0-80 nog niet"); infoRegel(y, r); y += 16;
  snprintf(r, sizeof r, "Max %.2fG %dkm %d rit", recG, (int)totKm, (int)totRit); infoRegel(y, r);
}
// Schuifbalk: x 40..200 op hoogte 122; sleep je vinger eroverheen.
static const float SL_X0 = 40, SL_X1 = 200, SL_Y = 122;
static void drawSlider(int pct, const char *titel, uint16_t kleur) {
  frame->setTextColor(0xFFFF); frame->setTextSize(3);
  frame->setCursor(X(120) - (int)strlen(titel) * 9, Y(48)); frame->print(titel);
  char r[8]; snprintf(r, sizeof r, "%d%%", pct);
  frame->setTextColor(kleur); frame->setTextSize(4); frame->setCursor(X(120) - (int)strlen(r) * 12, Y(76)); frame->print(r);
  float kx = SL_X0 + (SL_X1 - SL_X0) * pct / 100.0f;
  roundRect(SL_X0, SL_Y - 5, SL_X1 - SL_X0, 10, 5, 0x2945);
  if (kx > SL_X0 + 1) roundRect(SL_X0, SL_Y - 5, kx - SL_X0, 10, 5, kleur);
  circle(kx, SL_Y, 13, 0xFFFF); frame->drawCircle(X(kx), Y(SL_Y), S(13), kleur);
  frame->setTextColor(0x8410); frame->setTextSize(2);
  frame->setCursor(X(SL_X0) - 6, Y(SL_Y + 22)); frame->print("-");
  frame->setCursor(X(SL_X1) - 6, Y(SL_Y + 22)); frame->print("+");
}
static void drawVenster() {
  boxAdd(0, 0, 240, 240);
  frame->fillCircle(X(120), Y(120), S(119), 0x0841);
  frame->drawCircle(X(120), Y(120), S(118), EYE);
  if (venster == 1) drawInfo();
  else if (venster == 2) drawSlider(buddyVolume, "VOLUME", EYE);
  else drawSlider(helderheid * 100 / 255, "HELDER", GOLD);
  // TERUG-knop onderaan
  roundRect(70, 182, 100, 32, 16, 0x18E3); frame->drawRoundRect(X(70), Y(182), S(100), S(32), S(16), EYE);
  frame->setTextColor(0xFFFF); frame->setTextSize(2); frame->setCursor(X(120) - 30, Y(198) - 7); frame->print("TERUG");
}
static void openVenster(uint8_t v) { venster = v; vensterTot = millis() + 30000; menuOpen = false; }
static void sluitVenster() {
  venster = 0;
  if (lichtOpslaan) { buddyPrefs.putUChar("licht", helderheid); lichtOpslaan = false; }
}
// Vinger op de schuifbalk (ook tijdens slepen)
static void vensterSleep(float x, float y) {
  if (venster < 2 || y < SL_Y - 30 || y > SL_Y + 30 || x < SL_X0 - 20 || x > SL_X1 + 20) return;
  int v = constrain((int)lroundf((x - SL_X0) * 100.0f / (SL_X1 - SL_X0)), 0, 100);
  if (venster == 2) {
    if ((uint8_t)v != buddyVolume) { buddyVolume = (uint8_t)v; if (audioReady) applyCodecVolume(buddyVolume); volumeNeedsSave = true; lastVolumeChange = millis(); }
  } else {
    uint8_t h = (uint8_t)constrain(v * 255 / 100, 20, 255);   // nooit helemaal zwart
    if (h != helderheid) { helderheid = h; lichtOpslaan = true; }
  }
}
static void menuKies(float x, float y) {
  float dx = x - 120, dy = y - 120, r = sqrtf(dx * dx + dy * dy);
  if (r < 34) { menuOpen = false; return; }
  float a = atan2f(dy, dx); int i = (int)lroundf((a + PI2 / 4) / (PI2 / 5)); i = ((i % 5) + 5) % 5;
  menuOpen = false;
  if (i == 0) openVenster(2);
  else if (i == 1) openVenster(3);
  else if (i == 2) { bleSend("MELD:cam"); mood = bleConnected ? ALERT : WORRIED; lastMood = -1; idleMoodUntilMs = millis() + 2500; Serial.println(bleConnected ? "Flitser gemeld via telefoon" : "Flitser melden: telefoon niet verbonden"); }
  else if (i == 3) openVenster(1);
  else { zwaaiVraag = true; if (bleConnected) bleSend("MELD:zwaai"); startGag(22); gagMsg = "Zwaai naar de groep!"; }
}

// ===================== v21: animaties (overgezet uit de app) =====================
enum { G_IDLE = 1, G_DRIVE = 2, G_STOP = 4 };
struct GagDef { float dur; uint8_t flags; int8_t mood; const char *msg; };
static const GagDef GAGS[] = {
  {5.6f, G_STOP, BORED, "Rood... nog steeds rood..."},     // 0 ongeduldig (stilstaan tijdens een rit)
  {5.8f, G_IDLE, ALERT, "Een vliegje!"},                    // 1 vlieg
  {5.0f, G_IDLE, BLOW, "Ik blaas een bel..."},              // 2 bel
  {4.6f, G_IDLE | G_DRIVE, ALERT, "Even rondkijken..."},    // 3 rondkijken
  {4.8f, G_IDLE, BORED, "Wheeeee..."},                      // 4 ogen rollen
  {3.8f, G_IDLE, YAWN, "Ah... ah..."},                      // 5 niezen
  {6.0f, G_IDLE | G_DRIVE, HAPPY, "La la la..."},           // 6 neuriën
  {2.2f, G_IDLE | G_DRIVE, HAPPY, "Knipoog!"},              // 7 knipoog
  {6.0f, G_IDLE, EXCITED, "Dansje!"},                       // 8 dansen
  {2.6f, G_IDLE, HAPPY, "Bleh!"},                           // 9 tong
  {6.4f, G_IDLE, ALERT, "Wie knippert eerst?"},             // 10 staren
  {6.4f, G_IDLE, SLEEPY, "Zzz... zo moe..."},               // 11 knikkebollen
  {4.6f, G_IDLE | G_DRIVE, NEUTRAL, "Hik... sorry."},       // 12 hik
  {6.0f, G_IDLE, FOCUSED, "Drie ballen... kijk!"},          // 13 jongleren
  {3.6f, G_IDLE, NEUTRAL, "Waar is Buddy..."},              // 14 kiekeboe
  {6.4f, G_IDLE, WORRIED, "Regent het alleen bij mij?"},    // 15 regenwolkje
  {6.2f, G_IDLE, LOVE, "Ooh, een vlinder..."},              // 16 vlinder
  {5.0f, G_IDLE | G_DRIVE, CONTENT, "Zo. Cool."},           // 17 zonnebril
  {4.6f, G_IDLE, WORRIED, "Bonk... sterretjes."},           // 18 sterretjes
  {5.6f, G_IDLE, HAPPY, "Jojo!"},                           // 19 jojo
  {4.8f, G_IDLE, ANTICIPATE, "..."},                        // 20 om het hoekje gluren
  {4.4f, G_IDLE, WORRIED, "Brrr... verwarming aan?"},       // 21 koud
  {3.6f, G_IDLE | G_DRIVE, HAPPY, "Hoi hoi!"},              // 22 zwaaien
  {5.4f, G_IDLE | G_DRIVE, ALERT, "Kijk, een vliegtuig!"},  // 23 vliegtuig
  {5.2f, G_IDLE | G_DRIVE, EXCITED, "Ba-dum-tss!"},         // 24 trommelen
  {4.0f, G_IDLE | G_DRIVE, LOVE, "Jij bent mijn favoriet."},// 25 verlegen
  {4.2f, G_IDLE, EXCITED, "Boing boing!"},                  // 26 stuiteren
  {5.6f, G_IDLE, FOCUSED, "Hmm... even nadenken."},         // 27 nadenken
  // v28: nieuwe animaties
  {6.0f, G_IDLE | G_DRIVE, LOVE, "Ooh, een regenboog!"},      // 28 regenboog
  {5.0f, G_IDLE, EXCITED, "3, 2, 1... lancering!"},          // 29 raket
  {6.0f, G_IDLE, HAPPY, "Een ballonnetje!"},                 // 30 ballon
  {6.4f, G_IDLE, RELAXED, "Vuurvliegjes..."},                // 31 vuurvliegjes
  {6.0f, G_IDLE, ALERT, "Wat is DAT?!"},                     // 32 ufo
  {7.0f, G_STOP | G_IDLE, BORED, "Die slak is sneller..."},  // 33 slak (vooral in de file)
  {5.4f, G_IDLE | G_DRIVE, ALERT, "Hè? Wat zweeft daar..."}, // 34 spookje (Halloween)
  {5.0f, G_IDLE | G_DRIVE, EXCITED, "Snoep of je leven!"},   // 35 pompoen (Halloween)
};
static constexpr int GAG_N = sizeof(GAGS) / sizeof(GAGS[0]);
static inline void gdir(float x, float y) { tgx = x; tgy = y; nextGaze = t + .3f; }
static inline float frand(float a, float b) { return a + (b - a) * (random(10000) / 10000.0f); }
static void addParticleAt(uint8_t kind, float x, float y, float vx, float vy, float life, float size) {
  for (auto &p : particles) if (p.life <= 0) { p = {x, y, vx, vy, 0, life, size, kind}; return; }
}
static void burstAt(float x, float y, int n) { for (int i = 0; i < n; i++) { float a = frand(0, PI2), v = frand(30, 70); addParticleAt(1, x, y, cosf(a) * v, sinf(a) * v, .6f, frand(3, 6)); } }
static void noot(float x, float y) { addParticleAt(3, x, y, frand(-8, 8), -frand(18, 28), 1.5f, 6); }
static void ellipseF(float x, float y, float rx, float ry, uint16_t c) { boxAdd(x - rx, y - ry, x + rx, y + ry); frame->fillEllipse(X(x), Y(y), S(rx), S(ry), c); }
static void triF(float x1, float y1, float x2, float y2, float x3, float y3, uint16_t c) { boxAdd(fminf(x1, fminf(x2, x3)), fminf(y1, fminf(y2, y3)), fmaxf(x1, fmaxf(x2, x3)), fmaxf(y1, fmaxf(y2, y3))); frame->fillTriangle(X(x1), Y(y1), X(x2), Y(y2), X(x3), Y(y3), c); }
// ===================== v27: outfits (zelfde als in de app) en feestdagen =====================
static const char *feestOutfit() {
  int d = 0, m = 0;
  if (gpsDate[0] >= '0' && gpsDate[4] >= '0' && !(gpsDate[4] == '8' && gpsDate[5] == '0')) { d = (gpsDate[0] - '0') * 10 + gpsDate[1] - '0'; m = (gpsDate[2] - '0') * 10 + gpsDate[3] - '0'; }
  else { time_t nu = time(nullptr); if (nu > 1700000000) { struct tm tv; localtime_r(&nu, &tv); d = tv.tm_mday; m = tv.tm_mon + 1; } }
  if (!m) return nullptr;
  if ((m == 12 && d == 31) || (m == 1 && d == 1)) return "feesthoed";
  if (m == 2 && d == 14) return "hartjes";
  if (m == 4 && d == 27) return "oranjekroon";
  if (m == 10 && d == 31) return "heksenhoed";
  if ((m == 11 && d >= 15) || (m == 12 && d <= 5)) return "mijter";
  if (m == 12 && d >= 6 && d <= 26) return "kerstmuts";
  return nullptr;
}
static void drawOutfit() {
  const char *k = (outfitK[0] && millis() - outfitT < 24UL * 3600000) ? outfitK : feestOutfit();
  if (!k || !strcmp(k, "geen")) return;
  float x = faceOx * .4f, y = faceOy * .4f;
  const uint16_t ROOD = 0xE1A6, WIT = 0xF79E, GOUDK = 0xFE4A, ORANJE = 0xFBC0, PAARS = 0x7AB8;
  auto kroon = [&](uint16_t c) {
    triF(88 + x, 60 + y, 88 + x, 34 + y, 101 + x, 46 + y, c); triF(88 + x, 60 + y, 101 + x, 46 + y, 152 + x, 60 + y, c);
    triF(101 + x, 46 + y, 120 + x, 26 + y, 139 + x, 46 + y, c); triF(101 + x, 46 + y, 139 + x, 46 + y, 152 + x, 60 + y, c);
    triF(139 + x, 46 + y, 152 + x, 34 + y, 152 + x, 60 + y, c);
    circle(88 + x, 34 + y, 3.5f, c == GOUDK ? ROOD : WIT); circle(120 + x, 26 + y, 3.5f, c == GOUDK ? ROOD : WIT); circle(152 + x, 34 + y, 3.5f, c == GOUDK ? ROOD : WIT);
  };
  if (!strcmp(k, "pet")) { ellipseF(120 + x, 58 + y, 54, 34, ROOD); roundRect(60 + x, 56 + y, 120, 12, 4, 0x0000); triF(150 + x, 58 + y, 204 + x, 66 + y, 176 + x, 70 + y, 0xB0E3); circle(120 + x, 24 + y, 4, WIT); }
  else if (!strcmp(k, "kroon")) kroon(GOUDK);
  else if (!strcmp(k, "oranjekroon")) kroon(ORANJE);
  else if (!strcmp(k, "kerstmuts")) { triF(70 + x, 58 + y, 166 + x, 34 + y, 170 + x, 58 + y, ROOD); triF(70 + x, 58 + y, 120 + x, 22 + y, 166 + x, 34 + y, ROOD); roundRect(64 + x, 52 + y, 112, 14, 7, WIT); circle(166 + x, 34 + y, 8, WIT); }
  else if (!strcmp(k, "mijter")) { triF(92 + x, 62 + y, 120 + x, 8 + y, 148 + x, 62 + y, ROOD); roundRect(88 + x, 56 + y, 64, 8, 3, GOUDK); roundRect(117 + x, 24 + y, 6, 30, 1, GOUDK); roundRect(108 + x, 33 + y, 24, 6, 1, GOUDK); }
  else if (!strcmp(k, "heksenhoed")) { roundRect(56 + x, 54 + y, 128, 9, 4, PAARS); triF(100 + x, 56 + y, 120 + x, 4 + y, 140 + x, 56 + y, PAARS); roundRect(104 + x, 46 + y, 32, 7, 1, ORANJE); }
  else if (!strcmp(k, "feesthoed")) { triF(98 + x, 62 + y, 120 + x, 8 + y, 142 + x, 62 + y, 0x451F); roundRect(108 + x, 36 + y, 24, 8, 2, GOUDK); circle(120 + x, 8 + y, 6, HEART); }
  else if (!strcmp(k, "strik")) { triF(120 + x, 196 + y, 102 + x, 186 + y, 102 + x, 206 + y, ROOD); triF(120 + x, 196 + y, 138 + x, 186 + y, 138 + x, 206 + y, ROOD); circle(120 + x, 196 + y, 4.5f, ROOD); }
  else if (!strcmp(k, "oren")) { triF(56 + x, 64 + y, 70 + x, 18 + y, 100 + x, 50 + y, EYE); triF(184 + x, 64 + y, 170 + x, 18 + y, 140 + x, 50 + y, EYE); triF(66 + x, 56 + y, 72 + x, 32 + y, 88 + x, 50 + y, BLUSH); triF(174 + x, 56 + y, 168 + x, 32 + y, 152 + x, 50 + y, BLUSH); }
  else if (!strcmp(k, "koptel")) { for (int i = 0; i < 12; i++) { float a1 = 3.39f + i * .2f, a2 = a1 + .2f; line(120 + cosf(a1) * 88 + x, 112 + sinf(a1) * 88 + y, 120 + cosf(a2) * 88 + x, 112 + sinf(a2) * 88 + y, 0x9533, 6); } roundRect(22 + x, 90 + y, 20, 46, 8, 0x5373); roundRect(198 + x, 90 + y, 20, 46, 8, 0x5373); }
  else if (!strcmp(k, "helm")) { for (int i = 0; i < 16; i++) { float a1 = 3.46f + i * .157f, a2 = a1 + .16f; line(120 + cosf(a1) * 88 + x, 120 + sinf(a1) * 88 + y, 120 + cosf(a2) * 88 + x, 120 + sinf(a2) * 88 + y, ROOD, 13); } }
  else if (!strcmp(k, "hartjes")) { float w = sinf(t * 4) * 2; heart(96 + w + x, 36 + y, 12, HEART); heart(144 - w + x, 36 + y, 12, HEART); }
}
static void spiraal(float cx, float cy) { float px = cx, py = cy; for (int i = 1; i <= 40; i++) { float a = i * .5f + t * 6, r = i * .55f; float x = cx + cosf(a) * r, y = cy + sinf(a) * r; line(px, py, x, y, EYE, 3); px = x; py = y; } }

static float spdNow(bool *ok = nullptr) {
  bool g = gpsFix && millis() - gpsLastFixMs < 5000;
  if (ok) *ok = g || phoneFresh();
  return g ? gpsSpeedKmh : (phoneFresh() ? (float)phone.v : 0);
}
static bool tripActief = false;
static void startGag(int k) {
  gagAct = k; gagT0 = t; gagMood = GAGS[k].mood; gagMsg = GAGS[k].msg; g_b = false; g_nn = 0; g_r = 0;
}
static void gagStep(float dt) {
  fdx = fdy = 0; eyeL = eyeR = 1; fxNoBlink = fxSpiral = fxTongue = false;
  float v = spdNow();
  if (gagAct < 0) {
    if (t < nextGagT || menuOpen || millis() < flagTot || mood == ALERT || mood == WORRIED || mood == SLEEPING) return;
    bool rijdt = v >= 15, stil = v < 1;
    int kies[GAG_N]; int n = 0;
    const char *fo = feestOutfit(); bool halloween = fo && !strcmp(fo, "heksenhoed");
    for (int k = 0; k < GAG_N; k++) {
      uint8_t f = GAGS[k].flags;
      if ((k == 34 || k == 35) && !halloween) continue;
      if (k == 31 && klokOk()) { int hh, mm; lokaleTijd(hh, mm); if (hh >= 7 && hh < 18) continue; }   // vuurvliegjes alleen 's avonds
      if ((stil && (f & G_IDLE)) || (rijdt && (f & G_DRIVE)) || (stil && tripActief && (f & G_STOP))) kies[n++] = k;
    }
    if (!n) { nextGagT = t + 3; return; }
    int k = kies[random(n)];
    if (halloween && random(3) == 0) k = random(2) ? 34 : 35;   // op Halloween vaker het spookje en de pompoen
    startGag(k);
    nextGagT = t + GAGS[k].dur + (stil ? frand(8, 16) : frand(15, 30)) * animFactor;
    return;
  }
  float tt = t - gagT0; const GagDef &d = GAGS[gagAct];
  if (tt > d.dur) { gagAct = -1; gagMood = -1; gagMsg = nullptr; return; }
  switch (gagAct) {
    case 0: gdir(0, tt < 3.6f ? -.7f : .2f); fdy = tt > 1.5f ? fabsf(sinf(tt * 6)) * 3 : 0; if (tt > 3.6f) { gagMsg = "Kom op, groen!"; gagMood = ANTICIPATE; } break;
    case 1: { float x = sinf(tt * 2.1f) * .9f + sinf(tt * 5) * .1f, y = cosf(tt * 1.6f) * .6f; g_fx = 120 + x * 75; g_fy = 88 + y * 40;
      if (tt < 4.8f) gdir(x * .9f, y * .9f); else { gdir(0, .1f); gagMood = HAPPY; gagMsg = "Gevangen! ...oh, weg."; } } break;
    case 2: gdir(0, .3f); if (tt < 3.6f) g_r = 3 + tt / 3.6f * 27; else { g_r = 0; gagMood = HAPPY; gagMsg = "Plop!"; if (!g_b) { g_b = true; burstAt(120, 168, 12); } } break;
    case 3: { static const float dx[5] = {-1, 1, -.6f, .8f, 0}, dy[5] = {0, -.3f, .2f, -.2f, 0}; int i = (int)(tt / .85f) % 5; gdir(dx[i], dy[i]); if (tt > 4) gagMsg = "Alles veilig."; } break;
    case 4: { float a = tt * 6.5f; gdir(cosf(a) * .95f, sinf(a) * .85f); if (tt > 3.3f) { fxSpiral = true; gagMood = WORRIED; gagMsg = "Oei, duizelig..."; } } break;
    case 5: if (tt < 2) { fdy = -tt * 3; eyeL = eyeR = 1 - tt * .2f; gdir(0, -.4f); } else { gagMood = STARTLED; fdy = 9; gagMsg = "HATSJOE!"; eyeL = eyeR = .15f; if (!g_b) { g_b = true; burstAt(120, 172, 16); } } break;
    case 6: fdx = sinf(tt * 3) * 5; gdir(sinf(tt * 1.5f) * .3f, -.15f); if (tt > g_nn) { g_nn += .55f; noot(150 + frand(-10, 20), 80); } break;
    case 7: if (tt > .4f && tt < 1.3f) { eyeR = .08f; if (!g_b) { g_b = true; burstAt(190, 96, 6); } } gdir(0, 0); break;
    case 8: fdx = sinf(tt * 5) * 10; fdy = -fabsf(sinf(tt * 5)) * 7; gdir(sinf(tt * 5) * .5f, 0); if (tt > g_nn) { g_nn += .45f; noot(frand(40, 200), frand(60, 100)); } if (tt > 4.8f) gagMsg = "Kijk mij eens!"; break;
    case 9: fxTongue = tt > .3f && tt < 2.2f; if (fxTongue) eyeL = .45f; gdir(0, .1f); break;
    case 10: gdir(0, 0); if (tt < 5.4f) fxNoBlink = true; else { gagMsg = "Oei, ik knipperde."; if (!g_b) { g_b = true; blinkStart = t; } } break;
    case 11: if (tt < 4.4f) { fdy = tt * 3.2f; gdir(0, .6f); } else { gagMood = STARTLED; gagMsg = "Ik sliep niet!"; gdir(0, 0); } break;
    case 12: { float h = fmodf(tt, .95f); if (h < .14f && tt < 4) { fdy = -9; gagMood = STARTLED; gagMsg = "Hik!"; } else { gagMood = NEUTRAL; gagMsg = tt < 4 ? "Hik... sorry." : "Weg. Denk ik."; } } break;
    case 13: { float a = tt * 4.2f; gdir(cosf(a) * .5f, -.5f + fabsf(sinf(a)) * .3f); if (tt > 5) { gagMood = HAPPY; gagMsg = "Ta-daa!"; } } break;
    case 14: if (tt < 2.2f) { eyeL = eyeR = .06f; fxNoBlink = true; fdy = 3; } else { gagMood = EXCITED; gagMsg = "KIEKEBOE!"; fdy = -6; if (!g_b) { g_b = true; burstAt(120, 110, 14); } } break;
    case 15: gdir(0, -.8f); if (tt > 4.6f) { gagMood = HAPPY; gagMsg = "Oh, de zon komt al!"; gdir(0, -.3f); } break;
    case 16: { float x = sinf(tt * 1.3f) * .85f, y = sinf(tt * 2.6f) * .45f - .2f; g_fx = 120 + x * 80; g_fy = 80 + y * 45; gdir(x, y); if (tt > 5.4f) gagMsg = "Dag vlinder!"; } break;
    case 17: gdir(0, 0); g_fy = tt < .8f ? -60 + tt / .8f * 166 : 106; if (tt > 3.4f) gagMsg = "Coolste auto op de weg."; break;
    case 18: fxSpiral = tt < 3.6f; if (tt > 3.6f) { gagMood = CONTENT; gagMsg = "Gaat alweer."; } break;
    case 19: g_d = fabsf(sinf(tt * 2.6f)); gdir(.3f, -.2f + g_d * .9f); break;
    case 20: if (tt < 1.2f) fdx = tt / 1.2f * 150; else if (tt < 3.2f) { fdx = 150 - fminf(1, (tt - 1.2f) / .5f) * 60; gdir(-1, 0); gagMsg = "Psst... ben je er nog?"; }
             else { fdx = 90 * fmaxf(0, 1 - (tt - 3.2f) / .6f); gagMood = HAPPY; gagMsg = "Daar ben ik weer!"; gdir(0, 0); } break;
    case 21: if (tt < 3.4f) { fdx = sinf(tt * 60) * 2.2f; fdy = sinf(tt * 47) * 1.2f; gdir(0, .2f); } else { gagMood = CONTENT; gagMsg = "Ahh, lekker warm."; } break;
    case 22: gdir(.2f, 0); break;
    case 23: g_fx = -20 + tt / 5.4f * 280; gdir((g_fx - 120) / 120, -1); if (tt > 4.4f) gagMsg = "Die gaat op vakantie!"; break;
    case 24: { float b = fmodf(tt * 4, 1); fdy = b < .15f ? 4 : 0; gdir(sinf(tt * 4) * .4f, .3f); if (tt > g_nn) { g_nn += .5f; noot(frand(30, 210), frand(140, 165)); } } break;
    case 25: gdir(-.6f, .4f); if (tt > 2.8f) { gagMsg = "Oeps, zei ik dat hardop?"; gdir(.5f, .5f); } break;
    case 26: { float h = fabsf(sinf(tt * 5)) * fmaxf(0, 1 - tt / 4.2f); fdy = -h * 22; gdir(0, -h * .5f); } break;
    case 27: gdir(.7f, -.7f); if (tt > 4.4f) { gagMood = HAPPY; gagMsg = "Ik weet het! ...kwijt."; gdir(0, 0); } break;
    case 28: gdir(0, -.8f); if (tt > 4.5f) gagMsg = "Zo mooi..."; break;
    case 29: gdir(.4f, -.6f + fminf(0, -(tt - 2.6f) * .3f)); if (tt < 2.6f) gagMsg = tt < .9f ? "3..." : (tt < 1.8f ? "2..." : "1..."); else { gagMsg = "Lancering!"; if (!g_b) { g_b = true; burstAt(176, 190, 12); } } break;
    case 30: g_fy = 230 - tt * 40; gdir(.5f, (g_fy - 120) / 120); if (g_fy < 60 && !g_b) { g_b = true; burstAt(176, g_fy, 14); gagMood = STARTLED; gagMsg = "Pang! Oei..."; } break;
    case 31: gdir(sinf(tt * 1.3f) * .7f, cosf(tt * 1.1f) * .4f - .2f); break;
    case 32: g_fx = -30 + tt / 6.f * 300; gdir((g_fx - 120) / 110, -.9f); if (tt > 2.2f && tt < 3.8f) { gagMood = STARTLED; gagMsg = "Ze nemen me mee!"; fdy = -4; } else if (tt >= 3.8f) { gagMood = HAPPY; gagMsg = "Pfiew. Ze waren vriendelijk."; } break;
    case 33: g_fx = 260 - tt * 16; gdir((g_fx - 120) / 120, .8f); if (tt > 4.5f) gagMsg = "Sneller dan de file, haha."; break;
    case 34: g_fx = 250 - tt * 55; gdir((g_fx - 120) / 100, -.4f); if (tt > 2.4f && tt < 3.4f) { gagMood = STARTLED; gagMsg = "BOE! Aaah!"; if (!g_b) { g_b = true; burstAt(120, 110, 10); } fdy = -6; } else if (tt >= 3.4f) { gagMood = HAPPY; gagMsg = "Haha, een lief spookje."; } break;
    case 35: gdir(.6f, .3f); g_d = fminf(1, tt * 2); break;
  }
}
static void gagDraw() {
  if (gagAct < 0) return;
  float tt = t - gagT0;
  switch (gagAct) {
    case 1: if (tt < 4.8f) { circle(g_fx, g_fy, 2.6f, EYE); float f = sinf(tt * 60) > 0 ? 4 : 2; ellipseF(g_fx - 4, g_fy - 3, 4, f, 0x9CF3); ellipseF(g_fx + 4, g_fy - 3, 4, f, 0x9CF3); } break;
    case 2: if (g_r > 0) { float x = 120 + sinf(tt * 3) * 2, y = 182 - g_r * .4f + faceOy * .5f; boxAdd(x - g_r, y - g_r, x + g_r, y + g_r); for (int i = 0; i < 4; i++) frame->drawCircle(X(x), Y(y), S(g_r) - i, EYE); } break;
    case 9: if (fxTongue) { float y = 174 + faceOy * .5f; roundRect(112 + faceOx * .5f, y, 16, 13, 6, 0xF986); line(120 + faceOx * .5f, y + 2, 120 + faceOx * .5f, y + 9, 0xB000, 1); } break;
    case 13: if (tt <= 5) for (int i = 0; i < 3; i++) { float a = tt * 4.2f + i * PI2 / 3; circle(120 + cosf(a) * 46, 52 - fabsf(sinf(a)) * 26, 5, i == 1 ? GOLD : EYE); } break;
    case 15: { float x = 120 + sinf(tt) * 6;
      if (tt < 5.2f) { circle(x - 16, 34, 13, 0x9CF3); circle(x, 28, 16, 0x9CF3); circle(x + 17, 35, 12, 0x9CF3);
        for (int i = 0; i < 6; i++) { float y = 50 + fmodf(tt * 60 + i * 13, 34); line(x - 20 + i * 8, y, x - 22 + i * 8, y + 6, 0x7E5F, 2); } }
      if (tt > 4.6f) circle(176, 46, 12, GOLD); } break;
    case 16: { float f = fabsf(sinf(tt * 14)), x = g_fx, y = g_fy - (tt > 5.4f ? (tt - 5.4f) * 90 : 0);
      ellipseF(x - 5 * f - 1, y, 6 * f + 1, 7, 0xFC18); ellipseF(x + 5 * f + 1, y, 6 * f + 1, 7, 0xFC18);
      ellipseF(x - 4 * f, y + 7, 4 * f + 1, 4, GOLD); ellipseF(x + 4 * f, y + 7, 4 * f + 1, 4, GOLD); line(x, y - 6, x, y + 10, EYE, 2); } break;
    case 17: { float y = g_fy + faceOy; for (int k = 0; k < 2; k++) { float x = (k ? 167 : 73) + faceOx; roundRect(x - 30, y - 18, 60, 34, 10, 0x0000); frame->drawRoundRect(X(x - 30), Y(y - 18), S(60), S(34), S(10), 0x39E7); ellipseF(x - 18, y - 7, 8, 3, 0x6B4D); }
      line(103 + faceOx, y - 8, 137 + faceOx, y - 8, 0x39E7, 3); } break;
    case 18: if (tt <= 3.6f) for (int i = 0; i < 4; i++) { float a = tt * 3 + i * PI2 / 4; star(120 + cosf(a) * 50, 40 + sinf(a) * 12, 7, GOLD); } break;
    case 19: { float x = 175, y = 40 + g_d * 120; line(x, 30, x, y, EYE, 1); circle(x, y + 7, 8, RED); circle(x, y + 7, 2.5f, 0xFFFF); } break;
    case 21: if (tt <= 3.4f) for (int i = 0; i < 3; i++) { float x = 40 + i * 80 + sinf(tt * 3 + i) * 4, y = 36; line(x - 6, y, x + 6, y, 0x9EFF, 2); line(x, y - 6, x, y + 6, 0x9EFF, 2); line(x - 4, y - 4, x + 4, y + 4, 0x9EFF, 2); line(x + 4, y - 4, x - 4, y + 4, 0x9EFF, 2); } break;
    case 22: { float a = sinf(tt * 9) * .45f - .3f, hx = 196 + sinf(a) * 34, hy = 150 - cosf(a) * 34; line(196, 150, hx, hy, EYE, 6); circle(hx, hy, 10, EYE);
      for (int i = -1; i <= 1; i++) line(hx + i * 5, hy - 8, hx + i * 5 + sinf(a) * 8, hy - 8 - cosf(a) * 8, EYE, 2); } break;
    case 23: { float x = g_fx, y = 30 + sinf(tt) * 3; line(x - 70, y + 1, x - 12, y + 1, 0x632C, 1); roundRect(x - 12, y - 3, 26, 7, 3, EYE);
      triF(x - 2, y, x - 8, y + 11, x + 3, y + 1, EYE); triF(x - 8, y, x - 13, y - 8, x - 6, y - 1, EYE); } break;
    case 28: { float k = fminf(1, tt / 1.2f); static const uint16_t kl[6] = {0xF800, 0xFC00, 0xFFE0, 0x07E0, 0x041F, 0x801F};
      for (int b = 0; b < 6; b++) { float r = 64 - b * 6; int n = (int)(18 * k); for (int i = 0; i < n; i++) { float a1 = PI2 / 2 + i * PI2 / 36, a2 = a1 + PI2 / 36; line(120 + cosf(a1) * r, 96 + sinf(a1) * r, 120 + cosf(a2) * r, 96 + sinf(a2) * r, kl[b], 3); } } } break;
    case 29: { float y = tt < 2.6f ? 190 : 190 - (tt - 2.6f) * (tt - 2.6f) * 60; if (y > -30) { roundRect(170, y - 18, 12, 30, 5, 0xFFFF); triF(170, y - 16, 182, y - 16, 176, y - 30, RED); triF(170, y + 4, 164, y + 14, 170, y + 12, RED); triF(182, y + 4, 188, y + 14, 182, y + 12, RED); circle(176, y - 6, 3, 0x7E5F); if (tt >= 2.6f) { circle(176, y + 18, 4 + sinf(t * 40) * 1.5f, GOLD); circle(176, y + 24, 3, 0xFC00); } } } break;
    case 30: if (!g_b) { float x = 176 + sinf(tt * 2) * 6; line(x, g_fy + 16, x + sinf(tt * 3) * 4, g_fy + 44, 0xC618, 1); ellipseF(x, g_fy, 13, 16, 0xF8B2); triF(x - 3, g_fy + 16, x + 3, g_fy + 16, x, g_fy + 20, 0xF8B2); circle(x - 5, g_fy - 6, 3, 0xFE9F); } break;
    case 31: for (int i = 0; i < 7; i++) { float x = 120 + sinf(tt * (.7f + i * .13f) + i * 2) * 90, y = 100 + cosf(tt * (.5f + i * .11f) + i) * 70; float hl = .5f + .5f * sinf(tt * 5 + i * 1.7f); if (hl > .35f) circle(x, y, 1.5f + hl * 2.5f, hl > .7f ? 0xFFF0 : 0xAFE0); } break;
    case 32: { float x = g_fx, y = 34 + sinf(tt * 2) * 4; ellipseF(x, y, 24, 7, 0x9CF3); ellipseF(x, y - 6, 10, 7, 0x7E5F); for (int i = -2; i <= 2; i++) circle(x + i * 9, y + 2, 1.8f, (int)(tt * 6 + i) % 2 ? GOLD : RED);
      if (tt > 2.2f && tt < 3.8f) { triF(x - 6, y + 7, x + 6, y + 7, x + 30, y + 150, 0x3A7F); triF(x - 6, y + 7, x - 30, y + 150, x + 30, y + 150, 0x3A7F); } } break;
    case 33: { float x = g_fx, y = 196; ellipseF(x, y + 6, 16, 4, 0xC5E0); circle(x + 2, y - 2, 9, 0xBB24); circle(x + 2, y - 2, 5, 0xDD89); circle(x + 2, y - 2, 2, 0xBB24); line(x - 12, y + 2, x - 16, y - 8, 0xC5E0, 2); line(x - 14, y + 2, x - 20, y - 6, 0xC5E0, 2); circle(x - 16, y - 8, 1.5f, 0x0000); } break;
    case 34: { float x = g_fx, y = 70 + sinf(tt * 3) * 6; circle(x, y, 18, 0xF7BF); roundRect(x - 18, y, 36, 20, 2, 0xF7BF); for (int i = 0; i < 4; i++) circle(x - 13.5f + i * 9, y + 20, 4.5f, 0xF7BF); circle(x - 6, y - 2, 3, 0x0000); circle(x + 6, y - 2, 3, 0x0000); ellipseF(x, y + 8, 3, 4, 0x0000); } break;
    case 35: { float x = 190, y = 168, r = 20 * g_d; if (r < 1) break; ellipseF(x - r * .35f, y, r * .55f, r * .8f, 0xFBC0); ellipseF(x + r * .35f, y, r * .55f, r * .8f, 0xFBC0); ellipseF(x, y, r * .6f, r * .85f, 0xFBC0); roundRect(x - 2, y - r * .95f, 4, r * .3f, 1, 0x3BE5);
      uint16_t vl = sinf(t * 9) > -.3f ? 0xFF10 : 0xFDA0; triF(x - 9 * g_d, y - 3 * g_d, x - 4 * g_d, y - 9 * g_d, x - 1 * g_d, y - 3 * g_d, vl); triF(x + 9 * g_d, y - 3 * g_d, x + 4 * g_d, y - 9 * g_d, x + 1 * g_d, y - 3 * g_d, vl);
      triF(x - 11 * g_d, y + 4 * g_d, x + 11 * g_d, y + 4 * g_d, x, y + 12 * g_d, vl); } break;
    case 27: if (tt <= 4.4f) { if (tt > 0) circle(178, 64, 3, EYE); if (tt > .4f) circle(188, 50, 4.5f, EYE); if (tt > .8f) circle(200, 32, 8, EYE); } break;
  }
}

// ===================== v21: G-meter, 0-100 vlag en ritjes =====================
static float tripKm = 0, tripMax = 0, tripG = 0, tripBest = 0; static uint32_t tripStart = 0, stilSinds = 0, rijdtSinds = 0, ritKlaarTot = 0;
static float flagTijd = 0; static bool flagRec = false;
static void driveStep(float dt) {
  bool ok; float v = spdNow(&ok); uint32_t nu = millis();
  float gNu = sqrtf(leanX * leanX + pushZ * pushZ);
  if (new80) { new80 = false; float tt = res80; if (rec80 <= 0 || tt < rec80) { rec80 = tt; snprintf(dat80, sizeof dat80, "%.2s-%.2s-%.2s", gpsDate, gpsDate + 2, gpsDate + 4); buddyPrefs.putFloat("b80", rec80); buddyPrefs.putString("d80", dat80); } logRegel("0-80: %.1f s", tt); }
  if (new100) { new100 = false; float tt = res100; flagTijd = tt; flagRec = rec100 <= 0 || tt < rec100; flagTot = nu + 9000;
    if (flagRec) { rec100 = tt; snprintf(dat100, sizeof dat100, "%.2s-%.2s-%.2s", gpsDate, gpsDate + 2, gpsDate + 4); buddyPrefs.putFloat("b100", rec100); buddyPrefs.putString("d100", dat100); }
    if (tripBest <= 0 || tt < tripBest) tripBest = tt;
    for (int i = 0; i < 10; i++) addParticleAt(1, frand(30, 210), frand(40, 120), frand(-20, 20), frand(-30, 10), 1.2f, frand(5, 9));
    char m[24]; snprintf(m, sizeof m, "SPRINT:%.1f", tt); bleSend(m); logRegel("0-100: %.1f s%s", tt, flagRec ? " RECORD" : ""); }
  if (!ok) return;
  if (v > 5) { stilSinds = 0; if (!rijdtSinds) rijdtSinds = nu; } else { rijdtSinds = 0; if (!stilSinds) stilSinds = nu; }
  if (!tripActief && rijdtSinds && nu - rijdtSinds > 5000) { tripActief = true; tripKm = tripMax = tripG = tripBest = 0; tripStart = nu; logRegel("Rit begonnen"); }
  if (tripActief) {
    tripKm += v * dt / 3600.f; if (v > tripMax) tripMax = v; if (gNu > tripG && gNu < 1.3f && v > 10) tripG = gNu;
    if (stilSinds && nu - stilSinds > 180000) {   // 3 minuten stil = rit klaar
      tripActief = false;
      if (tripKm > .3f) {
        totKm += tripKm; totRit++; buddyPrefs.putFloat("km", totKm); buddyPrefs.putUInt("rit", totRit);
        if (tripG > recG) { recG = tripG; buddyPrefs.putFloat("gmax", recG); }
        ritKlaarTot = nu + 12000; logRegel("Rit klaar: %.1f km, max %.0f km/u, %.2f G", tripKm, tripMax, tripG);
      }
    }
  }
}
static void drawGmeter() {
  bool ok; float v = spdNow(&ok);
  if (!imuReady || !ok || v < 5) return;
  float cx = 120, cy = 20, r = 9;
  boxAdd(cx - r - 2, cy - r - 2, cx + r + 2, cy + r + 2);
  frame->drawCircle(X(cx), Y(cy), S(r), 0x4208); frame->drawCircle(X(cx), Y(cy), S(r * .5f), 0x2945);
  float dx = fmaxf(-1, fminf(1, -leanX * LEAN_RICHTING / .6f)) * r, dy = fmaxf(-1, fminf(1, -pushZ / .6f)) * r, g = sqrtf(leanX * leanX + pushZ * pushZ);
  circle(cx + dx, cy + dy, 2.6f, g < .3f ? 0x07E8 : (g < .5f ? GOLD : RED));
}
static void drawFlag() {
  if ((int32_t)(flagTot - millis()) <= 0) return;
  float w = sinf(t * 8) * 3;   // wapperen
  line(196 + faceOx * .3f, 88, 196 + faceOx * .3f, 176, 0xC618, 3);
  for (int i = 0; i < 5; i++) for (int j = 0; j < 3; j++) {
    float x = 160 + i * 7 + faceOx * .3f, y = 88 + j * 7 + w * (i / 5.f);
    roundRect(x, y, 7, 7, 0, ((i + j) & 1) ? 0x0000 : 0xFFFF);
  }
  char tx[16]; snprintf(tx, sizeof tx, "%.1f s", flagTijd); for (char *q = tx; *q; q++) if (*q == '.') *q = ',';
  int tw = strlen(tx) * 18; boxAdd(40, 24, 200, 60);
  frame->setTextColor(GOLD); frame->setTextSize(3); frame->setCursor(X(120) - tw / 2, Y(30)); frame->print(tx);
  if (flagRec && (millis() / 300) % 2) { frame->setTextSize(2); frame->setTextColor(0xFFFF); frame->setCursor(X(120) - 42, Y(48)); frame->print("RECORD!"); }
}
static void drawRitKlaar() {
  if ((int32_t)(ritKlaarTot - millis()) <= 0) return;
  boxAdd(30, 44, 210, 100);
  roundRect(32, 44, 176, 54, 14, 0x10A2);
  char r1[32], r2[32]; snprintf(r1, sizeof r1, "Rit: %.1f km", tripKm); snprintf(r2, sizeof r2, "max %d  %.2f G", (int)tripMax, tripG);
  for (char *q = r1; *q; q++) if (*q == '.') *q = ','; for (char *q = r2; *q; q++) if (*q == '.') *q = ',';
  frame->setTextSize(2); frame->setTextColor(EYE); frame->setCursor(X(120) - strlen(r1) * 6, Y(52)); frame->print(r1);
  frame->setTextColor(GOLD); frame->setCursor(X(120) - strlen(r2) * 6, Y(74)); frame->print(r2);
}

// ===================== v22: grote cijfers, klok, slapen en nachtstand =====================
static void dikLijn(float x1, float y1, float x2, float y2, float w, uint16_t c) {
  boxAdd(fminf(x1, x2) - w, fminf(y1, y2) - w, fmaxf(x1, x2) + w, fmaxf(y1, y2) + w);
  float dx = x2 - x1, dy = y2 - y1, l = sqrtf(dx * dx + dy * dy); if (l < .01f) l = .01f;
  float nx = -dy / l * w / 2, ny = dx / l * w / 2;
  frame->fillTriangle(X(x1 + nx), Y(y1 + ny), X(x2 + nx), Y(y2 + ny), X(x2 - nx), Y(y2 - ny), c);
  frame->fillTriangle(X(x1 + nx), Y(y1 + ny), X(x2 - nx), Y(y2 - ny), X(x1 - nx), Y(y1 - ny), c);
  frame->fillCircle(X(x1), Y(y1), S(w / 2), c); frame->fillCircle(X(x2), Y(y2), S(w / 2), c);
}
// segmenten: a=boven b=rechtsboven c=rechtsonder d=onder e=linksonder f=linksboven g=midden
static const uint8_t SEG[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
static float segBreed(char ch, float h) { return ch == ':' ? h * .22f : h * .56f; }
static void segCijfer(char ch, float x, float y, float h, uint16_t c) {   // x,y = linksboven
  float w = h * .56f, th = h * .15f, m = th * .5f;
  if (ch == ':') { circle(x + h * .11f, y + h * .3f, th * .55f, c); circle(x + h * .11f, y + h * .72f, th * .55f, c); return; }
  uint8_t s = ch == '-' ? 0x40 : (ch >= '0' && ch <= '9' ? SEG[ch - '0'] : 0x40);
  float L = x + m, R = x + w - m, T = y + m, M = y + h / 2, B = y + h - m;
  if (s & 0x01) dikLijn(L, T, R, T, th, c);
  if (s & 0x02) dikLijn(R, T, R, M, th, c);
  if (s & 0x04) dikLijn(R, M, R, B, th, c);
  if (s & 0x08) dikLijn(L, B, R, B, th, c);
  if (s & 0x10) dikLijn(L, M, L, B, th, c);
  if (s & 0x20) dikLijn(L, T, L, M, th, c);
  if (s & 0x40) dikLijn(L, M, R, M, th, c);
}
static void segTekst(const char *t, float cx, float cy, float h, uint16_t c) {   // gecentreerd op cx,cy
  float gap = h * .16f, tot = 0; int n = strlen(t);
  for (int i = 0; i < n; i++) tot += segBreed(t[i], h) + (i ? gap : 0);
  float x = cx - tot / 2, y = cy - h / 2;
  for (int i = 0; i < n; i++) { segCijfer(t[i], x, y, h, c); x += segBreed(t[i], h) + gap; }
}
// Nederlandse tijd uit de GPS (UTC + 1, of + 2 in de zomertijd)
static bool ntpOk() { time_t nu = time(nullptr); return nu > 1700000000; }
static bool klokOkGps() { int jr = (gpsDate[4] >= '0' && gpsDate[5] >= '0') ? (gpsDate[4] - '0') * 10 + (gpsDate[5] - '0') : 0; return gpsHH >= 0 && jr >= 24 && jr < 70 && millis() - gpsTijdMs < 120000; }  // 1980 = GPS weet de tijd nog niet
static int dagVanWeek(int d, int m, int y) { static const int tt[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4}; if (m < 3) y--; return (y + y / 4 - y / 100 + y / 400 + tt[m - 1] + d) % 7; }
static bool klokOk() { return klokOkGps() || ntpOk(); }
static void lokaleTijd(int &hh, int &mm) {
  if (!klokOkGps() && ntpOk()) { struct tm tmv; time_t nu = time(nullptr); localtime_r(&nu, &tmv); hh = tmv.tm_hour; mm = tmv.tm_min; return; }
  auto twee = [](int i) { return (gpsDate[i] >= '0' && gpsDate[i + 1] >= '0') ? (gpsDate[i] - '0') * 10 + (gpsDate[i + 1] - '0') : 1; };
  int d = twee(0), mo = twee(2), y = 2000 + twee(4);
  uint32_t extra = (millis() - gpsTijdMs) / 60000;
  int min = gpsHH * 60 + gpsMM + (int)extra;
  bool zomer = false;
  if (mo > 3 && mo < 10) zomer = true;
  else if (mo == 3 || mo == 10) { int laatsteZo = 31 - dagVanWeek(31, mo, y); bool na = d > laatsteZo || (d == laatsteZo && gpsHH >= 1); zomer = (mo == 3) ? na : !na; }
  min += zomer ? 120 : 60; min %= 1440;
  hh = min / 60; mm = min % 60;
}
static uint32_t laatsteActie = 0; static bool slaapt = false; static uint8_t huidigLicht = 0;
// v26: echt uit (diepe slaap) en weer aan als de auto beweegt (instappen, deur, rijden).
// De bewegingssensor blijft wakker en maakt het bordje wakker via zijn INT2-pin (GPIO21).
static void qmiWakkerBijBeweging() {
  if (!i2cLock(200)) return;
  qmiW(0x60, 0xB0); delay(30);
  qmiW(0x02, 0x40);                 // CTRL1: auto-increment
  qmiW(0x08, 0x00);                 // CTRL7: sensoren uit tijdens instellen
  qmiW(0x03, (0x02 << 4) | 12);     // CTRL2: +-8 G, laag-vermogen 128 Hz
  qmiW(0x0B, 80);                   // CAL1_L: drempel 80 mg
  qmiW(0x0C, (0x03 << 6) | 0x20);   // CAL1_H: INT2, beginwaarde 1 (hoog), even negeren na start
  qmiW(0x0A, 0x08);                 // CTRL9: WoM-instelling schrijven
  for (int i = 0; i < 100; i++) { Wire.beginTransmission(0x6B); Wire.write(0x2D); Wire.endTransmission(false); if (Wire.requestFrom((int)0x6B, 1) == 1 && (Wire.read() & 0x80)) break; delay(2); }
  qmiW(0x0A, 0x00);                 // bevestigen
  delay(5);
  qmiW(0x08, 0x01);                 // versnellingsmeter aan
  qmiW(0x02, 0x40 | 0x10);          // INT2 aan
  i2cUnlock();
}
static void diepSlapen(const char *reden) {
  logRegel("uit (diepe slaap): %s. Wakker bij beweging.", reden);
  delay(50);
  xSemaphoreTake(flushIdle, pdMS_TO_TICKS(1000));   // wachten tot het scherm klaar is met versturen
  gfx->displayOff();
  digitalWrite(PA, LOW);                            // versterker uit
  if (imuReady) qmiWakkerBijBeweging();
  delay(300);
  rtc_gpio_pullup_en(GPIO_NUM_21);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_21, 0);
  esp_deep_sleep_start();
}
static void slaapStap() {
  bool ok; float v = spdNow(&ok); uint32_t nu = millis();
  if (v > 3 || fabsf(pushZ) > .12f || hop > .5f || !laatsteActie) laatsteActie = nu;
  // v26: deur dicht en daarna stil (en je telefoon niet meer verbonden) = je bent uitgestapt -> uit
  bool geparkeerd = !ok || v < 1;
  if (geparkeerd && laatsteDeur && nu - laatsteDeur > 60000 && nu - laatsteDeur < 120000 && nu - laatsteBeweging > 45000
      && nu - laatsteActie > 45000 && !bleConnected && !menuOpen && otaPct < 0) diepSlapen("deur dicht en daarna stil");
  if (geparkeerd && nu - laatsteActie > 10UL * 60000 && nu - laatsteBeweging > 60000 && !menuOpen && otaPct < 0) diepSlapen("10 minuten niets");
  bool moetSlapen = nu - laatsteActie > 3UL * 60000 && !menuOpen;   // 3 minuten niks = dommelen (scherm zacht)
  if (moetSlapen != slaapt) { slaapt = moetSlapen; if (!slaapt) { mood = STARTLED; lastMood = -1; idleMoodUntilMs = nu + 1500; } }
  int hh = 12, mm = 0; if (klokOk()) lokaleTijd(hh, mm);
  bool nacht = hh >= 21 || hh < 7;
  uint8_t doel = slaapt ? 12 : (nacht ? (uint8_t)(helderheid * .45f) : helderheid);
  if (doel != huidigLicht) { huidigLicht = doel; wilLicht = doel ? doel : 1; }
}

// ===================== v25: zelfstandig via wifi (hotspot van je telefoon) =====================
// Haalt zelf: maximumsnelheden per weg (Rijkswaterstaat-tegels), alle vaste flitsers en trajectcontroles,
// de tijd (internet) en nieuwe versies van zichzelf (update via wifi, alleen als hij stilstaat).
// Tegels worden in het geheugen bewaard, dus op bekende routes werkt de limiet ook zonder internet.
struct Weg { uint16_t lim, clim, tb, te; uint32_t p0; uint16_t n; };
static Weg *wegen = nullptr; static float *wegPt = nullptr; static int wegN = 0; static char tegelNu[16] = "";
static volatile int wegLimiet = 0; static volatile uint32_t wegLimT = 0;
struct Traject { float la1, lo1, la2, lo2; int16_t lim; };
static Traject *trajecten = nullptr; static int trajN = 0;
static volatile bool trajActief = false; static volatile float trajGem = 0; static volatile int trajLim = 0;

static void jWs(JP &j) { while (j.p < j.e && (*j.p == ' ' || *j.p == '\n' || *j.p == '\r' || *j.p == '\t')) j.p++; }
static bool jIs(JP &j, char c) { jWs(j); if (j.p < j.e && *j.p == c) { j.p++; return true; } return false; }
static double jNum(JP &j) { jWs(j); char *q; double v = strtod(j.p, &q); if (q == j.p) { if (!strncmp(j.p, "null", 4)) j.p += 4; return 0; } j.p = q; return v; }
static void jSkipStr(JP &j) { jWs(j); if (j.p < j.e && *j.p == '"') { j.p++; while (j.p < j.e && *j.p != '"') { if (*j.p == '\\') j.p++; j.p++; } j.p++; } }
static bool jZoek(JP &j, const char *sleutel) { const char *q = strstr(j.p, sleutel); if (!q) return false; j.p = q + strlen(sleutel); return true; }

static uint8_t *haalUrl(const char *url, size_t &len) {
  len = 0; WiFiClientSecure cl; cl.setInsecure(); HTTPClient h; h.setTimeout(15000);
  if (!h.begin(cl, url)) return nullptr;
  int code = h.GET(); if (code != 200) { h.end(); logRegel("net: %s -> %d", url + 34, code); return nullptr; }
  int n = h.getSize(); size_t cap = n > 0 ? n + 1 : 600000;
  uint8_t *buf = (uint8_t *)heap_caps_malloc(cap, MALLOC_CAP_SPIRAM); if (!buf) { h.end(); return nullptr; }
  WiFiClient *st = h.getStreamPtr(); size_t got = 0; uint32_t t0 = millis();
  while ((n < 0 || got < (size_t)n) && millis() - t0 < 40000 && got < cap - 1) {
    size_t a = st->available();
    if (a) { if (got + a > cap - 1) a = cap - 1 - got; got += st->readBytes(buf + got, a); }
    else { if (!h.connected() && n < 0) break; vTaskDelay(pdMS_TO_TICKS(5)); }
  }
  buf[got] = 0; len = got; h.end(); return buf;
}
static void bewaar(const char *pad, const uint8_t *b, size_t n) { if (!logFs) return; File f = logFs->open(pad, FILE_WRITE); if (f) { f.write(b, n); f.close(); } }
static uint8_t *leesBestand(const char *pad, size_t &n) {
  n = 0; if (!logFs) return nullptr; File f = logFs->open(pad, FILE_READ); if (!f) return nullptr;
  n = f.size(); uint8_t *b = (uint8_t *)heap_caps_malloc(n + 1, MALLOC_CAP_SPIRAM); if (!b) { f.close(); return nullptr; }
  f.read(b, n); b[n] = 0; f.close(); return b;
}
static bool zetTegel(const char *key, const uint8_t *txt, size_t len) {
  JP j{(const char *)txt, (const char *)txt + len};
  if (!jZoek(j, "\"w\":[")) return false;
  int maxW = len / 25 + 8, maxP = len / 7 + 16;
  Weg *nw = (Weg *)heap_caps_malloc(sizeof(Weg) * maxW, MALLOC_CAP_SPIRAM); float *np = (float *)heap_caps_malloc(sizeof(float) * maxP, MALLOC_CAP_SPIRAM);
  if (!nw || !np) { free(nw); free(np); return false; }
  int w = 0, pc = 0;
  while (w < maxW && jIs(j, '[')) {
    Weg g{}; g.lim = (uint16_t)jNum(j); jIs(j, ','); jNum(j); jIs(j, ',');
    g.p0 = pc; g.n = 0;
    if (jIs(j, '[')) { while (pc + 2 < maxP) { if (jIs(j, ']')) break; float la = jNum(j); jIs(j, ','); float lo = jNum(j); jIs(j, ','); np[pc++] = la; np[pc++] = lo; g.n++; } }
    jIs(j, ',');
    if (jIs(j, '[')) { g.clim = (uint16_t)jNum(j); jIs(j, ','); g.tb = (uint16_t)jNum(j); jIs(j, ','); g.te = (uint16_t)jNum(j); jIs(j, ']'); } else jNum(j);   // null
    jIs(j, ','); jNum(j); jIs(j, ','); jNum(j); jIs(j, ','); jSkipStr(j); jIs(j, ']');
    if (g.n >= 2) nw[w++] = g;
    jIs(j, ',');
  }
  Weg *ow = wegen; float *op = wegPt; wegN = 0; wegen = nw; wegPt = np; wegN = w; free(ow); free(op);
  strncpy(tegelNu, key, sizeof tegelNu - 1);
  logRegel("tegel %s: %d wegvakken", key, w);
  return true;
}
static void tegelKey(double lat, double lon, char *uit, size_t n) { snprintf(uit, n, "%d_%d", (int)floor(lat / 0.03), (int)floor(lon / 0.05)); }
static void laadTegel(const char *key, bool online) {
  char pad[32]; snprintf(pad, sizeof pad, "/t_%s.json", key); size_t n; uint8_t *b = leesBestand(pad, n);
  if (b && n > 10) { bool ok = zetTegel(key, b, n); free(b); if (ok) return; } else free(b);
  if (!online) return;
  char url[128]; snprintf(url, sizeof url, DATA_BASIS "t/%s.json", key);
  b = haalUrl(url, n);
  if (b && n > 10) { if (zetTegel(key, b, n)) bewaar(pad, b, n); }
  else if (b == nullptr) { /* geen tegel = geen wegen met limiet hier; leeg bestand bewaren zodat hij niet blijft vragen */ }
  free(b);
}
static void laadFlitsers(bool online) {
  size_t n; uint8_t *b = online ? haalUrl(DATA_BASIS "flitsers.json", n) : nullptr; bool vers = b != nullptr;
  if (!b) b = leesBestand("/flitsers.json", n);
  if (!b) return;
  JP j{(const char *)b, (const char *)b + n};
  int max = n / 20 + 8; NetCam *nc = (NetCam *)heap_caps_malloc(sizeof(NetCam) * max, MALLOC_CAP_SPIRAM); int k = 0;
  if (nc && jZoek(j, "\"c\":[")) {
    while (k < max && jIs(j, '[')) { NetCam c; c.lat = jNum(j); jIs(j, ','); c.lon = jNum(j); jIs(j, ','); c.lim = (int16_t)jNum(j); jIs(j, ','); c.type = (uint8_t)jNum(j); while (j.p < j.e && *j.p != ']') j.p++; jIs(j, ']'); nc[k++] = c; jIs(j, ','); }
  }
  JP j2{(const char *)b, (const char *)b + n}; int tmax = 200, t = 0; Traject *tr = (Traject *)heap_caps_malloc(sizeof(Traject) * tmax, MALLOC_CAP_SPIRAM);
  if (tr && jZoek(j2, "\"t\":[")) {
    while (t < tmax && jIs(j2, '[')) { Traject x; x.la1 = jNum(j2); jIs(j2, ','); x.lo1 = jNum(j2); jIs(j2, ','); x.la2 = jNum(j2); jIs(j2, ','); x.lo2 = jNum(j2); jIs(j2, ','); x.lim = (int16_t)jNum(j2); jIs(j2, ','); jSkipStr(j2); jIs(j2, ']'); tr[t++] = x; jIs(j2, ','); }
  }
  if (k > 100) { NetCam *oc = netCams; netCamN = 0; netCams = nc; netCamN = k; free(oc); } else free(nc);
  if (t > 0) { Traject *ot = trajecten; trajN = 0; trajecten = tr; trajN = t; free(ot); } else free(tr);
  if (vers && k > 100) bewaar("/flitsers.json", b, n);
  logRegel("flitsers: %d vast, %d trajectcontroles (%s)", k, t, vers ? "nieuw van internet" : "uit geheugen");
  free(b);
}
static float afstandM(float la1, float lo1, float la2, float lo2) { float kx = 111320.f * cosf(la1 * 0.0174533f); float dx = (lo2 - lo1) * kx, dy = (la2 - la1) * 110540.f; return sqrtf(dx * dx + dy * dy); }
static void bepaalWeg() {
  if (!gpsFix || !wegN || !wegen) return;
  float lat = gpsLat, lon = gpsLon, hd = gpsKoers, v = gpsSpeedKmh;
  float kx = 111320.f * cosf(lat * 0.0174533f), ky = 110540.f, best = 1e9; int bi = -1;
  for (int i = 0; i < wegN; i++) {
    const Weg &g = wegen[i]; const float *p = wegPt + g.p0;
    for (int k = 0; k + 1 < g.n; k++) {
      float ax = (p[k * 2 + 1] - lon) * kx, ay = (p[k * 2] - lat) * ky, bx = (p[k * 2 + 3] - lon) * kx, by = (p[k * 2 + 2] - lat) * ky;
      if ((ax > 60 && bx > 60) || (ax < -60 && bx < -60) || (ay > 60 && by > 60) || (ay < -60 && by < -60)) continue;
      float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy, tt = l2 > 0 ? -(ax * dx + ay * dy) / l2 : 0; tt = fmaxf(0, fminf(1, tt));
      float px = ax + tt * dx, py = ay + tt * dy, d = sqrtf(px * px + py * py);
      if (d > 40) continue;
      float br = atan2f(dx, dy) * 57.2958f, df = fabsf(fmodf(fabsf(br - hd), 180.f)); if (df > 90) df = 180 - df;
      float score = d + ((v > 8 && df > 35) ? 25 : 0);
      if (score < best) { best = score; bi = i; }
    }
  }
  if (bi < 0) return;
  const Weg &g = wegen[bi]; int lim = g.lim;
  if (g.clim && klokOk()) { int hh, mm; lokaleTijd(hh, mm); int m = hh * 60 + mm; bool in = g.tb < g.te ? (m >= g.tb && m < g.te) : (m >= g.tb || m < g.te); if (in) lim = g.clim; }
  wegLimiet = lim; wegLimT = millis();
}
static void trajectStap(float dt) {
  static int ti = -1; static float afstand = 0, tijd = 0, rechtL = 0;
  if (!gpsFix || !trajN || !trajecten) { return; }
  float lat = gpsLat, lon = gpsLon, v = gpsSpeedKmh;
  if (ti < 0) {
    if (v < 30) return;
    for (int i = 0; i < trajN; i++) if (afstandM(lat, lon, trajecten[i].la1, trajecten[i].lo1) < 150) {
      // alleen starten als je richting het eind rijdt
      float br = atan2f((trajecten[i].lo2 - lon) * cosf(lat * 0.0174533f), trajecten[i].la2 - lat) * 57.2958f; float df = fabsf(fmodf(fabsf(br - gpsKoers), 360.f)); if (df > 180) df = 360 - df;
      if (df > 60) continue;
      ti = i; afstand = tijd = 0; rechtL = afstandM(trajecten[i].la1, trajecten[i].lo1, trajecten[i].la2, trajecten[i].lo2);
      trajLim = trajecten[i].lim; trajActief = true; trajGem = v; logRegel("traject begonnen (%d km/u, %.1f km)", trajLim, rechtL / 1000); break;
    }
    return;
  }
  afstand += v / 3.6f * dt; tijd += dt; if (tijd > 2) trajGem = afstand / tijd * 3.6f;
  bool eind = afstandM(lat, lon, trajecten[ti].la2, trajecten[ti].lo2) < 150 || afstand > rechtL * 1.6f + 2000 || tijd > 3600;
  if (eind) { logRegel("traject klaar: gemiddeld %.0f bij %d", trajGem, trajLim); ti = -1; trajActief = false; }
}
static void checkOta() {
  size_t n; uint8_t *b = haalUrl(FW_BASIS "versie.txt", n); if (!b) return;
  int v = atoi((char *)b); free(b);
  if (v <= FW_VERSIE) { logRegel("update: dit is de nieuwste (v%d)", FW_VERSIE); return; }
  logRegel("update: v%d -> v%d wordt binnengehaald", FW_VERSIE, v);
  otaPct = 0;
  WiFiClientSecure cl; cl.setInsecure();
  httpUpdate.onProgress([](int cur, int tot) { if (tot > 0) otaPct = cur * 100 / tot; });
  httpUpdate.rebootOnUpdate(true);
  t_httpUpdate_return r = httpUpdate.update(cl, FW_BASIS "firmware.bin");
  logRegel("update mislukt (%d): %s", (int)r, httpUpdate.getLastErrorString().c_str()); otaPct = -1;
}
// v28: files/werk uit het Worker-pakketje
struct Jam { char k; int16_t d; int32_t l; char w[6]; uint16_t p0; uint8_t n; };
static Jam *jams = nullptr; static float *jamPt = nullptr; static volatile int jamN = 0;
static volatile int lokFileD = -1, lokFileMin = 0; static volatile bool lokWerk = false; static volatile uint32_t lokT = 0;
static uint32_t pakketSinds = 0;
static const char *grpZin(const char *s) {
  if (!strcmp(s, "zwaai")) return "zwaait!"; if (!strcmp(s, "koffie")) return ": koffie?"; if (!strcmp(s, "koffieJa")) return ": ja, koffie!";
  if (!strcmp(s, "koffieStraks")) return ": straks koffie"; if (!strcmp(s, "file")) return "staat in de file"; if (!strcmp(s, "pech")) return "heeft pech!";
  if (!strcmp(s, "hart")) return "stuurt een hartje"; if (!strcmp(s, "thuis")) return "is thuis"; if (!strcmp(s, "mijlpaal")) return ": mijlpaal!"; if (!strcmp(s, "feli")) return "feliciteert!";
  return "test de groep";
}
// v29: weer en waarschuwingen (Open-Meteo, gratis, geen sleutel nodig)
static volatile float wxTemp = -99; static volatile int wxCode = -1, wxRegenOver = -1; static volatile uint32_t wxT = 0;
static void haalWeer() {
  char url[220]; snprintf(url, sizeof url, "https://api.open-meteo.com/v1/forecast?latitude=%.3f&longitude=%.3f&current=temperature_2m,weather_code,precipitation&minutely_15=precipitation&forecast_minutely_15=4&timezone=auto", (double)gpsLat, (double)gpsLon);
  size_t n; uint8_t *b = haalUrl(url, n); if (!b) return;
  JP j{(const char *)b, (const char *)b + n};
  if (jZoek(j, "\"current\":{")) {
    JP c = j; if (jZoek(c, "\"temperature_2m\":")) wxTemp = jNum(c);
    c = j; if (jZoek(c, "\"weather_code\":")) wxCode = (int)jNum(c);
  }
  JP m{(const char *)b, (const char *)b + n}; int over = -1;
  if (jZoek(m, "\"minutely_15\":{") && jZoek(m, "\"precipitation\":[")) { for (int i = 0; i < 4; i++) { float v = jNum(m); jIs(m, ','); if (v >= 0.2f && over < 0) over = i * 15; } }
  wxRegenOver = over; wxT = millis();
  logRegel("weer: %.1f graden, code %d, regen over %d min", (double)wxTemp, (int)wxCode, over);
  free(b);
}
static bool wxVers() { return wxT && millis() - wxT < 45UL * 60000; }
static bool wxGlad() { return wxVers() && wxTemp <= 1.0f && (wxCode >= 45 || wxRegenOver >= 0); }   // vorst + neerslag/mist
static void haalPakket() {
  if (!wCfg.url[0]) return;
  char url[256]; snprintf(url, sizeof url, "%s/scherm?lat=%.5f&lon=%.5f&g=%s&van=%s&sinds=%lu", wCfg.url, (double)gpsLat, (double)gpsLon, wCfg.grp, wCfg.van, (unsigned long)pakketSinds);
  size_t n; uint8_t *b = haalUrl(url, n); if (!b) return;
  int maxC = 300, maxJ = 60, maxP = 1200;
  NetCam *nc = (NetCam *)heap_caps_malloc(sizeof(NetCam) * maxC, MALLOC_CAP_SPIRAM); Jam *nj = (Jam *)heap_caps_malloc(sizeof(Jam) * maxJ, MALLOC_CAP_SPIRAM); float *np = (float *)heap_caps_malloc(sizeof(float) * maxP, MALLOC_CAP_SPIRAM);
  if (!nc || !nj || !np) { free(nc); free(nj); free(np); free(b); return; }
  int c = 0, j = 0, pc = 0;
  for (char *r = strtok((char *)b, "\n"); r; r = strtok(nullptr, "\n")) {
    if (r[0] == 'T' && r[1] == '|') { if (!pakketSinds) pakketSinds = strtoul(r + 2, nullptr, 10); }
    else if (r[0] == 'M' && r[1] == '|' && c < maxC) { char *q; NetCam x; x.lat = strtof(r + 2, &q); x.lon = strtof(q + 1, nullptr); x.lim = 0; x.type = 1; nc[c++] = x; }
    else if (r[0] == 'G' && r[1] == '|' && c < maxC) { char *q; NetCam x; x.lat = strtof(r + 2, &q); x.lon = strtof(q + 1, &q); x.lim = (int16_t)strtol(q + 1, &q, 10); x.type = (q[1] == 'm') ? 3 : 2; nc[c++] = x; }
    else if (r[0] == 'J' && r[1] == '|' && j < maxJ) {
      char *f[6] = {0}; int k = 0; f[k++] = r + 2; for (char *q = r + 2; *q && k < 6; q++) if (*q == '|') { *q = 0; f[k++] = q + 1; }
      if (k < 6) continue;
      Jam x{}; x.k = !strcmp(f[0], "jam") ? 'j' : (!strcmp(f[0], "lane") ? 'w' : (!strcmp(f[0], "closure") || !strcmp(f[0], "ramp") ? 'a' : (!strcmp(f[0], "accident") ? 'o' : 's')));
      x.d = atoi(f[1]); x.l = atol(f[2]); strncpy(x.w, f[3], 5); x.p0 = pc; x.n = 0;
      for (char *q = f[4]; *q && pc + 2 < maxP && x.n < 20;) { char *e; float la = strtof(q, &e); if (*e != ',') break; float lo = strtof(e + 1, &e); np[pc++] = la; np[pc++] = lo; x.n++; if (*e == ';') q = e + 1; else break; }
      if (x.n) nj[j++] = x;
    }
    else if (r[0] == 'B' && r[1] == '|') {
      char *f[3] = {0}; int k = 0; f[k++] = r + 2; for (char *q = r + 2; *q && k < 3; q++) if (*q == '|') { *q = 0; f[k++] = q + 1; }
      if (k < 3) continue; uint32_t tt = strtoul(f[0], nullptr, 10); if (tt > pakketSinds) pakketSinds = tt;
      if (!phoneFresh()) { snprintf(grpTekst, sizeof grpTekst, "%s %s", f[2], grpZin(f[1])); grpTot = millis() + 7000; logRegel("groep: %s", grpTekst); }
    }
  }
  NetCam *oc = extraCams; extraCamN = 0; extraCams = nc; extraCamN = c; free(oc);
  Jam *oj = jams; float *op = jamPt; jamN = 0; jams = nj; jamPt = np; jamN = j; free(oj); free(op);
  free(b);
}
static void fileVoorStap() {   // ligt er een file of werk voor je (binnen 3 km, in je rijrichting)?
  if (!gpsFix || !jamN || !jams) { if (millis() - lokT > 4000) { lokFileD = -1; lokWerk = false; } return; }
  float lat = gpsLat, lon = gpsLon, hd = gpsKoers; int bestF = -1, bestMin = 0; bool werk = false;
  for (int i = 0; i < jamN; i++) {
    const Jam &x = jams[i]; float dmin = 1e9; int pi = -1;
    for (int k = 0; k < x.n; k++) { float d = afstandM(lat, lon, jamPt[(x.p0 + k) * 2], jamPt[(x.p0 + k) * 2 + 1]); if (d < dmin) { dmin = d; pi = k; } }
    if (pi < 0 || dmin > 3000) continue;
    float la = jamPt[(x.p0 + pi) * 2], lo = jamPt[(x.p0 + pi) * 2 + 1];
    float br = atan2f((lo - lon) * cosf(lat * 0.0174533f), la - lat) * 57.2958f, df = fabsf(fmodf(fabsf(br - hd), 360.f)); if (df > 180) df = 360 - df;
    if (dmin > 80 && df > 60) continue;   // achter je of opzij
    if (x.k == 'j') { if (bestF < 0 || dmin < bestF) { bestF = (int)dmin; bestMin = x.d / 60; } }
    else if ((x.k == 'w' || x.k == 'a') && dmin < 900) werk = true;
  }
  lokFileD = bestF; lokFileMin = bestMin; lokWerk = werk; lokT = millis();
}
static void stuurZwaai() {
  if (!wCfg.url[0] || !wCfg.grp[0]) return;
  char url[256]; snprintf(url, sizeof url, "%s/cams/ping?g=%s&s=zwaai&van=%s&id=s%08lx&n=%s", wCfg.url, wCfg.grp, wCfg.van, (unsigned long)esp_random(), wCfg.naam);
  for (char *q = url; *q; q++) if (*q == ' ') *q = '+';
  size_t n; uint8_t *b = haalUrl(url, n); logRegel("zwaai naar de groep %s", b ? "verstuurd" : "mislukt"); free(b);
}
static void netTaak(void *) {
  uint32_t flitsT = 0, otaT = 0, beginT = 0, stapT = millis(); bool ntpAan = false, flitsOud = false;
  laadFlitsers(false); flitsOud = true;   // eerst uit het geheugen
  for (;;) {
    float dt = (millis() - stapT) / 1000.f; stapT = millis();
    // 1. tegel voor de huidige plek (uit het geheugen of van internet)
    bool online = WiFi.status() == WL_CONNECTED;
    if (gpsFix) { char key[16]; tegelKey(gpsLat, gpsLon, key, sizeof key); if (strcmp(key, tegelNu)) laadTegel(key, online); }
    bepaalWeg(); trajectStap(dt); fileVoorStap();
    // 2. wifi
    if (wifiHerstart) { wifiHerstart = false; WiFi.disconnect(true); vTaskDelay(pdMS_TO_TICKS(300)); beginT = 0; }
    if (!wifiNaam[0]) { netStatus = "wifi uit"; vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
    if (!online) {
      netStatus = "wifi zoekt";
      if (!beginT || millis() - beginT > 30000) { WiFi.mode(WIFI_STA); WiFi.setSleep(true); WiFi.begin(wifiNaam, wifiPass); beginT = millis(); }
      vTaskDelay(pdMS_TO_TICKS(1000)); continue;
    }
    netStatus = "wifi OK";
    if (!ntpAan) { ntpAan = true; configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.google.com"); logRegel("wifi verbonden (%s), IP %s", wifiNaam, WiFi.localIP().toString().c_str()); }
    if (!flitsT || millis() - flitsT > 24UL * 3600000 || flitsOud) { flitsOud = false; flitsT = millis(); laadFlitsers(true); }
    { static uint32_t wxHaalT = 0; if (gpsFix && (!wxHaalT || millis() - wxHaalT > 15UL * 60000)) { wxHaalT = millis(); haalWeer(); } }
    { static uint32_t pakT = 0; uint32_t elk = (gpsFix && gpsSpeedKmh > 5) ? 45000 : 120000; if (wCfg.url[0] && (!pakT || millis() - pakT > elk)) { pakT = millis(); haalPakket(); } }
    if (zwaaiVraag && !bleConnected) { zwaaiVraag = false; stuurZwaai(); } else if (zwaaiVraag) zwaaiVraag = false;
    if (gpsFix && tegelNu[0] == 0) { char key[16]; tegelKey(gpsLat, gpsLon, key, sizeof key); laadTegel(key, true); }
    bool stil = !gpsFix || gpsSpeedKmh < 1;
    if (stil && (!otaT || millis() - otaT > 3UL * 3600000) && millis() > 60000) { otaT = millis(); checkOta(); }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void drawFace(){
 // v15: alleen wissen wat er vorige keer getekend is, in plaats van het hele scherm
 Box &oud = drawnBox[frameIdx];
 frame->fillRect(oud.x0, oud.y0, oud.x1 - oud.x0 + 1, oud.y1 - oud.y0 + 1, BG);
 curBox = {32767, 32767, -32768, -32768};
 boxAdd(56, 176, 206, 240);   // rij-HUD, diagnose en volume: altijd vernieuwen
 // The original canvas clips all face artwork to a circular 204px viewport.
 // Black screen background already provides the same circular-screen appearance.
 if (menuOpen) { drawMenu(); return; }
 if (venster) { drawVenster(); return; }   // v30: venster ligt over alles heen
 float ox=gx*9 + fmaxf(-16.f, fminf(16.f, -leanX * 40.f * LEAN_RICHTING)), oy=gy*7 + headBob - hop * 10.f + fmaxf(-6.f, fminf(6.f, pushZ * 14.f));
 ox += fdx; oy += fdy; faceOx = ox; faceOy = oy;
 if(mood==LOVE){float pulse=1+sinf(t*7)*.08f;heart(73+ox,114+oy,29*pulse,HEART);heart(167+ox,114+oy,29*pulse,HEART);}
 else if(mood==EXCITED && ((int)(t*2)%2==0)){star(73+ox,106+oy,27+sinf(t*9)*2,EYE);star(167+ox,106+oy,27+sinf(t*9)*2,EYE);}
 else if(mood==HAPPY && ((int)(t*3)%4==0)){line(53+ox,120+oy,93+ox,120+oy,EYE,5);line(147+ox,120+oy,187+ox,120+oy,EYE,5);}
 else if(fxSpiral){spiraal(73+ox,106+oy);spiraal(167+ox,106+oy);}
 else {eye(73+ox,106+oy,-1);eye(167+ox,106+oy,1);}
 if(cur.blush>.02f){uint16_t c=BLUSH;circle(52+ox*.5f,150,8,c);circle(188+ox*.5f,150,8,c);}
 mouth(oy);
 // small floating hearts, sparkles and sleep-z particles from original spawn behaviour
 for(auto &p:particles) if(p.life>0){float a=1-p.age/p.life;uint16_t c=p.kind==0?HEART:EYE;if(p.kind==0)heart(p.x,p.y,p.size*a,c);else if(p.kind==3){circle(p.x,p.y,2.5f*a+1.5f,c);line(p.x+2.5f,p.y,p.x+2.5f,p.y-10,c,2);line(p.x+2.5f,p.y-10,p.x+8,p.y-7,c,2);}else if(p.kind==1)star(p.x,p.y,p.size*a,c);else { // Z mark
   line(p.x-4,p.y-4,p.x+4,p.y-4,c,2);line(p.x+4,p.y-4,p.x-4,p.y+4,c,2);line(p.x-4,p.y+4,p.x+4,p.y+4,c,2);
 }}
 drawOutfit(); gagDraw(); drawFlag(); drawRitKlaar(); drawGmeter();
 // Originele Buddy-rij-HUD: rond maximumsnelheidsbord links, eigen snelheid rechts.
 // Zelfde principe/positie als speedDraw() in de webapp; bij geen bekende limiet tonen we '?'.
 bool fixNow = gpsFix && (millis()-gpsLastFixMs)<5000;
 bool tel = phoneFresh();
 int ownSpeed = fixNow ? (int)lroundf(gpsSpeedKmh) : (tel ? phone.v : -1);
 int limit = (tel && phone.lim > 0) ? phone.lim : ((wegLimiet > 0 && millis() - wegLimT < 6000) ? wegLimiet : ((gpsNearestCameraM >= 0 && gpsNearestCameraM <= 600) ? gpsCameraLimit : 0));
 // v19: file en tekst van de telefoon bovenin, werk-driehoekje naast het bord
 int camM = (tel && phone.camD >= 0) ? phone.camD : gpsNearestCameraM;
 if (trajActief) {   // v25: trajectcontrole met je gemiddelde
   char ct[28]; snprintf(ct, sizeof ct, "TRAJECT %d / %d", (int)lroundf(trajGem), trajLim);
   bool tg = trajGem > trajLim + 2;
   boxAdd(30, 26, 210, 50); roundRect(34, 28, 172, 20, 10, tg ? 0xB000 : 0x0230);
   frame->setTextColor(0xFFFF); frame->setTextSize(2); frame->setCursor(X(120) - strlen(ct) * 6, Y(32)); frame->print(ct);
 } else if (otaPct >= 0) {
   char ct[24]; snprintf(ct, sizeof ct, "UPDATE %d%%", otaPct);
   boxAdd(40, 26, 200, 50); roundRect(44, 28, 152, 20, 10, 0x0418);
   frame->setTextColor(0xFFFF); frame->setTextSize(2); frame->setCursor(X(120) - strlen(ct) * 6, Y(32)); frame->print(ct);
 } else if ((int32_t)(pinTot - millis()) > 0) {   // v29: koppelcode laten zien bij de eerste keer verbinden
   char ct[28]; snprintf(ct, sizeof ct, "CODE %06lu", (unsigned long)koppelPin);
   boxAdd(40, 26, 200, 50); roundRect(44, 28, 152, 20, 10, 0x0451);
   frame->setTextColor(0xFFFF); frame->setTextSize(2); frame->setCursor(X(120) - strlen(ct) * 6, Y(32)); frame->print(ct);
 } else if (tel && phone.msi[0]) {   // v29: matrixbord boven de weg
   boxAdd(70, 18, 170, 76); roundRect(72, 20, 96, 54, 7, 0x0841); frame->drawRoundRect(X(72), Y(20), S(96), S(54), S(7), EYE);
   bool kn = (millis() / 250) % 2; circle(80, 27, 3, kn ? 0xFD80 : 0x4200); circle(160, 27, 3, kn ? 0xFD80 : 0x4200); circle(80, 67, 3, kn ? 0x4200 : 0xFD80); circle(160, 67, 3, kn ? 0x4200 : 0xFD80);
   if (phone.msi[0] == 'x') { dikLijn(104, 32, 136, 62, 6, 0xF800); dikLijn(136, 32, 104, 62, 6, 0xF800); }
   else { circle(120, 47, 19, 0xF800); circle(120, 47, 15, 0xFFFF); segTekst(phone.msi, 120, 47, strlen(phone.msi) >= 3 ? 10.f : 13.f, 0x0000); }
 } else if (camM >= 0 && camM < 1000) {
   char ct[24]; snprintf(ct, sizeof ct, "FLITSER %dm", (camM / 10) * 10);
   boxAdd(40, 26, 200, 50); roundRect(44, 28, 152, 20, 10, 0x8000);
   frame->setTextColor(0xFFFF); frame->setTextSize(2); frame->setCursor(X(120) - strlen(ct) * 6, Y(32)); frame->print(ct);
 } else if ((tel && phone.fileD >= 0) || (!tel && lokFileD >= 0)) {
   int fD = tel ? phone.fileD : lokFileD, fM = tel ? phone.fileMin : lokFileMin;
   char ft[32]; if (fD < 950) snprintf(ft, sizeof ft, "FILE %dm", (fD / 50) * 50); else snprintf(ft, sizeof ft, "FILE %d,%dkm", fD / 1000, (fD % 1000) / 100);
   if (fM > 0) { size_t l = strlen(ft); snprintf(ft + l, sizeof ft - l, " +%dm", fM); }
   boxAdd(50, 26, 190, 50); roundRect(52, 28, 136, 20, 10, 0x3082);
   bool aan = (millis() / 400) % 2; circle(64, 38, 3, aan ? 0xF8A6 : 0x7800); circle(73, 38, 3, aan ? 0x7800 : 0xF8A6);
   frame->setTextColor(0xFEDB); frame->setTextSize(2); frame->setCursor(X(82), Y(31)); frame->print(ft);
 } else if (tel && phone.nav) {   // v29: navigatiepijl met afstand
   float cx = 120, cy = 34; uint16_t c = EYE;
   boxAdd(70, 14, 170, 72);
   if (phone.nav == 'A') { line(cx - 2, cy - 14, cx - 2, cy + 14, c, 3); triF(cx, cy - 14, cx + 16, cy - 8, cx, cy - 2, 0xF800); }
   else if (phone.nav == 'O') { for (int i = 0; i < 14; i++) { float a1 = .6f + i * .38f, a2 = a1 + .38f; line(cx + cosf(a1) * 11, cy + sinf(a1) * 11, cx + cosf(a2) * 11, cy + sinf(a2) * 11, c, 3); } triF(cx + 11, cy - 10, cx + 3, cy - 3, cx + 16, cy - 2, c); }
   else {
     float a = 0; switch (phone.nav) { case 'L': a = -90; break; case 'R': a = 90; break; case 'l': a = -45; break; case 'r': a = 45; break; case 'U': a = 180; break; default: a = 0; }
     float rad = a * .0174533f, dx = sinf(rad), dy = -cosf(rad);
     if (phone.nav == 'L' || phone.nav == 'R' || phone.nav == 'U') { dikLijn(cx, cy + 14, cx, cy - 2, 5, c); dikLijn(cx, cy - 2, cx + dx * 12, cy - 2 + dy * 12, 5, c); float ex = cx + dx * 14, ey = cy - 2 + dy * 14; triF(ex + dx * 8, ey + dy * 8, ex - dy * 7, ey + dx * 7, ex + dy * 7, ey - dx * 7, c); }
     else { dikLijn(cx - dx * 12, cy - dy * 12 + 4, cx + dx * 6, cy + dy * 6, 5, c); float ex = cx + dx * 8, ey = cy + dy * 8; triF(ex + dx * 9, ey + dy * 9, ex - dy * 8, ey + dx * 8, ex + dy * 8, ey - dx * 8, c); }
   }
   char nd[16]; if (phone.navD >= 950) snprintf(nd, sizeof nd, "%d,%d km", phone.navD / 1000, (phone.navD % 1000) / 100); else snprintf(nd, sizeof nd, "%d m", (phone.navD / 10) * 10);
   frame->setTextColor(EYE); frame->setTextSize(2); frame->setCursor(X(120) - strlen(nd) * 6, Y(56)); frame->print(nd);
 } else if (wxGlad() || (wxVers() && wxRegenOver >= 0 && wxRegenOver <= 15 && spdNow() > 20)) {   // v29: gladheid of regen
   char ct[28]; bool gl = wxGlad(); if (gl) snprintf(ct, sizeof ct, "GLAD? %d GRADEN", (int)lroundf(wxTemp)); else if (wxRegenOver == 0) snprintf(ct, sizeof ct, "HET REGENT"); else snprintf(ct, sizeof ct, "REGEN OVER %d MIN", wxRegenOver);
   boxAdd(24, 26, 216, 50); roundRect(28, 28, 184, 20, 10, gl ? 0xC300 : 0x0318);
   frame->setTextColor(0xFFFF); frame->setTextSize(2); frame->setCursor(X(120) - strlen(ct) * 6, Y(32)); frame->print(ct);
 } else if ((int32_t)(grpTot - millis()) > 0 && grpTekst[0]) {   // v28: groepsbericht
   char tx[27]; strncpy(tx, grpTekst, 26); tx[26] = 0; int tw = strlen(tx) * 12;
   boxAdd(18, 26, 222, 50); roundRect(120 - tw / 2.f / 1.94f - 8, 28, tw / 1.94f + 16, 20, 10, 0x0451);
   frame->setTextColor(0xFFFF); frame->setTextSize(2); frame->setCursor(X(120) - tw / 2, Y(32)); frame->print(tx);
 } else if (gagAct >= 0 && gagMsg && gagMsg[0] && (int32_t)(flagTot - millis()) <= 0 && (int32_t)(ritKlaarTot - millis()) <= 0) {
   char tx[27]; strncpy(tx, gagMsg, 26); tx[26] = 0; int tw = strlen(tx) * 12;
   boxAdd(20, 30, 220, 46); frame->setTextColor(0xBDF7); frame->setTextSize(2); frame->setCursor(X(120) - tw / 2, Y(34)); frame->print(tx);
 } else if (tel && phone.txt[0] && mood != SLEEPING && (int32_t)(flagTot - millis()) <= 0) {
   char tx[27]; strncpy(tx, phone.txt, 26); tx[26] = 0; int tw = strlen(tx) * 12;
   boxAdd(20, 30, 220, 46); frame->setTextColor(0xBDF7); frame->setTextSize(2); frame->setCursor(X(120) - tw / 2, Y(34)); frame->print(tx);
 }
 if ((tel && phone.werk) || (!tel && lokWerk)) {   // werk-driehoekje boven het snelheidsbord
   boxAdd(64, 156, 90, 178);
   frame->fillTriangle(X(77), Y(157), X(89), Y(177), X(65), Y(177), 0xD8A6);
   frame->fillTriangle(X(77), Y(162), X(85), Y(174), X(69), Y(174), 0xFFFF);
   circle(77, 169, 1.8f, 0x0000);
 }
 // v22: grote, haarscherpe cijfers die precies in het midden staan (eigen 7-segment-cijfers)
 bool rust = (ownSpeed <= 0) && klokOk() && (gagAct < 0);   // stilstaan: klok in plaats van 0
 int sx = 76, sy = 200; float sr = 24;
 boxAdd(sx - sr - 2, sy - sr - 2, sx + sr + 2, sy + sr + 2);
 bool teHard = limit > 0 && ownSpeed > limit + 3;
 uint16_t ring = (teHard && (millis() / 350) % 2) ? 0xF800 : 0xD800;
 frame->fillCircle(X(sx), Y(sy), S(sr), ring);
 frame->fillCircle(X(sx), Y(sy), S(sr - 4.2f), 0xFFFF);
 char limTxt[6]; if (limit > 0) snprintf(limTxt, sizeof limTxt, "%d", limit); else snprintf(limTxt, sizeof limTxt, "-");
 segTekst(limTxt, sx, sy, strlen(limTxt) >= 3 ? 12.5f : 17.f, 0x0000);
 char spdTxt[8];
 if (rust) { int hh, mm; lokaleTijd(hh, mm); snprintf(spdTxt, sizeof spdTxt, "%02d:%02d", hh, mm); segTekst(spdTxt, 152, 197, 22, EYE);
   if (wxVers()) {   // v29: temperatuur en een weericoontje onder de klok
     char tg[8]; snprintf(tg, sizeof tg, "%d", (int)lroundf(wxTemp)); segTekst(tg, 160, 222, 11, 0xBDF7); circle(160 + strlen(tg) * 4.2f + 3, 217, 1.6f, 0xBDF7);
     float ix = 132, iy = 222; int c = wxCode;
     if (c <= 1) circle(ix, iy, 5, GOLD);
     else { if (c <= 3) circle(ix - 3, iy - 3, 4, GOLD); circle(ix - 3, iy + 1, 4.5f, 0xC618); circle(ix + 3, iy, 5, 0xC618); roundRect(ix - 7, iy + 1, 15, 4, 2, 0xC618);
       if (c >= 51 && c < 70) for (int i = -1; i <= 1; i++) line(ix + i * 4, iy + 7, ix + i * 4 - 1, iy + 10, 0x7E5F, 1);
       if (c >= 71 && c < 80) for (int i = -1; i <= 1; i++) circle(ix + i * 4, iy + 9, 1.2f, 0xFFFF);
       if (c >= 95) triF(ix, iy + 5, ix - 3, iy + 11, ix + 2, iy + 9, GOLD); }
   } }
 else { if (ownSpeed >= 0) snprintf(spdTxt, sizeof spdTxt, "%d", ownSpeed); else snprintf(spdTxt, sizeof spdTxt, "--");
   segTekst(spdTxt, 150, 196, 30, teHard ? 0xFB2C : EYE);
   frame->setTextColor(0x8C71); frame->setTextSize(2); frame->setCursor(X(150) - 24, Y(216)); frame->print("km/u"); }
 // kleine GPS-status helemaal onderin (alleen als er nog geen fix is)
 if (!fixNow) {
   char st[28]; if (gpsDataOk && millis() - gpsLastDataMs < 6000) snprintf(st, sizeof st, "GPS zoekt %d/%d", (int)gpsSatellites, (int)gpsInZicht); else snprintf(st, sizeof st, "GPS geen data");
   frame->setTextColor(0x6B4D); frame->setTextSize(1); frame->setCursor(X(120) - strlen(st) * 3, Y(229)); frame->print(st);
 }
}
// v15: alleen het veranderde vak naar het scherm sturen. De CO5300 wil even begin-/eindcoordinaten.
void flushDirty(){
  Box b = curBox;
  if (prevBox.x0 < b.x0) b.x0 = prevBox.x0; if (prevBox.y0 < b.y0) b.y0 = prevBox.y0;
  if (prevBox.x1 > b.x1) b.x1 = prevBox.x1; if (prevBox.y1 > b.y1) b.y1 = prevBox.y1;
  if (b.x0 < 0) b.x0 = 0; if (b.y0 < 0) b.y0 = 0; if (b.x1 > LCD_WIDTH - 1) b.x1 = LCD_WIDTH - 1; if (b.y1 > LCD_HEIGHT - 1) b.y1 = LCD_HEIGHT - 1;
  b.x0 &= ~1; b.y0 &= ~1; if (!(b.x1 & 1)) b.x1++; if (!(b.y1 & 1)) b.y1++;
  if (b.x1 > LCD_WIDTH - 1) b.x1 = LCD_WIDTH - 1; if (b.y1 > LCD_HEIGHT - 1) b.y1 = LCD_HEIGHT - 1;
  prevBox = curBox;
  if (prevBox.x0 < 0) prevBox.x0 = 0; if (prevBox.y0 < 0) prevBox.y0 = 0;
  if (prevBox.x1 > LCD_WIDTH - 1) prevBox.x1 = LCD_WIDTH - 1; if (prevBox.y1 > LCD_HEIGHT - 1) prevBox.y1 = LCD_HEIGHT - 1;
  drawnBox[frameIdx] = prevBox;
  if (b.x1 < b.x0 || b.y1 < b.y0) return;
  // v16: niet zelf versturen maar doorgeven aan de verstuur-taak op core 0, en meteen het volgende beeld gaan tekenen
  xSemaphoreTake(flushIdle, portMAX_DELAY);   // wacht tot het vorige beeld helemaal verstuurd is
  FlushJob job = { frame->getFramebuffer(), b };
  xQueueSend(flushQueue, &job, portMAX_DELAY);
  frameIdx ^= 1; frame = frames[frameIdx];
}
void flushTask(void *) {
  FlushJob job;
  for (;;) {
    if (xQueueReceive(flushQueue, &job, portMAX_DELAY) != pdTRUE) continue;
    if (wilLicht && wilLicht != isLicht) { isLicht = wilLicht; gfx->setBrightness(isLicht); }
    uint32_t t0 = micros();
    uint16_t w = job.b.x1 - job.b.x0 + 1, h = job.b.y1 - job.b.y0 + 1;
    gfx->startWrite();
    gfx->writeAddrWindow(job.b.x0, job.b.y0, w, h);
    for (int16_t y = job.b.y0; y <= job.b.y1; y++) gfx->writePixels(job.fb + (int32_t)y * LCD_WIDTH + job.b.x0, w);
    gfx->endWrite();
    flushLastUs = micros() - t0;
    vTaskDelay(1);   // v22: core 0 even vrijgeven (anders slaat de waakhond aan als Bluetooth ook bezig is)
    xSemaphoreGive(flushIdle);
  }
}
void updateMood(float dt){
 // Rijstatus heeft altijd voorrang. Als Buddy stilstaat zonder GPS-fix,
 // krijgt hij af en toe een korte spontane originele expressie i.p.v. stil te staan.
 if(gpsFix && gpsSpeedKmh>5) mood=FOCUSED;
 else if(gpsFix) mood=CONTENT;
 else {
   if(millis()>idleMoodUntilMs && !phoneFresh()){
     static const uint8_t idleSequence[]={NEUTRAL,RELAXED,BORED,YAWN,HAPPY,ANTICIPATE};
     mood=idleSequence[idleMoodStep++ % (sizeof(idleSequence)/sizeof(idleSequence[0]))];
     idleMoodUntilMs=millis()+((mood==YAWN)?1800:((mood==BORED)?2600:3200));
   }
 }
 // v19: als de telefoon verbonden is, neemt hij de stemming van de app over (die weet veel meer)
 if (phoneFresh() && phone.mood >= 0 && millis() > idleMoodUntilMs) mood = phone.mood;
 if (gagAct >= 0 && gagMood >= 0) mood = gagMood;
 { static uint32_t glT = 0; if (wxGlad() && spdNow() > 10 && millis() - glT > 900000) { glT = millis(); schrikTot = millis() + 1200; } }
 { static uint32_t kl0 = 0, kn0 = 0, mu0 = 0;
   if (klapT && klapT != kl0) { kl0 = klapT; aaiTot = millis() + 2000; laatsteActie = millis(); if (gagAct < 0) { startGag(7); gagMsg = "Ja? Ik ben er!"; } }
   if (knalT && knalT != kn0 && millis() - knalT < 300) { kn0 = knalT; if (millis() > schrikTot + 4000) schrikTot = millis() + 900; }
   if (muziekSinds && millis() - muziekSinds > 8000 && gagAct < 0 && millis() - mu0 > 90000 && !slaapt) { mu0 = millis(); startGag(random(2) ? 8 : 24); gagMsg = "Muziek! Dansje!"; } }
 if (slaapt) mood = SLEEPING;
 if ((int32_t)(grpTot - millis()) > 0) mood = HAPPY;
 if ((int32_t)(flagTot - millis()) > 0) mood = EXCITED;
 if ((int32_t)(ritKlaarTot - millis()) > 0) mood = HAPPY;
 if (phoneFresh() && phone.camD >= 0 && phone.camD < 350) mood = ALERT;
 if (millis() < schrikTot) mood = STARTLED;
 if (millis() < aaiTot) mood = LOVE;
 if(gpsFix && gpsNearestCameraM>=0 && gpsNearestCameraM<350) mood=ALERT;
 if(gpsFix && gpsSpeedKmh>0 && gpsCameraLimit>0 && gpsSpeedKmh>gpsCameraLimit+4 && gpsNearestCameraM>=0 && gpsNearestCameraM<600) mood=WORRIED;
 if(mood!=lastMood){lastMood=mood;if(mood==LOVE)addParticle(0);if(mood==EXCITED)addParticle(1);if(mood==SLEEPING)addParticle(2);playMoodSound(mood);}
 Expr target=exprs[mood];float k=1-expf(-dt*11.0f);
 cur.open+=(target.open-cur.open)*k;cur.lid+=(target.lid-cur.lid)*k;cur.tilt+=(target.tilt-cur.tilt)*k;cur.squint+=(target.squint-cur.squint)*k;cur.scale+=(target.scale-cur.scale)*k;cur.mouth+=(target.mouth-cur.mouth)*k;cur.mo+=(target.mo-cur.mo)*k;cur.blush+=(target.blush-cur.blush)*k;
 if(t>nextGaze)gazeTarget();
 float tgxL = tgx + fmaxf(-.6f, fminf(.6f, -leanX * 2.5f * LEAN_RICHTING));   // v19: kijkt de bocht in
 float kg=1-expf(-dt*9.0f);gx+=(tgxL-gx)*kg;gy+=(tgy-gy)*kg;
 breathe += dt*1.7f; headBob = sinf(breathe)*1.25f;
 blink=1;if(mood!=SLEEPING && !fxNoBlink){if(t>nextBlink){blinkStart=t;nextBlink=t+((random(100)<22)? .3f:random(22,56)/10.f);}float bt=(t-blinkStart)/.16f;if(bt>=0&&bt<1)blink=1-.92f*sinf(3.14159f*bt);}
}

void updateParticles(float dt){
 for(auto &p:particles)if(p.life>0){p.age+=dt;p.x+=p.vx*dt;p.y+=p.vy*dt;if(p.age>=p.life)p.life=0;}
 static float spawnTimer=0;spawnTimer-=dt;if(spawnTimer<=0){if(mood==LOVE){addParticle(0);spawnTimer=.22f;}else if(mood==EXCITED){addParticle(1);spawnTimer=.14f;}else if(mood==SLEEPING){addParticle(2);spawnTimer=1.2f;}else spawnTimer=.2f;}
}

// ES8311 DAC-volume (register 0x32): officiële schaal 0..100 -> 0..255.
bool codecWrite(uint8_t reg, uint8_t value);
void applyCodecVolume(uint8_t volume) {
  uint8_t reg = volume == 0 ? 0 : (uint8_t)(((uint16_t)volume * 256U / 100U) - 1U);
  if (!i2cLock(30)) return;
  codecWrite(0x32, reg);
  i2cUnlock();
}

// ES8311 initialisatie volgens de Waveshare Arduino-voorbeeldconfiguratie.
bool codecWrite(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(0x18);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

// v29: microfoons (ES7210 op 0x40) voor geluid horen. Experimenteel: reageert op muziek, harde knallen en 2x klappen.
// Echt Nederlands verstaan ("hé Buddy") kan dit chipje zelf niet; dat doet de Buddy-app op je telefoon.
static bool es7210W(uint8_t r, uint8_t v) { Wire.beginTransmission(0x40); Wire.write(r); Wire.write(v); return Wire.endTransmission() == 0; }
static bool es7210Init() {
  const uint8_t regs[][2] = {
    {0x00, 0xFF}, {0x00, 0x41}, {0x01, 0x3F}, {0x09, 0x30}, {0x0A, 0x30}, {0x23, 0x2A}, {0x22, 0x0A}, {0x20, 0x0A}, {0x21, 0x2A},
    {0x08, 0x10},                // slave-stand
    {0x40, 0x43}, {0x41, 0x70}, {0x42, 0x70},
    {0x07, 0x20}, {0x02, 0x81}, {0x04, 0x01}, {0x05, 0x00},   // MCLK 4,096 MHz, 16 kHz
    {0x11, 0x60}, {0x12, 0x00},  // I2S, 16 bit, geen TDM
    {0x43, 0x1A}, {0x44, 0x1A}, {0x45, 0x00}, {0x46, 0x00},   // MIC1+2 aan, ~30 dB versterking
    {0x47, 0x08}, {0x48, 0x08}, {0x49, 0xFF}, {0x4A, 0xFF}, {0x4B, 0x00}, {0x4C, 0xFF},
    {0x01, 0x14}, {0x00, 0x71}, {0x00, 0x41}
  };
  for (auto &r : regs) if (!es7210W(r[0], r[1])) return false;
  return true;
}
static void oorTaak(void *) {
  int16_t buf[512]; uint32_t laatst = millis();
  for (;;) {
    size_t n = buddyI2S.readBytes((char *)buf, sizeof buf);
    if (n < 64) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
    int cnt = n / 2; double som = 0; for (int i = 0; i < cnt; i++) { float v = buf[i] / 32768.0f; som += v * v; }
    float rms = sqrtf(som / cnt); oorNiveau = rms; oorOk = true;
    uint32_t nu = millis(); float dt = (nu - laatst) / 1000.f; laatst = nu;
    if (oorBasis <= 0) oorBasis = rms; oorBasis += (rms - oorBasis) * fminf(1, dt / 8.f);   // achtergrond (wegdek, ventilator)
    bool luid = rms > oorBasis * 2.2f + .004f;
    static uint32_t luidSinds = 0, stilSinds = 0, piekT = 0; static int pieken = 0;
    if (luid) { stilSinds = 0; if (!luidSinds) luidSinds = nu; } else { if (!stilSinds) stilSinds = nu; if (stilSinds && nu - stilSinds > 1500) luidSinds = 0; }
    if (luidSinds && nu - luidSinds > 6000 && !audioBusy) muziekSinds = luidSinds;   // lang achter elkaar geluid = muziek/praten
    if (!luid && stilSinds && nu - stilSinds > 4000) muziekSinds = 0;
    if (rms > oorBasis * 6 + .03f) {   // korte harde piek
      if (nu - piekT > 120) { if (nu - piekT < 700) pieken++; else pieken = 1; piekT = nu; }
      if (pieken == 2) { klapT = nu; pieken = 0; } else if (!audioBusy) knalT = nu;
    }
  }
}
bool initBuddyAudio() {
  pinMode(PA, OUTPUT);
  digitalWrite(PA, HIGH);
  buddyI2S.setPins(BCLKPIN, WSPIN, DIPIN, DOPIN, MCLKPIN);
  if (!buddyI2S.begin(I2S_MODE_STD, AUDIO_RATE, I2S_DATA_BIT_WIDTH_16BIT,
                      I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
    Serial.println("Buddy audio: I2S start mislukt");
    return false;
  }

  // ES8311 op 16 kHz, 16-bit I2S, MCLK = 256 x sample rate.
  const uint8_t regs[][2] = {
    {0x00,0x1F}, {0x00,0x00}, {0x00,0x80},
    {0x01,0x3F}, {0x02,0x00}, {0x03,0x10}, {0x04,0x10},
    {0x05,0x00}, {0x06,0x03}, {0x07,0x00}, {0x08,0xFF},
    {0x09,0x0C}, {0x0A,0x0C}, {0x0D,0x01}, {0x0E,0x02},
    {0x12,0x00}, {0x13,0x10}, {0x1C,0x6A}, {0x37,0x08},
    {0x31,0x00}, {0x32,0xFF} // DAC unmuted; volume op maximaal
  };
  for (size_t i=0; i<sizeof(regs)/sizeof(regs[0]); ++i) {
    if (!codecWrite(regs[i][0], regs[i][1])) {  // (draait voor de GPS-taak start, dus zonder mutex)
      Serial.printf("Buddy audio: ES8311 fout bij register 0x%02X\n", regs[i][0]);
      return false;
    }
    if (i==0) delay(20);
  }
  applyCodecVolume(buddyVolume);
  Serial.println("Buddy audio: ES8311 klaar");
  if (es7210Init()) { xTaskCreatePinnedToCore(oorTaak, "BuddyOor", 6144, nullptr, 1, nullptr, 0); Serial.println("Buddy oor: ES7210 microfoons klaar"); }
  else Serial.println("Buddy oor: ES7210 niet gevonden");
  return true;
}

struct Note { uint16_t hz; uint16_t ms; };
struct SoundJob { Note notes[4]; uint8_t count; };

void soundTask(void *arg) {
  SoundJob job = *(SoundJob*)arg;
  delete (SoundJob*)arg;
  int16_t pcm[640]; // 20 ms stereo buffer at 16 kHz
  for (uint8_t n=0; n<job.count; ++n) {
    const uint32_t totalFrames = (uint32_t)AUDIO_RATE * job.notes[n].ms / 1000;
    uint32_t done = 0;
    while (done < totalFrames) {
      uint32_t count = min((uint32_t)320, totalFrames-done);
      for (uint32_t i=0; i<count; ++i) {
        float phase = 6.2831853f * job.notes[n].hz * (done+i) / AUDIO_RATE;
        float env = 0.55f;
        if (done+i < 160) env *= (done+i)/160.0f;
        if (totalFrames-(done+i) < 240) env *= (totalFrames-(done+i))/240.0f;
        int16_t v = buddyVolume == 0 ? 0 : (int16_t)(sinf(phase) * 30000.0f * env);
        pcm[i*2] = v; pcm[i*2+1] = v;
      }
      buddyI2S.write((uint8_t*)pcm, count*2*sizeof(int16_t));
      done += count;
    }
    // Korte stilte tussen de tonen.
    memset(pcm, 0, sizeof(pcm));
    buddyI2S.write((uint8_t*)pcm, sizeof(pcm));
  }
  audioBusy = false;
  vTaskDelete(NULL);
}

void playMoodSound(int m) {
  if (!audioReady || audioBusy) return;
  SoundJob *j = new SoundJob();
  switch (m) {
    case HAPPY:     j->notes[0]={660,95}; j->notes[1]={880,135}; j->count=2; break;
    case LOVE:      j->notes[0]={523,100}; j->notes[1]={659,100}; j->notes[2]={784,180}; j->count=3; break;
    case EXCITED:   j->notes[0]={880,75}; j->notes[1]={1175,75}; j->notes[2]={1568,130}; j->count=3; break;
    case STARTLED:  j->notes[0]={1047,65}; j->notes[1]={740,110}; j->count=2; break;
    case SLEEPY:    j->notes[0]={440,180}; j->notes[1]={330,240}; j->count=2; break;
    case YAWN:      j->notes[0]={392,160}; j->notes[1]={294,230}; j->count=2; break;
    case ALERT:     j->notes[0]={880,90}; j->notes[1]={1047,90}; j->count=2; break;
    default: delete j; return;
  }
  audioBusy = true;
  if (xTaskCreatePinnedToCore(soundTask, "BuddySound", 4096, j, 1, nullptr, 1) != pdPASS) {
    audioBusy = false; delete j;
  }
}

void handleTouch(){
 static bool down = false; static uint32_t downMs = 0; static float dX = 0, dY = 0, lX = 0, lY = 0; static bool lang = false;
 uint32_t now=millis();
 if (venster && (int32_t)(now - vensterTot) > 0) sluitVenster();   // 30 s niks gedaan: venster dicht
 if(!touchReady || now-lastTouchPoll<12) return; lastTouchPoll=now;
 if(i2cMutex && xSemaphoreTake(i2cMutex,0)!=pdTRUE) return; uint8_t n=buddyTouch.getPoint(touchX,touchY,1); if(i2cMutex) xSemaphoreGive(i2cMutex);
 if(!n) {
   // loslaten = een tik afhandelen
   if (down && !lang && now - downMs < 600) {
     if (venster) { if (lY > 172.0f && fabsf(lX - 120.0f) < 62.0f) sluitVenster(); }   // TERUG
     else if (menuOpen) menuKies(lX, lY);
     else if (lY > 188.0f) openVenster(2);                          // onderaan tikken = volume
     else { aaiTot = now + 2200; idleMoodUntilMs = now + 2200; }    // op zijn gezicht tikken = aaien
   }
   down = false; lang = false; return;
 }
 // CST9217 levert paneelcoördinaten; vertaal naar de 240x240 tekenruimte.
 // v30: de touchlaag van dit bordje zit 180 graden gedraaid t.o.v. het beeld (net als in het Waveshare-voorbeeld: setMirrorXY(true,true))
 float x=240.0f-(float)touchX[0]*240.0f/(float)LCD_WIDTH;
 float y=240.0f-(float)touchY[0]*240.0f/(float)LCD_HEIGHT;
 if (!down) { down = true; downMs = now; dX = x; dY = y; lang = false; laatsteActie = now; if (slaapt) { slaapt = false; laatsteActie = now; } }
 lX = x; lY = y;
 if (venster) { vensterTot = now + 30000; vensterSleep(x, y); return; }   // in een venster: geen lang-drukken, wel slepen
 // lang drukken (0,7 s, zonder te slepen) opent of sluit het ronde menu
 if (!lang && now - downMs > 700 && fabsf(x - dX) < 12 && fabsf(y - dY) < 12) { lang = true; menuOpen = !menuOpen; menuTot = now + 10000; }
}

void setup(){
 Serial.begin(115200);
 // USB CDC kan na reset iets later beschikbaar komen; wacht kort en meld expliciet.
 uint32_t serialWaitStart=millis();
 while(!Serial && millis()-serialWaitStart<2500) delay(10);
 delay(200);
 Serial.println();
 Serial.println("========================================");
 Serial.println("BUDDY v29 BOOT - USB SERIAL ACTIEF");
 Serial.println("Baudrate: 115200 | USB CDC check");
 Serial.printf("PSRAM: %u bytes vrij | heap: %u bytes\n", (unsigned)ESP.getFreePsram(), (unsigned)ESP.getFreeHeap());
 buddyPrefs.begin("buddy", false); buddyVolume=buddyPrefs.getUChar("volume", 85);
 Wire.setBufferSize(1100);  // v15: groot genoeg voor een blok GPS-tekst via I2C
 Wire.begin(IIC_SDA,IIC_SCL); Wire.setClock(100000); i2cMutex=xSemaphoreCreateMutex(); randomSeed(esp_random());
 // v16: I2C-scan, zodat we zien of de GPS (0x50/0x54) er echt is
 Serial.print("I2C-scan:");
 for (uint8_t a = 1; a < 127; a++) { Wire.beginTransmission(a); if (Wire.endTransmission() == 0) Serial.printf(" 0x%02X", a); }
 Serial.println("   (verwacht o.a. 0x18 audio, 0x20 expander, 0x34 power, 0x50/0x54 GPS, 0x5A touch, 0x6B gyro)");
 Wire.setTimeOut(1000);   // v17: ruim de tijd geven, net als het Waveshare-voorbeeld (1 s)
 bool gpsResetOk = GPS_RESET_PULSE ? releaseLc76gReset() : false;
 if (!GPS_RESET_PULSE) {
   // v17: P7 (GPS_RST) terugzetten naar de fabrieksstand (ingang, dus los). De expander onthoudt de stand van v14/v15
   // tot de stroom er helemaal af is geweest; daardoor kon de GPS nog steeds "vastgehouden" worden.
   Wire.beginTransmission(0x20); Wire.write(0x03); Wire.endTransmission(false);
   uint8_t cfg = 0xFF; if (Wire.requestFrom((int)0x20, 1) == 1) cfg = Wire.read();
   Wire.beginTransmission(0x20); Wire.write(0x03); Wire.write((uint8_t)(cfg | 0x80)); int pe = Wire.endTransmission();
   Serial.printf("GPS_RST (P7) teruggezet naar ingang: %s (config was 0x%02X)\n", pe == 0 ? "OK" : "MISLUKT", cfg);
   delay(1500);  // GPS rustig laten opstarten
 }
 gpsSerial.begin(GPS_UART_BAUD, SERIAL_8N1, GPS_UART_RX, GPS_UART_TX);
 gpsSerial.setRxBufferSize(4096);
 delay(1200);  // LC76G heeft na de reset ruim een seconde nodig
 while(gpsSerial.available()) gpsSerial.read();
 Serial.printf("Buddy GPS UART gestart | RX=%d TX=%d | %lu baud | reset=%s\n", GPS_UART_RX, GPS_UART_TX, (unsigned long)GPS_UART_BAUD, GPS_RESET_PULSE ? (gpsResetOk?"OK":"NIET BEVESTIGD") : "UIT (niet aangeraakt)");
 buddyTouch.setPins(TP_RESET, TP_INT);
 if (buddyTouch.begin(Wire, CST92XX_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) { touchReady=true; Serial.println("Buddy touch: CST9217 klaar"); }
 else Serial.println("Buddy touch: niet beschikbaar; volumeregeling via scherm uitgeschakeld");
 frames[0] = new PsramCanvas(LCD_WIDTH, LCD_HEIGHT, gfx, 0, 0, 0);
 frames[1] = new PsramCanvas(LCD_WIDTH, LCD_HEIGHT, gfx, 0, 0, 0);
 frame = frames[0];
 if (!frames[0] || !frames[1]) { Serial.println("Buddy ERROR: canvas-object niet aangemaakt"); while(true) delay(1000); }
 if (!frames[0]->begin(QSPI_HZ) || !frames[1]->begin(GFX_SKIP_OUTPUT_BEGIN)) { Serial.printf("Buddy ERROR: framebuffer init mislukt (staat PSRAM op 'OPI PSRAM'?) | PSRAM=%u | heap=%u\n", (unsigned)ESP.getFreePsram(), (unsigned)ESP.getFreeHeap()); while(true) delay(1000); }
 gfx->setBrightness(160);
 frames[1]->fillScreen(BG); frame->fillScreen(BG); frame->flush();
 frames[1]->setTextColor(EYE); frames[1]->setTextSize(1);
 flushQueue = xQueueCreate(1, sizeof(FlushJob)); flushIdle = xSemaphoreCreateBinary(); xSemaphoreGive(flushIdle);
 xTaskCreatePinnedToCore(flushTask, "BuddyFlush", 8192, nullptr, 1, nullptr, 0);
 Serial.printf("Buddy beeld klaar | framebuffer=16-bit PSRAM | QSPI=%ld Hz | PSRAM over: %u | heap over: %u\n", (long)QSPI_HZ, (unsigned)ESP.getFreePsram(), (unsigned)ESP.getFreeHeap());
 frame->setTextColor(EYE); frame->setTextSize(1);
 audioReady = initBuddyAudio();
 // v19: bewegingssensor
 Serial.println("Buddy gyro: starten..."); Serial.flush();
 if (qmiInit()) { imuReady = true; Serial.println("Buddy gyro: QMI8658 klaar (meehangen in bochten)"); }
 else Serial.println("Buddy gyro: QMI8658 niet gevonden");
 Serial.println("Buddy BLE: starten..."); Serial.flush();
 helderheid = buddyPrefs.getUChar("licht", 160); wilLicht = helderheid;
 rec80 = buddyPrefs.getFloat("b80", 0); rec100 = buddyPrefs.getFloat("b100", 0); recG = buddyPrefs.getFloat("gmax", 0);
 totKm = buddyPrefs.getFloat("km", 0); totRit = buddyPrefs.getUInt("rit", 0);
 buddyPrefs.getString("d80", dat80, sizeof dat80); buddyPrefs.getString("d100", dat100, sizeof dat100);
 koppelPin = buddyPrefs.getUInt("pin", 0); if (koppelPin < 100000) { koppelPin = 100000 + esp_random() % 900000; buddyPrefs.putUInt("pin", koppelPin); }
 bleStart();
 if(i2cMutex) xTaskCreatePinnedToCore(gpsTask,"BuddyGPS",8192,nullptr,1,nullptr,0);
 logStart();
 bool wakkerDoorBeweging = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0;
 buddyPrefs.getString("wurl", wCfg.url, sizeof wCfg.url); buddyPrefs.getString("grp", wCfg.grp, sizeof wCfg.grp); buddyPrefs.getString("van", wCfg.van, sizeof wCfg.van); buddyPrefs.getString("naam", wCfg.naam, sizeof wCfg.naam);
 buddyPrefs.getString("ssid", wifiNaam, sizeof wifiNaam); buddyPrefs.getString("pass", wifiPass, sizeof wifiPass);
 xTaskCreatePinnedToCore(netTaak, "BuddyNet", 12288, nullptr, 1, nullptr, 0);
 logRegel("===== OPSTART v%d | reden: %s | heap %u =====", FW_VERSIE, resetReden(), (unsigned)ESP.getFreeHeap());
 if (wakkerDoorBeweging) { logRegel("wakker geworden door beweging (instappen?)"); startGag(22); gagMsg = "Hoi! Daar ben je!"; nextGagT = 20; }
 nextBlink=2;nextGaze=.5f;Serial.println("Buddy v29: koppelcode, navigatie, matrixborden, weer, oor | files/werk/groep zonder telefoon, zwaaien | outfits, instellen vanuit de app | uit/aan met de auto | wifi, eigen limieten, trajecten, updates | logboek | grote cijfers, klok, slapen, nachtstand | 28 animaties, 0-100 vlag, ritjes, G-meter | menu, meehangen, telefoon-koppeling | GPS-transactie in een keer | dubbele framebuffer (tekenen en versturen tegelijk) + LC76G via I2C zonder resetpuls + I2C-scan");
}
void loop(){
 handleTouch();
 {
   static uint32_t logStatusMs = 0; static bool hadFix = false; static int fpsLog = 0;
   bool fixNu = gpsFix && millis() - gpsLastFixMs < 5000;
   if (fixNu != hadFix) { hadFix = fixNu; logRegel(fixNu ? "GPS: FIX! %d satellieten" : "GPS: fix kwijt (%d sat)", (int)gpsSatellites); }
   if (millis() - logStatusMs > 60000) {
     logStatusMs = millis(); int hh = -1, mm = 0; if (klokOk()) lokaleTijd(hh, mm);
     bool ok; float v = spdNow(&ok);
     logRegel("status | klok %02d:%02d | gps %s sat=%d zicht=%d nmea=%lu/%lu | %.0f km/u | tel %s | heap %u | licht %d%s",
       hh, mm, fixNu ? "FIX" : "zoekt", (int)gpsSatellites, (int)gpsInZicht, (unsigned long)nmeaOk, (unsigned long)nmeaBad, v,
       bleConnected ? (phoneFresh() ? "data" : "stil") : "nee", (unsigned)ESP.getFreeHeap(), (int)huidigLicht, slaapt ? " | SLAAPT" : "");
   }
   if (Serial.available()) {
     static char cmd[16]; static uint8_t cl = 0; char c = Serial.read();
     if (c == '\n' || c == '\r') { cmd[cl] = 0; if (!strcmp(cmd, "log")) logDump(Serial); else if (!strcmp(cmd, "wis") && logFs) { logFs->remove(LOGF); logFs->remove(LOGOUD); Serial.println("Logboek gewist."); } cl = 0; }
     else if (cl < sizeof cmd - 1) cmd[cl++] = c;
   }
   if (netNieuw) { netNieuw = false; wCfg = wNieuw; buddyPrefs.putString("wurl", wCfg.url); buddyPrefs.putString("grp", wCfg.grp); buddyPrefs.putString("van", wCfg.van); buddyPrefs.putString("naam", wCfg.naam); logRegel("groep/worker ingesteld (%s)", wCfg.naam); }
   if (setLicht >= 0) { helderheid = (uint8_t)constrain(setLicht * 255 / 100, 20, 255); buddyPrefs.putUChar("licht", helderheid); setLicht = -1; }
   if (setVol >= 0) { buddyVolume = (uint8_t)constrain((int)setVol, 0, 100); if (audioReady) applyCodecVolume(buddyVolume); buddyPrefs.putUChar("volume", buddyVolume); setVol = -1; }
   if (wifiNieuw) { wifiNieuw = false; strcpy(wifiNaam, wifiNaamNieuw); strcpy(wifiPass, wifiPassNieuw); buddyPrefs.putString("ssid", wifiNaam); buddyPrefs.putString("pass", wifiPass); wifiHerstart = true; logRegel("wifi ingesteld: %s", wifiNaam); }
   if (logVraag) { logVraag = false; xTaskCreatePinnedToCore(logNaarTelefoonTaak, "BuddyLogBLE", 4096, nullptr, 1, nullptr, 0); }
 }
 if(millis()-serialHeartbeatMs>=3000){
   serialHeartbeatMs=millis();
   Serial.printf("BUDDY v29 ALIVE | scherm=OK | GPS=%s | heap=%u\n",gpsDataOk?gpsLastStage:"GEEN DATA",(unsigned)ESP.getFreeHeap());
 }
 uint32_t nowUs=micros();
 if(lastRenderUs && (uint32_t)(nowUs-lastRenderUs)<FRAME_INTERVAL_US){
   if(volumeNeedsSave && (uint32_t)(millis()-lastVolumeChange)>700){
     buddyPrefs.putUChar("volume", buddyVolume); volumeNeedsSave=false;
   }
   delay(1); return;
 }
 float dt=lastRenderUs ? fminf(0.10f,(uint32_t)(nowUs-lastRenderUs)/1000000.0f) : 1.0f/30.0f;
 lastRenderUs=nowUs; t+=dt;
 { static uint32_t volT = 0; if (millis() - volT > 8000) { volT = millis(); drawnBox[0] = drawnBox[1] = prevBox = {0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1}; } }  // v28: af en toe alles wissen en opnieuw versturen
 imuStep(dt); gagStep(dt); driveStep(dt); slaapStap();
 if (menuOpen && (int32_t)(menuTot - millis()) < 0) menuOpen = false;
 updateMood(dt); updateParticles(dt);
 uint32_t drawStartUs=micros();
 drawFace();
 uint32_t drawDoneUs=micros();
 flushDirty();
 uint32_t flushDoneUs=drawDoneUs+flushLastUs;  // v16: verzendtijd van het vorige beeld (loopt parallel)
 static uint64_t drawAccumUs=0, flushAccumUs=0;
 drawAccumUs += (uint32_t)(drawDoneUs-drawStartUs);
 flushAccumUs += (uint32_t)(flushDoneUs-drawDoneUs);
 // FPS- en rendertijdmeting via Seriële monitor, zonder extra schermbelasting.
 fpsFrames++;
 if(!fpsWindowUs) fpsWindowUs=nowUs;
 if((uint32_t)(nowUs-fpsWindowUs)>=3000000UL){
   float fps=fpsFrames*1000000.0f/(float)(nowUs-fpsWindowUs);
   float avgDrawMs=fpsFrames ? (drawAccumUs/(float)fpsFrames)/1000.0f : 0;
   float avgFlushMs=fpsFrames ? (flushAccumUs/(float)fpsFrames)/1000.0f : 0;
   Serial.printf("Buddy v29 render: %.1f FPS | draw=%.1f ms | flush=%.1f ms | framebuffer=16bit+dirty | heap=%u | PSRAM=%u\n",fps,avgDrawMs,avgFlushMs,(unsigned)ESP.getFreeHeap(),(unsigned)ESP.getFreePsram());
   fpsFrames=0; fpsWindowUs=nowUs; drawAccumUs=0; flushAccumUs=0;
 }
 if(volumeNeedsSave && (uint32_t)(millis()-lastVolumeChange)>700){
   buddyPrefs.putUChar("volume", buddyVolume); volumeNeedsSave=false;
 }
}
