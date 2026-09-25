#pragma once
#include "GPSProvider.h"
#include "SimulatedWheelPulseProvider.h"
#include <cmath>
#include <cstdlib>
#include <chrono>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

inline unsigned long desktopNowMs() {
    using namespace std::chrono;
    return (unsigned long) duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// یک منبع GPS ساختگی: هر بار که update() صدا زده شود، یک قدم کوچک
// (حدود ۸ متر، معادل تقریبی سرعت شهری) در یک جهت نیمه‌تصادفی حرکت می‌کند --
// مگر اینکه setMoving(false) صدا زده شده باشد (شبیه‌سازی توقف در ترافیک، برای
// تست کرایه‌ی زمان انتظار). دقیقاً همان اینترفیس GPSProvider را پیاده می‌کند، پس
// در آینده با Esp32GPSProvider (که از ماژول واقعی می‌خواند) بدون تغییر جای بقیه‌ی
// کد عوض می‌شود.
class SimulatedGPSProvider : public GPSProvider {
    double lat, lon, bearing;
    UpdateCallback updateCb;
    ErrorCallback errorCb;
    bool enabled = true;
    double stepMeters = 8.0;
    SimulatedWheelPulseProvider* wheel = nullptr; // اختیاری: هم‌زمان با GPS، پالس تزریق کن

public:
    SimulatedGPSProvider(double startLat = 35.700000, double startLon = 51.400000)
        : lat(startLat), lon(startLon), bearing(rand() % 360) {}

    void begin() override {}
    void onUpdate(UpdateCallback cb) override { updateCb = cb; }
    void onError(ErrorCallback cb) override { errorCb = cb; }

    // برای تست هشدار GPS ضعیف: وقتی خاموش است دیگر هیچ خوانشی تولید نمی‌شود،
    // اما (برخلاف قطع کردن کل حلقه‌ی سیستم) TaxiMeterSystem::loop() همچنان صدا زده
    // می‌شود، پس منطق تشخیص قطعی سیگنال واقعاً اجرا و آزمایش می‌شود.
    void setEnabled(bool e) { enabled = e; }

    // برای تست کرایه‌ی زمان انتظار: false یعنی ماشین ایستاده (ترافیک/چراغ‌قرمز) --
    // خوانش‌های GPS همچنان می‌آیند (برخلاف setEnabled) ولی بدون هیچ جابه‌جایی.
    void setMoving(bool moving) { stepMeters = moving ? 8.0 : 0.0; }

    // اگر وصل شود، هر بار که GPS قدم واقعی برمی‌دارد، همان مقدار به شمارنده‌ی
    // حسگر چرخ هم اضافه می‌شود (هم‌بستگی طبیعیِ حرکت واقعی ماشین با هر دو حسگر).
    void attachWheel(SimulatedWheelPulseProvider* w) { wheel = w; }

    void update() override {
        if (!enabled) return;
        double R = 6371000.0;
        double brng = bearing * M_PI / 180.0;
        double lat1 = lat * M_PI / 180.0, lon1 = lon * M_PI / 180.0;

        double lat2 = asin(sin(lat1) * cos(stepMeters / R) +
                            cos(lat1) * sin(stepMeters / R) * cos(brng));
        double lon2 = lon1 + atan2(sin(brng) * sin(stepMeters / R) * cos(lat1),
                                    cos(stepMeters / R) - sin(lat1) * sin(lat2));

        lat = lat2 * 180.0 / M_PI;
        lon = lon2 * 180.0 / M_PI;
        bearing += ((rand() % 1000) / 1000.0 - 0.5) * 25.0;

        if (wheel && wheel->isInstalled() && stepMeters > 0) wheel->injectDistance(stepMeters);

        if (updateCb) updateCb(GPSReading{lat, lon, 5.0, desktopNowMs()});
    }
};
