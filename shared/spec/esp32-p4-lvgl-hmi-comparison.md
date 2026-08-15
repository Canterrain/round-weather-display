# ESP32-P4 LVGL HMI Comparison

Compared on Friday, August 7, 2026.

This note compares Waveshare's official `ESP-IDF` LVGL HMI demo for the `ESP32-P4-WIFI6-Touch-LCD-3.4C` against this project's `targets/esp32-p4` firmware target before any product-UI redesign work.

## Exact Upstream Inspected

- Waveshare wiki page:
  `https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-3.4C`
- Waveshare repo:
  `https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-XC`
- Repo commit inspected:
  `d5ff91f6edb5c6e6f54753b5c00b349d90c6da28`
  `2026-06-15`
- Demo inspected:
  `examples/esp-idf/08_lvgl_demo_v9`

## Short Answer

The display bring-up path is not the main difference.

Waveshare's official demo and our target use almost the same MIPI-DSI panel-init code, the same `800x800` geometry, the same `RGB565` output, the same `3` framebuffers, and the same triple-partial anti-tear mode.

The meaningful differences are higher up the stack:

- the official demo is built around an older Waveshare BSP generation
- our target resolves a much newer `esp_lvgl_adapter`
- the official demo's `sdkconfig` is much more performance-biased
- our target currently runs with a slower LVGL/runtime configuration and a fixed `64 KB` LVGL heap

That combination is a much more plausible explanation for the failed heavy visual pass than a missing display-init step.

## Version And Dependency Differences

| Area | Official demo | Our target | Meaningful impact |
| --- | --- | --- | --- |
| Example app manifest | `lvgl/lvgl: ^9.*` in `examples/esp-idf/08_lvgl_demo_v9/main/idf_component.yml` | `waveshare/esp32_p4_wifi6_touch_lcd_xc: ^3.0.1` in `targets/esp32-p4/main/idf_component.yml` | Official repo clearly targets LVGL 9, but does not pin an exact LVGL patch version in the repo contents. |
| Exact LVGL version | Not pinned in the checked-in demo files | `9.5.0` in `targets/esp32-p4/dependencies.lock` | Our target is definitely on LVGL `9.5.0`; the official demo is only provably "LVGL 9.x" from checked-in sources. |
| Waveshare BSP version | `2.0.0` in the demo's local `components/waveshare__esp32_p4_wifi6_touch_lcd_xc/idf_component.yml` | `3.0.1` in `targets/esp32-p4/dependencies.lock` | We are on a newer BSP generation than the official demo repo snapshot. |
| `esp_lvgl_adapter` version | `0.1.*` in the demo BSP component manifest | `0.6.3` in `targets/esp32-p4/dependencies.lock` | This is a major adapter-generation jump and one of the most important differences in the whole comparison. |
| ESP-IDF version | `sdkconfig.defaults` header says generated for `ESP-IDF 5.4.0`; demo code comments discuss `IDF 5.5` triple-buffer fixes | `5.5.5` in `targets/esp32-p4/dependencies.lock` | We are closer to current ESP-IDF, but not on the same exact stack the upstream demo snapshot appears to have been tuned around. |

## `sdkconfig` Differences

### System and toolchain

| Setting | Official demo | Our target | Meaningful impact |
| --- | --- | --- | --- |
| Compiler optimization | `CONFIG_COMPILER_OPTIMIZATION_PERF=y` | `CONFIG_COMPILER_OPTIMIZATION_DEBUG=y` | This is a large performance difference on a graphics-heavy UI. |
| FreeRTOS tick rate | `CONFIG_FREERTOS_HZ=1000` | `CONFIG_FREERTOS_HZ=100` | Our scheduler granularity is `10x` coarser. |
| Main task stack | `10240` | `8192` | Not likely the primary bottleneck, but it is smaller. |
| L2 cache size | `256 KB` | `128 KB` | Official demo gives the renderer more cache. |
| L2 cache line size | `128 B` | `64 B` | Another performance-leaning upstream choice. |
| Flash size | `16 MB` | `32 MB` | Capacity difference, not a likely render-speed issue. |
| P4 minimum revision | `REV_MIN_1` | `REV_MIN_100` | Compatibility difference, not a likely HMI-speed issue by itself. |

### PSRAM and code/data placement

