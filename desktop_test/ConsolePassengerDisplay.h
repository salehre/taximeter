#pragma once
#include "PassengerDisplay.h"
#include <iostream>
#include <iomanip>
#include <sstream>

// پیش‌نمایش متنیِ نمایشگر اختصاصیِ زیر صندلیِ یک مسافر (معادل یک OLED کوچک
// پشتِ مالتی‌پلکسر روی ESP32 -- نگاه کنید به Esp32PassengerDisplay.h). سه‌تا
// از این کلاس ساخته می‌شود، یکی برای هر صندلی، و هرکدام فقط سطرِ خودش را
// چاپ می‌کند.
class ConsolePassengerDisplay : public PassengerDisplay {
    int slot;
    static const int COLS = 21;

    void printLine(const std::string& line) {
        std::cout << "  [P" << (slot + 1) << " mini-screen] "
                  << std::left << std::setw(COLS) << line.substr(0, COLS) << "\n";
    }

public:
    explicit ConsolePassengerDisplay(int slotIndex) : slot(slotIndex) {}

    void begin() override {
        printLine("waiting...");
    }

    void showMessage(const char* message) override {
        printLine(message);
    }

    void showTrip(const Trip& t, int hour, bool isNight, bool gpsWeak) override {
        std::ostringstream line;
        if (hour < 0) line << "--:-- ";
        else line << std::setfill('0') << std::setw(2) << hour << ":00 ";
        line << (isNight ? "NGT" : "DAY");
        if (gpsWeak) line << " *GPSWEAK*";

        if (t.state == TripState::IDLE) {
            line << " | IDLE";
        } else {
            line << " | " << t.fare() << "T " << (long)t.totalDistance() << "m";
            if (t.waitingSeconds > 0) line << " w" << (long)t.waitingSeconds << "s";
            if (t.baggageCount > 0) line << " bag" << t.baggageCount;
            if (t.state == TripState::FINISHED) line << " (DONE)";
        }
        printLine(line.str());
    }
};
