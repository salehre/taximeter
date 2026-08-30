#pragma once
#include "TimeProvider.h"
#include <ctime>

// برای تست: به‌جای ساعت واقعی سیستم، ساعت را دستی تنظیم می‌کنید
// (مثلاً برای تست نرخ شب بدون نیاز به صبر کردن تا واقعاً شب بشه).
// تاریخ/timestamp همچنان از ساعت واقعی سیستم گرفته می‌شود (برای تاریخچه).
class ManualTimeProvider : public TimeProvider {
    int hour;
public:
    ManualTimeProvider(int startHour = 12) : hour(startHour) {}
    void setHour(int h) { hour = h; }
    int currentHour() override { return hour; }
    long currentTimestamp() override { return (long)time(nullptr); }
};
