# ESP32-P4 Normal Runtime Rollout

Started on Friday, August 7, 2026.

This note tracks incremental rollout of benchmark-proven runtime settings onto the normal `targets/esp32-p4` product firmware.

## Validation Rules

After each individual runtime change:

- build normal product firmware
- flash physical board on `/dev/cu.usbmodem83201`
- capture serial log
- confirm benchmark mode is not enabled
- confirm clean boot
- confirm stored settings still load
- confirm boot-time config save still works via incremented `boot_count`
- confirm touch stack initializes and the normal UI path starts
- confirm Wi-Fi reconnect
- confirm Open-Meteo weather fetch
- let the clock run for several minutes
- check for watchdogs, LVGL lock errors, draw-buffer allocation failures, heap warnings, and obvious regressions

Touch/UI responsiveness note:

- This environment can verify touch controller registration and clean LVGL/UI runtime behavior from logs.
- It cannot inject physical touch events into the board remotely.
- Any “touch/UI responsive” result in this note is therefore based on booting into the normal UI path with touch detected and no LVGL/runtime fault signals, unless explicitly stated otherwise.

## Results

| Step | Setting | Build | Flash | Boot | Settings load/save | Touch/UI path | Wi-Fi | Weather | Multi-minute run | Issues | Verdict | Log |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0a | Baseline restored firmware backup | pass | pass | pass | pass | inferred pass | pass | pass | pass | none | backup baseline | `targets/esp32-p4/benchmark-logs/20260807-post-restore-boot.log` |
| 0b | Current-source control baseline (original runtime config) | pass | pass | pass | pass | inferred pass | pass | pass | pass | none observed during 4-minute capture | source baseline | `targets/esp32-p4/benchmark-logs/20260807-normal-control-prestep.log` |
| 1 | `CONFIG_COMPILER_OPTIMIZATION_PERF=y` | pass | pass | fail | fail | fail | fail | fail | fail | Repeating startup assert: `sdio_mempool_create sdio_drv.c:258 (buf_mp_g)` before normal app path, Wi-Fi, or UI | failed; reverted | `targets/esp32-p4/benchmark-logs/20260807-normal-step1-compiler-perf.log` |
| 2 | `CONFIG_FREERTOS_HZ=1000` | pass | pass | pass | pass | inferred pass | pass | pass | pass | Removed ESP-Hosted 100 Hz recommendation warning; explicit NTP sync was skipped because the retained clock was already valid | passed; enabled | `targets/esp32-p4/benchmark-logs/20260807-normal-step2-freertos-hz-1000.log` |
| 3 | `LV_OS_FREERTOS` | pass | pass | pass | pass | inferred pass | pass | fail | pass | First Open-Meteo request failed after a mid-handshake Wi-Fi disconnect; later retry succeeded, but this behavior was not present in the control or step-2 runs | failed; reverted | `targets/esp32-p4/benchmark-logs/20260807-normal-step3-lv-os-freertos.log` |
| 4 | `LV_USE_CLIB_MALLOC` | pass | pass | pass | pass | inferred pass | pass | pass | pass | No watchdogs, LVGL lock failures, allocator warnings, draw-buffer failures, or network regressions observed during the 4-minute capture | passed; enabled | `targets/esp32-p4/benchmark-logs/20260807-normal-step4-lv-use-clib-malloc.log` |
| 5 | `LV_DRAW_SW_DRAW_UNIT_CNT=2` | fail | not attempted | not attempted | not attempted | not attempted | not attempted | not attempted | not attempted | LVGL compile-time hard stop: `#error "OS support is required when more than one SW rendering units are enabled"` in `lv_draw_sw.c` while the product baseline remains `LV_OS_NONE=y` | failed; reverted | n/a (build failed before flash) |
| 6 | `CONFIG_CACHE_L2_CACHE_256KB=y` | pass | pass | pass | pass | inferred pass | pass | pass | pass | Internal RAM heap at `4FF40000` dropped from `384 KiB` to `256 KiB` as expected for the larger L2 cache reservation; no runtime regressions observed | passed; enabled | `targets/esp32-p4/benchmark-logs/20260807-normal-step6-cache-l2-256kb.log` |
| 7 | `CONFIG_CACHE_L2_CACHE_LINE_128B=y` | pass | pass | pass | pass | inferred pass | pass | pass | pass | No watchdogs, LVGL lock failures, allocator warnings, draw-buffer failures, or network regressions observed during the 4-minute capture | passed; enabled | `targets/esp32-p4/benchmark-logs/20260807-normal-step7-cache-l2-line-128b.log` |
| 8 | `CONFIG_SPIRAM_XIP_FROM_PSRAM=y` | pass | pass | pass | pass | inferred pass | pass | pass | pass | Boot confirmed both `.rodata xip on psram` and `.text xip on psram`; no watchdogs, LVGL lock failures, allocator warnings, draw-buffer failures, or network regressions observed during the 4-minute capture | passed; enabled | `targets/esp32-p4/benchmark-logs/20260807-normal-step8-spiram-xip.log` |

