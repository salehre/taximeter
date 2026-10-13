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
//   Passenger 1 board/alight -> GPIO25       Passenger 1 baggage -> GPIO18
//   Passenger 2 board/alight -> GPIO26       Passenger 2 baggage -> GPIO19
//   Passenger 3 board/alight -> GPIO27       Passenger 3 baggage -> GPIO23
//   باگیج: فشارِ کوتاه = یک واحد بار اضافه می‌شود؛ نگه‌داشتن (بیش از
//   BAGGAGE_LONG_PRESS_MS) = تعداد بارِ همان مسافر صفر می‌شود.
//
// Power-loss sense pin (see README for the voltage divider circuit):
//   12V rail (through a resistor divider down to <=3.3V) -> GPIO34 (ADC-only pin)
//
// Wheel/driveshaft pulse sensor (optional -- for more accurate, GPS-independent
// distance; see Esp32WheelPulseProvider.h for wiring and calibration). Disabled
// by default (WHEEL_SENSOR_INSTALLED = false below) -- until you wire it up and
// calibrate it, the system uses GPS-only distance exactly as before.
//   Hall/reed sensor -> GPIO4 (INPUT_PULLUP, FALLING edge)
//
// Required libraries (Arduino Library Manager):
//   - "TinyGPSPlus" by Mikal Hart   (only needed for real GPS mode)
//   - "Preferences"                 (built into the ESP32 core, no install needed)
//   - WiFi                          (built into the ESP32 core)
//   - "Adafruit GFX Library"        (for the OLED status display)
//   - "Adafruit SSD1306"            (for the OLED status display)
//
// OLED display (SSD1306, 128x64, I2C) -- see Esp32Display.h for wiring:
//   SDA -> GPIO21, SCL -> GPIO22, VCC -> 3.3V, GND -> GND
//
// نمایشگر اختصاصیِ زیرِ صندلیِ هر مسافر (اختیاری، برای فعال کردنش
// PASSENGER_DISPLAYS_INSTALLED پایین‌تر را true کنید). چون سه‌تا OLED ارزان
// همه آدرس ثابت I2C دارند (0x3C)، از یک مالتی‌پلکسر TCA9548A استفاده می‌شود
// (نگاه کنید به Tca9548a.h و Esp32PassengerDisplay.h برای جزئیات):
//   TCA9548A SDA/SCL -> همان GPIO21/GPIO22 (باس مشترک با OLED اصلی، آدرس متفاوت 0x70)
//   OLED مسافر ۱ -> کانال 0 مالتی‌پلکسر    OLED مسافر ۲ -> کانال 1    OLED مسافر ۳ -> کانال 2
//
// چراغ «پر» (هر سه مسافر سوارند، جای خالی نیست):
//   یک LED دیگر (با مقاومت محدودکننده) -> GPIO13
// ===================================================================

#define USE_SIMULATED_GPS   // <-- comment this out once a real GPS module is wired up

#include <WiFi.h>
#include "TaxiMeterSystem.h"
#include "Esp32TimeProvider.h"
#include "StatePersistence.h"
#include "Esp32Display.h"
#include "Esp32WheelPulseProvider.h"
#include "Tca9548a.h"
#include "Esp32PassengerDisplay.h"

#ifdef USE_SIMULATED_GPS
  #include "SimulatedGPSProvider.h"
#else
  #include "Esp32GPSProvider.h"
#endif

// ----- fill in your Wi-Fi so the clock can sync via NTP (needed for day/night rate) -----
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// اختلاف منطقه‌ی زمانی شما با UTC، بر حسب ثانیه (برای تشخیص درستِ ساعتِ شب/روز).
// پیش‌فرض +۳:۳۰ (ایران) است. برای مناطق دیگر عوضش کنید، مثلاً:
//   UTC+3:30 (ایران)      -> 12600
//   UTC+4 (امارات/دبی)     -> 14400
//   UTC+2 (اروپای مرکزی)   -> 7200
//   UTC+0 (لندن، زمستان)   -> 0
const long GMT_OFFSET_SECONDS = 12600;
const int DAYLIGHT_OFFSET_SECONDS = 0; // اگر منطقه‌ی شما ساعت تابستانی/زمستانی دارد

