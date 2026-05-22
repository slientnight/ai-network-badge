# Wiring Guide

This guide covers how to wire the AI Network Badge to either supported board. The badge is intentionally simple: one LED strip on one data pin, plus the BOOT button that's already on the board. If you can solder three wires, you can build this badge.

## Bill of Materials

- 1x ESP32-C3 board — either Seeed XIAO ESP32-C3 or ESP32-C3 SuperMini
- 1x 8-pixel WS2812 or SK6812 NeoPixel strip or ring
- USB-C cable for power and flashing
- Optional but recommended:
  - 470 µF electrolytic capacitor across the LED strip's V+ / GND
  - 330–470 Ω resistor in series with the data line

The optional parts aren't strictly required for 8 pixels at the brightness this firmware uses, but they're cheap insurance against power transients and data-line ringing — especially if you're soldering long leads or running on a marginal USB supply.

## LED Strip Wiring (Both Boards)

The wiring is identical on both supported boards — only the silkscreen label differs.

| Connection | Board pin   | Strip pin |
| ---------- | ----------- | --------- |
| Data       | GPIO 4      | DIN       |
| Ground     | GND         | GND       |
| Power      | 5V (or 3V3) | V+        |

Specifics:

- **Strip type**: 8 pixels, WS2812 or SK6812, GRB color order, 800 kHz timing. The firmware initializes the strip with `NEO_GRB + NEO_KHZ800`, so any standard WS2812-compatible strip in that configuration works.
- **Data**: `board GPIO 4 → strip DIN`. The strip is directional; data must enter at DIN, not DOUT.
- **Ground**: `board GND → strip GND`. This must be a shared ground — if board and strip are on separate supplies, tie the grounds together.
- **Power**: `board 5V → strip V+` for full brightness. The 3V3 pin works too if you're drawing from the onboard regulator and don't mind a dimmer strip.

The firmware drives the strip at a brightness clamped between 5 and 120 (out of 255), so peak current per pixel stays well under the WS2812's 60 mA spec. With 8 pixels at the maximum allowed brightness, total current draw is comfortably within what USB can supply.

## BOOT Button

- GPIO 9, built into both supported boards
- No external wiring required
- Pulled HIGH internally (`INPUT_PULLUP`); pulses LOW when pressed

The BOOT button cycles through the four idle patterns. Because both boards already expose this button as a labeled component on the PCB, you don't need to add anything — just press the existing button.

## Per-Board Pin Reference

### Seeed XIAO ESP32-C3

- **LED data (DIN)**: GPIO 4 — labeled `D2` on the silkscreen
- **GND**: any GND pad
- **Power**: 5V (USB passthrough) or 3V3 pad

Quick visual: the XIAO has pin pads in two rows along the long edges. Looking at the board with the USB-C connector at the top, GPIO 4 (`D2`) is the third pad down from the USB-C connector on the left edge.

### ESP32-C3 SuperMini

- **LED data (DIN)**: GPIO 4 pin (labeled `4` on the silkscreen)
- **GND**: any GND pin
- **Power**: 5V or 3V3 pin

Quick visual: GPIO 4 sits roughly in the middle of one of the long edges. The SuperMini labels GPIO numbers directly on the silkscreen, so look for the pin printed `4`.

## Power and Decoupling

For 8 pixels at this firmware's brightness range, USB power is sufficient — no external supply needed. The optional components below are recommended for clean operation, especially with longer leads or in noisy environments.

- **470 µF electrolytic capacitor across the LED strip's V+ and GND**, mounted as close to the first pixel as practical. Smooths the inrush when many pixels light at once. Watch polarity: the negative leg goes to GND.
- **330–470 Ω resistor in series on the data line**, between board GPIO 4 and the strip's DIN. Clamps ringing on long leads and reduces the chance of corrupted first-pixel data.
- **Keep the data wire short** — under ~10 cm if possible — to reduce noise pickup. Twisting the data and ground wires together helps if you need a longer run.

## Sanity Check After Soldering

A quick smoke test before you trust the badge in the wild:

1. Power the board over USB only (no battery).
2. Watch the LEDs for a brief "Badge booted" AI-pulse animation, then the default Packet Chase idle pattern.
3. Press the BOOT button once — the idle pattern should advance to the next one (AI Pulse, then Network Sparkle, then Rainbow Mesh, then back to Packet Chase).

What different failure modes look like:

- **Strip stays dark**: see `troubleshooting.md` → "NeoPixels Not Lighting Up". Common causes are reversed data direction (board hooked to DOUT instead of DIN), missing shared ground, or wrong pin.
- **Strip lights all-white and then goes dark or flickers**: USB supply can't keep up with the inrush. Lower brightness via the badge web page (`http://192.168.4.1`, brightness slider) or move the strip to a powered USB hub.

## Cricut Templates and Badge Front Art

The `cricut/` folder contains 4-inch round badge artwork sized for a Cricut vinyl cutter:

- `front_vinyl_cut.svg` — front vinyl: "I NETWORK / WITH AI / BUILD THE MESH" text plus copper traces and LED node rings.
- `led_placement_guide.svg` — paper print this and use it behind the acrylic to position the 8 LEDs (LED1 at 12 o'clock, LED2-8 clockwise).
- `back_label_print_then_cut.svg` — back instruction label with a QR code to `http://192.168.4.1`.

See `cricut/README_cricut.md` for full notes on Cricut Design Space settings, vinyl colors, and the LED numbering layout. The wiring chain (`board GPIO 4 → LED1 DIN → LED1 DOUT → LED2 DIN → ...`) follows that LED1-at-12-o'clock numbering.
