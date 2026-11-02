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
#if __has_include(<esp_gap_ble_api.h>)   // v55/v60: oude koppelingen kunnen wissen (Bluedroid of NimBLE, afhankelijk van de ESP32-versie)
#include <esp_gap_ble_api.h>
#define BOND_BLUEDROID 1
#elif __has_include("host/ble_store.h")
#include "host/ble_store.h"
#define BOND_NIMBLE 1
#endif

#include <pgmspace.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>
#include "driver/spi_master.h"   // v37: eigen, snellere verzending van het beeld
#include "driver/gpio.h"
#include <sys/time.h>
#include <LittleFS.h>             // v24: logboek in het flashgeheugen
#include <WiFi.h>                 // v25: zelfstandig via de hotspot van je telefoon
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>   // v68: update op de achtergrond binnenhalen, installeren wanneer jij wilt
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <time.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>
static const int FW_VERSIE = 70;
static volatile uint32_t sprintT0 = 0;   // v52: loopt er een 0-100? (voor de retro meter)
static volatile int doeOpdracht = 0;     // v52: DOE:... vanuit de app (1 koekje, 2 focus, 3 weer, 4 zwaai)
// v48: robotje voor op tafel
static uint8_t energie = 100; static bool koekjeZichtbaar = false; static uint32_t focusTot = 0; static uint8_t auKant = 0;
static float gRefX = 0, gRefY = 0, gRefZ = 1; static bool gRefOk = false; static volatile float laatsteSchok = 0; static bool slaapMuts = false;
static char robotTxt[40] = "";
static volatile float wxTemp = -99; static volatile int wxCode = -1, wxRegenOver = -1; static volatile uint32_t wxT = 0;
static bool wxVers();
static bool robotModus = false;
static bool klokOk(); static void lokaleTijd(int &hh, int &mm);   // v47: stilstaan = gewoon een mini-robotje (geen snelheid/bord), rijden = alles terug
// v46: echte geluidjes, weer in beeld, vliegen vangen
enum { GL_HOI, GL_GIECHEL, GL_VROEM, GL_DINGDONG, GL_PLING, GL_OEPS, GL_SLAAP, GL_TADA, GL_RUIMTE, GL_TURBO, GL_PODIUM, GL_TIJD };
void playGeluid(int g);
static bool vliegGevangen = false; static uint16_t vliegTotaal = 0; static char vliegTxt[32] = "";
static volatile bool persJarig = false;   // v45: vandaag jarig (van de app)
// v42: persoonlijkheid
static char persNaam[16] = "", persNaamNieuw[16] = ""; static volatile char persKar = 'E'; static volatile int persLv = 0, persStijl = -1; static volatile uint32_t persEpoch = 0; static volatile bool persNieuw = false;
static uint8_t vriend = 40; static uint8_t gagLike[64] = {0}; static uint8_t vertrekH[24] = {0};
static uint32_t vorigeRitT = 0, lastRitT = 0, eerstT = 0, mijlKm = 0; static uint8_t trucjesGezien = 0; static bool likeVuil = false;
static volatile bool zoekNu = false, otaNu = false, statVraag = false; static uint32_t zoekTot = 0;
static char groetTxt[64] = "", mijlTxt[40] = ""; static bool gegroet = false; static uint32_t groetVanaf = 3000;
static volatile uint8_t oorSoort = 0; static volatile uint32_t oorSoortT = 0;
static float karFactor = 1.0f;
static int trucjesOpen(); static bool gagOntgrendeld(int k);
// v40: ouderwetse snelheidsmeter bij flink optrekken
static uint32_t meterTot = 0, meterStart = 0; static float naald = 0, naaldV = 0;
// v56: sprint-shows die elkaar afwisselen (0 meter, 1 ruimte, 2 turbo, 3 podium, 4 tijdreis)
static uint32_t showTot = 0, showStart = 0, showNa = 0; static uint8_t showSoort = 0, sprintI = 0;
static float wxPrecip = 0, wxRadarNu = -1; static uint32_t wxRadarT = 0;   // v56: regen van de Buienradar
// v39: te-hard-waarschuwing, thuis-tijd, collega in de buurt, wisselbeker
static volatile int setWarn = -1; static bool waarschuwAan = true; static int hudLimiet = 0, hudSnelheid = -1;
static volatile bool buurNieuw = false; static char buurTxt[40] = ""; static bool bekerGetoond = false;
struct Box { int16_t x0, y0, x1, y1; };   // v37: bovenaan i.v.m. automatische functie-koppen van Arduino
static volatile int otaPct = -1; static volatile int otaKlaarV = 0; static volatile bool otaInstalleer = false; static volatile uint32_t otaKlaarT = 0;   // v68
static uint32_t koppelPin = 0; static volatile bool gekoppeld = false; static volatile uint32_t pinTot = 0;
static volatile bool pinOk = false; static volatile uint8_t pinAntw = 0;   // v65: koppelen met de code via de app zelf (geen Android-koppeling meer)
static volatile uint32_t verbindT = 0; static volatile uint8_t koppelMislukt = 0;   // v55: mislukte koppelingen tellen
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
static volatile bool gpsFix = false; static volatile uint32_t gpsEigenMs = 0; static volatile bool posVanTel = false;   // v44: eigen fix van de GPS-chip, of plek van de telefoon
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
Arduino_DataBus *bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3, true);   // v37: gedeelde bus, zodat onze snelle verzending er ook op mag
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
  // v36: kleuren meteen met omgedraaide bytes opslaan (zoals het scherm ze wil). Dan kan het beeld zonder
  // omrekenen rechtstreeks naar het scherm, en is het versturen veel sneller.
  static inline uint16_t sw(uint16_t c) { return (uint16_t)((c >> 8) | (c << 8)); }
  void writePixelPreclipped(int16_t x, int16_t y, uint16_t c) override { Arduino_Canvas::writePixelPreclipped(x, y, sw(c)); }
  void writeFastVLine(int16_t x, int16_t y, int16_t h, uint16_t c) override { Arduino_Canvas::writeFastVLine(x, y, h, sw(c)); }
  // v38: snel vullen: 2 pixels tegelijk (32 bit) en 4x uitgerold, zwart via memset. Was 1 pixel per keer.
  static inline void vulRij(uint16_t *p, int32_t w, uint16_t c) {
    if ((uint8_t)(c >> 8) == (uint8_t)c) { memset(p, (uint8_t)c, (size_t)w * 2); return; }
    if (((uintptr_t)p & 2) && w) { *p++ = c; w--; }
    uint32_t c2 = (uint32_t)c | ((uint32_t)c << 16); uint32_t *q = (uint32_t *)p; int32_t n = w >> 1;
    while (n >= 4) { q[0] = c2; q[1] = c2; q[2] = c2; q[3] = c2; q += 4; n -= 4; }
    while (n-- > 0) *q++ = c2;
    if (w & 1) *(uint16_t *)q = c;
  }
  void writeFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) override {
    if (w < 0) { x += w + 1; w = -w; }
    if (y < 0 || y >= _height || w == 0) return;
    int32_t x0 = x, x1 = (int32_t)x + w - 1; if (x0 < 0) x0 = 0; if (x1 >= _width) x1 = _width - 1; if (x1 < x0) return;
    vulRij(getFramebuffer() + (int32_t)y * _width + x0, x1 - x0 + 1, sw(c));
  }
  void writeFillRectPreclipped(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) override {
    uint16_t *r = getFramebuffer() + (int32_t)y * _width + x; uint16_t k = sw(c);
    for (int16_t j = 0; j < h; j++, r += _width) vulRij(r, w, k);
  }
};
PsramCanvas *frame = nullptr;            // het beeld waar nu in getekend wordt
PsramCanvas *frames[2] = {nullptr, nullptr};  // v16: twee beelden: core 1 tekent het ene terwijl core 0 het andere verstuurt
static uint8_t frameIdx = 0;
static constexpr int32_t QSPI_HZ = 80000000;  // bij strepen of flikkeren: 40000000

// Alleen het stukje scherm dat echt veranderd is wordt opnieuw verstuurd (de "dirty box").
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
static bool nachtStil = true; static uint32_t testPiepT = 0;   // v59: stil tussen 22 en 7 (aan/uit in het volume-venster)
static volatile uint32_t audioBusyT = 0; static char audioStatus[20] = "start"; static volatile uint16_t audioGespeeld = 0;   // v58: wat doet het geluid?
static volatile uint8_t buddyVolume = 85;
Preferences buddyPrefs;
static bool volumeNeedsSave = false;
static uint32_t lastVolumeChange = 0;
static uint32_t lastRenderUs = 0;
static uint32_t fpsWindowUs = 0;
static uint16_t fpsFrames = 0;
static float laatsteFps = 0, laatsteDrawMs = 0, laatsteFlushMs = 0;   // v34: voor logboek en INFO
static uint32_t volumeUiUntil = 0;
static uint32_t lastTouchPoll = 0;
TouchDrvCST92xx buddyTouch;
bool touchReady=false;
int16_t touchX[2] = {0}, touchY[2] = {0};
static const uint32_t AUDIO_RATE = 16000;
static const uint32_t FRAME_INTERVAL_US = 16667; // v37: tot 60 FPS; animaties lopen op echte tijd, dus niet sneller
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
  // v45: echt hartje: twee bollen die elkaar overlappen + een gevulde punt (eerst alleen lijntjes -> leek op billen)
  float r=s*.36f, cy=y-s*.18f;
  circle(x-s*.3f,cy,r,c); circle(x+s*.3f,cy,r,c);
  frame->fillTriangle(X(x-s*.64f),Y(cy+r*.28f),X(x+s*.64f),Y(cy+r*.28f),X(x),Y(y+s*.78f),c);
  frame->fillTriangle(X(x-s*.3f),Y(cy-r*.2f),X(x+s*.3f),Y(cy-r*.2f),X(x),Y(cy+r*.9f),c);
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
  sprintT0 = t0;
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
  if (!strncmp(line, "$PAIR001,59", 11) || !strncmp(line, "$PAIR001,60", 11)) logRegel("GPS antwoordt op hulp: %s (0 = gelukt)", line);   // v64
  char *f[20]={0}; int n=0; f[n++]=line;
  for(char *p=line; *p && n<20; ++p) if(*p==','){*p=0;f[n++]=p+1;}
  if (strstr(f[0],"RMC") && n>7 && f[2][0]=='A') {
    float lat=nmeaCoord(f[3],f[4]), lon=nmeaCoord(f[5],f[6]);
    float knots=atof(f[7]); if (n>8 && f[8][0]) gpsKoers=atof(f[8]); gpsLat=lat; gpsLon=lon; gpsSpeedKmh=knots*1.852f; gpsFix=true; gpsLastFixMs=millis(); gpsEigenMs=millis(); posVanTel=false; gpsNewFix=true;
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
    int q=atoi(f[6]); gpsSatellites=atoi(f[7]); if(q>0 && f[2][0]) {gpsLat=nmeaCoord(f[2],f[3]);gpsLon=nmeaCoord(f[4],f[5]);gpsFix=true;gpsEigenMs=millis();posVanTel=false;gpsLastFixMs=millis();} else if(q==0) gpsFix=false;
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

// v64: GPS-hulp. De plek en tijd van de telefoon als startpunt aan de GPS-chip geven (PAIR590 tijd, PAIR600 plek),
// dan hoeft hij niet alles zelf te zoeken en vindt hij zijn satellieten veel sneller (vooral na een update of herstart).
static volatile bool gpsHulpNu = false; static volatile float gpsHulpLat = 0, gpsHulpLon = 0;
static bool lcSchrijf(const char *s) {
  size_t len = strlen(s); if (len > 120) return false;
  if (!i2cLock(200)) return false;
  uint16_t oudTo = Wire.getTimeOut(); Wire.setTimeOut(300); bool ok = false;
  do {
    if (!lcTouch(0x50)) break; delay(3);
    uint32_t vrij = lcRead32(0xAA510004UL); if (vrij == 0xFFFFFFFFUL || vrij < len) break; delay(3);
    if (lcWriteCr(0xAA531000UL, len) != 0) break; delay(3);
    Wire.beginTransmission(0x58); Wire.write((const uint8_t *)s, len); ok = Wire.endTransmission() == 0;
  } while (0);
  Wire.setTimeOut(oudTo); i2cUnlock(); return ok;
}
static bool lcZin(const char *body) {
  uint8_t cs = 0; for (const char *q = body; *q; q++) cs ^= (uint8_t)*q;
  char z[128]; snprintf(z, sizeof z, "$%s*%02X\r\n", body, cs); return lcSchrijf(z);
}
static void gpsHulpGeven() {
  time_t nu = time(nullptr); if (nu < 1700000000) return;   // klok nog niet gezet
  struct tm u; gmtime_r(&nu, &u);
  char b[100]; snprintf(b, sizeof b, "PAIR590,%d,%d,%d,%d,%d,%d", u.tm_year + 1900, u.tm_mon + 1, u.tm_mday, u.tm_hour, u.tm_min, u.tm_sec);
  bool t = lcZin(b); delay(30);
  snprintf(b, sizeof b, "PAIR600,%.6f,%.6f,0.0,50.0,50.0,0.0,100.0", (double)gpsHulpLat, (double)gpsHulpLon);
  bool p = lcZin(b);
  logRegel("GPS-hulp gegeven: tijd %s, plek %s (%.4f, %.4f)", t ? "ok" : "mislukt", p ? "ok" : "mislukt", (double)gpsHulpLat, (double)gpsHulpLon);
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
    if (gpsHulpNu) { gpsHulpNu = false; gpsHulpGeven(); }   // v64
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
struct PhoneData { int v, lim, camD, camLim, fileD, fileMin; bool werk; int mood; char txt[64]; uint32_t ms; char nav; int navD; char msi[6]; char eta[6]; char buur[24]; bool beker; float lat, lon, hd; bool pos; bool limEst; };
static PhoneData phone = {0, 0, -1, 0, -1, 0, false, -1, "", 0, 0, 0, ""};
static BLECharacteristic *bleTx = nullptr;
static volatile bool bleConnected = false;
static char bleBuf[256]; static size_t bleLen = 0;
static const char *moodKeys[MOOD_COUNT] = {"neutral","content","relaxed","happy","love","excited","focused","startled","worried","bored","sleepy","yawn","sleeping","alert","blow","anticipate"};
static bool phoneFresh() { return phone.ms && millis() - phone.ms < 3500; }
static char evtTxt[44] = ""; static volatile char evtSoort = 0;   // v69: groepsgebeurtenissen van de app
static void parsePhone(char *line) {
  if (!pinOk) {   // v65: eerst de juiste code, anders luistert Buddy+ nergens naar
    if (!strncmp(line, "PIN:", 4)) {
      if ((uint32_t)atol(line + 4) == koppelPin) { pinOk = true; gekoppeld = true; pinTot = 0; koppelMislukt = 0; pinAntw = 1; logRegel("BLE: code goed, verbonden met de app"); }
      else { pinAntw = 2; pinTot = millis() + 60000; logRegel("BLE: verkeerde code"); }
    } else { static uint32_t gevraagd = 0; if (!gevraagd || millis() - gevraagd > 8000) { gevraagd = millis(); pinAntw = 3; } if ((int32_t)(pinTot - millis()) < 5000) pinTot = millis() + 60000; }
    return;
  }
  if (strcmp(line, "LOG?") == 0) { logVraag = true; return; }
  if (!strncmp(line, "EVT:", 4) && line[4] && line[5] == '|') { strncpy(evtTxt, line + 6, sizeof evtTxt - 1); evtTxt[sizeof evtTxt - 1] = 0; evtSoort = line[4]; return; }   // v69
  if (!strncmp(line, "NET:", 4)) {   // v28: NET:<worker-url>\t<groepscode>\t<mijn id>\t<naam>
    char *f[4] = {line + 4, nullptr, nullptr, nullptr}; int n = 1; for (char *q = line + 4; *q && n < 4; q++) if (*q == '\t') { *q = 0; f[n++] = q + 1; }
    if (n == 4) { strncpy(wNieuw.url, f[0], sizeof wNieuw.url - 1); strncpy(wNieuw.grp, f[1], sizeof wNieuw.grp - 1); strncpy(wNieuw.van, f[2], sizeof wNieuw.van - 1); strncpy(wNieuw.naam, f[3], sizeof wNieuw.naam - 1); netNieuw = true; }
    return;
  }
  if (!strncmp(line, "PERS:", 5)) {   // v42: PERS:naam<TAB>karakter<TAB>vriendschap<TAB>level<TAB>km<TAB>stijl laatste rit<TAB>gem stijl<TAB>epoch
    char *f[9] = {0}; int n = 0; f[n++] = line + 5; for (char *q = line + 5; *q && n < 9; q++) if (*q == '\t') { *q = 0; f[n++] = q + 1; }
    if (n >= 7) { strncpy(persNaamNieuw, f[0], sizeof persNaamNieuw - 1); persNaamNieuw[sizeof persNaamNieuw - 1] = 0; persKar = f[1][0]; persLv = atoi(f[3]); persStijl = atoi(f[5]);
      if (n >= 8) persEpoch = strtoul(f[7], nullptr, 10); persJarig = n >= 9 && f[8][0] == '1'; persNieuw = true; }
    return;
  }
  if (!strcmp(line, "ZOEK")) { zoekNu = true; return; }
  if (!strncmp(line, "DOE:", 4)) { const char *d = line + 4; doeOpdracht = !strcmp(d, "koekje") ? 1 : !strcmp(d, "focus") ? 2 : !strcmp(d, "weer") ? 3 : !strcmp(d, "zwaai") ? 4 : !strcmp(d, "mok") ? 5 : !strcmp(d, "juich") ? 6 : !strcmp(d, "sprint") ? 7 : !strcmp(d, "piep") ? 8 : !strcmp(d, "installeer") ? 9 : !strcmp(d, "tunnel") ? 10 : !strcmp(d, "rotonde") ? 11 : 0; return; }
  if (!strcmp(line, "UPD")) { otaNu = true; return; }
  if (!strcmp(line, "STAT?")) { statVraag = true; return; }
  if (!strncmp(line, "SET:", 4)) {   // v27: instellingen uit de app, bv. SET:l=65;v=80;a=1;o=pet
    for (char *tok = strtok(line + 4, ";"); tok; tok = strtok(nullptr, ";")) {
      if (tok[0] == 'l' && tok[1] == '=') setLicht = atoi(tok + 2);
      else if (tok[0] == 'v' && tok[1] == '=') setVol = atoi(tok + 2);
      else if (tok[0] == 'a' && tok[1] == '=') { float f = atof(tok + 2); if (f > .2f && f < 5) animFactor = f; }
      else if (tok[0] == 'w' && tok[1] == '=') setWarn = atoi(tok + 2) ? 1 : 0;   // v39: waarschuwen bij ruim te hard
      else if (tok[0] == 'o' && tok[1] == '=') { strncpy(outfitK, tok + 2, sizeof outfitK - 1); outfitK[sizeof outfitK - 1] = 0; outfitT = millis(); }
    }
    return;
  }
  if (!strncmp(line, "WIFI:", 5)) { char *tab = strchr(line + 5, '\t'); if (tab) { *tab = 0; strncpy(wifiNaamNieuw, line + 5, 32); wifiNaamNieuw[32] = 0; strncpy(wifiPassNieuw, tab + 1, 64); wifiPassNieuw[64] = 0; wifiNieuw = true; } return; }
  char *f[18] = {0}; int n = 0; f[n++] = line;
  for (char *q = line; *q && n < 18; ++q) if (*q == '|') { *q = 0; f[n++] = q + 1; }
  if (n < 9 || strcmp(f[0], "B1") != 0) return;
  PhoneData d = phone;
  d.v = atoi(f[1]); d.lim = atoi(f[2]); d.camD = atoi(f[3]); d.camLim = atoi(f[4]);
  d.fileD = atoi(f[5]); d.fileMin = atoi(f[6]); d.werk = atoi(f[7]) == 1; d.mood = -1;
  for (int i = 0; i < MOOD_COUNT; i++) if (strcmp(f[8], moodKeys[i]) == 0) d.mood = i;
  strncpy(d.txt, n > 9 ? f[9] : "", sizeof(d.txt) - 1); d.txt[sizeof(d.txt) - 1] = 0;
  d.nav = (n > 10 && f[10][0]) ? f[10][0] : 0; d.navD = d.nav ? atoi(f[10] + 1) : 0;   // v29: navigatiepijl, bv. R350
  strncpy(d.msi, n > 11 ? f[11] : "", sizeof(d.msi) - 1); d.msi[sizeof(d.msi) - 1] = 0;  // v29: matrixbord: x of 70
  strncpy(d.eta, n > 12 ? f[12] : "", sizeof(d.eta) - 1); d.eta[sizeof(d.eta) - 1] = 0;   // v39: thuis-tijd, bv. 18:42
  if (n > 13 && f[13][0] && strcmp(f[13], phone.buur)) { char *tl = strchr(f[13], '~'); int km = tl ? atoi(tl + 1) : 0; if (tl) *tl = 0;
    if (km > 0) snprintf(buurTxt, sizeof buurTxt, "%s rijdt ~%d km verderop!", f[13], km); else snprintf(buurTxt, sizeof buurTxt, "%s is vlakbij! Zwaai!", f[13]);
    if (tl) *tl = '~'; buurNieuw = true; }
  strncpy(d.buur, n > 13 ? f[13] : "", sizeof(d.buur) - 1); d.buur[sizeof(d.buur) - 1] = 0;
  d.beker = n > 14 && f[14][0] == '1';
  d.limEst = n > 16 && f[16][0] == '1';   // v68: bord is maar geschat (dan niet leren)
  d.pos = false;   // v44: plek van de telefoon (lat~lon~koers)
  if (n > 15 && f[15][0]) { char *a = f[15], *b = strchr(a, '~'); if (b) { *b = 0; char *c = strchr(b + 1, '~'); if (c) *c = 0;
      d.lat = atof(a); d.lon = atof(b + 1); d.hd = (c && c[1]) ? atof(c + 1) : -1; d.pos = d.lat > 30 && d.lat < 72 && d.lon > -25 && d.lon < 45; } }
  d.ms = millis(); phone = d;
}
#if defined(CONFIG_BLUEDROID_ENABLED) && !defined(CONFIG_NIMBLE_ENABLED)
// v64: laten weten of koppelen lukt; een oude, kapotte koppeling meteen opruimen
class BuddySecCb : public BLESecurityCallbacks {
  uint32_t onPassKeyRequest() override { return koppelPin; }
  void onPassKeyNotify(uint32_t) override { pinTot = millis() + 60000; }
  bool onSecurityRequest() override { return true; }
  bool onConfirmPIN(uint32_t) override { return true; }
  void onAuthenticationComplete(esp_ble_auth_cmpl_t c) override {
    if (c.success) { gekoppeld = true; pinTot = 0; logRegel("BLE: koppelen gelukt"); }
    else { logRegel("BLE: koppelen mislukt (reden 0x%02X), oude koppeling gewist", (unsigned)c.fail_reason); esp_ble_remove_bond_device(c.bd_addr); }
  }
};
#endif
class BuddySrvCb : public BLEServerCallbacks {
  void onConnect(BLEServer *) override { bleConnected = true; pinOk = false; verbindT = millis(); if (!gekoppeld) pinTot = millis() + 60000; logRegel("BLE: telefoon verbonden"); }
  void onDisconnect(BLEServer *) override { bleConnected = false; pinOk = false; if (!gekoppeld && verbindT && millis() - verbindT > 8000 && koppelMislukt < 250) koppelMislukt++; verbindT = 0; pinTot = 0; logRegel("BLE: telefoon weg"); BLEDevice::startAdvertising(); }
};
class BuddyRxCb : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    auto v = c->getValue();
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
  #if defined(CONFIG_NIMBLE_ENABLED)
  BLECharacteristic *rx = svc->createCharacteristic(NUS_RX, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);   // v61: NimBLE (core 3.3)
#else
  BLECharacteristic *rx = svc->createCharacteristic(NUS_RX, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
#endif
  rx->setCallbacks(new BuddyRxCb());
  rx->setAccessPermissions(ESP_GATT_PERM_WRITE);   // v65: geen Android-koppeling meer; de app stuurt de code (PIN:...) en pas dan luistert Buddy+
  bleTx = svc->createCharacteristic(NUS_TX, BLECharacteristic::PROPERTY_NOTIFY);
#if !defined(CONFIG_NIMBLE_ENABLED)
  bleTx->addDescriptor(new BLE2902());   // NimBLE maakt dit zelf aan
#endif
  svc->start();
  BLEAdvertising *adv = BLEDevice::getAdvertising(); adv->addServiceUUID(NUS_SVC); adv->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.println("BLE: zichtbaar als 'Buddy-scherm'");
}
static volatile bool logBezig = false;   // v43: tijdens het versturen van het logboek niets anders over Bluetooth (anders raakt het door elkaar)
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
  logBezig = false;
  vTaskDelete(NULL);
}
static void bleSend(const char *t) { if (bleTx && bleConnected && !logBezig) { bleTx->setValue((uint8_t *)t, strlen(t)); bleTx->notify(); } }

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
  if (schok > laatsteSchok) laatsteSchok = schok;   // v48: voor schudden (robotStap leest en wist)
}