const int BUTTON_PINS[3] = {25, 26, 27};

// دکمه‌ی «بار» هر مسافر -- زیر دکمه‌ی سوار/پیاده همان مسافر. فشارِ کوتاه یک
// واحد بار اضافه می‌کند؛ نگه‌داشتنِ بیش از BAGGAGE_LONG_PRESS_MS، بارِ همان
// مسافر را صفر می‌کند (برای اصلاح اشتباه، بدون نیاز به ریست کل سفر).
const int BAGGAGE_BUTTON_PINS[3] = {18, 19, 23};
const unsigned long BAGGAGE_LONG_PRESS_MS = 800;  // این‌قدر نگه‌داشتن -> ریست
const unsigned long BAGGAGE_DEBOUNCE_MS = 30;     // کمتر از این -> نویز/لرزشِ تماس، نادیده گرفته می‌شود
unsigned long baggagePressStart[3] = {0, 0, 0};   // 0 یعنی الان فشرده نیست
bool baggageLongFired[3] = {false, false, false}; // در همین نگه‌داشتن، ریست قبلاً انجام شده یا نه

// یک LED کوچک بالای دکمه‌ی هر مسافر -- روشن یعنی آن مسافر الان سوار است (ACTIVE)،
// تا راننده بدون نگاه کردن به صفحه‌ی OLED هم بفهمد کدام صندلی‌ها اشغال است.
// هر LED با یک مقاومت محدودکننده‌ی جریان (مثلاً 220 اهم) به GND وصل می‌شود.
const int PASSENGER_LED_PINS[3] = {32, 33, 14};
const unsigned long DEBOUNCE_MS = 250;
unsigned long lastPress[3] = {0, 0, 0};

// نمایشگرهای اختصاصیِ زیر صندلیِ هر مسافر -- اختیاری. تا وقتی false است،
// سیستم فقط از همان یک OLED مشترک (Esp32Display) استفاده می‌کند، دقیقاً مثل
// قبل. بعد از سیم‌کشیِ مالتی‌پلکسر و سه‌تا OLED (بالا توضیح داده شده)، این را
// true کنید.
const bool PASSENGER_DISPLAYS_INSTALLED = false;

// چراغ «پر» -- وقتی هر سه مسافر هم‌زمان سوارند (جای خالی نیست) روشن می‌شود.
const int FULL_LED_PIN = 13;

// تشخیص قطع برق ماشین -- اختیاری. تا وقتی مدار تقسیم‌ولتاژ ۱۲ ولت را به
// GPIO34 وصل نکرده‌اید، این را false نگه دارید؛ وگرنه پین آزاد است، همیشه
// «ولتاژ پایین» می‌خواند و سیستم مدام فکر می‌کند برق قطع شده.
const bool POWER_SENSE_INSTALLED = false;
const int POWER_SENSE_PIN = 34;       // ADC pin reading the (divided-down) 12V car rail
const int POWER_LOST_ADC_THRESHOLD = 1200; // tune this to your divider ratio; see README

// حسگر پالسی چرخ -- اختیاری. تا وقتی WHEEL_SENSOR_INSTALLED false است، سیستم
// دقیقاً مثل قبل فقط از GPS استفاده می‌کند. بعد از نصب فیزیکی حسگر و کالیبراسیون
// (روش کالیبراسیون در Esp32WheelPulseProvider.h توضیح داده شده)، این دو مقدار
// را با اعداد واقعی خودتان جایگزین کنید.
const bool WHEEL_SENSOR_INSTALLED = false;
const double WHEEL_PULSES_PER_METER = 8.0; // مقدار پیش‌فرض/نمونه -- حتماً کالیبره کنید

