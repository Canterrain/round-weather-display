# Round Weather Display Product Spec

This document freezes the current Raspberry Pi behavior so the ESP32-P4 target can match it as closely as possible.

## Display Envelope

- Logical canvas: `800x800`
- Presentation: full-screen square canvas with a circular composition
- Modes: `analog`, `digital`, `forecast`, `message`
- Shared status area: bottom status lines for clock and weather notices

## Primary Views

### Analog Home

- Day label near the top
- Short month/day label under the day
- Large weather icon behind the clock center
- Analog clock dial with:
  - 60 tick marks
  - emphasized quarter ticks
  - 12 numerals
  - hour, minute, and second hands
- Center temperature block with:
  - current temperature
  - high/low line
- Message edge indicator visible when unread messages exist

### Digital Home

- Uppercase day and date
- Large digital time block
- Optional meridiem indicator in 12-hour mode
- Current temperature, icon, summary, and high/low
- Five forecast day cards beneath the current conditions
- Same message edge indicator treatment as analog

### Forecast View

- Large circular "Tomorrow" hero card
- Tomorrow icon and high/low temperatures
- Four additional forecast rows beneath

### Message View

- Single active message card, or empty state
- Message text
- Sender and time metadata when present
- Important messages apply stronger emphasis
- Tap/click dismiss acknowledges the active message

## Navigation Contract

- From `analog`:
  - swipe left -> `forecast`
  - swipe right -> `message`
  - swipe down -> `digital`
- From `digital`:
  - swipe right -> `message`
  - swipe up -> `analog`
- From `forecast`:
  - swipe right -> last home view
- From `message`:
  - swipe left -> last home view
- Swipe threshold:
  - at least 70 px
  - dominant axis must be at least 1.5x the other axis

## Clock Behavior Contract

- Default time format comes from config
- `12` hour mode supports optional leading zero
- `24` hour mode suppresses meridiem
- Calendar labels use English month/day names
- Second hand is enabled
- Clock paused warning appears only when:
  - a clock view is visible
  - the page is visible
  - the displayed minute has not advanced for about `2m 15s`

## Night Shift Contract

- Controlled by:
  - `nightShift`
  - `nightShiftStart`
  - `nightShiftEnd`
- Applies a dim red visual mode during the configured window
- Supports overnight windows such as `22:00` to `06:00`

## Weather Contract

- Weather source: Open-Meteo
- Refresh interval: every 10 minutes
- Fallback UI renders a safe default weather state until live data arrives
- If live weather fails:
  - continue serving the last known good payload when available
  - show `Weather updated <age>` or `Weather data stale`
- Current condition rendering includes:
  - current temperature
  - high/low temperatures
  - icon selection from weather code plus day/night state
- Supported icon families:
  - `clear-day`
  - `clear-night`
  - `partlycloudy-day`
  - `partlycloudy-night`
  - `cloudy`
  - `fog`
  - `rain`
  - `showers-day`
  - `showers-night`
  - `sleet`
  - `snow`
  - `thunderstorm`
  - `thundersnow`

## Forecast Contract

- Forecast view uses five upcoming days
- Each day includes:
  - representative icon
  - high
  - low
- The displayed forecast icon does not blindly use Open-Meteo daily `weathercode`
- Instead, daytime hourly samples from `08:00` through `20:00` are summarized
- Shared forecast heuristics live in `shared/logic/forecast-representative.js`

## Location Resolution Contract

- Shared location-selection rules live in `shared/spec/location-resolution.md`
- Preferred user input is `City, State` with optional country code
- Pi runtime, Pi setup, and ESP32-P4 setup should resolve Open-Meteo matches using the same ranking rules

## Message Contract

- Message polling interval: every 15 seconds
- Each device has:
  - `deviceId`
  - `roomName`
- Modes:
  - `single`
  - `shared`
- Messages support:
  - `text`
  - `sender`
  - `target`
  - `priority`
  - `expiresAt`
- Important messages:
  - sort ahead of normal messages
  - light the edge indicator with an important state
- Message dismiss action:
  - acknowledges the current message for the local device
  - returns to the previous home view

## Shared-Clock Message Behavior

- Shared mode discovers a LAN message hub over UDP broadcast
- One device becomes the hub if none is found
- Hub selection is deterministic by device ID sort order
- Shared clients proxy message API operations to the current hub

## Status Text Contract

- Weather status line:
  - hidden when healthy
  - shows stale/update age when needed
- Clock status line:
  - hidden when healthy
  - shows `Clock paused` only after the conservative stale-clock threshold

## Config Contract

Canonical example:

- `shared/spec/config.example.json`

Expected fields:

- `location`
- `lat`
- `lon`
- `timezone`
- `units`
- `deviceId`
- `roomName`
- `messageSharing`
- `defaultClockFace`
- `timeFormat`
- `leadingZero12h`
- `nightShift`
- `nightShiftStart`
- `nightShiftEnd`
- `thundersnowF`
- `thundersnowC`
- `recentSnowHours`
- `recentSnowMm`
- `recentPrecipMinutes`
- `recentPrecipMm`
- `recentSnowMm15`
- `snowTempF`
- `snowTempC`

## Asset Contract

Shared asset root:

- `shared/assets/`

Shared icon root:

- `shared/assets/icons/`

The Pi runtime and ESP32-P4 runtime should keep the same icon filenames so they can share the same selection rules.
