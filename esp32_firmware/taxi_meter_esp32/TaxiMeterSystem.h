#pragma once
#include "Trip.h"
#include "GPSProvider.h"
#include "TimeProvider.h"
#include "WheelPulseProvider.h"
#include "DistanceCalculator.h"
#include <vector>
#include <functional>

// یک رکورد سفر تمام‌شده، برای تاریخچه‌ی روزانه
struct TripRecord {
    int passengerSlot;
    double distance;
    double waitingSeconds;
    int baggageCount;
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
    WheelPulseProvider* wheelPulseProvider = nullptr; // اختیاری؛ nullptr یعنی فقط GPS

    bool hasLastPosition = false;
    GPSReading lastPosition{};

    // برای نمایش/عیب‌یابی: آخرین قطعه‌ی مسیر از حسگر چرخ محاسبه شده یا GPS؟
    bool lastDistanceSourceWasWheel = false;

    // ----- مصرفِ مستقلِ حسگر چرخ (رفع باگ: قطعیِ طولانیِ GPS) -----
    // وقتی حسگر چرخ نصب است، پالس‌هایش را هر wheelTickIntervalMs میلی‌ثانیه
    // یک‌بار، *مستقل از خوانش GPS*، مصرف می‌کنیم -- نه فقط وقتی یک خوانش GPS
    // جدید برسد. قبلاً چون مصرفِ پالس‌ها به رویدادِ GPS گره خورده بود، در
    // نقاطِ بی‌سیگنال (تونل/زیرگذر) پالس‌ها انباشته می‌شدند و وقتی GPS برمی‌گشت،
    // کل آن مسافتِ انباشته یک‌جا می‌رسید و ممکن بود توسط فیلترِ پرش غیرمنطقی
    // رد شود -- یعنی دقیقاً در سناریویی که حسگر چرخ برایش اضافه شده (نبودِ
    // GPS)، مسافت واقعی از دست می‌رفت.
    unsigned long wheelTickIntervalMs = 1000;
    unsigned long lastWheelTickMillis = 0;

    // مسافتِ کوچکِ زیرِ آستانه‌ی نویز (وقتی منبع GPS است) که هنوز اضافه نشده؛
    // به‌جای دورانداختنِ کامل، برای قطعه‌ی بعدی نگه داشته می‌شود تا در نرخ‌های
    // خوانشِ بالای GPS (چند-هرتزی)، مسافتِ واقعی به‌طور سیستماتیک گم نشود.
    double pendingGpsNoiseMeters = 0;

    // ----- هشدار GPS ضعیف -----
    unsigned long lastReadingMillis = 0;
    unsigned long gpsWarningThresholdMs = 8000; // اگر این‌قدر خوانش جدید نیاد -> هشدار
    bool gpsSignalWeak = false;
    using WarningCallback = std::function<void(bool isWeak)>;
    WarningCallback onGpsWarningChange;

    // هر وقت یک سفر تمام شود (stopTrip) صدا زده می‌شود، دقیقاً همان لحظه —
    // نه فقط وقتی برق قطع می‌شود. این باعث می‌شود لایه‌ی ذخیره‌سازی (مثلاً
    // StatePersistence روی ESP32) بتواند همان یک رکورد را فوراً روی فلش
    // بنویسد، پس تا لحظه‌ی بعدیِ قطع برق، تاریخچه‌ی سفرهای قبلیِ همان روز
    // از قبل امن است (برخلاف وضعیت سفر در حال انجام که فقط لحظه‌ی قطع برق ذخیره می‌شود).
    using TripFinishedCallback = std::function<void(const TripRecord&)>;
    TripFinishedCallback onTripFinished;

    TaxiMeterSystem(FareConfig cfg, int slotCount = 3) : fareConfig(cfg) {
        for (int i = 0; i < slotCount; i++) trips.emplace_back(i, &fareConfig);
    }

    void setGPSProvider(GPSProvider* provider) {
        gps = provider;
        hasLastPosition = false;
        gps->onUpdate([this](const GPSReading& r) { this->handleReading(r); });
    }

