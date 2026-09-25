#pragma once
#include <cmath>

// تنظیمات نرخ کرایه:
// - baseFare: کرایه‌ی پایه (اولیه)، یک‌بار به محض سوار شدن مسافر اضافه می‌شود
// - dayPricePerUnit / nightPricePerUnit: هر unitMeters متر، این مقدار اضافه می‌شود
//   (بسته به اینکه آن بخش از مسیر در ساعت روز طی شده یا شب)
// - nightStartHour / nightEndHour: بازه‌ی ساعت شب (24 ساعته). اگر nightStartHour > nightEndHour
//   یعنی بازه از نیمه‌شب رد می‌شود (مثلاً 22 تا 6)
// - waitingUnitSeconds / waitingChargePerUnit: کرایه‌ی زمان انتظار. وقتی سرعت لحظه‌ای
//   ماشین از minMovingSpeedMps کمتر شود (ترافیک، چراغ‌قرمز، توقف)، به‌جای مسافت،
//   به‌ازای هر waitingUnitSeconds ثانیه‌ی سکون، waitingChargePerUnit اضافه می‌شود --
//   دقیقاً همان چیزی که تاکسی‌مترهای رسمی اروپا/آمریکا هم انجام می‌دهند (کرایه‌ی
//   ترکیبی مسافت + زمان، نه فقط مسافت). فعلاً این نرخ در همه‌ی ساعات یکسان است
//   (بدون تفکیک روز/شب) تا مدل ساده بماند.
// - tierStartKm / tierSizeKm / tierReductionPct / tierFloorPct: پله‌بندی مسافت
//   طولانی/بین‌شهری. تا tierStartKm نرخ کامل است؛ بعد از آن، هر tierSizeKm یک
//   "پله" حساب می‌شود که tierReductionPct درصد نسبت به پله‌ی قبل ارزان‌تر است
//   (کاهش تصاعدی/ترکیبی، نه خطی -- تا کرایه هیچ‌وقت منفی یا صفر نشود)، تا سقف
//   پایینِ tierFloorPct درصد نرخ اصلی (تا برای سفرهای خیلی خیلی طولانی هم کرایه
//   بی‌معنی کم نشود). این پله‌بندی برای مسافت روز و شب جدا جدا حساب می‌شود (نه
//   مجموع کل سفر) -- ساده‌سازی آگاهانه‌ای که چون سیستم فقط دو مجموع مستقل
//   (dayDistance/nightDistance) نگه می‌دارد نه یک خط زمانی دقیق، برای اکثر
//   سفرها (که کلاً در روز یا کلاً در شب طی می‌شوند) هیچ تفاوتی ایجاد نمی‌کند.
class FareConfig {
public:
    double unitMeters;
    long dayPricePerUnit;
    long nightPricePerUnit;
    long baseFare;
    int nightStartHour;
    int nightEndHour;

    double waitingUnitSeconds;
    long waitingChargePerUnit;
    double minMovingSpeedMps; // زیر این سرعت، "در حال حرکت" محسوب نمی‌شود

    // بالای این سرعت، قطعه‌ی مسافت رد می‌شود (فیکس اشتباه GPS یا خرابی حسگر
    // چرخ). عمداً بر اساس سرعت (متر بر ثانیه) است نه یک سقفِ مطلقِ متر: وقتی
    // حسگر چرخ نصب است و GPS برای مدتی (مثلاً تونل) خوانش نمی‌دهد، ممکن است
    // چند ده متر مسافتِ کاملاً واقعی یک‌جا برسد؛ اگر سقف را روی متر مطلق
    // می‌گذاشتیم، همین مسافتِ درست هم رد می‌شد. پیش‌فرض ۵۵ متر/ثانیه (~۲۰۰
    // کیلومتر/ساعت) خیلی بالاتر از سرعت واقعی تاکسی است.
    double maxPlausibleSpeedMps;

    double tierStartKm;
    double tierSizeKm;
    double tierReductionPct; // مثلاً 15 یعنی هر پله 15% ارزان‌تر از پله‌ی قبل
    double tierFloorPct;     // مثلاً 40 یعنی نرخ هیچ‌وقت زیر 40% نرخ اصلی نمی‌رود

