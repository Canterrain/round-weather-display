# Round Weather Display

[![Watch the Round Clock Weather Display video](https://github.com/user-attachments/assets/ffa7de12-8ffc-4f03-94cb-a3bf73f059a8)](https://www.youtube.com/watch?v=BLWPl8A7HzA)
**Click the image above to see more about this project on Youtube.**

A round clock and weather display for your nightstand, desk, or anywhere you want to keep an eye on the time and weather.

Pick an analog or digital clock face, swipe over to see the forecast, or leave a message for someone at home. Weather comes from Open-Meteo, with no API key needed.

You can build it with a Raspberry Pi and round HDMI touchscreen, or use the Waveshare ESP32-P4 board with its own round display. Both versions have the same clock faces, weather, and household messaging.

## ✨ Features

- Analog and digital clock faces
- 12-hour or 24-hour time, with an optional leading zero
- Current weather and a five-day forecast
- Forecast icons that reflect how most of the day looks
- A small indicator when the weather data is out of date
- Optional red nightshift mode for a dimmer nighttime display
- Household messages you can send from your phone or computer
- Shared messaging between multiple clocks, including a mix of Pi and ESP32-P4 builds
- WiFi setup from the touchscreen if you need to reconnect
- Automatic startup when you power it on

## 🖥 Hardware

There are two ways to build this. Choose the hardware you're using, then follow its setup instructions below.

### Raspberry Pi

- [Raspberry Pi Zero 2 W](https://seeedtechnologycoltd.sjv.io/QYvqYA) (affiliate, preferred)
- Or a [Raspberry Pi 4](https://amzn.to/40en56s) or [Raspberry Pi 5](https://amzn.to/3ZEJUQH) (affiliate)
- A Raspberry Pi 3 may also work, but is untested
- [Waveshare round HDMI touchscreen](https://amzn.to/4gHMrDa) (affiliate)
- Raspberry Pi OS 64-bit Trixie

Additional supplies for the **Raspberry Pi Zero 2 W**:

- [Power supply](https://amzn.to/4zY1Jei) (affiliate)
- [Micro USB to USB-A adapter](https://amzn.to/4hfPgeE) (affiliate)
- [Mini HDMI to HDMI cable](https://amzn.to/4ihBdpX) (affiliate)
- [microSD card](https://amzn.to/4ictxp1) (affiliate)

### ESP32-P4

- [Waveshare ESP32-P4 round touchscreen board](https://amzn.to/4csETBI) (affiliate)
- A [USB-C data cable](https://amzn.to/4gJS7uQ) (affiliate)
- A computer to install the software from your browser

The ESP32-P4 version is built for the **Waveshare ESP32-P4-WIFI6-Touch-LCD-3.4C**, with an 800×800 round touchscreen. The board and display are one unit, so this version doesn't need a Raspberry Pi.

### 3D Printed Cases

If you're printing a case, the files are here:

- [Pebble case — free on MakerWorld](https://makerworld.com/en/models/3274860-smart-round-weather-clock-pebble-style-case#profileId-3713868)
- [Classic case STEP files — available in my store](https://shop.anoraker.com/products/round-weather-clock-classic-case-step-files)
- [Magnets for Classic Case](https://amzn.to/4yCba1Q)

## 🚀 Quick Start

### Raspberry Pi

Start with Raspberry Pi OS installed and the Pi connected to your network. Run these commands on the Pi, either in a terminal or over SSH.

Download the installer:

```bash
wget https://raw.githubusercontent.com/Canterrain/round-weather-display/main/setup.sh
```

Run it:

```bash
bash setup.sh
```

Choose **Raspberry Pi** when asked. The installer will walk you through your location, room name, clock face, temperature units, message sharing, and nightshift settings.

It downloads the project, installs what it needs, saves your settings, and sets up the clock to start automatically.

When it finishes, reboot:

```bash
sudo reboot
```

The clock should appear after the Pi starts back up.

### ESP32-P4

You can install this version straight from your browser. Use Chrome, Edge, or Firefox on a desktop or laptop.

1. Plug the board into your computer with a USB-C data cable.
2. Open the [Round Weather Display installer](https://canterrain.github.io/round-weather-display/).
3. Click the connect button, choose your board, then click **Install**.
4. Once it finishes and restarts, use the touchscreen to set up WiFi, your location, and your clock preferences.

For updates, plug the board back into your computer and use the same installer. Wireless updates aren't available yet.

## Using the Clock

Most of the controls are swipes:

| From | Gesture | What it does |
| --- | --- | --- |
| Analog clock | Swipe down across the middle | Switch to the digital clock |
| Digital clock | Swipe up | Switch to the analog clock |
| Analog clock | Swipe left | Open the forecast |
| Forecast | Swipe right | Return to the clock |
| Either clock face | Swipe right | Open messages |
| Messages | Swipe left | Return to your clock face |
| Any main screen | Swipe down from the very top edge | Open WiFi setup on Pi, or the setup screen on ESP32-P4 |

Tap an unread message to mark it read and return to the clock.

On the Pi, the WiFi screen also opens automatically when the clock loses its connection. You can choose a network and enter its password with the on-screen keyboard. Initial location and clock settings are still handled by the Pi installer.

## 💬 Household Messages

You can leave a message on the clock from a browser on the same home network. Open the address for your device:

**Raspberry Pi:**

```text
http://<hostname>.local:3000/messages
```

For example, if you named your Pi `bedroom-clock`, use `http://bedroom-clock.local:3000/messages`.

**ESP32-P4:**

```text
http://<device-ip>/
```

Replace `<device-ip>` with the clock's IP address.

If you have more than one clock, enable shared messaging during setup. Give each one a room name so it's easy to choose where a message goes. Pi and ESP32-P4 clocks can share messages with each other.

## ⚙️ Configuration

The Pi installer creates your settings file at:

```text
~/round-weather-display/targets/pi/config.json
```

You can use the installer again to change your preferences. It uses your existing settings as the defaults and backs up the previous config in `~/round-weather-display-backups/`.

If you'd rather edit the file yourself, these are the main options. The [example config](shared/spec/config.example.json) includes the full set.

| Setting | What it controls |
| --- | --- |
| `defaultClockFace` | `analog` or `digital` |
| `timeFormat` | `12` or `24` |
| `leadingZero12h` | `true` for `07:00 AM`, `false` for `7:00 AM` |
| `units` | `imperial` for Fahrenheit, `metric` for Celsius |
| `roomName` | The name shown in the message controls, such as `Kitchen` |
| `deviceId` | A unique name used to identify this clock when sharing messages |
| `messageSharing` | `single` for just this clock, or `shared` for household messaging |
| `nightShift` | `true` to enable the dim red nighttime mode |
| `nightShiftStart` / `nightShiftEnd` | When nightshift runs, such as `22:00` to `06:00` |

On the ESP32-P4, use the touchscreen setup screen to change your settings. Swipe down from the very top edge of the display to open it.

## 🛠️ Development Notes

The Raspberry Pi version uses Electron and Express. The ESP32-P4 version uses ESP-IDF and LVGL. The code for each lives in its own folder:

| Path | What's there |
| --- | --- |
| `setup.sh` | Raspberry Pi installer; also launches ESP32-P4 setup from a full checkout |
| `targets/pi/` | Raspberry Pi app, web pages, and launch scripts |
| `targets/esp32-p4/` | ESP32-P4 firmware and build scripts |
| `shared/` | Weather icons, shared weather logic, config example, and behavior notes |
| `docs/` | Browser installer and downloadable ESP32-P4 firmware |

To run the Pi app from a checkout with its dependencies installed:

```bash
npm start
```

To build and flash the ESP32-P4 from source, connect the board over USB and run this from the repo root:

```bash
targets/esp32-p4/scripts/setup.sh
```

The script installs ESP-IDF `v5.5.5` if needed, builds the firmware, and flashes the board. You can also use it for updates. More details are in the [ESP32-P4 README](targets/esp32-p4/README.md).

Run the weather and location checks for both versions with:

```bash
npm run test:all
```

You can also run `npm run test:forecast`, `npm run test:location`, or `npm run test:esp32-parity` separately. See the [test notes](targets/esp32-p4/tests/README.md) for details and the [shared behavior spec](shared/spec/product-spec.md) for how the clock screens should work.

## Related Projects

- [Weather Display](https://github.com/Canterrain/weather-display), my wider clock and weather display for under a cabinet or on a desk
- [Inky Planner](https://github.com/Canterrain/Inky-Planner), a daily planner for Raspberry Pi and Inky e-paper displays

## 📃 License

This project is licensed under the [Creative Commons Attribution-NonCommercial 4.0 International License](https://creativecommons.org/licenses/by-nc/4.0/).

© 2026 Josh Hendrickson

Shout out to the [Magic Mirror](https://github.com/MagicMirrorOrg/MagicMirror) team for inspiring some of this project.

Made by [Josh Hendrickson](https://anoraker.com)
