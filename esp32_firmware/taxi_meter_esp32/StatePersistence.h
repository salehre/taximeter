#pragma once
#include <Preferences.h>
#include "TaxiMeterSystem.h"

// وقتی برق ماشین قطع می‌شود، باتری پشتیبان چند ثانیه به ESP32 وقت می‌دهد
// تا وضعیت سفرهای در حال انجام را روی فلش (حافظه‌ی غیرفرار NVS) ذخیره کند.
// با روشن شدن دوباره‌ی دستگاه، این وضعیت بازیابی می‌شود تا کرایه‌ی مسافری
// که وسط راه بوده از دست نرود.
class StatePersistence {
    Preferences prefs;

public:
    void saveActiveTrips(TaxiMeterSystem& system) {
        prefs.begin("taximeter", false);
        for (size_t i = 0; i < system.trips.size(); i++) {
            Trip& t = system.trips[i];
            String prefix = "t" + String(i) + "_";
            prefs.putInt((prefix + "state").c_str(), (int)t.state);
            prefs.putDouble((prefix + "day").c_str(), t.dayDistance);
            prefs.putDouble((prefix + "night").c_str(), t.nightDistance);
            prefs.putDouble((prefix + "waiting").c_str(), t.waitingSeconds);
            prefs.putInt((prefix + "bag").c_str(), t.baggageCount);
            prefs.putLong((prefix + "start").c_str(), t.startTimestamp);
        }
        prefs.putBool("hasSave", true);
        prefs.end();
    }

    // true برمی‌گرداند اگر واقعاً چیزی برای بازیابی وجود داشت
    bool restoreActiveTrips(TaxiMeterSystem& system) {
        prefs.begin("taximeter", true); // read-only
        bool hasSave = prefs.getBool("hasSave", false);
        if (!hasSave) { prefs.end(); return false; }

        for (size_t i = 0; i < system.trips.size(); i++) {
            Trip& t = system.trips[i];
            String prefix = "t" + String(i) + "_";
            int state = prefs.getInt((prefix + "state").c_str(), 0);
            t.state = (TripState)state;
            t.dayDistance = prefs.getDouble((prefix + "day").c_str(), 0);
            t.nightDistance = prefs.getDouble((prefix + "night").c_str(), 0);
            t.waitingSeconds = prefs.getDouble((prefix + "waiting").c_str(), 0);
            t.baggageCount = prefs.getInt((prefix + "bag").c_str(), 0);
            t.startTimestamp = prefs.getLong((prefix + "start").c_str(), 0);
        }
        prefs.end();
        return true;
    }

    // بعد از بازیابی موفق یا در شروع تازه، پرچم را پاک کن تا دفعه‌ی بعد
    // یک ری‌استارت عادی (نه ناشی از قطع برق) اشتباهی چیزی را بازیابی نکند.
    void clearSavedFlag() {
        prefs.begin("taximeter", false);
        prefs.putBool("hasSave", false);
        prefs.end();
    }

    // ================================================================
    // تاریخچه‌ی سفرهای تمام‌شده‌ی امروز
    // ================================================================
    // برخلاف saveActiveTrips (که فقط لحظه‌ی قطع برق صدا زده می‌شود)، این تابع
    // باید به system.onTripFinished وصل شود تا بلافاصله بعد از هر سفر تمام‌شده
    // اجرا شود -- یعنی حتی اگر قطع برق دقیقاً وسط سفر بعدی اتفاق بیفتد، تمام
    // سفرهای قبلیِ همان روز از قبل روی فلش امن هستند.
    //
    // یک namespace جدا ("taxihist") استفاده می‌کنیم تا قاطی سفرهای فعال نشود.
    // سقفی روی تعداد رکوردهای ذخیره‌شده می‌گذاریم (MAX_HISTORY_RECORDS) که برای
    // یک شیفت واقعی تاکسی بیش از کافی است؛ اگر با این حال رد شد، جمع‌کل روزانه
    // (totalFare/totalDistance/totalTrips) جدا و همیشه دقیق نگه داشته می‌شود
    // حتی اگر رکورد به رکورد همه ذخیره نشده باشند.
    static const int MAX_HISTORY_RECORDS = 150;