## Notes

- A current-source control build at the original runtime configuration was required because the initial comparison target on hardware was the restored full-flash backup image, not a freshly built normal firmware from the current source tree.
- The current-source control baseline booted cleanly, reconnected Wi-Fi, synchronized time, fetched Open-Meteo weather, and remained quiet for the remainder of the 4-minute capture.
- Step 1 is therefore a real regression attributable to enabling `CONFIG_COMPILER_OPTIMIZATION_PERF=y` in the normal product target.
- The step-1 failure occurs immediately after the ESP-Hosted warning about `CONFIG_FREERTOS_HZ is 100`, with an assert in `targets/esp32-p4/managed_components/espressif__esp_hosted/host/drivers/transport/sdio/sdio_drv.c:258`.
- Measured fact: the step-1 image never reaches `main_task: Started on CPU0`, `Calling app_main()`, `Display ready`, Wi-Fi association, or weather fetch.
- Inference: `hosted_mempool_create(&config)` is failing under the `-O3` normal build, likely because the optimization change alters the early memory layout or allocation behavior used by ESP-Hosted's SDIO transport pool. This inference is based on the assert site and the absence of later normal-app logs.
- Step 2 effective config value was `CONFIG_FREERTOS_HZ=1000` in both `sdkconfig` and generated `build/config/sdkconfig.cmake`.
- Step 2 remained stable through clean boot, ESP-Hosted bring-up, display/touch initialization, Wi-Fi reconnect, and Open-Meteo fetch, with no watchdogs, LVGL lock failures, allocator warnings, or SDIO regressions observed in the 4-minute capture.
- Measurable Step 2 difference versus the control baseline: the ESP-Hosted warning `CONFIG_FREERTOS_HZ is 100, ESP-Hosted recommended 1000` disappeared.
- Step 2 did not emit the explicit `Synchronizing clock` / `Network time synchronization completed` logs seen in the control baseline. This matches the current code path because `connectivity_sync_time()` returns immediately when `time(NULL)` is already above `VALID_CLOCK_EPOCH`, which was true on this run.
- Step 3 effective config value was `CONFIG_LV_OS_FREERTOS=y` with `CONFIG_LV_OS_NONE` unset in both `sdkconfig` and generated `build/config/sdkconfig.cmake`.
- Step 3 booted cleanly, initialized ESP-Hosted, display, and touch, and entered the normal UI path without watchdogs, LVGL lock failures, allocator warnings, or draw-buffer failures during the 4-minute capture.
- Measured Step 3 regression versus both the current-source control baseline and Step 2: the first weather fetch attempted at `I (15989)` was interrupted by `RPC_WRAP: ESP Event: Station mode: Disconnected`, followed by `mbedtls_ssl_handshake returned -0x004C`, `HTTP_CLIENT: Connection failed, sock < 0`, and `Weather refresh failed: ESP_ERR_HTTP_CONNECT`.
- The same Step 3 run later re-associated Wi-Fi at `I (34309)` and successfully fetched weather on the next scheduled refresh at `I (51718)`, so the regression is reliability on the initial fetch path rather than a total loss of weather support.
- The control baseline and Step 2 logs both completed their first weather request without any disconnect/reconnect cycle, so the Step 3 failure is attributed to enabling `LV_OS_FREERTOS` on the normal product target.
- `LV_OS_FREERTOS` was reverted in `sdkconfig` and `sdkconfig.defaults`; the product baseline remains `CONFIG_FREERTOS_HZ=1000` enabled with `LV_OS_NONE=y`.
- Restore verification after the revert was captured in `targets/esp32-p4/benchmark-logs/20260807-normal-step3-revert-restore.log`, which showed clean boot, touch registration, Wi-Fi association, and a successful first Open-Meteo fetch on the restored Step 2 baseline.
- Step 4 effective config value was `CONFIG_LV_USE_CLIB_MALLOC=y` with `CONFIG_LV_USE_BUILTIN_MALLOC` unset in both `sdkconfig` and generated `build/config/sdkconfig.cmake`.
- Step 4 remained stable through clean boot, ESP-Hosted bring-up, display/touch initialization, Wi-Fi reconnect, and first-attempt Open-Meteo fetch, with no watchdogs, LVGL lock failures, allocator warnings, draw-buffer failures, or network regressions observed in the 4-minute capture.
- Step 4 retained the original `384 KiB` internal RAM region at `heap_init: At 4FF40000 len 00060000 (384 KiB): RAM`, so this allocator change did not alter the cache reservation layout by itself.
- Step 5 attempted `CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=2` on top of the known-good baseline, but the normal product build failed before flash with `#error "OS support is required when more than one SW rendering units are enabled"` from `targets/esp32-p4/managed_components/lvgl__lvgl/src/draw/sw/lv_draw_sw.c:34`.
- Step 5 is therefore a measured product incompatibility while `LV_OS_NONE=y` remains required; it was reverted immediately and was not flashed to hardware.
- Step 6 effective config value was `CONFIG_CACHE_L2_CACHE_256KB=y` in both `sdkconfig` and generated `build/config/sdkconfig.cmake`.
- Step 6 booted cleanly, reconnected Wi-Fi, completed the first Open-Meteo fetch, and remained quiet for the remainder of the 4-minute capture, with no watchdogs, LVGL lock failures, allocator warnings, draw-buffer failures, or ESP-Hosted regressions observed.
- Measured Step 6 difference versus Step 4: the internal RAM region at `4FF40000` dropped from `00060000 (384 KiB)` to `00040000 (256 KiB)`, which matches the expected cost of reserving a larger `256 KiB` L2 cache.
- Step 7 effective config value was `CONFIG_CACHE_L2_CACHE_LINE_128B=y` in both `sdkconfig` and generated `build/config/sdkconfig.cmake`, on top of the already-enabled `CONFIG_CACHE_L2_CACHE_256KB=y`.
- Step 7 remained stable through clean boot, ESP-Hosted bring-up, display/touch initialization, Wi-Fi reconnect, and first-attempt Open-Meteo fetch, with no watchdogs, LVGL lock failures, allocator warnings, draw-buffer failures, or network regressions observed in the 4-minute capture.
- Step 8 effective config values were `CONFIG_SPIRAM_XIP_FROM_PSRAM=y`, `CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y`, `CONFIG_SPIRAM_RODATA=y`, and `CONFIG_SPIRAM_FLASH_LOAD_TO_PSRAM=y` in generated `build/config/sdkconfig.cmake`.
- Step 8 booted cleanly, logged both `mmu_psram: .rodata xip on psram` and `mmu_psram: .text xip on psram`, reconnected Wi-Fi, completed the first Open-Meteo fetch, and remained quiet for the remainder of the 4-minute capture with no watchdogs, LVGL lock failures, allocator warnings, draw-buffer failures, or network regressions observed.
- Across Steps 4, 6, 7, and 8, the normal product firmware incremented retained `boot_count` cleanly from `613` to `619`, confirming settings load/save continuity across the surviving runtime changes.
- The current surviving product runtime baseline after this rollout is: `CONFIG_FREERTOS_HZ=1000`, `CONFIG_LV_USE_CLIB_MALLOC=y`, `CONFIG_CACHE_L2_CACHE_256KB=y`, `CONFIG_CACHE_L2_CACHE_LINE_128B=y`, `CONFIG_SPIRAM_XIP_FROM_PSRAM=y`, `CONFIG_LV_OS_NONE=y`, `CONFIG_COMPILER_OPTIMIZATION_PERF` unset, and `CONFIG_LV_OS_FREERTOS` unset.

