// ===================================================================
// Taxi Meter on ESP32 -- 3 concurrent passengers, shared GPS,
// base fare, day/night rate, daily trip history, weak-GPS warning,
// and power-loss state saving (for use with a backup battery).
// ===================================================================
//
// Test mode (no GPS module needed) vs real mode:
//   #define USE_SIMULATED_GPS   -> fake movement, works on bare ESP32 or in Wokwi
//   comment it out               -> reads a real GPS module (e.g. NEO-6M) over UART
//
// Wiring (real GPS mode):
//   GPS module TX -> ESP32 GPIO16 (RX2)
//   GPS module RX -> ESP32 GPIO17 (TX2)
//   GPS module VCC -> 3.3V or 5V (depends on module)
//   GPS module GND -> GND
//
// Buttons (one leg to pin, other to GND, using INPUT_PULLUP):
//   Passenger 1 -> GPIO25
//   Passenger 2 -> GPIO26
//   Passenger 3 -> GPIO27
//
// Power-loss sense pin (see README for the voltage divider circuit):
//   12V rail (through a resistor divider down to <=3.3V) -> GPIO34 (ADC-only pin)
//
// Required libraries (Arduino Library Manager):
//   - "TinyGPSPlus" by Mikal Hart   (only needed for real GPS mode)
//   - "Preferences"                 (built into the ESP32 core, no install needed)
//   - WiFi                          (built into the ESP32 core)
// ===================================================================

#define USE_SIMULATED_GPS   // <-- comment this out once a real GPS module is wired up

#include <WiFi.h>
#include "TaxiMeterSystem.h"
#include "Esp32TimeProvider.h"
#include "StatePersistence.h"

#ifdef USE_SIMULATED_GPS
  #include "SimulatedGPSProvider.h"
#else
  #include "Esp32GPSProvider.h"
#endif

// ----- fill in your Wi-Fi so the clock can sync via NTP (needed for day/night rate) -----
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

const int BUTTON_PINS[3] = {25, 26, 27};
const unsigned long DEBOUNCE_MS = 250;
unsigned long lastPress[3] = {0, 0, 0};

const int POWER_SENSE_PIN = 34;       // ADC pin reading the (divided-down) 12V car rail
const int POWER_LOST_ADC_THRESHOLD = 1200; // tune this to your divider ratio; see README

FareConfig fareConfig(
  /*unitMeters=*/50.0,
  /*dayPricePerUnit=*/500,
  /*nightPricePerUnit=*/750,
  /*baseFare=*/15000,
  /*nightStartHour=*/22,
  /*nightEndHour=*/6
);
TaxiMeterSystem system_(fareConfig, /*slotCount=*/3);

Esp32TimeProvider clock_;
StatePersistence storage;

#ifdef USE_SIMULATED_GPS
SimulatedGPSProvider gpsProvider;
#else
Esp32GPSProvider gpsProvider(Serial2, /*rx=*/16, /*tx=*/17, /*baud=*/9600);
#endif

unsigned long lastPrint = 0;
bool powerLossHandled = false;

const char* stateLabel(TripState s) {
  switch (s) {
    case TripState::IDLE:     return "IDLE";
    case TripState::ACTIVE:   return "ACTIVE";
    case TripState::FINISHED: return "FINISHED";
  }
  return "?";
}

void printStatus() {
  int hour = clock_.currentHour();
  Serial.println(F("-----------------------------------------"));
  Serial.print("Clock hour: "); Serial.print(hour);
  Serial.print(hour < 0 ? " (not synced yet, using day rate)" : (fareConfig.isNightHour(hour) ? " (NIGHT rate)" : " (day rate)"));
  Serial.print(" | GPS signal: "); Serial.println(system_.gpsSignalWeak ? "WEAK/LOST" : "ok");
  for (auto& t : system_.trips) {
    Serial.print("Passenger "); Serial.print(t.id + 1);
    Serial.print(" | "); Serial.print(stateLabel(t.state));
    Serial.print(" | day: "); Serial.print(t.dayDistance, 1); Serial.print("m");
    Serial.print(" night: "); Serial.print(t.nightDistance, 1); Serial.print("m");
    Serial.print(" | fare: "); Serial.print(t.fare()); Serial.println(" toman");
  }
  Serial.println(F("-----------------------------------------"));
}

