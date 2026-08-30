#pragma once
#include "GPSProvider.h"
#include <TinyGPS++.h>   // از Library Manager آردوینو نصب کنید: "TinyGPSPlus" by Mikal Hart
#include <HardwareSerial.h>

// پیاده‌سازی واقعی: خواندن از ماژول GPS (مثل NEO-6M) روی UART شماره ۲ ESP32.
// سیم‌کشی پیش‌فرض: GPS TX -> ESP32 GPIO16 (RX2) | GPS RX -> ESP32 GPIO17 (TX2)
class Esp32GPSProvider : public GPSProvider {
    TinyGPSPlus gps;
    HardwareSerial& serial;
    int rxPin, txPin;
    long baud;
    UpdateCallback updateCb;
    ErrorCallback errorCb;
    unsigned long lastFixMillis = 0;

public:
    Esp32GPSProvider(HardwareSerial& serialPort, int rx = 16, int tx = 17, long baudRate = 9600)
        : serial(serialPort), rxPin(rx), txPin(tx), baud(baudRate) {}

    void begin() override {
        serial.begin(baud, SERIAL_8N1, rxPin, txPin);
    }

    void onUpdate(UpdateCallback cb) override { updateCb = cb; }
    void onError(ErrorCallback cb) override { errorCb = cb; }

    void update() override {
        while (serial.available() > 0) {
            gps.encode(serial.read());
        }

        if (gps.location.isUpdated() && gps.location.isValid()) {
            GPSReading reading{
                gps.location.lat(),
                gps.location.lng(),
                gps.hdop.isValid() ? gps.hdop.hdop() * 5.0 : 10.0, // تخمین دقت
                millis()
            };
            lastFixMillis = millis();
            if (updateCb) updateCb(reading);
        }

        // اگر بیش از ۱۰ ثانیه هیچ fix معتبری نیامد، خطا اعلام کن
        if (lastFixMillis > 0 && millis() - lastFixMillis > 10000 && errorCb) {
            errorCb("سیگنال GPS قطع شده است");
        }
    }
};
