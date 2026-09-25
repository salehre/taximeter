#pragma once
#include "Display.h"
#include <iostream>
#include <iomanip>
#include <sstream>

// پیش‌نمایش متنیِ همان چیزی که روی OLED واقعی (128x64، فونت پیش‌فرض ۶x۸ پیکسل)
// دیده می‌شود: یعنی حدود ۲۱ کاراکتر در هر خط و ۸ خط. با این کلاس می‌شود چیدمان
// صفحه را همین الان، قبل از خرید/سیم‌کشی هیچ نمایشگری، دید و تنظیم کرد.
class ConsoleDisplay : public Display {
    static const int COLS = 21;
    static const int ROWS = 8;

    void printFrame(const std::string lines[ROWS]) {
        std::cout << "\n  +" << std::string(COLS + 2, '-') << "+\n";
        for (int i = 0; i < ROWS; i++) {
            std::string line = lines[i].substr(0, COLS);
            std::cout << "  | " << std::left << std::setw(COLS) << line << " |\n";
        }
        std::cout << "  +" << std::string(COLS + 2, '-') << "+\n";
    }

public:
    void begin() override {
        std::string lines[ROWS] = {"Taxi Meter", "booting...", "", "", "", "", "", ""};
        printFrame(lines);
    }

    void showMessage(const char* message) override {
        std::string lines[ROWS] = {"", "", "", message, "", "", "", ""};
        printFrame(lines);
    }

    void showStatus(TaxiMeterSystem& system, int hour) override {
        std::string lines[ROWS];
        int row = 0;

        std::ostringstream top;
        if (hour < 0) top << "--:--  ";
        else top << std::setfill('0') << std::setw(2) << hour << ":00  ";
        top << (system.fareConfig.isNightHour(hour) ? "NIGHT" : "DAY");
        if (system.lastDistanceSourceWasWheel) top << " [wheel]";
        lines[row++] = top.str();

        if (system.gpsSignalWeak) lines[row++] = "** GPS WEAK **";

        for (auto& t : system.trips) {
            std::ostringstream line;
            line << "P" << (t.id + 1) << ": ";
            if (t.state == TripState::IDLE) {
                line << "-- idle --";
            } else {
                line << t.fare() << "T " << (long)t.totalDistance() << "m";
                if (t.waitingSeconds > 0) line << " w" << (long)t.waitingSeconds << "s";
                if (t.baggageCount > 0) line << " bag" << t.baggageCount;
                if (t.state == TripState::FINISHED) line << " (done)";
            }
            if (row < ROWS) lines[row++] = line.str();
        }

        printFrame(lines);
    }
};