void printDailyHistory() {
  auto summary = system_.todaysSummary();
  Serial.println(F("=== Today's history ==="));
  for (auto& r : system_.todaysRecords()) {
    Serial.print("  passenger "); Serial.print(r.passengerSlot + 1);
    Serial.print(" | "); Serial.print((long)r.distance); Serial.print("m");
    Serial.print(" | "); Serial.print(r.fare); Serial.println(" toman");
  }
  Serial.print("  TOTAL: "); Serial.print(summary.tripCount); Serial.print(" trips, ");
  Serial.print(summary.totalFare); Serial.println(" toman");
}

void handleButton(int slot) {
  Trip& trip = system_.trips[slot];
  if (trip.state == TripState::IDLE) {
    system_.startTrip(slot);
    Serial.print("Passenger "); Serial.print(slot + 1); Serial.println(" boarded.");
  } else if (trip.state == TripState::ACTIVE) {
    system_.stopTrip(slot);
    Serial.print("Passenger "); Serial.print(slot + 1); Serial.println(" alighted.");
    printDailyHistory();
  } else {
    system_.resetTrip(slot);
    Serial.print("Passenger "); Serial.print(slot + 1); Serial.println(" slot reset.");
  }
  printStatus();
}

// وقتی برق ماشین (12 ولت) قطع بشه، این تابع صدا زده می‌شود.
// باتری پشتیبان دقیقاً برای این چند ثانیه لازم است: تا ذخیره تمام بشود.
void handlePowerLoss() {
  if (powerLossHandled) return; // فقط یک‌بار انجام بده
  powerLossHandled = true;
  Serial.println(F("!!! Car power lost -- saving active trips to flash !!!"));
  storage.saveActiveTrips(system_);
  Serial.println(F("Saved. Running on backup battery until power returns or battery runs out."));
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println(F("=== Taxi Meter booting ==="));

  for (int i = 0; i < 3; i++) pinMode(BUTTON_PINS[i], INPUT_PULLUP);
  pinMode(POWER_SENSE_PIN, INPUT);

  // Wi-Fi + NTP time sync (needed for accurate day/night switching and trip history dates)
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 10000) {
    delay(300); Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" connected.");
    clock_.begin(); // NTP sync
  } else {
    Serial.println(" NOT connected -- clock will be unsynced, day rate used by default.");
  }
  system_.setTimeProvider(&clock_);

  system_.setGPSProvider(&gpsProvider);
  system_.begin();

  system_.onGpsWarningChange = [](bool weak) {
    Serial.println(weak ? "[WARNING] GPS signal lost/weak!" : "[OK] GPS signal restored.");
  };

  // اگر قبل از این ری‌استارت به‌خاطر قطع برق بوده، سفرهای در حال انجام را بازیابی کن
  if (storage.restoreActiveTrips(system_)) {
    Serial.println(F("Recovered in-progress trip(s) after power interruption."));
    storage.clearSavedFlag();
  }

  Serial.println(F("Ready."));
  printStatus();
}

void loop() {
  system_.loop(millis());

  // --- تشخیص قطع برق ماشین ---
  int powerReading = analogRead(POWER_SENSE_PIN);
  if (powerReading < POWER_LOST_ADC_THRESHOLD) {
    handlePowerLoss();
  } else {
    powerLossHandled = false; // برق برگشت، دوباره آماده‌ی تشخیص قطعی بعدی باش
  }

  // --- خواندن دکمه‌ها ---
  for (int i = 0; i < 3; i++) {
    if (digitalRead(BUTTON_PINS[i]) == LOW) {
      unsigned long now = millis();
      if (now - lastPress[i] > DEBOUNCE_MS) {
        lastPress[i] = now;
        handleButton(i);
      }
    }
  }

  if (millis() - lastPrint > 2000) {
    lastPrint = millis();
    printStatus();
  }
}
