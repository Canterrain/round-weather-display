# ESP32-P4 Benchmark Results

Measured on Friday, August 7, 2026, on the physical Waveshare `ESP32-P4-WIFI6-Touch-LCD-3.4C`.

## Scope

This report records two benchmark configurations for the dedicated clock benchmark build:

- baseline benchmark configuration
- tuned benchmark configuration

The stable product UI was not modified during this pass.

## Artifacts

- Git commit:
  `aa60dba194fb7a35b7134c9c6b2825e00fc7b601`
- Serial port:
  `/dev/cu.usbmodem83201`
- Baseline raw log:
  `targets/esp32-p4/benchmark-logs/20260807-baseline-raw.log`
- Tuned raw log:
  `targets/esp32-p4/benchmark-logs/20260807-tuned-raw.log`
- Baseline ELF:
  `targets/esp32-p4/build-benchmark/round_weather_display_esp32_p4.elf`
- Tuned ELF:
  `targets/esp32-p4/build-benchmark-tuned/round_weather_display_esp32_p4.elf`
- Stable full-flash backup:
  `targets/esp32-p4/backups/20260807-benchmark-preflash/flash-32mb.bin`

## Exact Commands Used

```bash
source /Users/josh/Documents/round-weather-display/targets/esp32-p4/scripts/activate-idf.sh >/dev/null && /Users/josh/Documents/round-weather-display/targets/esp32-p4/scripts/flash-benchmark.sh /dev/cu.usbmodem83201

source /Users/josh/Documents/round-weather-display/targets/esp32-p4/scripts/activate-idf.sh >/dev/null && /Users/josh/Documents/round-weather-display/targets/esp32-p4/scripts/capture-serial-log.sh /dev/cu.usbmodem83201 /Users/josh/Documents/round-weather-display/targets/esp32-p4/benchmark-logs/20260807-baseline-raw.log 210

source /Users/josh/Documents/round-weather-display/targets/esp32-p4/scripts/activate-idf.sh >/dev/null && /Users/josh/Documents/round-weather-display/targets/esp32-p4/scripts/build-benchmark-tuned.sh

source /Users/josh/Documents/round-weather-display/targets/esp32-p4/scripts/activate-idf.sh >/dev/null && /Users/josh/Documents/round-weather-display/targets/esp32-p4/scripts/flash-benchmark-tuned.sh /dev/cu.usbmodem83201

source /Users/josh/Documents/round-weather-display/targets/esp32-p4/scripts/activate-idf.sh >/dev/null && python3 /Users/josh/Documents/round-weather-display/targets/esp32-p4/tools/capture_serial.py --port /dev/cu.usbmodem83201 --baud 115200 --output /Users/josh/Documents/round-weather-display/targets/esp32-p4/benchmark-logs/20260807-tuned-raw.log --duration 150 --reset-before-capture --settle-seconds 2
```

The stable restore command is recorded in the restore section at the end of this report.

## Effective Configuration Comparison

These values were verified from the generated build configs:

- baseline:
  `targets/esp32-p4/build-benchmark/config/sdkconfig.cmake`
- tuned:
  `targets/esp32-p4/build-benchmark-tuned/config/sdkconfig.cmake`

Shared runtime constants that did not change between the two benchmark builds:

- `CONFIG_RWD_ENABLE_CLOCK_BENCHMARK=y`
- `CONFIG_BSP_LCD_DPI_BUFFER_NUMS=3`
- `CONFIG_BSP_LCD_COLOR_FORMAT_RGB565=y`
- board display profile `buffer_height = 50`
- board display profile `tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL`

| Setting | Baseline effective value | Tuned effective value |
| --- | --- | --- |
| Compiler optimization | `CONFIG_COMPILER_OPTIMIZATION_DEBUG=y` | `CONFIG_COMPILER_OPTIMIZATION_PERF=y` |
| FreeRTOS tick rate | `CONFIG_FREERTOS_HZ=100` | `CONFIG_FREERTOS_HZ=1000` |
| LVGL OS mode | `CONFIG_LV_OS_NONE=y` | `CONFIG_LV_OS_FREERTOS=y` |
| LVGL allocator | builtin LVGL heap | `CONFIG_LV_USE_CLIB_MALLOC=y` |
| LVGL builtin heap size | `CONFIG_LV_MEM_SIZE_KILOBYTES=64` | not used |
| LVGL pool expand size | `CONFIG_LV_MEM_POOL_EXPAND_SIZE_KILOBYTES=0` | not used |
| LVGL SW draw units | `CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=1` | `CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=2` |
| PSRAM speed | `CONFIG_SPIRAM_SPEED=200` | `CONFIG_SPIRAM_SPEED=200` |
| Fetch instructions from PSRAM | not set | `CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y` |
| Place rodata in PSRAM | not set | `CONFIG_SPIRAM_RODATA=y` |
| PSRAM XIP | not set | `CONFIG_SPIRAM_XIP_FROM_PSRAM=y` |
| Load flash code/data to PSRAM | not set | `CONFIG_SPIRAM_FLASH_LOAD_TO_PSRAM=y` |
| L2 cache size | `0x20000` | `0x40000` |
| L2 cache line size | `64` | `128` |
| Panel framebuffer count | `3` | `3` |
| Panel pixel format | `RGB565` | `RGB565` |
| Draw buffer height | `50` | `50` |

