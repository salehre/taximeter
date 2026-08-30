#pragma once

// تنظیمات نرخ کرایه:
// - baseFare: کرایه‌ی پایه (اولیه)، یک‌بار به محض سوار شدن مسافر اضافه می‌شود
// - dayPricePerUnit / nightPricePerUnit: هر unitMeters متر، این مقدار اضافه می‌شود
//   (بسته به اینکه آن بخش از مسیر در ساعت روز طی شده یا شب)
// - nightStartHour / nightEndHour: بازه‌ی ساعت شب (24 ساعته). اگر nightStartHour > nightEndHour
//   یعنی بازه از نیمه‌شب رد می‌شود (مثلاً 22 تا 6)
class FareConfig {
public:
    double unitMeters;
    long dayPricePerUnit;
    long nightPricePerUnit;
    long baseFare;
    int nightStartHour;
    int nightEndHour;

    FareConfig(double unit = 50.0,
               long dayPrice = 500,
               long nightPrice = 750,
               long base = 15000,
               int nightStart = 22,
               int nightEnd = 6)
        : unitMeters(unit),
          dayPricePerUnit(dayPrice),
          nightPricePerUnit(nightPrice),
          baseFare(base),
          nightStartHour(nightStart),
          nightEndHour(nightEnd) {}

    // hour == -1 یعنی زمان هنوز مشخص نیست (مثلاً NTP هنوز sync نشده) -> نرخ روز فرض می‌شود
    bool isNightHour(int hour) const {
        if (hour < 0) return false;
        if (nightStartHour <= nightEndHour) {
            return hour >= nightStartHour && hour < nightEndHour;
        }
        return hour >= nightStartHour || hour < nightEndHour; // بازه از نیمه‌شب رد می‌شود
    }

    long calc(double dayDistance, double nightDistance) const {
        long dayFare   = (long)(dayDistance   / unitMeters) * dayPricePerUnit;
        long nightFare = (long)(nightDistance / unitMeters) * nightPricePerUnit;
        return baseFare + dayFare + nightFare;
    }
};