FareConfig fareConfig(
  /*unitMeters=*/50.0,
  /*dayPricePerUnit=*/500,
  /*nightPricePerUnit=*/750,
  /*baseFare=*/15000,
  /*nightStartHour=*/22,
  /*nightEndHour=*/6
  // waitingUnitSeconds/waitingChargePerUnit/minMovingSpeedMps از مقدار پیش‌فرض
  // در FareConfig.h استفاده می‌کنند (هر ۶۰ ثانیه‌ی سکون = ۳۰۰۰ تومان، آستانه‌ی
  // حرکت = ۵ کیلومتر بر ساعت) -- اگر می‌خواهید عوضشان کنید، اینجا صریحاً بدهید.
);
TaxiMeterSystem system_(fareConfig, /*slotCount=*/3);
Esp32WheelPulseProvider wheelProvider(WHEEL_PULSES_PER_METER, WHEEL_SENSOR_INSTALLED);

Esp32TimeProvider clock_;
StatePersistence storage;
Esp32Display display;

// نمایشگرهای اختصاصیِ هر مسافر (پشتِ مالتی‌پلکسر) -- فقط وقتی
// PASSENGER_DISPLAYS_INSTALLED بالا true باشد واقعاً استفاده می‌شوند؛ در غیر
// این صورت begin()شان چیزی پیدا نمی‌کند، پیام می‌دهد و بی‌خطر ادامه می‌دهد.
Tca9548a passengerMux;
Esp32PassengerDisplay passengerDisplay0(passengerMux, 0);
Esp32PassengerDisplay passengerDisplay1(passengerMux, 1);
Esp32PassengerDisplay passengerDisplay2(passengerMux, 2);
PassengerDisplay* passengerDisplays[3] = {&passengerDisplay0, &passengerDisplay1, &passengerDisplay2};

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

// پیش‌اعلان‌ها: Arduino IDE این‌ها را خودش خودکار تولید می‌کند، ولی صریح نوشتنشان
// باعث می‌شود این فایل با هر کامپایلر استاندارد C++ (نه فقط Arduino) هم درست
// کامپایل شود -- مثلاً همان محیط شبیه‌سازی‌شده‌ای که برای تست این پروژه استفاده کردیم.
void printStatus();
void printDailyHistory();
void handleButton(int slot);
void handleBaggagePress(int slot);
void handleBaggageReset(int slot);
void showPassengerMessage(int slot, const char* msg);

// LED بالای هر مسافر را با وضعیت واقعی آن هماهنگ نگه می‌دارد -- روشن فقط وقتی
// آن مسافر ACTIVE باشد (سوار و در حال سفر)، نه IDLE و نه FINISHED (چون FINISHED
// یعنی مسافر پیاده شده، صندلی عملاً خالی است حتی اگر هنوز ریست نشده باشد).
// یک پیام گذرا فقط روی نمایشگر اختصاصیِ همان یک مسافر (اگر نصب باشد).
void showPassengerMessage(int slot, const char* msg) {
  if (PASSENGER_DISPLAYS_INSTALLED) passengerDisplays[slot]->showMessage(msg);
}

void updatePassengerLeds() {
  bool allActive = true;
  for (int i = 0; i < 3; i++) {
    bool active = (system_.trips[i].state == TripState::ACTIVE);
    digitalWrite(PASSENGER_LED_PINS[i], active ? HIGH : LOW);
    allActive = allActive && active;
  }
  // چراغ «پر»: فقط وقتی هر سه صندلی هم‌زمان ACTIVE‌اند، یعنی جای خالی نداریم.
  digitalWrite(FULL_LED_PIN, allActive ? HIGH : LOW);
}

