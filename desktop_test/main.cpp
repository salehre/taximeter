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
//   q              -> quit

#include "TaxiMeterSystem.h"
#include "SimulatedGPSProvider.h"
#include "ManualTimeProvider.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <iomanip>
#include <sstream>

std::mutex systemMutex;
std::atomic<bool> running(true);
std::atomic<bool> gpsEnabled(true);

const char* stateLabel(TripState s) {
    switch (s) {
        case TripState::IDLE:     return "IDLE    ";
        case TripState::ACTIVE:   return "ACTIVE  ";
        case TripState::FINISHED: return "FINISHED";
    }
    return "?";
}

void printStatus(TaxiMeterSystem& system, ManualTimeProvider& clock) {
    std::cout << "\n---------------------------------------------\n";
    std::cout << "Clock hour: " << clock.currentHour()
               << " (" << (system.fareConfig.isNightHour(clock.currentHour()) ? "NIGHT rate" : "day rate") << ")"
               << " | GPS: " << (gpsEnabled ? "on" : "OFF (simulating signal loss)")
               << " | signal: " << (system.gpsSignalWeak ? "WEAK/LOST" : "ok") << "\n";
    std::cout << "Base fare: " << system.fareConfig.baseFare
               << " | day: " << system.fareConfig.dayPricePerUnit << "/" << system.fareConfig.unitMeters << "m"
               << " | night: " << system.fareConfig.nightPricePerUnit << "/" << system.fareConfig.unitMeters << "m\n";
    std::cout << "---------------------------------------------\n";
    for (auto& t : system.trips) {
        std::cout << "Passenger " << (t.id + 1) << " | " << stateLabel(t.state) << " | "
                  << "day: " << std::fixed << std::setprecision(1) << t.dayDistance << "m, "
                  << "night: " << t.nightDistance << "m | "
                  << "fare: " << t.fare() << " toman\n";
    }
    std::cout << "---------------------------------------------\n";
}

void printHistory(TaxiMeterSystem& system) {
    auto records = system.todaysRecords();
    auto summary = system.todaysSummary();
    std::cout << "\n=== Today's trip history (" << records.size() << " trips) ===\n";
    for (auto& r : records) {
        std::cout << "  passenger slot " << (r.passengerSlot + 1)
                   << " | distance: " << (long)r.distance << "m"
                   << " | fare: " << r.fare << " toman\n";
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
    std::cout << "s0/s1/s2 board | e0/e1/e2 exit | r0/r1/r2 reset | h<hour> set clock | hist | gps toggle | q quit\n";

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
    system.begin();

    system.onGpsWarningChange = [](bool weak) {
        std::cout << (weak ? "\n[WARNING] GPS signal lost/weak!\n" : "\n[OK] GPS signal restored.\n");
    };

    std::thread bg(gpsLoop, std::ref(system));

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
        } else if (cmd.size() >= 2 && cmd[0] == 'h' && isdigit(cmd[1])) {
            int h = std::atoi(cmd.c_str() + 1);
            if (h >= 0 && h <= 23) { clock.setHour(h); std::cout << "Clock hour set to " << h << ".\n"; }
            else std::cout << "Hour must be 0-23.\n";
        } else if (cmd.size() == 2 && (cmd[0] == 's' || cmd[0] == 'e' || cmd[0] == 'r')) {
            int idx = cmd[1] - '0';
            if (idx >= 0 && idx < (int)system.trips.size()) {
                if (cmd[0] == 's') system.startTrip(idx);
                else if (cmd[0] == 'e') system.stopTrip(idx);
                else if (cmd[0] == 'r') system.resetTrip(idx);
            } else {
                std::cout << "Invalid passenger number (0 to 2).\n";
            }
        } else {
            std::cout << "Invalid command.\n";
        }

        printStatus(system, clock);
    }

    running = false;
    bg.join();
    std::cout << "\nBye.\n";
    return 0;
}
