# ESP32-P4 UI Parity Checklist

Updated on Friday, August 7, 2026.

Reference implementation:

- Pi product behavior in `shared/spec/product-spec.md`
- Pi runtime in `targets/pi/public/index.html`
- Pi styling in `targets/pi/public/style.css`
- Pi clock behavior in `targets/pi/public/renderer/clock.js`
- Pi message behavior in `targets/pi/public/renderer/messages.js`
- Pi weather behavior in `targets/pi/public/renderer/weather.js`
- Analog-specific details in `shared/spec/esp32-p4-analog-parity-checklist.md`

Goal:

- Keep the ESP32-P4 user-facing product flow aligned with the Raspberry Pi version across the main screens, touch navigation, setup flow, and night-shift behavior.
- Preserve the current proven ESP32-P4 runtime baseline and avoid extra build/flash cycles unless a measured hardware issue forces it.

## Checklist

| Area | Pi reference behavior | Current ESP32-P4 implementation | Match | Stable on hardware | Notes |
| --- | --- | --- | --- | --- | --- |
| Analog home | Accepted baseline analog clock face with unread edge indicator, night treatment, and shared status behavior | Implemented on normal product firmware with Pi-derived second-hand art, larger center temperature, and pulsing important-state indicator behavior | yes | yes | Important-state pulse uses the existing raster wedge rather than a second brighter full-screen asset because the heavier asset regressed transport stability |
| Digital home | Uppercase day/date, large digital time, meridiem rules, current weather block, five-day forecast strip, edge indicator | Rebuilt around the Pi `760x760` face-local coordinate system; time and meridiem are laid out as one centered group; current conditions now use live `current_weather` codes and the five-day row uses the shared representative daytime forecast heuristic | partial | yes | A fresh physical-panel recheck is still required after the August 7 timezone/weather parity pass; runtime success alone is not treated as visual parity |
| Forecast view | Tomorrow hero card plus four additional forecast rows | Rebuilt around the same centered Pi face-local coordinate system used by the forecast reference screen | partial | yes | Hardware/runtime is clean after the layout pass, but physical visual comparison still needs to be re-done on the panel |
| Message view | Single active message card or empty state, important emphasis, tap dismiss | Pi-style day/night stage asset, live message card, important color emphasis, empty state, tap-to-acknowledge behavior | partial | yes | Live queue/ack state is wired; shared-hub networking is still single-device only on ESP32-P4 |
| Swipe navigation | Analog left/right/down, digital right/up, forecast right-home, message left-home | Transparent gesture layer with the same directional mapping and threshold logic | yes | yes | Implemented with single-point touch tracking rather than LVGL gesture recognition |
| Setup / settings | User can complete Wi-Fi + location flow and preserve clock preferences | Wi-Fi scan/manual entry, password entry, shared location lookup, face/time/leading-zero/units/night-shift toggle, persisted save/restart | partial | yes | The visible ready-state home-screen Settings button was removed in favor of hidden long-press access so the normal product UI matches the Pi more closely; night-shift start/end, device/room identity, and message-sharing mode are still not exposed in the ESP setup UI |
| Shared night shift | Red-toned dimmed appearance across clock surfaces, message view, and status handling | Analog, digital, forecast, and message views all swap to night styling; weather/status visibility follows per-view rules | partial | yes | Implemented with view-specific assets/tints instead of browser-wide CSS/filter classes |

## Batch Validation

### Digital + Forecast

- Date tested: Friday, August 7, 2026
- Hardware log: `targets/esp32-p4/runtime-logs/20260807-digital-forecast-batch.log`
- Measured result:
  - Clean boot completed.
  - Display and touch initialized successfully.
  - ESP-Hosted transport initialized successfully.
  - Wi-Fi reconnected and obtained `192.168.1.65`.
  - First Open-Meteo fetch succeeded.
  - No watchdog, LVGL lock, or draw-buffer allocation failures appeared in the captured run.

### Digital + Forecast Layout Correction

- Date tested: Friday, August 7, 2026
- Hardware log: `targets/esp32-p4/runtime-logs/20260807-digital-forecast-layout-pass.log`
- Measured result:
  - The digital and forecast builders were corrected to use the Pi reference's centered `760x760` face-local coordinate system instead of placing those coordinates directly on the full `800x800` stage.
  - The digital time and meridiem were re-laid out as one centered group instead of two unrelated stage anchors.
  - The digital current-weather row was re-anchored to the Pi temp/icon/copy positions.
  - The persistent ready-state home-screen `Settings` button was removed, and the hidden setup long-press hotspot was enlarged for reliability.
  - The updated firmware booted cleanly on hardware, initialized display/touch and ESP-Hosted successfully, reconnected Wi-Fi, and completed the first Open-Meteo fetch without watchdog or transport regressions.
  - This pass validates runtime stability after the visual-layout correction, but it does not replace a fresh physical-screen comparison.