## Baseline Measurements

### Completed stages

| Pass | Stage | Build ms | Activation ms | Steady avg ms | Steady p95 ms | Steady max ms | Internal min | PSRAM min | LVGL min | LVGL frag pct | Draw buf fails | Lock fails | Stall warnings |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| cumulative | baseline | 0.02 | 358.76 | 0.02 | 0.02 | 0.02 | 265243 | 29704584 | 56224 | 1 | 0 | 0 | 3 |
| cumulative | day_date | 2.39 | 17.77 | 5.25 | 12.31 | 12.31 | 274307 | 29704584 | 55688 | 1 | 0 | 0 | 0 |
| cumulative | ring | 1.42 | 122.32 | 13.26 | 20.71 | 20.71 | 274307 | 29704584 | 55288 | 1 | 0 | 0 | 0 |

### Baseline failure point

- Last completed stage:
  cumulative `ring`
- First failing stage:
  cumulative `hour_minute_hands`
- `BENCH_STAGE_SUMMARY` for `ring` was emitted before the watchdog.
- No `BENCH_STAGE_START` for `hour_minute_hands` was emitted.

Measured interpretation:

- The watchdog did not happen while building or steadily redrawing the `ring` stage.
- It happened after `benchmark_advance()` closed the `ring` stage and before `benchmark_activate_stage()` could finish the next stage and emit its `BENCH_STAGE_START`.
- Because `BENCH_STAGE_START` is logged only after `benchmark_apply_stage_scene()`, `benchmark_update_clock_copy()`, and `benchmark_force_refresh()`, the failing work was most likely inside `hour_minute_hands` object creation and/or its first forced refresh.

### Baseline redraw behavior

- The scene was cumulative. By design, `ring` still included all earlier objects.
- Even though `ring` is labeled `static_after_create`, it was still being invalidated once per second.
- That repeated refresh came from `benchmark_clock_timer_cb()` incrementing the benchmark clock every second and `benchmark_update_clock_copy()` updating the day/date labels even before the second-hand phase.
- The `ring` stage therefore was not truly motionless in steady state.

### Baseline watchdog register dumps and resolved PCs

ESP-IDF did not emit a symbolic backtrace because `CONFIG_ESP_SYSTEM_USE_FRAME_POINTER` was disabled. The raw log preserves the full register dumps. The resolved `MEPC` and `RA` addresses were:

| Log time | MEPC | RA | Resolved MEPC | Resolved RA |
| --- | --- | --- | --- | --- |
| `24549 ms` | `0x4003a810` | `0x4003a8cc` | `lv_draw_dispatch_layer` at `lv_draw.c:244` | `lv_draw_dispatch` at `lv_draw.c:222` |
| `29549 ms` | `0x4005389c` | `0x4005217c` | `lv_tlsf_free` at `lv_tlsf.c:1165` | `lv_free_core` at `lv_mem_core_builtin.c:189` |
| `34549 ms` | `0x400413a4` | `0x4004137c` | `dispatch` at `lv_draw_sw.c:326` | `dispatch` at `lv_draw_sw.c:324` |
| `39549 ms` | `0x4005303a` | `0x400530bc` | `block_is_last` at `lv_tlsf.c:395` | `block_next` at `lv_tlsf.c:457` |
| `44549 ms` | `0x4003adec` | `0x4003b020` | `lv_draw_buf_width_to_stride` at `lv_draw_buf.c:102` | `lv_draw_buf_create_ex` at `lv_draw_buf.c:265` |
| `49549 ms` | `0x400538c6` | `0x400538c4` | `lv_tlsf_free` at `lv_tlsf.c:1171` | `lv_tlsf_free` at `lv_tlsf.c:1170` |

Measured interpretation:

