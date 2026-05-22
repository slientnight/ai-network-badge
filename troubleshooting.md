# Troubleshooting

## Captive Portal Does Not Auto-Launch

### iOS

Open Safari and navigate to `http://192.168.4.1`, or toggle Wi-Fi off and back on. iOS sometimes caches a previous "no-internet" decision for the network; forgetting the network and rejoining clears that state and lets the captive portal pop again.

### Android

Tap the "Sign in to network" notification, or open Chrome and go to `http://192.168.4.1`. On some Android versions you have to dismiss the "Sign in" notification first, then re-tap the SSID to make the captive portal screen reappear.

### Windows

Click the Wi-Fi icon in the system tray and use the "Open browser" link, or open Edge and go to `http://192.168.4.1`.

## "AI-BADGE" Wi-Fi Network Not Visible

- Confirm the badge is actually powered. Any LED activity at all means it's running.
- ESP32-C3 AP range is short. Move within about 5 m of the badge.
- Forget any cached "AI-BADGE" entry on the device and rescan.
- Reboot the badge by unplugging USB and plugging it back in.

## NeoPixels Not Lighting Up

- Verify GPIO 4 is wired to the strip's `DIN`, not `DOUT`. The strip is directional.
- Verify ground is shared between the board and the strip.
- Check the strip orientation arrow on the back of the PCB. Data flows in one direction only.
- If the strip flashes white and then dies, that is a power problem. Lower the brightness via the `/brightness` endpoint, or move the badge to a powered USB hub.

## BLE Peers Not Detected

- Both badges must be powered and within about 5 m of each other.
- The peer's BLE name must start with the prefix in `BLE_BADGE_PREFIX` (default `AI-BADGE`).
- Scans run every 20 s for 3 s. Wait at least one full cycle before assuming a peer is missing.
- Reboot a badge if its BLE stack got into a bad state.

## Contact Storage Full

- The cap is 25 contacts (`MAX_CONTACTS`). After that, the form returns HTTP 507.
- Admin export: `http://192.168.4.1/contacts.csv?key=<ADMIN_KEY>`
- Admin clear: `http://192.168.4.1/clearcontacts?key=<ADMIN_KEY>`
- Replace `<ADMIN_KEY>` with the badge's active admin key (see "Lost the admin key" below for how to find it).

## Lost the Admin Key

Each badge boots with a per-device admin key derived from its chip MAC. There are three ways to find it:

- **Serial console**: open a 115200-baud serial terminal and reboot the badge. The boot banner prints the active key.
- **BOOT-gated web reveal**: hold the BOOT button on the badge and visit `http://192.168.4.1/admin/key`. While the button is held, the page returns the active key in plain text. Without the button held, the page returns 403.
- **Reset to MAC default**: from the serial console, type `clearkey` and press enter. The badge will print the new active key (which is the MAC-derived default).

To set a custom key from the serial console: `setkey=your-custom-value`. There is no web form for rotating the admin key.

## Brightness Too Dim or Too Bright

- The `/brightness` endpoint clamps the slider value to the inclusive range 5–120 (out of 255).
- The default is 32. Raise it carefully — 8 NeoPixels at full brightness can pull more current than a marginal USB supply can provide.

## Sketch Will Not Compile

- Install the Adafruit NeoPixel library via the Arduino Library Manager.
- Install the ESP32 board package. The BLE library is bundled with it, so no separate BLE install is needed.
- Confirm the board selection (`XIAO_ESP32C3` or `ESP32C3 Dev Module`) matches the connected hardware.

## Upload Fails

- Install the USB-serial driver for your board (CP210x or CH340 depending on the revision; the Seeed XIAO ESP32-C3 uses native USB so usually no driver is needed).
- **ESP32-C3 SuperMini specifically**: hold the BOOT button while plugging in the USB cable to enter download mode, then release once the IDE starts the upload. Some SuperMini revisions need this every flash.
- Try a different USB cable. Some cables are charge-only and will not enumerate as a serial device.
- Try a different USB port. USB 3 hubs occasionally cause issues; a direct port on the host is safer.