### Digital Timezone + Weather Parity

- Date tested: Friday, August 7, 2026
- Hardware log: `targets/esp32-p4/runtime-logs/20260807-digital-timezone-forecast-pass.log`
- Measured result:
  - The ESP32-P4 now applies the configured timezone at boot before any `localtime()`-driven UI formatting, instead of only during SNTP sync.
  - The boot log confirmed `America/New_York` was mapped to `EST5EDT,M3.2.0/2,M11.1.0/2` and immediately formatted as local `EDT`, fixing the prior UTC-style `+4h` display bug.
  - The current weather block now uses the live Open-Meteo `current_weather` code for icon/summary selection instead of borrowing the day's daily code.
  - The five-day digital row now uses the same representative daytime forecast heuristic as the Pi/shared implementation rather than the raw daily weather code.
  - The updated firmware booted cleanly, initialized display/touch and ESP-Hosted successfully, reconnected Wi-Fi, completed the first Open-Meteo fetch, and did not emit watchdog, LVGL, draw-buffer, or transport-failure lines during the captured run.

### Message + Settings

- Date tested: Friday, August 7, 2026
- Boot-order failure log: `targets/esp32-p4/runtime-logs/20260807-message-settings-batch-hosted-psram.log`
- First fixed runtime log: `targets/esp32-p4/runtime-logs/20260807-message-settings-batch-bootfixed.log`
- Recheck runtime log: `targets/esp32-p4/runtime-logs/20260807-message-settings-batch-recheck.log`
- Message API exercise log: `targets/esp32-p4/runtime-logs/20260807-message-api-exercise.log`
- Measured result:
  - The first message/settings integration exposed a real startup-order bug: the embedded HTTP message server opened sockets before lwIP was initialized and crashed in `tcpip_send_msg_wait_sem(... Invalid mbox)`.
  - The fix moved shared network-runtime bootstrap into `connectivity_prepare_runtime()` and ran it before `message_service_init()`.
  - After the fix, a fresh boot completed cleanly, the message API started on port `80`, display/touch initialized, Wi-Fi reconnected, and the first Open-Meteo fetch succeeded.
  - One longer capture recorded an ESP-Hosted SDIO write failure and host restart at `37.7s` after the first weather update.
  - A fresh reset-and-capture recheck on the same firmware image did not reproduce that transport restart; the board again booted cleanly, reconnected Wi-Fi, and fetched weather successfully.
  - A live API exercise from the Mac verified:
    - `GET /api/message-runtime`
    - `GET /api/message-targets`
    - `POST /api/messages`
    - `POST /api/messages/:id/ack`
    - unread count increment/decrement for the local device
    - important message storage and acknowledgement
  - The API exercise capture logged the expected queue/ack events and did not trigger another SDIO restart.

### Final Analog Polish

- Date tested: Friday, August 7, 2026
- Heavy-pass logs:
  - `targets/esp32-p4/runtime-logs/20260807-analog-polish-boot.log`
  - `targets/esp32-p4/runtime-logs/20260807-analog-polish-recheck.log`
- Final lighter-pass logs:
  - `targets/esp32-p4/runtime-logs/20260807-analog-polish-lite-recheck.log`
  - `targets/esp32-p4/runtime-logs/20260807-analog-polish-lite-message.log`
- Measured result:
  - The first final-polish attempt reproduced ESP-Hosted SDIO restarts after boot.
  - The final lighter variant removed only the extra bright important-indicator asset, kept the second-hand asset and larger center temperature, and completed a full reset boot, Wi-Fi reconnect, TLS validation, first Open-Meteo fetch, and important-message queue/ack exercise without serial transport errors in the captured windows.

## Current Result

- The current product firmware has a stable runtime baseline on hardware for:
  - analog home
  - digital home
  - forecast view
  - message view
  - swipe/touch navigation
  - Wi-Fi/location/setup flow
  - shared night-shift treatment
- Visual parity is not yet considered complete.
- The next required check is a fresh physical-panel comparison of:
  - corrected digital home
  - corrected forecast view
  - current message view
  - current setup/settings flow
- The message/unread/important plumbing is working end to end for the local device and is reflected in the analog/digital edge-indicator state.
- Remaining known product gaps outside the immediate visual-parity pass:
  - setup UI does not yet expose night-shift start/end or message-sharing identity fields
  - Pi-style shared-message hub discovery/proxy behavior is not yet implemented on the ESP32-P4 target, which still reports `sharingMode: "single"` / `role: "single"`
