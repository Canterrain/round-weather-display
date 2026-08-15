# ESP32-P4 Analog Parity Checklist

Started on Friday, August 7, 2026.

Reference implementation:

- Raspberry Pi target in `targets/pi/public/index.html`
- styling in `targets/pi/public/style.css`
- clock behavior in `targets/pi/public/renderer/clock.js`
- weather behavior in `targets/pi/public/renderer/weather.js`
- message edge behavior in `targets/pi/public/renderer/messages.js`

Goal:

- Match the Raspberry Pi analog/default home screen as closely as practical from the user-facing perspective.
- Preserve stable ESP32-P4 runtime behavior by keeping decorative layers static where possible and updating only truly live elements.

## Reference Inventory

| Visual group | Pi reference behavior / appearance | Current ESP32 implementation | Match | Stable on hardware | Invisible implementation difference |
| --- | --- | --- | --- | --- | --- |
| Stage background | Full-screen dark radial backdrop on `body`; optional day/night photo background if matching assets exist; night mode dims the whole stage via `filter: brightness(0.55)` | Static day/night raster stage assets matching the Pi analog face composition | partial | yes | raster image swap instead of CSS + DOM layers |
| Clock face / outer rings | `760x760` circular face with deep blue radial gradient, inset edge line, soft inner glow, and outer drop shadow | Static raster face asset with matching circular geometry, ring, glow, and shadow | yes | yes | raster image instead of CSS gradients/shadows |
| Day label | `Thursday`, top-centered at `y=130`, `40px`, medium weight, muted white | Live `lv_label` at the Pi top position with muted styling | partial | yes | native font rendering instead of browser text |
| Date label | `Mar 19`, under day at `y=178`, `30px`, muted white | Live `lv_label` under the day label with larger muted styling | partial | yes | native font rendering instead of browser text |
| Weather artwork | Large `500x500` condition icon behind the clock center, centered around `50%/45%`, opacity `0.36`, soft shadow | Live shared weather icon image using the same shared icon family and Pi placement | partial | yes | shared SVGs are pre-rasterized into LVGL assets instead of browser DOM images |
| Tick marks | 60 ticks around the dial; minor ticks `2px`, major every 5 minutes `4px`, quarter ticks slightly longer/brighter | Baked into static raster face asset | yes | yes | raster image instead of live vector tick primitives |
| Numerals | 12 numerals, Avenir-like, major numerals at `12/3/6/9` larger (`48px`), others `34px`, centered on ring | Baked into static raster face asset | yes | yes | raster image instead of live text around dial |
| Hour hand | Custom faceted polygon, warm gray, shadowed, rotated around center | Rotated Pi-derived shaded hand asset | yes | yes | pre-rasterized hand image instead of browser SVG path |
| Minute hand | Custom faceted polygon, cooler gray-blue, shadowed, longer than hour hand | Rotated Pi-derived shaded hand asset | yes | yes | pre-rasterized hand image instead of browser SVG path |
| Second hand | Slim translucent pale hand with counterweight | Rotated Pi-derived second-hand asset using the same SVG geometry and counterweight silhouette | yes | yes | pre-rasterized image instead of browser SVG path |
| Center cap | Two concentric circles with metallic radial gradient, stroke, and shadow | Pi-style metallic center cap image layered above the hands | yes | yes | pre-rasterized cap image instead of SVG gradient circles |
| Center temperature block | Floating center-aligned stack near `y=472`; large current temp (`104px`), high/low below (`30px`) | Floating live temperature/high-low stack with Pi placement and transform-scaled temperature typography | yes | yes | live LVGL text is scaled instead of rendered with the browser font stack |
| Current condition text | Analog screen does not show summary text in the center block; summary is implied by the icon | Analog screen no longer shows a center summary label | yes | yes | none |
| Message edge indicator | Circular conic/glow wedge around right perimeter; brighter and animated when important | Pi-style edge-indicator asset with live unread wiring and a pulsing important state on the analog and digital home views | partial | yes | pulsing base raster wedge instead of browser conic-gradient + blur |
| Status lines | Bottom-centered two-line stack, hidden when healthy, muted white text with soft shadow | Narrower bottom-centered stack with Pi-style spacing and muted text styling | partial | yes | same live status data without browser text-shadow |
| Night shift | Red-toned background/face; home weather UI hidden; hands/ticks/text recolored; overall dimmer appearance | Analog view swaps to a red-toned night stage, hides weather artwork/temp block, and recolors dial copy and hands | partial | yes | stage swap and recolor instead of browser-wide filters/classes |
| Spacing / layering | Weather icon behind dial and hands; hands above icon; temp block floats over icon; indicator above face; status below face | Analog view now follows the Pi layer stack closely with static art kept static and live data layered above it | partial | yes | LVGL image/label layering instead of DOM/CSS stacking |

