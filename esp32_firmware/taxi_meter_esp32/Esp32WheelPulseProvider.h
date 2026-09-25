#pragma once
#include "WheelPulseProvider.h"
#include <Arduino.h>

// شمارش پالس واقعی از یک حسگر هال یا ریدسوییچ مغناطیسی نصب‌شده روی چرخ یا
// میل‌گاردان -- دقیقاً همان رویکردی که تاکسی‌مترهای رسمی به‌جای تکیه‌ی صرف به
// GPS استفاده می‌کنند.
//
// سیم‌کشی: حسگر -> WHEEL_PULSE_PIN (پیش‌فرض GPIO4)، با پول‌آپ داخلی؛ حسگر باید
// با هر عبور آهنربا/دندانه، پین را لحظه‌ای به GND وصل کند (FALLING).
//
// کالیبراسیون (pulsesPerMeter): به تعداد آهنربا/دندانه روی چرخ و محیط چرخ
// بستگی دارد. ساده‌ترین روش عملی:
//   1. ماشین را دقیقاً روی یک مسافت شناخته‌شده حرکت دهید (مثلاً ۱۰۰ متر،
//      با یک نوار متر یا فاصله‌ی بین دو نقطه‌ی مشخص در جاده).
//   2. تعداد پالس‌های شمرده‌شده را از طریق Serial (خط "Wheel pulses: N") بخوانید.
//   3. pulsesPerMeter = N / 100.0
//
// از وقفه‌ی سخت‌افزاری (interrupt) استفاده می‌شود تا هیچ پالسی، حتی در سرعت
// بالا، از دست نرود؛ شمارنده در یک متغیر volatile سراسری نگه داشته می‌شود چون
// ISR در ESP32 باید تابع آزاد (نه متد کلاس) و در IRAM باشد.
#define WHEEL_PULSE_PIN 4

static volatile unsigned long g_wheelPulseCount = 0;

void IRAM_ATTR onWheelPulseISR() {
    g_wheelPulseCount++;
}

class Esp32WheelPulseProvider : public WheelPulseProvider {
    double pulsesPerMeter;
    bool installed_;

public:
    // installed=false (پیش‌فرض) یعنی این حسگر فیزیکاً نصب نشده است -- سیستم
    // بدون هیچ تغییری فقط از GPS استفاده می‌کند. بعد از نصب و کالیبراسیون واقعی
    // سخت‌افزار، این را true کنید و pulsesPerMeter را با عدد کالیبره‌شده جایگزین کنید.
    Esp32WheelPulseProvider(double pulsesPerMeter_ = 8.0, bool installed = false)
        : pulsesPerMeter(pulsesPerMeter_), installed_(installed) {}

    void begin() {
        if (!installed_) return;
        pinMode(WHEEL_PULSE_PIN, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(WHEEL_PULSE_PIN), onWheelPulseISR, FALLING);
    }

    bool isInstalled() const override { return installed_; }

    double consumeDistanceMeters() override {
        if (!installed_) return 0;
        noInterrupts();
        unsigned long count = g_wheelPulseCount;
        g_wheelPulseCount = 0;
        interrupts();
        return count / pulsesPerMeter;
    }
};
