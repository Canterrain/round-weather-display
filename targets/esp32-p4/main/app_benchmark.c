#include "app_benchmark.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "esp_lv_adapter.h"
#include "lvgl.h"

#define BENCHMARK_VALID_CLOCK_EPOCH 1704067200
#define BENCHMARK_STALL_WARN_MS 2500
#define BENCHMARK_FRAME_SAMPLE_CAP 512
#define BENCHMARK_LOG_BUFFER_CAP 2048

#ifndef CONFIG_RWD_CLOCK_BENCHMARK_PHASE_MS
#define CONFIG_RWD_CLOCK_BENCHMARK_PHASE_MS 6000
#endif

#ifndef CONFIG_RWD_CLOCK_BENCHMARK_SAMPLE_MS
#define CONFIG_RWD_CLOCK_BENCHMARK_SAMPLE_MS 1000
#endif

#if !defined(CONFIG_RWD_CLOCK_BENCHMARK_PASS_MODE_CUMULATIVE_ONLY) && \
  !defined(CONFIG_RWD_CLOCK_BENCHMARK_PASS_MODE_ISOLATED_ONLY) && \
  !defined(CONFIG_RWD_CLOCK_BENCHMARK_PASS_MODE_BOTH)
#define CONFIG_RWD_CLOCK_BENCHMARK_PASS_MODE_BOTH 1
#endif

typedef enum {
  BENCHMARK_PHASE_BASELINE = 0,
  BENCHMARK_PHASE_DAY_DATE,
  BENCHMARK_PHASE_CLOCK_RING,
  BENCHMARK_PHASE_HOUR_MINUTE_HANDS,
  BENCHMARK_PHASE_SECOND_HAND,
  BENCHMARK_PHASE_CENTER_DISC,
  BENCHMARK_PHASE_WEATHER_COPY,
  BENCHMARK_PHASE_EDGE_INDICATOR,
  BENCHMARK_PHASE_COMPLETE
} benchmark_phase_t;

typedef enum {
  BENCHMARK_PASS_CUMULATIVE = 0,
  BENCHMARK_PASS_ISOLATED,
  BENCHMARK_PASS_COMPLETE
} benchmark_pass_t;

typedef struct {
  int64_t current_refr_start_us;
  int64_t current_render_start_us;
  int64_t current_flush_start_us;
  int64_t current_flush_wait_start_us;
  int64_t current_render_us;
  int64_t current_flush_total_us;
  int64_t current_flush_wait_total_us;
  int64_t last_refr_ready_us;
  int64_t last_sample_us;
  int64_t last_frame_us;
  int64_t last_render_us;
  int64_t last_flush_us;
  int64_t last_flush_wait_us;
  int64_t max_frame_us;
  int64_t max_render_us;
  int64_t max_flush_us;
  int64_t max_flush_wait_us;
  int64_t total_frame_us;
  int64_t total_render_us;
  int64_t total_flush_us;
  int64_t total_flush_wait_us;
  uint64_t frame_count;
  uint64_t watchdog_warning_count;
  uint64_t draw_buffer_failure_count;
  uint64_t lock_failure_count;
  uint64_t refresh_failure_count;
  uint64_t object_alloc_failure_count;
} benchmark_runtime_stats_t;

typedef struct {
  size_t internal_free;
  size_t internal_largest;
  size_t psram_free;
  size_t psram_largest;
  lv_mem_monitor_t lvgl_mem;
} benchmark_memory_snapshot_t;

typedef struct {
  benchmark_pass_t pass;
  benchmark_phase_t phase;
  bool active;
  bool waiting_for_activation_frame;
  int64_t start_us;
  int64_t build_us;
  int64_t activation_frame_us;
  int64_t activation_render_us;
  int64_t activation_flush_us;
  int64_t activation_flush_wait_us;
  uint64_t steady_frame_count;
  int64_t steady_total_frame_us;
  int64_t steady_total_render_us;
  int64_t steady_total_flush_us;
  int64_t steady_total_flush_wait_us;
  int64_t steady_max_frame_us;
  int64_t steady_max_render_us;
  int64_t steady_max_flush_us;
  int64_t steady_max_flush_wait_us;
  int32_t steady_frame_samples_us[BENCHMARK_FRAME_SAMPLE_CAP];
  size_t steady_frame_sample_count;
  size_t steady_frame_sample_overflow;
  size_t internal_free_start;
  size_t internal_largest_start;
  size_t psram_free_start;
  size_t psram_largest_start;
  size_t lvgl_free_start;
  size_t lvgl_biggest_start;
  uint32_t lvgl_used_pct_start;
  uint32_t lvgl_frag_pct_start;
  size_t current_internal_free;
  size_t current_internal_largest;
  size_t current_psram_free;
  size_t current_psram_largest;
  size_t current_lvgl_free;
  size_t current_lvgl_biggest;
  uint32_t current_lvgl_used_pct;
  uint32_t current_lvgl_frag_pct;
  size_t min_internal_free;
  size_t min_internal_largest;
  size_t min_psram_free;
  size_t min_psram_largest;
  size_t min_lvgl_free;
  size_t min_lvgl_biggest;
  uint32_t max_lvgl_used_pct;
  uint32_t max_lvgl_frag_pct;
  uint64_t sample_count;
  uint64_t watchdog_warning_start;
  uint64_t draw_buffer_failure_start;
  uint64_t lock_failure_start;
  uint64_t refresh_failure_start;
  uint64_t object_alloc_failure_start;
} benchmark_stage_stats_t;

typedef struct {
  lv_display_t *display;
  lv_obj_t *screen;
  lv_obj_t *clock_root;
  lv_obj_t *phase_label;
  lv_obj_t *info_label;
  lv_obj_t *day_label;
  lv_obj_t *date_label;
  lv_obj_t *clock_scale;
  lv_obj_t *hour_hand;
  lv_obj_t *minute_hand;
  lv_obj_t *second_hand;
  lv_obj_t *center_disc;
  lv_obj_t *summary_label;
  lv_obj_t *temp_label;
  lv_obj_t *high_low_label;
  lv_obj_t *edge_indicator;
  lv_timer_t *phase_timer;
  lv_timer_t *sample_timer;
  lv_timer_t *clock_timer;
  benchmark_pass_t pass;
  size_t phase_index;
  time_t benchmark_time;
  benchmark_runtime_stats_t runtime;
  benchmark_stage_stats_t stage;
} benchmark_state_t;

static const char *TAG = "rwd_benchmark";

static benchmark_state_t s_benchmark;
static vprintf_like_t s_previous_log_vprintf = NULL;

static const benchmark_phase_t BENCHMARK_SEQUENCE[] = {
  BENCHMARK_PHASE_BASELINE,
  BENCHMARK_PHASE_DAY_DATE,
  BENCHMARK_PHASE_CLOCK_RING,
  BENCHMARK_PHASE_HOUR_MINUTE_HANDS,
  BENCHMARK_PHASE_SECOND_HAND,
  BENCHMARK_PHASE_CENTER_DISC,
  BENCHMARK_PHASE_WEATHER_COPY,
  BENCHMARK_PHASE_EDGE_INDICATOR
};

