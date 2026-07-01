# Round Weather Display

A round analog weather clock designed for Raspberry Pi displays. The UI combines an analog clock face, current weather, a swipeable forecast view, and a local household message system that can be accessed from a phone on the same network.

## ✨ Features

- Analog round clock face with configurable 12h or 24h time
- Day/date display
- Real-time weather via Open-Meteo (no API key required)
- Smarter forecast icons based on how most of the day looks, not just one noisy daily weather code
- Subtle stale weather indicator if Open-Meteo is temporarily unavailable
- Conservative clock health warning if the visible clock appears paused
- Custom SVG weather icons
- Optional red nightshift mode for a dimmer nighttime display
- Swipeable forecast screen
- Local message screen with phone-accessible message entry
- Important/unread message indicator on the main clock face
- Auto-start with PM2 on boot
- Optional shared household messaging between clocks
- Clean circular UI designed for round displays
- Works on both X11 (Bookworm) and Wayland/labwc (Trixie)

## 🖥 Requirements

- [Raspberry Pi Zero 2 W](https://amzn.to/4ds03kH) (affiliate)
The Zero 2 W is preferred for provided cases, but youi can also use:
- [Raspberry Pi 4](https://amzn.to/40en56s) (affiliate)
- or
- [Raspberry Pi 5](https://amzn.to/3ZEJUQH) (affiliate)
- Raspberry Pi 3B may work on Bookworm only (not recommended for Trixie)
- [Raspberry Pi Zero 2 W Power Supply](https://amzn.to/4coiu8G) (affiliate)
- [Rasperry Pi 4 or 5 Power Supply](https://amzn.to/3MvrPBF) (affiliate)
- [Round HDMI display](https://amzn.to/41LmZEa) affiliate
- [Mini HDMI to HDMI for Pi Zero 2 adapter](https://amzn.to/4vbvwh8) (affiliate)
- Raspberry Pi OS 64-bit:
  - Bookworm (X11 or Wayland)
  - Trixie (Wayland/labwc default)
- 3D Printed case 

## 🚀 Quick Start

Download the install script:

```bash
wget https://raw.githubusercontent.com/Canterrain/round-weather-display/main/setup.sh
```

Install the software:

```bash
bash setup.sh
```

Reboot Raspberry Pi:

```bash
sudo reboot
```

During Raspberry Pi OS setup, choose a short, simple hostname for the device, such as `Clock`. That makes the local message page easier to remember later at:

```text
http://clock.local:3000/messages
```

## What This Script Does

- Installs required system dependencies
- Installs Node.js 20 LTS
- Installs and configures the app
- Detects Bookworm vs Trixie automatically
- Configures auto-start:
  - Bookworm (X11): PM2
  - Trixie (Wayland/labwc): labwc autostart
- Sets up fonts, weather configuration, and optional nightshift settings
- Sets up clock/device identity and message-sharing mode
- After reboot, the display should launch automatically.

---
## ⚙️ Configuration

The `setup.sh` script automatically creates a `config.json` file.

Example:

```
{
  "location": "Cincinnati,OH,US",
  "lat": 39.1031,
  "lon": -84.5120,
  "timezone": "America/New_York",
  "units": "imperial",
  "deviceId": "kitchen-clock",
  "roomName": "Kitchen",
  "messageSharing": "single",
  "defaultClockFace": "digital",
  "timeFormat": "12",
  "leadingZero12h": true,
  "nightShift": false,
  "nightShiftStart": "22:00",
  "nightShiftEnd": "06:00",
  "thundersnowF": 34,
  "thundersnowC": 1,
  "recentSnowHours": 2,
  "recentSnowMm": 0,
  "recentPrecipMinutes": 60,
  "recentPrecipMm": 0,
  "recentSnowMm15": 0,
  "snowTempF": 34,
  "snowTempC": 1
}
```

### Clock Options

- "deviceId"

  - Unique ID used for message targeting and shared-clock coordination
  - Example: `"kitchen-clock"`

- "roomName"

  - Human-readable label used in the message system UI
  - Example: `"Kitchen"`

- "defaultClockFace"

  - `"digital"` → digital home screen on startup

  - `"analog"` → analog home screen on startup

- "timeFormat"
  
  - "12" → 12-hour time (7:00 AM)
  
  - "24" → 24-hour time (07:00)
  
- "leadingZero12h" (12-hour mode only)
  
  - true → 07:00 AM
  
  - false → 7:00 AM

### Night Options

- "nightShift"

  - false → normal color mode all day

  - true → switches to a dim red mode during the configured night window

- "nightShiftStart"
- "nightShiftEnd"

  Example:

  - "22:00" → 10:00 PM

  - "06:00" → 6:00 AM

This mode is optional and is meant to make the display feel more like a night clock, not an alert screen.

### Message Options

- "messageSharing"

  - "single" → this clock uses its own local messages

  - "shared" → this clock automatically looks for other shared clocks on the LAN

These settings only affect how this specific clock behaves locally. The shared household message protocol remains compatible with `weather-display`.

### Weather Behavior

The system uses Open-Meteo’s `current_weather` field as the primary source for current conditions, then applies a few small corrections to make the display behave more like a real household weather clock.

Current weather behavior:

- The display prefers recent observed precipitation over a stale-looking current icon
- Recent snow can briefly keep the snow icon visible even after precipitation has just stopped
- If Open-Meteo temporarily fails, the server will keep serving the last known good weather payload instead of going blank
- When that happens, the frontend shows a subtle stale weather message so you know the display is running on cached data

Forecast behavior:

- Daily high and low temperatures still come from Open-Meteo daily data
- Forecast icons do not blindly use Open-Meteo’s daily `weathercode`
- Instead, the clock looks at daytime hourly conditions, roughly 8 AM through 8 PM, and picks a representative icon based on what most of the day looks like
- This helps avoid days showing as thunderstorms just because one small forecast window contains storm risk
- The raw Open-Meteo daily code is still included internally as `dailyCode` for debugging, but the displayed forecast icon uses the representative daytime code

Status behavior:

- Weather status text stays hidden unless there is actually a problem
- If weather data is being served from the last known good payload, the display shows a subtle `Weather updated ... ago` or `Weather data stale` message
- The frontend also uses a very conservative visible-clock health check and only shows `Clock paused` if the clock appears frozen for roughly 2 minutes while the clock is actually on screen

Optional tuning values (advanced users):

```
{
  "thundersnowF": 34,
  "thundersnowC": 1,
  "recentSnowHours": 2,
  "recentSnowMm": 0,
  "recentPrecipMinutes": 60,
  "recentPrecipMm": 0,
  "recentSnowMm15": 0,
  "snowTempF": 34,
  "snowTempC": 1
}
```

- recentSnowHours / recentSnowMm

  If measurable snowfall occurred within this window, the snow icon may persist briefly even if precipitation has just stopped.

- recentPrecipMinutes / recentPrecipMm / recentSnowMm15

  These values tune how recent minutely precipitation is interpreted when choosing between rain and snow-style icons.

- snowTempF / snowTempC

  These set the temperature threshold used when recent precipitation needs to be interpreted as rain versus snow.

- thundersnowF / thundersnowC

  These set the temperature threshold for preferring a thundersnow icon instead of a standard thunderstorm icon.

Additional advanced tuning is also supported for recent precipitation detection and snow/rain temperature thresholds.

These defaults are conservative and do not fabricate weather data — they only reinterpret recent official Open-Meteo measurements in a way that tends to look more like a normal consumer weather display.

## 🎨 Weather Icon Customization

All weather icons are SVG files stored in:

```
public/assets/icons/
```

To use your own custom icons:

- Replace existing files using the **same filenames** (e.g., `clear-day.svg`, `rain.svg`, etc.)
- Keep them in **SVG format**
- For consistent layout, aim for icons sized around **100×100 pixels**

---

## 🧠 Project Structure

| Path                        | Description                                  |
|-----------------------------|----------------------------------------------|
| `public/index.html`         | Main UI layout                               |
| `public/style.css`          | Display styles                               |
| `public/renderer/clock.js`  | Time and date logic                          |
| `public/renderer/weather.js`| Weather, forecast, and swipe navigation      |
| `public/renderer/messages.js`| Message screen logic and unread state       |
| `server.js`                 | Express server for frontend                  |
| `scripts/rwc.sh`            | PM2 launch script                            |
| `config.json`               | Created by `setup.sh` for user configuration |



---

## 🛠️ Development Notes

This project runs as a Node.js server (Express) and is typically launched via PM2 on a Raspberry Pi.

For development and testing, you can access the UI directly in a browser:

http://<pi-ip>:3000/

For everyday use on your home network, the local message entry page is easiest to reach using the Raspberry Pi hostname you chose during Raspberry Pi OS setup:

http://<hostname>.local:3000/messages

Current screen flow:

- Analog home screen
  - Swipe left → forecast
  - Swipe right → message screen
  - Swipe down → digital home screen
- Digital home screen
  - Swipe right → message screen
  - Swipe up → analog home screen

The default home screen is configurable with `"defaultClockFace"` in `config.json`.

---

## 📦 Autostart via PM2

The setup script automatically configures PM2 to:

```
pm2 start scripts/rwc.sh --name round-weather-display
pm2 save
pm2 startup
```

This ensures the app runs on boot.

---

## 📃 License

This project is licensed under the [Creative Commons Attribution-NonCommercial 4.0 International License](https://creativecommons.org/licenses/by-nc/4.0/).

© 2025 Josh Hendrickson

---

Shout out to the [Magic Mirror](https://github.com/MagicMirrorOrg/MagicMirror) team for inspiring some of this project.

---

Made by [Josh Hendrickson](https://anoraker.com)
