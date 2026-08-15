# Round Weather Display

Round household weather display with a consistent product experience across multiple hardware targets. Both the Raspberry Pi and ESP32-P4 targets are working implementations of the same product spec.

## Current Targets

- `targets/pi/`
  Working runtime using Electron + Express on Raspberry Pi OS.
- `targets/esp32-p4/`
  Native ESP-IDF + LVGL firmware for the 800x800 round ESP32-P4 board (Waveshare `ESP32-P4-WIFI6-Touch-LCD-3.4C`). Analog/digital/forecast/message screens, live weather, on-device WiFi and location setup, and shared house messaging across multiple clocks all work; see `targets/esp32-p4/README.md` for details.
- `shared/`
  Shared assets, shared logic, and the frozen product specification that both targets should follow.

## Features

- Analog round clock face with configurable 12h or 24h time
- Digital home screen
- Swipeable forecast screen
- Local message screen with phone-accessible message entry
- Shared household messaging mode for multiple clocks on the same LAN
- Real-time weather via Open-Meteo
- Representative forecast icons that reflect how most of the day looks
- Stale-weather indication and conservative clock-paused detection
- Optional red night shift mode
- Shared SVG weather icon set for all targets

## Repo Layout

| Path | Purpose |
| --- | --- |
| `setup.sh` | Top-level installer entrypoint |
| `shared/assets/` | Shared icons and image assets |
| `shared/logic/forecast-representative.js` | Shared forecast-icon heuristic |
| `shared/spec/product-spec.md` | Frozen behavior and UI contract for both targets |
| `shared/spec/config.example.json` | Canonical config example |
| `targets/pi/` | Raspberry Pi app runtime |
| `targets/pi/public/` | Pi UI markup, styles, and browser-side renderers |
| `targets/pi/server.js` | Pi Express server and weather/message APIs |
| `targets/pi/scripts/` | Pi launch, restart, theme, and validation scripts |
| `targets/esp32-p4/` | ESP32-P4 firmware runtime |
| `docs/` | GitHub Pages browser-flashing site for the ESP32-P4 (built firmware + [ESP Web Tools](https://esphome.github.io/esp-web-tools/) install page) |

## Quick Start

### Raspberry Pi

This is a fresh headless install onto the Pi itself — just the one file, no clone needed first.

Download the installer:

```bash
wget https://raw.githubusercontent.com/Canterrain/round-weather-display/main/setup.sh
```

Run it:

```bash
bash setup.sh
```

The installer asks which hardware target you want; choose Raspberry Pi and it walks through the rest (location, room name, message sharing, etc.) and installs itself.

### ESP32-P4

**Easiest way — no software install:** go to
**[canterrain.github.io/round-weather-display](https://canterrain.github.io/round-weather-display/)**,
plug the board in over USB, and click the button. It flashes straight from your browser (Chrome, Edge, or
Firefox on a desktop computer — not Safari, not a phone/tablet). Once it's done, the rest of setup (WiFi,
location, room name) happens right on the round display itself.

**If you're developing the firmware, or want to build from source instead:** clone the repo and run:

```bash
targets/esp32-p4/scripts/setup.sh
```

This installs ESP-IDF if it isn't present yet, builds, detects the board's serial port, and flashes. It's also
the update path for this route — re-run it any time to pull the latest source and reflash. See
`targets/esp32-p4/README.md` for details. Choosing ESP32-P4 in the root `setup.sh` delegates straight to this
script.

## Raspberry Pi Requirements

- Raspberry Pi Zero 2 W, Raspberry Pi 4, or Raspberry Pi 5
- Round HDMI display
- Raspberry Pi OS 64-bit
- Bookworm or Trixie

## ESP32-P4 Requirements

- Waveshare `ESP32-P4-WIFI6-Touch-LCD-3.4C` (800x800 round display)
- A USB-C data cable and a computer to flash it from (no OTA yet) — just a browser
  ([Chrome/Edge/Firefox](https://canterrain.github.io/round-weather-display/)) if using the browser-flashing page,
  or ESP-IDF `v5.5.5` (installed automatically by `targets/esp32-p4/scripts/setup.sh`) if building from source

## Configuration

The canonical config example lives at `shared/spec/config.example.json`.

The Pi installer writes the live device config to:

```text
~/round-weather-display/targets/pi/config.json
```

Important options:

- `deviceId`
  Unique ID used for message targeting and shared-clock coordination.
- `roomName`
  Human-friendly name shown in message controls.
- `defaultClockFace`
  `analog` or `digital`.
- `timeFormat`
  `12` or `24`.
- `leadingZero12h`
  Controls `07:00 AM` versus `7:00 AM`.
- `messageSharing`
  `single` or `shared`.
- `nightShift`
  Enables the dim red nighttime mode.

## Product Contract

The Pi build was the original reference implementation; both targets are now built against the same frozen spec.

- Exact UI and behavior contract:
  `shared/spec/product-spec.md`
- Shared forecast heuristic:
  `shared/logic/forecast-representative.js`
- Shared icons:
  `shared/assets/icons/`

The goal is same product behavior, different runtime implementations.

## Development Notes

Run the current Pi app from the repo root:

```bash
npm start
```

Or directly:

```bash
npm --prefix targets/pi start
```

Run the shared-logic tests (forecast heuristic, location resolution, and the ESP32-P4 C ports of both, checked
against the same fixtures the JS is tested against — see `targets/esp32-p4/tests/README.md`):

```bash
npm run test:forecast
npm run test:location
npm run test:esp32-parity
npm run test:all   # all three
```

On a running Pi, the message admin page is available at:

```text
http://<hostname>.local:3000/messages
```

Each ESP32-P4 clock serves the same composer page directly from the device itself:

```text
http://<device-ip>/
```

## Status

Both targets implement the full product spec:

- Analog, digital, forecast, and message screens with matched layout/behavior across targets.
- Live weather via Open-Meteo.
- Shared house messaging: any clock (Pi or ESP32-P4) can compose and receive messages, with UDP-based hub discovery/election so multiple clocks on the same LAN coordinate automatically.
- On-device WiFi and location setup on the ESP32-P4 (touchscreen: network scan, on-screen keyboard, location geocoding); the Pi path is configured through `setup.sh`.
- `shared/spec/product-spec.md` is the frozen behavior/UI contract both targets are built against.

The ESP32-P4 target has no wireless OTA yet — firmware updates are a USB reflash, either via the
[browser-flashing page](https://canterrain.github.io/round-weather-display/) or `targets/esp32-p4/scripts/setup.sh`.

## License

This project is licensed under the [Creative Commons Attribution-NonCommercial 4.0 International License](https://creativecommons.org/licenses/by-nc/4.0/).

© 2025 Josh Hendrickson

Shout out to the [Magic Mirror](https://github.com/MagicMirrorOrg/MagicMirror) team for inspiring some of this project.

Made by [Josh Hendrickson](https://anoraker.com)
