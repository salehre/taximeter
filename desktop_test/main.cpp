// Full taxi meter test on your own PC -- no hardware required.
//
// Compile:
//   g++ -std=c++11 -pthread main.cpp -o taxi_meter_test
// Run:
//   ./taxi_meter_test        (Windows: .\taxi_meter_test.exe)
//
// Commands:
//   s0 / s1 / s2   -> passenger 1/2/3 boards
//   e0 / e1 / e2   -> passenger 1/2/3 exits
//   r0 / r1 / r2   -> reset that slot for a new trip
//   h22            -> set simulated clock hour to 22 (test night rate). Try h6, h14, h23...
//   hist           -> print today's trip history + daily totals
//   gps            -> toggle GPS on/off (test the "weak GPS" warning)
//   stop / go      -> simulate the car stopping in traffic / moving again (test waiting-time fare)
//   wheel          -> toggle the simulated wheel pulse sensor on/off (test the hybrid GPS+wheel distance source)
//   q              -> quit

#include "TaxiMeterSystem.h"
#include "SimulatedGPSProvider.h"
#include "SimulatedWheelPulseProvider.h"
#include "ManualTimeProvider.h"
#include "FileStatePersistence.h"
#include "ConsoleDisplay.h"
#include "ConsolePassengerDisplay.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <iomanip>
#include <sstream>
#include <string>

std::mutex systemMutex;
std::atomic<bool> running(true);
std::atomic<bool> gpsEnabled(true);
std::atomic<bool> carMoving(true);
std::atomic<bool> wheelInstalled(false);

// نمایشگرهای اختصاصیِ زیر صندلیِ هر مسافر (پیش‌نمایشِ متنیِ سه OLED جدا پشتِ
// مالتی‌پلکسر روی سخت‌افزار واقعی -- نگاه کنید به Esp32PassengerDisplay.h).
ConsolePassengerDisplay passengerDisplay0(0);
ConsolePassengerDisplay passengerDisplay1(1);
ConsolePassengerDisplay passengerDisplay2(2);
PassengerDisplay* passengerDisplays[3] = {&passengerDisplay0, &passengerDisplay1, &passengerDisplay2};

const char* stateLabel(TripState s) {
    switch (s) {
        case TripState::IDLE:     return "IDLE    ";
        case TripState::ACTIVE:   return "ACTIVE  ";
        case TripState::FINISHED: return "FINISHED";
    }
    return "?";
}

void printStatus(TaxiMeterSystem& system, ManualTimeProvider& clock, bool moving, bool wheelInstalled) {
    std::cout << "\n---------------------------------------------\n";
    std::cout << "Clock hour: " << clock.currentHour()
               << " (" << (system.fareConfig.isNightHour(clock.currentHour()) ? "NIGHT rate" : "day rate") << ")"
               << " | GPS: " << (gpsEnabled ? "on" : "OFF (simulating signal loss)")
               << " | signal: " << (system.gpsSignalWeak ? "WEAK/LOST" : "ok") << "\n";
    std::cout << "Car: " << (moving ? "moving" : "STOPPED (traffic)")
               << " | Wheel sensor: " << (wheelInstalled ? "installed (hybrid mode)" : "not installed (GPS-only)")
               << " | last segment source: " << (system.lastDistanceSourceWasWheel ? "wheel" : "GPS") << "\n";
    std::cout << "Base fare: " << system.fareConfig.baseFare
               << " | day: " << system.fareConfig.dayPricePerUnit << "/" << system.fareConfig.unitMeters << "m"
               << " | night: " << system.fareConfig.nightPricePerUnit << "/" << system.fareConfig.unitMeters << "m"
               << " | waiting: " << system.fareConfig.waitingChargePerUnit << "/" << system.fareConfig.waitingUnitSeconds << "s\n";
    std::cout << "---------------------------------------------\n";
    for (auto& t : system.trips) {
        std::cout << "Passenger " << (t.id + 1) << " | " << stateLabel(t.state) << " | "
                  << "day: " << std::fixed << std::setprecision(1) << t.dayDistance << "m, "
                  << "night: " << t.nightDistance << "m, "
                  << "waiting: " << (long)t.waitingSeconds << "s";
        if (t.baggageCount > 0) std::cout << ", baggage: " << t.baggageCount;
        std::cout << " | fare: " << t.fare() << " toman\n";
    }
    bool allActive = true;
    for (auto& t : system.trips) allActive = allActive && (t.state == TripState::ACTIVE);
    if (allActive) std::cout << "*** FULL LED: ON (no empty seats) ***\n";
    std::cout << "---------------------------------------------\n";

    int hour = clock.currentHour();
    bool isNight = system.fareConfig.isNightHour(hour);
    for (auto& t : system.trips) {
        passengerDisplays[t.id]->showTrip(t, hour, isNight, system.gpsSignalWeak);
    }
}

