#pragma once
#include "Trip.h"

// اینترفیس یک نمایشگر کوچکِ اختصاصیِ یک صندلی/مسافر (نه کل سیستم مثل Display.h).
// هر پیاده‌سازی فقط مسئول یک صندلی است:
// - روی دسکتاپ: ConsolePassengerDisplay (پیش‌نمایش متنی، در ترمینال)
// - روی ESP32: Esp32PassengerDisplay (یک OLED کوچک، پشت یک مالتی‌پلکسر I2C
//   چون سه‌تا OLED ارزان معمولاً همه یک آدرس I2C ثابت (0x3C) دارند)
class PassengerDisplay {
public:
    virtual void begin() = 0;

    // t: فقط سفرِ همین مسافر (نه کل سیستم). hour/isNight برای نمایش نرخ فعلی؛
    // gpsWeak هشدارِ مشترکِ سیگنال GPS است (چون GPS بین هر سه مسافر مشترک است).
    virtual void showTrip(const Trip& t, int hour, bool isNight, bool gpsWeak) = 0;

    // یک پیام کوتاه و گذرا، فقط روی نمایشگر همین مسافر (مثلاً «سوار شد»/«پیاده شد»).
    virtual void showMessage(const char* message) = 0;

    virtual ~PassengerDisplay() {}
};
