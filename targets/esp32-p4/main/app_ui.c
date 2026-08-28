#include "app_ui.h"
#include "sdkconfig.h"

#include <ctype.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "assets/analog_center_cap.h"
#include "assets/analog_edge_indicator.h"
#include "assets/analog_hour_hand.h"
#include "assets/analog_minute_hand.h"
#include "assets/analog_second_hand.h"
#include "assets/analog_stage_day.h"
#include "assets/analog_stage_night.h"
#include "assets/digital_stage_day.h"
#include "assets/digital_stage_night.h"
#include "assets/forecast_tomorrow_frame_day.h"
#include "assets/forecast_tomorrow_frame_night.h"
#include "assets/lv_font_clock_time_144.h"
#include "assets/lv_font_temp_102.h"
#include "assets/lv_font_temp_105.h"
#include "assets/round_stage_day.h"
#include "assets/round_stage_night.h"
#include "assets/weather_icon_clear_day.h"
#include "assets/weather_icons.h"
#include "bsp/esp32_p4_wifi6_touch_lcd_xc.h"
#include "connectivity.h"
#include "location_lookup.h"
#include "lvgl.h"
#include "message_service.h"
#include "weather_icon_names.h"

#define ANALOG_CENTER_X (BSP_LCD_H_RES / 2)
#define ANALOG_CENTER_Y (BSP_LCD_V_RES / 2)
/* This panel's visible round glass is mounted a bit high relative to the
 * pixel array (not a coordinate bug — ticks/numerals computed from
 * ANALOG_FACE_CENTER sit 50-100px from the true edge and are unaffected, but
 * the background ring at ~20px from the edge was cut off at the bottom
 * while touching the top without compensating for it). The compensating
 * -18px trim is now baked into the stage art itself (see FACE_CY in
 * tools/render_analog_stage_svg.py / render_screen_stage_svg.py) rather than
 * applied as a runtime lv_obj_align offset here — the offset-align approach
 * was tried first and confirmed (by hiding the image entirely) to itself be
 * the source of a separate bright horizontal-line rendering artifact at the
 * bottom of the screen. Foreground content (ticks, hands, labels) is
 * intentionally left unshifted since it already has enough margin. */
#define ANALOG_FACE_SIZE 760
#define ANALOG_FACE_CENTER (ANALOG_FACE_SIZE / 2)
#define ANALOG_DAY_TOP 130
#define ANALOG_DATE_TOP 178
#define ANALOG_WEATHER_ICON_OFFSET_Y -38
#define ANALOG_WEATHER_ICON_SCALE 512
#define ANALOG_WEATHER_ICON_OPACITY 92
#define ANALOG_TICK_COUNT 60
#define ANALOG_NUMERAL_COUNT 12
#define ANALOG_TICK_OUTER_RADIUS 348
#define ANALOG_NUMERAL_RADIUS 286
#define ANALOG_MINOR_TICK_LENGTH 16
#define ANALOG_MAJOR_TICK_LENGTH 24
#define ANALOG_QUARTER_TICK_LENGTH 26
#define ANALOG_HOUR_HAND_LENGTH 162
#define ANALOG_MINUTE_HAND_LENGTH 254
#define ANALOG_SECOND_HAND_LENGTH 276
#define ANALOG_HAND_TAIL_LENGTH 18
#define ANALOG_SECOND_HAND_TAIL_LENGTH 42
#define ANALOG_HOUR_ASSET_X 350
#define ANALOG_HOUR_ASSET_Y 228
#define ANALOG_HOUR_PIVOT_X 30
#define ANALOG_HOUR_PIVOT_Y 152
#define ANALOG_MINUTE_ASSET_X 360
#define ANALOG_MINUTE_ASSET_Y 138
#define ANALOG_MINUTE_PIVOT_X 20
#define ANALOG_MINUTE_PIVOT_Y 242
#define ANALOG_SECOND_ASSET_X 372
#define ANALOG_SECOND_ASSET_Y 118
#define ANALOG_SECOND_PIVOT_X 8
#define ANALOG_SECOND_PIVOT_Y 262
/* Must center the 72x72 cap image on the hands' shared pivot (380,380) —
 * i.e. pivot minus half the cap size. X was previously 364, landing the cap
 * 20px right of the true pivot, so hand tails poked out past its left edge. */
#define ANALOG_CENTER_CAP_X 344
#define ANALOG_CENTER_CAP_Y 344
/* Must match the crop offset render_edge_indicator.py prints when
 * regenerated (scripts/generate-analog-parity-assets.sh) -- the asset is
 * cropped to its content bounding box rather than stored as a full 800x800
 * canvas, so this positions that crop back where it belongs on the face. */
#define ANALOG_EDGE_INDICATOR_X 13
#define ANALOG_EDGE_INDICATOR_Y 99
#define ANALOG_TEMP_BLOCK_WIDTH 300
#define ANALOG_TEMP_BLOCK_TOP 472
#define ANALOG_TEMP_LABEL_HEIGHT 116
/* This label used to render at lv_font_montserrat_48 stretched ~2.19x via a
 * style transform_scale -- upscaling an already-rasterized bitmap glyph,
 * which visibly blurs it. It now uses lv_font_temp_105, a custom font
 * generated at its true 105px render size (see
 * targets/esp32-p4/scripts/generate-custom-fonts.sh), with no scale
 * transform. PIVOT_X/Y and EXT_DRAW_MARGIN below were tuned to compensate
 * for that scale transform's effect on positioning/clipping; they're inert
 * no-ops now that there's no scale, and left in place since removing them
 * isn't necessary and risks nothing by staying. */
#define ANALOG_TEMP_PIVOT_X 160
#define ANALOG_TEMP_PIVOT_Y 6
#define ANALOG_TEMP_LABEL_EXT_DRAW_MARGIN 200
#define EDGE_INDICATOR_PULSE_MIN_OPA 184
#define EDGE_INDICATOR_PULSE_MAX_OPA 255
#define EDGE_INDICATOR_IMPORTANT_NIGHT_TINT_OPA 66
#define EDGE_INDICATOR_UNREAD_NIGHT_TINT_OPA 46
#define EDGE_INDICATOR_PULSE_DURATION_MS 900
/* Unlike ANALOG_FACE_SIZE, nothing here has a rotating-hand pivot calibrated
 * against this value, so it's safe to grow independently of the analog
 * screen. Paired with FACE_RADIUS=390 in render_screen_stage_svg.py so the
 * digital/forecast/message background art matches. */
#define FACE_CONTENT_SIZE 780
#define DIGITAL_DAY_TOP 80
#define DIGITAL_DATE_TOP 115
/* 150 was correct for the old scaled lv_font_montserrat_48 (line_height 52,
 * base_line 9, scaled 3x -> effective line_height 156 / base_line 27, baseline
 * at 150+156-27=279). lv_font_clock_time_144 -- generated from only digits
 * and ':', with no descenders/tall accents to reserve room for -- reports a
 * much tighter line_height 104 / base_line 2, landing the baseline at
 * 150+104-2=252: 27px higher, which read as the time floating away from the
 * PM/divider line below it. +27 restores the original baseline position. */
#define DIGITAL_TIME_TOP 177
#define DIGITAL_TIME_WIDTH 620
#define DIGITAL_MERIDIEM_TOP 268
#define DIGITAL_TIME_GAP 14
#define DIGITAL_DIVIDER_WIDTH 500
#define DIGITAL_DIVIDER_TOP 300
#define DIGITAL_DIVIDER_BOTTOM 486
#define DIGITAL_CURRENT_TOP 320
#define DIGITAL_CURRENT_WIDTH 600
#define DIGITAL_CURRENT_HEIGHT 150
#define DIGITAL_CURRENT_TEMP_LEFT 25
#define DIGITAL_CURRENT_COPY_LEFT 400
#define DIGITAL_CURRENT_COPY_WIDTH 270
#define DIGITAL_CURRENT_ICON_SIZE 200
#define DIGITAL_FORECAST_TOP 500
#define DIGITAL_FORECAST_WIDTH 478
#define DIGITAL_FORECAST_ICON_SIZE 75
#define FORECAST_TOMORROW_FRAME_TOP 68
#define FORECAST_TOMORROW_LABEL_TOP 146
/* Reference keeps a 276px icon box at top:180 then CSS-scales it 1.34x around
 * its own center (transform-origin center), so the visual center stays at
 * 180+276/2=318 while it grows to ~370px. We instead set the box size
 * directly to 364px, so top must be 318-364/2=136 to land the same center;
 * using 180 here left the icon's true center 44px lower than the reference. */
#define FORECAST_TOMORROW_ICON_TOP 136
#define FORECAST_TOMORROW_ICON_SIZE 364
#define FORECAST_TOMORROW_TEMPS_TOP 464
#define FORECAST_ROW_ICON_SIZE 90
#define FORECAST_ROW1_X 205
#define FORECAST_ROW1_Y 600
#define FORECAST_ROW2_X 315
#define FORECAST_ROW2_Y 640
#define FORECAST_ROW3_X 435
#define FORECAST_ROW3_Y 640
#define FORECAST_ROW4_X 545
#define FORECAST_ROW4_Y 600
#define DAY_COUNT APP_FORECAST_DAYS
#define FORECAST_ROW_COUNT 4
#define SETUP_SWIPE_TOP_ZONE_HEIGHT 90
#define SETUP_KEYBOARD_BOTTOM_OFFSET -150
#define SETUP_KEYBOARD_HEIGHT 200
#define SETUP_KEYBOARD_WIDTH 620
#define SETUP_NETWORK_LIST_HEIGHT 250
#define SETUP_SCAN_MAX_RESULTS 8
#define SETUP_SCAN_TASK_PRIORITY 5
#define SETUP_SCAN_TASK_STACK_SIZE 6144
#define SETUP_LOCATION_TASK_PRIORITY 5
#define SETUP_LOCATION_TASK_STACK_SIZE 8192
#define SETUP_RESTART_DELAY_MS 1200
#define SWIPE_THRESHOLD_PX 70
#define SWIPE_AXIS_RATIO_X10 15
#define VALID_CLOCK_EPOCH 1704067200
#define CLOCK_STALE_WARNING_MS (135ULL * 1000ULL)
#define STAGE_PADDING 28
#define UI_ASYNC_LOCK_TIMEOUT_MS 250
#define SETUP_KB_BTN(width) (LV_BUTTONMATRIX_CTRL_POPOVER | (width))

typedef enum {
  APP_VIEW_ANALOG = 0,
  APP_VIEW_DIGITAL,
  APP_VIEW_FORECAST,
  APP_VIEW_MESSAGE
} app_view_mode_t;

typedef struct {
  lv_obj_t *stage;
  lv_obj_t *analog_view;
  lv_obj_t *digital_view;
  lv_obj_t *forecast_view;
  lv_obj_t *message_view;
  lv_obj_t *gesture_layer;
  lv_obj_t *night_overlay;
  lv_obj_t *debug_direct_weather_icon;
  lv_obj_t *setup_overlay;
  lv_obj_t *setup_scan_panel;
  lv_obj_t *setup_credentials_panel;
  lv_obj_t *setup_location_panel;
  lv_obj_t *setup_scan_status_label;
  lv_obj_t *setup_network_list;
  lv_obj_t *setup_status_label;
  lv_obj_t *setup_location_status_label;
  lv_obj_t *setup_ssid_textarea;
  lv_obj_t *setup_password_textarea;
  lv_obj_t *setup_location_textarea;
  lv_obj_t *setup_room_name_textarea;
  lv_obj_t *setup_device_id_textarea;
  lv_obj_t *setup_keyboard;
  lv_obj_t *setup_message_sharing_button;
  lv_obj_t *setup_face_button;
  lv_obj_t *setup_time_format_button;
  lv_obj_t *setup_leading_zero_button;
  lv_obj_t *setup_units_button;
  lv_obj_t *setup_night_shift_button;
  lv_obj_t *setup_prompt_button;
  lv_obj_t *setup_settings_button;
  lv_obj_t *setup_scan_nav_button;
  lv_obj_t *status_stack;
  lv_obj_t *clock_status_label;
  lv_obj_t *weather_status_label;
  lv_obj_t *analog_stage_image;
  lv_obj_t *analog_weather_layer;
  lv_obj_t *analog_face;
  lv_obj_t *analog_weather_icon;
  lv_obj_t *analog_tick_lines[ANALOG_TICK_COUNT];
  lv_obj_t *analog_numeral_labels[ANALOG_NUMERAL_COUNT];
  lv_obj_t *analog_day_label;
  lv_obj_t *analog_date_label;
  lv_obj_t *analog_summary_label;
  lv_obj_t *analog_temp_label;
  lv_obj_t *analog_high_low_label;
  lv_obj_t *analog_edge_indicator;
  lv_obj_t *analog_hour_asset;
  lv_obj_t *analog_minute_asset;
  lv_obj_t *analog_center_cap;
  lv_obj_t *analog_temp_container;
  lv_obj_t *digital_stage_image;
  lv_obj_t *digital_day_label;
  lv_obj_t *digital_date_label;
  lv_obj_t *digital_time_label;
  lv_obj_t *digital_meridiem_label;
  lv_obj_t *digital_current_icon;
  lv_obj_t *digital_temp_label;
  lv_obj_t *digital_summary_label;
  lv_obj_t *digital_high_low_label;
  lv_obj_t *digital_edge_indicator;
  lv_obj_t *digital_divider_top;
  lv_obj_t *digital_divider_bottom;
  lv_obj_t *digital_current_panel;
  lv_obj_t *digital_forecast_container;
  lv_obj_t *digital_forecast_day_labels[DAY_COUNT];
  lv_obj_t *digital_forecast_icons[DAY_COUNT];
  lv_obj_t *digital_forecast_temp_labels[DAY_COUNT];
  lv_obj_t *forecast_stage_image;
  lv_obj_t *forecast_tomorrow_frame;
  lv_obj_t *forecast_tomorrow_day_label;
  lv_obj_t *forecast_tomorrow_summary_label;
  lv_obj_t *forecast_tomorrow_icon;
  lv_obj_t *forecast_tomorrow_temps_label;
  lv_obj_t *forecast_row_day_labels[FORECAST_ROW_COUNT];
  lv_obj_t *forecast_row_summary_labels[FORECAST_ROW_COUNT];
  lv_obj_t *forecast_row_icons[FORECAST_ROW_COUNT];
  lv_obj_t *forecast_row_temps_labels[FORECAST_ROW_COUNT];
  lv_obj_t *message_stage_image;
  lv_obj_t *message_face;
  lv_obj_t *message_title_label;
  lv_obj_t *message_card;
  lv_obj_t *message_text_label;
  lv_obj_t *message_meta_label;
  lv_obj_t *message_dismiss_label;
  lv_obj_t *message_empty_label;
  lv_obj_t *clock_scale;
  lv_obj_t *hour_hand;
  lv_obj_t *minute_hand;
  lv_obj_t *second_hand;
  app_view_mode_t current_view;
  app_view_mode_t last_home_view;
  lv_point_t gesture_start;
  bool gesture_tracking;
  bool clock_is_estimated;
  bool night_overlay_active;
  bool setup_scan_in_progress;
  bool setup_location_lookup_in_progress;
  bool setup_restart_pending;
  bool has_unread_messages;
  bool has_important_messages;
  bool pending_weather_diag_logged;
  lv_point_precise_t analog_tick_points[ANALOG_TICK_COUNT][2];
  lv_point_precise_t hour_hand_points[2];
  lv_point_precise_t minute_hand_points[2];
  lv_point_precise_t second_hand_points[2];
  app_weather_snapshot_t weather;
  char forecast_labels[APP_FORECAST_DAYS][8];
  device_config_t config_snapshot;
  connectivity_scan_result_t scan_results[SETUP_SCAN_MAX_RESULTS];
  size_t scan_result_count;
  time_t fallback_epoch;
  int last_displayed_minute_key;
  int64_t last_minute_advance_ms;
  device_config_t setup_edit_config;
  time_t last_logged_weather_updated_at;
} app_ui_state_t;

static app_ui_state_t s_ui;
static const char *TAG = "rwd_app_ui";

typedef struct {
  device_config_t pending_config;
  char query[DEVICE_CONFIG_STR_LEN];
} setup_location_lookup_request_t;

