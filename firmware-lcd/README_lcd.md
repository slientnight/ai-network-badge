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
- **TFT_eSPI** library — install via Library Manager
- **NimBLE-Arduino** library — install via Library Manager

### TFT_eSPI Configuration

You must configure TFT_eSPI for this board. The easiest method:

1. Find your TFT_eSPI library folder (usually `~/Arduino/libraries/TFT_eSPI/`)
2. Open `User_Setup.h` and replace its contents with:

```c
#define ST7789_DRIVER
#define TFT_WIDTH  172
#define TFT_HEIGHT 320
#define TFT_RGB_ORDER TFT_BGR
#define TFT_INVERSION_ON
#define TFT_BACKLIGHT_ON 1

#define TFT_CS    14
#define TFT_MOSI   6
#define TFT_SCLK   7
#define TFT_DC    15
#define TFT_BL    22
#define TFT_RST   21

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_GFXFF
#define SMOOTH_FONT

#define SPI_FREQUENCY 80000000
```

## Build and Flash

1. Open `firmware-lcd/firmware-lcd.ino` in Arduino IDE.
2. Tools → Board → **ESP32C6 Dev Module**
3. Tools → Partition Scheme → **Minimal SPIFFS (1.9MB APP with OTA)**
4. Tools → USB CDC On Boot → **Enabled**
5. Upload.

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
