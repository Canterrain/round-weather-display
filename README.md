# Round Weather Display

A round analog weather clock designed for Raspberry Pi displays. The UI combines an analog clock face, current weather, a swipeable forecast view, and a local household message system that can be accessed from a phone on the same network.

## ✨ Features

- Analog round clock face with configurable 12h or 24h time
- Day/date display
- Real-time weather via Open-Meteo (no API key required)
- Custom SVG weather icons
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

## What This Script Does

- Installs required system dependencies
- Installs Node.js 20 LTS
- Installs and configures the app
- Detects Bookworm vs Trixie automatically
- Configures auto-start:
  - Bookworm (X11): PM2
  - Trixie (Wayland/labwc): labwc autostart
- Sets up fonts and weather configuration
- Sets up clock/device identity and message-sharing mode
- After reboot, the display should launch automatically.

---
## ⚙️ Configuration

The `setup.sh` script automatically creates a `config.json` file.

Example:

```
{
  "location": "Cincinnati,OH,US",
  "lat": xx.xx,
  "lon": -xx.xxxx,
  "timezone": "America/New_York",
  "units": "imperial",
  "deviceId": "kitchen-clock",
  "roomName": "Kitchen",
  "messageSharing": "single",
  "timeFormat": "12",
  "leadingZero12h": true
}
```

### Clock Options

- "timeFormat"
  
  - "12" → 12-hour time (7:00 AM)
  
  - "24" → 24-hour time (07:00)
  
- "leadingZero12h" (12-hour mode only)
  
  - true → 07:00 AM
  
  - false → 7:00 AM

- "messageSharing"

  - "single" → this clock uses its own local messages

  - "shared" → this clock automatically looks for other shared clocks on the LAN

### Weather Behavior

The system uses Open-Meteo’s current_weather field as the primary source.

Optional tuning values (advanced users):

```
{
  "thundersnowF": 34,
  "thundersnowC": 1,
  "recentSnowHours": 2,
  "recentSnowMm": 0
}
```

- recentSnowHours

  If measurable snowfall occurred within this window, the snow icon may persist briefly even if precipitation has just stopped.

These defaults are conservative and do not fabricate weather data — they only interpret recent official Open-Meteo measurements.

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

The local message entry page is available at:

http://<pi-ip>:3000/messages.html

Current screen flow:

- Main clock screen
- Swipe left → forecast
- Swipe right → message screen

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
