/* Runs shared/test-data/forecast-representative-cases.json -- the exact
 * fixtures targets/pi/scripts/validate-forecast-representative.js is tested
 * against -- through the ESP32-P4 target's C port
 * (main/forecast_representative.c) of the same heuristic. If the two
 * implementations diverge, one of the two test runs fails; this is how
 * that gets caught during development instead of shipping to users.
 *
 * Compiles natively (plain cc, no ESP-IDF toolchain) via
 * scripts/test-weather-parity.sh.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "forecast_representative.h"

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

static int get_int(const cJSON *object, const char *key, int fallback)
{
  cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
  return cJSON_IsNumber(value) ? (int) cJSON_GetNumberValue(value) : fallback;
}

int main(int argc, char **argv)
{
  const char *fixture_path = argc > 1 ? argv[1] : "shared/test-data/forecast-representative-cases.json";

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
  cJSON *test_case = NULL;

  cJSON_ArrayForEach(test_case, cases) {
    total++;
    const char *name = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(test_case, "name"));
    const char *date = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(test_case, "date"));
    cJSON *hourly = cJSON_GetObjectItemCaseSensitive(test_case, "hourly");
    int daily_code = get_int(test_case, "dailyCode", 3);
    int daily_high = get_int(test_case, "dailyHigh", 0);
    int daily_low = get_int(test_case, "dailyLow", 0);
    int snow_threshold = get_int(test_case, "snowTempThreshold", 34);
    int expected_code = get_int(test_case, "expectedCode", -1);

    int actual_code = forecast_pick_representative_code(
      hourly, date, daily_code, daily_high, daily_low, snow_threshold
    );

    if (actual_code == expected_code) {
      printf("PASS %s: %d\n", name != NULL ? name : "(unnamed)", actual_code);
    } else {
      printf("FAIL %s: expected %d, got %d\n", name != NULL ? name : "(unnamed)", expected_code, actual_code);
      failures++;
    }
  }

  cJSON_Delete(cases);

  printf("%d/%d forecast cases passed\n", total - failures, total);
  return failures > 0 ? 1 : 0;
}
