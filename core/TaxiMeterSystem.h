#pragma once
#include "Trip.h"
#include "GPSProvider.h"
#include "TimeProvider.h"
#include "DistanceCalculator.h"
#include <vector>
#include <functional>

// یک رکورد سفر تمام‌شده، برای تاریخچه‌ی روزانه
struct TripRecord {
    int passengerSlot;
    double distance;
    long fare;
    long startTimestamp;
    long endTimestamp;
    long dayIndex; // startTimestamp / 86400 -> برای فیلتر کردن "امروز"
};

class TaxiMeterSystem {
public:
    FareConfig fareConfig;
    std::vector<Trip> trips;
    std::vector<TripRecord> history;

    GPSProvider* gps = nullptr;
    TimeProvider* timeProvider = nullptr;

    bool hasLastPosition = false;
    GPSReading lastPosition{};

    // ----- هشدار GPS ضعیف -----
    unsigned long lastReadingMillis = 0;
    unsigned long gpsWarningThresholdMs = 8000; // اگر این‌قدر خوانش جدید نیاد -> هشدار
    bool gpsSignalWeak = false;
    using WarningCallback = std::function<void(bool isWeak)>;
    WarningCallback onGpsWarningChange;

    TaxiMeterSystem(FareConfig cfg, int slotCount = 3) : fareConfig(cfg) {
        for (int i = 0; i < slotCount; i++) trips.emplace_back(i, &fareConfig);
    }

    void setGPSProvider(GPSProvider* provider) {
        gps = provider;
        hasLastPosition = false;
        gps->onUpdate([this](const GPSReading& r) { this->handleReading(r); });
    }

    void setTimeProvider(TimeProvider* tp) { timeProvider = tp; }

    void begin() { if (gps) gps->begin(); }

    // nowMillis باید هر بار از منبع زمان محلی محیط اجرا داده شود:
    // - دسکتاپ: بر اساس std::chrono
    // - ESP32: تابع millis()
    // این باعث می‌شود core به هیچ API خاص سیستم‌عاملی وابسته نباشد.
    void loop(unsigned long nowMillis) {
        if (gps) gps->update();

        bool stale = false;
        if (hasLastPosition) {
            // nowMillis آرگومان ورودی ممکن است کمی قدیمی‌تر از lastReadingMillis باشد
            // (چون درست همین تابع لحظاتی قبل آن را به‌روزرسانی کرد)؛ برای جلوگیری از
            // زیرریزی (underflow) در تفریق unsigned، این حالت را صفر در نظر می‌گیریم.
            unsigned long diff = (nowMillis > lastReadingMillis) ? (nowMillis - lastReadingMillis) : 0;
            stale = diff > gpsWarningThresholdMs;
        }
        setGpsWeak(stale);
    }

    void handleReading(const GPSReading& reading) {
        lastReadingMillis = reading.timestamp;

        if (hasLastPosition) {
            double segment = DistanceCalculator::haversine(
                lastPosition.lat, lastPosition.lon, reading.lat, reading.lon);

            if (segment > 0.8 && segment < 200) {
                int hour = timeProvider ? timeProvider->currentHour() : -1;
                bool isNight = fareConfig.isNightHour(hour);
                for (auto& t : trips) t.addDistance(segment, isNight);
            }
        }
        lastPosition = reading;
        hasLastPosition = true;
        setGpsWeak(false);
    }

    void startTrip(int i) {
        long now = timeProvider ? timeProvider->currentTimestamp() : 0;
        trips[i].start(now);
    }

    void stopTrip(int i) {
        long now = timeProvider ? timeProvider->currentTimestamp() : 0;
        trips[i].stop(now);

        TripRecord rec;
        rec.passengerSlot   = i;
        rec.distance        = trips[i].totalDistance();
        rec.fare            = trips[i].fare();
        rec.startTimestamp  = trips[i].startTimestamp;
        rec.endTimestamp    = trips[i].endTimestamp;
        rec.dayIndex        = trips[i].startTimestamp / 86400;
        history.push_back(rec);
    }

    void resetTrip(int i) { trips[i].reset(); }

    long currentDayIndex() const {
        return timeProvider ? (timeProvider->currentTimestamp() / 86400) : -1;
    }

    // فقط سفرهای امروز را برمی‌گرداند (برای «تاریخچه‌ی سفرهای روز»)
    std::vector<TripRecord> todaysRecords() const {
        std::vector<TripRecord> result;
        long today = currentDayIndex();
        for (auto& r : history) {
            if (today < 0 || r.dayIndex == today) result.push_back(r);
        }
        return result;
    }

    struct DailySummary { int tripCount = 0; double totalDistance = 0; long totalFare = 0; };

    DailySummary todaysSummary() const {
        DailySummary s;
        for (auto& r : todaysRecords()) {
            s.tripCount++;
            s.totalDistance += r.distance;
            s.totalFare += r.fare;
        }
        return s;
    }

private:
    void setGpsWeak(bool weak) {
        if (weak != gpsSignalWeak) {
            gpsSignalWeak = weak;
            if (onGpsWarningChange) onGpsWarningChange(weak);
        }
    }
};