void printStatus() {
  int hour = clock_.currentHour();
  Serial.println(F("-----------------------------------------"));
  Serial.print("Clock hour: "); Serial.print(hour);
  Serial.print(hour < 0 ? " (not synced yet, using day rate)" : (fareConfig.isNightHour(hour) ? " (NIGHT rate)" : " (day rate)"));
  Serial.print(" | GPS signal: "); Serial.print(system_.gpsSignalWeak ? "WEAK/LOST" : "ok");
  Serial.print(" | wheel sensor: "); Serial.print(WHEEL_SENSOR_INSTALLED ? "installed" : "not installed");
  Serial.print(" | last segment source: "); Serial.println(system_.lastDistanceSourceWasWheel ? "wheel" : "GPS");
  for (auto& t : system_.trips) {
    Serial.print("Passenger "); Serial.print(t.id + 1);
    Serial.print(" | "); Serial.print(stateLabel(t.state));
    Serial.print(" | day: "); Serial.print(t.dayDistance, 1); Serial.print("m");
    Serial.print(" night: "); Serial.print(t.nightDistance, 1); Serial.print("m");
    Serial.print(" waiting: "); Serial.print((long)t.waitingSeconds); Serial.print("s");
    if (t.baggageCount > 0) { Serial.print(" baggage: "); Serial.print(t.baggageCount); }
    Serial.print(" | fare: "); Serial.print(t.fare()); Serial.println(" toman");
  }
  Serial.println(F("-----------------------------------------"));
  display.showStatus(system_, hour);

  if (PASSENGER_DISPLAYS_INSTALLED) {
    bool night = fareConfig.isNightHour(hour);
    for (int i = 0; i < 3; i++) {
      passengerDisplays[i]->showTrip(system_.trips[i], hour, night, system_.gpsSignalWeak);
    }
  }
}

// ===================================================================
// تست بدون هیچ دکمه‌ای: همین دستورات دقیقاً مثل desktop_test/main.cpp از طریق
// Serial Monitor (نرخ ۱۱۵۲۰۰) قابل استفاده‌اند -- یعنی با فقط یک برد ESP32 و یک
// کابل USB، بدون خرید حتی یک دکمه، می‌شود کل رفتار سیستم را تست کرد:
//   s0 / s1 / s2   -> مسافر ۱/۲/۳ سوار شد
//   e0 / e1 / e2   -> مسافر ۱/۲/۳ پیاده شد
//   r0 / r1 / r2   -> ریست آن صندلی
//   b0 / b1 / b2   -> یک واحد بار برای آن مسافر (شبیه‌سازی فشارِ کوتاهِ دکمه‌ی بار)
//   k0 / k1 / k2   -> صفر کردن بارِ آن مسافر (شبیه‌سازی نگه‌داشتنِ دکمه‌ی بار)
//   hist           -> نمایش تاریخچه‌ی امروز
//   status         -> نمایش فوری وضعیت (به‌جای صبر کردن ۲ ثانیه)
// ===================================================================
void handleSerialCommands() {
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  if (cmd.length() == 0) return;

  if (cmd == "hist") {
    printDailyHistory();
  } else if (cmd == "status") {
    printStatus();
  } else if (cmd.length() == 2 && (cmd[0] == 's' || cmd[0] == 'e' || cmd[0] == 'r')) {
    int slot = cmd[1] - '0';
    if (slot < 0 || slot > 2) {
      Serial.println(F("Invalid passenger number (0 to 2)."));
      return;
    }
    Trip& trip = system_.trips[slot];
    bool matches = (cmd[0] == 's' && trip.state == TripState::IDLE) ||
                   (cmd[0] == 'e' && trip.state == TripState::ACTIVE) ||
                   (cmd[0] == 'r' && trip.state == TripState::FINISHED);
    if (!matches) {
      Serial.println(F("Ignored: that command doesn't match this passenger's current state (see 'status')."));
      return;
    }
    handleButton(slot); // دقیقاً همان تابعی که با فشردن دکمه‌ی فیزیکی صدا زده می‌شود
  } else if (cmd.length() == 2 && (cmd[0] == 'b' || cmd[0] == 'k')) {
    int slot = cmd[1] - '0';
    if (slot < 0 || slot > 2) {
      Serial.println(F("Invalid passenger number (0 to 2)."));
      return;
    }
    if (cmd[0] == 'b') handleBaggagePress(slot);
    else handleBaggageReset(slot);
  } else {
    Serial.println(F("Unknown command. Try: s0, e0, r0, b0, k0, hist, status"));
  }
}

