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
- Weather edge indicator (amber, right edge) visible while a storm alert is active

### Digital Home

- Uppercase day and date
- Large digital time block
- Optional meridiem indicator in 12-hour mode
- Current temperature, icon, summary, and high/low
- Five forecast day cards beneath the current conditions
- Same message and weather edge indicator treatment as analog

### Forecast View

- Large circular "Tomorrow" hero card
- Tomorrow icon and high/low temperatures
- Four additional forecast rows beneath

### Conditions View

- No title; a plain-language headline and detail line lead the view, e.g.
  `Calm and dry` / `for the next 6 hours`, or `Thunderstorms likely by 4 PM` /
  `Gusts up to 58 mph by 4 PM`. Amber while an alert is active
- Text fitting: the headline sits in a 440px-wide box (the circle is only
  ~510px across at that height) and steps its font down from 31px to 22px
  until it fits on one line, measured rather than guessed from length since
  fonts differ per device (the Pi falls back to DejaVu Sans). The detail line
  and the pressure/rain lines fit the same way. Only wrap as a last resort.
  - ESP32-P4: measure with `lv_text_get_size` and pick from the available
    font sizes the same way
- Wind compass: ring with N/E/S/W labels and ticks; a weathervane-style
  arrow crosses the dial in the direction the wind is moving: fletching
  (three swept-back strokes) on the upwind side, a filled arrowhead pointing
  out on the downwind side, and a faint dashed shaft behind the readout.
  Hidden when direction is unknown. (Unlike a real weathervane, the head
  points downwind, so it agrees with the `from the <direction>` text.)
  - ESP32-P4: render the arrow once as an alpha-only image and rotate it
    with `lv_image_set_rotation` like the clock hands, recoloring it for
    normal / alert / night shift. The Pi's glow is decorative only.
- Inside the compass: wind speed and unit, `from the <direction>` (8-point,
  spelled out), and `gusts to <n> <unit>` only when gusts exceed the speed
- Pressure column: status word and a hedged plain-language line, with the
  raw reading and its 3h change as small print:
  - `Rising` / `Can mean clearer weather`
  - `Steady` / `Weather likely to stay the same`
  - `Dropping` / `Can mean clouds or rain`
  - `Dropping fast` / `Can mean a storm is coming`
- Rain (or Snow) column: `None expected`, `Possible (n%)`, `Likely (n%)`,
  or `Raining now`, plus the expected amount by the end of the next 6h
- Values tied to an active alert turn amber
- Night shift keeps everything red-toned, including the alert colors
- All wording comes from the shared logic, not the renderer

### Settings View

- Reached by swiping down from home; identical fields on both targets, in
  this order: Location (City, State), Room Name, Device ID, House Messages
  (Single/Shared), Home Screen (Digital/Analog), Time Format (12-hour/24-hour),
  Leading Zero (On/Off), Units (Imperial/Metric), Night Shift (On/Off)
- Buttons: `Wi-Fi` (opens the network picker; its Done/Back returns to
  Settings with unsaved edits intact), `Cancel` (discards edits, returns
  home), `Save & Restart`
- Text fields are edited on their own step with the on-screen keyboard
- Saving validates (all three text fields required), looks up a changed
  location (lat/lon/timezone), writes the config, and restarts the app
- A lost Wi-Fi connection still opens the network picker directly
- Pi: `GET`/`POST /api/settings`, localhost-only like the Wi-Fi API; keys
  the screen doesn't edit are preserved; config is written atomically

### Message View

- Single active message card, or empty state
- Message text
- Sender and time metadata when present
- Important messages apply stronger emphasis
- Tap/click dismiss acknowledges the active message

## Navigation Contract

- Home is whichever face `defaultClockFace` selects (`analog` or `digital`).
  The other face is not reachable by gesture.
- From home (either face):
  - swipe left -> first view of the weather row (see below)
  - swipe right -> `message`
  - swipe down -> Settings; anywhere on the face, home only
  - swipe up -> unused (reserved for a future view)
- Weather row, left of home: `forecast` then `conditions`. While a storm
  alert is active the order is `conditions` then `forecast`. The order is
  fixed when leaving home, so an alert starting or clearing mid-visit only
  takes effect on the next swipe out of home.
  - swipe left -> next view in the row (no-op at the end)
  - swipe right -> previous view in the row, or home from the first
- From `message`:
  - swipe left -> home
- From Settings or its Wi-Fi page:
  - swipe up -> home
- Swipes that start on a scrolling or interactive area (WiFi network list,
  on-screen keyboard) are handled by that area and never navigate.
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

## Conditions Contract

- Shared logic lives in `shared/logic/storm-conditions.js`; fixtures in
  `shared/test-data/storm-conditions-cases.json`
- Inputs: `current_weather` (live wind speed/direction) plus hourly
  `pressure_msl`, `wind_speed_10m`, `wind_gusts_10m`, `precipitation`,
  `precipitation_probability`, `snowfall`, `weathercode`, requested with
  `wind_speed_unit=kmh`; everything is computed in km/h, hPa, mm and
  converted for display (mph/inHg/in for imperial)
- Pressure trend is the 3h change (falling/rising beyond 1 hPa, otherwise
  steady); before 03:00 local, the next 3h forecast change is used instead
- Alerts consider now through the next 3h:
  - `thunder`: weathercode 95/96/99
  - `gusts`: gusts >= 40 mph / 64 km/h, else `wind`: sustained >= 25 mph / 40 km/h
  - `pressure`: 3h change <= -3 hPa
  - `precip`: >= 7.6 mm in an hour (`Heavy snow` if snowfall, else `Heavy rain`)
- `summary.headline` / `summary.detail`: the first alert as a sentence with
  its timing (`now` or `by <hour>`), the second alert (or the pressure
  meaning) as the detail. With no alerts, in priority order: raining now,
  thunder later in the 6h outlook, gusts over the alert threshold later,
  rain likely, chance of rain, then `Breezy and dry` / `Calm and dry`
- Times follow `timeFormat` (`6 PM` or `18:00`)

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
- Optional storm thresholds: `stormGustMph` / `stormGustKmh`,
  `stormWindMph` / `stormWindKmh`, `stormPressureDropHpa`, `stormPrecipMmHr`

## Asset Contract

Shared asset root:

- `shared/assets/`

Shared icon root:

- `shared/assets/icons/`

The Pi runtime and ESP32-P4 runtime should keep the same icon filenames so they can share the same selection rules.
