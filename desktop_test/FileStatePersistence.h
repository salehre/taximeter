#pragma once
#include "TaxiMeterSystem.h"
#include <fstream>
#include <sstream>
#include <string>

// معادل دسکتاپیِ StatePersistence.h (که روی ESP32 از NVS/فلش استفاده می‌کند)،
// اینجا به‌جای فلش یک فایل ساده‌ی متنی روی دیسک به کار می‌رود. هدف این است که
// بتوانید رفتار «تاریخچه‌ی امروز جان سالم از قطع برق به در می‌برد» را همین الان،
// روی کامپیوتر خودتان و بدون هیچ سخت‌افزاری، امتحان کنید:
//
//   1. چند سفر انجام بدهید (s0 ... e0 و غیره)
//   2. برنامه را کامل ببندید (q) -- انگار برق قطع شده
//   3. دوباره اجرا کنید -- تاریخچه‌ی امروز باید هنوز سرجایش باشد
//
// فرمت فایل ساده و خط‌به‌خط است؛ صرفاً برای تست محلی است، نه معماری نهاییِ فلش.
class FileStatePersistence {
    std::string path = "taxi_meter_history.txt";

public:
    void appendHistoryRecord(const TripRecord& rec) {
        // اگر روز عوض شده باشد، فایل قبلی را کنار می‌گذاریم (تاریخچه‌ی تازه برای روز جدید)
        long savedDay = readSavedDayIndex();
        if (savedDay >= 0 && savedDay != rec.dayIndex) {
            std::ofstream clear(path, std::ios::trunc);
            clear.close();
        }

        std::ofstream out(path, std::ios::app);
        out << rec.dayIndex << ' ' << rec.passengerSlot << ' '
            << rec.distance << ' ' << rec.waitingSeconds << ' ' << rec.baggageCount << ' ' << rec.fare << ' '
            << rec.startTimestamp << ' ' << rec.endTimestamp << '\n';
    }

    // true برمی‌گرداند اگر واقعاً چیزی از «امروز» بازیابی شد
    bool loadTodaysHistory(TaxiMeterSystem& system) {
        std::ifstream in(path);
        if (!in.is_open()) return false;

        long today = system.currentDayIndex();
        bool any = false;
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            std::istringstream iss(line);
            TripRecord rec{};
            if (!(iss >> rec.dayIndex >> rec.passengerSlot >> rec.distance >> rec.waitingSeconds
                      >> rec.baggageCount >> rec.fare >> rec.startTimestamp >> rec.endTimestamp)) {
                // سازگاری با فایل‌های تاریخچه‌ی قدیمی‌تر از این فیچر (بدون ستونِ باگیج)
                iss.clear();
                iss.str(line);
                rec = TripRecord{};
                if (!(iss >> rec.dayIndex >> rec.passengerSlot >> rec.distance >> rec.waitingSeconds
                          >> rec.fare >> rec.startTimestamp >> rec.endTimestamp)) continue;
            }

            if (today < 0 || rec.dayIndex == today) {
                system.restoreHistoryRecord(rec);
                any = true;
            }
        }
        return any;
    }

private:
    long readSavedDayIndex() {
        std::ifstream in(path);
        if (!in.is_open()) return -1;
        std::string line;
        if (!std::getline(in, line) || line.empty()) return -1;
        std::istringstream iss(line);
        long day;
        iss >> day;
        return day;
    }
};
