/* Runs shared/test-data/storm-conditions-cases.json -- the exact fixtures
 * targets/pi/scripts/validate-storm-conditions.js is tested against --
 * through the ESP32-P4 target's C port (main/storm_conditions.c).
 *
 * The C result is converted back into the same JSON shape the JS returns,
 * then each case's `expected` object is checked the same way the JS
 * validator does it: alert kinds (and labels when listed) exactly, and every
 * other listed field as a subset match.
 *
 * Compiles natively (plain cc, no ESP-IDF toolchain) via
 * scripts/test-weather-parity.sh.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "storm_conditions.h"

static char *read_file(const char *path)
{
  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    fprintf(stderr, "Could not open %s\n", path);
    return NULL;
  }

  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  fseek(file, 0, SEEK_SET);

  char *buffer = malloc((size_t) size + 1);
  if (buffer == NULL) {
    fclose(file);
    return NULL;
  }

  size_t read = fread(buffer, 1, (size_t) size, file);
  buffer[read] = '\0';
  fclose(file);
  return buffer;
}

static void add_optional_number(cJSON *object, const char *key, bool present, double value)
{
  if (present) {
    cJSON_AddNumberToObject(object, key, value);
  } else {
    cJSON_AddNullToObject(object, key);
  }
}

static void add_optional_string(cJSON *object, const char *key, bool present, const char *value)
{
  if (present) {
    cJSON_AddStringToObject(object, key, value);
  } else {
    cJSON_AddNullToObject(object, key);
  }
}

/* Same shape as buildConditions() in shared/logic/storm-conditions.js. */
static cJSON *conditions_to_json(const storm_conditions_t *c)
{
  cJSON *root = cJSON_CreateObject();

  cJSON *units = cJSON_AddObjectToObject(root, "units");
  cJSON_AddStringToObject(units, "wind", c->wind_unit);
  cJSON_AddStringToObject(units, "pressure", c->pressure_unit);
  cJSON_AddStringToObject(units, "precip", c->precip_unit);

  cJSON *summary = cJSON_AddObjectToObject(root, "summary");
  cJSON_AddStringToObject(summary, "headline", c->headline);
  add_optional_string(summary, "detail", c->has_detail, c->detail);
  cJSON_AddBoolToObject(summary, "alert", c->alert);
  cJSON_AddBoolToObject(summary, "changing", c->changing);
  cJSON_AddStringToObject(summary, "changeLevel", storm_change_level_name(c->change_level));
  cJSON_AddStringToObject(summary, "changeLabel", c->change_label);

  cJSON *wind = cJSON_AddObjectToObject(root, "wind");
  add_optional_number(wind, "speed", c->has_speed, c->speed);
  add_optional_number(wind, "gust", c->has_gust, c->gust);
  add_optional_number(wind, "direction", c->has_direction, c->direction);
  add_optional_string(wind, "cardinal", c->has_direction, c->cardinal);
  add_optional_string(wind, "from", c->has_direction, c->from);
  add_optional_number(wind, "peakGust", c->has_peak_gust, c->peak_gust);
  add_optional_string(wind, "peakGustTime", c->peak_gust_time[0] != '\0', c->peak_gust_time);
  cJSON *outlook = cJSON_AddObjectToObject(wind, "outlook");
  cJSON_AddStringToObject(outlook, "status", c->outlook_status);
  cJSON_AddStringToObject(outlook, "detail", c->outlook_detail);

  cJSON *pressure = cJSON_AddObjectToObject(root, "pressure");
  add_optional_number(pressure, "value", c->has_pressure, c->pressure_value);
  add_optional_number(pressure, "change", c->has_pressure_change, c->pressure_change);
  add_optional_string(pressure, "trend", c->pressure_trend[0] != '\0', c->pressure_trend);
  add_optional_string(pressure, "status", c->pressure_status[0] != '\0', c->pressure_status);
  add_optional_string(pressure, "meaning", c->pressure_meaning[0] != '\0', c->pressure_meaning);

  cJSON *precip = cJSON_AddObjectToObject(root, "precip");
  cJSON_AddStringToObject(precip, "label", c->precip_label);
  add_optional_number(precip, "now", c->has_precip_now, c->precip_now);
  add_optional_number(precip, "outlook", c->has_precip_outlook, c->precip_outlook);
  cJSON_AddNumberToObject(precip, "outlookHours", c->outlook_hours);
  add_optional_number(precip, "chance", c->has_chance, c->chance);
  cJSON_AddStringToObject(precip, "status", c->precip_status);
  add_optional_string(precip, "detail", c->has_precip_detail, c->precip_detail);

  cJSON *alerts = cJSON_AddArrayToObject(root, "alerts");
  for (int i = 0; i < c->alert_count; ++i) {
    cJSON *alert = cJSON_CreateObject();
    cJSON_AddStringToObject(alert, "kind", storm_alert_kind_name(c->alerts[i].kind));
    cJSON_AddStringToObject(alert, "label", c->alerts[i].label);
    cJSON_AddItemToArray(alerts, alert);
  }

  return root;
}

