#pragma once
#include "PassengerDisplay.h"
#include "Tca9548a.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// نمایشگر OLED کوچکِ اختصاصیِ یک مسافر، پشتِ یک کانالِ مالتی‌پلکسر TCA9548A
// (نگاه کنید به Tca9548a.h). قبل از هر ارتباط با OLED، کانالِ خودش را روی
// مالتی‌پلکسر انتخاب می‌کند تا با OLEDهای هم‌آدرسِ مسافرهای دیگر تداخل نکند.
class Esp32PassengerDisplay : public PassengerDisplay {
    Tca9548a& mux;
    uint8_t channel;
    Adafruit_SSD1306 oled;
    unsigned long messageUntil = 0;
    static const unsigned long MESSAGE_DURATION_MS = 1500;
    static const uint8_t OLED_I2C_ADDRESS = 0x3C;

public:
    Esp32PassengerDisplay(Tca9548a& m, uint8_t ch) : mux(m), channel(ch), oled(128, 64, &Wire, -1) {}

    void begin() override {
        mux.selectChannel(channel);
        if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDRESS)) {
            Serial.print(F("[Esp32PassengerDisplay] OLED on mux channel "));
            Serial.print(channel);
            Serial.println(F(" not found -- continuing without it."));
            return;
        }
        oled.setTextColor(SSD1306_WHITE);
        oled.clearDisplay();
        oled.setTextSize(1);
        oled.setCursor(0, 0);
        oled.print(F("Passenger ")); oled.println(channel + 1);
        oled.println(F("waiting..."));
        oled.display();
    }

    void showMessage(const char* message) override {
        mux.selectChannel(channel);
        messageUntil = millis() + MESSAGE_DURATION_MS;
        oled.clearDisplay();
        oled.setTextSize(1);
        oled.setCursor(0, 28);
        oled.println(message);
        oled.display();
    }

    void showTrip(const Trip& t, int hour, bool isNight, bool gpsWeak) override {
        mux.selectChannel(channel);

        // اگر یک پیام گذرا هنوز باید دیده شود، صفحه را جایگزینش نکن
        if (messageUntil != 0 && millis() < messageUntil) return;
        messageUntil = 0;

        oled.clearDisplay();
        oled.setTextSize(1);
        oled.setCursor(0, 0);
        oled.print(F("P")); oled.print(t.id + 1); oled.print(F("   "));
        if (hour < 0) oled.print(F("--:--"));
        else { if (hour < 10) oled.print('0'); oled.print(hour); oled.print(F(":00")); }
        oled.println(isNight ? F(" NGT") : F(" DAY"));
        if (gpsWeak) oled.println(F("** GPS WEAK **"));

        if (t.state == TripState::IDLE) {
            oled.setTextSize(2);
            oled.setCursor(16, 30);
            oled.println(F("IDLE"));
        } else {
            // قیمتِ لحظه‌ای، درشت‌تر از بقیه چون مهم‌ترین عدد برای مسافر است
            oled.setTextSize(2);
            oled.setCursor(0, 18);
            oled.print(t.fare()); oled.println(F("T"));

            oled.setTextSize(1);
            oled.print((long)t.totalDistance()); oled.print(F("m"));
            if (t.waitingSeconds > 0) { oled.print(F("  w")); oled.print((long)t.waitingSeconds); oled.print('s'); }
            oled.println();
            if (t.baggageCount > 0) { oled.print(F("baggage: ")); oled.println(t.baggageCount); }
            if (t.state == TripState::FINISHED) oled.println(F("-- DONE --"));
        }
        oled.display();
    }
};
