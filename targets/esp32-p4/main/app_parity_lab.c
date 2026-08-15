#include "app_parity_lab.h"

#include "sdkconfig.h"

#include <math.h>
#include <stdbool.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

#include "app_runtime.h"
#include "assets/analog_center_cap.h"
#include "assets/analog_edge_indicator.h"
#include "assets/analog_hour_hand.h"
#include "assets/analog_hour_hand_flat.h"
#include "assets/analog_minute_hand.h"
#include "assets/analog_minute_hand_flat.h"
#include "assets/analog_stage_day.h"
#include "assets/analog_stage_night.h"
#include "assets/weather_icons.h"
#include "bsp/esp32_p4_wifi6_touch_lcd_xc.h"
#include "esp_lv_adapter.h"
#include "lvgl.h"

#define LAB_ANALOG_CENTER_X (BSP_LCD_H_RES / 2)
#define LAB_ANALOG_CENTER_Y (BSP_LCD_V_RES / 2)
#define LAB_DAY_TOP 130
#define LAB_DATE_TOP 178
#define LAB_WEATHER_ICON_OFFSET_Y -40
#define LAB_WEATHER_ICON_SCALE 512
#define LAB_WEATHER_ICON_OPACITY 92
#define LAB_HOUR_HAND_LENGTH 162
#define LAB_MINUTE_HAND_LENGTH 254
#define LAB_SECOND_HAND_LENGTH 276
#define LAB_HAND_TAIL_LENGTH 18
#define LAB_SECOND_HAND_TAIL_LENGTH 42
#define LAB_HOUR_ASSET_X 350
#define LAB_HOUR_ASSET_Y 228
#define LAB_HOUR_PIVOT_X 30
#define LAB_HOUR_PIVOT_Y 152
#define LAB_MINUTE_ASSET_X 360
#define LAB_MINUTE_ASSET_Y 138
#define LAB_MINUTE_PIVOT_X 20
#define LAB_MINUTE_PIVOT_Y 242
#define LAB_CENTER_CAP_X 364
#define LAB_CENTER_CAP_Y 344
#define LAB_EDGE_INDICATOR_X 560
#define LAB_EDGE_INDICATOR_Y 0
#define LAB_STATUS_STACK_BOTTOM 56
#define LAB_STALL_WARN_MS 2500
#define LAB_SUPERVISOR_POLL_MS 250
#define LAB_SUPERVISOR_RESTART_MS 4500
#define LAB_SUPERVISOR_STACK_WORDS 4096
#define LAB_LOG_BUFFER_CAP 2048
#define LAB_COMPONENT_LABEL_CAP 256
#define LAB_NVS_NAMESPACE "rwd_lab"
#define LAB_NVS_PENDING_KEY "pending_case"
#define LAB_NVS_DONE_KEY "done_case"
#define LAB_TOUCH_LEFT_BOUNDARY 240
#define LAB_TOUCH_RIGHT_BOUNDARY 560

#ifndef CONFIG_RWD_ANALOG_PARITY_LAB_PHASE_MS
#define CONFIG_RWD_ANALOG_PARITY_LAB_PHASE_MS 6000
#endif

#ifndef CONFIG_RWD_ANALOG_PARITY_LAB_SAMPLE_MS
#define CONFIG_RWD_ANALOG_PARITY_LAB_SAMPLE_MS 1000
#endif

typedef enum {
  LAB_COMPONENT_PLAIN_HOUR = 1U << 0,
  LAB_COMPONENT_PLAIN_MINUTE = 1U << 1,
  LAB_COMPONENT_PI_HOUR_ASSET = 1U << 2,
  LAB_COMPONENT_PI_MINUTE_ASSET = 1U << 3,
  LAB_COMPONENT_HAND_SHADING = 1U << 4,
  LAB_COMPONENT_SIMPLE_CENTER_CAP = 1U << 5,
  LAB_COMPONENT_METALLIC_CENTER_CAP = 1U << 6,
  LAB_COMPONENT_FLOATING_TEMP = 1U << 7,
  LAB_COMPONENT_EDGE_INDICATOR = 1U << 8,
  LAB_COMPONENT_STATUS_STYLE = 1U << 9,
  LAB_COMPONENT_NIGHT_SHIFT = 1U << 10,
} lab_component_t;

typedef struct {
  const char *id;
  const char *label;
  const char *group;
  uint32_t components;
} lab_case_def_t;

typedef struct {
  int64_t current_refr_start_us;
  int64_t current_render_start_us;
  int64_t current_flush_start_us;
  int64_t current_flush_wait_start_us;
  int64_t current_render_us;
  int64_t current_flush_total_us;
  int64_t current_flush_wait_total_us;
  int64_t last_refr_ready_us;
  int64_t last_frame_us;
  int64_t last_render_us;
  int64_t last_flush_us;
  int64_t last_flush_wait_us;
  int64_t total_frame_us;
  int64_t total_render_us;
  int64_t total_flush_us;
  int64_t total_flush_wait_us;
  int64_t max_frame_us;
  int64_t max_render_us;
  int64_t max_flush_us;
  int64_t max_flush_wait_us;
  uint64_t frame_count;
  uint64_t stall_warning_count;
  uint64_t draw_buffer_failure_count;
  uint64_t lock_failure_count;
  uint64_t refresh_failure_count;
  uint64_t object_alloc_failure_count;
} lab_runtime_stats_t;

typedef struct {
  size_t internal_free;
  size_t internal_largest;
  size_t psram_free;
  size_t psram_largest;
  lv_mem_monitor_t lvgl_mem;
} lab_memory_snapshot_t;

typedef struct {
  bool active;
  size_t case_index;
  uint32_t components;
  int64_t start_us;
  int64_t build_us;
  int64_t activation_frame_us;
  int64_t activation_render_us;
  int64_t activation_flush_us;
  int64_t activation_flush_wait_us;
  bool waiting_for_activation_frame;
  uint64_t steady_frame_count;
  int64_t steady_total_frame_us;
  int64_t steady_total_render_us;
  int64_t steady_total_flush_us;
  int64_t steady_total_flush_wait_us;
  int64_t steady_max_frame_us;
  int64_t steady_max_render_us;
  int64_t steady_max_flush_us;
  int64_t steady_max_flush_wait_us;
  size_t internal_free_start;
  size_t psram_free_start;
  size_t lvgl_free_start;
  size_t current_internal_free;
  size_t current_psram_free;
  size_t current_lvgl_free;
  size_t min_internal_free;
  size_t min_psram_free;
  size_t min_lvgl_free;
  uint32_t current_lvgl_used_pct;
  uint32_t current_lvgl_frag_pct;
  uint32_t max_lvgl_used_pct;
  uint32_t max_lvgl_frag_pct;
  uint64_t stall_warning_start;
  uint64_t draw_buffer_failure_start;
  uint64_t lock_failure_start;
  uint64_t refresh_failure_start;
  uint64_t object_alloc_failure_start;
  uint64_t sample_count;
} lab_case_stats_t;

typedef struct {
  lv_display_t *display;
  lv_obj_t *screen;
  lv_obj_t *root;
  lv_obj_t *hud_title;
  lv_obj_t *hud_subtitle;
  lv_obj_t *stage_image;
  lv_obj_t *weather_icon;
  lv_obj_t *day_label;
  lv_obj_t *date_label;
  lv_obj_t *plain_hour_hand;
  lv_obj_t *plain_minute_hand;
  lv_obj_t *second_hand;
  lv_obj_t *hour_asset;
  lv_obj_t *minute_asset;
  lv_obj_t *simple_cap_outer;
  lv_obj_t *simple_cap_inner;
  lv_obj_t *metallic_cap;
  lv_obj_t *temp_container;
  lv_obj_t *temp_label;
  lv_obj_t *high_low_label;
  lv_obj_t *edge_indicator;
  lv_obj_t *status_clock_label;
  lv_obj_t *status_weather_label;
  lv_timer_t *phase_timer;
  lv_timer_t *sample_timer;
  lv_timer_t *clock_timer;
  lab_runtime_stats_t runtime;
  lab_case_stats_t case_stats;
  lv_point_precise_t hour_points[2];
  lv_point_precise_t minute_points[2];
  lv_point_precise_t second_points[2];
  time_t display_time;
  bool autorun_enabled;
  bool supervisor_restart_requested;
  size_t case_index;
  nvs_handle_t nvs_handle;
  TaskHandle_t supervisor_task;
} lab_state_t;

static const char *TAG = "rwd_parity_lab";

static lab_state_t s_lab;
static vprintf_like_t s_previous_log_vprintf = NULL;

