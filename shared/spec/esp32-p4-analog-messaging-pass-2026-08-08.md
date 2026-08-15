# ESP32-P4 Analog + House Messages Pass

Date: August 8, 2026

## Scope

This pass covered two areas:

1. Analog/default screen correction on the Waveshare ESP32-P4-WIFI6-Touch-LCD-3.4C
2. House Messages audit and parity work for the ESP32-P4 target

## Analog Screen

### Root causes fixed

- Analog hand, center-cap, and edge-indicator assets were generated as `LV_COLOR_FORMAT_ARGB8565`.
- The ESP32-P4 target uses the LVGL 9.5 software renderer, which does not support `ARGB8565`.
- The hour, minute, and second hand assets were also being positioned against the full `800x800` stage instead of the Pi face-local `760x760` coordinate system, which shifted the effective rotation center off the clock center.
- The analog face glow in the generated stage asset was stronger than the Pi reference.

### Implemented analog corrections

- Regenerated alpha-bearing analog assets as `argb8888`.
- Kept the static stage/background as an image.
- Moved the weather artwork into its own layer below the face foreground.
- Rebuilt tick marks and numerals as lightweight live LVGL objects above weather art.
- Repositioned date and temperature content into the Pi face-local coordinate system.
- Reattached hour, minute, and second hands to the centered `760x760` face container so their pivots rotate around the correct center.
- Reduced the stage glow to better match the Pi reference treatment.

### Layering model now used

From back to front:

1. static background / subtle stage gradient
2. weather artwork layer
3. live dial foreground: ticks + numerals
4. date labels
5. temperature / high-low block
6. hour / minute / second hands
7. center cap
8. edge indicator

### Hardware verification completed

Serial logs captured on the physical board:

- `targets/esp32-p4/benchmark-logs/20260808-analog-postflash.log`
- `targets/esp32-p4/benchmark-logs/20260808-final-postflash.log`

Measured final-boot evidence:

- Clean boot
- Display initialized
- Touch initialized
- Wi-Fi reconnected
- First Open-Meteo fetch succeeded
- Hand assets are instantiated with visible opacity
- Hand angle updates continue at runtime

Representative final log evidence:

- `Analog hour hand ... opa=255`
- `Analog minute hand ... opa=255`
- `Analog second hand ... opa=255`
- `Analog hand angles update 1/2/3 ...`

### Remaining analog caveat

- This pass verified the corrected runtime path and final installed firmware on hardware, but it did not include a new post-fix physical photo review from the user.
- The analog code is materially corrected from the previously broken state, but final physical-device aesthetic confirmation still depends on the next real photo check.

## House Messages

### What was already real before this pass

- Local in-memory message queue
- `GET /api/messages`
- `POST /api/messages`
- `POST /api/messages/:id/ack`
- `POST /api/messages/:id/deactivate`
- On-device unread count / important-state UI wiring
- On-device dismiss action acknowledging the active message for the local device

### What was placeholder before this pass

- `POST /api/messages/sync` returned `501`
- `/api/message-runtime` always reported `single`
- No shared-hub discovery runtime
- No shared-client proxying
- No shared-client cached snapshot for the on-device UI
- Setup UI did not expose `messageSharing`, `roomName`, or `deviceId`

### Implemented in this pass

- Pi-style shared message runtime in `targets/esp32-p4/main/message_service.c`
- UDP discovery on port `41234`
- Deterministic hub election by device ID ordering
- Shared hub heartbeat handling
- Shared client failover back to discovery when hub heartbeat goes stale
- Hub/client runtime state exposed through `/api/message-runtime`
- Shared-client proxying for:
  - `GET /api/message-targets`
  - `GET /api/messages`
  - `POST /api/messages`
  - `POST /api/messages/:id/ack`
  - `POST /api/messages/:id/deactivate`
  - `POST /api/messages/sync`
- Shared sync merge behavior for hub mode
- Shared-client cached snapshot used by the on-device message screen and unread edge indicator
- Setup controls added for:
  - room name
  - device ID
  - single/shared message mode

### Final live hardware/API verification completed

Final flashed board runtime:

- `GET http://192.168.1.65/api/message-runtime`
  - returned `sharingMode=single`
  - returned `role=single`
  - returned `hubUrl=http://192.168.1.65:80`

Local message round-trip verified against the final installed firmware:

1. `POST /api/messages`
2. `GET /api/messages?deviceId=clock-esp32-p4`
3. `POST /api/messages/:id/ack`
4. `POST /api/messages/:id/deactivate`
5. `GET /api/messages?deviceId=clock-esp32-p4`

Observed results:

- unread count increased to `1` after posting the self-targeted important message
- the posted message appeared in the device-scoped list
- acknowledge succeeded for `clock-esp32-p4`
- deactivate succeeded
- unread count returned to `0`
- final visible message list returned empty again

### Current real vs remaining gap

Real now:

- local phone-to-this-clock message API flow
- on-device dismiss/ack path
- unread / important message state
- shared-mode backend runtime
- shared hub discovery/election/proxy/sync logic
- setup UI controls required to configure shared mode on-device

Still not fully proven in this pass:

- multi-device live interoperability on physical hardware

Reason:

- only one physical ESP32-P4 clock was available during this pass

That means the shared-mode implementation is now present, built, flashed, and locally testable, but the final proof of multi-clock behavior still requires either:

- a second physical clock, or
- one clock plus a Pi reference instance on the same LAN configured for shared mode

## Notable runtime note

- During one intermediate post-flash capture (`targets/esp32-p4/benchmark-logs/20260808-post-message-runtime.log`), the ESP-Hosted SDIO layer hit an unrecoverable host state and rebooted once during the first weather fetch cycle.
- A subsequent boot on the same firmware recovered cleanly.
- The final installed firmware boot captured in `targets/esp32-p4/benchmark-logs/20260808-final-postflash.log` completed Wi-Fi association and first Open-Meteo fetch without reproducing that restart.
- This was not conclusively tied to the message changes.

## Files changed in this pass

- `targets/esp32-p4/main/app_ui.c`
- `targets/esp32-p4/main/connectivity.c`
- `targets/esp32-p4/main/connectivity.h`
- `targets/esp32-p4/main/message_service.c`
- `targets/esp32-p4/scripts/generate-analog-parity-assets.sh`
- `targets/esp32-p4/scripts/generate-screen-parity-assets.sh`
- `targets/esp32-p4/tools/render_analog_stage_svg.py`
- regenerated analog and shared image assets under `targets/esp32-p4/main/assets/`
