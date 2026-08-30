#pragma once

// اینترفیس منبع زمان. مثل GPSProvider، این هم انتزاعی است تا core به
// هیچ API خاص سیستم‌عامل یا سخت‌افزار وابسته نباشد.
// - روی دسکتاپ: SystemTimeProvider (ساعت سیستم)
// - روی ESP32: Esp32TimeProvider (ساعت هماهنگ‌شده با NTP از طریق Wi-Fi)
class TimeProvider {
public:
    virtual int currentHour() = 0;        // ساعت روز، 0 تا 23. اگر هنوز همگام نشده: -1
    virtual long currentTimestamp() = 0;  // ثانیه از epoch (برای ثبت زمان سفرها در تاریخچه)
    virtual ~TimeProvider() {}
};
