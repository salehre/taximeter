#pragma once
#include "TimeProvider.h"
#include <time.h>

// منبع زمان روی ESP32: از طریق Wi-Fi با یک سرور NTP همگام می‌شود.
// اگر هنوز Wi-Fi وصل نشده یا همگام نشده، currentHour() مقدار -1 برمی‌گرداند
// (که در FareConfig به‌صورت خودکار به‌عنوان نرخ روز در نظر گرفته می‌شود).
class Esp32TimeProvider : public TimeProvider {
public:
    // gmtOffsetSec پیش‌فرض روی +۳:۳۰ (ایران) تنظیم شده — در صورت نیاز تغییر دهید
    void begin(long gmtOffsetSec = 12600, int daylightOffsetSec = 0,
               const char* ntpServer = "pool.ntp.org") {
        configTime(gmtOffsetSec, daylightOffsetSec, ntpServer);
    }

    int currentHour() override {
        struct tm timeinfo;
        if (!getLocalTime(&timeinfo, 100)) return -1;
        return timeinfo.tm_hour;
    }

    long currentTimestamp() override {
        struct tm timeinfo;
        if (!getLocalTime(&timeinfo, 100)) return 0;
        time_t t = mktime(&timeinfo);
        return (long)t;
    }
};
