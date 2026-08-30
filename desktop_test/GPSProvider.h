#pragma once
#include <functional>

// یک خوانش خام از GPS: مختصات، دقت (متر) و زمان
struct GPSReading {
    double lat;
    double lon;
    double accuracy;      // متر
    unsigned long timestamp;
};

// اینترفیس پایه‌ی منبع GPS.
// هر منبع (شبیه‌سازی، ماژول واقعی NEO-6M و ...) باید همین اینترفیس را پیاده‌سازی کند.
// به همین دلیل بقیه‌ی سیستم (Trip, TaxiMeterSystem) هیچ‌وقت نمی‌فهمد GPS واقعی است یا شبیه‌سازی‌شده.
class GPSProvider {
public:
    using UpdateCallback = std::function<void(const GPSReading&)>;
    using ErrorCallback  = std::function<void(const char*)>;

    virtual void begin() = 0;                     // راه‌اندازی اولیه
    virtual void update() = 0;                     // باید هر بار در loop() اصلی صدا زده شود
    virtual void onUpdate(UpdateCallback cb) = 0;   // callback هنگام دریافت موقعیت جدید
    virtual void onError(ErrorCallback cb) = 0;     // callback هنگام خطا

    virtual ~GPSProvider() {}
};