    void setTimeProvider(TimeProvider* tp) { timeProvider = tp; }

    void setWheelPulseProvider(WheelPulseProvider* w) { wheelPulseProvider = w; }

    void begin() { if (gps) gps->begin(); }

    // nowMillis باید هر بار از منبع زمان محلی محیط اجرا داده شود:
    // - دسکتاپ: بر اساس std::chrono
    // - ESP32: تابع millis()
    // این باعث می‌شود core به هیچ API خاص سیستم‌عاملی وابسته نباشد.
    void loop(unsigned long nowMillis) {
        if (gps) gps->update();

        bool wheelAvailable = wheelPulseProvider && wheelPulseProvider->isInstalled();
        if (wheelAvailable) {
            if (lastWheelTickMillis == 0) {
                // اولین بار: فقط زمان را کالیبره کن و شمارنده را خالی کن تا
                // پالس‌های قدیمی/باقی‌مانده از قبل به‌عنوان یک تکه‌ی بزرگ حساب نشوند.
                lastWheelTickMillis = nowMillis;
                wheelPulseProvider->consumeDistanceMeters();
            } else {
                unsigned long sinceTick = (nowMillis > lastWheelTickMillis) ? (nowMillis - lastWheelTickMillis) : 0;
                if (sinceTick >= wheelTickIntervalMs) {
                    processWheelTick(sinceTick / 1000.0);
                    lastWheelTickMillis = nowMillis;
                }
            }
        } else {
            lastWheelTickMillis = 0;
        }

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

    // این تابع فقط برای منبعِ GPS مسیر را جلو می‌برد. مسیر اطلاع‌رسانیِ GPS
    // (آخرین موقعیت/سیگنال ضعیف) همیشه فعال است -- حتی وقتی حسگر چرخ نصب
    // باشد -- چون هشدار «سیگنال GPS ضعیف» یک نشانگرِ سلامتِ مستقل است، ولی
    // محاسبه‌ی مسافت/زمانِ انتظار از روی GPS فقط وقتی انجام می‌شود که حسگر
    // چرخ نصب نباشد (وگرنه با processWheelTick دوبار حساب می‌شد).
    void handleReading(const GPSReading& reading) {
        lastReadingMillis = reading.timestamp;
        bool wheelAvailable = wheelPulseProvider && wheelPulseProvider->isInstalled();

        if (!wheelAvailable && hasLastPosition) {
            double gpsSegment = DistanceCalculator::haversine(
                lastPosition.lat, lastPosition.lon, reading.lat, reading.lon);

            unsigned long deltaMillis = (reading.timestamp > lastPosition.timestamp)
                ? (reading.timestamp - lastPosition.timestamp) : 0;
            double deltaSeconds = deltaMillis / 1000.0;

            if (deltaSeconds > 0) {
                double speedMps = gpsSegment / deltaSeconds;
                // رد کردن پرش‌های غیرممکن (فیکس اشتباه GPS)، بر اساس سرعتِ ضمنی
                // نه یک سقفِ مطلقِ متر -- تا وقتی فاصله‌ی بین دو خوانش GPS
                // (مثلاً به‌خاطر یک قطعیِ کوتاه) بیشتر از حد معمول شود، یک
                // قطعه‌ی مسافتِ کاملاً واقعی و طولانی هم به‌اشتباه رد نشود.
                if (speedMps <= fareConfig.maxPlausibleSpeedMps) {
                    int hour = timeProvider ? timeProvider->currentHour() : -1;
                    bool isNight = fareConfig.isNightHour(hour);
                    lastDistanceSourceWasWheel = false;

                    if (speedMps < fareConfig.minMovingSpeedMps) {
                        // سرعت خیلی کم -> ترافیک/چراغ‌قرمز/توقف -> کرایه‌ی زمان انتظار،
                        // نه مسافت. این دقیقاً همان کاری‌ست که تاکسی‌مترهای رسمی هم انجام
                        // می‌دهند: کرایه‌ی ترکیبیِ مسافت + زمان، نه فقط مسافت.
                        for (auto& t : trips) t.addWaitingTime(deltaSeconds);
                        pendingGpsNoiseMeters = 0; // ایستاده بودیم؛ نویزِ ریزِ قبلی بی‌ربط است
                    } else {
                        // مسافتِ کوچک‌تر از ۰.۸ متر را فوراً اضافه نمی‌کنیم (نویزِ
                        // موقعیتِ GPS)، ولی برخلاف قبل، آن را دور نمی‌اندازیم؛ برای
                        // قطعه‌ی بعدی نگه می‌داریم تا در نرخ‌های خوانشِ بالا مسافتِ
                        // واقعی به‌طور سیستماتیک گم نشود.
                        pendingGpsNoiseMeters += gpsSegment;
                        if (pendingGpsNoiseMeters > 0.8) {
                            for (auto& t : trips) t.addDistance(pendingGpsNoiseMeters, isNight);
                            pendingGpsNoiseMeters = 0;
                        }
                    }
                }
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
        rec.waitingSeconds  = trips[i].waitingSeconds;
        rec.baggageCount    = trips[i].baggageCount;
        rec.fare            = trips[i].fare();
        rec.startTimestamp  = trips[i].startTimestamp;
        rec.endTimestamp    = trips[i].endTimestamp;
        rec.dayIndex        = trips[i].startTimestamp / 86400;
        history.push_back(rec);
        if (onTripFinished) onTripFinished(rec);
    }

    void resetTrip(int i) { trips[i].reset(); }

    // دکمه‌ی «بار»: فشارِ کوتاه = یک واحد اضافه؛ نگه‌داشتن = صفر کردن بارِ همین سفر.
    void addBaggage(int i) { trips[i].addBaggage(); }
    void resetBaggage(int i) { trips[i].resetBaggage(); }

    // برای بازیابی تاریخچه‌ی امروز از حافظه‌ی غیرفرار در زمان بوت.
    // برخلاف stopTrip، اینجا onTripFinished دوباره صدا زده نمی‌شود
    // (وگرنه هر بوت دوباره همان رکوردها را روی فلش می‌نوشت).
    void restoreHistoryRecord(const TripRecord& rec) { history.push_back(rec); }

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
    // مصرفِ دوره‌ای (هر wheelTickIntervalMs) پالس‌های حسگر چرخ، مستقل از GPS.
    void processWheelTick(double deltaSeconds) {
        double meters = wheelPulseProvider->consumeDistanceMeters();
        if (deltaSeconds <= 0) return;

        double speedMps = meters / deltaSeconds;
        // چون تیک با فاصله‌ی زمانیِ تقریباً ثابت اجرا می‌شود، این چک عملاً هم‌ارزِ
        // همان فیلترِ پرش غیرمنطقی است، ولی اگر loop() یک‌بار تأخیر بخورد هم درست
        // کار می‌کند (چون بر اساس سرعت است، نه یک عددِ مطلق).
        if (speedMps > fareConfig.maxPlausibleSpeedMps) return;

        int hour = timeProvider ? timeProvider->currentHour() : -1;
        bool isNight = fareConfig.isNightHour(hour);
        lastDistanceSourceWasWheel = true;

        if (speedMps < fareConfig.minMovingSpeedMps) {
            for (auto& t : trips) t.addWaitingTime(deltaSeconds);
        } else if (meters > 0) {
            // منبع پالسیِ چرخ گسسته و فیزیکی است (نه نویز رادیوییِ GPS)، پس نیازی
            // به فیلترِ نویزِ ۰.۸ متر نیست؛ هر مقدارِ مثبت، مسافتِ واقعی است.
            for (auto& t : trips) t.addDistance(meters, isNight);
        }
    }

    void setGpsWeak(bool weak) {
        if (weak != gpsSignalWeak) {
            gpsSignalWeak = weak;
            if (onGpsWarningChange) onGpsWarningChange(weak);
        }
    }
};