static const char *const WEEKDAY_NAMES[] = {
  "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};
static const char *const WEEKDAY_NAMES_UPPER[] = {
  "SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"
};
static const char *const WEEKDAY_SHORT[] = {
  "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};
static const char *const MONTH_NAMES[] = {
  "Jan", "Feb", "Mar", "Apr", "May", "Jun",
  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};
static const char *const SETUP_KEYBOARD_LOWER_MAP[] = {
  "1#", "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", LV_SYMBOL_BACKSPACE, "\n",
  "ABC", "a", "s", "d", "f", "g", "h", "j", "k", "l", LV_SYMBOL_NEW_LINE, "\n",
  "_", "-", "^", "z", "x", "c", "v", "b", "n", "m", ".", ",", "\n",
  LV_SYMBOL_KEYBOARD,
#if LV_USE_ARABIC_PERSIAN_CHARS == 1
  LV_KEYBOARD_CTRL_BUTTON_MODE_TEXT_ARABIC,
#endif
  LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""
};

static const lv_buttonmatrix_ctrl_t SETUP_KEYBOARD_LOWER_CTRL_MAP[] = {
  LV_KEYBOARD_CTRL_BUTTON_FLAGS | 5, SETUP_KB_BTN(4), SETUP_KB_BTN(4), SETUP_KB_BTN(4), SETUP_KB_BTN(4),
  SETUP_KB_BTN(4), SETUP_KB_BTN(4), SETUP_KB_BTN(4), SETUP_KB_BTN(4), SETUP_KB_BTN(4), SETUP_KB_BTN(4),
  LV_BUTTONMATRIX_CTRL_CHECKED | 7,
  LV_KEYBOARD_CTRL_BUTTON_FLAGS | 6, SETUP_KB_BTN(3), SETUP_KB_BTN(3), SETUP_KB_BTN(3), SETUP_KB_BTN(3),
  SETUP_KB_BTN(3), SETUP_KB_BTN(3), SETUP_KB_BTN(3), SETUP_KB_BTN(3), SETUP_KB_BTN(3),
  LV_BUTTONMATRIX_CTRL_CHECKED | 7,
  LV_BUTTONMATRIX_CTRL_CHECKED | SETUP_KB_BTN(1), LV_BUTTONMATRIX_CTRL_CHECKED | SETUP_KB_BTN(1),
  SETUP_KB_BTN(1), SETUP_KB_BTN(1), SETUP_KB_BTN(1), SETUP_KB_BTN(1), SETUP_KB_BTN(1), SETUP_KB_BTN(1),
  SETUP_KB_BTN(1), SETUP_KB_BTN(1), LV_BUTTONMATRIX_CTRL_CHECKED | SETUP_KB_BTN(1),
  LV_BUTTONMATRIX_CTRL_CHECKED | SETUP_KB_BTN(1), LV_BUTTONMATRIX_CTRL_CHECKED | SETUP_KB_BTN(1),
  LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2,
#if LV_USE_ARABIC_PERSIAN_CHARS == 1
  LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2,
#endif
  LV_BUTTONMATRIX_CTRL_CHECKED | 2, 6, LV_BUTTONMATRIX_CTRL_CHECKED | 2, LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2
};

static const char *const SETUP_KEYBOARD_UPPER_MAP[] = {
  "1#", "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", LV_SYMBOL_BACKSPACE, "\n",
  "abc", "A", "S", "D", "F", "G", "H", "J", "K", "L", LV_SYMBOL_NEW_LINE, "\n",
  "_", "-", "^", "Z", "X", "C", "V", "B", "N", "M", ".", ",", "\n",
  LV_SYMBOL_CLOSE,
#if LV_USE_ARABIC_PERSIAN_CHARS == 1
  LV_KEYBOARD_CTRL_BUTTON_MODE_TEXT_ARABIC,
#endif
  LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""
};

static const lv_buttonmatrix_ctrl_t SETUP_KEYBOARD_UPPER_CTRL_MAP[] = {
  LV_KEYBOARD_CTRL_BUTTON_FLAGS | 5, SETUP_KB_BTN(4), SETUP_KB_BTN(4), SETUP_KB_BTN(4), SETUP_KB_BTN(4),
  SETUP_KB_BTN(4), SETUP_KB_BTN(4), SETUP_KB_BTN(4), SETUP_KB_BTN(4), SETUP_KB_BTN(4), SETUP_KB_BTN(4),
  LV_BUTTONMATRIX_CTRL_CHECKED | 7,
  LV_KEYBOARD_CTRL_BUTTON_FLAGS | 6, SETUP_KB_BTN(3), SETUP_KB_BTN(3), SETUP_KB_BTN(3), SETUP_KB_BTN(3),
  SETUP_KB_BTN(3), SETUP_KB_BTN(3), SETUP_KB_BTN(3), SETUP_KB_BTN(3), SETUP_KB_BTN(3),
  LV_BUTTONMATRIX_CTRL_CHECKED | 7,
  LV_BUTTONMATRIX_CTRL_CHECKED | SETUP_KB_BTN(1), LV_BUTTONMATRIX_CTRL_CHECKED | SETUP_KB_BTN(1),
  SETUP_KB_BTN(1), SETUP_KB_BTN(1), SETUP_KB_BTN(1), SETUP_KB_BTN(1), SETUP_KB_BTN(1), SETUP_KB_BTN(1),
  SETUP_KB_BTN(1), SETUP_KB_BTN(1), LV_BUTTONMATRIX_CTRL_CHECKED | SETUP_KB_BTN(1),
  LV_BUTTONMATRIX_CTRL_CHECKED | SETUP_KB_BTN(1), LV_BUTTONMATRIX_CTRL_CHECKED | SETUP_KB_BTN(1),
  LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2,
#if LV_USE_ARABIC_PERSIAN_CHARS == 1
  LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2,
#endif
  LV_BUTTONMATRIX_CTRL_CHECKED | 2, 6, LV_BUTTONMATRIX_CTRL_CHECKED | 2, LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2
};

static const char *const SETUP_KEYBOARD_SPECIAL_MAP[] = {
  "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", LV_SYMBOL_BACKSPACE, "\n",
  "abc", "+", "&", "/", "*", "=", "%", "!", "?", "#", "<", ">", "\n",
  "\\", "@", "$", "(", ")", "{", "}", "[", "]", ";", "\"", "'", "\n",
  LV_SYMBOL_KEYBOARD,
#if LV_USE_ARABIC_PERSIAN_CHARS == 1
  LV_KEYBOARD_CTRL_BUTTON_MODE_TEXT_ARABIC,
#endif
  LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, "^", LV_SYMBOL_OK, ""
};

static const lv_buttonmatrix_ctrl_t SETUP_KEYBOARD_SPECIAL_CTRL_MAP[] = {
  SETUP_KB_BTN(1), SETUP_KB_BTN(1), SETUP_KB_BTN(1), SETUP_KB_BTN(1), SETUP_KB_BTN(1),
  SETUP_KB_BTN(1), SETUP_KB_BTN(1), SETUP_KB_BTN(1), SETUP_KB_BTN(1), SETUP_KB_BTN(1),
  LV_BUTTONMATRIX_CTRL_CHECKED | 2,
  LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2, SETUP_KB_BTN(1), SETUP_KB_BTN(1), SETUP_KB_BTN(1),
  SETUP_KB_BTN(1), SETUP_KB_BTN(1), SETUP_KB_BTN(1), SETUP_KB_BTN(1), SETUP_KB_BTN(1),
  SETUP_KB_BTN(1), SETUP_KB_BTN(1), SETUP_KB_BTN(1),
  SETUP_KB_BTN(1), SETUP_KB_BTN(1), SETUP_KB_BTN(1), SETUP_KB_BTN(1), SETUP_KB_BTN(1),
  SETUP_KB_BTN(1), SETUP_KB_BTN(1), SETUP_KB_BTN(1), SETUP_KB_BTN(1), SETUP_KB_BTN(1),
  SETUP_KB_BTN(1), SETUP_KB_BTN(1),
  LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2,
#if LV_USE_ARABIC_PERSIAN_CHARS == 1
  LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2,
#endif
  LV_BUTTONMATRIX_CTRL_CHECKED | 2, 5, LV_BUTTONMATRIX_CTRL_CHECKED | 2, SETUP_KB_BTN(1),
  LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2
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

static lv_color_t color_digital_accent(void)
{
  return lv_color_hex(0x93d1ff);
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

static lv_color_t color_success(void)
{
  return lv_color_hex(0x9eea8b);
}

static lv_color_t color_warning(void)
{
  return lv_color_hex(0xf8c298);
}

static lv_color_t color_important(void)
{
  return lv_color_hex(0xf6d6b1);
}

static lv_obj_t *app_screen_active(void)
{
#if LVGL_VERSION_MAJOR >= 9
  return lv_screen_active();
#else
  return lv_scr_act();
#endif
}

static bool strings_equal_ignore_case(const char *a, const char *b)
{
  if (a == NULL || b == NULL) {
    return false;
  }

  while (*a != '\0' && *b != '\0') {
    if (tolower((unsigned char) *a) != tolower((unsigned char) *b)) {
      return false;
    }
    a++;
    b++;
  }

  return *a == '\0' && *b == '\0';
}

static void copy_uppercase_string(char *dest, size_t dest_size, const char *src)
{
  if (dest == NULL || dest_size == 0) {
    return;
  }

  if (src == NULL) {
    dest[0] = '\0';
    return;
  }

  size_t index = 0;
  while (src[index] != '\0' && index + 1 < dest_size) {
    dest[index] = (char) toupper((unsigned char) src[index]);
    index++;
  }
  dest[index] = '\0';
}

static int parse_hhmm_minutes(const char *value)
{
  if (value == NULL || strlen(value) != 5 || value[2] != ':') {
    return -1;
  }

  if (!isdigit((unsigned char) value[0]) || !isdigit((unsigned char) value[1])
      || !isdigit((unsigned char) value[3]) || !isdigit((unsigned char) value[4])) {
    return -1;
  }

  int hours = (value[0] - '0') * 10 + (value[1] - '0');
  int minutes = (value[3] - '0') * 10 + (value[4] - '0');
  if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59) {
    return -1;
  }

  return hours * 60 + minutes;
}

static bool is_night_shift_active(const device_config_t *config, const struct tm *local_time)
{
  if (config == NULL || local_time == NULL || !config->night_shift_enabled) {
    return false;
  }

  int start = parse_hhmm_minutes(config->night_shift_start);
  int end = parse_hhmm_minutes(config->night_shift_end);
  if (start < 0 || end < 0) {
    return false;
  }

  int now_minutes = local_time->tm_hour * 60 + local_time->tm_min;
  if (start == end) {
    return true;
  }
  if (start < end) {
    return now_minutes >= start && now_minutes < end;
  }

  return now_minutes >= start || now_minutes < end;
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

static lv_obj_t *create_label(
  lv_obj_t *parent,
  const lv_font_t *font,
  lv_color_t color,
  lv_text_align_t align,
  const char *text
)
{
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, text != NULL ? text : "");
  lv_obj_set_style_text_color(label, color, 0);
  lv_obj_set_style_text_align(label, align, 0);
  if (font != NULL) {
    lv_obj_set_style_text_font(label, font, 0);
  }
  return label;
}

static void style_panel(lv_obj_t *obj, int radius, lv_opa_t bg_opa)
{
  lv_obj_set_style_bg_color(obj, color_panel(), 0);
  lv_obj_set_style_bg_opa(obj, bg_opa, 0);
  lv_obj_set_style_border_color(obj, color_panel_border(), 0);
  lv_obj_set_style_border_width(obj, 1, 0);
  lv_obj_set_style_radius(obj, radius, 0);
}

static void clear_container_chrome(lv_obj_t *obj)
{
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

static void analog_polar_point(double angle_deg, double radius, int32_t *out_x, int32_t *out_y)
{
  double radians = (angle_deg - 90.0) * (3.14159265358979323846 / 180.0);
  double cos_value = cos(radians);
  double sin_value = sin(radians);

  if (out_x != NULL) {
    *out_x = (int32_t) lround(ANALOG_FACE_CENTER + (cos_value * radius));
  }

  if (out_y != NULL) {
    *out_y = (int32_t) lround(ANALOG_FACE_CENTER + (sin_value * radius));
  }
}

static void position_object_centered(lv_obj_t *obj, int32_t center_x, int32_t center_y)
{
  if (obj == NULL) {
    return;
  }

  lv_obj_update_layout(obj);
  lv_obj_set_pos(
    obj,
    center_x - (lv_obj_get_width(obj) / 2),
    center_y - (lv_obj_get_height(obj) / 2)
  );
}

static void apply_analog_dial_palette(bool night_active)
{
  for (int i = 0; i < ANALOG_TICK_COUNT; ++i) {
    lv_obj_t *tick = s_ui.analog_tick_lines[i];
    if (tick == NULL) {
      continue;
    }

    lv_color_t color = lv_color_hex(0xffffff);
    lv_opa_t opacity = LV_OPA_40;
    int width = 2;

    if ((i % 15) == 0) {
      color = night_active ? lv_color_hex(0xe0acac) : lv_color_hex(0xf4f8ff);
      opacity = night_active ? 64 : LV_OPA_80;
      width = 4;
    } else if ((i % 5) == 0) {
      color = night_active ? lv_color_hex(0xd6a0a0) : lv_color_hex(0xffffff);
      opacity = night_active ? 56 : LV_OPA_70;
      width = 4;
    } else {
      color = night_active ? lv_color_hex(0xc17e7e) : lv_color_hex(0xffffff);
      opacity = night_active ? 32 : LV_OPA_50;
    }

    lv_obj_set_style_line_color(tick, color, 0);
    lv_obj_set_style_line_opa(tick, opacity, 0);
    lv_obj_set_style_line_width(tick, width, 0);
  }

  for (int i = 0; i < ANALOG_NUMERAL_COUNT; ++i) {
    lv_obj_t *numeral = s_ui.analog_numeral_labels[i];
    if (numeral == NULL) {
      continue;
    }

    int display_hour = i == 0 ? 12 : i;
    bool major = (display_hour % 3) == 0;
    lv_obj_set_style_text_color(
      numeral,
      major
        ? (night_active ? lv_color_hex(0xe1b6b6) : lv_color_hex(0xf4f8ff))
        : (night_active ? lv_color_hex(0xc69696) : lv_color_hex(0xe0e9f7)),
      0
    );
    lv_obj_set_style_text_opa(numeral, major ? LV_OPA_90 : 76, 0);
  }
}

static void build_analog_dial_objects(lv_obj_t *parent)
{
  if (parent == NULL) {
    return;
  }

  for (int i = 0; i < ANALOG_TICK_COUNT; ++i) {
    int32_t outer_x = 0;
    int32_t outer_y = 0;
    int32_t inner_x = 0;
    int32_t inner_y = 0;
    int tick_length = ANALOG_MINOR_TICK_LENGTH;

    if ((i % 15) == 0) {
      tick_length = ANALOG_QUARTER_TICK_LENGTH;
    } else if ((i % 5) == 0) {
      tick_length = ANALOG_MAJOR_TICK_LENGTH;
    }

    analog_polar_point(i * 6.0, ANALOG_TICK_OUTER_RADIUS, &outer_x, &outer_y);
    analog_polar_point(i * 6.0, ANALOG_TICK_OUTER_RADIUS - tick_length, &inner_x, &inner_y);

    s_ui.analog_tick_points[i][0].x = outer_x;
    s_ui.analog_tick_points[i][0].y = outer_y;
    s_ui.analog_tick_points[i][1].x = inner_x;
    s_ui.analog_tick_points[i][1].y = inner_y;

    s_ui.analog_tick_lines[i] = lv_line_create(parent);
    lv_obj_set_size(s_ui.analog_tick_lines[i], ANALOG_FACE_SIZE, ANALOG_FACE_SIZE);
    lv_obj_align(s_ui.analog_tick_lines[i], LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_line_rounded(s_ui.analog_tick_lines[i], true, 0);
    lv_line_set_points_mutable(s_ui.analog_tick_lines[i], s_ui.analog_tick_points[i], 2);
  }

  static const int8_t major_offsets[ANALOG_NUMERAL_COUNT][2] = {
    {0, -2}, {0, 0}, {0, 0}, {1, 0}, {0, 0}, {0, 0},
    {0, 2}, {0, 0}, {0, 0}, {-1, 0}, {0, 0}, {0, 0}
  };

  for (int i = 0; i < ANALOG_NUMERAL_COUNT; ++i) {
    int display_hour = i == 0 ? 12 : i;
    bool major = (display_hour % 3) == 0;
    int32_t center_x = 0;
    int32_t center_y = 0;
    char numeral_text[4];

    analog_polar_point(i * 30.0, ANALOG_NUMERAL_RADIUS, &center_x, &center_y);
    center_x += major_offsets[i][0];
    center_y += major_offsets[i][1];
    snprintf(numeral_text, sizeof(numeral_text), "%d", display_hour);

    s_ui.analog_numeral_labels[i] = create_label(
      parent,
      major ? &lv_font_montserrat_32 : &lv_font_montserrat_24,
      color_text_primary(),
      LV_TEXT_ALIGN_CENTER,
      numeral_text
    );
    lv_obj_set_style_text_letter_space(s_ui.analog_numeral_labels[i], 0, 0);
    lv_obj_set_style_transform_scale(s_ui.analog_numeral_labels[i], major ? 384 : 362, 0);
    lv_obj_update_layout(s_ui.analog_numeral_labels[i]);
    lv_obj_set_style_transform_pivot_x(s_ui.analog_numeral_labels[i], lv_obj_get_width(s_ui.analog_numeral_labels[i]) / 2, 0);
    lv_obj_set_style_transform_pivot_y(s_ui.analog_numeral_labels[i], lv_obj_get_height(s_ui.analog_numeral_labels[i]) / 2, 0);
    position_object_centered(s_ui.analog_numeral_labels[i], center_x, center_y);
  }

  apply_analog_dial_palette(false);
}

static void log_object_bounds(
  const char *prefix,
  lv_obj_t *obj,
  const char *icon_name,
  const lv_image_dsc_t *asset,
  bool used_fallback
)
{
  int32_t x = 0;
  int32_t y = 0;
  int32_t width = 0;
  int32_t height = 0;
  bool hidden = true;

  if (obj != NULL) {
    lv_obj_update_layout(obj);
    x = lv_obj_get_x(obj);
    y = lv_obj_get_y(obj);
    width = lv_obj_get_width(obj);
    height = lv_obj_get_height(obj);
    hidden = lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);
  }

  ESP_LOGI(
    TAG,
    "%s: %s%s -> asset=%p cf=%u img=%ux%u stride=%u bytes=%u obj=%p hidden=%d pos=%" PRId32 ",%" PRId32 " size=%" PRId32 "x%" PRId32,
    prefix,
    icon_name != NULL ? icon_name : weather_icon_fallback_name(),
    used_fallback ? " (fallback)" : "",
    (const void *) asset,
    asset != NULL ? (unsigned int) asset->header.cf : 0U,
    asset != NULL ? (unsigned int) asset->header.w : 0U,
    asset != NULL ? (unsigned int) asset->header.h : 0U,
    asset != NULL ? (unsigned int) asset->header.stride : 0U,
    asset != NULL ? (unsigned int) asset->data_size : 0U,
    (void *) obj,
    hidden ? 1 : 0,
    x,
    y,
    width,
    height
  );
}

static void log_analog_hand_state(
  const char *name,
  lv_obj_t *obj,
  const lv_image_dsc_t *asset,
  int pivot_x,
  int pivot_y
)
{
  if (obj == NULL || asset == NULL) {
    ESP_LOGW(TAG, "Analog %s hand missing obj=%p asset=%p", name, (void *) obj, (const void *) asset);
    return;
  }

  lv_obj_update_layout(obj);
  ESP_LOGI(
    TAG,
    "Analog %s hand: asset=%p cf=%u pos=%" PRId32 ",%" PRId32 " size=%" PRId32 "x%" PRId32
    " pivot=%d,%d opa=%u hidden=%d",
    name,
    (const void *) asset,
    (unsigned) asset->header.cf,
    (int32_t) lv_obj_get_x(obj),
    (int32_t) lv_obj_get_y(obj),
    (int32_t) lv_obj_get_width(obj),
    (int32_t) lv_obj_get_height(obj),
    pivot_x,
    pivot_y,
    (unsigned) lv_obj_get_style_opa(obj, 0),
    lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) ? 1 : 0
  );
}

static void build_direct_weather_icon_probe(void)
{
#if CONFIG_RWD_DEBUG_DIRECT_WEATHER_ICON_TEST
  if (s_ui.debug_direct_weather_icon != NULL) {
    return;
  }

  s_ui.debug_direct_weather_icon = lv_image_create(app_screen_active());
  lv_image_set_src(s_ui.debug_direct_weather_icon, &weather_icon_clear_day);
  lv_obj_set_size(
    s_ui.debug_direct_weather_icon,
    weather_icon_clear_day.header.w,
    weather_icon_clear_day.header.h
  );
  lv_obj_center(s_ui.debug_direct_weather_icon);
  log_object_bounds(
    "DIRECT weather probe",
    s_ui.debug_direct_weather_icon,
    "clear-day",
    &weather_icon_clear_day,
    false
  );
#endif
}

static void log_digital_weather_diagnostics(bool live_weather)
{
  if (s_ui.digital_view != NULL) {
    lv_obj_update_layout(s_ui.digital_view);
  }

  const char *current_icon_name = live_weather
    ? weather_icon_name_for_snapshot(&s_ui.weather)
    : weather_icon_fallback_name();
  bool current_used_fallback = live_weather && !weather_icon_name_is_known(s_ui.weather.icon_name);
  const lv_image_dsc_t *current_asset = weather_icon_image_for_name(current_icon_name);

  log_object_bounds(
    "DIGITAL current icon",
    s_ui.digital_current_icon,
    current_icon_name,
    current_asset,
    current_used_fallback
  );

  if (s_ui.digital_current_panel != NULL) {
    log_object_bounds(
      "DIGITAL current panel",
      s_ui.digital_current_panel,
      current_icon_name,
      current_asset,
      current_used_fallback
    );
  }

  if (s_ui.digital_forecast_container != NULL) {
    log_object_bounds(
      "DIGITAL forecast container",
      s_ui.digital_forecast_container,
      current_icon_name,
      current_asset,
      current_used_fallback
    );
  }

  for (int i = 0; i < DAY_COUNT; ++i) {
    const char *forecast_icon_name = live_weather
      ? weather_icon_name_or_fallback(s_ui.weather.forecast[i].icon_name)
      : weather_icon_fallback_name();
    bool used_fallback = live_weather && !weather_icon_name_is_known(s_ui.weather.forecast[i].icon_name);
    const lv_image_dsc_t *forecast_asset = weather_icon_image_for_name(forecast_icon_name);
    int forecast_temp = live_weather ? s_ui.weather.forecast[i].high : 0;
    const char *forecast_day = s_ui.forecast_labels[i][0] != '\0' ? s_ui.forecast_labels[i] : "--";

    ESP_LOGI(
      TAG,
      "FORECAST[%d]: %s %s%s %d -> asset=%p obj=%p hidden=%d pos=%" PRId32 ",%" PRId32 " size=%" PRId32 "x%" PRId32,
      i,
      forecast_day,
      forecast_icon_name,
      used_fallback ? " (fallback)" : "",
      forecast_temp,
      (const void *) forecast_asset,
      (void *) s_ui.digital_forecast_icons[i],
      s_ui.digital_forecast_icons[i] != NULL && lv_obj_has_flag(s_ui.digital_forecast_icons[i], LV_OBJ_FLAG_HIDDEN) ? 1 : 0,
      s_ui.digital_forecast_icons[i] != NULL ? lv_obj_get_x(s_ui.digital_forecast_icons[i]) : 0,
      s_ui.digital_forecast_icons[i] != NULL ? lv_obj_get_y(s_ui.digital_forecast_icons[i]) : 0,
      s_ui.digital_forecast_icons[i] != NULL ? lv_obj_get_width(s_ui.digital_forecast_icons[i]) : 0,
      s_ui.digital_forecast_icons[i] != NULL ? lv_obj_get_height(s_ui.digital_forecast_icons[i]) : 0
    );
  }
}

static int32_t scale_dimension(int32_t value, int32_t scale)
{
  return (value * scale + 128) / 256;
}

static void layout_digital_time_group(bool show_meridiem)
{
  if (s_ui.digital_time_label == NULL || s_ui.digital_meridiem_label == NULL) {
    return;
  }

  lv_obj_update_layout(s_ui.digital_time_label);
  lv_obj_update_layout(s_ui.digital_meridiem_label);

  int32_t time_width = lv_obj_get_width(s_ui.digital_time_label);
  int32_t time_scale_x = lv_obj_get_style_transform_scale_x_safe(s_ui.digital_time_label, LV_PART_MAIN);
  int32_t scaled_time_width = scale_dimension(time_width, time_scale_x);
  int32_t meridiem_width = show_meridiem ? lv_obj_get_width(s_ui.digital_meridiem_label) : 0;
  int32_t group_width = scaled_time_width;
  if (show_meridiem) {
    group_width += DIGITAL_TIME_GAP + meridiem_width;
  }

  int32_t block_left = (FACE_CONTENT_SIZE - DIGITAL_TIME_WIDTH) / 2;
  int32_t group_left = block_left + (DIGITAL_TIME_WIDTH - group_width) / 2;
  if (group_left < block_left) {
    group_left = block_left;
  }

  lv_obj_set_pos(s_ui.digital_time_label, group_left, DIGITAL_TIME_TOP);

  if (!show_meridiem) {
    lv_obj_add_flag(s_ui.digital_meridiem_label, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  lv_obj_clear_flag(s_ui.digital_meridiem_label, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_pos(
    s_ui.digital_meridiem_label,
    group_left + scaled_time_width + DIGITAL_TIME_GAP,
    DIGITAL_MERIDIEM_TOP
  );
}

static void style_textarea(lv_obj_t *textarea)
{
  lv_obj_set_width(textarea, LV_PCT(100));
  lv_obj_set_height(textarea, 60);
  style_panel(textarea, 18, LV_OPA_90);
  lv_obj_set_style_pad_left(textarea, 16, 0);
  lv_obj_set_style_pad_right(textarea, 16, 0);
  lv_obj_set_style_pad_top(textarea, 14, 0);
  lv_obj_set_style_pad_bottom(textarea, 14, 0);
  lv_obj_set_style_shadow_width(textarea, 0, 0);
  lv_obj_set_style_text_color(textarea, color_text_primary(), 0);
  /* LVGL's textarea only draws a visible cursor rect if LV_PART_CURSOR has
   * a background -- text_color alone (the previous state here) has no
   * visible effect except very subtly recoloring whatever character
   * happens to already be under the cursor, which is invisible whenever
   * the cursor is at the end of the text (i.e. while actively typing).
   * These must be scoped to LV_STATE_FOCUSED (not bare LV_PART_CURSOR):
   * lv_textarea starts its cursor blink animation unconditionally at
   * construction with no focus-gating of its own, so an unscoped style
   * would render identically in every textarea regardless of which one
   * is actually focused. */
  lv_obj_set_style_bg_opa(textarea, LV_OPA_COVER, LV_PART_CURSOR | LV_STATE_FOCUSED);
  lv_obj_set_style_bg_color(textarea, color_accent(), LV_PART_CURSOR | LV_STATE_FOCUSED);
  lv_obj_set_style_radius(textarea, 3, LV_PART_CURSOR | LV_STATE_FOCUSED);
  lv_obj_set_style_text_color(textarea, color_panel(), LV_PART_CURSOR | LV_STATE_FOCUSED);
  lv_obj_set_style_anim_duration(textarea, 600, LV_PART_CURSOR | LV_STATE_FOCUSED);
}

static void set_object_opa_anim_cb(void *obj, int32_t value)
{
  if (obj == NULL) {
    return;
  }

  lv_obj_set_style_opa((lv_obj_t *) obj, (lv_opa_t) value, 0);
}

static void stop_indicator_pulse(lv_obj_t *indicator)
{
  if (indicator == NULL) {
    return;
  }

  lv_anim_delete(indicator, set_object_opa_anim_cb);
}

static void start_indicator_pulse(lv_obj_t *indicator)
{
  if (indicator == NULL) {
    return;
  }

  stop_indicator_pulse(indicator);

  lv_anim_t anim;
  lv_anim_init(&anim);
  lv_anim_set_var(&anim, indicator);
  lv_anim_set_exec_cb(&anim, set_object_opa_anim_cb);
  lv_anim_set_values(&anim, EDGE_INDICATOR_PULSE_MIN_OPA, EDGE_INDICATOR_PULSE_MAX_OPA);
  lv_anim_set_time(&anim, EDGE_INDICATOR_PULSE_DURATION_MS);
  lv_anim_set_playback_time(&anim, EDGE_INDICATOR_PULSE_DURATION_MS);
  lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&anim);
}

static void yield_ui_bootstrap(void)
{
  vTaskDelay(pdMS_TO_TICKS(1));
}

static void request_setup_scan(void);
static void hide_setup_keyboard(void);
static void open_setup_overlay(void);
static void apply_view_night_state(bool active);
static void setup_restart_timer_cb(lv_timer_t *timer);
static void setup_autopen_timer_cb(lv_timer_t *timer);
static void setup_keyboard_event_cb(lv_event_t *event);
static void show_setup_scan_panel(void);
static void show_setup_credentials_panel(
  const char *ssid,
  const char *password,
  bool focus_password,
  bool show_keyboard,
  const char *status_text
);
static void show_setup_location_panel(bool show_keyboard, const char *status_text);
static void populate_setup_network_list(void);
static void refresh_setup_settings_buttons(void);
static void refresh_message_ui_state(
  const app_message_snapshot_t *snapshot,
  uint32_t unread_count,
  bool has_important_message
);

static void reset_clock_health(void)
{
  s_ui.last_displayed_minute_key = -1;
  s_ui.last_minute_advance_ms = esp_timer_get_time() / 1000;
}

static void update_clock_health(const struct tm *local_time)
{
  if (local_time == NULL) {
    reset_clock_health();
    return;
  }

  int minute_key = (local_time->tm_yday * 1440) + (local_time->tm_hour * 60) + local_time->tm_min;
  int64_t now_ms = esp_timer_get_time() / 1000;
  if (minute_key != s_ui.last_displayed_minute_key) {
    s_ui.last_displayed_minute_key = minute_key;
    s_ui.last_minute_advance_ms = now_ms;
  }
}

static bool clock_is_stale(void)
{
  if (s_ui.last_displayed_minute_key < 0) {
    return false;
  }

  int64_t now_ms = esp_timer_get_time() / 1000;
  return (uint64_t) (now_ms - s_ui.last_minute_advance_ms) > CLOCK_STALE_WARNING_MS;
}

static bool is_setup_overlay_visible(void)
{
  return s_ui.setup_overlay != NULL && !lv_obj_has_flag(s_ui.setup_overlay, LV_OBJ_FLAG_HIDDEN);
}

static bool is_top_edge_swipe_start(const lv_point_t *point)
{
  return point != NULL && point->y <= SETUP_SWIPE_TOP_ZONE_HEIGHT;
}

static void update_setup_scan_status(const char *text, lv_color_t color)
{
  if (s_ui.setup_scan_status_label == NULL) {
    return;
  }

  lv_obj_set_style_text_color(s_ui.setup_scan_status_label, color, 0);
  set_label_text(s_ui.setup_scan_status_label, text);
}

static void update_setup_status(const char *text, lv_color_t color)
{
  if (s_ui.setup_status_label == NULL) {
    return;
  }

  lv_obj_set_style_text_color(s_ui.setup_status_label, color, 0);
  set_label_text(s_ui.setup_status_label, text);
}

static void update_setup_location_status(const char *text, lv_color_t color)
{
  if (s_ui.setup_location_status_label == NULL) {
    return;
  }

  lv_obj_set_style_text_color(s_ui.setup_location_status_label, color, 0);
  set_label_text(s_ui.setup_location_status_label, text);
}

static void set_setup_button_text(lv_obj_t *button, const char *text)
{
  if (button == NULL || text == NULL) {
    return;
  }

  lv_obj_t *label = lv_obj_get_child(button, 0);
  if (label == NULL) {
    return;
  }

  set_label_text(label, text);
}

static void refresh_setup_settings_buttons(void)
{
  char text[48];

  if (s_ui.setup_face_button != NULL) {
    snprintf(
      text,
      sizeof(text),
      "Home Screen: %s",
      strings_equal_ignore_case(s_ui.setup_edit_config.default_clock_face, "analog") ? "Analog" : "Digital"
    );
    set_setup_button_text(s_ui.setup_face_button, text);
  }

  if (s_ui.setup_time_format_button != NULL) {
    snprintf(
      text,
      sizeof(text),
      "Time Format: %s",
      strings_equal_ignore_case(s_ui.setup_edit_config.time_format, "24") ? "24-hour" : "12-hour"
    );
    set_setup_button_text(s_ui.setup_time_format_button, text);
  }

  if (s_ui.setup_leading_zero_button != NULL) {
    snprintf(
      text,
      sizeof(text),
      "Leading Zero: %s",
      s_ui.setup_edit_config.leading_zero_12h ? "On" : "Off"
    );
    set_setup_button_text(s_ui.setup_leading_zero_button, text);
  }

  if (s_ui.setup_units_button != NULL) {
    snprintf(
      text,
      sizeof(text),
      "Units: %s",
      strings_equal_ignore_case(s_ui.setup_edit_config.units, "metric") ? "Metric" : "Imperial"
    );
    set_setup_button_text(s_ui.setup_units_button, text);
  }

  if (s_ui.setup_night_shift_button != NULL) {
    snprintf(
      text,
      sizeof(text),
      "Night Shift: %s",
      s_ui.setup_edit_config.night_shift_enabled ? "On" : "Off"
    );
    set_setup_button_text(s_ui.setup_night_shift_button, text);
  }

  if (s_ui.setup_message_sharing_button != NULL) {
    snprintf(
      text,
      sizeof(text),
      "House Messages: %s",
      strings_equal_ignore_case(s_ui.setup_edit_config.message_sharing, "shared") ? "Shared" : "Single"
    );
    set_setup_button_text(s_ui.setup_message_sharing_button, text);
  }
}

static const char *default_location_prompt_text(void)
{
  return s_ui.setup_edit_config.location_ready
    ? "Update the weather location if you want to move this display."
    : "Enter a city and state, like Loveland, Ohio. We'll fill in timezone and coordinates automatically.";
}

static void schedule_setup_restart(void)
{
  s_ui.setup_restart_pending = true;
  hide_setup_keyboard();

  lv_timer_t *restart_timer = lv_timer_create(setup_restart_timer_cb, SETUP_RESTART_DELAY_MS, NULL);
  if (restart_timer != NULL) {
    lv_timer_set_repeat_count(restart_timer, 1);
  } else {
    esp_restart();
  }
}

static void hide_setup_keyboard(void)
{
  if (s_ui.setup_keyboard == NULL) {
    return;
  }

  lv_keyboard_set_textarea(s_ui.setup_keyboard, NULL);
  lv_obj_add_flag(s_ui.setup_keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void show_setup_keyboard(lv_obj_t *textarea)
{
  if (s_ui.setup_keyboard == NULL || textarea == NULL) {
    return;
  }

  lv_keyboard_set_textarea(s_ui.setup_keyboard, textarea);
  lv_keyboard_set_mode(s_ui.setup_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
  lv_obj_clear_flag(s_ui.setup_keyboard, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(s_ui.setup_keyboard);
}

static void setup_keyboard_event_cb(lv_event_t *event)
{
  lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_CANCEL || code == LV_EVENT_READY) {
    hide_setup_keyboard();
  }
}

static void close_setup_overlay(void)
{
  if (s_ui.setup_overlay == NULL) {
    return;
  }

  hide_setup_keyboard();
  lv_obj_add_flag(s_ui.setup_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void setup_autopen_timer_cb(lv_timer_t *timer)
{
  lv_timer_delete(timer);
  open_setup_overlay();
}

static void setup_restart_timer_cb(lv_timer_t *timer)
{
  lv_timer_delete(timer);
  esp_restart();
}

static void show_setup_scan_panel(void)
{
  if (s_ui.setup_scan_panel != NULL) {
    lv_obj_clear_flag(s_ui.setup_scan_panel, LV_OBJ_FLAG_HIDDEN);
  }
  if (s_ui.setup_credentials_panel != NULL) {
    lv_obj_add_flag(s_ui.setup_credentials_panel, LV_OBJ_FLAG_HIDDEN);
  }
  if (s_ui.setup_location_panel != NULL) {
    lv_obj_add_flag(s_ui.setup_location_panel, LV_OBJ_FLAG_HIDDEN);
  }

  if (s_ui.setup_scan_nav_button != NULL) {
    set_setup_button_text(s_ui.setup_scan_nav_button, s_ui.setup_edit_config.wifi_ready ? "Back" : "Cancel");
  }

  hide_setup_keyboard();
}

static void show_setup_credentials_panel(
  const char *ssid,
  const char *password,
  bool focus_password,
  bool show_keyboard,
  const char *status_text
)
{
  if (s_ui.setup_credentials_panel == NULL
      || s_ui.setup_ssid_textarea == NULL
      || s_ui.setup_password_textarea == NULL) {
    return;
  }

  lv_textarea_set_text(s_ui.setup_ssid_textarea, ssid != NULL ? ssid : "");
  lv_textarea_set_text(s_ui.setup_password_textarea, password != NULL ? password : "");
  update_setup_status(status_text != NULL ? status_text : "", color_text_muted());

  if (s_ui.setup_scan_panel != NULL) {
    lv_obj_add_flag(s_ui.setup_scan_panel, LV_OBJ_FLAG_HIDDEN);
  }
  if (s_ui.setup_location_panel != NULL) {
    lv_obj_add_flag(s_ui.setup_location_panel, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_clear_flag(s_ui.setup_credentials_panel, LV_OBJ_FLAG_HIDDEN);

  if (show_keyboard) {
    show_setup_keyboard(focus_password ? s_ui.setup_password_textarea : s_ui.setup_ssid_textarea);
  } else {
    hide_setup_keyboard();
  }
}

static void show_setup_location_panel(bool show_keyboard, const char *status_text)
{
  if (s_ui.setup_location_panel == NULL || s_ui.setup_location_textarea == NULL) {
    return;
  }

  if (s_ui.setup_scan_panel != NULL) {
    lv_obj_add_flag(s_ui.setup_scan_panel, LV_OBJ_FLAG_HIDDEN);
  }
  if (s_ui.setup_credentials_panel != NULL) {
    lv_obj_add_flag(s_ui.setup_credentials_panel, LV_OBJ_FLAG_HIDDEN);
  }

  if (s_ui.setup_edit_config.location_ready) {
    lv_textarea_set_text(s_ui.setup_location_textarea, s_ui.setup_edit_config.location);
  } else if (lv_textarea_get_text(s_ui.setup_location_textarea)[0] == '\0') {
    lv_textarea_set_text(s_ui.setup_location_textarea, "");
  }

  update_setup_location_status(status_text != NULL ? status_text : "", color_text_muted());
  refresh_setup_settings_buttons();
  lv_obj_clear_flag(s_ui.setup_location_panel, LV_OBJ_FLAG_HIDDEN);

  if (show_keyboard) {
    show_setup_keyboard(s_ui.setup_location_textarea);
  } else {
    hide_setup_keyboard();
  }
}

static void setup_textarea_focus_cb(lv_event_t *event)
{
  lv_obj_t *target = lv_event_get_target_obj(event);
  if (target == NULL) {
    return;
  }

  show_setup_keyboard(target);
}

static void setup_network_select_event_cb(lv_event_t *event)
{
  size_t index = (size_t) (uintptr_t) lv_event_get_user_data(event);
  if (index >= s_ui.scan_result_count) {
    return;
  }

  const connectivity_scan_result_t *network = &s_ui.scan_results[index];
  bool selected_existing_network = strcmp(network->ssid, s_ui.setup_edit_config.wifi_ssid) == 0;
  const char *saved_password = selected_existing_network ? s_ui.setup_edit_config.wifi_password : "";

  show_setup_credentials_panel(
    network->ssid,
    saved_password,
    true,
    network->requires_password,
    network->requires_password
      ? "Network selected. Enter the password, then save to restart."
      : "Open network selected. The password can stay blank."
  );
}

static void show_setup_location_editor(bool show_keyboard)
{
  show_setup_location_panel(show_keyboard, default_location_prompt_text());
}

static void populate_setup_network_list(void)
{
  if (s_ui.setup_network_list == NULL) {
    return;
  }

  lv_obj_clean(s_ui.setup_network_list);

  if (s_ui.setup_scan_in_progress) {
    lv_obj_t *label = create_label(
      s_ui.setup_network_list,
      &lv_font_montserrat_18,
      color_text_muted(),
      LV_TEXT_ALIGN_CENTER,
      "Scanning nearby networks..."
    );
    lv_obj_set_width(label, LV_PCT(100));
    return;
  }

  if (s_ui.scan_result_count == 0) {
    lv_obj_t *label = create_label(
      s_ui.setup_network_list,
      &lv_font_montserrat_18,
      color_text_muted(),
      LV_TEXT_ALIGN_CENTER,
      "No nearby networks found.\nRefresh the scan or use Manual Network."
    );
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    return;
  }

  for (size_t index = 0; index < s_ui.scan_result_count; ++index) {
    const connectivity_scan_result_t *network = &s_ui.scan_results[index];

    lv_obj_t *button = lv_button_create(s_ui.setup_network_list);
    lv_obj_set_width(button, LV_PCT(100));
    lv_obj_set_height(button, 58);
    style_panel(button, 18, LV_OPA_80);
    lv_obj_set_style_pad_left(button, 16, 0);
    lv_obj_set_style_pad_right(button, 16, 0);
    lv_obj_set_style_pad_top(button, 0, 0);
    lv_obj_set_style_pad_bottom(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_layout(button, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(button, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(button, setup_network_select_event_cb, LV_EVENT_CLICKED, (void *) (uintptr_t) index);

    lv_obj_t *ssid_label = create_label(
      button,
      &lv_font_montserrat_18,
      color_text_primary(),
      LV_TEXT_ALIGN_LEFT,
      network->ssid
    );
    lv_obj_set_flex_grow(ssid_label, 1);
    lv_obj_set_width(ssid_label, LV_PCT(70));
    lv_label_set_long_mode(ssid_label, LV_LABEL_LONG_DOT);

    char meta_text[32];
    snprintf(
      meta_text,
      sizeof(meta_text),
      "%s %ddBm",
      network->requires_password ? "Secured" : "Open",
      network->rssi
    );
    create_label(button, LV_FONT_DEFAULT, color_text_muted(), LV_TEXT_ALIGN_RIGHT, meta_text);
  }
}

static void setup_scan_task(void *arg)
{
  (void) arg;

  connectivity_scan_result_t results[SETUP_SCAN_MAX_RESULTS] = {0};
  size_t result_count = 0;
  esp_err_t err = connectivity_scan_networks(results, SETUP_SCAN_MAX_RESULTS, &result_count);

  if (bsp_display_lock(UI_ASYNC_LOCK_TIMEOUT_MS) == ESP_OK) {
    s_ui.setup_scan_in_progress = false;

    if (err == ESP_OK) {
      memset(s_ui.scan_results, 0, sizeof(s_ui.scan_results));
      if (result_count > 0) {
        memcpy(s_ui.scan_results, results, result_count * sizeof(results[0]));
      }
      s_ui.scan_result_count = result_count;
      populate_setup_network_list();

      if (result_count == 0) {
        update_setup_scan_status("No nearby networks found yet.", color_text_muted());
      } else {
        update_setup_scan_status("Choose your Wi-Fi network, or use Manual Network.", color_text_muted());
      }
    } else {
      char error_text[96];
      s_ui.scan_result_count = 0;
      populate_setup_network_list();
      snprintf(error_text, sizeof(error_text), "Wi-Fi scan failed: %s", esp_err_to_name(err));
      update_setup_scan_status(error_text, color_warning());
    }

    bsp_display_unlock();
  }

  vTaskDelete(NULL);
}

static void request_setup_scan(void)
{
  if (s_ui.setup_scan_in_progress) {
    return;
  }

  s_ui.setup_scan_in_progress = true;
  s_ui.scan_result_count = 0;
  memset(s_ui.scan_results, 0, sizeof(s_ui.scan_results));
  update_setup_scan_status("Scanning nearby networks...", color_text_muted());
  populate_setup_network_list();

  BaseType_t task_created = xTaskCreate(
    setup_scan_task,
    "wifi_scan",
    SETUP_SCAN_TASK_STACK_SIZE,
    NULL,
    SETUP_SCAN_TASK_PRIORITY,
    NULL
  );
  if (task_created != pdPASS) {
    s_ui.setup_scan_in_progress = false;
    update_setup_scan_status("Could not start the Wi-Fi scan task.", color_warning());
    populate_setup_network_list();
  }
}

static void setup_location_lookup_task(void *arg)
{
  setup_location_lookup_request_t *request = (setup_location_lookup_request_t *) arg;
  char query_copy[DEVICE_CONFIG_STR_LEN];
  device_config_t pending_config = {0};
  if (request != NULL) {
    snprintf(query_copy, sizeof(query_copy), "%s", request->query);
    pending_config = request->pending_config;
    free(request);
  } else {
    query_copy[0] = '\0';
  }

  location_lookup_result_t result = {0};
  esp_err_t lookup_err = location_lookup_resolve(query_copy, &result);

  if (bsp_display_lock(UI_ASYNC_LOCK_TIMEOUT_MS) == ESP_OK) {
    s_ui.setup_location_lookup_in_progress = false;

    if (lookup_err == ESP_OK) {
      device_config_t updated = pending_config;
      snprintf(
        updated.location,
        sizeof(updated.location),
        "%s",
        result.location[0] != '\0' ? result.location : query_copy
      );
      snprintf(updated.timezone, sizeof(updated.timezone), "%s", result.timezone);
      snprintf(updated.latitude, sizeof(updated.latitude), "%s", result.latitude);
      snprintf(updated.longitude, sizeof(updated.longitude), "%s", result.longitude);
      updated.location_ready = true;

      esp_err_t save_err = device_config_save(&updated);
      if (save_err == ESP_OK) {
        s_ui.config_snapshot = updated;
        s_ui.setup_edit_config = updated;
        update_setup_location_status("Location saved. Restarting for local weather...", color_success());
        schedule_setup_restart();
      } else {
        char error_text[80];
        snprintf(error_text, sizeof(error_text), "Save failed: %s", esp_err_to_name(save_err));
        update_setup_location_status(error_text, color_warning());
      }
    } else if (lookup_err == ESP_ERR_NOT_FOUND) {
      update_setup_location_status("Location not found. Try City, State.", color_warning());
      show_setup_keyboard(s_ui.setup_location_textarea);
    } else {
      char error_text[96];
      snprintf(error_text, sizeof(error_text), "Lookup failed: %s", esp_err_to_name(lookup_err));
      update_setup_location_status(error_text, color_warning());
      show_setup_keyboard(s_ui.setup_location_textarea);
    }

    bsp_display_unlock();
  }

  vTaskDelete(NULL);
}

static void open_setup_overlay(void)
{
  if (s_ui.setup_overlay == NULL
      || s_ui.setup_ssid_textarea == NULL
      || s_ui.setup_password_textarea == NULL
      || s_ui.setup_location_textarea == NULL
      || s_ui.setup_room_name_textarea == NULL
      || s_ui.setup_device_id_textarea == NULL) {
    return;
  }

  s_ui.setup_edit_config = s_ui.config_snapshot;
  lv_textarea_set_text(s_ui.setup_ssid_textarea, s_ui.setup_edit_config.wifi_ssid);
  lv_textarea_set_text(s_ui.setup_password_textarea, s_ui.setup_edit_config.wifi_password);
  lv_textarea_set_text(s_ui.setup_location_textarea, s_ui.setup_edit_config.location);
  lv_textarea_set_text(s_ui.setup_room_name_textarea, s_ui.setup_edit_config.room_name);
  lv_textarea_set_text(s_ui.setup_device_id_textarea, s_ui.setup_edit_config.device_id);
  s_ui.setup_restart_pending = false;
  lv_obj_clear_flag(s_ui.setup_overlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(s_ui.setup_overlay);

  if (!s_ui.config_snapshot.wifi_ready) {
    update_setup_status("This first pass configures Wi-Fi only.", color_text_muted());
    update_setup_scan_status("Scanning nearby networks...", color_text_muted());
    show_setup_scan_panel();
    request_setup_scan();
    return;
  }

  show_setup_location_editor(!s_ui.config_snapshot.location_ready);
}

static void setup_cancel_event_cb(lv_event_t *event)
{
  (void) event;

  if (s_ui.setup_restart_pending || s_ui.setup_location_lookup_in_progress) {
    return;
  }

  close_setup_overlay();
}

static void setup_prompt_event_cb(lv_event_t *event)
{
  (void) event;
  open_setup_overlay();
}

static void setup_face_toggle_event_cb(lv_event_t *event)
{
  (void) event;

  snprintf(
    s_ui.setup_edit_config.default_clock_face,
    sizeof(s_ui.setup_edit_config.default_clock_face),
    "%s",
    strings_equal_ignore_case(s_ui.setup_edit_config.default_clock_face, "analog") ? "digital" : "analog"
  );
  refresh_setup_settings_buttons();
}

static void setup_time_format_toggle_event_cb(lv_event_t *event)
{
  (void) event;

  snprintf(
    s_ui.setup_edit_config.time_format,
    sizeof(s_ui.setup_edit_config.time_format),
    "%s",
    strings_equal_ignore_case(s_ui.setup_edit_config.time_format, "24") ? "12" : "24"
  );
  refresh_setup_settings_buttons();
}

static void setup_leading_zero_toggle_event_cb(lv_event_t *event)
{
  (void) event;

  s_ui.setup_edit_config.leading_zero_12h = !s_ui.setup_edit_config.leading_zero_12h;
  refresh_setup_settings_buttons();
}

static void setup_units_toggle_event_cb(lv_event_t *event)
{
  (void) event;

  snprintf(
    s_ui.setup_edit_config.units,
    sizeof(s_ui.setup_edit_config.units),
    "%s",
    strings_equal_ignore_case(s_ui.setup_edit_config.units, "metric") ? "imperial" : "metric"
  );
  refresh_setup_settings_buttons();
}

static void setup_night_shift_toggle_event_cb(lv_event_t *event)
{
  (void) event;

  s_ui.setup_edit_config.night_shift_enabled = !s_ui.setup_edit_config.night_shift_enabled;
  refresh_setup_settings_buttons();
}

static void setup_message_sharing_toggle_event_cb(lv_event_t *event)
{
  (void) event;

  snprintf(
    s_ui.setup_edit_config.message_sharing,
    sizeof(s_ui.setup_edit_config.message_sharing),
    "%s",
    strings_equal_ignore_case(s_ui.setup_edit_config.message_sharing, "shared") ? "single" : "shared"
  );
  refresh_setup_settings_buttons();
}

static void setup_manual_network_event_cb(lv_event_t *event)
{
  (void) event;

  show_setup_credentials_panel(
    s_ui.setup_edit_config.wifi_ssid,
    s_ui.setup_edit_config.wifi_password,
    false,
    true,
    "Enter the network name, then add the password and save to restart."
  );
}

static void setup_back_event_cb(lv_event_t *event)
{
  (void) event;
  show_setup_scan_panel();
}

static void setup_scan_nav_event_cb(lv_event_t *event)
{
  (void) event;

  if (s_ui.config_snapshot.wifi_ready) {
    show_setup_location_editor(false);
    return;
  }

  setup_cancel_event_cb(event);
}

static void setup_wifi_settings_event_cb(lv_event_t *event)
{
  (void) event;
  show_setup_scan_panel();
  request_setup_scan();
}

static void setup_refresh_scan_event_cb(lv_event_t *event)
{
  (void) event;
  request_setup_scan();
}

static void setup_save_event_cb(lv_event_t *event)
{
  (void) event;

  if (s_ui.setup_restart_pending || s_ui.setup_ssid_textarea == NULL || s_ui.setup_password_textarea == NULL) {
    return;
  }

  const char *ssid = lv_textarea_get_text(s_ui.setup_ssid_textarea);
  const char *password = lv_textarea_get_text(s_ui.setup_password_textarea);
  if (ssid == NULL || ssid[0] == '\0') {
    update_setup_status("Enter a Wi-Fi network name before saving.", color_warning());
    show_setup_keyboard(s_ui.setup_ssid_textarea);
    return;
  }

  device_config_t updated = s_ui.setup_edit_config;
  snprintf(updated.wifi_ssid, sizeof(updated.wifi_ssid), "%s", ssid);
  snprintf(updated.wifi_password, sizeof(updated.wifi_password), "%s", password != NULL ? password : "");

  esp_err_t err = device_config_save(&updated);
  if (err != ESP_OK) {
    char error_text[80];
    snprintf(error_text, sizeof(error_text), "Save failed: %s", esp_err_to_name(err));
    update_setup_status(error_text, color_warning());
    return;
  }

  s_ui.config_snapshot = updated;
  s_ui.setup_edit_config = updated;
  update_setup_status("Saved. Restarting to join Wi-Fi...", color_success());
  schedule_setup_restart();
}

static void setup_location_save_event_cb(lv_event_t *event)
{
  (void) event;

  if (s_ui.setup_restart_pending
      || s_ui.setup_location_lookup_in_progress
      || s_ui.setup_location_textarea == NULL
      || s_ui.setup_room_name_textarea == NULL
      || s_ui.setup_device_id_textarea == NULL) {
    return;
  }

  if (!connectivity_is_wifi_connected()) {
    const char *offline_query = lv_textarea_get_text(s_ui.setup_location_textarea);
    bool location_changed_while_offline = offline_query != NULL
      && offline_query[0] != '\0'
      && strcmp(offline_query, s_ui.config_snapshot.location) != 0;
    if (location_changed_while_offline || !s_ui.config_snapshot.location_ready) {
      update_setup_location_status("Wait for Wi-Fi to connect before saving a new location.", color_warning());
      return;
    }
  }

  const char *query = lv_textarea_get_text(s_ui.setup_location_textarea);
  const char *room_name = lv_textarea_get_text(s_ui.setup_room_name_textarea);
  const char *device_id = lv_textarea_get_text(s_ui.setup_device_id_textarea);
  device_config_t pending_config = s_ui.setup_edit_config;

  if (room_name == NULL || room_name[0] == '\0') {
    update_setup_location_status("Enter a room name before saving.", color_warning());
    show_setup_keyboard(s_ui.setup_room_name_textarea);
    return;
  }

  if (device_id == NULL || device_id[0] == '\0') {
    update_setup_location_status("Enter a device ID before saving.", color_warning());
    show_setup_keyboard(s_ui.setup_device_id_textarea);
    return;
  }

  if (strings_equal_ignore_case(pending_config.message_sharing, "shared")
      && strings_equal_ignore_case(device_id, "clock-esp32-p4")) {
    update_setup_location_status("Set a unique device ID before enabling shared mode.", color_warning());
    show_setup_keyboard(s_ui.setup_device_id_textarea);
    return;
  }

  snprintf(pending_config.room_name, sizeof(pending_config.room_name), "%s", room_name);
  snprintf(pending_config.device_id, sizeof(pending_config.device_id), "%s", device_id);

  if (query != NULL && query[0] != '\0') {
    snprintf(pending_config.location, sizeof(pending_config.location), "%s", query);
  }

  bool location_changed = query != NULL
    && query[0] != '\0'
    && (strcmp(query, s_ui.config_snapshot.location) != 0 || !s_ui.config_snapshot.location_ready);

  if (!location_changed) {
    if ((query == NULL || query[0] == '\0') && !s_ui.config_snapshot.location_ready) {
      update_setup_location_status("Enter a City, State before saving.", color_warning());
      show_setup_keyboard(s_ui.setup_location_textarea);
      return;
    }

    esp_err_t save_err = device_config_save(&pending_config);
    if (save_err != ESP_OK) {
      char error_text[80];
      snprintf(error_text, sizeof(error_text), "Save failed: %s", esp_err_to_name(save_err));
      update_setup_location_status(error_text, color_warning());
      return;
    }

    s_ui.config_snapshot = pending_config;
    s_ui.setup_edit_config = pending_config;
    update_setup_location_status("Settings saved. Restarting...", color_success());
    schedule_setup_restart();
    return;
  }

  if (query == NULL || query[0] == '\0') {
    update_setup_location_status("Enter a City, State before saving.", color_warning());
    show_setup_keyboard(s_ui.setup_location_textarea);
    return;
  }

  setup_location_lookup_request_t *request = calloc(1, sizeof(*request));
  if (request == NULL) {
    update_setup_location_status("Could not start the location lookup.", color_warning());
    return;
  }

  request->pending_config = pending_config;
  snprintf(request->query, sizeof(request->query), "%s", query);

  s_ui.setup_location_lookup_in_progress = true;
  hide_setup_keyboard();
  update_setup_location_status("Looking up your location...", color_text_muted());

  BaseType_t task_created = xTaskCreate(
    setup_location_lookup_task,
    "loc_lookup",
    SETUP_LOCATION_TASK_STACK_SIZE,
    request,
    SETUP_LOCATION_TASK_PRIORITY,
    NULL
  );
  if (task_created != pdPASS) {
    s_ui.setup_location_lookup_in_progress = false;
    free(request);
    update_setup_location_status("Could not start the location lookup.", color_warning());
  }
}

static lv_obj_t *create_setup_button(
  lv_obj_t *parent,
  const char *text,
  lv_color_t bg_color,
  lv_color_t border_color,
  lv_event_cb_t event_cb
)
{
  lv_obj_t *button = lv_button_create(parent);
  lv_obj_set_size(button, 192, 56);
  lv_obj_set_style_radius(button, 18, 0);
  lv_obj_set_style_bg_color(button, bg_color, 0);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(button, border_color, 0);
  lv_obj_set_style_border_width(button, 1, 0);
  lv_obj_set_style_shadow_width(button, 0, 0);
  lv_obj_add_event_cb(button, event_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *label = create_label(button, &lv_font_montserrat_18, color_text_primary(), LV_TEXT_ALIGN_CENTER, text);
  lv_obj_center(label);
  return button;
}

static void update_setup_prompt(const device_config_t *config)
{
  if (s_ui.setup_prompt_button == NULL || s_ui.setup_settings_button == NULL || config == NULL) {
    return;
  }

  if (!config->wifi_ready) {
    set_setup_button_text(s_ui.setup_prompt_button, "Set Up Wi-Fi");
    lv_obj_clear_flag(s_ui.setup_prompt_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ui.setup_settings_button, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  if (!config->location_ready) {
    set_setup_button_text(s_ui.setup_prompt_button, "Set Location");
    lv_obj_clear_flag(s_ui.setup_prompt_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ui.setup_settings_button, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  lv_obj_add_flag(s_ui.setup_prompt_button, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(s_ui.setup_settings_button, LV_OBJ_FLAG_HIDDEN);
}

static void format_high_low(char *out, size_t out_size, int high, int low)
{
  snprintf(out, out_size, "H:%d°  L:%d°", high, low);
}

static int month_index_from_name(const char *month)
{
  for (int i = 0; i < 12; ++i) {
    if (strncmp(month, MONTH_NAMES[i], 3) == 0) {
      return i;
    }
  }

  return 0;
}

static time_t parse_compile_time_fallback(const char *timezone)
{
  connectivity_apply_timezone(timezone);

  struct tm tm_value = {0};
  char month[4] = {0};
  int day = 1;
  int year = 2024;
  int hour = 12;
  int minute = 0;
  int second = 0;

  (void) sscanf(__DATE__, "%3s %d %d", month, &day, &year);
  (void) sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second);

  tm_value.tm_year = year - 1900;
  tm_value.tm_mon = month_index_from_name(month);
  tm_value.tm_mday = day;
  tm_value.tm_hour = hour;
  tm_value.tm_min = minute;
  tm_value.tm_sec = second;
  tm_value.tm_isdst = -1;

  return mktime(&tm_value);
}

static time_t resolve_display_time(const device_config_t *config, uint32_t uptime_seconds)
{
  time_t now = time(NULL);
  if (now >= VALID_CLOCK_EPOCH) {
    s_ui.clock_is_estimated = false;
    return now;
  }

  if (s_ui.fallback_epoch <= 0) {
    s_ui.fallback_epoch = parse_compile_time_fallback(config->timezone);
  }

  s_ui.clock_is_estimated = true;
  return s_ui.fallback_epoch + (time_t) uptime_seconds;
}

static void update_pending_weather_labels(bool location_ready)
{
  const char *summary = location_ready ? "Loading Weather" : "Set Location";
  const char *detail = location_ready ? "Please wait" : "Save City/State";
  const char *forecast_summary = location_ready ? "Updating..." : "Finish setup";
  const char *pending_temp = "--";
  const char *pending_range = location_ready ? "Please wait" : "Local weather off";
  const lv_image_dsc_t *fallback_icon = weather_icon_image_for_name(weather_icon_fallback_name());

  if (s_ui.analog_summary_label != NULL) {
    set_label_text(s_ui.analog_summary_label, summary);
  }
  set_label_text(s_ui.analog_temp_label, pending_temp);
  set_label_text(s_ui.analog_high_low_label, pending_range);
  if (s_ui.analog_weather_icon != NULL) {
    lv_image_set_src(s_ui.analog_weather_icon, fallback_icon);
  }

  set_label_text(s_ui.digital_temp_label, pending_temp);
  set_label_text(s_ui.digital_summary_label, summary);
  set_label_text(s_ui.digital_high_low_label, pending_range);
  if (s_ui.digital_current_icon != NULL) {
    lv_image_set_src(s_ui.digital_current_icon, fallback_icon);
  }

  for (int i = 0; i < DAY_COUNT; ++i) {
    char forecast_day[8];
    copy_uppercase_string(forecast_day, sizeof(forecast_day), s_ui.forecast_labels[i]);
    set_label_text(s_ui.digital_forecast_day_labels[i], s_ui.forecast_labels[i]);
    set_label_text(s_ui.digital_forecast_day_labels[i], forecast_day);
    set_label_text(s_ui.digital_forecast_temp_labels[i], pending_temp);
    if (s_ui.digital_forecast_icons[i] != NULL) {
      lv_image_set_src(s_ui.digital_forecast_icons[i], fallback_icon);
    }
  }

  set_label_text(s_ui.forecast_tomorrow_day_label, "Tomorrow");
  if (s_ui.forecast_tomorrow_summary_label != NULL) {
    set_label_text(s_ui.forecast_tomorrow_summary_label, forecast_summary);
  }
  set_label_text(s_ui.forecast_tomorrow_temps_label, pending_temp);
  if (s_ui.forecast_tomorrow_icon != NULL) {
    lv_image_set_src(s_ui.forecast_tomorrow_icon, fallback_icon);
  }

  for (int i = 0; i < FORECAST_ROW_COUNT; ++i) {
    int item_index = i + 1;
    set_label_text(s_ui.forecast_row_day_labels[i], s_ui.forecast_labels[item_index]);
    if (s_ui.forecast_row_summary_labels[i] != NULL) {
      set_label_text(s_ui.forecast_row_summary_labels[i], detail);
    }
    set_label_text(s_ui.forecast_row_temps_labels[i], pending_temp);
    if (s_ui.forecast_row_icons[i] != NULL) {
      lv_image_set_src(s_ui.forecast_row_icons[i], fallback_icon);
    }
  }
}

static void refresh_forecast_labels(time_t display_time)
{
  for (int i = 0; i < DAY_COUNT; ++i) {
    struct tm label_time;
    localtime_r(&display_time, &label_time);
    label_time.tm_mday += (i + 1);
    mktime(&label_time);

    snprintf(
      s_ui.forecast_labels[i],
      sizeof(s_ui.forecast_labels[i]),
      "%s",
      WEEKDAY_SHORT[label_time.tm_wday]
    );
  }
}

static void update_night_overlay_visibility(void)
{
  if (s_ui.night_overlay == NULL) {
    return;
  }

  bool show_overlay = s_ui.night_overlay_active && s_ui.current_view == APP_VIEW_MESSAGE;
  if (show_overlay) {
    lv_obj_clear_flag(s_ui.night_overlay, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(s_ui.night_overlay, LV_OBJ_FLAG_HIDDEN);
  }
}

static void apply_image_night_tint(lv_obj_t *image, bool active, lv_color_t tint, lv_opa_t opa)
{
  if (image == NULL) {
    return;
  }

  lv_obj_set_style_image_recolor(image, tint, 0);
  lv_obj_set_style_image_recolor_opa(image, active ? opa : LV_OPA_TRANSP, 0);
}

static void apply_analog_night_state(bool active)
{
  if (s_ui.analog_stage_image != NULL) {
    lv_image_set_src(s_ui.analog_stage_image, active ? &analog_stage_night : &analog_stage_day);
  }

  if (s_ui.analog_weather_icon != NULL) {
    lv_obj_set_style_opa(s_ui.analog_weather_icon, active ? LV_OPA_TRANSP : ANALOG_WEATHER_ICON_OPACITY, 0);
  }

  if (s_ui.analog_day_label != NULL) {
    lv_obj_set_style_text_color(s_ui.analog_day_label, active ? color_night_primary() : color_text_primary(), 0);
  }

  if (s_ui.analog_date_label != NULL) {
    lv_obj_set_style_text_color(s_ui.analog_date_label, active ? color_night_muted() : color_text_muted(), 0);
  }

  apply_analog_dial_palette(active);

  if (s_ui.analog_temp_container != NULL) {
    if (active) {
      lv_obj_add_flag(s_ui.analog_temp_container, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_clear_flag(s_ui.analog_temp_container, LV_OBJ_FLAG_HIDDEN);
    }
  }

  if (s_ui.analog_temp_label != NULL) {
    lv_obj_set_style_text_color(s_ui.analog_temp_label, active ? color_night_primary() : color_temp(), 0);
  }

  if (s_ui.analog_high_low_label != NULL) {
    lv_obj_set_style_text_color(s_ui.analog_high_low_label, active ? color_night_muted() : color_text_primary(), 0);
  }

  if (s_ui.analog_hour_asset != NULL) {
    apply_image_night_tint(s_ui.analog_hour_asset, active, lv_color_hex(0xecc0c0), LV_OPA_80);
  }

  if (s_ui.analog_minute_asset != NULL) {
    apply_image_night_tint(s_ui.analog_minute_asset, active, lv_color_hex(0xecc0c0), LV_OPA_80);
  }

  if (s_ui.second_hand != NULL) {
    apply_image_night_tint(s_ui.second_hand, active, lv_color_hex(0xbb8484), LV_OPA_60);
    lv_obj_set_style_opa(s_ui.second_hand, active ? LV_OPA_80 : LV_OPA_COVER, 0);
  }

  if (s_ui.analog_center_cap != NULL) {
    apply_image_night_tint(s_ui.analog_center_cap, active, lv_color_hex(0xd6aaaa), LV_OPA_30);
  }
}

static void apply_digital_night_state(bool active)
{
  if (s_ui.digital_stage_image != NULL) {
    lv_image_set_src(s_ui.digital_stage_image, active ? &digital_stage_night : &digital_stage_day);
  }

  if (s_ui.digital_day_label != NULL) {
    lv_obj_set_style_text_color(s_ui.digital_day_label, active ? color_night_primary() : color_digital_accent(), 0);
  }

  if (s_ui.digital_date_label != NULL) {
    lv_obj_set_style_text_color(s_ui.digital_date_label, active ? color_night_muted() : lv_color_hex(0xbce4ff), 0);
  }

  if (s_ui.digital_time_label != NULL) {
    lv_obj_set_style_text_color(s_ui.digital_time_label, active ? color_night_primary() : color_text_primary(), 0);
  }

  if (s_ui.digital_meridiem_label != NULL) {
    lv_obj_set_style_text_color(s_ui.digital_meridiem_label, active ? color_night_muted() : color_accent(), 0);
  }

  if (s_ui.digital_current_panel != NULL) {
    if (active) {
      lv_obj_add_flag(s_ui.digital_current_panel, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_clear_flag(s_ui.digital_current_panel, LV_OBJ_FLAG_HIDDEN);
    }
  }

  if (s_ui.digital_divider_top != NULL) {
    if (active) {
      lv_obj_add_flag(s_ui.digital_divider_top, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_clear_flag(s_ui.digital_divider_top, LV_OBJ_FLAG_HIDDEN);
    }
  }

  if (s_ui.digital_divider_bottom != NULL) {
    if (active) {
      lv_obj_add_flag(s_ui.digital_divider_bottom, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_clear_flag(s_ui.digital_divider_bottom, LV_OBJ_FLAG_HIDDEN);
    }
  }

  if (s_ui.digital_forecast_container != NULL) {
    if (active) {
      lv_obj_add_flag(s_ui.digital_forecast_container, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_clear_flag(s_ui.digital_forecast_container, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

static void apply_forecast_night_state(bool active)
{
  if (s_ui.forecast_stage_image != NULL) {
    lv_image_set_src(s_ui.forecast_stage_image, active ? &round_stage_night : &round_stage_day);
  }

  if (s_ui.forecast_tomorrow_frame != NULL) {
    lv_image_set_src(
      s_ui.forecast_tomorrow_frame,
      active ? &forecast_tomorrow_frame_night : &forecast_tomorrow_frame_day
    );
  }

  if (s_ui.forecast_tomorrow_day_label != NULL) {
    lv_obj_set_style_text_color(s_ui.forecast_tomorrow_day_label, active ? color_night_primary() : color_text_primary(), 0);
  }

  if (s_ui.forecast_tomorrow_temps_label != NULL) {
    lv_obj_set_style_text_color(s_ui.forecast_tomorrow_temps_label, active ? color_night_primary() : lv_color_hex(0xf1f7ff), 0);
  }

  if (s_ui.forecast_tomorrow_icon != NULL) {
    apply_image_night_tint(s_ui.forecast_tomorrow_icon, active, lv_color_hex(0xb06b6b), LV_OPA_40);
    lv_obj_set_style_opa(s_ui.forecast_tomorrow_icon, active ? LV_OPA_80 : LV_OPA_COVER, 0);
  }

  for (int i = 0; i < FORECAST_ROW_COUNT; ++i) {
    if (s_ui.forecast_row_day_labels[i] != NULL) {
      lv_obj_set_style_text_color(
        s_ui.forecast_row_day_labels[i],
        active ? color_night_primary() : lv_color_hex(0xf5f9ff),
        0
      );
    }

    if (s_ui.forecast_row_temps_labels[i] != NULL) {
      lv_obj_set_style_text_color(
        s_ui.forecast_row_temps_labels[i],
        active ? color_night_muted() : lv_color_hex(0xe8f0fb),
        0
      );
    }

    if (s_ui.forecast_row_icons[i] != NULL) {
      apply_image_night_tint(s_ui.forecast_row_icons[i], active, lv_color_hex(0xb06b6b), LV_OPA_40);
      lv_obj_set_style_opa(s_ui.forecast_row_icons[i], active ? LV_OPA_80 : LV_OPA_COVER, 0);
    }
  }
}

static void apply_edge_indicator_state(lv_obj_t *indicator, bool visible, bool important, bool night_active)
{
  if (indicator == NULL) {
    return;
  }

  stop_indicator_pulse(indicator);

  if (!visible) {
    lv_obj_add_flag(indicator, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  lv_obj_clear_flag(indicator, LV_OBJ_FLAG_HIDDEN);
  lv_image_set_src(indicator, &analog_edge_indicator);

  if (important) {
    apply_image_night_tint(
      indicator,
      night_active,
      lv_color_hex(0xd9a0a0),
      EDGE_INDICATOR_IMPORTANT_NIGHT_TINT_OPA
    );
    lv_obj_set_style_opa(indicator, EDGE_INDICATOR_PULSE_MIN_OPA, 0);
    start_indicator_pulse(indicator);
    return;
  }

  apply_image_night_tint(
    indicator,
    night_active,
    lv_color_hex(0xbf8888),
    EDGE_INDICATOR_UNREAD_NIGHT_TINT_OPA
  );
  lv_obj_set_style_opa(indicator, night_active ? LV_OPA_80 : LV_OPA_COVER, 0);
}

static void update_message_indicator_state(void)
{
  apply_edge_indicator_state(
    s_ui.analog_edge_indicator,
    s_ui.has_unread_messages,
    s_ui.has_important_messages,
    s_ui.night_overlay_active
  );
  apply_edge_indicator_state(
    s_ui.digital_edge_indicator,
    s_ui.has_unread_messages,
    s_ui.has_important_messages,
    s_ui.night_overlay_active
  );
}

static void apply_message_night_state(bool active)
{
  if (s_ui.message_stage_image != NULL) {
    lv_image_set_src(s_ui.message_stage_image, active ? &round_stage_night : &round_stage_day);
  }

  if (s_ui.message_title_label != NULL) {
    lv_obj_set_style_text_color(
      s_ui.message_title_label,
      active ? color_night_primary() : (s_ui.has_important_messages ? color_important() : color_text_primary()),
      0
    );
  }

  if (s_ui.message_text_label != NULL) {
    lv_obj_set_style_text_color(s_ui.message_text_label, active ? color_night_primary() : color_text_primary(), 0);
  }

  if (s_ui.message_meta_label != NULL) {
    lv_obj_set_style_text_color(
      s_ui.message_meta_label,
      active ? color_night_muted() : (s_ui.has_important_messages ? color_important() : color_text_muted()),
      0
    );
  }

  if (s_ui.message_dismiss_label != NULL) {
    lv_obj_set_style_text_color(s_ui.message_dismiss_label, active ? color_night_muted() : color_text_subtle(), 0);
  }

  if (s_ui.message_empty_label != NULL) {
    lv_obj_set_style_text_color(s_ui.message_empty_label, active ? color_night_muted() : color_text_muted(), 0);
  }
}

static void apply_status_night_state(bool active)
{
  if (s_ui.clock_status_label != NULL) {
    lv_obj_set_style_text_color(s_ui.clock_status_label, active ? color_night_muted() : color_text_muted(), 0);
  }

  if (s_ui.weather_status_label != NULL) {
    lv_obj_set_style_text_color(s_ui.weather_status_label, active ? color_night_muted() : color_text_muted(), 0);
  }
}

static void apply_view_night_state(bool active)
{
  apply_analog_night_state(active);
  apply_digital_night_state(active);
  apply_forecast_night_state(active);
  apply_message_night_state(active);
  apply_status_night_state(active);
  update_message_indicator_state();
  update_night_overlay_visibility();
}

static void set_view_mode(app_view_mode_t next_view)
{
  s_ui.current_view = next_view;
  if (next_view == APP_VIEW_ANALOG || next_view == APP_VIEW_DIGITAL) {
    s_ui.last_home_view = next_view;
  } else {
    reset_clock_health();
  }

  if (s_ui.analog_view != NULL) {
    if (next_view == APP_VIEW_ANALOG) {
      lv_obj_clear_flag(s_ui.analog_view, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(s_ui.analog_view, LV_OBJ_FLAG_HIDDEN);
    }
  }

  if (s_ui.digital_view != NULL) {
    if (next_view == APP_VIEW_DIGITAL) {
      lv_obj_clear_flag(s_ui.digital_view, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(s_ui.digital_view, LV_OBJ_FLAG_HIDDEN);
    }
  }

  if (s_ui.forecast_view != NULL) {
    if (next_view == APP_VIEW_FORECAST) {
      lv_obj_clear_flag(s_ui.forecast_view, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(s_ui.forecast_view, LV_OBJ_FLAG_HIDDEN);
    }
  }

  if (s_ui.message_view != NULL) {
    if (next_view == APP_VIEW_MESSAGE) {
      lv_obj_clear_flag(s_ui.message_view, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(s_ui.message_view, LV_OBJ_FLAG_HIDDEN);
    }
  }

  if (s_ui.status_stack != NULL) {
    if (next_view == APP_VIEW_MESSAGE) {
      lv_obj_add_flag(s_ui.status_stack, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_clear_flag(s_ui.status_stack, LV_OBJ_FLAG_HIDDEN);
    }
  }

  apply_view_night_state(s_ui.night_overlay_active);
}

static void handle_swipe(int delta_x, int delta_y)
{
  if (is_setup_overlay_visible()) {
    return;
  }

  int abs_x = LV_ABS(delta_x);
  int abs_y = LV_ABS(delta_y);

  if (abs_x >= SWIPE_THRESHOLD_PX && (abs_x * 10) > (abs_y * SWIPE_AXIS_RATIO_X10)) {
    if (s_ui.current_view == APP_VIEW_ANALOG) {
      if (delta_x < 0) {
        set_view_mode(APP_VIEW_FORECAST);
      } else {
        set_view_mode(APP_VIEW_MESSAGE);
      }
      return;
    }

    if (s_ui.current_view == APP_VIEW_DIGITAL && delta_x > 0) {
      set_view_mode(APP_VIEW_MESSAGE);
      return;
    }

    if (s_ui.current_view == APP_VIEW_FORECAST && delta_x > 0) {
      set_view_mode(s_ui.last_home_view);
      return;
    }

    if (s_ui.current_view == APP_VIEW_MESSAGE && delta_x < 0) {
      set_view_mode(s_ui.last_home_view);
    }
    return;
  }

  if (abs_y >= SWIPE_THRESHOLD_PX && (abs_y * 10) > (abs_x * SWIPE_AXIS_RATIO_X10)) {
    if (s_ui.current_view == APP_VIEW_ANALOG && delta_y > 0) {
      set_view_mode(APP_VIEW_DIGITAL);
      return;
    }

    if (s_ui.current_view == APP_VIEW_DIGITAL && delta_y < 0) {
      set_view_mode(APP_VIEW_ANALOG);
    }
  }
}

static void gesture_layer_event_cb(lv_event_t *event)
{
  lv_event_code_t code = lv_event_get_code(event);
  lv_indev_t *indev = lv_indev_active();
  if (indev == NULL) {
    return;
  }

  lv_point_t point;
  lv_indev_get_point(indev, &point);

  if (code == LV_EVENT_PRESSED) {
    s_ui.gesture_tracking = true;
    s_ui.gesture_start = point;
    return;
  }

  if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) && s_ui.gesture_tracking) {
    int delta_x = point.x - s_ui.gesture_start.x;
    int delta_y = point.y - s_ui.gesture_start.y;
    s_ui.gesture_tracking = false;

    if (!is_setup_overlay_visible()
        && is_top_edge_swipe_start(&s_ui.gesture_start)
        && delta_y >= SWIPE_THRESHOLD_PX
        && (LV_ABS(delta_y) * 10) > (LV_ABS(delta_x) * SWIPE_AXIS_RATIO_X10)) {
      open_setup_overlay();
      return;
    }
    if (s_ui.current_view == APP_VIEW_MESSAGE
        && LV_ABS(delta_x) < 12
        && LV_ABS(delta_y) < 12
        && s_ui.has_unread_messages) {
      if (message_service_acknowledge_active(s_ui.config_snapshot.device_id) == ESP_OK) {
        app_message_snapshot_t message_snapshot = {0};
        uint32_t unread_count = 0;
        bool has_important_message = false;
        if (message_service_get_snapshot(
              s_ui.config_snapshot.device_id,
              &message_snapshot,
              &unread_count,
              &has_important_message
            ) == ESP_OK) {
          refresh_message_ui_state(&message_snapshot, unread_count, has_important_message);
        }
        set_view_mode(s_ui.last_home_view);
        return;
      }
    }
    handle_swipe(delta_x, delta_y);
  }
}

static void build_analog_view(lv_obj_t *parent)
{
  s_ui.analog_view = lv_obj_create(parent);
  lv_obj_set_size(s_ui.analog_view, LV_PCT(100), LV_PCT(100));
  /* Opaque, not transparent: a solid, deliberate backing color behind the
   * stage image rather than relying on transparency down through the stack. */
  lv_obj_set_style_bg_color(s_ui.analog_view, color_bg(), 0);
  lv_obj_set_style_bg_opa(s_ui.analog_view, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(s_ui.analog_view, 0, 0);
  lv_obj_set_style_pad_all(s_ui.analog_view, 0, 0);
  lv_obj_set_scrollbar_mode(s_ui.analog_view, LV_SCROLLBAR_MODE_OFF);

  s_ui.analog_stage_image = lv_image_create(s_ui.analog_view);
  lv_image_set_src(s_ui.analog_stage_image, &analog_stage_day);
  lv_obj_align(s_ui.analog_stage_image, LV_ALIGN_CENTER, 0, 0);

  s_ui.analog_weather_layer = lv_obj_create(s_ui.analog_view);
  lv_obj_set_size(s_ui.analog_weather_layer, ANALOG_FACE_SIZE, ANALOG_FACE_SIZE);
  lv_obj_align(s_ui.analog_weather_layer, LV_ALIGN_CENTER, 0, 0);
  clear_container_chrome(s_ui.analog_weather_layer);
  lv_obj_add_flag(s_ui.analog_weather_layer, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

  s_ui.analog_weather_icon = lv_image_create(s_ui.analog_weather_layer);
  lv_image_set_src(s_ui.analog_weather_icon, weather_icon_image_for_snapshot(NULL));
  lv_image_set_scale(s_ui.analog_weather_icon, ANALOG_WEATHER_ICON_SCALE);
  lv_obj_set_style_opa(s_ui.analog_weather_icon, ANALOG_WEATHER_ICON_OPACITY, 0);
  lv_obj_align(s_ui.analog_weather_icon, LV_ALIGN_CENTER, 0, ANALOG_WEATHER_ICON_OFFSET_Y);

  s_ui.analog_face = lv_obj_create(s_ui.analog_view);
  lv_obj_set_size(s_ui.analog_face, ANALOG_FACE_SIZE, ANALOG_FACE_SIZE);
  lv_obj_align(s_ui.analog_face, LV_ALIGN_CENTER, 0, 0);
  clear_container_chrome(s_ui.analog_face);
  lv_obj_add_flag(s_ui.analog_face, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  build_analog_dial_objects(s_ui.analog_face);

  s_ui.analog_edge_indicator = lv_image_create(s_ui.analog_view);
  lv_image_set_src(s_ui.analog_edge_indicator, &analog_edge_indicator);
  lv_obj_align(s_ui.analog_edge_indicator, LV_ALIGN_TOP_LEFT, ANALOG_EDGE_INDICATOR_X, ANALOG_EDGE_INDICATOR_Y);
  lv_obj_add_flag(s_ui.analog_edge_indicator, LV_OBJ_FLAG_HIDDEN);

  s_ui.analog_day_label = create_label(
    s_ui.analog_face, &lv_font_montserrat_32, color_text_primary(), LV_TEXT_ALIGN_CENTER, "Thursday"
  );
  lv_obj_align(s_ui.analog_day_label, LV_ALIGN_TOP_MID, 0, ANALOG_DAY_TOP);
  lv_obj_set_style_text_opa(s_ui.analog_day_label, LV_OPA_70, 0);
  lv_obj_set_style_text_letter_space(s_ui.analog_day_label, 1, 0);

  s_ui.analog_date_label = create_label(
    s_ui.analog_face, &lv_font_montserrat_24, color_text_muted(), LV_TEXT_ALIGN_CENTER, "Aug 6"
  );
  lv_obj_align(s_ui.analog_date_label, LV_ALIGN_TOP_MID, 0, ANALOG_DATE_TOP);
  lv_obj_set_style_text_opa(s_ui.analog_date_label, LV_OPA_60, 0);

  s_ui.analog_temp_container = lv_obj_create(s_ui.analog_face);
  clear_container_chrome(s_ui.analog_temp_container);
  lv_obj_set_size(s_ui.analog_temp_container, ANALOG_TEMP_BLOCK_WIDTH, LV_SIZE_CONTENT);
  lv_obj_align(s_ui.analog_temp_container, LV_ALIGN_TOP_MID, 0, ANALOG_TEMP_BLOCK_TOP);
  lv_obj_add_flag(s_ui.analog_temp_container, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  /* OVERFLOW_VISIBLE only widens the clip region it hands to children by this
   * container's OWN ext_draw_size, which defaults to 0 without a transform
   * style of its own. Without this, the scaled temp label below never
   * actually gets clip room from its parent no matter what margin the label
   * requests for itself. */
  lv_obj_set_style_transform_width(s_ui.analog_temp_container, ANALOG_TEMP_LABEL_EXT_DRAW_MARGIN, 0);
  lv_obj_set_style_transform_height(s_ui.analog_temp_container, ANALOG_TEMP_LABEL_EXT_DRAW_MARGIN, 0);
  lv_obj_set_layout(s_ui.analog_temp_container, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(s_ui.analog_temp_container, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(
    s_ui.analog_temp_container,
    LV_FLEX_ALIGN_CENTER,
    LV_FLEX_ALIGN_CENTER,
    LV_FLEX_ALIGN_CENTER
  );
  lv_obj_set_style_pad_row(s_ui.analog_temp_container, 8, 0);

  s_ui.analog_temp_label = create_label(
    s_ui.analog_temp_container, &lv_font_temp_105, color_temp(), LV_TEXT_ALIGN_CENTER, "52°"
  );
  lv_obj_set_width(s_ui.analog_temp_label, LV_PCT(100));
  lv_obj_set_height(s_ui.analog_temp_label, ANALOG_TEMP_LABEL_HEIGHT);
  lv_obj_set_style_text_letter_space(s_ui.analog_temp_label, -2, 0);
  lv_obj_set_style_transform_width(s_ui.analog_temp_label, ANALOG_TEMP_LABEL_EXT_DRAW_MARGIN, 0);
  lv_obj_set_style_transform_height(s_ui.analog_temp_label, ANALOG_TEMP_LABEL_EXT_DRAW_MARGIN, 0);

  s_ui.analog_high_low_label = create_label(
    s_ui.analog_temp_container,
    &lv_font_montserrat_24,
    color_text_primary(),
    LV_TEXT_ALIGN_CENTER,
    "H:61°  L:48°"
  );
  lv_obj_set_width(s_ui.analog_high_low_label, LV_PCT(100));
  lv_obj_set_style_text_letter_space(s_ui.analog_high_low_label, 1, 0);

  s_ui.analog_hour_asset = lv_image_create(s_ui.analog_face);
  lv_image_set_src(s_ui.analog_hour_asset, &analog_hour_hand);
  lv_image_set_pivot(s_ui.analog_hour_asset, ANALOG_HOUR_PIVOT_X, ANALOG_HOUR_PIVOT_Y);
  lv_image_set_antialias(s_ui.analog_hour_asset, false);
  lv_obj_set_pos(s_ui.analog_hour_asset, ANALOG_HOUR_ASSET_X, ANALOG_HOUR_ASSET_Y);

  s_ui.analog_minute_asset = lv_image_create(s_ui.analog_face);
  lv_image_set_src(s_ui.analog_minute_asset, &analog_minute_hand);
  lv_image_set_pivot(s_ui.analog_minute_asset, ANALOG_MINUTE_PIVOT_X, ANALOG_MINUTE_PIVOT_Y);
  lv_image_set_antialias(s_ui.analog_minute_asset, false);
  lv_obj_set_pos(s_ui.analog_minute_asset, ANALOG_MINUTE_ASSET_X, ANALOG_MINUTE_ASSET_Y);

  s_ui.second_hand = lv_image_create(s_ui.analog_face);
  lv_image_set_src(s_ui.second_hand, &analog_second_hand);
  lv_image_set_pivot(s_ui.second_hand, ANALOG_SECOND_PIVOT_X, ANALOG_SECOND_PIVOT_Y);
  lv_image_set_antialias(s_ui.second_hand, false);
  lv_obj_set_pos(s_ui.second_hand, ANALOG_SECOND_ASSET_X, ANALOG_SECOND_ASSET_Y);

  s_ui.analog_center_cap = lv_image_create(s_ui.analog_face);
  lv_image_set_src(s_ui.analog_center_cap, &analog_center_cap);
  lv_obj_set_pos(s_ui.analog_center_cap, ANALOG_CENTER_CAP_X, ANALOG_CENTER_CAP_Y);

  log_analog_hand_state("hour", s_ui.analog_hour_asset, &analog_hour_hand, ANALOG_HOUR_PIVOT_X, ANALOG_HOUR_PIVOT_Y);
  log_analog_hand_state(
    "minute",
    s_ui.analog_minute_asset,
    &analog_minute_hand,
    ANALOG_MINUTE_PIVOT_X,
    ANALOG_MINUTE_PIVOT_Y
  );
  log_analog_hand_state(
    "second",
    s_ui.second_hand,
    &analog_second_hand,
    ANALOG_SECOND_PIVOT_X,
    ANALOG_SECOND_PIVOT_Y
  );
}

static void build_digital_view(lv_obj_t *parent)
{
  s_ui.digital_view = lv_obj_create(parent);
  lv_obj_set_size(s_ui.digital_view, LV_PCT(100), LV_PCT(100));
  /* See analog_view: opaque, deliberate backing behind the stage image. */
  lv_obj_set_style_bg_color(s_ui.digital_view, color_bg(), 0);
  lv_obj_set_style_bg_opa(s_ui.digital_view, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(s_ui.digital_view, 0, 0);
  lv_obj_set_style_pad_all(s_ui.digital_view, 0, 0);
  lv_obj_set_scrollbar_mode(s_ui.digital_view, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(s_ui.digital_view, LV_OBJ_FLAG_HIDDEN);

  s_ui.digital_stage_image = lv_image_create(s_ui.digital_view);
  lv_image_set_src(s_ui.digital_stage_image, &digital_stage_day);
  lv_obj_align(s_ui.digital_stage_image, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t *digital_face = lv_obj_create(s_ui.digital_view);
  lv_obj_set_size(digital_face, FACE_CONTENT_SIZE, FACE_CONTENT_SIZE);
  lv_obj_align(digital_face, LV_ALIGN_CENTER, 0, 0);
  clear_container_chrome(digital_face);
  lv_obj_add_flag(digital_face, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

  s_ui.digital_edge_indicator = lv_image_create(s_ui.digital_view);
  lv_image_set_src(s_ui.digital_edge_indicator, &analog_edge_indicator);
  lv_obj_align(s_ui.digital_edge_indicator, LV_ALIGN_TOP_LEFT, ANALOG_EDGE_INDICATOR_X, ANALOG_EDGE_INDICATOR_Y);
  lv_obj_add_flag(s_ui.digital_edge_indicator, LV_OBJ_FLAG_HIDDEN);

  s_ui.digital_day_label = create_label(
    digital_face, &lv_font_montserrat_32, color_digital_accent(), LV_TEXT_ALIGN_CENTER, "THURSDAY"
  );
  lv_obj_set_width(s_ui.digital_day_label, LV_PCT(100));
  lv_obj_align(s_ui.digital_day_label, LV_ALIGN_TOP_MID, 0, DIGITAL_DAY_TOP);
  lv_obj_set_style_text_letter_space(s_ui.digital_day_label, 1, 0);

  s_ui.digital_date_label = create_label(
    digital_face, &lv_font_montserrat_24, lv_color_hex(0xbce4ff), LV_TEXT_ALIGN_CENTER, "AUG 6"
  );
  lv_obj_set_width(s_ui.digital_date_label, LV_PCT(100));
  lv_obj_align(s_ui.digital_date_label, LV_ALIGN_TOP_MID, 0, DIGITAL_DATE_TOP);
  lv_obj_set_style_text_letter_space(s_ui.digital_date_label, 2, 0);

  /* Previously lv_font_montserrat_48 stretched 3x via transform_scale --
   * upscaling an already-rasterized bitmap glyph, which visibly blurred it.
   * Now uses lv_font_clock_time_144, generated at its true 144px render
   * size (targets/esp32-p4/scripts/generate-custom-fonts.sh), no scale
   * transform needed. layout_digital_time_group() reads the transform
   * scale style dynamically (lv_obj_get_style_transform_scale_x_safe), so
   * it adapts on its own now that none is set (default LV_SCALE_NONE). */
  s_ui.digital_time_label = create_label(
    digital_face, &lv_font_clock_time_144, color_text_primary(), LV_TEXT_ALIGN_CENTER, "10:42"
  );
  lv_obj_set_width(s_ui.digital_time_label, LV_SIZE_CONTENT);
  lv_obj_set_style_text_letter_space(s_ui.digital_time_label, -2, 0);

  s_ui.digital_meridiem_label = create_label(
    digital_face, &lv_font_montserrat_24, color_accent(), LV_TEXT_ALIGN_CENTER, "AM"
  );
  lv_obj_set_width(s_ui.digital_meridiem_label, LV_SIZE_CONTENT);
  lv_obj_set_style_text_letter_space(s_ui.digital_meridiem_label, 2, 0);
  layout_digital_time_group(true);

  s_ui.digital_divider_top = lv_obj_create(digital_face);
  clear_container_chrome(s_ui.digital_divider_top);
  lv_obj_set_size(s_ui.digital_divider_top, DIGITAL_DIVIDER_WIDTH, 1);
  lv_obj_align(s_ui.digital_divider_top, LV_ALIGN_TOP_MID, 0, DIGITAL_DIVIDER_TOP);
  lv_obj_set_style_bg_color(s_ui.digital_divider_top, lv_color_hex(0x82cbff), 0);
  lv_obj_set_style_bg_opa(s_ui.digital_divider_top, LV_OPA_50, 0);

  s_ui.digital_current_panel = lv_obj_create(digital_face);
  clear_container_chrome(s_ui.digital_current_panel);
  lv_obj_set_size(s_ui.digital_current_panel, DIGITAL_CURRENT_WIDTH, DIGITAL_CURRENT_HEIGHT);
  lv_obj_align(s_ui.digital_current_panel, LV_ALIGN_TOP_MID, 0, DIGITAL_CURRENT_TOP);
  lv_obj_add_flag(s_ui.digital_current_panel, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  /* current_copy is intentionally positioned to overhang this panel's right
   * edge by ~70px (mirrors the Pi reference's `.digital-current-copy` with
   * left:400px, width:250px inside a 600px box, which the browser never
   * clips). OVERFLOW_VISIBLE alone doesn't widen the clip region LVGL passes
   * to children -- only ext_draw_size does, which only grows when
   * transform_width/height is set (see lv_obj_calculate_ext_draw_size). */
  lv_obj_set_style_transform_width(s_ui.digital_current_panel, 100, 0);
  lv_obj_set_style_transform_height(s_ui.digital_current_panel, 40, 0);

  /* Previously lv_font_montserrat_48 stretched ~2.13x via transform_scale --
   * upscaling an already-rasterized bitmap glyph, which visibly blurred it.
   * Now uses lv_font_temp_102, generated at its true 102px render size
   * (targets/esp32-p4/scripts/generate-custom-fonts.sh), no scale transform
   * needed. */
  s_ui.digital_temp_label = create_label(
    s_ui.digital_current_panel, &lv_font_temp_102, color_text_primary(), LV_TEXT_ALIGN_LEFT, "52°"
  );
  lv_obj_set_width(s_ui.digital_temp_label, 200);
  lv_obj_align(s_ui.digital_temp_label, LV_ALIGN_LEFT_MID, DIGITAL_CURRENT_TEMP_LEFT, 0);
  lv_obj_set_style_text_letter_space(s_ui.digital_temp_label, -2, 0);

  s_ui.digital_current_icon = lv_image_create(s_ui.digital_current_panel);
  lv_image_set_src(s_ui.digital_current_icon, weather_icon_image_for_name(weather_icon_fallback_name()));
  lv_obj_set_size(s_ui.digital_current_icon, DIGITAL_CURRENT_ICON_SIZE, DIGITAL_CURRENT_ICON_SIZE);
  lv_image_set_inner_align(s_ui.digital_current_icon, LV_IMAGE_ALIGN_CONTAIN);
  lv_obj_set_style_opa(s_ui.digital_current_icon, LV_OPA_COVER, 0);
  lv_obj_align(s_ui.digital_current_icon, LV_ALIGN_CENTER, 0, 0);
  lv_obj_add_flag(s_ui.digital_current_icon, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

  lv_obj_t *current_copy = lv_obj_create(s_ui.digital_current_panel);
  clear_container_chrome(current_copy);
  lv_obj_set_size(current_copy, DIGITAL_CURRENT_COPY_WIDTH, LV_SIZE_CONTENT);
  lv_obj_align(current_copy, LV_ALIGN_LEFT_MID, DIGITAL_CURRENT_COPY_LEFT, 0);
  /* The summary label wraps to 2 lines when the condition text is long
   * ("Mostly Sunny" etc). Its LV_SIZE_CONTENT height is computed against
   * the initial placeholder text at build time; when a runtime weather
   * update swaps in a longer string that newly needs wrapping, this
   * container's height can stay sized for one line and clip the wrapped
   * second line — same clipping pattern as the temp label and forecast
   * rows elsewhere in this file. */
  lv_obj_add_flag(current_copy, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_set_style_transform_width(current_copy, 40, 0);
  lv_obj_set_style_transform_height(current_copy, 40, 0);
  lv_obj_set_layout(current_copy, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(current_copy, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(current_copy, 3, 0);

  s_ui.digital_summary_label = create_label(
    current_copy, &lv_font_montserrat_32, color_text_primary(), LV_TEXT_ALIGN_LEFT, "Partly Cloudy"
  );
  lv_obj_set_width(s_ui.digital_summary_label, LV_PCT(100));
  lv_obj_set_height(s_ui.digital_summary_label, LV_SIZE_CONTENT);
  lv_label_set_long_mode(s_ui.digital_summary_label, LV_LABEL_LONG_WRAP);
  s_ui.digital_high_low_label = create_label(
    current_copy, &lv_font_montserrat_24, color_digital_accent(), LV_TEXT_ALIGN_LEFT, "H:61°  L:48°"
  );
  lv_obj_set_width(s_ui.digital_high_low_label, LV_PCT(100));

  s_ui.digital_divider_bottom = lv_obj_create(digital_face);
  clear_container_chrome(s_ui.digital_divider_bottom);
  lv_obj_set_size(s_ui.digital_divider_bottom, DIGITAL_DIVIDER_WIDTH, 1);
  lv_obj_align(s_ui.digital_divider_bottom, LV_ALIGN_TOP_MID, 0, DIGITAL_DIVIDER_BOTTOM);
  lv_obj_set_style_bg_color(s_ui.digital_divider_bottom, lv_color_hex(0x82cbff), 0);
  lv_obj_set_style_bg_opa(s_ui.digital_divider_bottom, LV_OPA_50, 0);

  s_ui.digital_forecast_container = lv_obj_create(digital_face);
  clear_container_chrome(s_ui.digital_forecast_container);
  lv_obj_set_size(s_ui.digital_forecast_container, DIGITAL_FORECAST_WIDTH, 126);
  lv_obj_align(s_ui.digital_forecast_container, LV_ALIGN_TOP_MID, 0, DIGITAL_FORECAST_TOP);
  lv_obj_add_flag(s_ui.digital_forecast_container, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_set_layout(s_ui.digital_forecast_container, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(s_ui.digital_forecast_container, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(
    s_ui.digital_forecast_container,
    LV_FLEX_ALIGN_SPACE_BETWEEN,
    LV_FLEX_ALIGN_START,
    LV_FLEX_ALIGN_START
  );

  for (int i = 0; i < DAY_COUNT; ++i) {
    lv_obj_t *item = lv_obj_create(s_ui.digital_forecast_container);
    clear_container_chrome(item);
    lv_obj_set_size(item, 94, 120);
    lv_obj_set_layout(item, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(item, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(item, 0, 0);

    s_ui.digital_forecast_day_labels[i] = create_label(
      item, &lv_font_montserrat_24, color_digital_accent(), LV_TEXT_ALIGN_CENTER, "THU"
    );
    lv_obj_set_width(s_ui.digital_forecast_day_labels[i], LV_PCT(100));

    s_ui.digital_forecast_icons[i] = lv_image_create(item);
    lv_image_set_src(s_ui.digital_forecast_icons[i], weather_icon_image_for_name(weather_icon_fallback_name()));
    lv_obj_set_size(s_ui.digital_forecast_icons[i], DIGITAL_FORECAST_ICON_SIZE, DIGITAL_FORECAST_ICON_SIZE);
    lv_image_set_inner_align(s_ui.digital_forecast_icons[i], LV_IMAGE_ALIGN_CONTAIN);
    lv_obj_set_style_opa(s_ui.digital_forecast_icons[i], LV_OPA_COVER, 0);

    s_ui.digital_forecast_temp_labels[i] = create_label(
      item, &lv_font_montserrat_24, color_text_primary(), LV_TEXT_ALIGN_CENTER, "61°"
    );
    lv_obj_set_width(s_ui.digital_forecast_temp_labels[i], LV_PCT(100));
  }
}

static void build_forecast_view(lv_obj_t *parent)
{
  s_ui.forecast_view = lv_obj_create(parent);
  lv_obj_set_size(s_ui.forecast_view, LV_PCT(100), LV_PCT(100));
  /* See analog_view: opaque, deliberate backing behind the stage image. */
  lv_obj_set_style_bg_color(s_ui.forecast_view, color_bg(), 0);
  lv_obj_set_style_bg_opa(s_ui.forecast_view, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(s_ui.forecast_view, 0, 0);
  lv_obj_set_style_pad_all(s_ui.forecast_view, 0, 0);
  lv_obj_set_scrollbar_mode(s_ui.forecast_view, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(s_ui.forecast_view, LV_OBJ_FLAG_HIDDEN);

  s_ui.forecast_stage_image = lv_image_create(s_ui.forecast_view);
  lv_image_set_src(s_ui.forecast_stage_image, &round_stage_day);
  lv_obj_align(s_ui.forecast_stage_image, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t *forecast_face = lv_obj_create(s_ui.forecast_view);
  lv_obj_set_size(forecast_face, FACE_CONTENT_SIZE, FACE_CONTENT_SIZE);
  lv_obj_align(forecast_face, LV_ALIGN_CENTER, 0, 0);
  clear_container_chrome(forecast_face);

  s_ui.forecast_tomorrow_frame = lv_image_create(forecast_face);
  lv_image_set_src(s_ui.forecast_tomorrow_frame, &forecast_tomorrow_frame_day);
  lv_obj_align(s_ui.forecast_tomorrow_frame, LV_ALIGN_TOP_MID, 0, FORECAST_TOMORROW_FRAME_TOP);

  s_ui.forecast_tomorrow_day_label = create_label(
    forecast_face, &lv_font_montserrat_24, color_text_primary(), LV_TEXT_ALIGN_CENTER, "Tomorrow"
  );
  lv_obj_set_width(s_ui.forecast_tomorrow_day_label, LV_PCT(100));
  lv_obj_align(s_ui.forecast_tomorrow_day_label, LV_ALIGN_TOP_MID, 0, FORECAST_TOMORROW_LABEL_TOP);
  s_ui.forecast_tomorrow_summary_label = NULL;

  s_ui.forecast_tomorrow_icon = lv_image_create(forecast_face);
  lv_image_set_src(s_ui.forecast_tomorrow_icon, weather_icon_image_for_name(weather_icon_fallback_name()));
  lv_obj_set_size(s_ui.forecast_tomorrow_icon, FORECAST_TOMORROW_ICON_SIZE, FORECAST_TOMORROW_ICON_SIZE);
  lv_image_set_inner_align(s_ui.forecast_tomorrow_icon, LV_IMAGE_ALIGN_CONTAIN);
  lv_obj_set_style_opa(s_ui.forecast_tomorrow_icon, LV_OPA_COVER, 0);
  lv_obj_align(s_ui.forecast_tomorrow_icon, LV_ALIGN_TOP_MID, 0, FORECAST_TOMORROW_ICON_TOP);

  s_ui.forecast_tomorrow_temps_label = create_label(
    forecast_face, &lv_font_montserrat_48, lv_color_hex(0xf1f7ff), LV_TEXT_ALIGN_CENTER, "61° / 48°"
  );
  lv_obj_set_width(s_ui.forecast_tomorrow_temps_label, LV_PCT(100));
  lv_obj_align(s_ui.forecast_tomorrow_temps_label, LV_ALIGN_TOP_MID, 0, FORECAST_TOMORROW_TEMPS_TOP);

  static const int ROW_X[FORECAST_ROW_COUNT] = {
    FORECAST_ROW1_X, FORECAST_ROW2_X, FORECAST_ROW3_X, FORECAST_ROW4_X
  };
  static const int ROW_Y[FORECAST_ROW_COUNT] = {
    FORECAST_ROW1_Y, FORECAST_ROW2_Y, FORECAST_ROW3_Y, FORECAST_ROW4_Y
  };
  for (int i = 0; i < FORECAST_ROW_COUNT; ++i) {
    lv_obj_t *row = lv_obj_create(forecast_face);
    clear_container_chrome(row);
    lv_obj_set_size(row, 104, 120);
    lv_obj_align(row, LV_ALIGN_TOP_LEFT, ROW_X[i] - 52, ROW_Y[i] - 50);
    /* Day label + icon + temp label stack to ~144px (27+90+27), taller than
     * this row's fixed 120px box, so the top/bottom labels were clipped
     * without this — same class of bug as the temp label and message bg.
     * OVERFLOW_VISIBLE alone is not enough: it only widens the clip area
     * passed to children by THIS object's own ext_draw_size, which is 0
     * without a transform style (see lv_refr.c lv_obj_redraw) — the same
     * gap that made the first temp-label fix a no-op. */
    lv_obj_add_flag(row, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_style_transform_width(row, 40, 0);
    lv_obj_set_style_transform_height(row, 40, 0);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(row, 0, 0);

    s_ui.forecast_row_day_labels[i] = create_label(
      row, &lv_font_montserrat_24, lv_color_hex(0xf5f9ff), LV_TEXT_ALIGN_CENTER, "Fri"
    );
    lv_obj_set_width(s_ui.forecast_row_day_labels[i], LV_PCT(100));
    s_ui.forecast_row_summary_labels[i] = NULL;

    s_ui.forecast_row_icons[i] = lv_image_create(row);
    lv_image_set_src(s_ui.forecast_row_icons[i], weather_icon_image_for_name(weather_icon_fallback_name()));
    lv_obj_set_size(s_ui.forecast_row_icons[i], FORECAST_ROW_ICON_SIZE, FORECAST_ROW_ICON_SIZE);
    lv_image_set_inner_align(s_ui.forecast_row_icons[i], LV_IMAGE_ALIGN_CONTAIN);
    lv_obj_set_style_opa(s_ui.forecast_row_icons[i], LV_OPA_COVER, 0);

    s_ui.forecast_row_temps_labels[i] = create_label(
      row, &lv_font_montserrat_24, lv_color_hex(0xe8f0fb), LV_TEXT_ALIGN_CENTER, "64° / 50°"
    );
    lv_obj_set_width(s_ui.forecast_row_temps_labels[i], LV_PCT(100));
  }
}

static void build_message_view(lv_obj_t *parent)
{
  s_ui.message_view = lv_obj_create(parent);
  lv_obj_set_size(s_ui.message_view, LV_PCT(100), LV_PCT(100));
  /* See analog_view: opaque, deliberate backing behind the stage image. */
  lv_obj_set_style_bg_color(s_ui.message_view, color_bg(), 0);
  lv_obj_set_style_bg_opa(s_ui.message_view, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(s_ui.message_view, 0, 0);
  lv_obj_set_style_pad_all(s_ui.message_view, 0, 0);
  lv_obj_set_scrollbar_mode(s_ui.message_view, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(s_ui.message_view, LV_OBJ_FLAG_HIDDEN);

  s_ui.message_stage_image = lv_image_create(s_ui.message_view);
  lv_image_set_src(s_ui.message_stage_image, &round_stage_day);
  lv_obj_align(s_ui.message_stage_image, LV_ALIGN_CENTER, 0, 0);

  s_ui.message_face = lv_obj_create(s_ui.message_view);
  lv_obj_set_size(s_ui.message_face, 760, 760);
  lv_obj_align(s_ui.message_face, LV_ALIGN_CENTER, 0, 0);
  clear_container_chrome(s_ui.message_face);

  s_ui.message_title_label = create_label(
    s_ui.message_face, &lv_font_montserrat_32, color_text_primary(), LV_TEXT_ALIGN_CENTER, "Message"
  );
  lv_obj_set_width(s_ui.message_title_label, LV_PCT(100));
  lv_obj_align(s_ui.message_title_label, LV_ALIGN_TOP_MID, 0, 138);

  s_ui.message_card = lv_obj_create(s_ui.message_face);
  clear_container_chrome(s_ui.message_card);
  lv_obj_set_size(s_ui.message_card, 430, LV_SIZE_CONTENT);
  lv_obj_align(s_ui.message_card, LV_ALIGN_TOP_MID, 0, 232);
  lv_obj_set_style_pad_row(s_ui.message_card, 26, 0);
  lv_obj_set_layout(s_ui.message_card, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(s_ui.message_card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(
    s_ui.message_card,
    LV_FLEX_ALIGN_CENTER,
    LV_FLEX_ALIGN_CENTER,
    LV_FLEX_ALIGN_CENTER
  );
  lv_obj_add_flag(s_ui.message_card, LV_OBJ_FLAG_HIDDEN);

  s_ui.message_text_label = create_label(
    s_ui.message_card, &lv_font_montserrat_48, color_text_primary(), LV_TEXT_ALIGN_CENTER, "Dinner is ready."
  );
  lv_obj_set_width(s_ui.message_text_label, LV_PCT(100));
  lv_label_set_long_mode(s_ui.message_text_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_letter_space(s_ui.message_text_label, -1, 0);

  s_ui.message_meta_label = create_label(
    s_ui.message_card, &lv_font_montserrat_24, color_text_muted(), LV_TEXT_ALIGN_CENTER, "- Alex, 5:30 PM"
  );
  lv_obj_set_width(s_ui.message_meta_label, LV_PCT(100));

  s_ui.message_dismiss_label = create_label(
    s_ui.message_card, &lv_font_montserrat_18, color_text_subtle(), LV_TEXT_ALIGN_CENTER, "Tap to dismiss"
  );
  lv_obj_set_width(s_ui.message_dismiss_label, LV_PCT(100));
  lv_obj_set_style_text_letter_space(s_ui.message_dismiss_label, 1, 0);

  s_ui.message_empty_label = create_label(
    s_ui.message_face, &lv_font_montserrat_48, color_text_muted(), LV_TEXT_ALIGN_CENTER, "No messages"
  );
  lv_obj_set_width(s_ui.message_empty_label, 420);
  lv_obj_align(s_ui.message_empty_label, LV_ALIGN_TOP_MID, 0, 318);
}

static void build_status_stack(lv_obj_t *parent)
{
  s_ui.status_stack = lv_obj_create(parent);
  lv_obj_set_width(s_ui.status_stack, 360);
  lv_obj_set_height(s_ui.status_stack, LV_SIZE_CONTENT);
  lv_obj_align(s_ui.status_stack, LV_ALIGN_BOTTOM_MID, 0, -56);
  lv_obj_set_style_bg_opa(s_ui.status_stack, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_ui.status_stack, 0, 0);
  lv_obj_set_style_pad_all(s_ui.status_stack, 0, 0);
  lv_obj_set_style_pad_row(s_ui.status_stack, 6, 0);
  lv_obj_set_layout(s_ui.status_stack, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(s_ui.status_stack, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scrollbar_mode(s_ui.status_stack, LV_SCROLLBAR_MODE_OFF);

  s_ui.clock_status_label = create_label(
    s_ui.status_stack, &lv_font_montserrat_18, color_text_muted(), LV_TEXT_ALIGN_CENTER, ""
  );
  lv_obj_set_width(s_ui.clock_status_label, LV_PCT(100));
  lv_obj_set_style_text_opa(s_ui.clock_status_label, LV_OPA_70, 0);
  lv_obj_set_style_text_letter_space(s_ui.clock_status_label, 1, 0);
  lv_obj_add_flag(s_ui.clock_status_label, LV_OBJ_FLAG_HIDDEN);

  s_ui.weather_status_label = create_label(
    s_ui.status_stack, &lv_font_montserrat_18, color_text_muted(), LV_TEXT_ALIGN_CENTER, ""
  );
  lv_obj_set_width(s_ui.weather_status_label, LV_PCT(100));
  lv_obj_set_style_text_opa(s_ui.weather_status_label, LV_OPA_70, 0);
  lv_obj_set_style_text_letter_space(s_ui.weather_status_label, 1, 0);
  lv_obj_add_flag(s_ui.weather_status_label, LV_OBJ_FLAG_HIDDEN);

  s_ui.setup_prompt_button = create_setup_button(
    s_ui.status_stack,
    "Set Up Wi-Fi",
    lv_color_hex(0x245289),
    color_accent(),
    setup_prompt_event_cb
  );
  lv_obj_set_width(s_ui.setup_prompt_button, 260);
  lv_obj_add_flag(s_ui.setup_prompt_button, LV_OBJ_FLAG_HIDDEN);

  s_ui.setup_settings_button = create_setup_button(
    s_ui.status_stack,
    "Settings",
    lv_color_hex(0x17263b),
    color_panel_border(),
    setup_prompt_event_cb
  );
  lv_obj_set_width(s_ui.setup_settings_button, 200);
  lv_obj_add_flag(s_ui.setup_settings_button, LV_OBJ_FLAG_HIDDEN);
}

static void build_gesture_layer(lv_obj_t *parent)
{
  s_ui.gesture_layer = lv_obj_create(parent);
  lv_obj_set_size(s_ui.gesture_layer, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_opa(s_ui.gesture_layer, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_ui.gesture_layer, 0, 0);
  lv_obj_set_style_pad_all(s_ui.gesture_layer, 0, 0);
  lv_obj_add_flag(s_ui.gesture_layer, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(s_ui.gesture_layer, gesture_layer_event_cb, LV_EVENT_PRESSED, NULL);
  lv_obj_add_event_cb(s_ui.gesture_layer, gesture_layer_event_cb, LV_EVENT_RELEASED, NULL);
  lv_obj_add_event_cb(s_ui.gesture_layer, gesture_layer_event_cb, LV_EVENT_PRESS_LOST, NULL);
}

static void build_night_overlay(lv_obj_t *parent)
{
  s_ui.night_overlay = lv_obj_create(parent);
  lv_obj_set_size(s_ui.night_overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(s_ui.night_overlay, lv_color_hex(0x6b0909), 0);
  lv_obj_set_style_bg_opa(s_ui.night_overlay, LV_OPA_30, 0);
  lv_obj_set_style_border_width(s_ui.night_overlay, 0, 0);
  lv_obj_add_flag(s_ui.night_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void build_setup_overlay(lv_obj_t *parent)
{
  s_ui.setup_overlay = lv_obj_create(parent);
  lv_obj_set_size(s_ui.setup_overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(s_ui.setup_overlay, lv_color_hex(0x02060d), 0);
  lv_obj_set_style_bg_opa(s_ui.setup_overlay, LV_OPA_80, 0);
  lv_obj_set_style_border_width(s_ui.setup_overlay, 0, 0);
  lv_obj_set_style_pad_all(s_ui.setup_overlay, 0, 0);
  lv_obj_set_scrollbar_mode(s_ui.setup_overlay, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(s_ui.setup_overlay, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(s_ui.setup_overlay, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *title = create_label(
    s_ui.setup_overlay,
    &lv_font_montserrat_24,
    color_text_primary(),
    LV_TEXT_ALIGN_CENTER,
    "Device Setup"
  );
  lv_obj_set_width(title, LV_PCT(100));
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 28);

  lv_obj_t *body = create_label(
    s_ui.setup_overlay,
    LV_FONT_DEFAULT,
    color_text_muted(),
    LV_TEXT_ALIGN_CENTER,
    "Connect Wi-Fi, then choose the weather location you want this display to use."
  );
  lv_obj_set_width(body, LV_PCT(100));
  lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
  lv_obj_align_to(body, title, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);

  s_ui.setup_scan_panel = lv_obj_create(s_ui.setup_overlay);
  lv_obj_set_size(s_ui.setup_scan_panel, 660, 460);
  lv_obj_align(s_ui.setup_scan_panel, LV_ALIGN_TOP_MID, 0, 100);
  style_panel(s_ui.setup_scan_panel, 32, LV_OPA_COVER);
  lv_obj_set_style_pad_all(s_ui.setup_scan_panel, 24, 0);
  lv_obj_set_style_pad_row(s_ui.setup_scan_panel, 14, 0);
  lv_obj_set_layout(s_ui.setup_scan_panel, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(s_ui.setup_scan_panel, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scrollbar_mode(s_ui.setup_scan_panel, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_shadow_width(s_ui.setup_scan_panel, 0, 0);

  lv_obj_t *scan_title = create_label(
    s_ui.setup_scan_panel,
    &lv_font_montserrat_18,
    color_text_primary(),
    LV_TEXT_ALIGN_CENTER,
    "Nearby Networks"
  );
  lv_obj_set_width(scan_title, LV_PCT(100));

  s_ui.setup_scan_status_label = create_label(
    s_ui.setup_scan_panel,
    LV_FONT_DEFAULT,
    color_text_muted(),
    LV_TEXT_ALIGN_CENTER,
    "Scanning nearby networks..."
  );
  lv_obj_set_width(s_ui.setup_scan_status_label, LV_PCT(100));
  lv_label_set_long_mode(s_ui.setup_scan_status_label, LV_LABEL_LONG_WRAP);

  s_ui.setup_network_list = lv_obj_create(s_ui.setup_scan_panel);
  lv_obj_set_width(s_ui.setup_network_list, LV_PCT(100));
  lv_obj_set_height(s_ui.setup_network_list, SETUP_NETWORK_LIST_HEIGHT);
  style_panel(s_ui.setup_network_list, 24, LV_OPA_70);
  lv_obj_set_style_pad_all(s_ui.setup_network_list, 12, 0);
  lv_obj_set_style_pad_row(s_ui.setup_network_list, 10, 0);
  lv_obj_set_layout(s_ui.setup_network_list, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(s_ui.setup_network_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scrollbar_mode(s_ui.setup_network_list, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_style_shadow_width(s_ui.setup_network_list, 0, 0);

  lv_obj_t *scan_button_row = lv_obj_create(s_ui.setup_scan_panel);
  lv_obj_set_width(scan_button_row, LV_PCT(100));
  lv_obj_set_height(scan_button_row, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(scan_button_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(scan_button_row, 0, 0);
  lv_obj_set_style_pad_all(scan_button_row, 0, 0);
  lv_obj_set_style_pad_column(scan_button_row, 12, 0);
  lv_obj_set_layout(scan_button_row, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(scan_button_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(
    scan_button_row,
    LV_FLEX_ALIGN_SPACE_BETWEEN,
    LV_FLEX_ALIGN_CENTER,
    LV_FLEX_ALIGN_CENTER
  );

  create_setup_button(
    scan_button_row,
    "Refresh",
    lv_color_hex(0x17263b),
    color_panel_border(),
    setup_refresh_scan_event_cb
  );
  create_setup_button(
    scan_button_row,
    "Manual Network",
    lv_color_hex(0x17263b),
    color_panel_border(),
    setup_manual_network_event_cb
  );
  s_ui.setup_scan_nav_button = create_setup_button(
    scan_button_row,
    "Cancel",
    lv_color_hex(0x17263b),
    color_panel_border(),
    setup_scan_nav_event_cb
  );

  s_ui.setup_credentials_panel = lv_obj_create(s_ui.setup_overlay);
  lv_obj_set_size(s_ui.setup_credentials_panel, 620, LV_SIZE_CONTENT);
  lv_obj_align(s_ui.setup_credentials_panel, LV_ALIGN_TOP_MID, 0, 116);
  style_panel(s_ui.setup_credentials_panel, 32, LV_OPA_COVER);
  lv_obj_set_style_pad_all(s_ui.setup_credentials_panel, 24, 0);
  lv_obj_set_style_pad_row(s_ui.setup_credentials_panel, 14, 0);
  lv_obj_set_layout(s_ui.setup_credentials_panel, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(s_ui.setup_credentials_panel, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scrollbar_mode(s_ui.setup_credentials_panel, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_shadow_width(s_ui.setup_credentials_panel, 0, 0);
  lv_obj_add_flag(s_ui.setup_credentials_panel, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *credentials_title = create_label(
    s_ui.setup_credentials_panel,
    &lv_font_montserrat_18,
    color_text_primary(),
    LV_TEXT_ALIGN_CENTER,
    "Network Details"
  );
  lv_obj_set_width(credentials_title, LV_PCT(100));

  s_ui.setup_ssid_textarea = lv_textarea_create(s_ui.setup_credentials_panel);
  style_textarea(s_ui.setup_ssid_textarea);
  lv_textarea_set_one_line(s_ui.setup_ssid_textarea, true);
  lv_textarea_set_max_length(s_ui.setup_ssid_textarea, DEVICE_CONFIG_WIFI_LEN - 1);
  lv_textarea_set_placeholder_text(s_ui.setup_ssid_textarea, "Wi-Fi network name");
  lv_obj_add_event_cb(s_ui.setup_ssid_textarea, setup_textarea_focus_cb, LV_EVENT_FOCUSED, NULL);
  lv_obj_add_event_cb(s_ui.setup_ssid_textarea, setup_textarea_focus_cb, LV_EVENT_CLICKED, NULL);

  s_ui.setup_password_textarea = lv_textarea_create(s_ui.setup_credentials_panel);
  style_textarea(s_ui.setup_password_textarea);
  lv_textarea_set_one_line(s_ui.setup_password_textarea, true);
  lv_textarea_set_max_length(s_ui.setup_password_textarea, DEVICE_CONFIG_WIFI_PASS_LEN - 1);
  lv_textarea_set_password_mode(s_ui.setup_password_textarea, true);
  lv_textarea_set_placeholder_text(s_ui.setup_password_textarea, "Wi-Fi password");
  lv_obj_add_event_cb(s_ui.setup_password_textarea, setup_textarea_focus_cb, LV_EVENT_FOCUSED, NULL);
  lv_obj_add_event_cb(s_ui.setup_password_textarea, setup_textarea_focus_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *credentials_button_row = lv_obj_create(s_ui.setup_credentials_panel);
  lv_obj_set_width(credentials_button_row, LV_PCT(100));
  lv_obj_set_height(credentials_button_row, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(credentials_button_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(credentials_button_row, 0, 0);
  lv_obj_set_style_pad_all(credentials_button_row, 0, 0);
  lv_obj_set_style_pad_column(credentials_button_row, 14, 0);
  lv_obj_set_layout(credentials_button_row, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(credentials_button_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(
    credentials_button_row,
    LV_FLEX_ALIGN_SPACE_BETWEEN,
    LV_FLEX_ALIGN_CENTER,
    LV_FLEX_ALIGN_CENTER
  );

  create_setup_button(
    credentials_button_row,
    "Back",
    lv_color_hex(0x17263b),
    color_panel_border(),
    setup_back_event_cb
  );
  create_setup_button(
    credentials_button_row,
    "Save & Restart",
    lv_color_hex(0x245289),
    color_accent(),
    setup_save_event_cb
  );

  s_ui.setup_status_label = create_label(
    s_ui.setup_credentials_panel,
    LV_FONT_DEFAULT,
    color_text_muted(),
    LV_TEXT_ALIGN_CENTER,
    "This first pass configures Wi-Fi only."
  );
  lv_obj_set_width(s_ui.setup_status_label, LV_PCT(100));
  lv_label_set_long_mode(s_ui.setup_status_label, LV_LABEL_LONG_WRAP);

  s_ui.setup_location_panel = lv_obj_create(s_ui.setup_overlay);
  lv_obj_set_size(s_ui.setup_location_panel, 620, 500);
  lv_obj_align(s_ui.setup_location_panel, LV_ALIGN_TOP_MID, 0, 116);
  style_panel(s_ui.setup_location_panel, 32, LV_OPA_COVER);
  lv_obj_set_style_pad_all(s_ui.setup_location_panel, 24, 0);
  lv_obj_set_style_pad_row(s_ui.setup_location_panel, 14, 0);
  lv_obj_set_layout(s_ui.setup_location_panel, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(s_ui.setup_location_panel, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scrollbar_mode(s_ui.setup_location_panel, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_style_shadow_width(s_ui.setup_location_panel, 0, 0);
  lv_obj_add_flag(s_ui.setup_location_panel, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *location_title = create_label(
    s_ui.setup_location_panel,
    &lv_font_montserrat_18,
    color_text_primary(),
    LV_TEXT_ALIGN_CENTER,
    "Weather Location"
  );
  lv_obj_set_width(location_title, LV_PCT(100));

  s_ui.setup_location_textarea = lv_textarea_create(s_ui.setup_location_panel);
  style_textarea(s_ui.setup_location_textarea);
  lv_textarea_set_one_line(s_ui.setup_location_textarea, true);
  lv_textarea_set_max_length(s_ui.setup_location_textarea, DEVICE_CONFIG_STR_LEN - 1);
  lv_textarea_set_placeholder_text(s_ui.setup_location_textarea, "City, State");
  lv_obj_add_event_cb(s_ui.setup_location_textarea, setup_textarea_focus_cb, LV_EVENT_FOCUSED, NULL);
  lv_obj_add_event_cb(s_ui.setup_location_textarea, setup_textarea_focus_cb, LV_EVENT_CLICKED, NULL);

  s_ui.setup_room_name_textarea = lv_textarea_create(s_ui.setup_location_panel);
  style_textarea(s_ui.setup_room_name_textarea);
  lv_textarea_set_one_line(s_ui.setup_room_name_textarea, true);
  lv_textarea_set_max_length(s_ui.setup_room_name_textarea, DEVICE_CONFIG_STR_LEN - 1);
  lv_textarea_set_placeholder_text(s_ui.setup_room_name_textarea, "Room name");
  lv_obj_add_event_cb(s_ui.setup_room_name_textarea, setup_textarea_focus_cb, LV_EVENT_FOCUSED, NULL);
  lv_obj_add_event_cb(s_ui.setup_room_name_textarea, setup_textarea_focus_cb, LV_EVENT_CLICKED, NULL);

  s_ui.setup_device_id_textarea = lv_textarea_create(s_ui.setup_location_panel);
  style_textarea(s_ui.setup_device_id_textarea);
  lv_textarea_set_one_line(s_ui.setup_device_id_textarea, true);
  lv_textarea_set_max_length(s_ui.setup_device_id_textarea, DEVICE_CONFIG_STR_LEN - 1);
  lv_textarea_set_placeholder_text(s_ui.setup_device_id_textarea, "Device ID");
  lv_obj_add_event_cb(s_ui.setup_device_id_textarea, setup_textarea_focus_cb, LV_EVENT_FOCUSED, NULL);
  lv_obj_add_event_cb(s_ui.setup_device_id_textarea, setup_textarea_focus_cb, LV_EVENT_CLICKED, NULL);

  s_ui.setup_message_sharing_button = create_setup_button(
    s_ui.setup_location_panel,
    "House Messages: Single",
    lv_color_hex(0x17263b),
    color_panel_border(),
    setup_message_sharing_toggle_event_cb
  );
  lv_obj_set_width(s_ui.setup_message_sharing_button, LV_PCT(100));

  s_ui.setup_face_button = create_setup_button(
    s_ui.setup_location_panel,
    "Home Screen: Digital",
    lv_color_hex(0x17263b),
    color_panel_border(),
    setup_face_toggle_event_cb
  );
  lv_obj_set_width(s_ui.setup_face_button, LV_PCT(100));

  s_ui.setup_time_format_button = create_setup_button(
    s_ui.setup_location_panel,
    "Time Format: 12-hour",
    lv_color_hex(0x17263b),
    color_panel_border(),
    setup_time_format_toggle_event_cb
  );
  lv_obj_set_width(s_ui.setup_time_format_button, LV_PCT(100));

  s_ui.setup_leading_zero_button = create_setup_button(
    s_ui.setup_location_panel,
    "Leading Zero: On",
    lv_color_hex(0x17263b),
    color_panel_border(),
    setup_leading_zero_toggle_event_cb
  );
  lv_obj_set_width(s_ui.setup_leading_zero_button, LV_PCT(100));

  s_ui.setup_units_button = create_setup_button(
    s_ui.setup_location_panel,
    "Units: Imperial",
    lv_color_hex(0x17263b),
    color_panel_border(),
    setup_units_toggle_event_cb
  );
  lv_obj_set_width(s_ui.setup_units_button, LV_PCT(100));

  s_ui.setup_night_shift_button = create_setup_button(
    s_ui.setup_location_panel,
    "Night Shift: Off",
    lv_color_hex(0x17263b),
    color_panel_border(),
    setup_night_shift_toggle_event_cb
  );
  lv_obj_set_width(s_ui.setup_night_shift_button, LV_PCT(100));

  lv_obj_t *location_button_row = lv_obj_create(s_ui.setup_location_panel);
  lv_obj_set_width(location_button_row, LV_PCT(100));
  lv_obj_set_height(location_button_row, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(location_button_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(location_button_row, 0, 0);
  lv_obj_set_style_pad_all(location_button_row, 0, 0);
  lv_obj_set_style_pad_column(location_button_row, 14, 0);
  lv_obj_set_layout(location_button_row, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(location_button_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(
    location_button_row,
    LV_FLEX_ALIGN_SPACE_BETWEEN,
    LV_FLEX_ALIGN_CENTER,
    LV_FLEX_ALIGN_CENTER
  );

  create_setup_button(
    location_button_row,
    "Wi-Fi",
    lv_color_hex(0x17263b),
    color_panel_border(),
    setup_wifi_settings_event_cb
  );
  create_setup_button(
    location_button_row,
    "Cancel",
    lv_color_hex(0x17263b),
    color_panel_border(),
    setup_cancel_event_cb
  );
  create_setup_button(
    location_button_row,
    "Save & Restart",
    lv_color_hex(0x245289),
    color_accent(),
    setup_location_save_event_cb
  );

  s_ui.setup_location_status_label = create_label(
    s_ui.setup_location_panel,
    LV_FONT_DEFAULT,
    color_text_muted(),
    LV_TEXT_ALIGN_CENTER,
    "Enter a city and state, like Loveland, Ohio. We'll look up timezone and coordinates automatically."
  );
  lv_obj_set_width(s_ui.setup_location_status_label, LV_PCT(100));
  lv_label_set_long_mode(s_ui.setup_location_status_label, LV_LABEL_LONG_WRAP);

  s_ui.setup_keyboard = lv_keyboard_create(s_ui.setup_overlay);
  lv_obj_set_size(s_ui.setup_keyboard, SETUP_KEYBOARD_WIDTH, SETUP_KEYBOARD_HEIGHT);
  lv_obj_align(s_ui.setup_keyboard, LV_ALIGN_BOTTOM_MID, 0, SETUP_KEYBOARD_BOTTOM_OFFSET);
  lv_keyboard_set_map(
    s_ui.setup_keyboard,
    LV_KEYBOARD_MODE_TEXT_LOWER,
    SETUP_KEYBOARD_LOWER_MAP,
    SETUP_KEYBOARD_LOWER_CTRL_MAP
  );
  lv_keyboard_set_map(
    s_ui.setup_keyboard,
    LV_KEYBOARD_MODE_TEXT_UPPER,
    SETUP_KEYBOARD_UPPER_MAP,
    SETUP_KEYBOARD_UPPER_CTRL_MAP
  );
  lv_keyboard_set_map(
    s_ui.setup_keyboard,
    LV_KEYBOARD_MODE_SPECIAL,
    SETUP_KEYBOARD_SPECIAL_MAP,
    SETUP_KEYBOARD_SPECIAL_CTRL_MAP
  );
  lv_keyboard_set_popovers(s_ui.setup_keyboard, true);
  lv_obj_set_style_radius(s_ui.setup_keyboard, 24, 0);
  lv_obj_set_style_border_color(s_ui.setup_keyboard, color_panel_border(), 0);
  lv_obj_set_style_border_width(s_ui.setup_keyboard, 1, 0);
  lv_obj_add_event_cb(s_ui.setup_keyboard, setup_keyboard_event_cb, LV_EVENT_READY, NULL);
  lv_obj_add_event_cb(s_ui.setup_keyboard, setup_keyboard_event_cb, LV_EVENT_CANCEL, NULL);
  lv_obj_add_flag(s_ui.setup_keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void update_analog_clock(const struct tm *local_time)
{
  static int s_logged_angle_updates = 0;

  if (local_time == NULL || s_ui.second_hand == NULL) {
    return;
  }

  double minute_angle = local_time->tm_min * 6.0;
  double hour_angle = ((local_time->tm_hour % 12) + (local_time->tm_min / 60.0)) * 30.0;
  double second_angle = local_time->tm_sec * 6.0;

  if (s_ui.analog_hour_asset != NULL) {
    lv_image_set_rotation(s_ui.analog_hour_asset, (int32_t) lround(hour_angle * 10.0));
  }

  if (s_ui.analog_minute_asset != NULL) {
    lv_image_set_rotation(s_ui.analog_minute_asset, (int32_t) lround(minute_angle * 10.0));
  }

  lv_image_set_rotation(s_ui.second_hand, (int32_t) lround(second_angle * 10.0));

  if (s_logged_angle_updates < 3) {
    ESP_LOGI(
      TAG,
      "Analog hand angles update %d: hour=%.1f minute=%.1f second=%.1f",
      s_logged_angle_updates + 1,
      hour_angle,
      minute_angle,
      second_angle
    );
    s_logged_angle_updates++;
  }
}

static void update_day_labels(const struct tm *local_time)
{
  if (local_time == NULL || local_time->tm_wday < 0 || local_time->tm_wday > 6
      || local_time->tm_mon < 0 || local_time->tm_mon > 11) {
    return;
  }

  char analog_date[16];
  char digital_date[16];
  char digital_date_upper[16];
  snprintf(analog_date, sizeof(analog_date), "%s %d", MONTH_NAMES[local_time->tm_mon], local_time->tm_mday);
  snprintf(
    digital_date,
    sizeof(digital_date),
    "%s %d",
    MONTH_NAMES[local_time->tm_mon],
    local_time->tm_mday
  );
  copy_uppercase_string(digital_date_upper, sizeof(digital_date_upper), digital_date);

  set_label_text(s_ui.analog_day_label, WEEKDAY_NAMES[local_time->tm_wday]);
  set_label_text(s_ui.analog_date_label, analog_date);
  set_label_text(s_ui.digital_day_label, WEEKDAY_NAMES_UPPER[local_time->tm_wday]);
  set_label_text(s_ui.digital_date_label, digital_date_upper);
}

static void update_digital_clock(const device_config_t *config, const struct tm *local_time)
{
  if (config == NULL || local_time == NULL) {
    return;
  }

  char time_text[16];
  int hour = local_time->tm_hour;
  bool show_meridiem = false;

  if (strings_equal_ignore_case(config->time_format, "24")) {
    snprintf(time_text, sizeof(time_text), "%02d:%02d", hour, local_time->tm_min);
    set_label_text(s_ui.digital_meridiem_label, "");
  } else {
    bool afternoon = hour >= 12;
    int display_hour = hour % 12;
    if (display_hour == 0) {
      display_hour = 12;
    }

    if (config->leading_zero_12h) {
      snprintf(time_text, sizeof(time_text), "%02d:%02d", display_hour, local_time->tm_min);
    } else {
      snprintf(time_text, sizeof(time_text), "%d:%02d", display_hour, local_time->tm_min);
    }

    set_label_text(s_ui.digital_meridiem_label, afternoon ? "PM" : "AM");
    show_meridiem = true;
  }

  set_label_text(s_ui.digital_time_label, time_text);
  layout_digital_time_group(show_meridiem);
}

static void update_weather_labels(void)
{
  if (!s_ui.weather.valid) {
    return;
  }

  char high_low[24];
  char temps[24];

  snprintf(temps, sizeof(temps), "%d°", s_ui.weather.temp);
  if (s_ui.analog_summary_label != NULL) {
    set_label_text(s_ui.analog_summary_label, s_ui.weather.summary);
  }
  set_label_text(s_ui.analog_temp_label, temps);
  format_high_low(high_low, sizeof(high_low), s_ui.weather.high, s_ui.weather.low);
  set_label_text(s_ui.analog_high_low_label, high_low);
  if (s_ui.analog_weather_icon != NULL) {
    lv_image_set_src(s_ui.analog_weather_icon, weather_icon_image_for_name(weather_icon_name_for_snapshot(&s_ui.weather)));
  }

  set_label_text(s_ui.digital_temp_label, temps);
  if (s_ui.digital_current_icon != NULL) {
    lv_image_set_src(s_ui.digital_current_icon, weather_icon_image_for_name(s_ui.weather.icon_name));
  }
  set_label_text(s_ui.digital_summary_label, s_ui.weather.summary);
  set_label_text(s_ui.digital_high_low_label, high_low);

  for (int i = 0; i < DAY_COUNT; ++i) {
    char day_temp[16];
    char forecast_day[8];
    snprintf(day_temp, sizeof(day_temp), "%d°", s_ui.weather.forecast[i].high);
    copy_uppercase_string(forecast_day, sizeof(forecast_day), s_ui.forecast_labels[i]);
    set_label_text(s_ui.digital_forecast_day_labels[i], forecast_day);
    set_label_text(s_ui.digital_forecast_temp_labels[i], day_temp);
    if (s_ui.digital_forecast_icons[i] != NULL) {
      lv_image_set_src(
        s_ui.digital_forecast_icons[i],
        weather_icon_image_for_name(s_ui.weather.forecast[i].icon_name)
      );
    }
  }

  char tomorrow_temps[24];
  snprintf(
    tomorrow_temps,
    sizeof(tomorrow_temps),
    "%d° / %d°",
    s_ui.weather.forecast[0].high,
    s_ui.weather.forecast[0].low
  );
  set_label_text(s_ui.forecast_tomorrow_day_label, "Tomorrow");
  if (s_ui.forecast_tomorrow_summary_label != NULL) {
    set_label_text(s_ui.forecast_tomorrow_summary_label, s_ui.weather.forecast[0].summary);
  }
  set_label_text(s_ui.forecast_tomorrow_temps_label, tomorrow_temps);
  if (s_ui.forecast_tomorrow_icon != NULL) {
    lv_image_set_src(
      s_ui.forecast_tomorrow_icon,
      weather_icon_image_for_name(s_ui.weather.forecast[0].icon_name)
    );
  }

  for (int i = 0; i < FORECAST_ROW_COUNT; ++i) {
    int item_index = i + 1;
    char row_temps[24];
    snprintf(
      row_temps,
      sizeof(row_temps),
      "%d° / %d°",
      s_ui.weather.forecast[item_index].high,
      s_ui.weather.forecast[item_index].low
    );
    set_label_text(s_ui.forecast_row_day_labels[i], s_ui.forecast_labels[item_index]);
    if (s_ui.forecast_row_summary_labels[i] != NULL) {
      set_label_text(s_ui.forecast_row_summary_labels[i], s_ui.weather.forecast[item_index].summary);
    }
    set_label_text(s_ui.forecast_row_temps_labels[i], row_temps);
    if (s_ui.forecast_row_icons[i] != NULL) {
      lv_image_set_src(
        s_ui.forecast_row_icons[i],
        weather_icon_image_for_name(s_ui.weather.forecast[item_index].icon_name)
      );
    }
  }
}

static void refresh_message_ui_state(
  const app_message_snapshot_t *snapshot,
  uint32_t unread_count,
  bool has_important_message
)
{
  s_ui.has_unread_messages = unread_count > 0;
  s_ui.has_important_messages = has_important_message;
  update_message_indicator_state();

  if (s_ui.message_title_label != NULL) {
    lv_obj_set_style_text_color(
      s_ui.message_title_label,
      s_ui.night_overlay_active
        ? color_night_primary()
        : (has_important_message ? color_important() : color_text_primary()),
      0
    );
  }

  if (snapshot == NULL || !snapshot->available) {
    if (s_ui.message_card != NULL) {
      lv_obj_add_flag(s_ui.message_card, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_ui.message_empty_label != NULL) {
      lv_obj_clear_flag(s_ui.message_empty_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_ui.message_title_label != NULL) {
      lv_obj_set_style_text_color(
        s_ui.message_title_label,
        s_ui.night_overlay_active ? color_night_primary() : color_text_primary(),
        0
      );
    }
    return;
  }

  if (s_ui.message_card != NULL) {
    lv_obj_clear_flag(s_ui.message_card, LV_OBJ_FLAG_HIDDEN);
  }
  if (s_ui.message_empty_label != NULL) {
    lv_obj_add_flag(s_ui.message_empty_label, LV_OBJ_FLAG_HIDDEN);
  }

  set_label_text(s_ui.message_text_label, snapshot->text);
  set_label_text(s_ui.message_meta_label, snapshot->meta);
  if (s_ui.message_meta_label != NULL) {
    lv_obj_set_style_text_color(
      s_ui.message_meta_label,
      s_ui.night_overlay_active
        ? color_night_muted()
        : (snapshot->important ? color_important() : color_text_muted()),
      0
    );
  }
}

static void update_clock_status(bool clock_estimated)
{
  if (s_ui.current_view != APP_VIEW_ANALOG && s_ui.current_view != APP_VIEW_DIGITAL) {
    set_label_text(s_ui.clock_status_label, "");
    lv_obj_add_flag(s_ui.clock_status_label, LV_OBJ_FLAG_HIDDEN);
    reset_clock_health();
    return;
  }

  if (clock_estimated) {
    set_label_text(s_ui.clock_status_label, "Clock pending network sync");
    lv_obj_clear_flag(s_ui.clock_status_label, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  if (clock_is_stale()) {
    set_label_text(s_ui.clock_status_label, "Clock paused");
    lv_obj_clear_flag(s_ui.clock_status_label, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  set_label_text(s_ui.clock_status_label, "");
  lv_obj_add_flag(s_ui.clock_status_label, LV_OBJ_FLAG_HIDDEN);
}

static void update_weather_status(const char *status_text)
{
  if (s_ui.night_overlay_active || s_ui.current_view == APP_VIEW_MESSAGE) {
    set_label_text(s_ui.weather_status_label, "");
    lv_obj_add_flag(s_ui.weather_status_label, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  if (status_text != NULL && status_text[0] != '\0') {
    set_label_text(s_ui.weather_status_label, status_text);
    lv_obj_clear_flag(s_ui.weather_status_label, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  set_label_text(s_ui.weather_status_label, "");
  lv_obj_add_flag(s_ui.weather_status_label, LV_OBJ_FLAG_HIDDEN);
}

static void update_night_overlay(const device_config_t *config, const struct tm *local_time)
{
  bool active = is_night_shift_active(config, local_time);
  s_ui.night_overlay_active = active;
  apply_view_night_state(active);
}

static void build_app_shell(const device_config_t *config)
{
  lv_obj_t *screen = app_screen_active();
  lv_obj_clean(screen);
  lv_obj_set_style_bg_color(screen, color_bg(), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

  memset(&s_ui, 0, sizeof(s_ui));
  s_ui.config_snapshot = *config;

  s_ui.stage = lv_obj_create(screen);
  lv_obj_set_size(s_ui.stage, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_opa(s_ui.stage, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_ui.stage, 0, 0);
  lv_obj_set_style_pad_all(s_ui.stage, 0, 0);
  lv_obj_set_scrollbar_mode(s_ui.stage, LV_SCROLLBAR_MODE_OFF);

  build_analog_view(s_ui.stage);
  yield_ui_bootstrap();
  build_digital_view(s_ui.stage);
  yield_ui_bootstrap();
  build_forecast_view(s_ui.stage);
  yield_ui_bootstrap();
  build_message_view(s_ui.stage);
  yield_ui_bootstrap();
  build_gesture_layer(s_ui.stage);
  build_status_stack(s_ui.stage);
  build_night_overlay(s_ui.stage);
  build_setup_overlay(s_ui.stage);
  build_direct_weather_icon_probe();

  if (strings_equal_ignore_case(config->default_clock_face, "analog")) {
    s_ui.last_home_view = APP_VIEW_ANALOG;
    set_view_mode(APP_VIEW_ANALOG);
  } else {
    s_ui.last_home_view = APP_VIEW_DIGITAL;
    set_view_mode(APP_VIEW_DIGITAL);
  }
}

esp_err_t app_ui_show_boot_screen(const device_config_t *config, bool touch_ready)
{
  if (config == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  build_app_shell(config);
  yield_ui_bootstrap();

  app_runtime_state_t runtime = {0};
  runtime.wifi_configured = config->wifi_ready;
  snprintf(
    runtime.weather_status,
    sizeof(runtime.weather_status),
    "%s",
    !config->wifi_ready
      ? "No Wi-Fi profile found. Tap 'Set Up Wi-Fi' to connect."
      : (config->location_ready
          ? "Stored Wi-Fi profile found. Connecting for time sync and live weather..."
          : "Stored Wi-Fi profile found. Set location for local weather.")
  );

  esp_err_t err = app_ui_update_runtime(config, &runtime, 0);
  if (err != ESP_OK) {
    return err;
  }

  if ((!config->wifi_ready || !config->location_ready) && touch_ready) {
    lv_timer_t *setup_timer = lv_timer_create(setup_autopen_timer_cb, 120, NULL);
    if (setup_timer != NULL) {
      lv_timer_set_repeat_count(setup_timer, 1);
    } else {
      open_setup_overlay();
    }
  }

  return ESP_OK;
}

esp_err_t app_ui_update_runtime(
  const device_config_t *config,
  const app_runtime_state_t *runtime,
  uint32_t uptime_seconds
)
{
  if (config == NULL || runtime == NULL || s_ui.stage == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  bool weather_changed = false;
  if (runtime->weather.valid) {
    weather_changed = !s_ui.weather.valid
      || runtime->weather.updated_at != s_ui.weather.updated_at
      || strncmp(
        runtime->weather.icon_name,
        s_ui.weather.icon_name,
        sizeof(runtime->weather.icon_name)
      ) != 0;
    s_ui.weather = runtime->weather;
    s_ui.pending_weather_diag_logged = false;
  }

  time_t display_time = resolve_display_time(config, uptime_seconds);
  refresh_forecast_labels(display_time);

  struct tm local_time;
  localtime_r(&display_time, &local_time);

  update_day_labels(&local_time);
  update_digital_clock(config, &local_time);
  update_analog_clock(&local_time);
  update_clock_health(&local_time);
  if (s_ui.weather.valid) {
    update_weather_labels();
    if (weather_changed || s_ui.last_logged_weather_updated_at != s_ui.weather.updated_at) {
      log_digital_weather_diagnostics(true);
      s_ui.last_logged_weather_updated_at = s_ui.weather.updated_at;
    }
  } else {
    update_pending_weather_labels(config->location_ready);
    if (!s_ui.pending_weather_diag_logged) {
      log_digital_weather_diagnostics(false);
      s_ui.pending_weather_diag_logged = true;
      s_ui.last_logged_weather_updated_at = 0;
    }
  }
  refresh_message_ui_state(&runtime->message, runtime->unread_message_count, runtime->has_important_message);
  update_night_overlay(config, &local_time);
  update_clock_status(s_ui.clock_is_estimated);
  update_weather_status(runtime->weather_status);
  update_setup_prompt(config);

  return ESP_OK;
}
