#pragma once
#include "WheelPulseProvider.h"

// نسخه‌ی قابل‌کنترل برای تست دستی روی دسکتاپ: به‌جای شمارش پالس واقعی، مسافت را
// مستقیماً "تزریق" می‌کنیم (injectDistance) -- در main.cpp این کار هم‌زمان با هر
// قدم GPS شبیه‌سازی‌شده انجام می‌شود، تا بشود رفتار منبع مسافت ترکیبی (GPS + چرخ)
// را بدون هیچ سخت‌افزاری امتحان کرد.
class SimulatedWheelPulseProvider : public WheelPulseProvider {
    bool installed_ = false; // پیش‌فرض: نصب‌نشده (سیستم فقط با GPS کار می‌کند)
    double pending = 0;

public:
    void setInstalled(bool v) { installed_ = v; }
    bool isInstalled() const override { return installed_; }

    void injectDistance(double meters) { pending += meters; }

    double consumeDistanceMeters() override {
        double d = pending;
        pending = 0;
        return d;
    }
};