- The resolved PCs point at LVGL draw dispatch, software draw-task dispatch, draw-buffer creation, and the builtin TLSF allocator free/merge path.
- The resolved PCs do not directly land inside box-shadow rasterization, arc drawing, PPA blending, or framebuffer flush callbacks.
- That means allocator churn and draw-buffer lifecycle activity are visible in the failure path, but the exact draw primitive being processed at the time of the stall is still unknown from the available registers.

## Tuned Measurements

### Cumulative pass

| Stage | Build ms | Activation ms | Steady avg ms | Steady p95 ms | Steady max ms | Internal min | PSRAM min | LVGL heap | Draw buf fails | Lock fails | WDT |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| baseline | 0.01 | 338.03 | 0.16 | 1.63 | 12.35 | 169071 | 28517716 | n/a, CLIB allocator | 0 | 0 | none |
| day_date | 1.60 | 18.13 | 0.17 | 1.63 | 11.96 | 177439 | 28517716 | n/a, CLIB allocator | 0 | 0 | none |
| ring | 1.04 | 143.30 | 0.45 | 4.92 | 23.67 | 176995 | 28517716 | n/a, CLIB allocator | 0 | 0 | none |
| hour_minute_hands | 1.04 | 165.25 | 1.22 | 4.96 | 63.73 | 176371 | 28517716 | n/a, CLIB allocator | 0 | 0 | none |
| second_hand | 1.04 | 164.94 | 1.25 | 7.58 | 60.59 | 176119 | 28517716 | n/a, CLIB allocator | 0 | 0 | none |
| center_disc | 0.39 | 174.30 | 1.35 | 7.65 | 64.83 | 175911 | 28517716 | n/a, CLIB allocator | 0 | 0 | none |
| weather_copy | 1.62 | 177.79 | 1.35 | 7.62 | 65.55 | 175095 | 28517716 | n/a, CLIB allocator | 0 | 0 | none |
| edge_indicator | 0.22 | 177.85 | 1.35 | 7.60 | 65.83 | 174935 | 28517716 | n/a, CLIB allocator | 0 | 0 | none |

### Isolated pass

| Stage | Build ms | Activation ms | Steady avg ms | Steady p95 ms | Steady max ms | Internal min | PSRAM min | LVGL heap | Draw buf fails | Lock fails | WDT |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| baseline | 0.00 | 18.42 | 0.15 | 1.62 | 12.40 | 177919 | 28517716 | n/a, CLIB allocator | 0 | 0 | none |
| day_date | 3.20 | 18.05 | 0.17 | 1.52 | 11.95 | 177399 | 28517716 | n/a, CLIB allocator | 0 | 0 | none |
| ring | 0.36 | 144.03 | 0.32 | 4.87 | 15.19 | 177503 | 28517716 | n/a, CLIB allocator | 0 | 0 | none |
| hour_minute_hands | 0.89 | 166.43 | 1.07 | 7.05 | 50.85 | 176871 | 28517716 | n/a, CLIB allocator | 0 | 0 | none |
| second_hand | 1.36 | 167.32 | 1.07 | 6.99 | 50.01 | 176627 | 28517716 | n/a, CLIB allocator | 0 | 0 | none |
| center_disc | 0.49 | 24.53 | 0.14 | 1.51 | 11.95 | 177711 | 28517716 | n/a, CLIB allocator | 0 | 0 | none |
| weather_copy | 1.07 | 28.78 | 0.15 | 1.64 | 12.14 | 176899 | 28517716 | n/a, CLIB allocator | 0 | 0 | none |
| edge_indicator | 0.17 | 19.83 | 0.15 | 1.63 | 12.09 | 177759 | 28517716 | n/a, CLIB allocator | 0 | 0 | none |

### Tuned completion and failure status

- `BENCH_RUN_COMPLETE` was emitted.
- No `task_wdt` lines appeared in the tuned raw log.
- No draw-buffer allocation failures were recorded.
- No LVGL lock failures were recorded.
- No refresh failures were recorded.
- No object allocation failures were recorded.

## Measured Comparison

### Common cumulative stages

| Stage | Baseline p95 ms | Tuned p95 ms | Baseline max ms | Tuned max ms | Notes |
| --- | --- | --- | --- | --- | --- |
| baseline | `0.02` | `1.63` | `0.02` | `12.35` | Not directly schedule-comparable. The baseline run only emitted one steady frame here, while the tuned run emitted hundreds of refreshes. |
| day_date | `12.31` | `1.63` | `12.31` | `11.96` | Tuned steady-state is materially lower and stable. |
| ring | `20.71` | `4.92` | `20.71` | `23.67` | Tuned p95 improved substantially, and the run advanced past the original failure boundary. |