static const char *const WEEKDAY_NAMES[] = {
  "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};
static const char *const MONTH_NAMES[] = {
  "Jan", "Feb", "Mar", "Apr", "May", "Jun",
  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};
static const char *CLOCK_LABELS[] = {
  "12", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", NULL
};

static lv_color_t color_bg(void)
{
  return lv_color_hex(0x07101c);
}

static lv_color_t color_panel(void)
{
  return lv_color_hex(0x0f1c2d);
}

static lv_color_t color_panel_border(void)
{
  return lv_color_hex(0x253a57);
}

static lv_color_t color_text_primary(void)
{
  return lv_color_hex(0xf5f8ff);
}

static lv_color_t color_text_muted(void)
{
  return lv_color_hex(0xa6b6cf);
}

static lv_color_t color_text_subtle(void)
{
  return lv_color_hex(0x6d819d);
}

static lv_color_t color_accent(void)
{
  return lv_color_hex(0x78b8ff);
}

static lv_color_t color_temp(void)
{
  return lv_color_hex(0xffd06b);
}

static float us_to_ms(int64_t value_us)
{
  return value_us <= 0 ? -1.0f : (float) value_us / 1000.0f;
}

static float average_us_to_ms(int64_t total_us, uint64_t count)
{
  return count == 0 ? -1.0f : ((float) total_us / (float) count) / 1000.0f;
}

static void benchmark_note_draw_buffer_failure_from_log(const char *line)
{
  if (line == NULL) {
    return;
  }

  if (
    strstr(line, "Failed to allocate dummy draw buffer") != NULL ||
    strstr(line, "alloc draw buffer failed") != NULL ||
    strstr(line, "Could not create draw buffer") != NULL
  ) {
    s_benchmark.runtime.draw_buffer_failure_count += 1;
  }

  if (strstr(line, "Could not lock LVGL") != NULL) {
    s_benchmark.runtime.lock_failure_count += 1;
  }
}

static int benchmark_log_vprintf(const char *fmt, va_list args)
{
  char buffer[BENCHMARK_LOG_BUFFER_CAP];
  va_list copy;
  va_copy(copy, args);
  int len = vsnprintf(buffer, sizeof(buffer), fmt, copy);
  va_end(copy);

  if (len <= 0) {
    return len;
  }

  benchmark_note_draw_buffer_failure_from_log(buffer);

  size_t bytes_to_write = (size_t) len;
  if (bytes_to_write >= sizeof(buffer)) {
    bytes_to_write = sizeof(buffer) - 1;
  }

  ssize_t write_result = write(STDOUT_FILENO, buffer, bytes_to_write);
  (void) write_result;
  return len;
}

static const char *benchmark_phase_name(benchmark_phase_t phase)
{
  switch (phase) {
    case BENCHMARK_PHASE_BASELINE:
      return "Baseline";
    case BENCHMARK_PHASE_DAY_DATE:
      return "Day + Date";
    case BENCHMARK_PHASE_CLOCK_RING:
      return "Clock Ring";
    case BENCHMARK_PHASE_HOUR_MINUTE_HANDS:
      return "Hour + Minute Hands";
    case BENCHMARK_PHASE_SECOND_HAND:
      return "Second Hand";
    case BENCHMARK_PHASE_CENTER_DISC:
      return "Center Disc";
    case BENCHMARK_PHASE_WEATHER_COPY:
      return "Weather Copy";
    case BENCHMARK_PHASE_EDGE_INDICATOR:
      return "Edge Indicator";
    case BENCHMARK_PHASE_COMPLETE:
      return "Complete";
  }

  return "Unknown";
}

static const char *benchmark_phase_id(benchmark_phase_t phase)
{
  switch (phase) {
    case BENCHMARK_PHASE_BASELINE:
      return "baseline";
    case BENCHMARK_PHASE_DAY_DATE:
      return "day_date";
    case BENCHMARK_PHASE_CLOCK_RING:
      return "ring";
    case BENCHMARK_PHASE_HOUR_MINUTE_HANDS:
      return "hour_minute_hands";
    case BENCHMARK_PHASE_SECOND_HAND:
      return "second_hand";
    case BENCHMARK_PHASE_CENTER_DISC:
      return "center_disc";
    case BENCHMARK_PHASE_WEATHER_COPY:
      return "weather_copy";
    case BENCHMARK_PHASE_EDGE_INDICATOR:
      return "edge_indicator";
    case BENCHMARK_PHASE_COMPLETE:
      return "complete";
  }

  return "unknown";
}

static const char *benchmark_pass_name(benchmark_pass_t pass)
{
  switch (pass) {
    case BENCHMARK_PASS_CUMULATIVE:
      return "Cumulative";
    case BENCHMARK_PASS_ISOLATED:
      return "Isolated";
    case BENCHMARK_PASS_COMPLETE:
      return "Complete";
  }

  return "Unknown";
}

static const char *benchmark_pass_id(benchmark_pass_t pass)
{
  switch (pass) {
    case BENCHMARK_PASS_CUMULATIVE:
      return "cumulative";
    case BENCHMARK_PASS_ISOLATED:
      return "isolated";
    case BENCHMARK_PASS_COMPLETE:
      return "complete";
  }

  return "unknown";
}

static const char *benchmark_redraw_mode(benchmark_pass_t pass, benchmark_phase_t phase)
{
  if (pass == BENCHMARK_PASS_CUMULATIVE && phase >= BENCHMARK_PHASE_SECOND_HAND) {
    return "clock_tick_1hz";
  }

  if (pass == BENCHMARK_PASS_ISOLATED && phase == BENCHMARK_PHASE_SECOND_HAND) {
    return "clock_tick_1hz";
  }

  return "static_after_create";
}

static benchmark_pass_t benchmark_initial_pass(void)
{
#if CONFIG_RWD_CLOCK_BENCHMARK_PASS_MODE_ISOLATED_ONLY
  return BENCHMARK_PASS_ISOLATED;
#else
  return BENCHMARK_PASS_CUMULATIVE;
#endif
}

static benchmark_pass_t benchmark_next_pass(benchmark_pass_t pass)
{
#if CONFIG_RWD_CLOCK_BENCHMARK_PASS_MODE_BOTH
  if (pass == BENCHMARK_PASS_CUMULATIVE) {
    return BENCHMARK_PASS_ISOLATED;
  }
#endif
  return BENCHMARK_PASS_COMPLETE;
}

static time_t benchmark_initial_time(void)
{
  time_t now = time(NULL);
  if (now >= BENCHMARK_VALID_CLOCK_EPOCH) {
    return now;
  }

  struct tm fallback = {
    .tm_year = 2026 - 1900,
    .tm_mon = 7,
    .tm_mday = 7,
    .tm_hour = 9,
    .tm_min = 41,
    .tm_sec = 0,
    .tm_isdst = -1
  };
  return mktime(&fallback);
}

static void benchmark_note_object_alloc_failure(const char *what)
{
  s_benchmark.runtime.object_alloc_failure_count += 1;
  ESP_LOGE(TAG, "Benchmark allocation failed for %s", what);
}

static lv_obj_t *benchmark_obj_create(lv_obj_t *parent, const char *what)
{
  lv_obj_t *obj = lv_obj_create(parent);
  if (obj == NULL) {
    benchmark_note_object_alloc_failure(what);
  }
  return obj;
}

static lv_obj_t *benchmark_label_create_raw(lv_obj_t *parent, const char *what)
{
  lv_obj_t *label = lv_label_create(parent);
  if (label == NULL) {
    benchmark_note_object_alloc_failure(what);
  }
  return label;
}

static lv_obj_t *benchmark_line_create(lv_obj_t *parent, const char *what)
{
  lv_obj_t *line = lv_line_create(parent);
  if (line == NULL) {
    benchmark_note_object_alloc_failure(what);
  }
  return line;
}

static lv_obj_t *benchmark_scale_create(lv_obj_t *parent, const char *what)
{
  lv_obj_t *scale = lv_scale_create(parent);
  if (scale == NULL) {
    benchmark_note_object_alloc_failure(what);
  }
  return scale;
}

static lv_timer_t *benchmark_timer_create(lv_timer_cb_t cb, uint32_t period_ms, const char *what)
{
  lv_timer_t *timer = lv_timer_create(cb, period_ms, NULL);
  if (timer == NULL) {
    benchmark_note_object_alloc_failure(what);
  }
  return timer;
}

static lv_obj_t *create_label(
  lv_obj_t *parent,
  const lv_font_t *font,
  lv_color_t color,
  lv_text_align_t text_align,
  const char *text,
  const char *what
)
{
  lv_obj_t *label = benchmark_label_create_raw(parent, what);
  if (label == NULL) {
    return NULL;
  }

  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, color, 0);
  lv_obj_set_style_text_align(label, text_align, 0);
  lv_label_set_text(label, text);
  return label;
}

