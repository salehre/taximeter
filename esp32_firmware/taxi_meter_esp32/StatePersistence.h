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
};