| Setting | Official demo | Our target | Meaningful impact |
| --- | --- | --- | --- |
| PSRAM enabled | Yes | Yes | Same baseline capability. |
| PSRAM memtest | `y` | `n` | Diagnostic difference only. |
| Fetch instructions from PSRAM | `y` | not set | Official demo uses a more aggressive PSRAM/XIP setup. |
| Place rodata in PSRAM | `y` | not set | Same. |
| `SPIRAM_XIP_FROM_PSRAM` | `y` | not set | Same. |
| PSRAM malloc mode | Enabled in both | Enabled in both | Same allocator baseline outside LVGL's own heap choice. |

### LVGL core configuration

| Setting | Official demo | Our target | Meaningful impact |
| --- | --- | --- | --- |
| LVGL OS mode | `CONFIG_LV_OS_FREERTOS=y` | `CONFIG_LV_OS_NONE=y` | Official demo enables LVGL's FreeRTOS-aware path; we do not. |
| LVGL stdlib | `CONFIG_LV_USE_CLIB_MALLOC=y`, `STRING=y`, `SPRINTF=y` | `CONFIG_LV_USE_BUILTIN_MALLOC=y`, `STRING=y`, `SPRINTF=y` | This is critical. Official demo uses libc/system allocation; our target uses LVGL's builtin heap. |
| LVGL heap size | No fixed `64 KB` builtin pool because clib allocator is used | `CONFIG_LV_MEM_SIZE_KILOBYTES=64` | Our product target hard-limits LVGL's own heap to `64 KB`. This is a likely failure contributor for complex scenes. |
| Refresh period | `15 ms` | `33 ms` | Official demo refreshes more often and reduces visual latency. |
| Software draw units | `2` | `1` | Official demo allows more software drawing parallelism. |
| Draw thread stack | `8192` | not present in resolved config | Official demo has LVGL draw-thread settings because LVGL runs in FreeRTOS OS mode. |
| Draw thread priority | `3` | not present in resolved config | Same. |
| Sysmon | `CONFIG_LV_USE_SYSMON=y` | not set | Official demo ships with built-in LVGL visibility tools. |
| Perf monitor | `CONFIG_LV_USE_PERF_MONITOR=y` | not set | Same. |

## Pixel Format, Framebuffers, Draw Buffers, And Anti-Tear

## Same between both

- Display type:
  `800x800` round `3.4C`
- Pixel format:
  `RGB565`
- Framebuffer count:
  `CONFIG_BSP_LCD_DPI_BUFFER_NUMS=3`
- DPI clock:
  `80 MHz`
- DSI lane count:
  `2`
- Tear avoid mode:
  `ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL`
- LVGL adapter display profile:
  `buffer_height = 50`
- LVGL adapter display profile:
  `use_psram = false`
- LVGL adapter display profile:
  `enable_ppa_accel = false`
- LVGL adapter display profile:
  `require_double_buffer = false`

## Meaningful nuance

The high-level numbers are the same, but they are being driven by different adapter generations:

- official demo BSP manifest expects `esp_lvgl_adapter 0.1.*`
- our target resolves `esp_lvgl_adapter 0.6.3`

So even where the top-level profile looks identical, the implementation under that profile is not guaranteed to behave identically.

## PPA And 2D-DMA Differences

### PPA

- `CONFIG_SOC_PPA_SUPPORTED=y` in both builds.
- The display profile explicitly sets `enable_ppa_accel = false` in both the official demo BSP code and our managed BSP code.

Conclusion:

- PPA exists on the SoC in both cases.
- Neither configuration is actually opting into PPA acceleration for this screen path.

### 2D-DMA / DMA2D

- The official demo BSP hard-codes `.flags.use_dma2d = true` in the DPI panel config.
- Our managed BSP does the same for `ESP-IDF < 6.0`, but wraps it in an IDF-version compatibility guard.

Because our target is on `ESP-IDF 5.5.5`, that guarded path is still active today.

Conclusion:

- the official demo and our current target both request `DMA2D` in the LCD DPI panel config on the IDF version we are using
- this is not the main divergence

## LVGL Task And App Task Differences

### Official demo

The official demo is minimal:

- it starts the BSP display
- it turns on the backlight
- it locks LVGL
- it launches `lv_demo_widgets()`
- it unlocks LVGL

There is no additional product runtime on top of that in `main.c`.

### Our target

Our target adds app runtime work on top of the display stack:

- LVGL adapter task from `ESP_LV_ADAPTER_DEFAULT_CONFIG()`
- `rwd_ui_heartbeat` task
  `4096` stack, priority `4`