static const char *const WEEKDAY_NAMES[] = {
  "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};

static const char *const MONTH_NAMES[] = {
  "Jan", "Feb", "Mar", "Apr", "May", "Jun",
  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

static const app_weather_snapshot_t SAMPLE_DAY_WEATHER = {
  .valid = true,
  .stale = false,
  .temp = 89,
  .high = 90,
  .low = 72,
  .code = 3,
  .is_day = true,
  .thundersnow = false,
  .summary = "Partly Cloudy",
};

static const app_weather_snapshot_t SAMPLE_NIGHT_WEATHER = {
  .valid = true,
  .stale = false,
  .temp = 73,
  .high = 81,
  .low = 68,
  .code = 2,
  .is_day = false,
  .thundersnow = false,
  .summary = "Clear",
};

static const lab_case_def_t LAB_CASES[] = {
  { "baseline_slice2", "Baseline Slice 2", "individual", LAB_COMPONENT_PLAIN_HOUR | LAB_COMPONENT_PLAIN_MINUTE },
  { "plain_hour", "Plain Hour Hand", "individual", LAB_COMPONENT_PLAIN_HOUR },
  { "plain_minute", "Plain Minute Hand", "individual", LAB_COMPONENT_PLAIN_MINUTE },
  { "pi_hour_flat", "Pi Hour Asset", "individual", LAB_COMPONENT_PI_HOUR_ASSET | LAB_COMPONENT_PLAIN_MINUTE },
  { "pi_minute_flat", "Pi Minute Asset", "individual", LAB_COMPONENT_PI_MINUTE_ASSET | LAB_COMPONENT_PLAIN_HOUR },
  { "pi_shaded_hands", "Hand Decoration / Shading", "individual", LAB_COMPONENT_PI_HOUR_ASSET | LAB_COMPONENT_PI_MINUTE_ASSET | LAB_COMPONENT_HAND_SHADING },
  { "simple_center_cap", "Simple Center Cap", "individual", LAB_COMPONENT_PLAIN_HOUR | LAB_COMPONENT_PLAIN_MINUTE | LAB_COMPONENT_SIMPLE_CENTER_CAP },
  { "metallic_center_cap", "Pre-rendered Metallic Cap", "individual", LAB_COMPONENT_PLAIN_HOUR | LAB_COMPONENT_PLAIN_MINUTE | LAB_COMPONENT_METALLIC_CENTER_CAP },
  { "floating_temp", "Floating Temperature Block", "individual", LAB_COMPONENT_PLAIN_HOUR | LAB_COMPONENT_PLAIN_MINUTE | LAB_COMPONENT_FLOATING_TEMP },
  { "edge_indicator", "Edge Indicator", "individual", LAB_COMPONENT_PLAIN_HOUR | LAB_COMPONENT_PLAIN_MINUTE | LAB_COMPONENT_EDGE_INDICATOR },
  { "status_styling", "Status Styling", "individual", LAB_COMPONENT_PLAIN_HOUR | LAB_COMPONENT_PLAIN_MINUTE | LAB_COMPONENT_STATUS_STYLE },
  { "night_shift", "Night Shift Treatment", "individual", LAB_COMPONENT_PLAIN_HOUR | LAB_COMPONENT_PLAIN_MINUTE | LAB_COMPONENT_NIGHT_SHIFT },
  { "full_day_flat", "Full Day Parity (Flat Hands)", "cumulative", LAB_COMPONENT_PI_HOUR_ASSET | LAB_COMPONENT_PI_MINUTE_ASSET | LAB_COMPONENT_METALLIC_CENTER_CAP | LAB_COMPONENT_FLOATING_TEMP },
  { "full_day_shaded", "Full Day Parity (Shaded Hands)", "cumulative", LAB_COMPONENT_PI_HOUR_ASSET | LAB_COMPONENT_PI_MINUTE_ASSET | LAB_COMPONENT_HAND_SHADING | LAB_COMPONENT_METALLIC_CENTER_CAP | LAB_COMPONENT_FLOATING_TEMP },
  { "full_night_shaded", "Full Night Parity", "cumulative", LAB_COMPONENT_PI_HOUR_ASSET | LAB_COMPONENT_PI_MINUTE_ASSET | LAB_COMPONENT_HAND_SHADING | LAB_COMPONENT_METALLIC_CENTER_CAP | LAB_COMPONENT_FLOATING_TEMP | LAB_COMPONENT_NIGHT_SHIFT },
};

static void lab_phase_timer_cb(lv_timer_t *timer);
static void lab_sample_timer_cb(lv_timer_t *timer);
static void lab_clock_timer_cb(lv_timer_t *timer);
static void lab_touch_event_cb(lv_event_t *event);
static void lab_supervisor_task(void *arg);

static void lab_reset_scene_handles(void)
{
  s_lab.root = NULL;
  s_lab.hud_title = NULL;
  s_lab.hud_subtitle = NULL;
  s_lab.stage_image = NULL;
  s_lab.weather_icon = NULL;
  s_lab.day_label = NULL;
  s_lab.date_label = NULL;
  s_lab.plain_hour_hand = NULL;
  s_lab.plain_minute_hand = NULL;
  s_lab.second_hand = NULL;
  s_lab.hour_asset = NULL;
  s_lab.minute_asset = NULL;
  s_lab.simple_cap_outer = NULL;
  s_lab.simple_cap_inner = NULL;
  s_lab.metallic_cap = NULL;
  s_lab.temp_container = NULL;
  s_lab.temp_label = NULL;
  s_lab.high_low_label = NULL;
  s_lab.edge_indicator = NULL;
  s_lab.status_clock_label = NULL;
  s_lab.status_weather_label = NULL;
}

static lv_color_t color_bg(void)
{
  return lv_color_hex(0x07101c);
}

static lv_color_t color_text_primary(void)
{
  return lv_color_hex(0xf5f8ff);
}

static lv_color_t color_text_muted(void)
{
  return lv_color_hex(0xa6b6cf);
}

static lv_color_t color_temp(void)
{
  return lv_color_hex(0xfff1d1);
}

static lv_color_t color_night_primary(void)
{
  return lv_color_hex(0xecc0c0);
}

static lv_color_t color_night_muted(void)
{
  return lv_color_hex(0xc17a7a);
}

static lv_obj_t *app_screen_active(void)
{
#if LVGL_VERSION_MAJOR >= 9
  return lv_screen_active();
#else
  return lv_scr_act();
#endif
}

static float us_to_ms(int64_t value_us)
{
  return value_us <= 0 ? -1.0f : (float) value_us / 1000.0f;
}

static float average_us_to_ms(int64_t total_us, uint64_t count)
{
  return count == 0 ? -1.0f : ((float) total_us / (float) count) / 1000.0f;
}

static void lab_note_draw_buffer_failure_from_log(const char *line)
{
  if (line == NULL) {
    return;
  }

  if (
    strstr(line, "Failed to allocate dummy draw buffer") != NULL ||
    strstr(line, "alloc draw buffer failed") != NULL ||
    strstr(line, "Could not create draw buffer") != NULL
  ) {
    s_lab.runtime.draw_buffer_failure_count += 1;
  }

  if (strstr(line, "Could not lock LVGL") != NULL) {
    s_lab.runtime.lock_failure_count += 1;
  }
}

static int lab_log_vprintf(const char *fmt, va_list args)
{
  char buffer[LAB_LOG_BUFFER_CAP];
  va_list copy;
  va_copy(copy, args);
  int len = vsnprintf(buffer, sizeof(buffer), fmt, copy);
  va_end(copy);

  if (len <= 0) {
    return len;
  }

  lab_note_draw_buffer_failure_from_log(buffer);

  size_t bytes_to_write = (size_t) len;
  if (bytes_to_write >= sizeof(buffer)) {
    bytes_to_write = sizeof(buffer) - 1;
  }

  ssize_t write_result = write(STDOUT_FILENO, buffer, bytes_to_write);
  (void) write_result;
  return len;
}

static void lab_components_to_string(uint32_t components, char *buffer, size_t buffer_size)
{
  struct component_name {
    uint32_t flag;
    const char *name;
  };

  static const struct component_name NAMES[] = {
    { LAB_COMPONENT_PLAIN_HOUR, "plain_hour" },
    { LAB_COMPONENT_PLAIN_MINUTE, "plain_minute" },
    { LAB_COMPONENT_PI_HOUR_ASSET, "pi_hour_asset" },
    { LAB_COMPONENT_PI_MINUTE_ASSET, "pi_minute_asset" },
    { LAB_COMPONENT_HAND_SHADING, "hand_shading" },
    { LAB_COMPONENT_SIMPLE_CENTER_CAP, "simple_cap" },
    { LAB_COMPONENT_METALLIC_CENTER_CAP, "metallic_cap" },
    { LAB_COMPONENT_FLOATING_TEMP, "floating_temp" },
    { LAB_COMPONENT_EDGE_INDICATOR, "edge_indicator" },
    { LAB_COMPONENT_STATUS_STYLE, "status_style" },
    { LAB_COMPONENT_NIGHT_SHIFT, "night_shift" },
  };

  if (buffer == NULL || buffer_size == 0) {
    return;
  }

  buffer[0] = '\0';
  size_t used = 0;
  for (size_t i = 0; i < (sizeof(NAMES) / sizeof(NAMES[0])); ++i) {
    if ((components & NAMES[i].flag) == 0) {
      continue;
    }

    const char *separator = used == 0 ? "" : ",";
    int written = snprintf(buffer + used, buffer_size - used, "%s%s", separator, NAMES[i].name);
    if (written < 0) {
      break;
    }

    size_t written_size = (size_t) written;
    if (written_size >= (buffer_size - used)) {
      used = buffer_size - 1;
      break;
    }

    used += written_size;
  }

  if (used == 0) {
    snprintf(buffer, buffer_size, "none");
  }
}

static void lab_note_object_alloc_failure(const char *what)
{
  s_lab.runtime.object_alloc_failure_count += 1;
  ESP_LOGE(TAG, "Parity lab allocation failed for %s", what);
}

static lv_obj_t *lab_obj_create(lv_obj_t *parent, const char *what)
{
  lv_obj_t *obj = lv_obj_create(parent);
  if (obj == NULL) {
    lab_note_object_alloc_failure(what);
  }
  return obj;
}

static lv_obj_t *lab_label_create_raw(lv_obj_t *parent, const char *what)
{
  lv_obj_t *label = lv_label_create(parent);
  if (label == NULL) {
    lab_note_object_alloc_failure(what);
  }
  return label;
}

static lv_obj_t *lab_line_create(lv_obj_t *parent, const char *what)
{
  lv_obj_t *line = lv_line_create(parent);
  if (line == NULL) {
    lab_note_object_alloc_failure(what);
  }
  return line;
}

static lv_obj_t *lab_image_create(lv_obj_t *parent, const char *what)
{
  lv_obj_t *image = lv_image_create(parent);
  if (image == NULL) {
    lab_note_object_alloc_failure(what);
  }
  return image;
}

static lv_timer_t *lab_timer_create(lv_timer_cb_t cb, uint32_t period_ms, const char *what)
{
  lv_timer_t *timer = lv_timer_create(cb, period_ms, NULL);
  if (timer == NULL) {
    lab_note_object_alloc_failure(what);
  }
  return timer;
}

static lv_obj_t *create_label(
  lv_obj_t *parent,
  const lv_font_t *font,
  lv_color_t color,
  lv_text_align_t align,
  const char *text,
  const char *what
)
{
  lv_obj_t *label = lab_label_create_raw(parent, what);
  if (label == NULL) {
    return NULL;
  }

  lv_label_set_text(label, text != NULL ? text : "");
  lv_obj_set_style_text_color(label, color, 0);
  lv_obj_set_style_text_align(label, align, 0);
  if (font != NULL) {
    lv_obj_set_style_text_font(label, font, 0);
  }
  return label;
}

static void clear_container_chrome(lv_obj_t *obj)
{
  if (obj == NULL) {
    return;
  }

  lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  lv_obj_set_style_outline_width(obj, 0, 0);
  lv_obj_set_style_radius(obj, 0, 0);
  lv_obj_set_style_pad_all(obj, 0, 0);
  lv_obj_set_style_pad_row(obj, 0, 0);
  lv_obj_set_style_pad_column(obj, 0, 0);
  lv_obj_set_style_shadow_width(obj, 0, 0);
  lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

static void set_label_text(lv_obj_t *label, const char *text)
{
  if (label == NULL) {
    return;
  }

  const char *next_text = text != NULL ? text : "";
  const char *current_text = lv_label_get_text(label);
  if (current_text != NULL && strcmp(current_text, next_text) == 0) {
    return;
  }

  lv_label_set_text(label, next_text);
}

static void set_analog_hand_points(
  lv_obj_t *line,
  lv_point_precise_t points[2],
  int32_t tail_length,
  int32_t forward_length,
  double angle_deg
)
{
  if (line == NULL || points == NULL) {
    return;
  }

  double radians = (angle_deg - 90.0) * (3.14159265358979323846 / 180.0);
  double sin_value = sin(radians);
  double cos_value = cos(radians);

  points[0].x = LAB_ANALOG_CENTER_X - (cos_value * tail_length);
  points[0].y = LAB_ANALOG_CENTER_Y - (sin_value * tail_length);
  points[1].x = LAB_ANALOG_CENTER_X + (cos_value * forward_length);
  points[1].y = LAB_ANALOG_CENTER_Y + (sin_value * forward_length);
  lv_obj_invalidate(line);
}

static void lab_read_memory_snapshot(lab_memory_snapshot_t *snapshot)
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

static void lab_update_case_memory_metrics(const lab_memory_snapshot_t *snapshot)
{
  if (snapshot == NULL || !s_lab.case_stats.active) {
    return;
  }

  s_lab.case_stats.current_internal_free = snapshot->internal_free;
  s_lab.case_stats.current_psram_free = snapshot->psram_free;
  s_lab.case_stats.current_lvgl_free = snapshot->lvgl_mem.free_size;
  s_lab.case_stats.current_lvgl_used_pct = snapshot->lvgl_mem.used_pct;
  s_lab.case_stats.current_lvgl_frag_pct = snapshot->lvgl_mem.frag_pct;

  if (s_lab.case_stats.sample_count == 0) {
    s_lab.case_stats.min_internal_free = snapshot->internal_free;
    s_lab.case_stats.min_psram_free = snapshot->psram_free;
    s_lab.case_stats.min_lvgl_free = snapshot->lvgl_mem.free_size;
    s_lab.case_stats.max_lvgl_used_pct = snapshot->lvgl_mem.used_pct;
    s_lab.case_stats.max_lvgl_frag_pct = snapshot->lvgl_mem.frag_pct;
  } else {
    if (snapshot->internal_free < s_lab.case_stats.min_internal_free) {
      s_lab.case_stats.min_internal_free = snapshot->internal_free;
    }
    if (snapshot->psram_free < s_lab.case_stats.min_psram_free) {
      s_lab.case_stats.min_psram_free = snapshot->psram_free;
    }
    if (snapshot->lvgl_mem.free_size < s_lab.case_stats.min_lvgl_free) {
      s_lab.case_stats.min_lvgl_free = snapshot->lvgl_mem.free_size;
    }
    if (snapshot->lvgl_mem.used_pct > s_lab.case_stats.max_lvgl_used_pct) {
      s_lab.case_stats.max_lvgl_used_pct = snapshot->lvgl_mem.used_pct;
    }
    if (snapshot->lvgl_mem.frag_pct > s_lab.case_stats.max_lvgl_frag_pct) {
      s_lab.case_stats.max_lvgl_frag_pct = snapshot->lvgl_mem.frag_pct;
    }
  }

  s_lab.case_stats.sample_count += 1;
}

static const lab_case_def_t *lab_case_def(size_t index)
{
  if (index >= (sizeof(LAB_CASES) / sizeof(LAB_CASES[0]))) {
    return NULL;
  }

  return &LAB_CASES[index];
}

static time_t lab_case_base_time(bool night_mode)
{
  struct tm tm_value = {
    .tm_year = 2026 - 1900,
    .tm_mon = 7,
    .tm_mday = 7,
    .tm_hour = night_mode ? 23 : 10,
    .tm_min = 10,
    .tm_sec = 30,
    .tm_isdst = -1
  };
  return mktime(&tm_value);
}

static esp_err_t lab_persistence_write_i32(const char *key, int32_t value)
{
  if (s_lab.nvs_handle == 0) {
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t err = nvs_set_i32(s_lab.nvs_handle, key, value);
  if (err != ESP_OK) {
    return err;
  }

  return nvs_commit(s_lab.nvs_handle);
}

static int32_t lab_persistence_read_i32(const char *key, int32_t fallback)
{
  if (s_lab.nvs_handle == 0) {
    return fallback;
  }

  int32_t value = fallback;
  esp_err_t err = nvs_get_i32(s_lab.nvs_handle, key, &value);
  if (err != ESP_OK) {
    return fallback;
  }

  return value;
}

static void lab_mark_case_pending(size_t index)
{
  (void) lab_persistence_write_i32(LAB_NVS_PENDING_KEY, (int32_t) index);
}

static void lab_mark_case_complete(size_t index)
{
  (void) lab_persistence_write_i32(LAB_NVS_DONE_KEY, (int32_t) index);
  (void) lab_persistence_write_i32(LAB_NVS_PENDING_KEY, -1);
}

static size_t lab_initial_case_index(void)
{
  esp_reset_reason_t reset_reason = esp_reset_reason();
  int32_t pending_case = lab_persistence_read_i32(LAB_NVS_PENDING_KEY, -1);
  int32_t done_case = lab_persistence_read_i32(LAB_NVS_DONE_KEY, -1);
  size_t total_cases = sizeof(LAB_CASES) / sizeof(LAB_CASES[0]);

  if (reset_reason == ESP_RST_USB) {
    if (pending_case >= 0 || done_case >= 0) {
      ESP_LOGI(
        TAG,
        "LAB_RESET_PROGRESS reason=usb_reset pending_case=%" PRId32 " done_case=%" PRId32,
        pending_case,
        done_case
      );
    }
    (void) lab_persistence_write_i32(LAB_NVS_PENDING_KEY, -1);
    (void) lab_persistence_write_i32(LAB_NVS_DONE_KEY, -1);
    return 0;
  }

  if (pending_case >= 0) {
    size_t failed_index = (size_t) pending_case;
    size_t next_index = failed_index + 1;
    if (next_index >= total_cases) {
      next_index = total_cases - 1;
    }

    const lab_case_def_t *failed_case = lab_case_def(failed_index);
    const lab_case_def_t *next_case = lab_case_def(next_index);
    ESP_LOGW(
      TAG,
      "LAB_RESUME previous_case=%s next_case=%s reason=unclean_reboot",
      failed_case != NULL ? failed_case->id : "unknown",
      next_case != NULL ? next_case->id : "unknown"
    );
    return next_index;
  }

  if (done_case >= 0 && ((size_t) done_case + 1U) < total_cases) {
    return (size_t) done_case + 1U;
  }

  return 0;
}

static void lab_stage_begin(size_t case_index, uint32_t components)
{
  lab_memory_snapshot_t snapshot;

  memset(&s_lab.case_stats, 0, sizeof(s_lab.case_stats));
  s_lab.case_stats.active = true;
  s_lab.supervisor_restart_requested = false;
  s_lab.case_stats.case_index = case_index;
  s_lab.case_stats.components = components;
  s_lab.case_stats.start_us = esp_timer_get_time();
  s_lab.case_stats.waiting_for_activation_frame = true;
  s_lab.case_stats.stall_warning_start = s_lab.runtime.stall_warning_count;
  s_lab.case_stats.draw_buffer_failure_start = s_lab.runtime.draw_buffer_failure_count;
  s_lab.case_stats.lock_failure_start = s_lab.runtime.lock_failure_count;
  s_lab.case_stats.refresh_failure_start = s_lab.runtime.refresh_failure_count;
  s_lab.case_stats.object_alloc_failure_start = s_lab.runtime.object_alloc_failure_count;

  lab_read_memory_snapshot(&snapshot);
  s_lab.case_stats.internal_free_start = snapshot.internal_free;
  s_lab.case_stats.psram_free_start = snapshot.psram_free;
  s_lab.case_stats.lvgl_free_start = snapshot.lvgl_mem.free_size;
  lab_update_case_memory_metrics(&snapshot);
}

static void lab_force_refresh(void)
{
  if (s_lab.screen != NULL) {
    lv_obj_invalidate(s_lab.screen);
  }

  if (s_lab.display != NULL) {
    esp_err_t err = esp_lv_adapter_refresh_now(s_lab.display);
    if (err != ESP_OK) {
      s_lab.runtime.refresh_failure_count += 1;
      ESP_LOGW(TAG, "Forced refresh returned %s", esp_err_to_name(err));
    }
  }
}

static void lab_update_hand_states(const lab_case_def_t *case_def)
{
  if (case_def == NULL) {
    return;
  }

  bool night_mode = (case_def->components & LAB_COMPONENT_NIGHT_SHIFT) != 0;
  struct tm local_time;
  localtime_r(&s_lab.display_time, &local_time);

  double minute_angle = local_time.tm_min * 6.0;
  double hour_angle = ((local_time.tm_hour % 12) + (local_time.tm_min / 60.0)) * 30.0;
  double second_angle = local_time.tm_sec * 6.0;

  if (s_lab.plain_hour_hand != NULL) {
    lv_obj_set_style_line_color(
      s_lab.plain_hour_hand,
      night_mode ? lv_color_hex(0xb89191) : lv_color_hex(0xaeb7c1),
      0
    );
    set_analog_hand_points(
      s_lab.plain_hour_hand,
      s_lab.hour_points,
      LAB_HAND_TAIL_LENGTH,
      LAB_HOUR_HAND_LENGTH,
      hour_angle
    );
  }

  if (s_lab.plain_minute_hand != NULL) {
    lv_obj_set_style_line_color(
      s_lab.plain_minute_hand,
      night_mode ? lv_color_hex(0xa17676) : lv_color_hex(0x92a4b8),
      0
    );
    set_analog_hand_points(
      s_lab.plain_minute_hand,
      s_lab.minute_points,
      LAB_HAND_TAIL_LENGTH,
      LAB_MINUTE_HAND_LENGTH,
      minute_angle
    );
  }

  if (s_lab.second_hand != NULL) {
    lv_obj_set_style_line_color(
      s_lab.second_hand,
      night_mode ? lv_color_hex(0xbb8484) : lv_color_hex(0xd6e7f7),
      0
    );
    lv_obj_set_style_line_opa(s_lab.second_hand, night_mode ? LV_OPA_40 : LV_OPA_60, 0);
    set_analog_hand_points(
      s_lab.second_hand,
      s_lab.second_points,
      LAB_SECOND_HAND_TAIL_LENGTH,
      LAB_SECOND_HAND_LENGTH,
      second_angle
    );
  }

  if (s_lab.hour_asset != NULL) {
    lv_image_set_rotation(s_lab.hour_asset, (int32_t) lround(hour_angle * 10.0));
  }

  if (s_lab.minute_asset != NULL) {
    lv_image_set_rotation(s_lab.minute_asset, (int32_t) lround(minute_angle * 10.0));
  }
}

static void lab_update_copy(const lab_case_def_t *case_def)
{
  if (case_def == NULL) {
    return;
  }

  bool night_mode = (case_def->components & LAB_COMPONENT_NIGHT_SHIFT) != 0;
  struct tm local_time;
  localtime_r(&s_lab.display_time, &local_time);

  if (local_time.tm_wday >= 0 && local_time.tm_wday <= 6 && local_time.tm_mon >= 0 && local_time.tm_mon <= 11) {
    char date_text[16];
    snprintf(date_text, sizeof(date_text), "%s %d", MONTH_NAMES[local_time.tm_mon], local_time.tm_mday);
    set_label_text(s_lab.day_label, WEEKDAY_NAMES[local_time.tm_wday]);
    set_label_text(s_lab.date_label, date_text);
  }

  if (s_lab.day_label != NULL) {
    lv_obj_set_style_text_color(s_lab.day_label, night_mode ? color_night_primary() : color_text_primary(), 0);
    lv_obj_set_style_text_opa(s_lab.day_label, LV_OPA_70, 0);
  }

  if (s_lab.date_label != NULL) {
    lv_obj_set_style_text_color(s_lab.date_label, night_mode ? color_night_muted() : color_text_muted(), 0);
    lv_obj_set_style_text_opa(s_lab.date_label, LV_OPA_60, 0);
  }

  if (s_lab.temp_label != NULL) {
    set_label_text(s_lab.temp_label, night_mode ? "73°" : "89°");
    lv_obj_set_style_text_color(s_lab.temp_label, night_mode ? color_night_primary() : color_temp(), 0);
  }

  if (s_lab.high_low_label != NULL) {
    set_label_text(s_lab.high_low_label, night_mode ? "H:81°  L:68°" : "H:90°  L:72°");
    lv_obj_set_style_text_color(s_lab.high_low_label, night_mode ? color_night_muted() : color_text_primary(), 0);
  }

  if (s_lab.status_clock_label != NULL) {
    lv_obj_set_style_text_color(s_lab.status_clock_label, night_mode ? color_night_muted() : color_text_muted(), 0);
  }

  if (s_lab.status_weather_label != NULL) {
    lv_obj_set_style_text_color(s_lab.status_weather_label, color_text_muted(), 0);
  }

  lab_update_hand_states(case_def);
}

static bool lab_add_plain_hour_hand(void)
{
  s_lab.plain_hour_hand = lab_line_create(s_lab.root, "plain-hour-hand");
  if (s_lab.plain_hour_hand == NULL) {
    return false;
  }

  lv_obj_set_size(s_lab.plain_hour_hand, BSP_LCD_H_RES, BSP_LCD_V_RES);
  lv_obj_align(s_lab.plain_hour_hand, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_line_width(s_lab.plain_hour_hand, 10, 0);
  lv_obj_set_style_line_rounded(s_lab.plain_hour_hand, true, 0);
  lv_line_set_points_mutable(s_lab.plain_hour_hand, s_lab.hour_points, 2);
  return true;
}

static bool lab_add_plain_minute_hand(void)
{
  s_lab.plain_minute_hand = lab_line_create(s_lab.root, "plain-minute-hand");
  if (s_lab.plain_minute_hand == NULL) {
    return false;
  }

  lv_obj_set_size(s_lab.plain_minute_hand, BSP_LCD_H_RES, BSP_LCD_V_RES);
  lv_obj_align(s_lab.plain_minute_hand, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_line_width(s_lab.plain_minute_hand, 6, 0);
  lv_obj_set_style_line_rounded(s_lab.plain_minute_hand, true, 0);
  lv_line_set_points_mutable(s_lab.plain_minute_hand, s_lab.minute_points, 2);
  return true;
}

static bool lab_add_second_hand(void)
{
  s_lab.second_hand = lab_line_create(s_lab.root, "second-hand");
  if (s_lab.second_hand == NULL) {
    return false;
  }

  lv_obj_set_size(s_lab.second_hand, BSP_LCD_H_RES, BSP_LCD_V_RES);
  lv_obj_align(s_lab.second_hand, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_line_width(s_lab.second_hand, 3, 0);
  lv_obj_set_style_line_rounded(s_lab.second_hand, true, 0);
  lv_line_set_points_mutable(s_lab.second_hand, s_lab.second_points, 2);
  return true;
}

static bool lab_add_hour_asset(bool shaded)
{
  s_lab.hour_asset = lab_image_create(s_lab.root, "hour-asset");
  if (s_lab.hour_asset == NULL) {
    return false;
  }

  lv_image_set_src(s_lab.hour_asset, shaded ? &analog_hour_hand : &analog_hour_hand_flat);
  lv_image_set_pivot(s_lab.hour_asset, LAB_HOUR_PIVOT_X, LAB_HOUR_PIVOT_Y);
  lv_image_set_antialias(s_lab.hour_asset, false);
  lv_obj_align(s_lab.hour_asset, LV_ALIGN_TOP_LEFT, LAB_HOUR_ASSET_X, LAB_HOUR_ASSET_Y);
  return true;
}

static bool lab_add_minute_asset(bool shaded)
{
  s_lab.minute_asset = lab_image_create(s_lab.root, "minute-asset");
  if (s_lab.minute_asset == NULL) {
    return false;
  }

  lv_image_set_src(s_lab.minute_asset, shaded ? &analog_minute_hand : &analog_minute_hand_flat);
  lv_image_set_pivot(s_lab.minute_asset, LAB_MINUTE_PIVOT_X, LAB_MINUTE_PIVOT_Y);
  lv_image_set_antialias(s_lab.minute_asset, false);
  lv_obj_align(s_lab.minute_asset, LV_ALIGN_TOP_LEFT, LAB_MINUTE_ASSET_X, LAB_MINUTE_ASSET_Y);
  return true;
}

static bool lab_add_simple_center_cap(void)
{
  s_lab.simple_cap_outer = lab_obj_create(s_lab.root, "simple-cap-outer");
  if (s_lab.simple_cap_outer == NULL) {
    return false;
  }

  clear_container_chrome(s_lab.simple_cap_outer);
  lv_obj_set_size(s_lab.simple_cap_outer, 30, 30);
  lv_obj_align(s_lab.simple_cap_outer, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(s_lab.simple_cap_outer, lv_color_hex(0xe1e9f2), 0);
  lv_obj_set_style_bg_opa(s_lab.simple_cap_outer, LV_OPA_90, 0);
  lv_obj_set_style_radius(s_lab.simple_cap_outer, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_color(s_lab.simple_cap_outer, lv_color_hex(0x7d8796), 0);
  lv_obj_set_style_border_width(s_lab.simple_cap_outer, 1, 0);

  s_lab.simple_cap_inner = lab_obj_create(s_lab.simple_cap_outer, "simple-cap-inner");
  if (s_lab.simple_cap_inner == NULL) {
    return false;
  }

  clear_container_chrome(s_lab.simple_cap_inner);
  lv_obj_set_size(s_lab.simple_cap_inner, 12, 12);
  lv_obj_center(s_lab.simple_cap_inner);
  lv_obj_set_style_bg_color(s_lab.simple_cap_inner, lv_color_hex(0xb6c2d0), 0);
  lv_obj_set_style_bg_opa(s_lab.simple_cap_inner, LV_OPA_90, 0);
  lv_obj_set_style_radius(s_lab.simple_cap_inner, LV_RADIUS_CIRCLE, 0);
  return true;
}

static bool lab_add_metallic_center_cap(bool night_mode)
{
  s_lab.metallic_cap = lab_image_create(s_lab.root, "metallic-cap");
  if (s_lab.metallic_cap == NULL) {
    return false;
  }

  lv_image_set_src(s_lab.metallic_cap, &analog_center_cap);
  lv_obj_align(s_lab.metallic_cap, LV_ALIGN_TOP_LEFT, LAB_CENTER_CAP_X, LAB_CENTER_CAP_Y);
  if (night_mode) {
    lv_obj_set_style_image_recolor(s_lab.metallic_cap, lv_color_hex(0xd6aaaa), 0);
    lv_obj_set_style_image_recolor_opa(s_lab.metallic_cap, LV_OPA_30, 0);
  }
  return true;
}

static bool lab_add_floating_temp(bool night_mode)
{
  s_lab.temp_container = lab_obj_create(s_lab.root, "temp-container");
  if (s_lab.temp_container == NULL) {
    return false;
  }

  clear_container_chrome(s_lab.temp_container);
  lv_obj_set_size(s_lab.temp_container, 320, LV_SIZE_CONTENT);
  lv_obj_align(s_lab.temp_container, LV_ALIGN_CENTER, 0, 118);
  lv_obj_set_layout(s_lab.temp_container, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(s_lab.temp_container, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(
    s_lab.temp_container,
    LV_FLEX_ALIGN_CENTER,
    LV_FLEX_ALIGN_CENTER,
    LV_FLEX_ALIGN_CENTER
  );
  lv_obj_set_style_pad_row(s_lab.temp_container, 0, 0);

  s_lab.temp_label = create_label(
    s_lab.temp_container,
    &lv_font_montserrat_48,
    night_mode ? color_night_primary() : color_temp(),
    LV_TEXT_ALIGN_CENTER,
    night_mode ? "73°" : "89°",
    "temp-label"
  );
  if (s_lab.temp_label == NULL) {
    return false;
  }
  lv_obj_set_width(s_lab.temp_label, LV_PCT(100));
  lv_obj_set_style_text_letter_space(s_lab.temp_label, -2, 0);

  s_lab.high_low_label = create_label(
    s_lab.temp_container,
    &lv_font_montserrat_24,
    night_mode ? color_night_muted() : color_text_primary(),
    LV_TEXT_ALIGN_CENTER,
    night_mode ? "H:81°  L:68°" : "H:90°  L:72°",
    "high-low-label"
  );
  if (s_lab.high_low_label == NULL) {
    return false;
  }
  lv_obj_set_width(s_lab.high_low_label, LV_PCT(100));
  lv_obj_set_style_text_letter_space(s_lab.high_low_label, 1, 0);
  return true;
}

static bool lab_add_edge_indicator(void)
{
  s_lab.edge_indicator = lab_image_create(s_lab.root, "edge-indicator");
  if (s_lab.edge_indicator == NULL) {
    return false;
  }

  lv_image_set_src(s_lab.edge_indicator, &analog_edge_indicator);
  lv_obj_align(s_lab.edge_indicator, LV_ALIGN_TOP_LEFT, LAB_EDGE_INDICATOR_X, LAB_EDGE_INDICATOR_Y);
  return true;
}

static bool lab_add_status_labels(bool night_mode)
{
  s_lab.status_clock_label = create_label(
    s_lab.root,
    &lv_font_montserrat_18,
    night_mode ? color_night_muted() : color_text_muted(),
    LV_TEXT_ALIGN_CENTER,
    "Clock paused",
    "status-clock"
  );
  if (s_lab.status_clock_label == NULL) {
    return false;
  }
  lv_obj_set_width(s_lab.status_clock_label, 360);
  lv_obj_align(s_lab.status_clock_label, LV_ALIGN_BOTTOM_MID, 0, -(LAB_STATUS_STACK_BOTTOM + 24));
  lv_obj_set_style_text_opa(s_lab.status_clock_label, LV_OPA_70, 0);
  lv_obj_set_style_text_letter_space(s_lab.status_clock_label, 1, 0);

  s_lab.status_weather_label = create_label(
    s_lab.root,
    &lv_font_montserrat_18,
    color_text_muted(),
    LV_TEXT_ALIGN_CENTER,
    "Weather refresh delayed",
    "status-weather"
  );
  if (s_lab.status_weather_label == NULL) {
    return false;
  }
  lv_obj_set_width(s_lab.status_weather_label, 360);
  lv_obj_align(s_lab.status_weather_label, LV_ALIGN_BOTTOM_MID, 0, -LAB_STATUS_STACK_BOTTOM);
  lv_obj_set_style_text_opa(s_lab.status_weather_label, LV_OPA_70, 0);
  lv_obj_set_style_text_letter_space(s_lab.status_weather_label, 1, 0);
  return true;
}

static bool lab_build_case_scene(const lab_case_def_t *case_def)
{
  if (case_def == NULL) {
    return false;
  }

  bool night_mode = (case_def->components & LAB_COMPONENT_NIGHT_SHIFT) != 0;

  s_lab.screen = app_screen_active();
  if (s_lab.screen == NULL) {
    lab_note_object_alloc_failure("active-screen");
    return false;
  }

  lab_reset_scene_handles();
  lv_obj_clean(s_lab.screen);
  lv_obj_set_style_bg_color(s_lab.screen, color_bg(), 0);
  lv_obj_set_style_bg_opa(s_lab.screen, LV_OPA_COVER, 0);

  s_lab.root = lab_obj_create(s_lab.screen, "parity-root");
  if (s_lab.root == NULL) {
    return false;
  }
  lv_obj_set_size(s_lab.root, LV_PCT(100), LV_PCT(100));
  clear_container_chrome(s_lab.root);
  lv_obj_add_flag(s_lab.root, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(s_lab.root, lab_touch_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(s_lab.root, lab_touch_event_cb, LV_EVENT_LONG_PRESSED, NULL);

  s_lab.hud_title = create_label(
    s_lab.screen,
    &lv_font_montserrat_24,
    color_text_primary(),
    LV_TEXT_ALIGN_CENTER,
    "Analog Parity Lab",
    "hud-title"
  );
  if (s_lab.hud_title == NULL) {
    return false;
  }
  lv_obj_align(s_lab.hud_title, LV_ALIGN_TOP_MID, 0, 20);

  s_lab.hud_subtitle = create_label(
    s_lab.screen,
    &lv_font_montserrat_14,
    color_text_muted(),
    LV_TEXT_ALIGN_CENTER,
    case_def->label,
    "hud-subtitle"
  );
  if (s_lab.hud_subtitle == NULL) {
    return false;
  }
  lv_obj_set_width(s_lab.hud_subtitle, 680);
  lv_label_set_long_mode(s_lab.hud_subtitle, LV_LABEL_LONG_WRAP);
  lv_obj_align(s_lab.hud_subtitle, LV_ALIGN_BOTTOM_MID, 0, -18);

  s_lab.stage_image = lab_image_create(s_lab.root, "stage-image");
  if (s_lab.stage_image == NULL) {
    return false;
  }
  lv_image_set_src(s_lab.stage_image, night_mode ? &analog_stage_night : &analog_stage_day);
  lv_obj_align(s_lab.stage_image, LV_ALIGN_CENTER, 0, 0);

  s_lab.weather_icon = lab_image_create(s_lab.root, "weather-icon");
  if (s_lab.weather_icon == NULL) {
    return false;
  }
  lv_image_set_src(s_lab.weather_icon, weather_icon_image_for_snapshot(night_mode ? &SAMPLE_NIGHT_WEATHER : &SAMPLE_DAY_WEATHER));
  lv_image_set_scale(s_lab.weather_icon, LAB_WEATHER_ICON_SCALE);
  lv_obj_set_style_opa(s_lab.weather_icon, night_mode ? LV_OPA_TRANSP : LAB_WEATHER_ICON_OPACITY, 0);
  lv_obj_align(s_lab.weather_icon, LV_ALIGN_CENTER, 0, LAB_WEATHER_ICON_OFFSET_Y);

  s_lab.day_label = create_label(
    s_lab.root,
    &lv_font_montserrat_32,
    night_mode ? color_night_primary() : color_text_primary(),
    LV_TEXT_ALIGN_CENTER,
    "Friday",
    "day-label"
  );
  if (s_lab.day_label == NULL) {
    return false;
  }
  lv_obj_align(s_lab.day_label, LV_ALIGN_TOP_MID, 0, LAB_DAY_TOP);
  lv_obj_set_style_text_opa(s_lab.day_label, LV_OPA_70, 0);
  lv_obj_set_style_text_letter_space(s_lab.day_label, 1, 0);

  s_lab.date_label = create_label(
    s_lab.root,
    &lv_font_montserrat_24,
    night_mode ? color_night_muted() : color_text_muted(),
    LV_TEXT_ALIGN_CENTER,
    "Aug 7",
    "date-label"
  );
  if (s_lab.date_label == NULL) {
    return false;
  }
  lv_obj_align(s_lab.date_label, LV_ALIGN_TOP_MID, 0, LAB_DATE_TOP);
  lv_obj_set_style_text_opa(s_lab.date_label, LV_OPA_60, 0);

  if (!lab_add_second_hand()) {
    return false;
  }

  if ((case_def->components & LAB_COMPONENT_PLAIN_HOUR) != 0 && !lab_add_plain_hour_hand()) {
    return false;
  }

  if ((case_def->components & LAB_COMPONENT_PLAIN_MINUTE) != 0 && !lab_add_plain_minute_hand()) {
    return false;
  }

  if ((case_def->components & LAB_COMPONENT_PI_HOUR_ASSET) != 0
      && !lab_add_hour_asset((case_def->components & LAB_COMPONENT_HAND_SHADING) != 0)) {
    return false;
  }

  if ((case_def->components & LAB_COMPONENT_PI_MINUTE_ASSET) != 0
      && !lab_add_minute_asset((case_def->components & LAB_COMPONENT_HAND_SHADING) != 0)) {
    return false;
  }

  if ((case_def->components & LAB_COMPONENT_SIMPLE_CENTER_CAP) != 0 && !lab_add_simple_center_cap()) {
    return false;
  }

  if ((case_def->components & LAB_COMPONENT_METALLIC_CENTER_CAP) != 0 && !lab_add_metallic_center_cap(night_mode)) {
    return false;
  }

  if ((case_def->components & LAB_COMPONENT_FLOATING_TEMP) != 0 && !night_mode && !lab_add_floating_temp(false)) {
    return false;
  }

  if ((case_def->components & LAB_COMPONENT_EDGE_INDICATOR) != 0 && !lab_add_edge_indicator()) {
    return false;
  }

  if ((case_def->components & LAB_COMPONENT_STATUS_STYLE) != 0 && !lab_add_status_labels(night_mode)) {
    return false;
  }

  if (night_mode && s_lab.status_weather_label != NULL) {
    lv_obj_add_flag(s_lab.status_weather_label, LV_OBJ_FLAG_HIDDEN);
  }

  s_lab.display_time = lab_case_base_time(night_mode);
  lab_update_copy(case_def);
  return true;
}

static void lab_case_log_start(const lab_case_def_t *case_def)
{
  if (case_def == NULL) {
    return;
  }

  char components[LAB_COMPONENT_LABEL_CAP];
  lab_memory_snapshot_t snapshot;

  lab_components_to_string(case_def->components, components, sizeof(components));
  lab_read_memory_snapshot(&snapshot);
  lab_update_case_memory_metrics(&snapshot);

  ESP_LOGI(
    TAG,
    "LAB_CASE_START index=%u id=%s group=%s components=%s build_ms=%.2f case_interval_ms=%d "
    "internal_free=%u internal_min=%u psram_free=%u psram_min=%u lvgl_free=%u lvgl_min=%u",
    (unsigned int) s_lab.case_index,
    case_def->id,
    case_def->group,
    components,
    us_to_ms(s_lab.case_stats.build_us),
    CONFIG_RWD_ANALOG_PARITY_LAB_PHASE_MS,
    (unsigned int) snapshot.internal_free,
    (unsigned int) s_lab.case_stats.min_internal_free,
    (unsigned int) snapshot.psram_free,
    (unsigned int) s_lab.case_stats.min_psram_free,
    (unsigned int) snapshot.lvgl_mem.free_size,
    (unsigned int) s_lab.case_stats.min_lvgl_free
  );
}

static void lab_case_log_end(void)
{
  lab_memory_snapshot_t snapshot;
  const lab_case_def_t *case_def = lab_case_def(s_lab.case_index);
  char components[LAB_COMPONENT_LABEL_CAP];

  if (!s_lab.case_stats.active || case_def == NULL) {
    return;
  }

  lab_components_to_string(case_def->components, components, sizeof(components));
  lab_read_memory_snapshot(&snapshot);
  lab_update_case_memory_metrics(&snapshot);

  ESP_LOGI(
    TAG,
    "LAB_CASE_END index=%u id=%s group=%s components=%s activation_frame_ms=%.2f activation_render_ms=%.2f "
    "steady_frames=%llu avg_frame_ms=%.2f max_frame_ms=%.2f avg_render_ms=%.2f max_render_ms=%.2f "
    "internal_free=%u internal_min=%u psram_free=%u psram_min=%u lvgl_free=%u lvgl_min=%u "
    "lvgl_used_pct=%u lvgl_frag_pct=%u draw_buffer_failures=%llu lock_failures=%llu "
    "refresh_failures=%llu object_alloc_failures=%llu stall_warnings=%llu elapsed_ms=%lld",
    (unsigned int) s_lab.case_index,
    case_def->id,
    case_def->group,
    components,
    us_to_ms(s_lab.case_stats.activation_frame_us),
    us_to_ms(s_lab.case_stats.activation_render_us),
    (unsigned long long) s_lab.case_stats.steady_frame_count,
    average_us_to_ms(s_lab.case_stats.steady_total_frame_us, s_lab.case_stats.steady_frame_count),
    us_to_ms(s_lab.case_stats.steady_max_frame_us),
    average_us_to_ms(s_lab.case_stats.steady_total_render_us, s_lab.case_stats.steady_frame_count),
    us_to_ms(s_lab.case_stats.steady_max_render_us),
    (unsigned int) s_lab.case_stats.current_internal_free,
    (unsigned int) s_lab.case_stats.min_internal_free,
    (unsigned int) s_lab.case_stats.current_psram_free,
    (unsigned int) s_lab.case_stats.min_psram_free,
    (unsigned int) s_lab.case_stats.current_lvgl_free,
    (unsigned int) s_lab.case_stats.min_lvgl_free,
    (unsigned int) s_lab.case_stats.max_lvgl_used_pct,
    (unsigned int) s_lab.case_stats.max_lvgl_frag_pct,
    (unsigned long long) (s_lab.runtime.draw_buffer_failure_count - s_lab.case_stats.draw_buffer_failure_start),
    (unsigned long long) (s_lab.runtime.lock_failure_count - s_lab.case_stats.lock_failure_start),
    (unsigned long long) (s_lab.runtime.refresh_failure_count - s_lab.case_stats.refresh_failure_start),
    (unsigned long long) (s_lab.runtime.object_alloc_failure_count - s_lab.case_stats.object_alloc_failure_start),
    (unsigned long long) (s_lab.runtime.stall_warning_count - s_lab.case_stats.stall_warning_start),
    (long long) ((esp_timer_get_time() - s_lab.case_stats.start_us) / 1000)
  );

  lab_mark_case_complete(s_lab.case_index);
  s_lab.case_stats.active = false;
}

static void lab_log_stall_warning_if_needed(void)
{
  if (!s_lab.case_stats.active) {
    return;
  }

  int64_t now_us = esp_timer_get_time();
  if (s_lab.runtime.last_refr_ready_us <= 0) {
    return;
  }

  int64_t last_refresh_ms = (now_us - s_lab.runtime.last_refr_ready_us) / 1000;
  if (last_refresh_ms <= LAB_STALL_WARN_MS) {
    return;
  }

  s_lab.runtime.stall_warning_count += 1;
  const lab_case_def_t *case_def = lab_case_def(s_lab.case_index);
  ESP_LOGW(
    TAG,
    "LAB_STALL_WARN index=%u id=%s last_refresh_ms=%lld",
    (unsigned int) s_lab.case_index,
    case_def != NULL ? case_def->id : "unknown",
    (long long) last_refresh_ms
  );
}

static void lab_display_event_cb(lv_event_t *event)
{
  int64_t now_us = esp_timer_get_time();

  switch (lv_event_get_code(event)) {
    case LV_EVENT_REFR_START:
      s_lab.runtime.current_refr_start_us = now_us;
      s_lab.runtime.current_render_us = 0;
      s_lab.runtime.current_flush_total_us = 0;
      s_lab.runtime.current_flush_wait_total_us = 0;
      break;

    case LV_EVENT_RENDER_START:
      s_lab.runtime.current_render_start_us = now_us;
      break;

    case LV_EVENT_RENDER_READY:
      if (s_lab.runtime.current_render_start_us > 0) {
        s_lab.runtime.current_render_us = now_us - s_lab.runtime.current_render_start_us;
      }
      break;

    case LV_EVENT_FLUSH_START:
      s_lab.runtime.current_flush_start_us = now_us;
      break;

    case LV_EVENT_FLUSH_FINISH:
      if (s_lab.runtime.current_flush_start_us > 0) {
        s_lab.runtime.current_flush_total_us += now_us - s_lab.runtime.current_flush_start_us;
      }
      break;

    case LV_EVENT_FLUSH_WAIT_START:
      s_lab.runtime.current_flush_wait_start_us = now_us;
      break;

    case LV_EVENT_FLUSH_WAIT_FINISH:
      if (s_lab.runtime.current_flush_wait_start_us > 0) {
        s_lab.runtime.current_flush_wait_total_us += now_us - s_lab.runtime.current_flush_wait_start_us;
      }
      break;

    case LV_EVENT_REFR_READY:
      if (s_lab.runtime.current_refr_start_us > 0) {
        s_lab.runtime.last_frame_us = now_us - s_lab.runtime.current_refr_start_us;
        s_lab.runtime.last_render_us = s_lab.runtime.current_render_us;
        s_lab.runtime.last_flush_us = s_lab.runtime.current_flush_total_us;
        s_lab.runtime.last_flush_wait_us = s_lab.runtime.current_flush_wait_total_us;
        s_lab.runtime.last_refr_ready_us = now_us;
        s_lab.runtime.frame_count += 1;

        s_lab.runtime.total_frame_us += s_lab.runtime.last_frame_us;
        s_lab.runtime.total_render_us += s_lab.runtime.last_render_us;
        s_lab.runtime.total_flush_us += s_lab.runtime.last_flush_us;
        s_lab.runtime.total_flush_wait_us += s_lab.runtime.last_flush_wait_us;

        if (s_lab.runtime.last_frame_us > s_lab.runtime.max_frame_us) {
          s_lab.runtime.max_frame_us = s_lab.runtime.last_frame_us;
        }
        if (s_lab.runtime.last_render_us > s_lab.runtime.max_render_us) {
          s_lab.runtime.max_render_us = s_lab.runtime.last_render_us;
        }
        if (s_lab.runtime.last_flush_us > s_lab.runtime.max_flush_us) {
          s_lab.runtime.max_flush_us = s_lab.runtime.last_flush_us;
        }
        if (s_lab.runtime.last_flush_wait_us > s_lab.runtime.max_flush_wait_us) {
          s_lab.runtime.max_flush_wait_us = s_lab.runtime.last_flush_wait_us;
        }

        if (s_lab.case_stats.active) {
          if (s_lab.case_stats.waiting_for_activation_frame) {
            s_lab.case_stats.waiting_for_activation_frame = false;
            s_lab.case_stats.activation_frame_us = s_lab.runtime.last_frame_us;
            s_lab.case_stats.activation_render_us = s_lab.runtime.last_render_us;
            s_lab.case_stats.activation_flush_us = s_lab.runtime.last_flush_us;
            s_lab.case_stats.activation_flush_wait_us = s_lab.runtime.last_flush_wait_us;
          } else {
            s_lab.case_stats.steady_frame_count += 1;
            s_lab.case_stats.steady_total_frame_us += s_lab.runtime.last_frame_us;
            s_lab.case_stats.steady_total_render_us += s_lab.runtime.last_render_us;
            s_lab.case_stats.steady_total_flush_us += s_lab.runtime.last_flush_us;
            s_lab.case_stats.steady_total_flush_wait_us += s_lab.runtime.last_flush_wait_us;

            if (s_lab.runtime.last_frame_us > s_lab.case_stats.steady_max_frame_us) {
              s_lab.case_stats.steady_max_frame_us = s_lab.runtime.last_frame_us;
            }
            if (s_lab.runtime.last_render_us > s_lab.case_stats.steady_max_render_us) {
              s_lab.case_stats.steady_max_render_us = s_lab.runtime.last_render_us;
            }
            if (s_lab.runtime.last_flush_us > s_lab.case_stats.steady_max_flush_us) {
              s_lab.case_stats.steady_max_flush_us = s_lab.runtime.last_flush_us;
            }
            if (s_lab.runtime.last_flush_wait_us > s_lab.case_stats.steady_max_flush_wait_us) {
              s_lab.case_stats.steady_max_flush_wait_us = s_lab.runtime.last_flush_wait_us;
            }
          }
        }
      }
      break;

    default:
      break;
  }
}

static void lab_finish_run(void)
{
  if (s_lab.phase_timer != NULL) {
    lv_timer_pause(s_lab.phase_timer);
  }

  if (s_lab.sample_timer != NULL) {
    lv_timer_pause(s_lab.sample_timer);
  }

  if (s_lab.clock_timer != NULL) {
    lv_timer_pause(s_lab.clock_timer);
  }

  if (s_lab.hud_title != NULL) {
    set_label_text(s_lab.hud_title, "Analog Parity Lab Complete");
  }

  if (s_lab.hud_subtitle != NULL) {
    set_label_text(
      s_lab.hud_subtitle,
      "Auto-run finished. Tap left/right to review cases. Tap center to restart auto-run."
    );
  }

  ESP_LOGI(TAG, "LAB_RUN_COMPLETE cases=%u", (unsigned int) (sizeof(LAB_CASES) / sizeof(LAB_CASES[0])));
  (void) lab_persistence_write_i32(LAB_NVS_PENDING_KEY, -1);
  s_lab.supervisor_restart_requested = false;
}

static bool lab_activate_case(size_t index)
{
  const lab_case_def_t *case_def = lab_case_def(index);
  if (case_def == NULL) {
    return false;
  }

  s_lab.case_index = index;
  lab_mark_case_pending(index);
  lab_stage_begin(index, case_def->components);

  int64_t build_start_us = esp_timer_get_time();
  bool build_ok = lab_build_case_scene(case_def);
  s_lab.case_stats.build_us = esp_timer_get_time() - build_start_us;
  if (!build_ok) {
    return false;
  }

  lab_force_refresh();
  lab_case_log_start(case_def);
  return true;
}

static void lab_transition_to_case(size_t index)
{
  if (s_lab.case_stats.active) {
    lab_case_log_end();
  }

  if (!lab_activate_case(index)) {
    ESP_LOGE(TAG, "Parity lab activation failed for case index %u", (unsigned int) index);
    lab_finish_run();
    return;
  }

  if (s_lab.autorun_enabled && s_lab.phase_timer != NULL) {
    lv_timer_resume(s_lab.phase_timer);
    lv_timer_reset(s_lab.phase_timer);
  }
}

static void lab_advance_case(void)
{
  size_t total_cases = sizeof(LAB_CASES) / sizeof(LAB_CASES[0]);
  size_t next_index = s_lab.case_index + 1U;

  if (next_index >= total_cases) {
    lab_case_log_end();
    lab_finish_run();
    return;
  }

  lab_transition_to_case(next_index);
}

static void lab_phase_timer_cb(lv_timer_t *timer)
{
  (void) timer;
  lab_advance_case();
}

static void lab_sample_timer_cb(lv_timer_t *timer)
{
  (void) timer;
  lab_memory_snapshot_t snapshot;
  lab_read_memory_snapshot(&snapshot);
  lab_update_case_memory_metrics(&snapshot);
  lab_log_stall_warning_if_needed();
}

static void lab_clock_timer_cb(lv_timer_t *timer)
{
  (void) timer;
  const lab_case_def_t *case_def = lab_case_def(s_lab.case_index);
  if (case_def == NULL) {
    return;
  }

  s_lab.display_time += 1;
  lab_update_copy(case_def);
}

static void lab_touch_event_cb(lv_event_t *event)
{
  lv_event_code_t code = lv_event_get_code(event);
  lv_indev_t *indev = lv_indev_get_act();
  if (indev == NULL) {
    return;
  }

  lv_point_t point;
  lv_indev_get_point(indev, &point);

  if (code == LV_EVENT_LONG_PRESSED) {
    s_lab.autorun_enabled = true;
    if (s_lab.phase_timer != NULL) {
      lv_timer_resume(s_lab.phase_timer);
      lv_timer_reset(s_lab.phase_timer);
    }
    (void) lab_persistence_write_i32(LAB_NVS_DONE_KEY, -1);
    lab_transition_to_case(0);
    return;
  }

  if (code != LV_EVENT_CLICKED) {
    return;
  }

  if (point.x < LAB_TOUCH_LEFT_BOUNDARY) {
    size_t previous_index = s_lab.case_index == 0 ? 0 : s_lab.case_index - 1U;
    s_lab.autorun_enabled = false;
    if (s_lab.phase_timer != NULL) {
      lv_timer_pause(s_lab.phase_timer);
    }
    lab_transition_to_case(previous_index);
    return;
  }

  if (point.x > LAB_TOUCH_RIGHT_BOUNDARY) {
    size_t total_cases = sizeof(LAB_CASES) / sizeof(LAB_CASES[0]);
    size_t next_index = (s_lab.case_index + 1U) < total_cases ? s_lab.case_index + 1U : s_lab.case_index;
    s_lab.autorun_enabled = false;
    if (s_lab.phase_timer != NULL) {
      lv_timer_pause(s_lab.phase_timer);
    }
    lab_transition_to_case(next_index);
    return;
  }

  s_lab.autorun_enabled = !s_lab.autorun_enabled;
  if (s_lab.phase_timer != NULL) {
    if (s_lab.autorun_enabled) {
      lv_timer_resume(s_lab.phase_timer);
      lv_timer_reset(s_lab.phase_timer);
    } else {
      lv_timer_pause(s_lab.phase_timer);
    }
  }

  if (s_lab.hud_subtitle != NULL) {
    const lab_case_def_t *case_def = lab_case_def(s_lab.case_index);
    if (case_def != NULL) {
      lv_label_set_text_fmt(
        s_lab.hud_subtitle,
        "%s\nTouch: left=prev, right=next, center=%s auto, long-press=restart",
        case_def->label,
        s_lab.autorun_enabled ? "pause" : "resume"
      );
    }
  }
}

static void lab_supervisor_task(void *arg)
{
  (void) arg;
  const TickType_t poll_delay = pdMS_TO_TICKS(LAB_SUPERVISOR_POLL_MS);

  while (true) {
    vTaskDelay(poll_delay);

    if (!s_lab.case_stats.active || s_lab.supervisor_restart_requested) {
      continue;
    }

    int64_t last_refr_ready_us = s_lab.runtime.last_refr_ready_us;
    if (last_refr_ready_us <= 0) {
      continue;
    }

    int64_t now_us = esp_timer_get_time();
    int64_t last_refresh_ms = (now_us - last_refr_ready_us) / 1000;
    if (last_refresh_ms < LAB_SUPERVISOR_RESTART_MS) {
      continue;
    }

    const lab_case_def_t *case_def = lab_case_def(s_lab.case_index);
    s_lab.supervisor_restart_requested = true;
    ESP_LOGE(
      TAG,
      "LAB_SUPERVISOR_RESTART index=%u id=%s last_refresh_ms=%lld",
      (unsigned int) s_lab.case_index,
      case_def != NULL ? case_def->id : "unknown",
      (long long) last_refresh_ms
    );
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
  }
}

esp_err_t app_parity_lab_start(lv_display_t *display)
{
  if (display == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  memset(&s_lab, 0, sizeof(s_lab));
  s_lab.display = display;
  s_lab.autorun_enabled = true;

  esp_err_t nvs_err = nvs_open(LAB_NVS_NAMESPACE, NVS_READWRITE, &s_lab.nvs_handle);
  if (nvs_err != ESP_OK) {
    ESP_LOGE(TAG, "Could not open parity lab NVS namespace: %s", esp_err_to_name(nvs_err));
    return nvs_err;
  }

  if (s_previous_log_vprintf == NULL) {
    s_previous_log_vprintf = esp_log_set_vprintf(lab_log_vprintf);
    (void) s_previous_log_vprintf;
  }

  lv_display_add_event_cb(display, lab_display_event_cb, LV_EVENT_ALL, NULL);

  s_lab.phase_timer = lab_timer_create(lab_phase_timer_cb, CONFIG_RWD_ANALOG_PARITY_LAB_PHASE_MS, "phase-timer");
  s_lab.sample_timer = lab_timer_create(lab_sample_timer_cb, CONFIG_RWD_ANALOG_PARITY_LAB_SAMPLE_MS, "sample-timer");
  s_lab.clock_timer = lab_timer_create(lab_clock_timer_cb, 1000, "clock-timer");
  if (s_lab.phase_timer == NULL || s_lab.sample_timer == NULL || s_lab.clock_timer == NULL) {
    return ESP_ERR_NO_MEM;
  }

  if (xTaskCreatePinnedToCore(
        lab_supervisor_task,
        "lab-supervisor",
        LAB_SUPERVISOR_STACK_WORDS,
        NULL,
        5,
        &s_lab.supervisor_task,
        1
      ) != pdPASS) {
    ESP_LOGE(TAG, "Could not create parity lab supervisor task");
    return ESP_ERR_NO_MEM;
  }

  size_t initial_index = lab_initial_case_index();
  if (!lab_activate_case(initial_index)) {
    return ESP_ERR_NO_MEM;
  }

  ESP_LOGI(
    TAG,
    "Analog parity lab enabled. case_interval_ms=%d sample_interval_ms=%d start_case=%u total_cases=%u",
    CONFIG_RWD_ANALOG_PARITY_LAB_PHASE_MS,
    CONFIG_RWD_ANALOG_PARITY_LAB_SAMPLE_MS,
    (unsigned int) initial_index,
    (unsigned int) (sizeof(LAB_CASES) / sizeof(LAB_CASES[0]))
  );
  return ESP_OK;
}