    // هزینه‌ی هر «واحد بار» (چمدان/بسته). راننده با دکمه‌ی «بار» هر واحد را
    // جدا ثبت می‌کند (نگاه کنید به Trip::addBaggage) و این مبلغ به ازای هر
    // واحد به کرایه‌ی نهایی اضافه می‌شود -- ثابت است، پله‌بندی/تخفیف مسافت
    // طولانی روی آن اثر ندارد.
    long baggageChargePerItem;

    FareConfig(double unit = 50.0,
               long dayPrice = 500,
               long nightPrice = 750,
               long base = 15000,
               int nightStart = 22,
               int nightEnd = 6,
               double waitingUnitSec = 60.0,
               long waitingCharge = 3000,
               double minSpeedMps = 5.0 / 3.6, // ۵ کیلومتر بر ساعت
               double maxSpeedMps = 55.0, // ~۲۰۰ کیلومتر بر ساعت
               double tierStart = 10.0,
               double tierSize = 20.0,
               double tierReduction = 15.0,
               double tierFloor = 40.0,
               long baggageCharge = 5000)
        : unitMeters(unit),
          dayPricePerUnit(dayPrice),
          nightPricePerUnit(nightPrice),
          baseFare(base),
          nightStartHour(nightStart),
          nightEndHour(nightEnd),
          waitingUnitSeconds(waitingUnitSec),
          waitingChargePerUnit(waitingCharge),
          minMovingSpeedMps(minSpeedMps),
          maxPlausibleSpeedMps(maxSpeedMps),
          tierStartKm(tierStart),
          tierSizeKm(tierSize),
          tierReductionPct(tierReduction),
          tierFloorPct(tierFloor),
          baggageChargePerItem(baggageCharge) {}

    // hour == -1 یعنی زمان هنوز مشخص نیست (مثلاً NTP هنوز sync نشده) -> نرخ روز فرض می‌شود
    bool isNightHour(int hour) const {
        if (hour < 0) return false;
        if (nightStartHour <= nightEndHour) {
            return hour >= nightStartHour && hour < nightEndHour;
        }
        return hour >= nightStartHour || hour < nightEndHour; // بازه از نیمه‌شب رد می‌شود
    }

    // مسافت (متر) یک نوع (روز یا شب) را به واحدهای unitMeters تقسیم می‌کند و هر
    // واحد را طبق پله‌ی مسافتی خودش قیمت‌گذاری می‌کند.
    long tieredUnitFare(double distanceMeters, long basePrice) const {
        long unitCount = (long)(distanceMeters / unitMeters);
        if (unitCount <= 0) return 0;

        long thresholdUnits = (long)((tierStartKm * 1000.0) / unitMeters);
        long tierSizeUnits = (long)((tierSizeKm * 1000.0) / unitMeters);
        if (tierSizeUnits < 1) tierSizeUnits = 1;
        double floorPrice = basePrice * (tierFloorPct / 100.0);

        long remaining = unitCount;
        double fare = 0;

        long fullPriceUnits = remaining < thresholdUnits ? remaining : thresholdUnits;
        fare += fullPriceUnits * (double)basePrice;
        remaining -= fullPriceUnits;

        int tierIndex = 0;
        while (remaining > 0) {
            tierIndex++;
            double price = basePrice * pow(1.0 - tierReductionPct / 100.0, tierIndex);
            if (price < floorPrice) price = floorPrice;
            long unitsThisTier = remaining < tierSizeUnits ? remaining : tierSizeUnits;
            fare += unitsThisTier * price;
            remaining -= unitsThisTier;
        }
        return (long)(fare + 0.5); // رند به نزدیک‌ترین عدد صحیح
    }

    long calc(double dayDistance, double nightDistance, double waitingSeconds, int baggageCount = 0) const {
        long dayFare     = tieredUnitFare(dayDistance, dayPricePerUnit);
        long nightFare   = tieredUnitFare(nightDistance, nightPricePerUnit);
        long waitingFare = (long)(waitingSeconds / waitingUnitSeconds) * waitingChargePerUnit;
        long baggageFare = (long)baggageCount * baggageChargePerItem;
        return baseFare + dayFare + nightFare + waitingFare + baggageFare;
    }
};
