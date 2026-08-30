#pragma once
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// محاسبه‌ی فاصله‌ی بین دو مختصات جغرافیایی (فرمول Haversine)
class DistanceCalculator {
public:
    static double haversine(double lat1, double lon1, double lat2, double lon2) {
        const double R = 6371000.0; // شعاع زمین بر حسب متر
        auto toRad = [](double d) { return d * M_PI / 180.0; };

        double dLat = toRad(lat2 - lat1);
        double dLon = toRad(lon2 - lon1);

        double a = sin(dLat / 2) * sin(dLat / 2) +
                   cos(toRad(lat1)) * cos(toRad(lat2)) *
                   sin(dLon / 2) * sin(dLon / 2);

        double c = 2 * atan2(sqrt(a), sqrt(1 - a));
        return R * c; // متر
    }
};