static double number_or_nan(const cJSON *value)
{
  return cJSON_IsNumber(value) ? cJSON_GetNumberValue(value) : NAN;
}

static storm_conditions_config_t config_from_fixture(const cJSON *cfg)
{
  storm_conditions_config_t config;
  storm_conditions_default_config(&config);
  const cJSON *units = cJSON_GetObjectItemCaseSensitive(cfg, "units");
  const cJSON *time_format = cJSON_GetObjectItemCaseSensitive(cfg, "timeFormat");
  config.metric = cJSON_IsString(units) && strcmp(cJSON_GetStringValue(units), "metric") == 0;
  config.time_24h = cJSON_IsString(time_format) && strcmp(cJSON_GetStringValue(time_format), "24") == 0;
  /* The Pi reads whichever unit's key matches cfg.units. */
  config.gust_override = number_or_nan(cJSON_GetObjectItemCaseSensitive(cfg, config.metric ? "stormGustKmh" : "stormGustMph"));
  config.wind_override = number_or_nan(cJSON_GetObjectItemCaseSensitive(cfg, config.metric ? "stormWindKmh" : "stormWindMph"));
  config.pressure_drop_hpa_override = number_or_nan(cJSON_GetObjectItemCaseSensitive(cfg, "stormPressureDropHpa"));
  config.precip_mm_hr_override = number_or_nan(cJSON_GetObjectItemCaseSensitive(cfg, "stormPrecipMmHr"));
  return config;
}

static bool values_equal(const cJSON *actual, const cJSON *expected)
{
  if (cJSON_IsNull(expected)) {
    return actual == NULL || cJSON_IsNull(actual);
  }
  if (cJSON_IsNumber(expected)) {
    return cJSON_IsNumber(actual) && fabs(cJSON_GetNumberValue(actual) - cJSON_GetNumberValue(expected)) < 1e-9;
  }
  if (cJSON_IsString(expected)) {
    return cJSON_IsString(actual) && strcmp(cJSON_GetStringValue(actual), cJSON_GetStringValue(expected)) == 0;
  }
  if (cJSON_IsBool(expected)) {
    return cJSON_IsBool(actual) && cJSON_IsTrue(actual) == cJSON_IsTrue(expected);
  }
  return cJSON_Compare(actual, expected, true);
}