void printHistory(TaxiMeterSystem& system) {
    auto records = system.todaysRecords();
    auto summary = system.todaysSummary();
    std::cout << "\n=== Today's trip history (" << records.size() << " trips) ===\n";
    for (auto& r : records) {
        std::cout << "  passenger slot " << (r.passengerSlot + 1)
                   << " | distance: " << (long)r.distance << "m"
                   << " | waiting: " << (long)r.waitingSeconds << "s";
        if (r.baggageCount > 0) std::cout << " | baggage: " << r.baggageCount;
        std::cout << " | fare: " << r.fare << " toman\n";
    }
    std::cout << "  ---\n  TOTAL: " << summary.tripCount << " trips, "
               << (long)summary.totalDistance << "m, "
               << summary.totalFare << " toman\n";
}

void gpsLoop(TaxiMeterSystem& system) {
    while (running) {
        {
            std::lock_guard<std::mutex> lock(systemMutex);
            // loop() همیشه صدا زده می‌شود (حتی وقتی GPS "خاموش" شبیه‌سازی شده)
            // چون منطق تشخیص قطعی سیگنال داخل همین تابع اجرا می‌شود.
            system.loop(desktopNowMs());
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

int main() {
    std::cout << "=== Taxi Meter Simulator (no hardware needed) ===\n";
    std::cout << "s0/s1/s2 board | e0/e1/e2 exit | r0/r1/r2 reset | b0/b1/b2 baggage+1 | k0/k1/k2 baggage reset | h<hour> set clock | hist | gps toggle | stop/go | wheel toggle | q quit\n";

    FareConfig fareConfig(
        /*unitMeters=*/50.0,
        /*dayPricePerUnit=*/500,
        /*nightPricePerUnit=*/750,
        /*baseFare=*/15000,
        /*nightStartHour=*/22,
        /*nightEndHour=*/6
    );
    TaxiMeterSystem system(fareConfig, /*slotCount=*/3);

    ManualTimeProvider clock(/*startHour=*/12);
    system.setTimeProvider(&clock);

    SimulatedGPSProvider gpsProvider;
    system.setGPSProvider(&gpsProvider);

    SimulatedWheelPulseProvider wheelProvider;
    gpsProvider.attachWheel(&wheelProvider);          // برای هم‌بستگی طبیعیِ حرکت شبیه‌سازی‌شده
    system.setWheelPulseProvider(&wheelProvider);      // برای اینکه TaxiMeterSystem واقعاً از آن استفاده کند

    system.begin();

    system.onGpsWarningChange = [](bool weak) {
        std::cout << (weak ? "\n[WARNING] GPS signal lost/weak!\n" : "\n[OK] GPS signal restored.\n");
    };

    // ذخیره‌سازی تاریخچه‌ی روزانه: هر سفر تمام‌شده فوراً روی فایل نوشته می‌شود،
    // و در شروع برنامه، تاریخچه‌ی امروز (اگر باشد) بازیابی می‌شود.
    FileStatePersistence storage;
    system.onTripFinished = [&storage](const TripRecord& rec) {
        storage.appendHistoryRecord(rec);
    };
    if (storage.loadTodaysHistory(system)) {
        std::cout << "Recovered today's trip history: " << system.history.size() << " trip(s) from disk.\n";
    }

    std::thread bg(gpsLoop, std::ref(system));

    ConsoleDisplay display;
    display.begin();

    std::string cmd;
    while (running) {
        std::cout << "\n> ";
        if (!(std::cin >> cmd)) break;

        std::lock_guard<std::mutex> lock(systemMutex);

        if (cmd == "q") {
            running = false;
        } else if (cmd == "hist") {
            printHistory(system);
            continue;
        } else if (cmd == "gps") {
            gpsEnabled = !gpsEnabled;
            gpsProvider.setEnabled(gpsEnabled);
            std::cout << (gpsEnabled ? "GPS turned ON.\n" : "GPS turned OFF (wait ~8 seconds to see the warning).\n");
        } else if (cmd == "stop") {
            carMoving = false;
            gpsProvider.setMoving(false);
            std::cout << "Car stopped (simulating traffic/red light) -- waiting-time fare should start accruing.\n";
        } else if (cmd == "go") {
            carMoving = true;
            gpsProvider.setMoving(true);
            std::cout << "Car moving again.\n";
        } else if (cmd == "wheel") {
            wheelInstalled = !wheelInstalled;
            wheelProvider.setInstalled(wheelInstalled);
            std::cout << (wheelInstalled
                ? "Wheel pulse sensor turned ON -- distance now sourced from the (simulated) wheel, not GPS.\n"
                : "Wheel pulse sensor turned OFF -- back to GPS-only distance.\n");
        } else if (cmd.size() >= 2 && cmd[0] == 'h' && isdigit(cmd[1])) {
            int h = std::atoi(cmd.c_str() + 1);
            if (h >= 0 && h <= 23) { clock.setHour(h); std::cout << "Clock hour set to " << h << ".\n"; }
            else std::cout << "Hour must be 0-23.\n";
        } else if (cmd.size() == 2 && (cmd[0] == 's' || cmd[0] == 'e' || cmd[0] == 'r')) {
            int idx = cmd[1] - '0';
            if (idx >= 0 && idx < (int)system.trips.size()) {
                if (cmd[0] == 's') {
                    system.startTrip(idx);
                    display.showMessage(("Passenger " + std::to_string(idx + 1) + " boarded").c_str());
                    passengerDisplays[idx]->showMessage("boarded");
                } else if (cmd[0] == 'e') {
                    system.stopTrip(idx);
                    display.showMessage(("Passenger " + std::to_string(idx + 1) + " alighted").c_str());
                    passengerDisplays[idx]->showMessage("alighted - please pay");
                } else if (cmd[0] == 'r') {
                    system.resetTrip(idx);
                    display.showMessage(("Passenger " + std::to_string(idx + 1) + " slot reset").c_str());
                    passengerDisplays[idx]->showMessage("reset");
                }
            } else {
                std::cout << "Invalid passenger number (0 to 2).\n";
            }
        } else if (cmd.size() == 2 && (cmd[0] == 'b' || cmd[0] == 'k')) {
            int idx = cmd[1] - '0';
            if (idx >= 0 && idx < (int)system.trips.size()) {
                if (system.trips[idx].state != TripState::ACTIVE) {
                    std::cout << "That passenger isn't boarded right now (see 'status'/board with s" << idx << ").\n";
                } else if (cmd[0] == 'b') {
                    system.addBaggage(idx);
                    std::string msg = "baggage +1 (now " + std::to_string(system.trips[idx].baggageCount) + ")";
                    display.showMessage(("P" + std::to_string(idx + 1) + " " + msg).c_str());
                    passengerDisplays[idx]->showMessage(msg.c_str());
                } else {
                    system.resetBaggage(idx);
                    display.showMessage(("P" + std::to_string(idx + 1) + " baggage reset to 0").c_str());
                    passengerDisplays[idx]->showMessage("baggage reset to 0");
                }
            } else {
                std::cout << "Invalid passenger number (0 to 2).\n";
            }
        } else {
            std::cout << "Invalid command.\n";
        }

        printStatus(system, clock, carMoving, wheelInstalled);
        display.showStatus(system, clock.currentHour());
    }

    running = false;
    bg.join();
    std::cout << "\nBye.\n";
    return 0;
}
