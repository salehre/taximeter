#pragma once
#include <Wire.h>

// درایور خیلی ساده برای مالتی‌پلکسر I2C از نوع TCA9548A (حدود ۳۰-۵۰ هزار تومان).
// چرا لازم است: سه‌تا OLED ارزان SSD1306 معمولاً همه یک آدرس I2C ثابت (0x3C)
// دارند، پس نمی‌شود هر سه را مستقیم روی یک باس I2C زد. این مالتی‌پلکسر ۸ کانالِ
// جدا می‌سازد؛ هر OLED به یک کانال وصل می‌شود و قبل از هر ارتباط با یک OLED
// خاص، فقط کانالِ همان یکی باز می‌شود (selectChannel).
//
// سیم‌کشی:
//   TCA9548A VCC -> 3.3V, GND -> GND
//   TCA9548A SDA/SCL -> همان SDA=GPIO21 / SCL=GPIO22 که بقیه‌ی I2C (فقط این
//     یک مالتی‌پلکسر) روی آن است
//   TCA9548A آدرس پیش‌فرض 0x70 (اگر پین‌های آدرس A0/A1/A2 را تغییر دهید، عوض می‌شود)
//   هر OLED به یکی از خروجی‌های SD0..SD2/SC0..SC2 (کانال 0، 1، 2) وصل می‌شود
class Tca9548a {
    uint8_t address;

public:
    explicit Tca9548a(uint8_t addr = 0x70) : address(addr) {}

    void selectChannel(uint8_t channel) {
        if (channel > 7) return;
        Wire.beginTransmission(address);
        Wire.write((uint8_t)(1 << channel));
        Wire.endTransmission();
    }
};
