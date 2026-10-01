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

Top to bottom:

- Headline and detail line curve along the top edge of the circle (text on
  two concentric arcs). Amber while an alert is active. Each is fitted to a
  maximum arc length (headline 600px, detail 500px) by stepping the font
  down, measured rather than guessed since fonts differ per device.
  - ESP32-P4: draw text along an arc (LVGL's arc label if the bundled LVGL
    version has it, otherwise per-glyph placement), same fit rule.
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
  spelled out), and `gusts to <n> <unit>` only when gusts exceed the speed;
  the two lines are fitted to a width that clears the W/E letters
- Divider (`summary.changeLabel`): `Conditions changing` when something
  concrete is forecast (an alert, rain now or likely, wind picking up or
  easing, a front or a day-to-day swing); `Conditions may change` for early
  hints only (falling pressure, a low chance of rain); otherwise
  `Conditions steady`
- Three columns with line icons, separated by thin rules. Each is just
  icon, label and one value -- a glance-able summary; explanations belong
  in the headline, which says what matters when it matters:
  - Wind: the outlook, not the current wind (the compass shows that):
    `Picking up`, `Easing` or `Steady`
  - Pressure: `Rising`, `Steady`, `Dropping`, `Dropping fast`
  - Rain (Snow, with a snowflake icon, when snow is expected): `No rain`,
    `<n>% chance`, or `Raining` / `Snowing`
- Column values start at 27px and step down to 18px to stay on one line
  (the middle column is slightly wider for `Dropping fast`). The payload
  still carries `wind.outlook.detail`, `pressure.meaning` and `precip.detail`
  for the headline logic and future use, but they aren't shown in columns
- Values tied to an active alert turn amber
- Same face background as the other views (no scenery)
- Night shift keeps everything red-toned, including the alert colors and
  the curved SVG text
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
  `precipitation_probability`, `snowfall`, `weathercode`, `temperature_2m`
  (in the configured temperature unit), plus daily `temperature_2m_max`, requested with
  `wind_speed_unit=kmh`; everything is computed in km/h, hPa, mm and
  converted for display (mph/inHg/in for imperial)
- Pressure trend is the 3h change (falling/rising beyond 1.5 hPa, otherwise
  steady -- wide enough to ignore the ~1 hPa twice-daily atmospheric tide); before 03:00 local, the next 3h forecast change is used instead
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
- Things already happening say when they end, things on the way say when
  they start: `Thunderstorms until 8 PM`, `Gusts up to 58 mph until 9 PM`,
  `Heavy rain until 5 PM`, `Rain ending by 4 PM` vs `Thunderstorms likely by
  4 PM`. Quiet-day priority: raining now (ending or ongoing), thunder later,
  a front (`Turning much colder by 5 PM`: a drop of 15F / 8C within any 3h
  of the outlook, so evening cooling doesn't count), gusts over the alert
  threshold later, rain likely, chance of rain, a big day-to-day swing
  (`Much colder tomorrow`: tomorrow's high 15F / 8C off today's), falling
  pressure (`Dry for now` / `Falling pressure can mean rain later`), winds
  easing, then `Breezy and dry` / `Calm and dry`
- `summary.changeLevel` is `changing`, `may-change` or `steady` (see the
  divider above); `summary.changing` is true only for `changing`
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