## Implementation Order

1. Static analog backdrop and face geometry
2. Day/date typography positioning
3. Dynamic weather artwork behind the dial
4. Tick/numeral parity if not already baked into the face asset
5. Faceted hour/minute/second hands plus center cap
6. Floating temperature block and analog text cleanup
7. Message edge indicator parity
8. Status line styling
9. Night-shift parity

## Slice 1 Validation

- Date tested: Friday, August 7, 2026
- Git commit baseline: `aa60dba`
- Hardware log: `targets/esp32-p4/runtime-logs/20260807-analog-slice1.log`
- Measured result:
  - Clean boot completed.
  - Display and touch initialized successfully.
  - ESP-Hosted transport initialized successfully.
  - Wi-Fi reconnected and obtained `192.168.1.65`.
  - First Open-Meteo fetch succeeded with `Live weather updated (88°, H:90° L:72°)`.
  - No watchdog, LVGL lock, heap-allocation, or draw-buffer failures appeared during the captured runtime window.
- Known non-parity after slice 1:
  - No analog weather artwork yet.
  - Hands are still simple line primitives.
  - Center info block, edge indicator, status styling, and night mode remain pre-parity.

## Slice 2 Validation

- Date tested: Friday, August 7, 2026
- Git commit baseline: `aa60dba`
- Hardware log: `targets/esp32-p4/runtime-logs/20260807-analog-slice2-weather.log`
- Measured result:
  - Clean boot completed with the larger icon asset set.
  - Display and touch initialized successfully.
  - ESP-Hosted transport initialized successfully.
  - Wi-Fi reconnected and obtained `192.168.1.65`.
  - First Open-Meteo fetch succeeded with `Live weather updated (89°, H:90° L:72°)`.
  - No watchdog, LVGL lock, heap-allocation, or draw-buffer failures appeared during the captured runtime window.
  - App binary grew to `0x5396b0`, still leaving `35%` of the factory app partition free.
- Known non-parity after slice 2:
  - Hour, minute, and second hands are still simple line primitives rather than the Pi's faceted hand artwork.
  - The Pi-style metallic center cap is still missing.
  - The center temperature block still uses the older solid card treatment.
  - The message edge indicator, refined status styling, and night mode remain pre-parity.

## Slice 3 Validation

- Date tested: Friday, August 7, 2026
- Git commit baseline: `aa60dba`
- Hardware log: `targets/esp32-p4/runtime-logs/20260807-analog-slice3-hands.log`
- Restored stable log: `targets/esp32-p4/runtime-logs/20260807-analog-slice2-restored.log`
- Slice goal:
  - Replace the analog hour and minute lines with Pi-derived hand artwork.
  - Add the Pi-style center cap image.
  - Start moving the center temperature block toward the Pi floating layout.
- Measured result:
  - The build flashed successfully, but a task watchdog fired before `Display ready`.
  - The watchdog happened at `I (7343)` while CPU 0 was still in early LVGL scene setup, before Wi-Fi association and before the first live weather request.
  - Resolved PC values pointed into LVGL object-tree / effect walking, with `MEPC=0x4803aa96` resolving to `blur_walk_cb` and `RA=0x4803df74` resolving to `walk_core`.
  - Because this was a real product regression, the hand/cap runtime integration was reverted and the previously stable slice 2 firmware was reflashed.
  - The restored slice 2 boot completed cleanly, reconnected Wi-Fi, and fetched live weather again.
- Measured conclusion:
  - The first parity attempt for custom hand/cap artwork is not yet hardware-stable on the current product baseline.
  - The failure point is tied to the additional LVGL scene/effect work introduced by that slice, not to transport, weather, or persisted settings.
- Current stopping point:
  - Slice 2 remains the active hardware baseline for the analog parity pass.
  - A future retry should keep the user-facing Pi hand appearance, but reproduce it with a cheaper embedded composition path than this first image-object approach.

## Parity Lab Validation