static void style_panel(lv_obj_t *obj, int radius, lv_opa_t bg_opa)
{
  if (obj == NULL) {
    return;
  }

  lv_obj_set_style_bg_color(obj, color_panel(), 0);
  lv_obj_set_style_bg_opa(obj, bg_opa, 0);
  lv_obj_set_style_border_color(obj, color_panel_border(), 0);
  lv_obj_set_style_border_width(obj, 2, 0);
  lv_obj_set_style_radius(obj, radius, 0);
  lv_obj_set_style_shadow_width(obj, 0, 0);
}

static void benchmark_read_memory_snapshot(benchmark_memory_snapshot_t *snapshot)
{
  if (snapshot == NULL) {
    return;
  }

  memset(snapshot, 0, sizeof(*snapshot));
  snapshot->internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  snapshot->internal_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  snapshot->psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  snapshot->psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
  lv_mem_monitor(&snapshot->lvgl_mem);
}

static void benchmark_update_stage_memory_metrics(const benchmark_memory_snapshot_t *snapshot)
{
  benchmark_stage_stats_t *stage = &s_benchmark.stage;
  if (snapshot == NULL || !stage->active) {
    return;
  }

  stage->current_internal_free = snapshot->internal_free;
  stage->current_internal_largest = snapshot->internal_largest;
  stage->current_psram_free = snapshot->psram_free;
  stage->current_psram_largest = snapshot->psram_largest;
  stage->current_lvgl_free = snapshot->lvgl_mem.free_size;
  stage->current_lvgl_biggest = snapshot->lvgl_mem.free_biggest_size;
  stage->current_lvgl_used_pct = snapshot->lvgl_mem.used_pct;
  stage->current_lvgl_frag_pct = snapshot->lvgl_mem.frag_pct;

  if (stage->sample_count == 0) {
    stage->min_internal_free = snapshot->internal_free;
    stage->min_internal_largest = snapshot->internal_largest;
    stage->min_psram_free = snapshot->psram_free;
    stage->min_psram_largest = snapshot->psram_largest;
    stage->min_lvgl_free = snapshot->lvgl_mem.free_size;
    stage->min_lvgl_biggest = snapshot->lvgl_mem.free_biggest_size;
    stage->max_lvgl_used_pct = snapshot->lvgl_mem.used_pct;
    stage->max_lvgl_frag_pct = snapshot->lvgl_mem.frag_pct;
  } else {
    if (snapshot->internal_free < stage->min_internal_free) {
      stage->min_internal_free = snapshot->internal_free;
    }
    if (snapshot->internal_largest < stage->min_internal_largest) {
      stage->min_internal_largest = snapshot->internal_largest;
    }
    if (snapshot->psram_free < stage->min_psram_free) {
      stage->min_psram_free = snapshot->psram_free;
    }
    if (snapshot->psram_largest < stage->min_psram_largest) {
      stage->min_psram_largest = snapshot->psram_largest;
    }
    if (snapshot->lvgl_mem.free_size < stage->min_lvgl_free) {
      stage->min_lvgl_free = snapshot->lvgl_mem.free_size;
    }
    if (snapshot->lvgl_mem.free_biggest_size < stage->min_lvgl_biggest) {
      stage->min_lvgl_biggest = snapshot->lvgl_mem.free_biggest_size;
    }
    if (snapshot->lvgl_mem.used_pct > stage->max_lvgl_used_pct) {
      stage->max_lvgl_used_pct = snapshot->lvgl_mem.used_pct;
    }
    if (snapshot->lvgl_mem.frag_pct > stage->max_lvgl_frag_pct) {
      stage->max_lvgl_frag_pct = snapshot->lvgl_mem.frag_pct;
    }
  }

  stage->sample_count += 1;
}

static void benchmark_clear_visual_handles(void)
{
  s_benchmark.clock_root = NULL;
  s_benchmark.phase_label = NULL;
  s_benchmark.info_label = NULL;
  s_benchmark.day_label = NULL;
  s_benchmark.date_label = NULL;
  s_benchmark.clock_scale = NULL;
  s_benchmark.hour_hand = NULL;
  s_benchmark.minute_hand = NULL;
  s_benchmark.second_hand = NULL;
  s_benchmark.center_disc = NULL;
  s_benchmark.summary_label = NULL;
  s_benchmark.temp_label = NULL;
  s_benchmark.high_low_label = NULL;
  s_benchmark.edge_indicator = NULL;
}