### First failing stage

- baseline:
  cumulative `hour_minute_hands`, before stage start was logged
- tuned:
  none, full cumulative and isolated passes completed

### Largest measured visual spike

- In the tuned cumulative pass, the largest activation frame time was `177.85 ms` on `edge_indicator`.
- The largest activation spike near the original failure boundary was `165.25 ms` on cumulative `hour_minute_hands`.
- The baseline configuration watchdog hit before `hour_minute_hands` could finish activation.

### Second-hand impact

Measured result:

- cumulative `hour_minute_hands` p95:
  `4.96 ms`
- cumulative `second_hand` p95:
  `7.58 ms`
- isolated `hour_minute_hands` p95:
  `7.05 ms`
- isolated `second_hand` p95:
  `6.99 ms`

Interpretation:

- The second hand did not introduce a unique catastrophic periodic spike in the tuned build.
- It adds some steady redraw cost in the cumulative pass, but not enough to destabilize the run.

### Allocator churn

Measured result:

- baseline watchdog PCs included LVGL builtin TLSF free/merge and draw-buffer creation paths
- tuned run recorded no allocator-related warnings or failures
- tuned run reported `lvgl_free=0`, `lvgl_min=0`, `lvgl_frag_pct=0`

Interpretation:

- In the tuned build those LVGL heap counters are not meaningful because `LV_USE_CLIB_MALLOC` disables the builtin LVGL heap that the benchmark knows how to introspect.
- So the measured result is "no allocator failures were logged", not "the system allocator performed zero allocations".

## Measured Conclusions

- The original watchdog issue was reproducible in the baseline benchmark and occurred before cumulative `hour_minute_hands` could emit its stage-start log.
- The tuned benchmark completed the full cumulative pass and the full isolated pass without a watchdog.
- The tuned configuration bundle materially improved stability across the original failure boundary.
- The tuned configuration bundle also removed the visible LVGL builtin-heap pressure seen in the baseline run because the tuned build switched to CLIB allocation.

## Remaining Unknowns

- No full symbolic watchdog backtrace was available in the baseline run because frame pointers were disabled.
- The tuned run changed several performance settings at once, so this pass does not prove which single setting contributed the most.
- Static-stage steady-state averages are not perfectly apples-to-apples between baseline and tuned because the tuned LVGL runtime emitted many more refresh cycles in each six-second window.

## Smallest Proven Recommendation

Strictly measured recommendation:

- the smallest proven change set is the full tuned benchmark configuration as a bundle

Reason:

- this pass did not isolate the settings one by one
- it only proves that the combined tuned runtime completed successfully where the baseline runtime watchdoged

If the next step is to apply changes incrementally to the product target, the highest-signal candidates from this successful bundle are:

1. `CONFIG_COMPILER_OPTIMIZATION_PERF=y`
2. `CONFIG_FREERTOS_HZ=1000`
3. `CONFIG_LV_OS_FREERTOS=y`
4. `CONFIG_LV_USE_CLIB_MALLOC=y`
5. `CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=2`

The PSRAM-XIP and cache changes were part of the successful tuned run too, but this pass did not isolate them separately.

## Restore Procedure Verified

The backup was created with:

```bash
python -m esptool --chip esp32p4 -p /dev/cu.usbmodem83201 -b 460800 read_flash 0 0x2000000 targets/esp32-p4/backups/20260807-benchmark-preflash/flash-32mb.bin
```

So the matching full-flash restore procedure is:

```bash
python -m esptool --chip esp32p4 -p /dev/cu.usbmodem83201 -b 460800 --before default_reset --after hard_reset write_flash 0 targets/esp32-p4/backups/20260807-benchmark-preflash/flash-32mb.bin
```

The actual restore command used in this session is recorded after the restore succeeds.

## Restore Confirmation

Restore command used in this session:

```bash
source /Users/josh/Documents/round-weather-display/targets/esp32-p4/scripts/activate-idf.sh >/dev/null && /Users/josh/Documents/round-weather-display/targets/esp32-p4/scripts/restore-device.sh /dev/cu.usbmodem83201 /Users/josh/Documents/round-weather-display/targets/esp32-p4/backups/20260807-benchmark-preflash/flash-32mb.bin
```

Post-restore boot confirmation log:

- `targets/esp32-p4/benchmark-logs/20260807-post-restore-boot.log`

Measured confirmation from that boot log:

- the restored firmware is not the benchmark build
- it booted the normal Round Weather Display app
- it rejoined Wi-Fi
- it fetched live weather successfully