- Date tested: Friday, August 7, 2026
- Raw log: `targets/esp32-p4/runtime-logs/20260807-analog-parity-lab-hardened-raw.log`
- Measured result:
  - The parity lab completed all 15 staged cases from `baseline_slice2` through `full_night_shaded` in one firmware build and one flash.
  - No draw-buffer allocation failures, LVGL lock failures, refresh failures, object-allocation failures, stall warnings, or watchdog-triggered supervisor restarts occurred in the hardened full sweep.
  - The full cumulative day and night analog compositions were both stable, which cleared the original assumption that Pi-style hand/cap assets were inherently too expensive on the tuned runtime baseline.

## Normal Product Validation

- Date tested: Friday, August 7, 2026
- First raw log: `targets/esp32-p4/runtime-logs/20260807-analog-product-parity.log`
- Final raw log: `targets/esp32-p4/runtime-logs/20260807-analog-product-parity-yielded.log`
- Measured result:
  - The first normal-product integration booted, reconnected Wi-Fi, and fetched live weather, but it emitted a one-time startup task watchdog before `Display ready`.
  - Resolved watchdog PC values pointed into LVGL font glyph descriptor lookup:
    - `0x480797a4 -> lv_font_get_glyph_dsc_fmt_txt`
    - `0x4807975a -> lv_font_get_glyph_dsc_fmt_txt`
  - The watchdog happened during synchronous initial UI construction on CPU0, not during steady-state analog rendering.
  - A follow-up fix inserted short scheduler yields between major boot-time UI build phases.
  - The final normal-product boot completed without any watchdog warnings.
  - The final normal-product run showed:
    - clean boot and display/touch initialization
    - ESP-Hosted startup and Wi-Fi reconnect to `TheGoodPlace`
    - IP acquisition at `192.168.1.65`
    - first Open-Meteo fetch success with `Live weather updated (87°, H:89° L:72°)`
    - no additional watchdogs, LVGL lock failures, or draw-buffer allocation failures during the extended capture window

## Current Parity Result

- The ESP32-P4 analog/default screen now uses the proven Pi-style hand assets, metallic center cap, floating temperature layout, refined status styling, and analog-specific night treatment on the normal product firmware.
- The analog/default screen is hardware-stable on the current product runtime baseline after the boot-time UI construction was split with short task yields.

## Final Polish Validation

- Date tested: Friday, August 7, 2026
- First heavier-pass boot log: `targets/esp32-p4/runtime-logs/20260807-analog-polish-boot.log`
- First heavier-pass recheck log: `targets/esp32-p4/runtime-logs/20260807-analog-polish-recheck.log`
- Final lighter-pass stable log: `targets/esp32-p4/runtime-logs/20260807-analog-polish-lite-recheck.log`
- Final lighter-pass message exercise log: `targets/esp32-p4/runtime-logs/20260807-analog-polish-lite-message.log`
- Implementation result:
  - The analog second hand now uses a Pi-derived raster asset instead of the earlier `lv_line` primitive.
  - The analog center temperature label now uses LVGL transform scaling so its visual weight tracks the Pi reference more closely without introducing a custom font pipeline.
  - The unread/important edge indicator now stays Pi-red in normal mode and pulses when an important message is active.
- Measured result:
  - The first final-polish pass added a second full-screen important-indicator asset and reproduced ESP-Hosted SDIO transport restarts in two captures.
  - The lighter follow-up build removed that extra indicator asset while keeping the second-hand asset, larger center temperature, and pulsing important-state behavior on the existing wedge.
  - The lighter build completed a clean reset boot, display/touch initialization, Wi-Fi reconnect, TLS validation, and first Open-Meteo fetch with no `Failed to send data`, `Unrecoverable host sdio state`, or `Restarting host` lines during the full 90-second capture window.
  - A live important-message exercise on the lighter build queued and acknowledged a message successfully without triggering transport or UI errors.
- Measured conclusion:
  - The accepted stable analog parity baseline now includes the second-hand artwork, larger center temperature typography, and pulsing important-message behavior.
  - A dedicated brighter important-indicator asset is not part of the product baseline because it correlated with repeated transport restarts, while the lighter pulsing-base-indicator approach remained stable in the measured run.

## Current Result

- The ESP32-P4 analog/default screen now includes the intended second-hand artwork, larger center temperature emphasis, and live pulsing important-message behavior on the stable lighter product build.
- The accepted stable analog parity baseline remains front-end aligned with the Pi reference while keeping the ESP32 implementation on the proven runtime path.
- The remaining analog differences are implementation-level rather than product-blocking:
  - the important-state wedge uses the existing Pi-style raster asset plus pulse instead of a separate brighter full-screen asset because the heavier asset destabilized the transport stack in measurement