/* Mirrors assertSubset() in validate-storm-conditions.js. */
static bool check_subset(const char *case_name, const char *path, const cJSON *actual, const cJSON *expected)
{
  bool ok = true;
  const cJSON *item = NULL;
  cJSON_ArrayForEach(item, expected) {
    char here[160];
    snprintf(here, sizeof(here), "%s.%s", path, item->string);
    const cJSON *actual_item = actual != NULL ? cJSON_GetObjectItemCaseSensitive(actual, item->string) : NULL;
    if (cJSON_IsObject(item)) {
      ok = check_subset(case_name, here, actual_item, item) && ok;
    } else if (!values_equal(actual_item, item)) {
      char *want = cJSON_PrintUnformatted(item);
      char *got = actual_item != NULL ? cJSON_PrintUnformatted(actual_item) : NULL;
      printf("FAIL %s: %s expected %s, got %s\n", case_name, here, want, got != NULL ? got : "undefined");
      free(want);
      free(got);
      ok = false;
    }
  }
  return ok;
}

static bool check_alerts(const char *case_name, const cJSON *alerts, const cJSON *expected, const char *key)
{
  if (expected == NULL) {
    return true;
  }
  bool ok = cJSON_GetArraySize(alerts) == cJSON_GetArraySize(expected);
  for (int i = 0; ok && i < cJSON_GetArraySize(expected); ++i) {
    const cJSON *got = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(alerts, i), key);
    ok = values_equal(got, cJSON_GetArrayItem(expected, i));
  }
  if (!ok) {
    char *want = cJSON_PrintUnformatted(expected);
    printf("FAIL %s: alert %ss expected %s\n", case_name, key, want);
    free(want);
  }
  return ok;
}

int main(int argc, char **argv)
{
  const char *fixture_path = argc > 1 ? argv[1] : "shared/test-data/storm-conditions-cases.json";

  char *json_text = read_file(fixture_path);
  if (json_text == NULL) {
    return 1;
  }

  cJSON *cases = cJSON_Parse(json_text);
  free(json_text);
  if (!cJSON_IsArray(cases)) {
    fprintf(stderr, "Fixture file did not parse as a JSON array: %s\n", fixture_path);
    return 1;
  }

  int failures = 0;
  int total = 0;
  const cJSON *test_case = NULL;

  cJSON_ArrayForEach(test_case, cases) {
    total++;
    const char *name = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(test_case, "name"));
    name = name != NULL ? name : "(unnamed)";
    storm_conditions_config_t config = config_from_fixture(cJSON_GetObjectItemCaseSensitive(test_case, "cfg"));
    const cJSON *expected = cJSON_GetObjectItemCaseSensitive(test_case, "expected");

    storm_conditions_t result;
    bool built = storm_conditions_build(
      &config,
      cJSON_GetObjectItemCaseSensitive(test_case, "current"),
      cJSON_GetObjectItemCaseSensitive(test_case, "hourly"),
      cJSON_GetObjectItemCaseSensitive(test_case, "daily"),
      &result
    );

    bool ok;
    if (cJSON_IsNull(expected)) {
      ok = !built;
      if (!ok) {
        printf("FAIL %s: expected null\n", name);
      }
    } else if (!built) {
      printf("FAIL %s: expected a result, got null\n", name);
      ok = false;
    } else {
      cJSON *actual = conditions_to_json(&result);
      cJSON *rest = cJSON_Duplicate(expected, true);
      const cJSON *alerts = cJSON_GetObjectItemCaseSensitive(actual, "alerts");
      ok = check_alerts(name, alerts, cJSON_GetObjectItemCaseSensitive(rest, "alerts"), "kind");
      ok = check_alerts(name, alerts, cJSON_GetObjectItemCaseSensitive(rest, "alertLabels"), "label") && ok;
      cJSON_DeleteItemFromObjectCaseSensitive(rest, "alerts");
      cJSON_DeleteItemFromObjectCaseSensitive(rest, "alertLabels");
      ok = check_subset(name, name, actual, rest) && ok;
      cJSON_Delete(rest);
      cJSON_Delete(actual);
    }

    if (ok) {
      printf("PASS %s\n", name);
    } else {
      failures++;
    }
  }

  cJSON_Delete(cases);

  printf("%d/%d storm-conditions cases passed\n", total - failures, total);
  return failures > 0 ? 1 : 0;
}
