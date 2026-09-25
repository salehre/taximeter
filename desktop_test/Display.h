#pragma once
#include "TaxiMeterSystem.h"

// اینترفیس نمایشگر جلوی چشم راننده. مثل GPSProvider و TimeProvider، این هم
// انتزاعی است تا هسته‌ی سیستم (core) به هیچ کتابخانه یا مدل خاصی از نمایشگر
// وابسته نباشد:
// - روی دسکتاپ: ConsoleDisplay (یک پیش‌نمایش متنی از صفحه، در ترمینال)
// - روی ESP32: Esp32Display (یک OLED کوچک I2C، مثلاً SSD1306 128x64)
//
// بدون این نمایشگر، راننده فقط با وصل بودن به کامپیوتر (Serial) می‌تواند کرایه
// را ببیند که برای استفاده‌ی واقعی توی ماشین کافی نیست.
class Display {
public:
    virtual void begin() = 0;

    // صفحه‌ی اصلی: وضعیت هر سه مسافر (کرایه/مسافت)، نرخ فعلی (روز/شب)،
    // و هشدار GPS در صورت وجود. hour ساعت فعلی است (-1 یعنی ساعت هنوز
    // همگام نشده).
    virtual void showStatus(TaxiMeterSystem& system, int hour) = 0;

    // یک پیام کوتاه و گذرا (مثلاً "Passenger 2 boarded" یا هشدار قطع برق).
    // هر پیاده‌سازی خودش تصمیم می‌گیرد چقدر روی صفحه بماند و کِی خودکار به
    // showStatus برگردد.
    virtual void showMessage(const char* message) = 0;

    virtual ~Display() {}
};