static bool benchmark_build_shell(void)
{
  benchmark_clear_visual_handles();

  s_benchmark.screen = lv_screen_active();
  if (s_benchmark.screen == NULL) {
    benchmark_note_object_alloc_failure("active-screen");
    return false;
  }

  lv_obj_clean(s_benchmark.screen);
  lv_obj_set_style_bg_color(s_benchmark.screen, color_bg(), 0);
  lv_obj_set_style_bg_opa(s_benchmark.screen, LV_OPA_COVER, 0);

  s_benchmark.clock_root = benchmark_obj_create(s_benchmark.screen, "clock-root");
  if (s_benchmark.clock_root == NULL) {
    return false;
  }

  lv_obj_set_size(s_benchmark.clock_root, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_opa(s_benchmark.clock_root, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_benchmark.clock_root, 0, 0);
  lv_obj_set_style_pad_all(s_benchmark.clock_root, 0, 0);
  lv_obj_set_scrollbar_mode(s_benchmark.clock_root, LV_SCROLLBAR_MODE_OFF);

  s_benchmark.phase_label = create_label(
    s_benchmark.screen,
    &lv_font_montserrat_24,
    color_text_primary(),
    LV_TEXT_ALIGN_CENTER,
    "Clock Benchmark",
    "phase-label"
  );
  if (s_benchmark.phase_label == NULL) {
    return false;
  }
  lv_obj_align(s_benchmark.phase_label, LV_ALIGN_TOP_MID, 0, 24);

  s_benchmark.info_label = create_label(
    s_benchmark.screen,
    &lv_font_montserrat_14,
    color_text_muted(),
    LV_TEXT_ALIGN_CENTER,
    "Timed benchmark pass.\nWatch serial logs for per-stage creation and steady-state metrics.",
    "info-label"
  );
  if (s_benchmark.info_label == NULL) {
    return false;
  }
  lv_obj_set_width(s_benchmark.info_label, 660);
  lv_label_set_long_mode(s_benchmark.info_label, LV_LABEL_LONG_WRAP);
  lv_obj_align(s_benchmark.info_label, LV_ALIGN_BOTTOM_MID, 0, -32);

  return true;
}

static void benchmark_update_clock_copy(void)
{
  struct tm local_time;

  if (s_benchmark.benchmark_time <= 0) {
    return;
  }

  localtime_r(&s_benchmark.benchmark_time, &local_time);

  if (s_benchmark.day_label != NULL) {
    lv_label_set_text(s_benchmark.day_label, WEEKDAY_NAMES[local_time.tm_wday]);
  }

  if (s_benchmark.date_label != NULL) {
    lv_label_set_text_fmt(
      s_benchmark.date_label,
      "%s %d",
      MONTH_NAMES[local_time.tm_mon],
      local_time.tm_mday
    );
  }

  if (s_benchmark.clock_scale != NULL) {
    int hour = local_time.tm_hour % 12;
    int minute = local_time.tm_min;
    int second = local_time.tm_sec;

    if (s_benchmark.minute_hand != NULL) {
      lv_scale_set_line_needle_value(s_benchmark.clock_scale, s_benchmark.minute_hand, 52, minute);
    }

    if (s_benchmark.hour_hand != NULL) {
      lv_scale_set_line_needle_value(
        s_benchmark.clock_scale,
        s_benchmark.hour_hand,
        34,
        (hour * 5) + (minute / 12)
      );
    }

    if (s_benchmark.second_hand != NULL) {
      lv_scale_set_line_needle_value(s_benchmark.clock_scale, s_benchmark.second_hand, 56, second);
    }
  }
}

static bool benchmark_build_day_date(void)
{
  if (s_benchmark.day_label != NULL) {
    return true;
  }

  s_benchmark.day_label = create_label(
    s_benchmark.clock_root,
    &lv_font_montserrat_24,
    color_text_primary(),
    LV_TEXT_ALIGN_CENTER,
    "Friday",
    "day-label"
  );
  if (s_benchmark.day_label == NULL) {
    return false;
  }
  lv_obj_align(s_benchmark.day_label, LV_ALIGN_TOP_MID, 0, 70);

  s_benchmark.date_label = create_label(
    s_benchmark.clock_root,
    &lv_font_montserrat_18,
    color_text_muted(),
    LV_TEXT_ALIGN_CENTER,
    "Aug 7",
    "date-label"
  );
  if (s_benchmark.date_label == NULL) {
    return false;
  }
  lv_obj_align_to(s_benchmark.date_label, s_benchmark.day_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);

  benchmark_update_clock_copy();
  return true;
}

static bool benchmark_build_clock_ring(void)
{
  if (s_benchmark.clock_scale != NULL) {
    return true;
  }

  s_benchmark.clock_scale = benchmark_scale_create(s_benchmark.clock_root, "clock-scale");
  if (s_benchmark.clock_scale == NULL) {
    return false;
  }

  lv_obj_set_size(s_benchmark.clock_scale, 610, 610);
  lv_obj_align(s_benchmark.clock_scale, LV_ALIGN_CENTER, 0, 14);
  lv_scale_set_mode(s_benchmark.clock_scale, LV_SCALE_MODE_ROUND_INNER);
  lv_scale_set_label_show(s_benchmark.clock_scale, true);
  lv_scale_set_total_tick_count(s_benchmark.clock_scale, 61);
  lv_scale_set_major_tick_every(s_benchmark.clock_scale, 5);
  lv_scale_set_range(s_benchmark.clock_scale, 0, 60);
  lv_scale_set_angle_range(s_benchmark.clock_scale, 360);
  lv_scale_set_rotation(s_benchmark.clock_scale, 270);
  lv_scale_set_text_src(s_benchmark.clock_scale, CLOCK_LABELS);
  lv_obj_set_style_bg_color(s_benchmark.clock_scale, lv_color_hex(0x0a1628), 0);
  lv_obj_set_style_bg_opa(s_benchmark.clock_scale, LV_OPA_70, 0);
  lv_obj_set_style_radius(s_benchmark.clock_scale, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_clip_corner(s_benchmark.clock_scale, true, 0);
  lv_obj_set_style_arc_color(s_benchmark.clock_scale, color_panel_border(), LV_PART_MAIN);
  lv_obj_set_style_arc_width(s_benchmark.clock_scale, 3, LV_PART_MAIN);
  lv_obj_set_style_line_color(s_benchmark.clock_scale, color_text_subtle(), LV_PART_ITEMS);
  lv_obj_set_style_line_width(s_benchmark.clock_scale, 2, LV_PART_ITEMS);
  lv_obj_set_style_length(s_benchmark.clock_scale, 12, LV_PART_ITEMS);
  lv_obj_set_style_line_color(s_benchmark.clock_scale, color_text_primary(), LV_PART_INDICATOR);
  lv_obj_set_style_line_width(s_benchmark.clock_scale, 3, LV_PART_INDICATOR);
  lv_obj_set_style_length(s_benchmark.clock_scale, 22, LV_PART_INDICATOR);
  lv_obj_set_style_text_font(s_benchmark.clock_scale, &lv_font_montserrat_18, LV_PART_INDICATOR);
  lv_obj_set_style_text_color(s_benchmark.clock_scale, color_text_muted(), LV_PART_INDICATOR);

  benchmark_update_clock_copy();
  return true;
}

static bool benchmark_build_hour_minute_hands(void)
{
  if (s_benchmark.clock_scale == NULL && !benchmark_build_clock_ring()) {
    return false;
  }

  if (s_benchmark.minute_hand == NULL) {
    s_benchmark.minute_hand = benchmark_line_create(s_benchmark.clock_scale, "minute-hand");
    if (s_benchmark.minute_hand == NULL) {
      return false;
    }
    lv_obj_set_style_line_width(s_benchmark.minute_hand, 6, 0);
    lv_obj_set_style_line_color(s_benchmark.minute_hand, color_text_primary(), 0);
    lv_obj_set_style_line_rounded(s_benchmark.minute_hand, true, 0);
  }

  if (s_benchmark.hour_hand == NULL) {
    s_benchmark.hour_hand = benchmark_line_create(s_benchmark.clock_scale, "hour-hand");
    if (s_benchmark.hour_hand == NULL) {
      return false;
    }
    lv_obj_set_style_line_width(s_benchmark.hour_hand, 10, 0);
    lv_obj_set_style_line_color(s_benchmark.hour_hand, color_accent(), 0);
    lv_obj_set_style_line_rounded(s_benchmark.hour_hand, true, 0);
  }

  benchmark_update_clock_copy();
  return true;
}

static bool benchmark_build_second_hand(void)
{
  if (!benchmark_build_hour_minute_hands()) {
    return false;
  }

  if (s_benchmark.second_hand != NULL) {
    benchmark_update_clock_copy();
    return true;
  }

  s_benchmark.second_hand = benchmark_line_create(s_benchmark.clock_scale, "second-hand");
  if (s_benchmark.second_hand == NULL) {
    return false;
  }

  lv_obj_set_style_line_width(s_benchmark.second_hand, 3, 0);
  lv_obj_set_style_line_color(s_benchmark.second_hand, lv_color_hex(0xff9b7d), 0);
  lv_obj_set_style_line_rounded(s_benchmark.second_hand, true, 0);

  benchmark_update_clock_copy();
  return true;
}

static bool benchmark_build_center_disc(void)
{
  if (s_benchmark.center_disc != NULL) {
    return true;
  }

  s_benchmark.center_disc = benchmark_obj_create(s_benchmark.clock_root, "center-disc");
  if (s_benchmark.center_disc == NULL) {
    return false;
  }

  lv_obj_set_size(s_benchmark.center_disc, 230, 210);
  lv_obj_align(s_benchmark.center_disc, LV_ALIGN_CENTER, 0, 16);
  style_panel(s_benchmark.center_disc, LV_RADIUS_CIRCLE, LV_OPA_80);
  lv_obj_set_style_pad_all(s_benchmark.center_disc, 20, 0);
  lv_obj_set_style_pad_row(s_benchmark.center_disc, 6, 0);
  lv_obj_set_layout(s_benchmark.center_disc, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(s_benchmark.center_disc, LV_FLEX_FLOW_COLUMN);
  return true;
}

static bool benchmark_build_weather_copy(void)
{
  if (!benchmark_build_center_disc()) {
    return false;
  }

  if (s_benchmark.summary_label != NULL) {
    return true;
  }

  s_benchmark.summary_label = create_label(
    s_benchmark.center_disc,
    &lv_font_montserrat_18,
    color_text_muted(),
    LV_TEXT_ALIGN_CENTER,
    "Partly Cloudy",
    "summary-label"
  );
  if (s_benchmark.summary_label == NULL) {
    return false;
  }
  lv_obj_set_width(s_benchmark.summary_label, LV_PCT(100));

  s_benchmark.temp_label = create_label(
    s_benchmark.center_disc,
    &lv_font_montserrat_48,
    color_temp(),
    LV_TEXT_ALIGN_CENTER,
    "72°",
    "temp-label"
  );
  if (s_benchmark.temp_label == NULL) {
    return false;
  }
  lv_obj_set_width(s_benchmark.temp_label, LV_PCT(100));

  s_benchmark.high_low_label = create_label(
    s_benchmark.center_disc,
    &lv_font_montserrat_18,
    color_text_primary(),
    LV_TEXT_ALIGN_CENTER,
    "H:78°  L:64°",
    "high-low-label"
  );
  if (s_benchmark.high_low_label == NULL) {
    return false;
  }
  lv_obj_set_width(s_benchmark.high_low_label, LV_PCT(100));
  return true;
}

static bool benchmark_build_edge_indicator(void)
{
  if (s_benchmark.edge_indicator != NULL) {
    return true;
  }

  s_benchmark.edge_indicator = benchmark_obj_create(s_benchmark.clock_root, "edge-indicator");
  if (s_benchmark.edge_indicator == NULL) {
    return false;
  }

  lv_obj_set_size(s_benchmark.edge_indicator, 8, 140);
  lv_obj_align(s_benchmark.edge_indicator, LV_ALIGN_RIGHT_MID, -18, 0);
  lv_obj_set_style_radius(s_benchmark.edge_indicator, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(s_benchmark.edge_indicator, lv_color_hex(0xffae54), 0);
  lv_obj_set_style_border_width(s_benchmark.edge_indicator, 0, 0);
  return true;
}

static bool benchmark_apply_cumulative_stage(benchmark_phase_t phase)
{
  switch (phase) {
    case BENCHMARK_PHASE_BASELINE:
      return true;
    case BENCHMARK_PHASE_DAY_DATE:
      return benchmark_build_day_date();
    case BENCHMARK_PHASE_CLOCK_RING:
      return benchmark_build_clock_ring();
    case BENCHMARK_PHASE_HOUR_MINUTE_HANDS:
      return benchmark_build_hour_minute_hands();
    case BENCHMARK_PHASE_SECOND_HAND:
      return benchmark_build_second_hand();
    case BENCHMARK_PHASE_CENTER_DISC:
      return benchmark_build_center_disc();
    case BENCHMARK_PHASE_WEATHER_COPY:
      return benchmark_build_weather_copy();
    case BENCHMARK_PHASE_EDGE_INDICATOR:
      return benchmark_build_edge_indicator();
    case BENCHMARK_PHASE_COMPLETE:
      return true;
  }

  return false;
}

static bool benchmark_apply_isolated_stage(benchmark_phase_t phase)
{
  switch (phase) {
    case BENCHMARK_PHASE_BASELINE:
      return true;
    case BENCHMARK_PHASE_DAY_DATE:
      return benchmark_build_day_date();
    case BENCHMARK_PHASE_CLOCK_RING:
      return benchmark_build_clock_ring();
    case BENCHMARK_PHASE_HOUR_MINUTE_HANDS:
      return benchmark_build_clock_ring() && benchmark_build_hour_minute_hands();
    case BENCHMARK_PHASE_SECOND_HAND:
      return benchmark_build_clock_ring() && benchmark_build_hour_minute_hands() && benchmark_build_second_hand();
    case BENCHMARK_PHASE_CENTER_DISC:
      return benchmark_build_center_disc();
    case BENCHMARK_PHASE_WEATHER_COPY:
      return benchmark_build_center_disc() && benchmark_build_weather_copy();
    case BENCHMARK_PHASE_EDGE_INDICATOR:
      return benchmark_build_edge_indicator();
    case BENCHMARK_PHASE_COMPLETE:
      return true;
  }

  return false;
}

static bool benchmark_apply_stage_scene(benchmark_pass_t pass, benchmark_phase_t phase)
{
  if (pass == BENCHMARK_PASS_CUMULATIVE) {
    return benchmark_apply_cumulative_stage(phase);
  }

  return benchmark_apply_isolated_stage(phase);
}

static void benchmark_force_refresh(void)
{
  if (s_benchmark.screen != NULL) {
    lv_obj_invalidate(s_benchmark.screen);
  }

  if (s_benchmark.display != NULL) {
    esp_err_t err = esp_lv_adapter_refresh_now(s_benchmark.display);
    if (err != ESP_OK) {
      s_benchmark.runtime.refresh_failure_count += 1;
      ESP_LOGW(TAG, "Forced refresh returned %s", esp_err_to_name(err));
    }
  }
}

static void benchmark_stage_begin(benchmark_pass_t pass, benchmark_phase_t phase)
{
  benchmark_memory_snapshot_t snapshot;

  memset(&s_benchmark.stage, 0, sizeof(s_benchmark.stage));
  s_benchmark.stage.pass = pass;
  s_benchmark.stage.phase = phase;
  s_benchmark.stage.active = true;
  s_benchmark.stage.waiting_for_activation_frame = true;
  s_benchmark.stage.start_us = esp_timer_get_time();
  s_benchmark.stage.watchdog_warning_start = s_benchmark.runtime.watchdog_warning_count;
  s_benchmark.stage.draw_buffer_failure_start = s_benchmark.runtime.draw_buffer_failure_count;
  s_benchmark.stage.lock_failure_start = s_benchmark.runtime.lock_failure_count;
  s_benchmark.stage.refresh_failure_start = s_benchmark.runtime.refresh_failure_count;
  s_benchmark.stage.object_alloc_failure_start = s_benchmark.runtime.object_alloc_failure_count;

  benchmark_read_memory_snapshot(&snapshot);
  s_benchmark.stage.internal_free_start = snapshot.internal_free;
  s_benchmark.stage.internal_largest_start = snapshot.internal_largest;
  s_benchmark.stage.psram_free_start = snapshot.psram_free;
  s_benchmark.stage.psram_largest_start = snapshot.psram_largest;
  s_benchmark.stage.lvgl_free_start = snapshot.lvgl_mem.free_size;
  s_benchmark.stage.lvgl_biggest_start = snapshot.lvgl_mem.free_biggest_size;
  s_benchmark.stage.lvgl_used_pct_start = snapshot.lvgl_mem.used_pct;
  s_benchmark.stage.lvgl_frag_pct_start = snapshot.lvgl_mem.frag_pct;
  benchmark_update_stage_memory_metrics(&snapshot);
}

static int benchmark_compare_i32(const void *lhs, const void *rhs)
{
  const int32_t left = *(const int32_t *) lhs;
  const int32_t right = *(const int32_t *) rhs;

  if (left < right) {
    return -1;
  }
  if (left > right) {
    return 1;
  }
  return 0;
}

static float benchmark_stage_p95_frame_ms(const benchmark_stage_stats_t *stage)
{
  if (stage == NULL || stage->steady_frame_sample_count == 0) {
    return -1.0f;
  }

  int32_t sorted[BENCHMARK_FRAME_SAMPLE_CAP];
  memcpy(sorted, stage->steady_frame_samples_us, stage->steady_frame_sample_count * sizeof(sorted[0]));
  qsort(sorted, stage->steady_frame_sample_count, sizeof(sorted[0]), benchmark_compare_i32);

  size_t ordinal = ((stage->steady_frame_sample_count * 95U) + 99U) / 100U;
  if (ordinal == 0) {
    ordinal = 1;
  }
  size_t index = ordinal - 1;
  if (index >= stage->steady_frame_sample_count) {
    index = stage->steady_frame_sample_count - 1;
  }

  return us_to_ms(sorted[index]);
}

static void benchmark_stage_log_summary(void)
{
  benchmark_memory_snapshot_t snapshot;
  benchmark_stage_stats_t *stage = &s_benchmark.stage;

  if (!stage->active) {
    return;
  }

  benchmark_read_memory_snapshot(&snapshot);
  benchmark_update_stage_memory_metrics(&snapshot);

  ESP_LOGI(
    TAG,
    "BENCH_STAGE_SUMMARY pass=%s stage=%s redraw=%s build_ms=%.2f activation_frame_ms=%.2f "
    "activation_render_ms=%.2f activation_flush_ms=%.2f activation_wait_ms=%.2f "
    "steady_frames=%llu steady_avg_frame_ms=%.2f steady_p95_frame_ms=%.2f steady_max_frame_ms=%.2f "
    "steady_avg_render_ms=%.2f steady_avg_flush_ms=%.2f steady_avg_wait_ms=%.2f "
    "internal_free=%u internal_min=%u psram_free=%u psram_min=%u "
    "lvgl_free=%u lvgl_min=%u lvgl_biggest=%u lvgl_used_pct=%u lvgl_frag_pct=%u "
    "draw_buffer_failures=%llu lock_failures=%llu refresh_failures=%llu object_alloc_failures=%llu "
    "stall_warnings=%llu sample_count=%llu steady_sample_overflow=%u elapsed_ms=%lld",
    benchmark_pass_id(stage->pass),
    benchmark_phase_id(stage->phase),
    benchmark_redraw_mode(stage->pass, stage->phase),
    us_to_ms(stage->build_us),
    us_to_ms(stage->activation_frame_us),
    us_to_ms(stage->activation_render_us),
    us_to_ms(stage->activation_flush_us),
    us_to_ms(stage->activation_flush_wait_us),
    (unsigned long long) stage->steady_frame_count,
    average_us_to_ms(stage->steady_total_frame_us, stage->steady_frame_count),
    benchmark_stage_p95_frame_ms(stage),
    us_to_ms(stage->steady_max_frame_us),
    average_us_to_ms(stage->steady_total_render_us, stage->steady_frame_count),
    average_us_to_ms(stage->steady_total_flush_us, stage->steady_frame_count),
    average_us_to_ms(stage->steady_total_flush_wait_us, stage->steady_frame_count),
    (unsigned int) stage->current_internal_free,
    (unsigned int) stage->min_internal_free,
    (unsigned int) stage->current_psram_free,
    (unsigned int) stage->min_psram_free,
    (unsigned int) stage->current_lvgl_free,
    (unsigned int) stage->min_lvgl_free,
    (unsigned int) stage->current_lvgl_biggest,
    (unsigned int) stage->max_lvgl_used_pct,
    (unsigned int) stage->max_lvgl_frag_pct,
    (unsigned long long) (s_benchmark.runtime.draw_buffer_failure_count - stage->draw_buffer_failure_start),
    (unsigned long long) (s_benchmark.runtime.lock_failure_count - stage->lock_failure_start),
    (unsigned long long) (s_benchmark.runtime.refresh_failure_count - stage->refresh_failure_start),
    (unsigned long long) (s_benchmark.runtime.object_alloc_failure_count - stage->object_alloc_failure_start),
    (unsigned long long) (s_benchmark.runtime.watchdog_warning_count - stage->watchdog_warning_start),
    (unsigned long long) stage->sample_count,
    (unsigned int) stage->steady_frame_sample_overflow,
    (long long) ((esp_timer_get_time() - stage->start_us) / 1000)
  );

  stage->active = false;
}

static void benchmark_log_snapshot(const char *reason)
{
  benchmark_memory_snapshot_t snapshot;
  int64_t now_us = esp_timer_get_time();
  int64_t last_refresh_ms = -1;
  int64_t sample_gap_ms = -1;
  bool stall_warning = false;

  benchmark_read_memory_snapshot(&snapshot);
  benchmark_update_stage_memory_metrics(&snapshot);

  if (s_benchmark.runtime.last_refr_ready_us > 0) {
    last_refresh_ms = (now_us - s_benchmark.runtime.last_refr_ready_us) / 1000;
    if (last_refresh_ms > BENCHMARK_STALL_WARN_MS) {
      stall_warning = true;
    }
  }

  if (s_benchmark.runtime.last_sample_us > 0) {
    sample_gap_ms = (now_us - s_benchmark.runtime.last_sample_us) / 1000;
    if (sample_gap_ms > (CONFIG_RWD_CLOCK_BENCHMARK_SAMPLE_MS * 2)) {
      stall_warning = true;
    }
  }

  if (stall_warning) {
    s_benchmark.runtime.watchdog_warning_count += 1;
  }

  ESP_LOGI(
    TAG,
    "[%s] pass=%s phase=%s redraw=%s frames=%llu last_frame=%.2fms avg_frame=%.2fms max_frame=%.2fms "
    "last_render=%.2fms avg_render=%.2fms last_flush=%.2fms avg_flush=%.2fms "
    "heap_int=%u min_int=%u psram=%u min_psram=%u lvgl_free=%u lvgl_min=%u lvgl_used_pct=%u lvgl_frag_pct=%u "
    "watchdog=%s stall_count=%llu last_refresh_ms=%lld sample_gap_ms=%lld",
    reason,
    benchmark_pass_id(s_benchmark.pass),
    benchmark_phase_id(s_benchmark.stage.phase),
    benchmark_redraw_mode(s_benchmark.pass, s_benchmark.stage.phase),
    (unsigned long long) s_benchmark.runtime.frame_count,
    us_to_ms(s_benchmark.runtime.last_frame_us),
    average_us_to_ms(s_benchmark.runtime.total_frame_us, s_benchmark.runtime.frame_count),
    us_to_ms(s_benchmark.runtime.max_frame_us),
    us_to_ms(s_benchmark.runtime.last_render_us),
    average_us_to_ms(s_benchmark.runtime.total_render_us, s_benchmark.runtime.frame_count),
    us_to_ms(s_benchmark.runtime.last_flush_us),
    average_us_to_ms(s_benchmark.runtime.total_flush_us, s_benchmark.runtime.frame_count),
    (unsigned int) snapshot.internal_free,
    (unsigned int) s_benchmark.stage.min_internal_free,
    (unsigned int) snapshot.psram_free,
    (unsigned int) s_benchmark.stage.min_psram_free,
    (unsigned int) snapshot.lvgl_mem.free_size,
    (unsigned int) s_benchmark.stage.min_lvgl_free,
    (unsigned int) snapshot.lvgl_mem.used_pct,
    (unsigned int) snapshot.lvgl_mem.frag_pct,
    stall_warning ? "warning" : "ok",
    (unsigned long long) s_benchmark.runtime.watchdog_warning_count,
    (long long) last_refresh_ms,
    (long long) sample_gap_ms
  );

  s_benchmark.runtime.last_sample_us = now_us;
}

static void benchmark_display_event_cb(lv_event_t *event)
{
  benchmark_runtime_stats_t *runtime = &s_benchmark.runtime;
  benchmark_stage_stats_t *stage = &s_benchmark.stage;
  int64_t now_us = esp_timer_get_time();

  switch (lv_event_get_code(event)) {
    case LV_EVENT_REFR_START:
      runtime->current_refr_start_us = now_us;
      runtime->current_render_us = 0;
      runtime->current_flush_total_us = 0;
      runtime->current_flush_wait_total_us = 0;
      break;

    case LV_EVENT_RENDER_START:
      runtime->current_render_start_us = now_us;
      break;

    case LV_EVENT_RENDER_READY:
      if (runtime->current_render_start_us > 0) {
        runtime->current_render_us = now_us - runtime->current_render_start_us;
      }
      break;

    case LV_EVENT_FLUSH_START:
      runtime->current_flush_start_us = now_us;
      break;

    case LV_EVENT_FLUSH_FINISH:
      if (runtime->current_flush_start_us > 0) {
        runtime->current_flush_total_us += now_us - runtime->current_flush_start_us;
      }
      break;

    case LV_EVENT_FLUSH_WAIT_START:
      runtime->current_flush_wait_start_us = now_us;
      break;

    case LV_EVENT_FLUSH_WAIT_FINISH:
      if (runtime->current_flush_wait_start_us > 0) {
        runtime->current_flush_wait_total_us += now_us - runtime->current_flush_wait_start_us;
      }
      break;

    case LV_EVENT_REFR_READY:
      if (runtime->current_refr_start_us > 0) {
        runtime->last_frame_us = now_us - runtime->current_refr_start_us;
        runtime->last_render_us = runtime->current_render_us;
        runtime->last_flush_us = runtime->current_flush_total_us;
        runtime->last_flush_wait_us = runtime->current_flush_wait_total_us;
        runtime->last_refr_ready_us = now_us;
        runtime->frame_count += 1;

        runtime->total_frame_us += runtime->last_frame_us;
        runtime->total_render_us += runtime->last_render_us;
        runtime->total_flush_us += runtime->last_flush_us;
        runtime->total_flush_wait_us += runtime->last_flush_wait_us;

        if (runtime->last_frame_us > runtime->max_frame_us) {
          runtime->max_frame_us = runtime->last_frame_us;
        }
        if (runtime->last_render_us > runtime->max_render_us) {
          runtime->max_render_us = runtime->last_render_us;
        }
        if (runtime->last_flush_us > runtime->max_flush_us) {
          runtime->max_flush_us = runtime->last_flush_us;
        }
        if (runtime->last_flush_wait_us > runtime->max_flush_wait_us) {
          runtime->max_flush_wait_us = runtime->last_flush_wait_us;
        }

        if (stage->active) {
          if (stage->waiting_for_activation_frame) {
            stage->waiting_for_activation_frame = false;
            stage->activation_frame_us = runtime->last_frame_us;
            stage->activation_render_us = runtime->last_render_us;
            stage->activation_flush_us = runtime->last_flush_us;
            stage->activation_flush_wait_us = runtime->last_flush_wait_us;
          } else {
            stage->steady_frame_count += 1;
            stage->steady_total_frame_us += runtime->last_frame_us;
            stage->steady_total_render_us += runtime->last_render_us;
            stage->steady_total_flush_us += runtime->last_flush_us;
            stage->steady_total_flush_wait_us += runtime->last_flush_wait_us;

            if (runtime->last_frame_us > stage->steady_max_frame_us) {
              stage->steady_max_frame_us = runtime->last_frame_us;
            }
            if (runtime->last_render_us > stage->steady_max_render_us) {
              stage->steady_max_render_us = runtime->last_render_us;
            }
            if (runtime->last_flush_us > stage->steady_max_flush_us) {
              stage->steady_max_flush_us = runtime->last_flush_us;
            }
            if (runtime->last_flush_wait_us > stage->steady_max_flush_wait_us) {
              stage->steady_max_flush_wait_us = runtime->last_flush_wait_us;
            }

            if (stage->steady_frame_sample_count < BENCHMARK_FRAME_SAMPLE_CAP) {
              stage->steady_frame_samples_us[stage->steady_frame_sample_count++] = (int32_t) runtime->last_frame_us;
            } else {
              stage->steady_frame_sample_overflow += 1;
            }
          }
        }
      }
      break;

    default:
      break;
  }
}

static bool benchmark_activate_stage(benchmark_pass_t pass, benchmark_phase_t phase)
{
  if (pass == BENCHMARK_PASS_ISOLATED || s_benchmark.phase_index == 0) {
    if (!benchmark_build_shell()) {
      return false;
    }
  }

  benchmark_stage_begin(pass, phase);

  int64_t build_start_us = esp_timer_get_time();
  bool build_ok = benchmark_apply_stage_scene(pass, phase);
  s_benchmark.stage.build_us = esp_timer_get_time() - build_start_us;
  if (!build_ok) {
    return false;
  }

  lv_label_set_text_fmt(
    s_benchmark.phase_label,
    "Clock Benchmark: %s / %s",
    benchmark_pass_name(pass),
    benchmark_phase_name(phase)
  );
  lv_label_set_text_fmt(
    s_benchmark.info_label,
    "Timed benchmark mode. Stage=%s, redraw=%s.\nSerial logs separate build cost from the first frame and steady-state updates.",
    benchmark_phase_name(phase),
    benchmark_redraw_mode(pass, phase)
  );

  benchmark_update_clock_copy();
  benchmark_force_refresh();

  ESP_LOGI(
    TAG,
    "BENCH_STAGE_START pass=%s stage=%s redraw=%s build_ms=%.2f phase_interval_ms=%d sample_interval_ms=%d",
    benchmark_pass_id(pass),
    benchmark_phase_id(phase),
    benchmark_redraw_mode(pass, phase),
    us_to_ms(s_benchmark.stage.build_us),
    CONFIG_RWD_CLOCK_BENCHMARK_PHASE_MS,
    CONFIG_RWD_CLOCK_BENCHMARK_SAMPLE_MS
  );
  benchmark_log_snapshot("stage-start");
  return true;
}

static void benchmark_finish_run(void)
{
  s_benchmark.pass = BENCHMARK_PASS_COMPLETE;

  if (s_benchmark.phase_timer != NULL) {
    lv_timer_pause(s_benchmark.phase_timer);
  }
  if (s_benchmark.sample_timer != NULL) {
    lv_timer_pause(s_benchmark.sample_timer);
  }
  if (s_benchmark.clock_timer != NULL) {
    lv_timer_pause(s_benchmark.clock_timer);
  }

  if (s_benchmark.phase_label != NULL) {
    lv_label_set_text(s_benchmark.phase_label, "Clock Benchmark: Complete");
  }
  if (s_benchmark.info_label != NULL) {
    lv_label_set_text(
      s_benchmark.info_label,
      "Benchmark run complete.\nSee serial logs for per-stage cumulative and isolated summaries."
    );
  }

  ESP_LOGI(TAG, "BENCH_RUN_COMPLETE mode=%s", "timed");
  benchmark_log_snapshot("complete");
}

static void benchmark_advance(void)
{
  benchmark_stage_log_summary();

  if ((s_benchmark.phase_index + 1) < (sizeof(BENCHMARK_SEQUENCE) / sizeof(BENCHMARK_SEQUENCE[0]))) {
    s_benchmark.phase_index += 1;
    if (!benchmark_activate_stage(s_benchmark.pass, BENCHMARK_SEQUENCE[s_benchmark.phase_index])) {
      ESP_LOGE(TAG, "Benchmark activation failed for %s/%s", benchmark_pass_id(s_benchmark.pass), benchmark_phase_id(BENCHMARK_SEQUENCE[s_benchmark.phase_index]));
      benchmark_finish_run();
    }
    return;
  }

  benchmark_pass_t next_pass = benchmark_next_pass(s_benchmark.pass);
  if (next_pass == BENCHMARK_PASS_COMPLETE) {
    benchmark_finish_run();
    return;
  }

  s_benchmark.pass = next_pass;
  s_benchmark.phase_index = 0;
  if (!benchmark_activate_stage(s_benchmark.pass, BENCHMARK_SEQUENCE[s_benchmark.phase_index])) {
    ESP_LOGE(TAG, "Benchmark activation failed for %s/%s", benchmark_pass_id(s_benchmark.pass), benchmark_phase_id(BENCHMARK_SEQUENCE[s_benchmark.phase_index]));
    benchmark_finish_run();
  }
}

static void benchmark_phase_timer_cb(lv_timer_t *timer)
{
  (void) timer;
  benchmark_advance();
}

static void benchmark_sample_timer_cb(lv_timer_t *timer)
{
  (void) timer;
  benchmark_log_snapshot("sample");
}

static void benchmark_clock_timer_cb(lv_timer_t *timer)
{
  (void) timer;
  s_benchmark.benchmark_time += 1;
  benchmark_update_clock_copy();
}

esp_err_t app_benchmark_start(lv_display_t *display)
{
  if (display == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  memset(&s_benchmark, 0, sizeof(s_benchmark));
  s_benchmark.display = display;
  s_benchmark.benchmark_time = benchmark_initial_time();
  s_benchmark.pass = benchmark_initial_pass();

  if (s_previous_log_vprintf == NULL) {
    s_previous_log_vprintf = esp_log_set_vprintf(benchmark_log_vprintf);
    (void) s_previous_log_vprintf;
  }

  if (!benchmark_build_shell()) {
    return ESP_ERR_NO_MEM;
  }

  lv_display_add_event_cb(display, benchmark_display_event_cb, LV_EVENT_ALL, NULL);

  s_benchmark.clock_timer = benchmark_timer_create(benchmark_clock_timer_cb, 1000, "clock-timer");
  s_benchmark.sample_timer = benchmark_timer_create(
    benchmark_sample_timer_cb,
    CONFIG_RWD_CLOCK_BENCHMARK_SAMPLE_MS,
    "sample-timer"
  );
  s_benchmark.phase_timer = benchmark_timer_create(
    benchmark_phase_timer_cb,
    CONFIG_RWD_CLOCK_BENCHMARK_PHASE_MS,
    "phase-timer"
  );

  if (s_benchmark.clock_timer == NULL || s_benchmark.sample_timer == NULL || s_benchmark.phase_timer == NULL) {
    return ESP_ERR_NO_MEM;
  }

  s_benchmark.phase_index = 0;
  if (!benchmark_activate_stage(s_benchmark.pass, BENCHMARK_SEQUENCE[s_benchmark.phase_index])) {
    return ESP_ERR_NO_MEM;
  }

  ESP_LOGI(
    TAG,
    "Clock benchmark enabled. phase_interval_ms=%d sample_interval_ms=%d first_pass=%s",
    CONFIG_RWD_CLOCK_BENCHMARK_PHASE_MS,
    CONFIG_RWD_CLOCK_BENCHMARK_SAMPLE_MS,
    benchmark_pass_id(s_benchmark.pass)
  );
  return ESP_OK;
}