## Clang / Activation Investigation

- No repo helper script in `targets/esp32-p4` invokes `install.sh`, `idf_tools.py install`, or any ESP-IDF bootstrap action. The local wrapper at `targets/esp32-p4/scripts/activate-idf.sh` only sources `$IDF_ROOT/export.sh`.
- The repeated activation chatter comes from ESP-IDF itself. Sourcing `export.sh` runs `.esp-idf/esp-idf-v5.5.5/tools/export_utils/activate_venv.py`, which in turn runs `idf_tools.py check-python-dependencies`, `idf_tools.py export`, and `idf_tools.py uninstall --dry-run` every time a fresh shell activates the environment.
- Measured conclusion: normal `source targets/esp32-p4/scripts/activate-idf.sh && idf.py ...` build/flash/monitor commands are reusing the already-installed ESP-IDF toolchain. They are re-validating the environment on each fresh shell activation, but they are not reinstalling ESP-IDF or Clang.
- The `esp-clang` tool in `.esp-idf/esp-idf-v5.5.5/tools/tools.json` is marked `install: on_request`, so it is not part of the mandatory normal product build/flash path.
- The earlier Clang-related failure was reproducible only on the nonstandard direct `idf_tools.py export` path, where ESP-IDF treated `/usr/bin/clang` as `esp-clang` on `PATH` and failed its probe with a non-zero exit. That failure did not reproduce on the normal `activate-idf.sh` plus `idf.py` workflow used for the runtime rollout.
- No repo-side workflow change was applied in this pass because there is no clear low-risk defect in the normal build/flash/monitor scripts themselves. If we want to reduce the repeated activation overhead later, the safe direction is to reuse a live ESP-IDF shell session during local iteration rather than patching upstream ESP-IDF activation behavior blindly.