void printDailyHistory() {
  auto summary = system_.todaysSummary();
  Serial.println(F("=== Today's history ==="));
  for (auto& r : system_.todaysRecords()) {
    Serial.print("  passenger "); Serial.print(r.passengerSlot + 1);
    Serial.print(" | "); Serial.print((long)r.distance); Serial.print("m");
    Serial.print(" | waiting: "); Serial.print((long)r.waitingSeconds); Serial.print("s");
    if (r.baggageCount > 0) { Serial.print(" | baggage: "); Serial.print(r.baggageCount); }
    Serial.print(" | "); Serial.print(r.fare); Serial.println(" toman");
  }
  Serial.print("  TOTAL: "); Serial.print(summary.tripCount); Serial.print(" trips, ");
  Serial.print(summary.totalFare); Serial.println(" toman");
}

void handleButton(int slot) {
  Trip& trip = system_.trips[slot];
  char msg[40];
  if (trip.state == TripState::IDLE) {
    system_.startTrip(slot);
    snprintf(msg, sizeof(msg), "Passenger %d boarded", slot + 1);
    Serial.println(msg);
  } else if (trip.state == TripState::ACTIVE) {
    system_.stopTrip(slot);
    snprintf(msg, sizeof(msg), "Passenger %d alighted", slot + 1);
    Serial.println(msg);
    printDailyHistory();
  } else {
    system_.resetTrip(slot);
    snprintf(msg, sizeof(msg), "Passenger %d slot reset", slot + 1);
    Serial.println(msg);
  }
  display.showMessage(msg);
  showPassengerMessage(slot, msg);
  printStatus();
}

// دکمه‌ی «بار»: فشارِ کوتاه -> یک واحد اضافه می‌شود. فقط وقتی آن مسافر
// ACTIVE باشد اثر دارد (خودِ Trip::addBaggage هم این را چک می‌کند).
void handleBaggagePress(int slot) {
  Trip& trip = system_.trips[slot];
  if (trip.state != TripState::ACTIVE) return; // مسافری سوار نیست، فشار را نادیده بگیر
  system_.addBaggage(slot);
  char msg[40];
  snprintf(msg, sizeof(msg), "P%d baggage +1 (now %d)", slot + 1, trip.baggageCount);
  Serial.println(msg);
  display.showMessage(msg);
  showPassengerMessage(slot, msg);
  printStatus();
}

// نگه‌داشتنِ دکمه‌ی «بار»: تعداد بارِ همان مسافر صفر می‌شود.
void handleBaggageReset(int slot) {
  Trip& trip = system_.trips[slot];
  if (trip.state != TripState::ACTIVE) return;
  system_.resetBaggage(slot);
  char msg[40];
  snprintf(msg, sizeof(msg), "P%d baggage reset to 0", slot + 1);
  Serial.println(msg);
  display.showMessage(msg);
  showPassengerMessage(slot, msg);
  printStatus();
}

