#pragma once
#include "Display.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// نمایشگر روی یک OLED کوچک I2C از نوع SSD1306 (۱۲۸x۶۴ پیکسل، آدرس معمولاً 0x3C) --
// ارزان‌ترین و رایج‌ترین گزینه، حدود ۲۰-۵۰ هزار تومان.
//
// سیم‌کشی (I2C پیش‌فرض ESP32 -- به هیچ پین دیگری توی این پروژه (دکمه‌ها،
// GPS UART، حس‌گر ولتاژ) نمی‌خورد):
//   OLED VCC -> 3.3V
//   OLED GND -> GND
//   OLED SDA -> GPIO21
//   OLED SCL -> GPIO22
//
// کتابخانه‌های لازم (Arduino Library Manager):
//   - "Adafruit GFX Library"
//   - "Adafruit SSD1306"
//
// اگر آدرس ماژول شما 0x3C نیست (بعضی برد‌ها 0x3D هستند)، مقدار OLED_I2C_ADDRESS
// را پایین‌تر عوض کنید.
#define OLED_I2C_ADDRESS 0x3C

class Esp32Display : public Display {
    Adafruit_SSD1306 oled;
    unsigned long messageUntil = 0;
    static const unsigned long MESSAGE_DURATION_MS = 1500;

public:
    Esp32Display() : oled(128, 64, &Wire, -1) {}

    void begin() override {
        if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDRESS)) {
            // اگر OLED پیدا نشود (سیم‌کشی اشتباه یا وصل نبودن)، برنامه نباید گیر کند --
            // فقط روی Serial خبر می‌دهیم و سیستم بدون نمایشگر ادامه پیدا می‌کند.
            Serial.println(F("[Esp32Display] OLED not found on I2C bus -- continuing without display."));
            return;
        }
        oled.setTextColor(SSD1306_WHITE);
        oled.clearDisplay();
        oled.setTextSize(1);
        oled.setCursor(0, 0);
        oled.println(F("Taxi Meter"));
        oled.println(F("booting..."));
        oled.display();
    }

    // یک پیام کوتاه (مثلاً "Passenger 2 boarded") چند لحظه به‌جای صفحه‌ی وضعیت
    // نشان داده می‌شود؛ بعدش خودکار به showStatus برمی‌گردد چون تابع اصلیِ loop()
    // هر ۲ ثانیه showStatus را دوباره صدا می‌زند و اینجا چک می‌شود که پیام هنوز
    // فعال است یا نه.
    void showMessage(const char* message) override {
        messageUntil = millis() + MESSAGE_DURATION_MS;
        oled.clearDisplay();
        oled.setTextSize(1);
        oled.setCursor(0, 28);
        oled.println(message);
        oled.display();
    }

    void showStatus(TaxiMeterSystem& system, int hour) override {
        // اگر یک پیام گذرا هنوز باید دیده شود، صفحه‌ی وضعیت را جایگزینش نکن
        if (messageUntil != 0 && millis() < messageUntil) return;
        messageUntil = 0;

        oled.clearDisplay();
        oled.setTextSize(1);
        oled.setCursor(0, 0);

        bool night = system.fareConfig.isNightHour(hour);
        if (hour < 0) oled.print(F("--:--  "));
        else { if (hour < 10) oled.print('0'); oled.print(hour); oled.print(F(":00  ")); }
        oled.print(night ? F("NIGHT") : F("DAY"));
        if (system.lastDistanceSourceWasWheel) oled.print(F(" [W]"));
        oled.println();

        if (system.gpsSignalWeak) {
            oled.println(F("** GPS WEAK **"));
        }

        for (auto& t : system.trips) {
            oled.print('P'); oled.print(t.id + 1); oled.print(F(": "));
            if (t.state == TripState::IDLE) {
                oled.println(F("-- idle --"));
            } else {
                oled.print(t.fare()); oled.print(F("T "));
                oled.print((long)t.totalDistance()); oled.print(F("m"));
                if (t.waitingSeconds > 0) { oled.print(F(" w")); oled.print((long)t.waitingSeconds); oled.print('s'); }
                if (t.baggageCount > 0) { oled.print(F(" bag")); oled.print(t.baggageCount); }
                if (t.state == TripState::FINISHED) oled.print(F(" *"));
                oled.println();
            }
        }

        oled.display();
    }
};
