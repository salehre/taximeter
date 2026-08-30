#pragma once
#include "GPSProvider.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// نسخه‌ی Arduino/ESP32 برای تست بدون ماژول GPS واقعی (روی برد واقعی یا در Wokwi).
// هر ۱۰۰۰ میلی‌ثانیه یک قدم حرکت جعلی تولید می‌کند.
class SimulatedGPSProvider : public GPSProvider {
    double lat, lon, bearing;
    unsigned long lastTick = 0;
    UpdateCallback updateCb;
    ErrorCallback errorCb;

public:
    SimulatedGPSProvider(double startLat = 35.700000, double startLon = 51.400000)
        : lat(startLat), lon(startLon) {
        bearing = random(0, 360);
    }

    void begin() override { lastTick = millis(); }
    void onUpdate(UpdateCallback cb) override { updateCb = cb; }
    void onError(ErrorCallback cb) override { errorCb = cb; }

    void update() override {
        unsigned long now = millis();
        if (now - lastTick < 1000) return;
        lastTick = now;

        double stepMeters = 8.0;
        double R = 6371000.0;
        double brng = bearing * M_PI / 180.0;
        double lat1 = lat * M_PI / 180.0, lon1 = lon * M_PI / 180.0;

        double lat2 = asin(sin(lat1) * cos(stepMeters / R) +
                            cos(lat1) * sin(stepMeters / R) * cos(brng));
        double lon2 = lon1 + atan2(sin(brng) * sin(stepMeters / R) * cos(lat1),
                                    cos(stepMeters / R) - sin(lat1) * sin(lat2));

        lat = lat2 * 180.0 / M_PI;
        lon = lon2 * 180.0 / M_PI;
        bearing += (random(-250, 250) / 10.0);

        if (updateCb) updateCb(GPSReading{lat, lon, 5.0, now});
    }
};