// وقتی برق ماشین (12 ولت) قطع بشه، این تابع صدا زده می‌شود.
// باتری پشتیبان دقیقاً برای این چند ثانیه لازم است: تا ذخیره تمام بشود.
void handlePowerLoss() {
  if (powerLossHandled) return; // فقط یک‌بار انجام بده
  powerLossHandled = true;
  Serial.println(F("!!! Car power lost -- saving active trips to flash !!!"));
  display.showMessage("POWER LOST - saving...");
  for (int i = 0; i < 3; i++) showPassengerMessage(i, "POWER LOST - saving...");
  storage.saveActiveTrips(system_);
  Serial.println(F("Saved. Running on backup battery until power returns or battery runs out."));
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println(F("=== Taxi Meter booting ==="));

  for (int i = 0; i < 3; i++) pinMode(BUTTON_PINS[i], INPUT_PULLUP);
  for (int i = 0; i < 3; i++) pinMode(BAGGAGE_BUTTON_PINS[i], INPUT_PULLUP);
  for (int i = 0; i < 3; i++) pinMode(PASSENGER_LED_PINS[i], OUTPUT);
  pinMode(FULL_LED_PIN, OUTPUT);
  pinMode(POWER_SENSE_PIN, INPUT);

  display.begin();
  if (PASSENGER_DISPLAYS_INSTALLED) {
    for (int i = 0; i < 3; i++) passengerDisplays[i]->begin();
  }

  // Wi-Fi + NTP time sync (needed for accurate day/night switching and trip history dates)
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 10000) {
    delay(300); Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" connected.");
    clock_.begin(GMT_OFFSET_SECONDS, DAYLIGHT_OFFSET_SECONDS); // NTP sync
  } else {
    Serial.println(" NOT connected -- clock will be unsynced, day rate used by default.");
  }
  system_.setTimeProvider(&clock_);

  system_.setGPSProvider(&gpsProvider);
  wheelProvider.begin(); // بی‌اثر است اگر WHEEL_SENSOR_INSTALLED پایین صفحه false باشد
  system_.setWheelPulseProvider(&wheelProvider);
  system_.begin();

  system_.onGpsWarningChange = [](bool weak) {
    Serial.println(weak ? "[WARNING] GPS signal lost/weak!" : "[OK] GPS signal restored.");
  };

  // هر بار یک سفر تمام شود (نه فقط لحظه‌ی قطع برق)، همان یک رکورد فوراً روی
  // فلش نوشته می‌شود -- پس تاریخچه‌ی سفرهای قبلیِ امروز همیشه از قبل امن است.
  system_.onTripFinished = [](const TripRecord& rec) {
    storage.appendHistoryRecord(rec);
  };

  // اگر قبل از این ری‌استارت به‌خاطر قطع برق بوده، سفرهای در حال انجام را بازیابی کن
  if (storage.restoreActiveTrips(system_)) {
    Serial.println(F("Recovered in-progress trip(s) after power interruption."));
    storage.clearSavedFlag();
  }

  // تاریخچه‌ی سفرهای تمام‌شده‌ی امروز را از فلش بازیابی کن (اگر مال همین روز باشد)
  if (storage.loadTodaysHistory(system_)) {
    Serial.print(F("Recovered today's trip history: "));
    Serial.print(system_.history.size());
    Serial.println(F(" trip(s)."));
  }

  Serial.println(F("Ready."));
  printStatus();
}

void loop() {
  system_.loop(millis());
  updatePassengerLeds();
  handleSerialCommands();

  // --- تشخیص قطع برق ماشین ---
  if (POWER_SENSE_INSTALLED) {
    int powerReading = analogRead(POWER_SENSE_PIN);
    if (powerReading < POWER_LOST_ADC_THRESHOLD) {
      handlePowerLoss();
    } else {
      powerLossHandled = false; // برق برگشت، دوباره آماده‌ی تشخیص قطعی بعدی باش
    }
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

  // --- خواندن دکمه‌های «بار» (فشارِ کوتاه = اضافه، نگه‌داشتن = ریست) ---
  for (int i = 0; i < 3; i++) {
    bool pressed = (digitalRead(BAGGAGE_BUTTON_PINS[i]) == LOW);
    unsigned long now = millis();
    if (pressed) {
      if (baggagePressStart[i] == 0) {
        baggagePressStart[i] = now; // تازه فشرده شد
        baggageLongFired[i] = false;
      } else if (!baggageLongFired[i] && (now - baggagePressStart[i] >= BAGGAGE_LONG_PRESS_MS)) {
        baggageLongFired[i] = true; // فقط یک‌بار در همین نگه‌داشتن
        handleBaggageReset(i);
      }
    } else {
      if (baggagePressStart[i] != 0 && !baggageLongFired[i] &&
          (now - baggagePressStart[i] >= BAGGAGE_DEBOUNCE_MS)) {
        // قبل از رسیدن به آستانه‌ی نگه‌داشتن رها شد -> فشارِ کوتاه بود
        handleBaggagePress(i);
      }
      baggagePressStart[i] = 0;
      baggageLongFired[i] = false;
    }
  }

  if (millis() - lastPrint > 2000) {
    lastPrint = millis();
    printStatus();
  }
}