- `rwd_weather` task
  `8192` stack, priority `4`

Also, in our resolved adapter headers, `ESP_LV_ADAPTER_DEFAULT_CONFIG()` currently means:

- task stack:
  `8192`
- task priority:
  `6`
- task core:
  `-1`
- tick period:
  `1 ms`
- min delay:
  `1 ms`
- max delay:
  `15 ms`
- stack in PSRAM:
  `false`

That matters because our product UI is not running in the same "just show the LVGL demo" environment as Waveshare's reference.

## MIPI-DSI Initialization Differences

The panel-init code is almost the same.

### Same between both

- DSI bus ID:
  `0`
- lane count:
  `BSP_LCD_MIPI_DSI_LANE_NUM`
- lane bitrate:
  `BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS`
- DPI clock:
  `80 MHz`
- RGB565 path:
  `16 bpp`
- video timing:
  `hsync_back_porch=20`
  `hsync_pulse_width=20`
  `hsync_front_porch=40`
  `vsync_back_porch=12`
  `vsync_pulse_width=4`
  `vsync_front_porch=24`
- framebuffer count:
  `CONFIG_BSP_LCD_DPI_BUFFER_NUMS`
- panel driver:
  `JD9365`

### Small code differences

- The official demo uses `.phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT`.
- Our managed BSP uses `.phy_clk_src = 0`.

This appears intended as a compatibility substitution, not a deliberate behavior change.
That sentence is an inference from the code shape, not an explicit upstream statement.

- Our managed BSP adds `ESP_IDF_VERSION >= 6.0.0` guards for the `in_color_format` field vs the older `pixel_format` field.
- Our managed BSP also guards `use_dma2d` behind `ESP_IDF_VERSION < 6.0.0`.

Conclusion:

- our current failure is very unlikely to be caused by missing MIPI-DSI timings or panel bring-up steps
- the init path is already extremely close to upstream

## `bsp_extra` Check

The official demo includes a `bsp_extra` component, but in the inspected `08_lvgl_demo_v9` tree it adds audio and board helper functionality, not an alternate display stack.

That means there is not a hidden second display/HMI pipeline elsewhere in the official demo that we forgot to copy.

## Most Important Differences In Plain Language

If we rank the deltas by how likely they are to explain a failed heavy visual pass, the highest-signal items are:

1. Our target is built with debug optimization, while the official demo is built for performance.
2. Our target runs at `FreeRTOS 100 Hz`, while the official demo runs at `1000 Hz`.
3. Our target uses `LV_OS_NONE`; the official demo uses `LV_OS_FREERTOS`.
4. Our target uses `1` LVGL software draw unit; the official demo uses `2`.
5. Our target uses LVGL's builtin allocator with a fixed `64 KB` heap; the official demo uses libc/system allocation.
6. Our target uses less aggressive cache and PSRAM-XIP settings.
7. Our target adds real product tasks on top of the graphics stack; the official demo does not.
8. Our target resolves a much newer `esp_lvgl_adapter` generation than the official demo repo snapshot.

## Why The Failed Visual Pass Most Likely Happened

The evidence points away from "we initialized the display wrong" and toward "we asked a slower runtime profile to draw a much heavier scene."

The likely chain is:

- the redesigned clock view introduced a large `lv_scale`, many tick labels, multiple needle lines, translucent circular panels, and more layered object styling
- that work landed on a target currently built with debug optimization, `100 Hz` RTOS ticks, a single LVGL draw unit, and a fixed `64 KB` LVGL heap
- the official demo does not carry those penalties at the same time
- so our heavy scene had less rendering throughput and less LVGL allocation headroom than the official reference environment

That makes long refresh cycles, allocation pressure, and watchdog-adjacent stalls much more believable than a missing DSI init step.

## Benchmark Added In This Phase

To keep the stable product UI untouched, the benchmark is opt-in.

It builds a dedicated analog-clock benchmark screen that adds these elements one phase at a time:

1. baseline screen
2. day and date labels
3. full analog clock ring
4. hour and minute hands
5. second hand
6. center disc
7. weather copy inside the disc
8. edge indicator

It logs:

- last, average, and max LVGL refresh time
- render time
- flush time
- flush-wait time
- internal heap free and largest block
- PSRAM free and largest block
- LVGL heap free, max used, and fragmentation
- stall warnings that are useful as watchdog early-warning signals

The normal product UI remains the default build.
