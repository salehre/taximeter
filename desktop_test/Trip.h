#pragma once
#include "FareConfig.h"

enum class TripState { IDLE, ACTIVE, FINISHED };

// نمایانگر سفر یک مسافر. مسافت روز و شب جدا نگه داشته می‌شود چون نرخشان فرق دارد؛
// waitingSeconds هم جدا نگه داشته می‌شود: زمانی که ماشین توی ترافیک/چراغ‌قرمز
// ایستاده (سرعت نزدیک صفر)، به‌جای مسافت، این عدد بالا می‌رود.
class Trip {
public:
    int id;
    FareConfig* fareConfig;
    TripState state = TripState::IDLE;
    double dayDistance = 0;
    double nightDistance = 0;
    double waitingSeconds = 0;
    int baggageCount = 0;
    long startTimestamp = 0; // ثانیه از epoch (0 یعنی نامشخص)
    long endTimestamp = 0;

    Trip(int id_, FareConfig* cfg) : id(id_), fareConfig(cfg) {}

    void start(long now) {
        state = TripState::ACTIVE;
        dayDistance = 0;
        nightDistance = 0;
        waitingSeconds = 0;
        baggageCount = 0;
        startTimestamp = now;
        endTimestamp = 0;
    }

    void stop(long now) {
        state = TripState::FINISHED;
        endTimestamp = now;
    }

    void reset() {
        state = TripState::IDLE;
        dayDistance = 0;
        nightDistance = 0;
        waitingSeconds = 0;
        baggageCount = 0;
        startTimestamp = 0;
        endTimestamp = 0;
    }

    void addDistance(double meters, bool isNight) {
        if (state != TripState::ACTIVE) return;
        if (isNight) nightDistance += meters;
        else dayDistance += meters;
    }

    void addWaitingTime(double seconds) {
        if (state != TripState::ACTIVE) return;
        waitingSeconds += seconds;
    }

    // دکمه‌ی «بار»: هر فشارِ کوتاه یک واحد اضافه می‌کند.
    void addBaggage() {
        if (state != TripState::ACTIVE) return;
        baggageCount++;
    }

    // نگه‌داشتنِ دکمه‌ی «بار»: تعداد بارِ همین مسافر را صفر می‌کند (اشتباهیِ راننده
    // را جبران می‌کند، بدون نیاز به ریست کل سفر).
    void resetBaggage() {
        if (state != TripState::ACTIVE) return;
        baggageCount = 0;
    }

    double totalDistance() const { return dayDistance + nightDistance; }
    long fare() const { return fareConfig->calc(dayDistance, nightDistance, waitingSeconds, baggageCount); }
};
