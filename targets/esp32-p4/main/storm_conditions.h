#pragma once

#include <stdbool.h>

#include "cJSON.h"

/* Pure C port of shared/logic/storm-conditions.js -- the storm / changing-
 * conditions summary behind the Conditions view: wind, gusts, pressure trend,
 * precipitation, alert reasons, and all of the plain-language wording
 * (headline, column values). Like forecast_representative.c it has zero
 * ESP-IDF dependencies (only cJSON + the C standard library), so
 * tests/test_storm_conditions.c can run it natively against the exact same
 * fixtures (shared/test-data/storm-conditions-cases.json) the JS is tested
 * against. If you change the logic or wording here, change it in the JS too
 * (or vice versa) and re-run both -- see scripts/test-weather-parity.sh.
 */

#define STORM_TEXT_LEN 80
#define STORM_SHORT_TEXT_LEN 24
#define STORM_MAX_ALERTS 4

typedef enum {
  STORM_ALERT_THUNDER = 0,
  STORM_ALERT_GUSTS,
  STORM_ALERT_WIND,
  STORM_ALERT_PRESSURE,
  STORM_ALERT_PRECIP
} storm_alert_kind_t;

typedef enum {
  STORM_CHANGE_STEADY = 0,
  STORM_CHANGE_MAY_CHANGE,
  STORM_CHANGE_CHANGING
} storm_change_level_t;

typedef struct {
  storm_alert_kind_t kind;
  char label[STORM_SHORT_TEXT_LEN];
} storm_alert_t;

/* Threshold overrides are in the display unit for wind (mph when imperial,
 * km/h when metric), matching the Pi's stormGustMph / stormGustKmh config
 * keys. Leave them as NAN to use the defaults. */
typedef struct {
  bool metric;
  bool time_24h;
  double gust_override;
  double wind_override;
  double pressure_drop_hpa_override;
  double precip_mm_hr_override;
} storm_conditions_config_t;

typedef struct {
  /* Units */
  char wind_unit[8];
  char pressure_unit[8];
  char precip_unit[4];

  /* Summary: headline + detail curve along the top; the divider label sits
   * above the columns. */
  char headline[STORM_TEXT_LEN];
  bool has_detail;
  char detail[STORM_TEXT_LEN];
  bool alert;
  bool changing;
  storm_change_level_t change_level;
  char change_label[STORM_SHORT_TEXT_LEN];

  /* Wind (compass shows now; outlook is the column) */
  bool has_speed;
  int speed;
  bool has_gust;
  int gust;
  bool has_direction;
  double direction;
  char cardinal[4];
  char from[STORM_SHORT_TEXT_LEN];
  bool has_peak_gust;
  int peak_gust;
  char peak_gust_time[20];
  char outlook_status[16];
  char outlook_detail[STORM_TEXT_LEN];

  /* Pressure */
  bool has_pressure;
  double pressure_value;
  bool has_pressure_change;
  double pressure_change;
  char pressure_trend[8];
  char pressure_status[16];
  char pressure_meaning[40];

  /* Precipitation */
  char precip_label[8];
  bool has_precip_now;
  double precip_now;
  bool has_precip_outlook;
  double precip_outlook;
  int outlook_hours;
  bool has_chance;
  double chance;
  /* Room for any number JS would print before " chance". */
  char precip_status[40];
  bool has_precip_detail;
  char precip_detail[STORM_TEXT_LEN];

  int alert_count;
  storm_alert_t alerts[STORM_MAX_ALERTS];
} storm_conditions_t;

void storm_conditions_default_config(storm_conditions_config_t *config);

/* `current` is Open-Meteo's current_weather object; `hourly` and `daily` are
 * the response's hourly/daily objects (daily may be NULL). Returns false when
 * the current hour can't be found in the hourly data (the JS returns null). */
bool storm_conditions_build(
  const storm_conditions_config_t *config,
  const cJSON *current,
  const cJSON *hourly,
  const cJSON *daily,
  storm_conditions_t *out
);

const char *storm_alert_kind_name(storm_alert_kind_t kind);
const char *storm_change_level_name(storm_change_level_t level);