    void appendHistoryRecord(const TripRecord& rec) {
        prefs.begin("taxihist", false);

        long savedDay = prefs.getLong("dayIndex", -1);
        int count = prefs.getInt("count", 0);

        if (savedDay != rec.dayIndex) {
            // روز عوض شده -> تاریخچه‌ی روز قبل را دور بریز، از صفر شروع کن
            count = 0;
            prefs.putLong("dayIndex", rec.dayIndex);
            prefs.putLong("totalFare", 0);
            prefs.putDouble("totalDist", 0);
            prefs.putInt("totalTrips", 0);
        }

        if (count < MAX_HISTORY_RECORDS) {
            String key = "r" + String(count);
            prefs.putBytes(key.c_str(), &rec, sizeof(TripRecord));
            count++;
            prefs.putInt("count", count);
        }

        long totalFare   = prefs.getLong("totalFare", 0) + rec.fare;
        double totalDist = prefs.getDouble("totalDist", 0) + rec.distance;
        int totalTrips   = prefs.getInt("totalTrips", 0) + 1;
        prefs.putLong("totalFare", totalFare);
        prefs.putDouble("totalDist", totalDist);
        prefs.putInt("totalTrips", totalTrips);

        prefs.end();
    }

    // هنگام بوت صدا زده می‌شود: اگر تاریخچه‌ی ذخیره‌شده مال «امروز» است، رکوردها
    // را در system.history می‌ریزد. اگر مال روز قبل بود، دور ریخته می‌شود
    // (یعنی برای «امروز» یک تاریخچه‌ی خالی درست است، نه دستکاری‌شده).
    // true برمی‌گرداند اگر واقعاً چیزی از همان روز بازیابی شد.
    bool loadTodaysHistory(TaxiMeterSystem& system) {
        prefs.begin("taxihist", true);

        long savedDay = prefs.getLong("dayIndex", -1);
        long today = system.currentDayIndex();

        if (savedDay < 0 || (today >= 0 && savedDay != today)) {
            prefs.end();
            return false; // چیزی برای امروز نیست
        }

        int count = prefs.getInt("count", 0);
        for (int i = 0; i < count; i++) {
            String key = "r" + String(i);
            TripRecord rec;
            size_t len = prefs.getBytes(key.c_str(), &rec, sizeof(TripRecord));
            if (len == sizeof(TripRecord)) system.restoreHistoryRecord(rec);
        }

        prefs.end();
        return count > 0;
    }

    // جمع‌کل واقعی امروز را مستقیم از فلش می‌خواند -- حتی اگر تعداد سفرها از
    // MAX_HISTORY_RECORDS بیشتر شده باشد و بعضی رکوردها تک‌به‌تک ذخیره نشده
    // باشند، این عدد همیشه دقیق است چون جدا از رکوردها نگه داشته می‌شود.
    struct PersistedDailyTotals {
        bool valid = false;
        int tripCount = 0;
        double totalDistance = 0;
        long totalFare = 0;
    };

    PersistedDailyTotals loadTodaysTotals(TaxiMeterSystem& system) {
        PersistedDailyTotals t;
        prefs.begin("taxihist", true);
        long savedDay = prefs.getLong("dayIndex", -1);
        long today = system.currentDayIndex();
        if (savedDay >= 0 && (today < 0 || savedDay == today)) {
            t.tripCount = prefs.getInt("totalTrips", 0);
            t.totalDistance = prefs.getDouble("totalDist", 0);
            t.totalFare = prefs.getLong("totalFare", 0);
            t.valid = true;
        }
        prefs.end();
        return t;
    }
};