// ===================== v19: rond menu (lang drukken) =====================
static bool menuOpen = false; static uint32_t menuTot = 0, infoTot = 0, aaiTot = 0;
// v30: vensters over alles heen (0 = geen, 1 = info, 2 = volume, 3 = helderheid); sluiten met TERUG
static uint8_t venster = 0; static uint32_t vensterTot = 0; static bool lichtOpslaan = false;
static uint8_t helderheid = 160;
static const char *menuTxt[7] = {"VOLUME", "HELDER", "FLITSER", "INFO", "ZWAAI", "PLEK", "FOCUS"};   // v48: FOCUS = 25 minuten samen werken
static void drawMenu() {
  boxAdd(0, 0, 240, 240);
  frame->fillCircle(X(120), Y(120), S(118), 0x0841);
  for (int i = 0; i < 7; i++) {
    float a = -PI2 / 4 + i * PI2 / 7; float cx = 120 + cosf(a) * 80, cy = 120 + sinf(a) * 80;
    frame->fillCircle(X(cx), Y(cy), S(29), i == 2 ? 0x8000 : (i == 4 ? 0x0451 : (i == 5 ? 0x600F : (i == 6 ? 0x0320 : 0x18E3))));
    frame->drawCircle(X(cx), Y(cy), S(29), EYE);
    frame->setTextColor(0xFFFF); frame->setTextSize(2);
    int tw = strlen(menuTxt[i]) * 12; frame->setCursor(X(cx) - tw / 2, Y(cy) - 7); frame->print(menuTxt[i]);
  }
  frame->fillCircle(X(120), Y(120), S(26), 0x2124); frame->drawCircle(X(120), Y(120), S(26), EYE);
  frame->setTextColor(EYE); frame->setTextSize(2); frame->setCursor(X(120) - 30, Y(120) - 7); frame->print("TERUG");
}
// v21: records en ritjes (blijven bewaard in het geheugen van het bordje)
static float rec80 = 0, rec100 = 0, recG = 0, totKm = 0; static uint32_t totRit = 0; static char dat80[9] = "", dat100[9] = "";
static const char *netStatus = "wifi uit";
// v67: checklist (info-venster): wat werkt er en wat niet
static volatile uint32_t netOkT = 0, netFoutT = 0; static volatile int netFoutCode = 0; static volatile int8_t netDnsOk = -1, netTcpOk = -1; static volatile uint8_t bordBron = 0;
static void infoRegel(int y, const char *r) { frame->setCursor(X(120) - (int)strlen(r) * 6, Y(y)); frame->print(r); }
static void infoKl(int y, const char *r, uint16_t kl) { frame->setTextColor(kl); infoRegel(y, r); }
static void drawInfo() {   // v67: checklist
  const uint16_t OK = 0x87F0, NEE = 0xF9A6, MID = 0xFEA0; frame->setTextSize(2);
  char r[48]; int y = 44; uint32_t nu = millis();
  if (otaKlaarV) { snprintf(r, sizeof r, "TIK: UPDATE v%d", (int)otaKlaarV); infoKl(y, r, GOLD); } else { snprintf(r, sizeof r, "Buddy+ v%d", FW_VERSIE); infoKl(y, r, EYE); } y += 16;
  bool bt = bleConnected && pinOk;
  snprintf(r, sizeof r, "BT %s", bt ? (phoneFresh() ? "OK app+code" : "OK, app stil") : bleConnected ? "geen code" : "niet verbonden"); infoKl(y, r, bt && phoneFresh() ? OK : bt ? MID : NEE); y += 16;
  bool wf = WiFi.status() == WL_CONNECTED;
  snprintf(r, sizeof r, "WIFI %s %.10s", wf ? "OK" : (wifiNaam[0] ? "zoekt" : "uit"), wifiNaam); infoKl(y, r, wf ? OK : NEE); y += 16;
  bool netGoed = netOkT && (!netFoutT || netOkT > netFoutT);
  if (!wf) snprintf(r, sizeof r, "NET -");
  else if (netGoed) { time_t t = time(nullptr) - (nu - netOkT) / 1000; struct tm lt; localtime_r(&t, &lt); snprintf(r, sizeof r, "NET OK %02d:%02d", lt.tm_hour, lt.tm_min); }
  else if (netDnsOk == 0) snprintf(r, sizeof r, "NET X geen DNS");
  else if (netTcpOk == 0) snprintf(r, sizeof r, "NET X geen verb.");
  else if (netFoutT) snprintf(r, sizeof r, "NET X fout %d", netFoutCode);
  else snprintf(r, sizeof r, "NET nog niet getest");
  infoKl(y, r, !wf ? 0x8410 : netGoed ? OK : NEE); y += 16;
  snprintf(r, sizeof r, "GPS %s %d/%d", posVanTel ? "via tel" : (gpsFix ? "FIX" : (gpsDataOk ? "zoekt" : "geen data")), (int)gpsSatellites, (int)gpsInZicht); infoKl(y, r, (gpsFix && !posVanTel) ? OK : posVanTel ? MID : NEE); y += 16;
  snprintf(r, sizeof r, "BORD %s", bordBron == 1 ? "via telefoon" : bordBron == 2 ? "eigen kaart" : bordBron == 3 ? "van flitser" : bordBron == 4 ? "geleerd" : bordBron == 5 ? "onthouden" : "geen"); infoKl(y, r, bordBron ? OK : NEE); y += 16;
  snprintf(r, sizeof r, "GELUID %s %d%%", audioReady ? "OK" : "FOUT", buddyVolume); infoKl(y, r, audioReady ? OK : NEE); y += 16;
  if (rec100 > 0) snprintf(r, sizeof r, "0-100 %.1fs %.2fG", rec100, recG); else snprintf(r, sizeof r, "Max %.2fG", recG); infoKl(y, r, GOLD);
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
  else if (venster == 2) { drawSlider(buddyVolume, "VOLUME", EYE);   // v59: geluid testen en nachtstand, ook zonder telefoon
    char fb[28]; snprintf(fb, sizeof fb, "FOUT: %s", audioStatus);
    const char *st = !audioReady ? fb : audioBusy ? "SPEELT" : (testPiepT && millis() - testPiepT < 2500) ? "PIEP!" : "GELUID OK";
    frame->setTextColor(audioReady ? 0x87F0 : 0xF9A6); frame->setTextSize(2); frame->setCursor(X(120) - (int)strlen(st) * 6, Y(101)); frame->print(st);
    roundRect(42, 150, 74, 22, 11, 0x18E3); frame->drawRoundRect(X(42), Y(150), S(74), S(22), S(11), EYE);
    frame->setTextColor(0xFFFF); frame->setTextSize(2); frame->setCursor(X(79) - 24, Y(161) - 7); frame->print("TEST");
    roundRect(124, 150, 74, 22, 11, nachtStil ? 0x2104 : 0x18E3); frame->drawRoundRect(X(124), Y(150), S(74), S(22), S(11), nachtStil ? 0x8410 : EYE);
    frame->setTextColor(0xFFFF); frame->setTextSize(1); frame->setCursor(X(161) - 27, Y(156)); frame->print(nachtStil ? "22-7 STIL" : "22-7 GELUID");
    frame->setCursor(X(161) - 24, Y(164)); frame->print(nachtStil ? "tik: aan" : "tik: stil"); }
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
  float a = atan2f(dy, dx); int i = (int)lroundf((a + PI2 / 4) / (PI2 / 7)); i = ((i % 7) + 7) % 7;
  menuOpen = false;
  if (i == 0) openVenster(2);
  else if (i == 1) openVenster(3);
  else if (i == 2) { bleSend("MELD:cam"); mood = bleConnected ? ALERT : WORRIED; lastMood = -1; idleMoodUntilMs = millis() + 2500; Serial.println(bleConnected ? "Flitser gemeld via telefoon" : "Flitser melden: telefoon niet verbonden"); }
  else if (i == 3) openVenster(1);
  else if (i == 6) { if (focusTot) { focusTot = 0; startGag(7); gagMsg = "Focus gestopt. Even pauze?"; } else { focusTot = millis() + 25UL * 60000; startGag(7); gagMsg = "Focus! 25 minuten, ik doe mee."; playGeluid(GL_PLING); } }
  else if (i == 5) { if (bleConnected) { bleSend("MELD:plek"); startGag(7); gagMsg = "Plek onthouden!"; } else { startGag(27); gagMsg = "Telefoon niet verbonden..."; } }
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
  // v39
  {6.0f, G_IDLE | G_DRIVE, HAPPY, "Pepernoten! Heerlijk avondje..."},   // 36 Sinterklaas
  {6.4f, G_IDLE | G_DRIVE, LOVE, "Het sneeuwt! Fijne feestdagen."},     // 37 kerst
  {5.0f, 0, EXCITED, "De wisselbeker is van jou!"},                     // 38 wisselbeker (alleen op seintje)
  {3.6f, 0, WORRIED, "Rustig aan... ruim te hard!"},                    // 39 te hard (alleen op seintje)
  // v42
  {6.0f, G_IDLE | G_DRIVE, EXCITED, "Vuurwerk! Gelukkig nieuwjaar!"},   // 40 vuurwerk (oud en nieuw)
  {6.0f, 0, EXCITED, "Headbangen!"},                                    // 41 harde muziek
  {7.0f, 0, RELAXED, "Mooi nummer..."},                                 // 42 zachte muziek: meewiegen
  {4.5f, 0, FOCUSED, "Ik luister mee..."},                              // 43 er wordt gepraat
  {4.0f, 0, BORED, "Hmpf... je was me vergeten."},                      // 44 mokken
  {5.0f, 0, SLEEPY, "Koffie...?"},                                      // 45 vroeg: koffie
  // v48: robotje
  {3.5f, 0, STARTLED, "Wieeee... ik ben duizelig!"},                    // 46 geschud
  {1.8f, 0, STARTLED, "Au! Mijn oog!"},                                 // 47 in zijn oog getikt
  {2.4f, 0, LOVE, "Hihi, dat kriebelt!"},                               // 48 op zijn wang
  {3.2f, 0, EXCITED, "Hahaha! Stop! Hihihi!"},                          // 49 kietelen
  {3.0f, 0, EXCITED, "Kiekeboe!"},                                      // 50 hand erop
  {3.6f, 0, HAPPY, "Nom nom nom... lekker!"},                           // 51 koekje eten
  {5.0f, G_IDLE, CONTENT, "Kijk, de tijd!"},                            // 52 klokbordje (alleen als robotje)
  {6.0f, 0, FOCUSED, "Het weer van vandaag:"},                          // 53 weerman
  {3.5f, 0, HAPPY, "Goeiemorgen! *uitrek*"},                            // 54 uitrekken
  {4.0f, 0, ALERT, "Hé, zet me rechtop!"},                              // 55 scheef/plat/op zijn kop
  {3.0f, 0, BORED, "Ik heb trek... tik op het koekje!"},                // 56 trek
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
// v69: ritten onthouden en naar de app sturen, zodat je account ook de ritten krijgt die de telefoon miste
struct PlusRit { uint32_t t0, t1; uint16_t km10, vmax, b100; uint8_t g100, vrij; };
static PlusRit plusRit[40]; static int plusRitN = 0; static volatile bool ritStuurNu = false; static uint32_t ritStuurT = 0;
static void plusRitBewaar() { buddyPrefs.putBytes("ritten", plusRit, sizeof(PlusRit) * plusRitN); }
static void plusRitLaad() { size_t n = buddyPrefs.getBytesLength("ritten"); if (n && n <= sizeof(plusRit) && n % sizeof(PlusRit) == 0) { buddyPrefs.getBytes("ritten", plusRit, n); plusRitN = n / sizeof(PlusRit); } }
static void startGag(int k) {
  if (k == 1) { vliegGevangen = false; }
  if (robotModus && k >= 0 && GAGS[k].flags) {   // v47: als robotje bij elk trucje een geluidje (niet 's nachts)
    int hh = 12, mm = 0; if (klokOk()) lokaleTijd(hh, mm);
    if ((!nachtStil || (hh >= 7 && hh < 22)) && random(3) == 0) { int m = GAGS[k].mood; playGeluid(m == EXCITED ? GL_TADA : m == LOVE ? GL_GIECHEL : (m == HAPPY || m == CONTENT) ? GL_HOI : (m == SLEEPY || m == YAWN || m == BORED) ? GL_SLAAP : (m == ALERT || m == WORRIED || m == STARTLED) ? GL_OEPS : GL_PLING); }
  }
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
    bool sint = fo && !strcmp(fo, "mijter"), kerst = fo && !strcmp(fo, "kerstmuts"), nieuwjaar = fo && !strcmp(fo, "feesthoed");
    for (int k = 0; k < GAG_N; k++) {
      uint8_t f = GAGS[k].flags;
      if ((k == 34 || k == 35) && !halloween) continue;
      if (k == 36 && !sint) continue;
      if (k == 37 && !kerst) continue;
      if (k == 40 && !nieuwjaar) continue;
      if (k == 52 && !(robotModus && klokOk())) continue;
      if (!gagOntgrendeld(k)) continue;   // v42: sommige trucjes leert hij pas na genoeg km samen
      if (k == 31 && klokOk()) { int hh, mm; lokaleTijd(hh, mm); if (hh >= 7 && hh < 18) continue; }   // vuurvliegjes alleen 's avonds
      if ((stil && (f & G_IDLE)) || (rijdt && (f & G_DRIVE)) || (stil && tripActief && (f & G_STOP))) kies[n++] = k;
    }
    if (!n) { nextGagT = t + 3; return; }
    int som = 0; for (int i = 0; i < n; i++) som += 4 + min(12, gagLike[kies[i]] * 2);   // v42: wat jij leuk vindt komt vaker
    int r = random(som), k = kies[0]; for (int i = 0; i < n; i++) { r -= 4 + min(12, gagLike[kies[i]] * 2); if (r < 0) { k = kies[i]; break; } }
    if (klokOk()) { int hh, mm; lokaleTijd(hh, mm); if (hh >= 22 && stil && random(3) == 0) k = 11; }   // laat: knikkebollen
    { int fav = -1, fl = 2; for (int i = 0; i < n; i++) if (gagLike[kies[i]] > fl) { fl = gagLike[kies[i]]; fav = kies[i]; }   // v45: af en toe zijn lievelingstrucje
      if (fav >= 0 && random(8) == 0) { k = fav; startGag(k); gagMsg = "Mijn lievelingstrucje! (dankzij jou)"; nextGagT = t + GAGS[k].dur + frand(10, 20) * animFactor * karFactor; return; } }
    if (halloween && random(3) == 0) k = random(2) ? 34 : 35;   // op Halloween vaker het spookje en de pompoen
    if (sint && random(4) == 0) k = 36;
    if (kerst && random(4) == 0) k = 37;
    if (nieuwjaar && random(3) == 0) k = 40;
    startGag(k);
    nextGagT = t + GAGS[k].dur + (robotModus ? frand(14, 32) : (stil ? frand(8, 16) : frand(15, 30))) * animFactor * karFactor;   // v51: als robotje ook gewoon even rondkijken en 'saai' zijn
    return;
  }
  float tt = t - gagT0; const GagDef &d = GAGS[gagAct];
  if (tt > d.dur) { gagAct = -1; gagMood = -1; gagMsg = nullptr; return; }
  switch (gagAct) {
    case 0: gdir(0, tt < 3.6f ? -.7f : .2f); fdy = tt > 1.5f ? fabsf(sinf(tt * 6)) * 3 : 0; if (tt > 3.6f) { gagMsg = "Kom op, groen!"; gagMood = ANTICIPATE; } break;
    case 1: { float x = sinf(tt * 2.1f) * .9f + sinf(tt * 5) * .1f, y = cosf(tt * 1.6f) * .6f; g_fx = 120 + x * 75; g_fy = 88 + y * 40;
      if (tt < 4.8f) gdir(x * .9f, y * .9f); else { gdir(0, .1f); gagMood = HAPPY; gagMsg = vliegGevangen ? vliegTxt : "Hij ontsnapte! Tik hem de volgende keer!"; } } break;
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
    case 36: gdir(sinf(tt * 2) * .5f, -.5f); if (tt > 4 && !g_b) { g_b = true; gagMsg = "Lekker! Nog eentje?"; } break;
    case 37: gdir(sinf(tt * 1.3f) * .4f, -.6f); break;
    case 38: gdir(0, -.7f); if (!g_b && tt > .6f) { g_b = true; burstAt(120, 52, 14); } fdy = -fabsf(sinf(tt * 6)) * 4; break;
    case 39: gdir(0, .2f); fdx = sinf(tt * 30) * (tt < 1 ? 2.5f : 0); break;
    case 40: gdir(sinf(tt * 1.7f) * .5f, -.7f); break;
    case 41: gdir(0, sinf(tt * 14) > 0 ? .6f : -.4f); fdy = sinf(tt * 14) * 6; if ((int)(tt * 3) != g_nn) { g_nn = (int)(tt * 3); noot(frand(60, 180), 60); } break;
    case 42: fdx = sinf(tt * 2.5f) * 8; gdir(sinf(tt * 2.5f) * .5f, 0); if ((int)tt != g_nn) { g_nn = (int)tt; noot(frand(70, 170), 70); } break;
    case 43: gdir(sinf(tt * 1.2f) * .8f, -.1f); break;
    case 44: gdir(-.7f, .3f); if (tt > 3.2f) { gagMood = HAPPY; gagMsg = "...oké, ik vergeef het je."; } break;
    case 45: gdir(.5f, .5f); eyeL = eyeR = .55f; break;
    case 46: fxSpiral = true; fdx = sinf(tt * 9) * 4; fdy = cosf(tt * 7) * 2; break;
    case 47: if (auKant == 0) eyeL = .08f; else eyeR = .08f; fdx = tt < .3f ? (auKant ? -3 : 3) : 0; gdir(auKant ? -.4f : .4f, .2f); break;
    case 48: gdir(0, .4f); break;
    case 49: fdx = sinf(tt * 30) * 3; fdy = -fabsf(sinf(tt * 12)) * 3; eyeL = eyeR = .35f; break;
    case 50: if (tt < 1.2f) { eyeL = eyeR = .05f; gagMsg = "Hé! Het is donker!"; } else { gagMsg = "Kiekeboe!"; fdy = -fabsf(sinf(tt * 8)) * 4; } break;
    case 51: gdir(.2f, .6f); fdy = sinf(tt * 14) * 1.5f; break;
    case 52: gdir(0, .5f); if (klokOk()) { int hh, mm; lokaleTijd(hh, mm); snprintf(robotTxt, sizeof robotTxt, "Het is %02d:%02d", hh, mm); gagMsg = robotTxt; } break;
    case 53: gdir(-.3f, .4f); break;
    case 54: fdy = -sinf(fminf(tt, 3.1f)) * 8; if (tt < 1.2f) eyeL = eyeR = .3f; break;
    case 55: gdir(0, -.5f); fdx = sinf(tt * 5) * 2; break;
    case 56: gdir(.6f, .4f); break;
  }
}
static void gagDraw() {
  if (gagAct < 0) return;
  float tt = t - gagT0;
  switch (gagAct) {
    case 1: if (tt < 4.8f) { circle(g_fx, g_fy, 2.6f, EYE); float f = sinf(tt * 60) > 0 ? 4 : 2; ellipseF(g_fx - 4, g_fy - 3, 4, f, 0x9CF3); ellipseF(g_fx + 4, g_fy - 3, 4, f, 0x9CF3); } break;
    case 2: if (g_r > 0) { float x = 120 + sinf(tt * 3) * 2, y = 182 - g_r * .4f + faceOy * .5f;   // v45: gevulde bel, mondje verdwijnt erachter
      circle(x, y, g_r, 0x1A4B); circle(x, y, g_r * .88f, 0x0C2A);
      for (int i = 0; i < 3; i++) frame->drawCircle(X(x), Y(y), S(g_r) - i, EYE);
      circle(x - g_r * .38f, y - g_r * .4f, fmaxf(1.5f, g_r * .16f), 0xFFFF); circle(x - g_r * .12f, y - g_r * .55f, fmaxf(1.f, g_r * .07f), 0xDFFF);
      circle(x, y + g_r + 2, 3.5f, EYE); } break;
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
    case 40: for (int i = 0; i < 3; i++) { float p = fmodf(tt * .7f + i * .33f, 1.f), x = 60 + i * 60;
        uint16_t kl = i == 0 ? 0xF9E0 : (i == 1 ? 0x07FF : 0xFFE0);
        if (p < .4f) circle(x, 220 - p / .4f * 150, 2.5f, 0xFFFF);
        else { float r = (p - .4f) / .6f * 34; for (int j = 0; j < 10; j++) { float a = j * PI2 / 10; circle(x + cosf(a) * r, 70 + sinf(a) * r, 2.6f * (1 - (p - .4f) / .6f) + .8f, kl); } } } break;
    case 45: roundRect(160, 160, 30, 26, 5, 0xFFFF); circle(193, 172, 7, 0xFFFF); circle(193, 172, 3.5f, BG); roundRect(163, 163, 24, 6, 2, 0x8A22);
      for (int j = 0; j < 3; j++) { float x = 167 + j * 8 + sinf(tt * 3 + j) * 2; line(x, 152, x + sinf(tt * 4 + j) * 3, 140, 0xBDF7, 2); } break;
    case 51: { float k = fminf(1, tt / 3.f); float r = 11 * (1 - k * .85f); if (r > 1) { circle(150, 168, r, 0xA2A0); circle(147, 165, 1.6f, 0x3180); circle(153, 170, 1.6f, 0x3180); }
      if ((int)(tt * 6) % 2) for (int i = 0; i < 3; i++) circle(130 + i * 9, 186 + (i % 2) * 4, 1.6f, 0xA2A0); } break;
    case 52: { roundRect(78, 150, 84, 40, 6, 0xFFFF); roundRect(81, 153, 78, 34, 5, 0x18E3); line(90, 150, 100, 130, 0xFFFF, 2); line(150, 150, 140, 130, 0xFFFF, 2);
      if (klokOk()) { int hh, mm; lokaleTijd(hh, mm); char tb[8]; snprintf(tb, sizeof tb, "%02d:%02d", hh, mm); segTekst(tb, 120, 170, 18, GOLD); } } break;
    case 53: { roundRect(30, 140, 180, 64, 12, 0x0841); frame->drawRoundRect(X(30), Y(140), S(180), S(64), S(12), EYE);
      if (wxVers()) { float ix = 66, iy = 172; int c = wxCode;
        if (c <= 1) { circle(ix, iy, 13, GOLD); for (int i = 0; i < 8; i++) { float a = i * PI2 / 8 + tt; line(ix + cosf(a) * 16, iy + sinf(a) * 16, ix + cosf(a) * 21, iy + sinf(a) * 21, GOLD, 2); } }
        else { if (c <= 3) circle(ix - 6, iy - 6, 10, GOLD); circle(ix - 7, iy + 1, 10, 0xC618); circle(ix + 7, iy - 1, 12, 0xC618); roundRect(ix - 17, iy + 2, 34, 9, 4, 0xC618);
          if ((c >= 51 && c < 70) || (c >= 80 && c <= 82)) for (int i = -1; i <= 1; i++) line(ix + i * 8, iy + 14, ix + i * 8 - 2, iy + 21 + fmodf(tt * 20, 4), 0x7E5F, 2);
          if (c >= 71 && c < 80) for (int i = -1; i <= 1; i++) circle(ix + i * 8, iy + 18, 2.2f, 0xFFFF); }
        char tg[8]; snprintf(tg, sizeof tg, "%d", (int)lroundf(wxTemp)); segTekst(tg, 140, 164, 22, EYE); circle(140 + strlen(tg) * 8.5f + 4, 154, 2.6f, EYE);
        frame->setTextColor(0xBDF7); frame->setTextSize(2); frame->setCursor(X(98), Y(186));
        if (wxRegenOver == 0) frame->print("Het regent"); else if (wxRegenOver > 0 && wxRegenOver <= 120) { char rr[20]; snprintf(rr, sizeof rr, "Regen over %d min", (int)wxRegenOver); frame->print(rr); } else frame->print("Droog");
      } else { frame->setTextColor(0xBDF7); frame->setTextSize(2); frame->setCursor(X(52), Y(166)); frame->print("Geen weer: geen wifi"); } } break;
    case 54: if (tt < 2.5f) { line(40, 120 - sinf(tt * 2) * 30, 20, 90 - sinf(tt * 2) * 40, EYE, 3); line(200, 120 - sinf(tt * 2) * 30, 220, 90 - sinf(tt * 2) * 40, EYE, 3); } break;
    case 36: for (int i = 0; i < 8; i++) { float y = fmodf(tt * 55 + i * 41, 210) + 15, x = 34 + i * 24 + sinf(tt * 2 + i) * 4; if (y < 205) { circle(x, y, 4.5f, 0xA2A0); circle(x - 1.2f, y - 1.2f, 1.4f, 0xC3A6); } } break;
    case 37: for (int i = 0; i < 14; i++) { float y = fmodf(tt * 32 + i * 29, 215) + 12, x = 22 + i * 14.5f + sinf(tt * 1.7f + i) * 7; if (y < 208) circle(x, y, (i % 3) ? 2.2f : 3.2f, 0xFFFF); }
      if (tt > .5f) { float g = fminf(1, (tt - .5f) * 2); triF(196, 120 - 30 * g, 182, 148, 210, 148, 0x2589); triF(196, 132 - 30 * g, 178, 166, 214, 166, 0x2589); roundRect(192, 166, 8, 9, 1, 0x8A22); star(196, 118 - 30 * g, 5, GOLD); } break;
    case 38: { float y = 58 - fminf(1, tt * 1.5f) * 10; ellipseF(120, y, 16, 13, GOLD); roundRect(104, y - 13, 32, 8, 2, GOLD); circle(101, y - 4, 6, GOLD); circle(101, y - 4, 3, BG); circle(139, y - 4, 6, GOLD); circle(139, y - 4, 3, BG);
      roundRect(116, y + 10, 8, 10, 1, GOLD); roundRect(107, y + 19, 26, 6, 2, GOLD); if ((int)(tt * 4) % 2) star(120, y - 2, 4, 0xFFFF); } break;
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
    char m[24]; snprintf(m, sizeof m, "SPRINT:%.1f", tt); bleSend(m); logRegel("0-100: %.1f s%s", tt, flagRec ? " RECORD" : "");
    showSoort = 1 + sprintI % 4; showNa = flagTot; sprintI++; buddyPrefs.putUChar("spri", sprintI); }   // v57: na de vlag om de beurt een van de vier shows
  if (!ok) return;
  if (v > 5) { stilSinds = 0; if (!rijdtSinds) rijdtSinds = nu; } else { rijdtSinds = 0; if (!stilSinds) stilSinds = nu; }
  if (!tripActief && rijdtSinds && nu - rijdtSinds > 5000) { tripActief = true; tripKm = tripMax = tripG = tripBest = 0; tripStart = nu; logRegel("Rit begonnen"); }
  if (tripActief) {
    tripKm += v * dt / 3600.f; if (v > tripMax) tripMax = v; if (gNu > tripG && gNu < 1.3f && v > 10) tripG = gNu;
    if (stilSinds && nu - stilSinds > 180000) {   // 3 minuten stil = rit klaar
      tripActief = false;
      if (tripKm > .3f) {
        int mijlOud = (int)(totKm / 500);
        totKm += tripKm; totRit++; buddyPrefs.putFloat("km", totKm); buddyPrefs.putUInt("rit", totRit);
        if ((int)(totKm / 500) > mijlOud) { snprintf(evtTxt, sizeof evtTxt, "%d KM SAMEN!", (int)(totKm / 500) * 500); evtSoort = 'f'; logRegel("mijlpaal: %d km", (int)(totKm / 500) * 500); }
        else if (totRit % 50 == 0) { snprintf(evtTxt, sizeof evtTxt, "RIT %u SAMEN!", (unsigned)totRit); evtSoort = 'h'; }
        if (tripG > recG) { if (recG > .2f && evtSoort == 0) { snprintf(evtTxt, sizeof evtTxt, "G-RECORD %.2f!", (double)tripG); evtSoort = 'f'; } recG = tripG; buddyPrefs.putFloat("gmax", recG); }
        ritKlaarTot = nu + 12000; logRegel("Rit klaar: %.1f km, max %.0f km/u, %.2f G", tripKm, tripMax, tripG);
        time_t ep = time(nullptr);   // v69: rit onthouden (alleen met een echte klok)
        if (ep > 1700000000) { PlusRit r; r.t1 = (uint32_t)ep - 180; r.t0 = r.t1 - (uint32_t)((nu - 180000 - tripStart) / 1000); r.km10 = (uint16_t)lroundf(tripKm * 10); r.vmax = (uint16_t)lroundf(tripMax); r.b100 = (uint16_t)lroundf(tripBest * 10); r.g100 = (uint8_t)fminf(255, tripG * 100); r.vrij = 0;
          if (plusRitN >= 40) { memmove(plusRit, plusRit + 1, sizeof(PlusRit) * 39); plusRitN = 39; }
          plusRit[plusRitN++] = r; plusRitBewaar(); ritStuurNu = true; }
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
static float segBreed(char ch, float h) { return ch == ':' ? h * .22f : ch == '1' ? h * .24f : h * .56f; }   // v68: een 1 is smal, dan staat 100 netjes in het midden
static void segCijfer(char ch, float x, float y, float h, uint16_t c) {   // x,y = linksboven
  float w = h * .56f, th = h * .15f, m = th * .5f;
  if (ch == ':') { circle(x + h * .11f, y + h * .3f, th * .55f, c); circle(x + h * .11f, y + h * .72f, th * .55f, c); return; }
  uint8_t s = ch == '-' ? 0x40 : (ch >= '0' && ch <= '9' ? SEG[ch - '0'] : 0x40);
  float L = x + m, R = x + w - m, T = y + m, M = y + h / 2, B = y + h - m;
  if (ch == '1') { float x1 = x + h * .12f; dikLijn(x1, T, x1, M, th, c); dikLijn(x1, M, x1, B, th, c); return; }
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
  qmiW(0x0B, 40);                   // CAL1_L: drempel 40 mg (v32: gevoeliger, deur dicht maakt hem nu wakker)
  qmiW(0x0C, (0x03 << 6) | 0x20);   // CAL1_H: INT2, beginwaarde 1 (hoog), even negeren na start
  qmiW(0x0A, 0x08);                 // CTRL9: WoM-instelling schrijven
  for (int i = 0; i < 100; i++) { Wire.beginTransmission(0x6B); Wire.write(0x2D); Wire.endTransmission(false); if (Wire.requestFrom((int)0x6B, 1) == 1 && (Wire.read() & 0x80)) break; delay(2); }
  qmiW(0x0A, 0x00);                 // bevestigen
  delay(5);
  qmiW(0x08, 0x01);                 // versnellingsmeter aan
  qmiW(0x02, 0x40 | 0x10);          // INT2 aan
  i2cUnlock();
}
static void persBewaar();
static void diepSlapen(const char *reden) {
  persBewaar();
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
      && nu - laatsteActie > 45000 && !bleConnected && !menuOpen && otaPct < 0) {
    static uint32_t afscheidT = 0;   // v42: eerst even zwaaien
    if (!afscheidT) { afscheidT = nu; static char tt[44]; char nm[20]; naamDeel(nm, sizeof nm); int hh = -1, mm = 0; if (klokOk()) lokaleTijd(hh, mm);
      const char *w = hh < 0 ? "Tot later%s!" : (hh >= 22 || hh < 5) ? "Slaap lekker%s!" : (hh < 10) ? "Fijne dag%s!" : (hh >= 17) ? "Fijne avond%s!" : "Tot straks%s!";
      snprintf(tt, sizeof tt, w, nm); startGag(22); gagMsg = tt; playGeluid((hh >= 22 || hh < 5) ? GL_SLAAP : GL_HOI); }
    else if (nu - afscheidT > 3200) { afscheidT = 0; diepSlapen("deur dicht en daarna stil"); }
  }
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
static volatile bool trajActief = false; static volatile float trajGem = 0; static volatile int trajLim = 0; static volatile float trajFrac = 0;   // v68: hoe ver in het traject (voor de ring)

static void jWs(JP &j) { while (j.p < j.e && (*j.p == ' ' || *j.p == '\n' || *j.p == '\r' || *j.p == '\t')) j.p++; }
static bool jIs(JP &j, char c) { jWs(j); if (j.p < j.e && *j.p == c) { j.p++; return true; } return false; }
static double jNum(JP &j) { jWs(j); char *q; double v = strtod(j.p, &q); if (q == j.p) { if (!strncmp(j.p, "null", 4)) j.p += 4; return 0; } j.p = q; return v; }
static void jSkipStr(JP &j) { jWs(j); if (j.p < j.e && *j.p == '"') { j.p++; while (j.p < j.e && *j.p != '"') { if (*j.p == '\\') j.p++; j.p++; } j.p++; } }
static bool jZoek(JP &j, const char *sleutel) { const char *q = strstr(j.p, sleutel); if (!q) return false; j.p = q + strlen(sleutel); return true; }

static uint8_t *haalUrl(const char *url, size_t &len) {
  len = 0; WiFiClientSecure cl; cl.setInsecure(); HTTPClient h; h.setTimeout(15000);
  if (!h.begin(cl, url)) return nullptr;
  int code = h.GET(); if (code != 200) { h.end(); netFoutCode = code; netFoutT = millis(); logRegel("net: %s -> %d", url + 34, code); return nullptr; }
  netOkT = millis();   // v67
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
// v68: borden leren van de telefoon. Elk bord dat de app doorgeeft wordt met plek en rijrichting bewaard,
// zodat Buddy+ het de volgende keer zelf weet (zonder telefoon en zonder internet).
struct Leer { int32_t la, lo; uint8_t hd, lim; uint16_t vrij; };
static Leer *leer = nullptr; static volatile int leerN = 0; static const int LEER_MAX = 40000;
static Leer leerBuf[24]; static volatile int leerBufN = 0;
static void leerLaad() {
  if (!leer) leer = (Leer *)heap_caps_malloc(sizeof(Leer) * LEER_MAX, MALLOC_CAP_SPIRAM); if (!leer) return;
  size_t n; uint8_t *b = leesBestand("/leer.bin", n); if (!b) return;
  int k = n / sizeof(Leer); if (k > LEER_MAX) { memcpy(leer, b + (k - LEER_MAX) * sizeof(Leer), LEER_MAX * sizeof(Leer)); k = LEER_MAX; } else memcpy(leer, b, k * sizeof(Leer));
  free(b); leerN = k; logRegel("borden geleerd van de telefoon: %d punten", k);
}
static void leerBewaar() {   // in de net-taak: buffer naar het bestand
  int n = leerBufN; if (!n || !logFs) return; Leer tmp[24]; memcpy(tmp, leerBuf, n * sizeof(Leer)); leerBufN = 0;
  File fl = logFs->open("/leer.bin", FILE_APPEND); if (fl) { fl.write((const uint8_t *)tmp, n * sizeof(Leer)); fl.close(); }
  if (leerN >= LEER_MAX - 50) { File w = logFs->open("/leer.bin", FILE_WRITE); if (w) { int van = leerN - 30000; w.write((const uint8_t *)(leer + van), 30000 * sizeof(Leer)); w.close(); memmove(leer, leer + van, 30000 * sizeof(Leer)); leerN = 30000; } }
}
static void leerOnthoud(float lat, float lon, float hd, int lim) {   // vanuit de loop, als de telefoon een bord geeft
  static float vLat = 0, vLon = 0; static int vLim = 0;
  if (!leer || lim <= 0 || lim > 130 || hd < 0) return;
  float dx = (lon - vLon) * 111320.f * cosf(lat * 0.0174533f), dy = (lat - vLat) * 110540.f;
  if (dx * dx + dy * dy < 40 * 40 && lim == vLim) return;   // elke 40 m (of als het bord verandert)
  vLat = lat; vLon = lon; vLim = lim;
  Leer e; e.la = (int32_t)lroundf(lat * 1e5f); e.lo = (int32_t)lroundf(lon * 1e5f); e.hd = (uint8_t)((int)lroundf(hd / 360.f * 256) & 255); e.lim = lim; e.vrij = 0;
  if (leerN < LEER_MAX) { leer[leerN] = e; leerN++; }
  if (leerBufN < 24) { leerBuf[leerBufN] = e; leerBufN++; }
}
static int leerZoek(float lat, float lon, float hd) {   // nieuwste geleerde bord binnen 35 m in dezelfde richting
  if (!leer || !leerN) return 0;
  int32_t la = (int32_t)lroundf(lat * 1e5f), lo = (int32_t)lroundf(lon * 1e5f); float kx = cosf(lat * 0.0174533f);
  int hb = (int)lroundf(hd / 360.f * 256) & 255;
  for (int i = leerN - 1; i >= 0; i--) { const Leer &e = leer[i];
    int dla = e.la - la, dlo = e.lo - lo; if (dla > 32 || dla < -32 || dlo > 52 || dlo < -52) continue;
    float d = sqrtf((float)dla * dla * 1.2218f + (float)dlo * dlo * kx * kx * 1.2392f); if (d > 35) continue;
    int dh = abs(((int)e.hd - hb + 384) % 256 - 128); if (dh > 32) continue;   // binnen ongeveer 45 graden
    return e.lim; }
  return 0;
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
static volatile uint32_t leerLimT = 0; static volatile int leerLim = 0;
static void bepaalWegGeleerd() {   // v68: geleerde borden (van de telefoon) als de kaart niets weet
  if (!gpsFix || gpsSpeedKmh < 5) return; int l = leerZoek(gpsLat, gpsLon, gpsKoers); if (l > 0) { leerLim = l; leerLimT = millis(); }
}
static void bepaalWeg() {
  bepaalWegGeleerd();
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
  const Weg &g = wegen[bi]; int lim = g.lim;   // (geen kaart: zie bepaalWegGeleerd)
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
  afstand += v / 3.6f * dt; tijd += dt; if (tijd > 2) trajGem = afstand / tijd * 3.6f; trajFrac = rechtL > 0 ? fminf(1.f, afstand / rechtL) : 0;
  bool eind = afstandM(lat, lon, trajecten[ti].la2, trajecten[ti].lo2) < 150 || afstand > rechtL * 1.6f + 2000 || tijd > 3600;
  if (eind) { logRegel("traject klaar: gemiddeld %.0f bij %d", trajGem, trajLim); ti = -1; trajActief = false; }
}
// v64: het logboek ook via wifi naar de Worker sturen (dan komt het op GitHub, ook als Bluetooth het niet doet)
static void logNaarWorker() {
  static size_t vanaf = (size_t)-1;
  if (!logFs || !wCfg.url[0]) return;
  String tekst;
  if (logMutex && xSemaphoreTake(logMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
    File fl = logFs->open(LOGF, FILE_READ);
    if (fl) { size_t sz = fl.size(); if (vanaf == (size_t)-1 || vanaf > sz) vanaf = sz > 8000 ? sz - 8000 : 0;
      if (sz > vanaf) { size_t n = min((size_t)8000, sz - vanaf); fl.seek(vanaf); tekst.reserve(n + 64); uint8_t b[256]; size_t gelezen = 0;
        while (gelezen < n) { int r = fl.read(b, min(sizeof b, n - gelezen)); if (r <= 0) break; for (int i = 0; i < r; i++) tekst += (char)b[i]; gelezen += r; }
        vanaf += gelezen; }
      fl.close(); }
    xSemaphoreGive(logMutex);
  }
  if (!tekst.length()) return;
  String kop = "--- Buddy+ v" + String(FW_VERSIE) + " ---\n";
  char url[220]; snprintf(url, sizeof url, "%s/log?id=plus%s&n=%s", wCfg.url, wCfg.van, wCfg.naam[0] ? wCfg.naam : "BuddyPlus");
  for (char *q = url; *q; q++) if (*q == ' ') *q = '+';
  WiFiClientSecure cl; cl.setInsecure(); HTTPClient h; h.setTimeout(15000);
  if (!h.begin(cl, url)) return;
  h.addHeader("Content-Type", "text/plain");
  int code = h.POST(kop + tekst); h.end();
  if (code != 200) { vanaf -= tekst.length(); Serial.printf("logboek naar worker: %d\n", code); }
}
static void checkOta() {   // v68: alleen binnenhalen (op de achtergrond, ook tijdens het rijden); installeren doe je zelf
  size_t n; uint8_t *b = haalUrl(FW_BASIS "versie.txt", n); if (!b) return;
  int v = atoi((char *)b); free(b);
  if (v <= FW_VERSIE) { logRegel("update: dit is de nieuwste (v%d)", FW_VERSIE); return; }
  if (v == otaKlaarV) { logRegel("update: v%d staat al klaar om te installeren", v); return; }
  const esp_partition_t *doel = esp_ota_get_next_update_partition(NULL); if (!doel) { logRegel("update: geen plek om te bewaren"); return; }
  logRegel("update: v%d -> v%d wordt op de achtergrond binnengehaald", FW_VERSIE, v);
  WiFiClientSecure cl; cl.setInsecure(); HTTPClient h; h.setTimeout(20000);
  if (!h.begin(cl, FW_BASIS "firmware.bin")) return;
  int code = h.GET(); if (code != 200) { logRegel("update: download -> %d", code); h.end(); return; }
  int tot = h.getSize(); esp_ota_handle_t oh;
  if (esp_ota_begin(doel, tot > 0 ? (size_t)tot : OTA_SIZE_UNKNOWN, &oh) != ESP_OK) { logRegel("update: kan niet beginnen"); h.end(); return; }
  otaPct = 0; WiFiClient *st = h.getStreamPtr(); uint8_t *buf = (uint8_t *)malloc(4096); size_t got = 0; uint32_t t0 = millis(), laatsteData = millis(); bool fout = !buf;
  while (!fout && (tot < 0 || got < (size_t)tot) && millis() - t0 < 600000UL && millis() - laatsteData < 30000) {
    size_t a = st->available();
    if (a) { int r = st->readBytes(buf, a > 4096 ? 4096 : a); if (r > 0) { if (esp_ota_write(oh, buf, r) != ESP_OK) fout = true; got += r; laatsteData = millis(); if (tot > 0) otaPct = got * 100 / tot; } }
    else { if (!h.connected() && tot < 0) break; vTaskDelay(pdMS_TO_TICKS(5)); }
  }
  if (buf) free(buf); h.end(); otaPct = -1;
  if (fout || (tot > 0 && got != (size_t)tot)) { esp_ota_abort(oh); logRegel("update: download onderbroken (%u van %d bytes), later opnieuw", (unsigned)got, tot); return; }
  if (esp_ota_end(oh) != ESP_OK) { logRegel("update: binnengehaald bestand klopt niet, later opnieuw"); return; }
  buddyPrefs.putInt("otaV", v); buddyPrefs.putString("otaP", doel->label);
  otaKlaarV = v; otaKlaarT = millis(); statVraag = true;
  logRegel("update: v%d staat klaar. Installeren via het info-venster of de app", v);
}
static void installeerOta() {   // v68: de klaarstaande update aanzetten en herstarten
  if (!otaKlaarV) return; const esp_partition_t *p = esp_ota_get_next_update_partition(NULL);
  if (!p || buddyPrefs.getString("otaP", "") != String(p->label)) { logRegel("update: klaarstaande versie niet meer gevonden"); otaKlaarV = 0; return; }
  if (esp_ota_set_boot_partition(p) != ESP_OK) { logRegel("update: installeren mislukt"); return; }
  logRegel("update: v%d wordt geinstalleerd, herstarten...", (int)otaKlaarV); delay(400); ESP.restart();
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
// (weer-variabelen staan bovenaan, v48)
static void haalWeer() {
  char url[220]; snprintf(url, sizeof url, "https://api.open-meteo.com/v1/forecast?latitude=%.3f&longitude=%.3f&current=temperature_2m,weather_code,precipitation&minutely_15=precipitation&forecast_minutely_15=4&timezone=auto", (double)gpsLat, (double)gpsLon);
  size_t n; uint8_t *b = haalUrl(url, n); if (!b) return;
  JP j{(const char *)b, (const char *)b + n};
  if (jZoek(j, "\"current\":{")) {
    JP c = j; if (jZoek(c, "\"temperature_2m\":")) wxTemp = jNum(c);
    c = j; if (jZoek(c, "\"weather_code\":")) wxCode = (int)jNum(c);
    c = j; if (jZoek(c, "\"precipitation\":")) wxPrecip = jNum(c);   // v56: hoeveel valt er nu
  }
  JP m{(const char *)b, (const char *)b + n}; int over = -1;
  if (jZoek(m, "\"minutely_15\":{") && jZoek(m, "\"precipitation\":[")) { for (int i = 0; i < 4; i++) { float v = jNum(m); jIs(m, ','); if (v >= 0.2f && over < 0) over = i * 15; } }
  wxRegenOver = over; wxT = millis();
  free(b);
  if (wCfg.url[0]) {   // v56: de Buienradar (echte radar, per 5 minuten) weet beter of en wanneer het regent
    char ru[200]; snprintf(ru, sizeof ru, "%s/regen?lat=%.2f&lon=%.2f", wCfg.url, (double)gpsLat, (double)gpsLon);
    size_t rn; uint8_t *rb = haalUrl(ru, rn);
    if (rb) { float nu = -1; int rov = -1, regel = 0; const char *q = (const char *)rb, *e = q + rn;
      while (q < e) { int v = 0; bool cijfer = false; while (q < e && *q >= '0' && *q <= '9') { v = v * 10 + (*q - '0'); q++; cijfer = true; }
        if (cijfer && q < e && *q == '|') { float mm = v > 0 ? powf(10.f, (v - 109) / 32.f) : 0; if (regel < 2) nu = fmaxf(nu, mm); if (mm >= .3f && rov < 0) rov = regel * 5; regel++; }
        while (q < e && *q != '\n') q++; if (q < e) q++; }
      free(rb);
      if (regel >= 6) { wxRadarNu = nu; wxRadarT = millis(); wxRegenOver = nu >= .1f ? 0 : rov; } }
  }
  logRegel("weer: %.1f graden, code %d, regen over %d min%s", (double)wxTemp, (int)wxCode, (int)wxRegenOver, wxRadarT && millis() - wxRadarT < 60000 ? " (buienradar)" : "");
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
  leerLaad();   // v68: geleerde borden
  for (;;) {
    float dt = (millis() - stapT) / 1000.f; stapT = millis();
    // 1. tegel voor de huidige plek (uit het geheugen of van internet)
    bool online = WiFi.status() == WL_CONNECTED && (uint32_t)WiFi.localIP() != 0;   // v32: pas online met een IP-adres
    if (gpsFix) { char key[16]; tegelKey(gpsLat, gpsLon, key, sizeof key); if (strcmp(key, tegelNu)) laadTegel(key, online); }
    bepaalWeg(); trajectStap(dt); fileVoorStap();
    { static uint32_t lbT = 0; if (millis() - lbT > 20000 || leerBufN >= 20) { lbT = millis(); leerBewaar(); } }   // v68
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
    { static uint32_t vlT = 0; static int vi = 0; if (gpsFix && logFs && millis() - vlT > 4000) { vlT = millis();   // v68: kaartstukjes rondom vooruit laden (buffer voor als het internet wegvalt)
        static const int8_t DX[8] = {-1, 0, 1, -1, 1, -1, 0, 1}, DY[8] = {-1, -1, -1, 0, 0, 1, 1, 1}; vi = (vi + 1) % 8;
        char key[16]; tegelKey(gpsLat + DY[vi] * 0.03, gpsLon + DX[vi] * 0.05, key, sizeof key); char pad[32]; snprintf(pad, sizeof pad, "/t_%s.json", key);
        if (!logFs->exists(pad)) { char url[128]; snprintf(url, sizeof url, DATA_BASIS "t/%s.json", key); size_t n; uint8_t *b = haalUrl(url, n); if (b && n > 10) bewaar(pad, b, n); free(b); } } }
    if (otaNu) { otaNu = false; otaT = millis(); logRegel("update gevraagd vanuit de app"); checkOta(); }
    { static uint32_t chkT = 0; if (!chkT || millis() - chkT > 120000UL) { chkT = millis();   // v67: internet-check
        IPAddress ip; netDnsOk = WiFi.hostByName("raw.githubusercontent.com", ip) == 1 ? 1 : 0; netTcpOk = -1;
        if (netDnsOk) { WiFiClient c; netTcpOk = c.connect(ip, 443, 5000) ? 1 : 0; c.stop(); }
        logRegel("internet-check: dns %s, verbinding %s, laatste download %s", netDnsOk ? "ok" : "MISLUKT", netTcpOk == 1 ? "ok" : netTcpOk == 0 ? "MISLUKT" : "-", netOkT && (!netFoutT || netOkT > netFoutT) ? "gelukt" : netFoutT ? "mislukt" : "nog geen"); } }
    { static uint32_t logT = 0; if ((!logT && millis() > 60000) || (logT && millis() - logT > 300000)) { logT = millis(); logNaarWorker(); } }   // v64
    bool stil = !gpsFix || gpsSpeedKmh < 1;
    (void)stil; if ((!otaT || millis() - otaT > 1UL * 3600000) && millis() > 60000) { otaT = millis(); checkOta(); }   // v68: elk uur kijken, binnenhalen mag altijd
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// ===================== v42: persoonlijkheid =====================
static const int UNLOCK_KM[][2] = {{13, 30}, {29, 80}, {30, 150}, {32, 250}, {28, 400}, {31, 600}, {19, 900}, {26, 1300}};   // trucje, vanaf zoveel km samen
static bool gagOntgrendeld(int k) { for (auto &u : UNLOCK_KM) if (u[0] == k) return totKm >= u[1]; return true; }
static int trucjesOpen() { int n = 0; for (auto &u : UNLOCK_KM) if (totKm >= u[1]) n++; return n; }
static uint32_t nuEpoch() { time_t e = time(nullptr); return e > 1700000000 ? (uint32_t)e : 0; }
static void persLaad() {
  String nm = buddyPrefs.getString("naam", ""); strncpy(persNaam, nm.c_str(), sizeof persNaam - 1);
  vriend = buddyPrefs.getUChar("vriend", 40);
  sprintI = buddyPrefs.getUChar("spri", 0);   // v56
  plusRitLaad();   // v69
  { int ov = buddyPrefs.getInt("otaV", 0);   // v68: staat er nog een binnengehaalde update klaar?
    if (ov > FW_VERSIE) { const esp_partition_t *p = esp_ota_get_next_update_partition(NULL); if (p && buddyPrefs.getString("otaP", "") == String(p->label)) otaKlaarV = ov; }
    else if (ov) buddyPrefs.remove("otaV"); }
  nachtStil = buddyPrefs.getBool("nstil", true);   // v59
  if (buddyPrefs.isKey("likes")) buddyPrefs.getBytes("likes", gagLike, sizeof gagLike);
  if (buddyPrefs.isKey("vertrek")) buddyPrefs.getBytes("vertrek", vertrekH, sizeof vertrekH);
  vorigeRitT = lastRitT = buddyPrefs.getUInt("lastT", 0); eerstT = buddyPrefs.getUInt("eerst", 0);
  if (!buddyPrefs.isKey("mijl")) { static const int M[] = {100, 250, 500, 1000, 2500, 5000, 10000, 25000}; uint32_t m = 0; for (int x : M) if (totKm >= x) m = x; mijlKm = m; buddyPrefs.putUInt("mijl", m); }
  else mijlKm = buddyPrefs.getUInt("mijl", 0);
  if (!buddyPrefs.isKey("truc")) { trucjesGezien = trucjesOpen(); buddyPrefs.putUChar("truc", trucjesGezien); } else trucjesGezien = buddyPrefs.getUChar("truc", 0);
}
static void persBewaar() {
  buddyPrefs.putUChar("vriend", vriend); buddyPrefs.putUChar("energie", energie); buddyPrefs.putUInt("lastT", lastRitT);
  if (likeVuil) { likeVuil = false; buddyPrefs.putBytes("likes", gagLike, sizeof gagLike); }
}
static void vriendPlus(int d) { vriend = (uint8_t)constrain((int)vriend + d, 0, 100); }
static const char *naamDeel(char *b, size_t n) { if (persNaam[0]) snprintf(b, n, ", %s", persNaam); else b[0] = 0; return b; }
static void doeGroet() {
  gegroet = true; char nm[20]; naamDeel(nm, sizeof nm);
  int hh = -1, mm = 0; if (klokOk()) lokaleTijd(hh, mm);
  uint32_t ep = nuEpoch(); int wd = -1, md = 0, mo = 0; if (ep) { struct tm tv; time_t e = ep; localtime_r(&e, &tv); wd = tv.tm_wday; md = tv.tm_mday; mo = tv.tm_mon + 1; }
  float weg = (ep && vorigeRitT && ep > vorigeRitT) ? (ep - vorigeRitT) / 86400.f : 0;
  if (ep && !eerstT) { eerstT = ep; buddyPrefs.putUInt("eerst", eerstT); }
  int gewoon = -1, best = 2; for (int i = 0; i < 24; i++) if (vertrekH[i] > best) { best = vertrekH[i]; gewoon = i; }
  if (hh >= 0) { if (vertrekH[hh] < 250) vertrekH[hh]++; buddyPrefs.putBytes("vertrek", vertrekH, sizeof vertrekH); }
  int gag = 22;
  if (ep && eerstT && md && ep - eerstT > 360UL * 86400) { struct tm t0; time_t e0 = eerstT; localtime_r(&e0, &t0);
    if (t0.tm_mday == md && t0.tm_mon + 1 == mo) { snprintf(groetTxt, sizeof groetTxt, "%d jaar samen%s! Feest!", (int)((ep - eerstT) / 31536000UL), nm); gag = 38; } }
  if (gag == 22) {
    if (weg >= 3) { vriendPlus(-min(20, (int)(weg * 3))); snprintf(groetTxt, sizeof groetTxt, "Waar wás je nou%s?! %d dagen!", nm, (int)weg); gag = vriend < 25 ? 44 : 22; }
    else if (vriend < 20) { snprintf(groetTxt, sizeof groetTxt, "Hmpf%s... aai je me nog eens?", nm); gag = 44; }
    else if (hh >= 0 && hh < 7) { snprintf(groetTxt, sizeof groetTxt, "Pfoe, vroeg%s... koffie?", nm); gag = 45; }
    else if (hh >= 0 && gewoon >= 0 && hh < gewoon - 1) snprintf(groetTxt, sizeof groetTxt, "Vroeg vandaag%s!", nm);
    else if (hh >= 0 && gewoon >= 0 && hh > gewoon + 2 && hh < 12) snprintf(groetTxt, sizeof groetTxt, "Later dan normaal%s. Uitgeslapen?", nm);
    else if (wd == 5 && hh >= 12) snprintf(groetTxt, sizeof groetTxt, "Vrijdag%s! Bijna weekend!", nm);
    else if (wd == 1 && hh >= 0 && hh < 12) snprintf(groetTxt, sizeof groetTxt, "Maandag%s... we kunnen het!", nm);
    else if (wd == 0 || wd == 6) snprintf(groetTxt, sizeof groetTxt, "Weekend%s! Waar gaan we heen?", nm);
    else if (hh >= 0 && hh < 12) snprintf(groetTxt, sizeof groetTxt, "Goeiemorgen%s!", nm);
    else if (hh >= 0 && hh < 18) snprintf(groetTxt, sizeof groetTxt, "Goedemiddag%s!", nm);
    else if (hh >= 0 && hh < 23) snprintf(groetTxt, sizeof groetTxt, "Goedenavond%s!", nm);
    else if (hh >= 0) snprintf(groetTxt, sizeof groetTxt, "Laat hoor%s. Rustig aan.", nm);
    else snprintf(groetTxt, sizeof groetTxt, "Hoi%s! Daar ben je!", nm);
  }
  startGag(gag); gagMsg = groetTxt; if (gag != 44) playGeluid(gag == 38 ? GL_TADA : GL_HOI); else playGeluid(GL_OEPS);
  logRegel("groet: %s (vriend %d)", groetTxt, vriend);
}
static void stuurStat() {
  char b[24]; int sats = (int)gpsSatellites, zicht = (int)gpsInZicht;
  snprintf(b, sizeof b, "S:v=%d", FW_VERSIE); bleSend(b); delay(15);
  snprintf(b, sizeof b, "S:f=%.0f", laatsteFps); bleSend(b); delay(15);
  snprintf(b, sizeof b, "S:s=%d/%d", sats, zicht); bleSend(b); delay(15);
  snprintf(b, sizeof b, "S:w=%d", WiFi.status() == WL_CONNECTED ? 1 : 0); bleSend(b); delay(15);
  snprintf(b, sizeof b, "S:t=%.0f", temperatureRead()); bleSend(b); delay(15);
  snprintf(b, sizeof b, "S:fr=%d", (int)vriend); bleSend(b); delay(15);
  if (otaKlaarV) { snprintf(b, sizeof b, "S:upd=%d", (int)otaKlaarV); bleSend(b); delay(15); }   // v68
  snprintf(b, sizeof b, "S:au=%s/%d/%d", audioReady ? (audioBusy ? "bezig" : "ok") : audioStatus, (int)buddyVolume, (int)audioGespeeld); bleSend(b);   // v58: geluid
}
static void persStap(float dt) {
  uint32_t nu = millis(); float v = spdNow();
  if (persNieuw) { persNieuw = false;
    if (strcmp(persNaam, persNaamNieuw)) { strcpy(persNaam, persNaamNieuw); buddyPrefs.putString("naam", persNaam); }
    karFactor = persKar == 'W' ? .75f : (persKar == 'R' ? 1.3f : 1.f);
    if (!ntpOk() && persEpoch > 1700000000) { struct timeval tv = {(time_t)persEpoch, 0}; settimeofday(&tv, nullptr); logRegel("klok gezet via de telefoon"); }
    static bool stijlGezegd = false;
    if (!stijlGezegd && gegroet && persStijl >= 0 && gagAct < 0) { stijlGezegd = true;
      if (persStijl < 70) { startGag(27); gagMsg = "Vorige rit was pittig. Rustig aan vandaag?"; }
      else if (persStijl >= 92) { startGag(17); gagMsg = "Vorige rit: zen-modus. Top!"; } }
  }
  if (!gegroet && nu > groetVanaf && gagAct < 0 && !menuOpen && !venster && (klokOk() || nu > groetVanaf + 12000)) doeGroet();
  if (otaInstalleer) { otaInstalleer = false; installeerOta(); }   // v68
  if (evtSoort && gagAct < 0 && (int32_t)(showTot - millis()) <= 0) { char s = evtSoort; evtSoort = 0; laatsteActie = millis(); slaapt = false;   // v69: groepsgebeurtenis
    int g = s == 'f' ? 38 : s == 'm' ? 44 : s == 'z' ? 22 : 7; startGag(g); gagMsg = evtTxt;
    playGeluid(s == 'f' ? GL_TADA : s == 'm' ? GL_OEPS : s == 'z' ? GL_HOI : s == 'h' ? GL_GIECHEL : GL_PLING); logRegel("gebeurtenis: %s", evtTxt); }
  if (pinAntw && bleConnected && !logBezig) { uint8_t a = pinAntw; pinAntw = 0; bleSend(a == 1 ? "PIN:OK" : a == 2 ? "PIN:FOUT" : "PIN?"); if (a == 1) { ritStuurNu = true; ritStuurT = millis() + 6000; } }   // v65
  if (ritStuurNu && bleConnected && pinOk && !logBezig && (int32_t)(millis() - ritStuurT) >= 0) { ritStuurNu = false;   // v69: ritten naar de app
    time_t ep = time(nullptr);
    for (int i = 0; i < plusRitN; i++) { const PlusRit &r = plusRit[i]; if (ep > 1700000000 && (uint32_t)ep - r.t1 > 14UL * 86400) continue;
      char m[64]; snprintf(m, sizeof m, "RIT:%lu|%lu|%u|%u|%u|%u", (unsigned long)r.t0, (unsigned long)r.t1, r.km10, r.vmax, r.b100, r.g100); bleSend(m); delay(25); } }
  if (statVraag && !logBezig) { statVraag = false; stuurStat(); }
  // v44: zoekt de eigen GPS nog? Dan de plek van de telefoon gebruiken (bordjes, flitsers, files, klok blijven werken)
  { static uint32_t telPosMs = 0;
    if (phone.pos && phone.ms != telPosMs && millis() - gpsEigenMs > 4000 && millis() - phone.ms < 2500) { telPosMs = phone.ms;
      gpsLat = phone.lat; gpsLon = phone.lon; gpsSpeedKmh = (float)phone.v; if (phone.hd >= 0) gpsKoers = phone.hd;
      gpsLastFixMs = millis(); gpsFix = true; posVanTel = true; gpsNewFix = true;
      static uint32_t hulpT = 0;   // v64: GPS-chip zoekt nog? Geef hem de plek en tijd van de telefoon als startpunt (hooguit eens per 10 min)
      if (klokOk() && (!hulpT || millis() - hulpT > 600000UL) && (!gpsEigenMs || millis() - gpsEigenMs > 60000)) { hulpT = millis(); gpsHulpLat = phone.lat; gpsHulpLon = phone.lon; gpsHulpNu = true; } } }
  if (zoekNu) { zoekNu = false; zoekTot = nu + 6000; laatsteActie = nu; slaapt = false; }
  { static uint32_t zb = 0; if ((int32_t)(zoekTot - nu) > 0 && nu - zb > 700) { zb = nu; playMoodSound(ALERT); } }
  // samen rijden: vriendschap groeit, laatste-ritmoment bijhouden
  { static float rijS = 0; if (v > 10) { rijS += dt; if (rijS >= 600) { rijS = 0; vriendPlus(1); } uint32_t ep = nuEpoch(); if (ep) lastRitT = ep; } }
  // mijlpalen en nieuwe trucjes
  { static uint32_t mT = 0; if (nu - mT > 5000 && gagAct < 0 && !menuOpen && !venster) { mT = nu;
      static const int M[] = {100, 250, 500, 1000, 2500, 5000, 10000, 25000}; uint32_t m = mijlKm; for (int x : M) if (totKm >= x) m = x;
      if (m > mijlKm) { mijlKm = m; buddyPrefs.putUInt("mijl", m); snprintf(mijlTxt, sizeof mijlTxt, "%u km samen! Feestje!", (unsigned)m); startGag(38); gagMsg = mijlTxt; playGeluid(GL_TADA); vriendPlus(5); }
      else if (trucjesOpen() > trucjesGezien) { int k = UNLOCK_KM[trucjesGezien][0]; trucjesGezien++; buddyPrefs.putUChar("truc", trucjesGezien); startGag(k); gagMsg = "Nieuw trucje geleerd! Kijk!"; playMoodSound(EXCITED); } } }
  // microfoon: muziek en praten
  { static uint32_t mu0 = 0, pr0 = 0; uint32_t sindsS = nu - oorSoortT;
    if (oorSoort >= 2 && sindsS > 8000 && gagAct < 0 && nu - mu0 > 90000 && !slaapt) { mu0 = nu; startGag(oorSoort == 3 ? 41 : 42); }
    if (oorSoort == 1 && sindsS > 5000 && gagAct < 0 && nu - pr0 > 120000 && !slaapt) { pr0 = nu; startGag(43); }
    if (oorSoort == 1 && nextGagT < t + 8) nextGagT = t + 8;   // tijdens een gesprek geen gekke dingen
  }
  { static uint32_t bT = 0; if (nu - bT > 300000) { bT = nu; persBewaar(); } }
  { static uint32_t g0 = 0; uint32_t gt = grpTot; if (gt && gt != g0) { g0 = gt; playGeluid(GL_DINGDONG); } }   // v46: groepsbericht
  // v45: jarig, pauze na 2 uur, weer, lievelingstrucje
  { static bool jarigGedaan = false; static char jt[40];
    if (persJarig && !jarigGedaan && gegroet && gagAct < 0 && !menuOpen && !venster) { jarigGedaan = true; char nm[20]; naamDeel(nm, sizeof nm);
      snprintf(jt, sizeof jt, "Gefeliciteerd%s! Hoera!", nm); startGag(40); gagMsg = jt; playMoodSound(LOVE); vriendPlus(5); } }
  { static float rijAan = 0, stilS = 0; static uint32_t pauzeT = 0;
    if (v > 10) { rijAan += dt; stilS = 0; } else { stilS += dt; if (stilS > 600) rijAan = 0; }
    if (rijAan > 7200 && (!pauzeT || nu - pauzeT > 1800000) && gagAct < 0 && v < 60) { pauzeT = nu; startGag(45); gagMsg = "Al 2 uur onderweg. Pauze? Koffie!"; } }
  { static bool wxGezegd = false; static char wt[44];
    if (!wxGezegd && gegroet && wxVers() && gagAct < 0 && !menuOpen && !venster && nu > groetVanaf + 20000) { wxGezegd = true;
      if (wxRegenOver >= 0 && wxRegenOver <= 30) { snprintf(wt, sizeof wt, "Over %d min regen. Wissers klaar!", (int)wxRegenOver); startGag(15); gagMsg = wt; }
      else if (wxTemp < 3) { snprintf(wt, sizeof wt, "Brrr, %d graden! Verwarming aan?", (int)lroundf(wxTemp)); startGag(21); gagMsg = wt; }
      else if (wxTemp > 25) { snprintf(wt, sizeof wt, "Pfoe, %d graden! Airco aan!", (int)lroundf(wxTemp)); startGag(17); gagMsg = wt; } } }
}

// ===================== v48: robotje op tafel =====================
static void robotStap(float dt) {
  uint32_t nu = millis(); float v = spdNow();
  // geluid in de kamer (klappen, praten, muziek) houdt hem wakker zolang je stilstaat
  if (v < 2 && (oorSoort > 0 || (klapT && nu - klapT < 3000))) laatsteActie = nu;
  // referentie 'rechtop': eerst bij het opstarten, daarna wat hij tijdens het rijden ziet (zo staat hij in de auto)
  { static float rijS = 0; static uint32_t bootT = 0; if (!bootT) bootT = nu;
    if (!gRefOk && nu - bootT > 5000) { if (buddyPrefs.isKey("grefz")) { gRefX = buddyPrefs.getFloat("grefx", 0); gRefY = buddyPrefs.getFloat("grefy", 0); gRefZ = buddyPrefs.getFloat("grefz", 1); } else { gRefX = gX; gRefY = gY; gRefZ = gZ; } gRefOk = true; }
    if (v > 15) { rijS += dt; if (rijS > 60) { rijS = -1e9; gRefX = gX; gRefY = gY; gRefZ = gZ; buddyPrefs.putFloat("grefx", gRefX); buddyPrefs.putFloat("grefy", gRefY); buddyPrefs.putFloat("grefz", gRefZ); } } }
  float sch = laatsteSchok; laatsteSchok = 0;
  if (doeOpdracht) { int d = doeOpdracht; doeOpdracht = 0; laatsteActie = nu; slaapt = false;   // v52: knoppen in de app
    if (d == 1) { koekjeZichtbaar = false; energie = 100; startGag(51); playGeluid(GL_PLING); vriendPlus(1); }
    else if (d == 2) { if (focusTot) { focusTot = 0; startGag(7); gagMsg = "Focus gestopt. Even pauze?"; } else { focusTot = nu + 25UL * 60000; startGag(7); gagMsg = "Focus! 25 minuten, ik doe mee."; playGeluid(GL_PLING); } }
    else if (d == 3) { startGag(53); }
    else if (d == 4) { startGag(22); playGeluid(GL_HOI); }
    else if (d == 5) { startGag(44); gagMsg = "Hmpf! Ons record is verbroken..."; playGeluid(GL_OEPS); }   // v54: iemand verbrak je record
    else if (d == 6) { startGag(38); gagMsg = "WRAAK! Record terug!"; playGeluid(GL_TADA); }   // v54: record teruggepakt
    else if (d == 7) { showSoort = 1 + sprintI % 4; sprintI++; buddyPrefs.putUChar("spri", sprintI); showNa = millis(); }
    else if (d == 8) { playGeluid(GL_PLING); logRegel("geluid test: klaar=%d bezig=%d volume=%d status=%s", (int)audioReady, (int)audioBusy, (int)buddyVolume, audioStatus); statVraag = true; }
    else if (d == 9) { otaInstalleer = true; }
    else if ((d == 10 || d == 11) && (int32_t)(showTot - millis()) <= 0 && !showNa) { showSoort = d == 10 ? 5 : 6; showNa = millis(); } }   // v69: tunnel / rotonde   // v58: geluid testen / v68: update installeren   // v56: sprint-show testen vanuit de app
  if (!robotModus) { koekjeZichtbaar = false; return; }
  // schudden of oppakken
  { static uint32_t piekT[6] = {0}; static int pi = 0; static uint32_t duizT = 0;
    if (sch > .6f) { piekT[pi] = nu; pi = (pi + 1) % 6; int n = 0; for (auto x : piekT) if (x && nu - x < 1500) n++;
      if (n >= 4 && nu - duizT > 6000) { duizT = nu; startGag(46); playGeluid(GL_OEPS); } } }
  // scheef, plat of op zijn kop
  if (gRefOk) { static float scheefS = 0; static uint32_t scheefT = 0;
    float gl = sqrtf(gX * gX + gY * gY + gZ * gZ), rl = sqrtf(gRefX * gRefX + gRefY * gRefY + gRefZ * gRefZ);
    float hoek = (gl > .3f && rl > .3f) ? acosf(fmaxf(-1, fminf(1, (gX * gRefX + gY * gRefY + gZ * gRefZ) / (gl * rl)))) : 0;
    if (hoek > .45f) scheefS += dt; else scheefS = 0;
    if (scheefS > 1.5f && nu - scheefT > 20000 && gagAct < 0) { scheefT = nu; startGag(55);
      gagMsg = hoek > 2.4f ? "Ik sta op mijn kop!" : (hoek > .95f ? "Ik zie alleen plafond... zet me rechtop!" : "Hou me recht, ik glij weg!"); playGeluid(GL_OEPS); } }
  // energie en koekje
  { static float eS = 0; eS += dt; if (eS > 360) { eS = 0; if (energie > 0) energie--; }
    if (energie < 45 && !koekjeZichtbaar) { koekjeZichtbaar = true; if (gagAct < 0) { startGag(56); playGeluid(GL_OEPS); } }
    if (energie < 25 && nextGagT < t + 12) nextGagT = t + 12; }   // moe: rustiger
  // focusmaatje
  if ((int32_t)(focusTot - nu) > 0) { laatsteActie = nu; if (nextGagT < t + 5) nextGagT = t + 5; }
  else if (focusTot) { focusTot = 0; startGag(38); gagMsg = "Pauze! Goed gedaan!"; playGeluid(GL_TADA); vriendPlus(2); }
  // elk heel uur zwaaien
  if (klokOk()) { static int laatsteU = -1; int hh, mm; lokaleTijd(hh, mm);
    if (mm == 0 && hh != laatsteU && gagAct < 0 && !slaapt) { laatsteU = hh; startGag(22); snprintf(robotTxt, sizeof robotTxt, "Het is %d uur!", hh); gagMsg = robotTxt; playGeluid(GL_DINGDONG); }
    if (mm != 0 && laatsteU == hh) {} }
  // slaapmutsje 's avonds, uitrekken 's ochtends
  { static bool wasSlaapt = false; int hh = 12, mm = 0; if (klokOk()) lokaleTijd(hh, mm);
    if (slaapt && !wasSlaapt && (hh >= 21 || hh < 6)) { slaapMuts = true; playGeluid(GL_SLAAP); }
    if (!slaapt && wasSlaapt && slaapMuts) { slaapMuts = false; if (hh >= 6 && hh < 12) { startGag(54); playGeluid(GL_HOI); } }
    wasSlaapt = slaapt; }
}

// v40: retro snelheidsmeter (crème wijzerplaat, chromen rand, rode naald die doorschiet en natrilt)
static inline float meterHoek(float v) { return (225.f - fminf(fmaxf(v, 0.f), 200.f) / 200.f * 270.f) * (float)M_PI / 180.f; }
// v56: de sprint-shows
static uint16_t kl565(uint8_t r, uint8_t g, uint8_t b) { return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); }
static uint16_t hsv565(float h, float s, float v) { h = fmodf(h, 360.f) / 60.f; int i = (int)h; float f = h - i, p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f)); float r, g, b;
  switch (i) { case 0: r = v; g = t; b = p; break; case 1: r = q; g = v; b = p; break; case 2: r = p; g = v; b = t; break; case 3: r = p; g = q; b = v; break; case 4: r = t; g = p; b = v; break; default: r = v; g = p; b = q; }
  return kl565(r * 255, g * 255, b * 255); }
static void showRect(float x, float y, float w, float h, uint16_t c) { boxAdd(x, y, x + w, y + h); frame->fillRect(X(x), Y(y), S(w), S(h), c); }
static void showTekst(const char *t, float x, float y, int sz, uint16_t c) { frame->setTextColor(c); frame->setTextSize(sz); frame->setCursor(X(x) - (int)strlen(t) * 3 * sz, Y(y) - 4 * sz); frame->print(t); }
static void miniBuddy(float x, float y, float s, bool vlam, int ogen) {
  if (vlam) { float l = (14 + random(14)) * s; triF(x - 10 * s, y + 26 * s, x + 10 * s, y + 26 * s, x, y + 26 * s + l, 0xFD20); triF(x - 5 * s, y + 26 * s, x + 5 * s, y + 26 * s, x, y + 26 * s + l * .6f, 0xFFE0); }
  roundRect(x - 31 * s, y - 27 * s, 62 * s, 54 * s, 16 * s, EYE); roundRect(x - 28 * s, y - 24 * s, 56 * s, 48 * s, 13 * s, 0x0000);
  if (ogen == 1) { for (int e = -1; e <= 1; e += 2) for (int k = 0; k < 10; k++) { float an = k * .7f + millis() / 150.f, r = k * .5f * s; circle(x + e * 12 * s + cosf(an) * r, y - 4 * s + sinf(an) * r, 1.3f * s + .4f, EYE); } }
  else if (ogen == 2) { for (int e = -1; e <= 1; e += 2) for (int k = 0; k < 7; k++) { float an = 3.1416f + k * .52f; circle(x + e * 11 * s + cosf(an) * 6 * s, y - 2 * s + sinf(an) * 6 * s, 1.6f * s + .4f, EYE); } }
  else { circle(x - 12 * s, y - 4 * s, 6 * s, EYE); circle(x + 12 * s, y - 4 * s, 6 * s, EYE); }
  for (int k = 0; k < 7; k++) { float an = k * .52f; circle(x + cosf(an) * 7 * s, y + 9 * s + sinf(an) * 7 * s, 1.4f * s + .4f, EYE); }
}
static void drawShow() {
  boxAdd(0, 0, 240, 240); circle(120, 120, 119, 0x0000);
  float tt = (millis() - showStart) / 1000.f; uint32_t ms = millis();
  if (showSoort == 1) {   // de ruimte in
    if (tt < 1.3f) { showRect(0, 150, 240, 90, 0x2104); for (int i = 0; i < 6; i++) { float lx = 240 - fmodf(tt * 600 + i * 70, 300); dikLijn(lx, 70 + i * 14, lx + 40, 70 + i * 14, 2, 0x8410); }
      for (int i = 0; i < 9; i++) { float dx = fmodf(i * 30 - tt * 400, 270); if (dx < 0) dx += 270; showRect(dx - 15, 194, 16, 3, 0xFEA0); }
      miniBuddy(110 + sinf(tt * 60) * 2, 130, 1, false, 0); showTekst("VROEEEM!", 120, 60, 3, 0xFFE0); return; }
    float k = tt - 1.3f, sn = fminf(1, k / 1.2f) * (k < 3 ? 1 : .35f);
    for (int i = 0; i < 40; i++) { float an = i * 2.39996f, sp = .4f + (i % 7) * .15f, r = fmodf(i * 37 + k * 160 * sp, 140), r2 = r + 4 + 30 * sn * sp;
      dikLijn(120 + cosf(an) * r, 120 + sinf(an) * r, 120 + cosf(an) * r2, 120 + sinf(an) * r2, 1.2f, r > 50 ? 0xFFFF : 0x8410); }
    if (k > 1.6f && k < 5) { float e = fmaxf(4, 70 - (k - 1.6f) * 26), ey = 150 + (k - 1.6f) * 8; circle(120, ey, e + 3, 0x4A9F); circle(120, ey, e, 0x1C9F); circle(120 - e * .3f, ey - e * .2f, e * .35f, 0x35E6); circle(120 + e * .35f, ey + e * .25f, e * .25f, 0x35E6); }
    if (k > 3.2f && k < 6.3f) { float sa = fmaxf(3, 30 - (k - 3.2f) * 9), sx = 60 + (k - 3.2f) * 6; for (int j = 0; j < 24; j++) { float a0 = j * .2618f, a1 = a0 + .2618f; dikLijn(sx + cosf(a0) * sa * 1.9f, 70 + sinf(a0) * sa * .5f - cosf(a0) * sa * .5f, sx + cosf(a1) * sa * 1.9f, 70 + sinf(a1) * sa * .5f - cosf(a1) * sa * .5f, 2, 0xD58D); } circle(sx, 70, sa, 0xEE4F); }
    if (k > 4.7f) { float gs = fminf(1, (k - 4.7f) / 1.2f), rot = k * .8f; for (int arm = 0; arm < 2; arm++) for (int i = 0; i < 40; i++) { float t2 = i / 40.f, an = rot + arm * 3.1416f + t2 * 5, r = t2 * 70 * gs; circle(120 + cosf(an) * r, 120 + sinf(an) * r * .6f, 1.4f + (1 - t2) * 1.4f, hsv565(230 + i, .35f, 1 - t2 * .6f)); } circle(120, 120, 8 * gs, 0xFF79); }
    float bs = fmaxf(.25f, 1 - k * .14f); miniBuddy(120 + sinf(k * 1.7f) * 40 * (1 - bs), 120 - fminf(40, k * 30) + 30 * (1 - bs), bs, true, 0);
    const char *lab = k < 1.6f ? "" : k < 3.3f ? "NEDERLAND" : k < 4.7f ? "AARDE" : k < 6.2f ? "ZONNESTELSEL" : k < 7.4f ? "MELKWEG" : "OEPS, TE HARD";
    if (lab[0]) showTekst(lab, 120, 196, 2, 0xFFFF);
  } else if (showSoort == 2) {   // turbo
    showRect(0, 140, 240, 100, 0x2104); for (int i = 0; i < 9; i++) { float dx = fmodf(i * 30 - tt * 500, 270); if (dx < 0) dx += 270; showRect(dx - 15, 194, 16, 3, 0xFEA0); }
    float x = tt < 1.4f ? -60 + tt * 130 : tt < 4.6f ? 122 + sinf(tt * 40) * 2 : 122 + (tt - 4.6f) * 260, y = 150;
    if (tt > 1 && tt < 5.4f) for (int i = 0; i < 10; i++) { float a = fmodf(tt * 3 + i * .37f, 1.2f); circle(x - 34 - a * 90, y + 20 - a * 25 + sinf(i) * 6, 4 + a * 10, a < .6f ? 0x9CF3 : 0x6B4D); }
    if (tt > 1.3f) { float l = 18 + random(20); triF(x - 40, y + 4, x - 40, y + 16, x - 40 - l, y + 10, 0xFD20); triF(x - 40, y + 7, x - 40, y + 13, x - 40 - l * .6f, y + 10, 0xFFE0); }
    roundRect(x - 40, y - 6, 80, 26, 10, 0xF8A6); roundRect(x - 23, y - 29, 46, 28, 10, EYE); roundRect(x - 21, y - 27, 42, 24, 9, 0x0000);
    circle(x - 8, y - 16, 4, EYE); circle(x + 8, y - 16, 4, EYE);
    for (int i = -1; i <= 1; i += 2) { circle(x + i * 24, y + 22, 10, 0x18E3); float wa = tt * 40; dikLijn(x + i * 24, y + 22, x + i * 24 + cosf(wa) * 8, y + 22 + sinf(wa) * 8, 2, 0x8410); }
    if (tt > 1.4f && tt < 4.8f) showTekst("TURBO!", 120, 70, 4, 0xFEA0);
    if (tt > 4.8f) { char m[20]; snprintf(m, sizeof m, "0-100 %.1fs", flagTijd); showTekst(m, 120, 70, 2, 0xFFFF); }
  } else if (showSoort == 3) {   // podium
    triF(120, 0, 60, 200, 180, 200, 0x2100);
    const float P[3][4] = {{50, 168, 2, 0}, {120, 150, 1, 1}, {190, 176, 3, 2}}; const uint16_t PK[3] = {0xC618, 0xFEA0, 0xCC06};
    for (int i = 0; i < 3; i++) { showRect(P[i][0] - 32, P[i][1], 64, 240 - P[i][1], 0x29A7); char n[2] = {(char)('0' + (int)P[i][2]), 0}; showTekst(n, P[i][0], P[i][1] + 20, 3, PK[(int)P[i][3]]); }
    float by = 120 - (tt > 2.4f && tt < 5 ? fabsf(sinf(tt * 6)) * 6 : 0);
    miniBuddy(120, by, .85f, false, tt > 2.4f ? 2 : 0);
    if (tt > 2.4f) { float cy = by - 22; triF(103, cy, 137, cy, 120, cy - 4, 0xFEA0); triF(103, cy, 109, cy - 16, 115, cy, 0xFEA0); triF(115, cy, 120, cy - 20, 125, cy, 0xFEA0); triF(125, cy, 131, cy - 16, 137, cy, 0xFEA0);
      for (int i = 0; i < 40; i++) { float cx = fmodf(i * 53.7f, 220) + 10, cyy = fmodf(i * 31 + (tt - 2.4f) * (50 + i % 5 * 15), 240); static const uint16_t CK[5] = {0xF8A6, 0xFEA0, 0x35E6, 0x1C9F, 0xBADF}; showRect(cx, cyy, 5, 3, CK[i % 5]); }
      char m[20]; snprintf(m, sizeof m, "0-100 %.1fs", flagTijd); showTekst(m, 120, 40, 2, 0xFFFF); }
    else showTekst("EN DE WINNAAR IS", 120, 40, 2, 0xFFFF);
  } else if (showSoort == 4) {   // tijdreis
    float kr = tt < 3.2f ? tt / 3.2f : tt < 5 ? 1 : fmaxf(0, 1 - (tt - 5) / 2);
    for (int i = 11; i >= 0; i--) { float r = fmodf(i * 14 + tt * 90 * (.3f + kr), 168); if (r < 6) continue; circle(120, 120, fminf(118, r), hsv565(i * 30 + tt * 120, .9f, .25f + .6f * kr * (r / 168))); circle(120, 120, fminf(116, r - 3), 0x0000); }
    struct tm tmn; int j0 = 2026; if (getLocalTime(&tmn, 0)) j0 = tmn.tm_year + 1900;
    int jaar = tt < 3.2f ? j0 + (int)(powf(tt / 3.2f, 3) * 1000) : tt < 5 ? j0 + 1000 - (int)((tt - 3.2f) / 1.8f * 1000) : j0;
    char js[8]; snprintf(js, sizeof js, "%d", jaar); showTekst(js, 120 + sinf(tt * 30) * kr * 2, 56, 4, 0xFFFF);
    miniBuddy(120 + sinf(tt * 47) * 3 * kr, 145, .85f, false, kr > .5f ? 1 : 0);
    if (tt > 5.2f) showTekst("GEWOON VANDAAG", 120, 200, 2, 0xFFFF);
  } else if (showSoort == 5) {   // v69: tunnel
    for (int i = 0; i < 6; i++) { float a = -2.6f + i * .32f; dikLijn(120 + cosf(a) * 20, 120 + sinf(a) * 20, 120 + cosf(a) * 118, 120 + sinf(a) * 118, 2, 0x2945); a = .52f + i * .32f; dikLijn(120 + cosf(a) * 20, 120 + sinf(a) * 20, 120 + cosf(a) * 118, 120 + sinf(a) * 118, 2, 0x2945); }
    for (int i = 0; i < 5; i++) { float k = fmodf(tt * 1.6f + i * .2f, 1.f), r = 14 + k * k * 110;   // lampen die voorbij flitsen
      for (int s2 = -1; s2 <= 1; s2 += 2) { float a = -1.5708f + s2 * .55f; circle(120 + cosf(a) * r, 120 + sinf(a) * r, 2 + k * 6, k > .3f ? 0xFEA0 : 0x8400); } }
    miniBuddy(120, 150, .7f, false, 0); triF(108, 150, 70, 210, 100, 210, 0x4208); triF(132, 150, 140, 210, 170, 210, 0x4208);   // koplampbundels
    showTekst(tt < 3 ? "TUNNEL!" : "LICHTEN AAN", 120, 52, 2, 0xFEA0);
  } else if (showSoort == 6) {   // v69: rotonde, even duizelig
    float rot = tt * 4.f; for (int i = 0; i < 3; i++) { float a0 = rot + i * 2.094f; for (int k = 0; k < 8; k++) { float a1 = a0 + k * .16f, a2 = a1 + .16f; dikLijn(120 + cosf(a1) * 92, 120 + sinf(a1) * 92, 120 + cosf(a2) * 92, 120 + sinf(a2) * 92, 5, 0x5DFF); }
      float ae = a0 + 1.4f; triF(120 + cosf(ae) * 104, 120 + sinf(ae) * 104, 120 + cosf(ae) * 80, 120 + sinf(ae) * 80, 120 + cosf(ae + .25f) * 92, 120 + sinf(ae + .25f) * 92, 0x5DFF); }
    miniBuddy(120 + sinf(tt * 6) * 8, 120 + cosf(tt * 6) * 5, .9f, false, 1);
    showTekst(tt < 2.5f ? "WIEEE!" : "DUIZELIG...", 120, 200, 2, 0xFFFF);
  }
  (void)ms;
}
static void drawMeter() {
  const float cx = 120, cy = 122;
  boxAdd(0, 0, 240, 240);
  // v53: donkere wijzerplaat (past bij het zwarte scherm) met lichtgevende cijfers
  circle(120, 120, 119, 0x2945); circle(120, 120, 116, 0x632C); circle(120, 120, 112, 0x10A2);
  circle(120, 120, 109, 0x0841);
  for (int v = 160; v < 200; v += 2) { float a0 = meterHoek(v), a1 = meterHoek(v + 2.4f);   // rode zone
    dikLijn(cx + cosf(a0) * 99, cy - sinf(a0) * 99, cx + cosf(a1) * 99, cy - sinf(a1) * 99, 6, 0xD800); }
  for (int v = 0; v <= 200; v += 10) { float a = meterHoek(v), c = cosf(a), sn = sinf(a); bool groot = v % 20 == 0;
    dikLijn(cx + c * (groot ? 88 : 94), cy - sn * (groot ? 88 : 94), cx + c * 103, cy - sn * 103, groot ? 3.2f : 1.6f, groot ? EYE : 0x6B6D);
    if (groot) { char t[4]; snprintf(t, sizeof t, "%d", v); frame->setTextColor(v >= 160 ? 0xF9A6 : 0xFFFF); frame->setTextSize(2);
      frame->setCursor(X(cx + c * 74) - (int)strlen(t) * 6, Y(cy - sn * 74) - 7); frame->print(t); } }
  frame->setTextColor(0xF9A6); frame->setTextSize(2); frame->setCursor(X(120) - 30, Y(84)); frame->print("BUDDY");
  frame->setTextColor(0x8410); frame->setTextSize(2); frame->setCursor(X(120) - 24, Y(150)); frame->print("km/h");
  if (imuReady) { char gt[10]; snprintf(gt, sizeof gt, "%.2fG", fabsf(pushZ)); frame->setTextColor(0xFD20); frame->setTextSize(2); frame->setCursor(X(84) - (int)strlen(gt) * 6, Y(118)); frame->print(gt); }   // v52: G-kracht
  if (sprintT0) { char st[10]; snprintf(st, sizeof st, "%.1fs", (millis() - sprintT0) / 1000.f); frame->setTextColor(0x5DFF); frame->setTextSize(2); frame->setCursor(X(156) - (int)strlen(st) * 6, Y(118)); frame->print(st); }   // 0-100-klok
  roundRect(88, 170, 64, 18, 3, 0x0000); roundRect(90, 172, 60, 14, 2, 0x2104);   // kilometerteller
  char od[10]; snprintf(od, sizeof od, "%06d", (int)totKm % 1000000);
  frame->setTextColor(0x87F0); frame->setTextSize(2); frame->setCursor(X(120) - 36, Y(179) - 7); frame->print(od);
  float a = meterHoek(naald), c = cosf(a), sn = sinf(a);
  // vlammetjes achter de naaldpunt zolang je flink optrekt (flikkeren mee met de G-kracht)
  float vuur = fminf(1.f, fmaxf(0.f, (fabsf(pushZ) - .05f) * 4.f) + (naaldV > 15 ? .5f : 0));
  if (vuur > .05f) for (int i = 0; i < 4; i++) {
    float b = 94 - i * 10, px = cx + c * b, py = cy - sn * b;              // punt op de naald
    float l = (12 + random(16)) * vuur * (1 - i * .15f), w = 3 + random(3);  // vlam waait achter de naald aan (tegen de draairichting in)
    uint16_t kl = (i + (millis() / 60)) % 3 == 0 ? 0xFFE0 : ((i + (millis() / 60)) % 3 == 1 ? 0xFD20 : 0xF9A0);
    triF(px + c * w, py - sn * w, px - c * w, py + sn * w, px - sn * l, py - c * l, kl); }
  dikLijn(cx - c * 16, cy + sn * 16, cx + c * 98, cy - sn * 98, 6.0f, 0x6000);     // gloed
  dikLijn(cx - c * 16, cy + sn * 16, cx + c * 98, cy - sn * 98, 3.0f, 0xF800);     // rode naald
  circle(cx + c * 96, cy - sn * 96, 2.2f, 0xFFE0);                                 // gloeiend puntje
  circle(cx, cy, 10, 0x2104); circle(cx, cy, 5, 0xC618);                           // naaf
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
 if ((int32_t)(zoekTot - millis()) > 0) {   // v42: zoek mijn Buddy
   boxAdd(0, 0, 240, 240); bool aan = (millis() / 350) % 2; circle(120, 120, 118, aan ? 0xFFE0 : 0xF800); circle(120, 120, 96, BG);
   frame->setTextColor(0xFFFF); frame->setTextSize(4); frame->setCursor(X(120) - 60, Y(120) - 16); frame->print("HIER!"); return; }
 if ((int32_t)(showTot - millis()) > 0 && (int32_t)(flagTot - millis()) <= 0) { drawShow(); return; }   // v56: sprint-show
 if ((int32_t)(meterTot - millis()) > 0 && (int32_t)(flagTot - millis()) <= 0) { drawMeter(); return; }   // v52: de 0-100-vlag gaat voor   // v40: retro meter bij flink optrekken
 if (wxVers() && !slaapt) {   // v46: echt regen- of sneeuwweer -> druppels of vlokjes achter het gezicht
   int c = wxCode; bool regen = ((c >= 51 && c <= 67) || (c >= 80 && c <= 82) || c >= 95) && !(c >= 51 && c <= 57 && wxPrecip < .2f), sneeuw = (c >= 71 && c <= 77) || c == 85 || c == 86;
   if (wxRadarT && millis() - wxRadarT < 20UL * 60000) regen = wxRadarNu >= .1f;   // v56: radar gaat voor
   if (regen) for (int i = 0; i < 12; i++) { float x = 30 + i * 16 + sinf(i * 7.3f) * 5, y = fmodf(t * 140 + i * 37, 200) + 20; dikLijn(x, y, x - 2, y + 7, 1.6f, 0x3A9F); }
   else if (sneeuw) for (int i = 0; i < 12; i++) { float x = 26 + i * 17 + sinf(t * 1.3f + i) * 6, y = fmodf(t * 28 + i * 31, 200) + 20; circle(x, y, (i % 3) ? 1.8f : 2.6f, 0xDEFB); }
 }
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
 if (robotModus && koekjeZichtbaar && gagAct != 51) { float b = sinf(t * 3) * 2; circle(196, 176 + b, 12, 0xA2A0); circle(192, 172 + b, 1.8f, 0x3180); circle(200, 179 + b, 1.8f, 0x3180); circle(198, 170 + b, 1.4f, 0x3180); }   // v48: koekje
 if (slaapt && slaapMuts) { triF(150 + faceOx, 28 + faceOy, 96 + faceOx, 62 + faceOy, 176 + faceOx, 58 + faceOy, 0x319F); roundRect(92 + faceOx, 56 + faceOy, 88, 9, 4, 0xFFFF); circle(152 + faceOx, 26 + faceOy, 6, 0xFFFF); }   // slaapmutsje
 if ((int32_t)(focusTot - millis()) > 0) {   // focusmaatje: brilletje + aftellen
   for (int s2 = -1; s2 <= 1; s2 += 2) { float ex = 120 + s2 * 47 + faceOx, ey = 106 + faceOy; for (int i = 0; i < 3; i++) frame->drawRoundRect(X(ex - 28) + i, Y(ey - 26) + i, S(56) - 2 * i, S(52) - 2 * i, S(10), 0x8C51); boxAdd(ex - 28, ey - 26, ex + 28, ey + 26); }
   line(101 + faceOx, 104 + faceOy, 139 + faceOx, 104 + faceOy, 0x8C51, 3);
   uint32_t rest = (focusTot - millis()) / 1000; char ft[8]; snprintf(ft, sizeof ft, "%02u:%02u", (unsigned)(rest / 60), (unsigned)(rest % 60)); segTekst(ft, 120, 214, 14, GOLD); }
 // Originele Buddy-rij-HUD: rond maximumsnelheidsbord links, eigen snelheid rechts.
 // Zelfde principe/positie als speedDraw() in de webapp; bij geen bekende limiet tonen we '?'.
 bool fixNow = gpsFix && (millis()-gpsLastFixMs)<5000;
 bool tel = phoneFresh();
 int ownSpeed = fixNow ? (int)lroundf(gpsSpeedKmh) : (tel ? phone.v : -1);
 int limit = (tel && phone.lim > 0) ? phone.lim : ((wegLimiet > 0 && millis() - wegLimT < 6000) ? wegLimiet : ((gpsNearestCameraM >= 0 && gpsNearestCameraM <= 600) ? gpsCameraLimit : 0));
 bordBron = (tel && phone.lim > 0) ? 1 : (wegLimiet > 0 && millis() - wegLimT < 6000) ? 2 : (limit > 0 ? 3 : 0);   // v67: 1 telefoon, 2 eigen kaart, 3 flitser
 if (tel && phone.lim > 0 && !phone.limEst && fixNow) leerOnthoud(gpsLat, gpsLon, gpsKoers, phone.lim);   // v68: leren van de telefoon (geen geschatte borden)
 if (limit <= 0 && leerLim > 0 && millis() - leerLimT < 6000) { limit = leerLim; bordBron = 4; }   // v68: geleerd bord
 { static int lLim = 0; static uint32_t lT = 0; static float lKm = 0;   // v68: het laatste bord onthouden (2 km of 3 min) als de bron even wegvalt
   if (limit > 0) { lLim = limit; lT = millis(); lKm = totKm + tripKm; }
   else if (lLim > 0 && millis() - lT < 180000 && (totKm + tripKm) - lKm < 2.f) { limit = lLim; bordBron = 5; } }
 // v19: file en tekst van de telefoon bovenin, werk-driehoekje naast het bord
 int camM = (tel && phone.camD >= 0) ? phone.camD : gpsNearestCameraM;
 if (trajActief) {   // v25: trajectcontrole met je gemiddelde
   char ct[28]; snprintf(ct, sizeof ct, "TRAJECT %d / %d", (int)lroundf(trajGem), trajLim);
   bool tg = trajGem > trajLim + 2;
   boxAdd(30, 26, 210, 50); roundRect(34, 28, 172, 20, 10, tg ? 0xB000 : 0x0230);
   frame->setTextColor(0xFFFF); frame->setTextSize(2); frame->setCursor(X(120) - strlen(ct) * 6, Y(32)); frame->print(ct);
 } else if (otaPct >= 0) {
   char ct[24]; snprintf(ct, sizeof ct, "DOWNLOAD %d%%", otaPct);
   boxAdd(40, 26, 200, 50); roundRect(44, 28, 152, 20, 10, 0x0418);
   frame->setTextColor(0xFFFF); frame->setTextSize(2); frame->setCursor(X(120) - strlen(ct) * 6, Y(32)); frame->print(ct);
 } else if ((int32_t)(pinTot - millis()) > 0) {   // v29: koppelcode laten zien bij de eerste keer verbinden
   char ct[28]; snprintf(ct, sizeof ct, "CODE %06lu", (unsigned long)koppelPin);
   boxAdd(40, 26, 200, 50); roundRect(44, 28, 152, 20, 10, 0x0451);
   frame->setTextColor(0xFFFF); frame->setTextSize(2); frame->setCursor(X(120) - strlen(ct) * 6, Y(32)); frame->print(ct);
 } else if (!robotModus && tel && phone.msi[0]) {   // v29: matrixbord boven de weg
   boxAdd(70, 18, 170, 76); roundRect(72, 20, 96, 54, 7, 0x0841); frame->drawRoundRect(X(72), Y(20), S(96), S(54), S(7), EYE);
   bool kn = (millis() / 250) % 2; circle(80, 27, 3, kn ? 0xFD80 : 0x4200); circle(160, 27, 3, kn ? 0xFD80 : 0x4200); circle(80, 67, 3, kn ? 0x4200 : 0xFD80); circle(160, 67, 3, kn ? 0x4200 : 0xFD80);
   if (phone.msi[0] == 'x') { dikLijn(104, 32, 136, 62, 6, 0xF800); dikLijn(136, 32, 104, 62, 6, 0xF800); }
   else { circle(120, 47, 19, 0xF800); circle(120, 47, 15, 0xFFFF); segTekst(phone.msi, 120, 47, strlen(phone.msi) >= 3 ? 10.f : 13.f, 0x0000); }
 } else if (!robotModus && camM >= 0 && camM < 1000) {
   char ct[24]; snprintf(ct, sizeof ct, "FLITSER %dm", (camM / 10) * 10);
   boxAdd(40, 26, 200, 50); roundRect(44, 28, 152, 20, 10, 0x8000);
   frame->setTextColor(0xFFFF); frame->setTextSize(2); frame->setCursor(X(120) - strlen(ct) * 6, Y(32)); frame->print(ct);
 } else if (!robotModus && ((tel && phone.fileD >= 0) || (!tel && lokFileD >= 0))) {
   int fD = tel ? phone.fileD : lokFileD, fM = tel ? phone.fileMin : lokFileMin;
   char ft[32]; if (fD < 950) snprintf(ft, sizeof ft, "FILE %dm", (fD / 50) * 50); else snprintf(ft, sizeof ft, "FILE %d,%dkm", fD / 1000, (fD % 1000) / 100);
   if (fM > 0) { size_t l = strlen(ft); snprintf(ft + l, sizeof ft - l, " +%dm", fM); }
   boxAdd(50, 26, 190, 50); roundRect(52, 28, 136, 20, 10, 0x3082);
   bool aan = (millis() / 400) % 2; circle(64, 38, 3, aan ? 0xF8A6 : 0x7800); circle(73, 38, 3, aan ? 0x7800 : 0xF8A6);
   frame->setTextColor(0xFEDB); frame->setTextSize(2); frame->setCursor(X(82), Y(31)); frame->print(ft);
 } else if (!robotModus && tel && phone.nav) {   // v29: navigatiepijl met afstand
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
 } else if (!robotModus && (wxGlad() || (wxVers() && wxRegenOver >= 0 && wxRegenOver <= 15 && spdNow() > 20))) {   // v29: gladheid of regen
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
 if (robotModus) return;   // v47: robotmodus: geen bord, snelheid of GPS-regel
 if ((tel && phone.werk) || (!tel && lokWerk)) {   // werk-driehoekje boven het snelheidsbord
   boxAdd(96, 144, 124, 176);   // v68: een oranje pionnetje in plaats van een rood bord
   float pw = sinf(millis() / 260.f) * .8f;
   triF(110 + pw, 146, 101, 170, 119, 170, 0xFC40);
   dikLijn(106.6f + pw * .7f, 155, 113.4f + pw * .7f, 155, 2.6f, 0xFFFF); dikLijn(104 + pw * .4f, 163, 116 + pw * .4f, 163, 2.6f, 0xFFFF);
   roundRect(97, 169, 26, 5, 2, 0xC300);
 }
 // v22: grote, haarscherpe cijfers die precies in het midden staan (eigen 7-segment-cijfers)
 bool rust = (ownSpeed <= 0) && klokOk() && (gagAct < 0);   // stilstaan: klok in plaats van 0
 int sx = 78, sy = 194; float sr = 29;   // v68: groter bord
 boxAdd(sx - sr - 2, sy - sr - 2, sx + sr + 2, sy + sr + 2);
 bool teHard = limit > 0 && ownSpeed > limit + 3;
 hudLimiet = limit; hudSnelheid = ownSpeed;   // v39: voor de te-hard-waarschuwing
 { float rp = -1; uint16_t rk = EYE;   // v68: ring langs de rand, net als in de app: flitser dichterbij of hoe ver je in het traject bent
   if (trajActief) { rp = trajFrac; rk = trajGem > trajLim + 2 ? 0xF800 : EYE; }
   else if (camM >= 0 && camM < 600) { rp = 1.f - camM / 600.f; rk = teHard || camM < 250 ? 0xF800 : EYE; }
   if (rp >= 0) { const float R = 114; for (int i = 0; i < 48; i++) { float a0 = -1.5708f + i * .1309f, a1 = a0 + .1309f; dikLijn(120 + cosf(a0) * R, 120 + sinf(a0) * R, 120 + cosf(a1) * R, 120 + sinf(a1) * R, 4.5f, 0x2104); }
     int nseg = (int)(fminf(1.f, rp) * 48); for (int i = 0; i < nseg; i++) { float a0 = -1.5708f + i * .1309f, a1 = a0 + .1309f; dikLijn(120 + cosf(a0) * R, 120 + sinf(a0) * R, 120 + cosf(a1) * R, 120 + sinf(a1) * R, 5.f, rk); } } }
 uint16_t ring = (teHard && (millis() / 350) % 2) ? 0xF800 : 0xD800;
 frame->fillCircle(X(sx), Y(sy), S(sr), ring);
 frame->fillCircle(X(sx), Y(sy), S(sr - 4.2f), 0xFFFF);
 char limTxt[6]; if (limit > 0) snprintf(limTxt, sizeof limTxt, "%d", limit); else snprintf(limTxt, sizeof limTxt, "-");
 segTekst(limTxt, sx, sy, strlen(limTxt) >= 3 ? 16.f : 22.f, 0x0000);
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
 } else if (tel && phone.eta[0]) {   // v39: hoe laat je thuis bent
   char st[16]; snprintf(st, sizeof st, "Thuis %s", phone.eta);
   frame->setTextColor(0x8C71); frame->setTextSize(2); frame->setCursor(X(120) - strlen(st) * 6, Y(229) - 8); frame->print(st);
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
// v37: snelle verzending. Twee stukjes geheugen in de chip zelf: terwijl het ene stukje via DMA naar het scherm gaat,
// wordt het volgende al klaargezet. Grote stukken (8 KB) i.p.v. 2 KB, en rijen van een smal vak achter elkaar geplakt.
static spi_device_handle_t snelSpi = nullptr;
static uint8_t *snelBuf[2] = {nullptr, nullptr};
static spi_transaction_ext_t snelTr[2];
static constexpr size_t SNEL_STUK = 8192;
static bool snelInit() {
  spi_device_interface_config_t dc = {};
  dc.command_bits = 8; dc.address_bits = 24; dc.mode = 0;
  dc.clock_source = SPI_CLK_SRC_DEFAULT; dc.clock_speed_hz = QSPI_HZ;
  dc.spics_io_num = -1; dc.flags = SPI_DEVICE_HALFDUPLEX; dc.queue_size = 2;
  if (spi_bus_add_device(SPI2_HOST, &dc, &snelSpi) != ESP_OK) { snelSpi = nullptr; return false; }
  for (int i = 0; i < 2; i++) {
    snelBuf[i] = (uint8_t *)heap_caps_aligned_alloc(16, SNEL_STUK, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!snelBuf[i]) { spi_bus_remove_device(snelSpi); snelSpi = nullptr; return false; }
  }
  return true;
}
static void snelVerstuur(const uint16_t *fb, const Box &b) {
  const size_t rij = (size_t)(b.x1 - b.x0 + 1) * 2;
  int y = b.y0; size_t inRij = 0; int bezig = 0, idx = 0; bool eerste = true;
  spi_transaction_t *klaar;
  spi_device_acquire_bus(snelSpi, portMAX_DELAY);
  gpio_set_level((gpio_num_t)LCD_CS, 0);
  while (y <= b.y1) {
    if (bezig == 2) { spi_device_get_trans_result(snelSpi, &klaar, portMAX_DELAY); bezig--; }   // dit stukje is weer vrij
    uint8_t *dst = snelBuf[idx]; size_t n = 0;
    while (n < SNEL_STUK && y <= b.y1) {
      size_t neem = rij - inRij; if (neem > SNEL_STUK - n) neem = SNEL_STUK - n;
      memcpy(dst + n, (const uint8_t *)(fb + (int32_t)y * LCD_WIDTH + b.x0) + inRij, neem);
      n += neem; inRij += neem; if (inRij == rij) { inRij = 0; y++; }
    }
    spi_transaction_ext_t &t = snelTr[idx]; memset(&t, 0, sizeof t);
    if (eerste) { t.base.flags = SPI_TRANS_MODE_QIO; t.base.cmd = 0x32; t.base.addr = 0x003C00; eerste = false; }
    else t.base.flags = SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY;
    t.base.tx_buffer = dst; t.base.length = n * 8;
    spi_device_queue_trans(snelSpi, &t.base, portMAX_DELAY); bezig++; idx ^= 1;
  }
  while (bezig) { spi_device_get_trans_result(snelSpi, &klaar, portMAX_DELAY); bezig--; }
  gpio_set_level((gpio_num_t)LCD_CS, 1);
  spi_device_release_bus(snelSpi);
}
void flushTask(void *) {
  FlushJob job;
  for (;;) {
    if (xQueueReceive(flushQueue, &job, portMAX_DELAY) != pdTRUE) continue;
    if (wilLicht && wilLicht != isLicht) { isLicht = wilLicht; gfx->setBrightness(isLicht); }
    uint32_t t0 = micros();
    uint16_t w = job.b.x1 - job.b.x0 + 1, h = job.b.y1 - job.b.y0 + 1;
    if (snelSpi) {
      gfx->startWrite(); gfx->writeAddrWindow(job.b.x0, job.b.y0, w, h); gfx->endWrite();
      snelVerstuur(job.fb, job.b);
    } else {
    gfx->startWrite();
    if (w * 10 >= LCD_WIDTH * 6) {
      // v35: breed vak -> hele rijen in één keer versturen (één lange stroom i.p.v. honderden losse rijtjes)
      gfx->writeAddrWindow(0, job.b.y0, LCD_WIDTH, h);
      bus->writeBytes((uint8_t *)(job.fb + (int32_t)job.b.y0 * LCD_WIDTH), (uint32_t)LCD_WIDTH * h * 2);   // v36: zonder omrekenen
    } else {
      gfx->writeAddrWindow(job.b.x0, job.b.y0, w, h);
      for (int16_t y = job.b.y0; y <= job.b.y1; y++) bus->writeBytes((uint8_t *)(job.fb + (int32_t)y * LCD_WIDTH + job.b.x0), (uint32_t)w * 2);
    }
    gfx->endWrite();
    }
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
   if (klapT && klapT != kl0) { kl0 = klapT; aaiTot = millis() + 2000; laatsteActie = millis(); if (gagAct < 0) { if (robotModus && wxVers()) startGag(53); else { startGag(7); gagMsg = "Ja? Ik ben er!"; } } }
   if (knalT && knalT != kn0 && millis() - knalT < 300) { kn0 = knalT; if (millis() > schrikTot + 4000) schrikTot = millis() + 900; }
   (void)mu0; }   // v42: muziek en praten zitten nu in persStap
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
  uint8_t reg = volume == 0 ? 0 : (uint8_t)(0x70 + (uint16_t)volume * (0xD8 - 0x70) / 100U);   // v57: 100% = wat vroeger 85% was (hoger stuurt het versterkertje over en dan valt het geluid weg)
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
    { // v42: praten of muziek? Praten = geluid met veel korte stiltes ertussen; muziek = (bijna) aan één stuk door.
      static uint32_t wT = 0, stilStart = 0; static float luidS = 0, rmsSom = 0; static int gaps = 0, nRms = 0; static bool wasLuid = false;
      if (!wT) wT = nu;
      if (!audioBusy) { if (luid) { luidS += dt; if (!wasLuid && stilStart && nu - stilStart >= 120 && nu - stilStart <= 800) gaps++; } else if (wasLuid) stilStart = nu;
        wasLuid = luid; rmsSom += rms; nRms++; }
      if (nu - wT >= 6000) {
        float frac = luidS / ((nu - wT) / 1000.f), gem = nRms ? rmsSom / nRms : 0; uint8_t sr = 0;
        if (frac > .75f && gaps <= 2) sr = gem > oorBasis * 5 + .01f ? 3 : 2;
        else if (frac > .3f && gaps >= 4) sr = 1;
        if (sr != oorSoort) { oorSoort = sr; oorSoortT = nu; }
        wT = nu; luidS = 0; gaps = 0; rmsSom = 0; nRms = 0;
      }
    }
    if (rms > oorBasis * 6 + .03f) {   // korte harde piek
      if (nu - piekT > 120) { if (nu - piekT < 700) pieken++; else pieken = 1; piekT = nu; }
      if (pieken == 2) { klapT = nu; pieken = 0; } else if (!audioBusy) knalT = nu;
    }
  }
}
static char idfFouten[4][110]; static int idfFoutN = 0; static bool audioAlleenSpeaker = false;   // v66: alle foutregels van de ESP32 bij het starten van het geluid
static int idfVangPrintf(const char *fmt, va_list ap) { char t[110]; va_list ap2; va_copy(ap2, ap); vsnprintf(t, sizeof t, fmt, ap2); va_end(ap2);
  if (idfFoutN < 4 && (strstr(t, "E (") || strstr(t, "W (") || strstr(t, "rror") || strstr(t, "fail"))) { char *nl = strchr(t, '\n'); if (nl) *nl = 0; strncpy(idfFouten[idfFoutN], t, 109); idfFouten[idfFoutN][109] = 0; idfFoutN++; }
  return vprintf(fmt, ap); }
bool initBuddyAudio() {
  logRegel("geluid: start, vrij DMA %u (grootste blok %u)", (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  vprintf_like_t oudLog = esp_log_set_vprintf(idfVangPrintf);
  pinMode(PA, OUTPUT);
  digitalWrite(PA, HIGH);
  buddyI2S.setPins(BCLKPIN, WSPIN, DIPIN, DOPIN, MCLKPIN);
  heap_caps_malloc_extmem_enable(1024 * 1024);   // v67: alles wat het geluid nu aanmaakt in het interne geheugen (anders: 'user context not in internal RAM')
  bool i2sOk = false;   // v62: een paar keer proberen (op sommige ESP32-versies lukt de eerste keer niet)
  for (int poging = 0; poging < 3 && !i2sOk; poging++) { if (poging) { buddyI2S.end(); delay(150); buddyI2S.setPins(BCLKPIN, WSPIN, DIPIN, DOPIN, MCLKPIN); }
    i2sOk = buddyI2S.begin(I2S_MODE_STD, AUDIO_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH); }
  if (!i2sOk) {   // v66: lukt speaker + microfoons samen niet? Dan alleen de speaker (het oor doet dan even niet mee)
    for (int k = 0; k < idfFoutN; k++) logRegel("geluid fout %d: %s", k + 1, idfFouten[k]);
    idfFoutN = 0; buddyI2S.end(); delay(150); buddyI2S.setPins(BCLKPIN, WSPIN, DIPIN, -1, MCLKPIN);
    i2sOk = buddyI2S.begin(I2S_MODE_STD, AUDIO_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH);
    audioAlleenSpeaker = i2sOk; logRegel("geluid: alleen speaker %s", i2sOk ? "gelukt" : "ook mislukt"); }
  esp_log_set_vprintf(oudLog);
  heap_caps_malloc_extmem_enable(4096);   // v67: daarna weer normaal
  if (!i2sOk) {
    Serial.println("Buddy audio: I2S start mislukt"); strcpy(audioStatus, "i2s-fout"); logRegel("geluid: I2S start mislukt");
    for (int k = 0; k < idfFoutN; k++) logRegel("geluid fout %d: %s", k + 1, idfFouten[k]);
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
      Serial.printf("Buddy audio: ES8311 fout bij register 0x%02X\n", regs[i][0]); snprintf(audioStatus, sizeof audioStatus, "codec-%02X", regs[i][0]); logRegel("geluid: ES8311 fout bij register 0x%02X", regs[i][0]);
      return false;
    }
    if (i==0) delay(20);
  }
  applyCodecVolume(buddyVolume);
  Serial.println("Buddy audio: ES8311 klaar"); strcpy(audioStatus, "ok"); logRegel("geluid: ES8311 klaar, volume %d", (int)buddyVolume);
  if (!audioAlleenSpeaker && es7210Init()) { xTaskCreatePinnedToCore(oorTaak, "BuddyOor", 6144, nullptr, 1, nullptr, 0); Serial.println("Buddy oor: ES7210 microfoons klaar"); }
  else Serial.println("Buddy oor: ES7210 niet gevonden");
  return true;
}

struct Note { uint16_t hz; uint16_t ms; uint16_t hz2; };   // v46: hz2 = glijden naar deze toon (0 = vaste toon)
struct SoundJob { Note notes[8]; uint8_t count; };

void soundTask(void *arg) {
  SoundJob job = *(SoundJob*)arg;
  delete (SoundJob*)arg;
  int16_t pcm[640]; // 20 ms stereo buffer at 16 kHz
  for (uint8_t n=0; n<job.count; ++n) {
    const uint32_t totalFrames = (uint32_t)AUDIO_RATE * job.notes[n].ms / 1000;
    uint32_t done = 0; float phase = 0;
    const float f0 = job.notes[n].hz, f1 = job.notes[n].hz2 ? job.notes[n].hz2 : f0;
    while (done < totalFrames) {
      uint32_t count = min((uint32_t)320, totalFrames-done);
      for (uint32_t i=0; i<count; ++i) {
        float k = (float)(done + i) / totalFrames, f = f0 + (f1 - f0) * k;   // glijdende toon
        phase += 6.2831853f * f / AUDIO_RATE; if (phase > 6.2831853f) phase -= 6.2831853f;
        float env = 0.55f;
        if (done+i < 160) env *= (done+i)/160.0f;
        if (totalFrames-(done+i) < 240) env *= (totalFrames-(done+i))/240.0f;
        float golf = f0 < 30 ? 0 : sinf(phase) * .82f + sinf(phase * 2) * .18f;   // zachter, ronder klankje (hz 0..29 = stilte)
        int16_t v = buddyVolume == 0 ? 0 : (int16_t)(golf * 30000.0f * env);
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

void playGeluid(int g) {   // v46: echte Buddy-geluidjes met glijtonen
  if (audioBusy && millis() - audioBusyT > 8000) { audioBusy = false; logRegel("geluid: hing vast, losgemaakt"); }   // v58: vangnet
  if (!audioReady) { static uint32_t gemeld = 0; if (!gemeld || millis() - gemeld > 60000) { gemeld = millis(); logRegel("geluid: niet klaar (%s)", audioStatus); } return; }
  if (audioBusy) return;
  SoundJob *j = new SoundJob(); uint8_t n = 0;
  auto z = [&](uint16_t a, uint16_t ms, uint16_t b) { if (n < 8) j->notes[n++] = {a, ms, b}; };
  switch (g) {
    case GL_HOI:     z(520, 110, 780); z(10, 40, 0); z(700, 160, 1050); break;                                   // "hoi!" omhoog
    case GL_GIECHEL: for (int i = 0; i < 4; i++) { z(900 + i * 60, 55, 1250 + i * 60); z(10, 25, 0); } break;      // hihihi
    case GL_VROEM:   z(110, 260, 180); z(180, 220, 320); z(320, 300, 560); break;                                // motor die optrekt
    case GL_DINGDONG: z(988, 220, 0); z(10, 30, 0); z(784, 360, 0); break;                                       // ding-dong
    case GL_PLING:   z(1320, 70, 1760); z(1760, 160, 2100); break;                                               // pling!
    case GL_OEPS:    z(900, 120, 520); z(10, 30, 0); z(600, 180, 380); break;                                    // oeps (omlaag)
    case GL_SLAAP:   z(600, 260, 380); z(10, 60, 0); z(380, 380, 260); break;                                    // welterusten
    case GL_TADA:    z(523, 90, 0); z(659, 90, 0); z(784, 90, 0); z(1047, 260, 1100); break;                      // ta-da!
    case GL_RUIMTE:  z(80, 900, 1400); z(587, 150, 0); z(740, 150, 0); z(880, 150, 0); z(1175, 300, 0); z(1480, 150, 0); z(1760, 500, 2349); break;   // v56: de ruimte in
    case GL_TURBO:   z(110, 700, 900); z(900, 250, 300); z(523, 90, 0); z(659, 90, 0); z(784, 90, 0); z(1047, 400, 0); break;                        // v56: turbo
    case GL_PODIUM:  z(392, 150, 0); z(523, 150, 0); z(659, 150, 0); z(784, 250, 0); z(10, 60, 0); z(659, 150, 0); z(784, 180, 0); z(1047, 600, 0); break;   // v56: podium
    case GL_TIJD:    z(200, 1200, 1600); z(1600, 900, 150); z(10, 200, 0); z(880, 180, 0); z(1109, 180, 0); z(1319, 500, 0); break;                   // v56: tijdreis
    default: delete j; return;
  }
  j->count = n; audioBusy = true; audioBusyT = millis(); audioGespeeld++;
  if (xTaskCreatePinnedToCore(soundTask, "BuddySound", 4096, j, 1, nullptr, 1) != pdPASS) { audioBusy = false; delete j; logRegel("geluid: taak starten mislukt, vrij %u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL)); }
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
 if(i2cMutex && xSemaphoreTake(i2cMutex,0)!=pdTRUE) return; uint8_t n=buddyTouch.getPoint(touchX,touchY,1); if(i2cMutex) xSemaphoreGive(i2cMutex);   // v50: het vrijgeven stond per ongeluk in commentaar -> I2C bleef bezet (touch dood, GPS-fouten, crashes)
 if(!n) {
   // loslaten = een tik afhandelen
   if (down && !lang && now - downMs < 600) {
     if (venster) { if (lY > 177.0f && fabsf(lX - 120.0f) < 62.0f) sluitVenster();   // TERUG
       else if (venster == 1 && otaKlaarV && lY < 62.0f) installeerOta();   // v68: update installeren
       else if (venster == 2 && lY > 146.0f && lY < 176.0f) { vensterTot = now + 30000;   // v59: TEST of nachtstand
         if (lX < 120) { if (audioBusy) { audioBusy = false; logRegel("geluid: los gemaakt via TEST"); } testPiepT = now; playGeluid(GL_PLING); logRegel("geluid test (scherm): klaar=%d volume=%d status=%s", (int)audioReady, (int)buddyVolume, audioStatus); }
         else { nachtStil = !nachtStil; buddyPrefs.putBool("nstil", nachtStil); logRegel("nachtstand: %s", nachtStil ? "stil 22-7" : "geluid ook 's nachts"); } } }
     else if (menuOpen) menuKies(lX, lY);
     else if (lY > 188.0f && !(koekjeZichtbaar && robotModus && fabsf(lX - 196) < 24)) openVenster(2);   // onderaan tikken = volume
     else if (koekjeZichtbaar && robotModus && fabsf(lX - 196) < 24 && fabsf(lY - 176) < 24) {   // v48: koekje!
       koekjeZichtbaar = false; energie = 100; startGag(51); playGeluid(GL_PLING); vriendPlus(1); laatsteActie = now; }
     else if (gagAct != 1 && (hypotf(lX - (73 + faceOx), lY - (106 + faceOy)) < 22 || hypotf(lX - (167 + faceOx), lY - (106 + faceOy)) < 22)) {   // v48: in zijn oog
       auKant = lX > 120 ? 1 : 0; startGag(47); playGeluid(GL_OEPS); }
     else if (gagAct != 1 && lY > 132 && lY < 172 && (lX < 72 || lX > 168)) { startGag(48); playGeluid(GL_GIECHEL); aaiTot = now + 2000; }   // op zijn wang
     else if ([&]() { static uint32_t tk[3] = {0}; tk[0] = tk[1]; tk[1] = tk[2]; tk[2] = now; return tk[0] && now - tk[0] < 1300; }() && gagAct != 1 && robotModus) { startGag(50); playGeluid(GL_HOI); }   // v49: 3x snel tikken = kiekeboe
     else { aaiTot = now + 2200; idleMoodUntilMs = now + 2200;    // op zijn gezicht tikken = aaien
       static uint32_t aaiPlusT = 0; if (now - aaiPlusT > 20000) { aaiPlusT = now; vriendPlus(1); }   // v42: aaien = vriendschap (max 3 per minuut)
       if (vriend > 70) { addParticleAt(0, frand(90, 150), 70, frand(-10, 10), -25, 1.4f, 10); static uint32_t gT = 0; if (now - gT > 15000) { gT = now; playGeluid(GL_GIECHEL); } }
       if (gagAct == 1 && t - gagT0 < 4.8f && fabsf(lX - g_fx) < 30 && fabsf(lY - g_fy) < 30) {   // v46: vlieg gevangen!
         vliegGevangen = true; vliegTotaal++; buddyPrefs.putUShort("vlieg", vliegTotaal); vriendPlus(1); playGeluid(GL_PLING);
         snprintf(vliegTxt, sizeof vliegTxt, "Gevangen! Vlieg nr. %u", (unsigned)vliegTotaal); burstAt(g_fx, g_fy, 10); gagT0 = t - 4.8f; }
       if (gagAct >= 0 && gagAct < 64 && GAGS[gagAct].flags) { if (gagLike[gagAct] < 20) gagLike[gagAct]++; likeVuil = true; gagMsg = "Vind je dit leuk? Doe ik vaker!"; }
     }
   }
   down = false; lang = false; return;
 }
 // CST9217 levert paneelcoördinaten; vertaal naar de 240x240 tekenruimte.
 // v30: de touchlaag van dit bordje zit 180 graden gedraaid t.o.v. het beeld (net als in het Waveshare-voorbeeld: setMirrorXY(true,true))
 float x=240.0f-(float)touchX[0]*240.0f/(float)LCD_WIDTH;
 float y=240.0f-(float)touchY[0]*240.0f/(float)LCD_HEIGHT;
 if (!down) { down = true; downMs = now; dX = x; dY = y; lang = false; laatsteActie = now; if (slaapt) { slaapt = false; laatsteActie = now; } }
 { // v48: hand erop (2 vingers) = kiekeboe; snel heen en weer vegen = kietelen
   static float vx = 0; static int rich = 0, keer = 0; static uint32_t keerT = 0;
   float ddx = x - vx; vx = x;
   if (fabsf(ddx) > 4) { int r2 = ddx > 0 ? 1 : -1; if (rich && r2 != rich) { if (now - keerT > 1200) keer = 0; keer++; keerT = now; } rich = r2; }
   if (keer >= 4 && !menuOpen && !venster) { keer = 0; lang = true; startGag(49); playGeluid(GL_GIECHEL); vriendPlus(1); }
 }
 lX = x; lY = y;
 if (venster) { vensterTot = now + 30000; vensterSleep(x, y); return; }   // in een venster: geen lang-drukken, wel slepen
 // lang drukken (0,7 s, zonder te slepen) opent of sluit het ronde menu
 if (!lang && now - downMs > 700 && fabsf(x - dX) < 12 && fabsf(y - dY) < 12) { lang = true; menuOpen = !menuOpen; menuTot = now + 10000; }
}

void setup(){
 Serial.begin(115200);
 // USB CDC kan na reset iets later beschikbaar komen; wacht kort en meld expliciet.
 uint32_t serialWaitStart=millis();
 while(!Serial && millis()-serialWaitStart<300) delay(10);   // v33: niet meer 2,5 s wachten op een computer (in de auto is die er niet)
 logStart();   // v64: logboek als eerste, zodat ook fouten bij het opstarten (geluid) erin komen
 delay(200);
 Serial.println();
 Serial.println("========================================");
 Serial.printf("BUDDY v%d BOOT - USB SERIAL ACTIEF\n", FW_VERSIE);
 Serial.println("Baudrate: 115200 | USB CDC check");
 Serial.printf("PSRAM: %u bytes vrij | heap: %u bytes\n", (unsigned)ESP.getFreePsram(), (unsigned)ESP.getFreeHeap());
 buddyPrefs.begin("buddy", false); buddyVolume=buddyPrefs.getUChar("volume", 85);
 Wire.setBufferSize(1100);  // v15: groot genoeg voor een blok GPS-tekst via I2C
 { // v51: I2C-bus losmaken als een apparaat de datalijn nog vasthoudt (na een crash of diepe slaap)
   pinMode(IIC_SDA, INPUT_PULLUP); pinMode(IIC_SCL, INPUT_PULLUP); delayMicroseconds(10);
   if (digitalRead(IIC_SDA) == LOW) { pinMode(IIC_SCL, OUTPUT_OPEN_DRAIN);
     for (int i = 0; i < 18 && digitalRead(IIC_SDA) == LOW; i++) { digitalWrite(IIC_SCL, LOW); delayMicroseconds(6); digitalWrite(IIC_SCL, HIGH); delayMicroseconds(6); }
     pinMode(IIC_SDA, OUTPUT_OPEN_DRAIN); digitalWrite(IIC_SDA, LOW); delayMicroseconds(6); digitalWrite(IIC_SCL, HIGH); delayMicroseconds(6); digitalWrite(IIC_SDA, HIGH); delayMicroseconds(6);   // STOP
     Serial.println("I2C-bus losgemaakt"); }
 }
 Wire.begin(IIC_SDA,IIC_SCL); Wire.setClock(100000); Wire.setTimeOut(50); i2cMutex=xSemaphoreCreateMutex(); randomSeed(esp_random());
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
   delay(400);   // GPS rustig laten opstarten (v33: korter, de GPS-taak vangt latere data toch op)
 }
 gpsSerial.begin(GPS_UART_BAUD, SERIAL_8N1, GPS_UART_RX, GPS_UART_TX);
 gpsSerial.setRxBufferSize(4096);
 delay(300);   // v33: korter; eventuele rommel van de LC76G wordt later gewoon overgeslagen
 while(gpsSerial.available()) gpsSerial.read();
 Serial.printf("Buddy GPS UART gestart | RX=%d TX=%d | %lu baud | reset=%s\n", GPS_UART_RX, GPS_UART_TX, (unsigned long)GPS_UART_BAUD, GPS_RESET_PULSE ? (gpsResetOk?"OK":"NIET BEVESTIGD") : "UIT (niet aangeraakt)");
 buddyTouch.setPins(TP_RESET, TP_INT);
 if (buddyTouch.begin(Wire, CST92XX_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) { touchReady=true; Serial.println("Buddy touch: CST9217 klaar"); }
 else Serial.println("Buddy touch: niet beschikbaar; volumeregeling via scherm uitgeschakeld");
 audioReady = initBuddyAudio();   // v63: geluid eerst starten, voordat het beeld het speciale (DMA-)geheugen inpikt
 frames[0] = new PsramCanvas(LCD_WIDTH, LCD_HEIGHT, gfx, 0, 0, 0);
 frames[1] = new PsramCanvas(LCD_WIDTH, LCD_HEIGHT, gfx, 0, 0, 0);
 frame = frames[0];
 if (!frames[0] || !frames[1]) { Serial.println("Buddy ERROR: canvas-object niet aangemaakt"); while(true) delay(1000); }
 if (!frames[0]->begin(QSPI_HZ) || !frames[1]->begin(GFX_SKIP_OUTPUT_BEGIN)) { Serial.printf("Buddy ERROR: framebuffer init mislukt (staat PSRAM op 'OPI PSRAM'?) | PSRAM=%u | heap=%u\n", (unsigned)ESP.getFreePsram(), (unsigned)ESP.getFreeHeap()); while(true) delay(1000); }
 gfx->setBrightness(160);
 frames[1]->fillScreen(BG); frame->fillScreen(BG); frame->flush();
 frames[1]->setTextColor(EYE); frames[1]->setTextSize(1);
 flushQueue = xQueueCreate(1, sizeof(FlushJob)); flushIdle = xSemaphoreCreateBinary(); xSemaphoreGive(flushIdle);
 if (snelInit()) Serial.println("Buddy beeld: snelle verzending (DMA, 2x8 KB) aan"); else Serial.println("Buddy beeld: snelle verzending niet gelukt, gewone verzending");
 xTaskCreatePinnedToCore(flushTask, "BuddyFlush", 8192, nullptr, 1, nullptr, 0);   // v36: weer prioriteit 1 (met 2 kreeg core 0 geen adem -> waakhond)
 Serial.printf("Buddy beeld klaar | framebuffer=16-bit PSRAM | QSPI=%ld Hz | PSRAM over: %u | heap over: %u\n", (long)QSPI_HZ, (unsigned)ESP.getFreePsram(), (unsigned)ESP.getFreeHeap());
 frame->setTextColor(EYE); frame->setTextSize(1);
 // v19: bewegingssensor
 Serial.println("Buddy gyro: starten..."); Serial.flush();
 if (qmiInit()) { imuReady = true; Serial.println("Buddy gyro: QMI8658 klaar (meehangen in bochten)"); }
 else Serial.println("Buddy gyro: QMI8658 niet gevonden");
 Serial.println("Buddy BLE: starten..."); Serial.flush();
 helderheid = buddyPrefs.getUChar("licht", 160); wilLicht = helderheid; waarschuwAan = buddyPrefs.getBool("warn", true);
 rec80 = buddyPrefs.getFloat("b80", 0); rec100 = buddyPrefs.getFloat("b100", 0); recG = buddyPrefs.getFloat("gmax", 0);
 totKm = buddyPrefs.getFloat("km", 0); totRit = buddyPrefs.getUInt("rit", 0);
 buddyPrefs.getString("d80", dat80, sizeof dat80); buddyPrefs.getString("d100", dat100, sizeof dat100);
 koppelPin = buddyPrefs.getUInt("pin", 0); if (koppelPin < 100000) { koppelPin = 100000 + esp_random() % 900000; buddyPrefs.putUInt("pin", koppelPin); }
 bleStart();
 if(i2cMutex) xTaskCreatePinnedToCore(gpsTask,"BuddyGPS",8192,nullptr,1,nullptr,0);
 bool wakkerDoorBeweging = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0;
 buddyPrefs.getString("wurl", wCfg.url, sizeof wCfg.url); buddyPrefs.getString("grp", wCfg.grp, sizeof wCfg.grp); buddyPrefs.getString("van", wCfg.van, sizeof wCfg.van); buddyPrefs.getString("naam", wCfg.naam, sizeof wCfg.naam);
 buddyPrefs.getString("ssid", wifiNaam, sizeof wifiNaam); buddyPrefs.getString("pass", wifiPass, sizeof wifiPass);
 xTaskCreatePinnedToCore(netTaak, "BuddyNet", 12288, nullptr, 1, nullptr, 0);
 logRegel("===== OPSTART v%d | reden: %s | heap %u =====", FW_VERSIE, resetReden(), (unsigned)ESP.getFreeHeap());
 setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1); tzset();   // v42: Nederlandse tijd, ook zonder wifi
 persLaad(); groetVanaf = millis() + 2500; nextGagT = 20; vliegTotaal = buddyPrefs.getUShort("vlieg", 0); energie = buddyPrefs.getUChar("energie", 100);
 if (wakkerDoorBeweging) logRegel("wakker geworden door beweging (instappen?)");
 nextBlink=2;nextGaze=.5f;Serial.println("Buddy: koppelcode, navigatie, matrixborden, weer, oor | files/werk/groep zonder telefoon, zwaaien | outfits, instellen vanuit de app | uit/aan met de auto | wifi, eigen limieten, trajecten, updates | logboek | grote cijfers, klok, slapen, nachtstand | 28 animaties, 0-100 vlag, ritjes, G-meter | menu, meehangen, telefoon-koppeling | GPS-transactie in een keer | dubbele framebuffer (tekenen en versturen tegelijk) + LC76G via I2C zonder resetpuls + I2C-scan");
}
// v55: lukt koppelen steeds niet (bijvoorbeeld na een weggedrukt koppelvenster), dan oude koppelingen wissen zodat de telefoon opnieuw kan koppelen
static void koppelHerstel() {
  static uint32_t laatst = 0; uint32_t nu = millis(); if (nu - laatst < 1000) return; laatst = nu;
  if (bleConnected && !gekoppeld && verbindT && nu - verbindT > 90000) { verbindT = nu; if (koppelMislukt < 250) koppelMislukt++; }
  if (gekoppeld) koppelMislukt = 0;
  if (koppelMislukt < 2) return;
  koppelMislukt = 0;
  int n = -1;
#if defined(BOND_BLUEDROID)
  n = esp_ble_get_bond_device_num();
  if (n > 0) { esp_ble_bond_dev_t *l = (esp_ble_bond_dev_t *)malloc(sizeof(esp_ble_bond_dev_t) * n);
    if (l) { esp_ble_get_bond_device_list(&n, l); for (int i = 0; i < n; i++) esp_ble_remove_bond_device(l[i].bd_addr); free(l); } }
#elif defined(BOND_NIMBLE)
  ble_store_clear();
#endif
  logRegel("BLE: koppelen lukte niet, %d oude koppeling(en) gewist", n);
  if (bleConnected) pinTot = millis() + 60000;
}
void loop(){
 handleTouch();
 koppelHerstel();
 {
   static uint32_t logStatusMs = 0; static bool hadFix = false; static int fpsLog = 0;
   bool fixNu = gpsFix && millis() - gpsLastFixMs < 5000;
   if (fixNu != hadFix) { hadFix = fixNu; logRegel(fixNu ? "GPS: FIX! %d satellieten" : "GPS: fix kwijt (%d sat)", (int)gpsSatellites); }
   if (millis() - logStatusMs > 60000) {
     logStatusMs = millis(); int hh = -1, mm = 0; if (klokOk()) lokaleTijd(hh, mm);
     bool ok; float v = spdNow(&ok);
     logRegel("status | klok %02d:%02d | gps %s sat=%d zicht=%d nmea=%lu/%lu | %.0f km/u | tel %s | heap %u | licht %d | fps %.0f teken %.1fms stuur %.1fms%s",
       hh, mm, fixNu ? (posVanTel ? "TEL" : "FIX") : "zoekt", (int)gpsSatellites, (int)gpsInZicht, (unsigned long)nmeaOk, (unsigned long)nmeaBad, v,
       bleConnected ? (phoneFresh() ? "data" : "stil") : "nee", (unsigned)ESP.getFreeHeap(), (int)huidigLicht, laatsteFps, laatsteDrawMs, laatsteFlushMs, slaapt ? " | SLAAPT" : "");
   }
   if (Serial.available()) {
     static char cmd[16]; static uint8_t cl = 0; char c = Serial.read();
     if (c == '\n' || c == '\r') { cmd[cl] = 0; if (!strcmp(cmd, "log")) logDump(Serial); else if (!strcmp(cmd, "wis") && logFs) { logFs->remove(LOGF); logFs->remove(LOGOUD); Serial.println("Logboek gewist."); } cl = 0; }
     else if (cl < sizeof cmd - 1) cmd[cl++] = c;
   }
   if (netNieuw) { netNieuw = false; wCfg = wNieuw; buddyPrefs.putString("wurl", wCfg.url); buddyPrefs.putString("grp", wCfg.grp); buddyPrefs.putString("van", wCfg.van); buddyPrefs.putString("naam", wCfg.naam); logRegel("groep/worker ingesteld (%s)", wCfg.naam); }
   if (setWarn >= 0) { waarschuwAan = setWarn == 1; buddyPrefs.putBool("warn", waarschuwAan); setWarn = -1; }
   if (setLicht >= 0) { helderheid = (uint8_t)constrain(setLicht * 255 / 100, 20, 255); buddyPrefs.putUChar("licht", helderheid); setLicht = -1; }
   if (setVol >= 0) { buddyVolume = (uint8_t)constrain((int)setVol, 0, 100); if (audioReady) applyCodecVolume(buddyVolume); buddyPrefs.putUChar("volume", buddyVolume); setVol = -1; }
   if (wifiNieuw) { wifiNieuw = false; strcpy(wifiNaam, wifiNaamNieuw); strcpy(wifiPass, wifiPassNieuw); buddyPrefs.putString("ssid", wifiNaam); buddyPrefs.putString("pass", wifiPass); wifiHerstart = true; logRegel("wifi ingesteld: %s", wifiNaam); }
   if (logVraag) { logVraag = false; if (!logBezig) { logBezig = true; xTaskCreatePinnedToCore(logNaarTelefoonTaak, "BuddyLogBLE", 4096, nullptr, 1, nullptr, 0); } }
 }
 if(millis()-serialHeartbeatMs>=3000){
   serialHeartbeatMs=millis();
   Serial.printf("BUDDY v%d ALIVE | scherm=OK | GPS=%s | heap=%u\n",FW_VERSIE,gpsDataOk?gpsLastStage:"GEEN DATA",(unsigned)ESP.getFreeHeap());
 }
 uint32_t nowUs=micros();
 if(lastRenderUs && (uint32_t)(nowUs-lastRenderUs)<(slaapt ? 100000u : FRAME_INTERVAL_US)){   // v41: in slaapstand maar 10 fps (scheelt stroom en warmte)
   if(volumeNeedsSave && (uint32_t)(millis()-lastVolumeChange)>700){
     buddyPrefs.putUChar("volume", buddyVolume); volumeNeedsSave=false;
   }
   delay(1); return;
 }
 float dt=lastRenderUs ? fminf(0.10f,(uint32_t)(nowUs-lastRenderUs)/1000000.0f) : 1.0f/30.0f;
 lastRenderUs=nowUs; t+=dt;
 { static uint32_t volT = 0; if (millis() - volT > 8000) { volT = millis(); drawnBox[0] = drawnBox[1] = prevBox = {0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1}; } }  // v28: af en toe alles wissen en opnieuw versturen
 { // v47: robotmodus: 20 s stilstaan (en geen trajectcontrole/update) -> mini-robotje; zodra je rijdt is alles terug
   static uint32_t stilSinds = 0; float vr = spdNow(); uint32_t nu = millis();
   if (vr < 1.5f) { if (!stilSinds) stilSinds = nu; } else stilSinds = 0;
   bool r = stilSinds && nu - stilSinds > 20000 && !trajActief && otaPct < 0;
   if (r != robotModus) { robotModus = r; drawnBox[0] = drawnBox[1] = prevBox = {0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1}; if (r && gagAct < 0 && nextGagT > t + 10) nextGagT = t + 8; }
   if (robotModus && bleConnected) laatsteActie = nu;   // telefoon erbij (bv. op tafel): wakker blijven en spelen
 }
 imuStep(dt); gagStep(dt); driveStep(dt); slaapStap(); persStap(dt); robotStap(dt);
 { // v39: te hard, collega in de buurt, wisselbeker
   static uint32_t hardSinds = 0, hardT = 0; uint32_t nu = millis();
   if (waarschuwAan && hudLimiet > 0 && hudSnelheid > hudLimiet + max(5, hudLimiet / 10)) {
     if (!hardSinds) hardSinds = nu;
     if (nu - hardSinds > 4000 && (!hardT || nu - hardT > 60000)) { hardT = nu; startGag(39); playMoodSound(ALERT); logRegel("te hard: %d bij %d", hudSnelheid, hudLimiet); }
   } else hardSinds = 0;
   { // v40: flink optrekken (>= 12 km/u erbij in 1,5 s) -> ouderwetse snelheidsmeter
     static uint32_t vT = 0; static float vH[4] = {0, 0, 0, 0}; float vn = spdNow();
     if (nu - vT >= 500) { vT = nu; vH[0] = vH[1]; vH[1] = vH[2]; vH[2] = vH[3]; vH[3] = vn; }
     if (vn > 8 && vH[3] - vH[0] >= 12 && !menuOpen && !venster && (int32_t)(showTot - nu) <= 0) {
       if ((int32_t)(meterTot - nu) > 0) { meterTot = nu + 2500; if (meterTot < meterStart + 6000) meterTot = meterStart + 6000; }
       else { meterStart = nu; naald = 0; naaldV = 0; meterTot = nu + 6000; playGeluid(GL_VROEM); logRegel("retro meter: optrekken %.0f -> %.0f", vH[0], vH[3]); }   // v57: de vuurwijzer komt altijd
       { static uint32_t losShowT = 0; if (meterStart == nu && !showNa && random(4) == 0 && (!losShowT || nu - losShowT > 600000UL)) { losShowT = nu; showSoort = 1 + sprintI % 4; showNa = nu + 6500; sprintI++; buddyPrefs.putUChar("spri", sprintI); } }   // v57: af en toe ook na gewoon flink optrekken
     }
     if (showNa && (int32_t)(nu - showNa) >= 0) {   // v56: na de vlag de sprint-show
       static const uint16_t DUUR[7] = {0, 9500, 7000, 7500, 8000, 6000, 5000};
       showNa = 0; showStart = nu; showTot = nu + DUUR[showSoort]; playGeluid(showSoort == 5 ? GL_VROEM : showSoort == 6 ? GL_GIECHEL : GL_RUIMTE + showSoort - 1); logRegel("sprint-show %d", (int)showSoort);
     }
     float doel = vn; naaldV += ((doel - naald) * 55.f - naaldV * 6.5f) * dt; naald += naaldV * dt;   // veer + demper: schiet door en trilt na
     if (naald < 0) { naald = 0; naaldV = 0; }
   }
   if (buurNieuw) { buurNieuw = false; startGag(22); gagMood = EXCITED; gagMsg = buurTxt; playMoodSound(EXCITED); }
   if (phoneFresh() && phone.beker && !bekerGetoond && gagAct < 0 && !menuOpen && !venster) { bekerGetoond = true; startGag(38); playMoodSound(EXCITED); }
 }
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
   laatsteFps=fps; laatsteDrawMs=avgDrawMs; laatsteFlushMs=avgFlushMs;
   Serial.printf("Buddy render: %.1f FPS | draw=%.1f ms | flush=%.1f ms | framebuffer=16bit+dirty | heap=%u | PSRAM=%u\n",fps,avgDrawMs,avgFlushMs,(unsigned)ESP.getFreeHeap(),(unsigned)ESP.getFreePsram());
   fpsFrames=0; fpsWindowUs=nowUs; drawAccumUs=0; flushAccumUs=0;
 }
 if(volumeNeedsSave && (uint32_t)(millis()-lastVolumeChange)>700){
   buddyPrefs.putUChar("volume", buddyVolume); volumeNeedsSave=false;
 }
}
