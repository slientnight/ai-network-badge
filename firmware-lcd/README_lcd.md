# AI Network Badge — LCD Variant

This is the LCD variant of the AI Network Badge, built for the **Waveshare ESP32-C6-LCD-1.47** board.

## Hardware

| Component | Spec |
|-----------|------|
| Board | Waveshare ESP32-C6-LCD-1.47 |
| Chip | ESP32-C6 (RISC-V, 160 MHz) |
| Display | 1.47" IPS LCD, 172×320, ST7789 |
| Wireless | Wi-Fi 6 (2.4 GHz) + BLE 5 |
| Flash | 4 MB |

## Features

Same badge functionality as the NeoPixel variant:
- Wi-Fi captive portal with per-badge unique SSID
- BLE peer discovery and leaderboard sync
- Contact card submissions stored in NVS
- Admin key, web console, OTA updates

Plus:
- **LCD shows badge info directly** — name, level, stats, nearby peers
- **No phone required** to see your badge status
- Display updates every 5 seconds

## Prerequisites

- Arduino IDE 2.x with ESP32 board package (≥3.0.0)
- **Arduino_GFX_Library** by moononournation — install via Library Manager (search "GFX Library for Arduino")
- **NimBLE-Arduino** by h2zero — install via Library Manager

No special display configuration files needed — pin mapping is defined directly in the sketch.

## Build and Flash

1. Open `firmware-lcd/firmware-lcd.ino` in Arduino IDE.
2. Tools → Board → **ESP32C6 Dev Module**
3. Tools → Partition Scheme → **Minimal SPIFFS (1.9MB APP with OTA)**
4. Tools → USB CDC On Boot → **Enabled**
5. Upload (hold BOOT + press RST if upload fails, then release BOOT).

## Pin Mapping

| Function | GPIO |
|----------|------|
| LCD CS | 14 |
| LCD MOSI (SDA) | 6 |
| LCD SCK | 7 |
| LCD DC | 15 |
| LCD Backlight | 22 |
| LCD Reset | 21 |
| BOOT button | 9 |
