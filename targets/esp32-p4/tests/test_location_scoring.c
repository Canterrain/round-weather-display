/* Runs shared/test-data/location-resolution-cases.json -- the exact
 * fixtures shared/scripts/validate-location-resolution.js is tested
 * against -- through the ESP32-P4 target's C port
 * (main/location_scoring.c) of the same geocoding-result scorer. If the
 * two implementations diverge, one of the two test runs fails; this is how
 * that gets caught during development instead of shipping to users.
 *
 * Compiles natively (plain cc, no ESP-IDF toolchain) via
 * scripts/test-weather-parity.sh.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "location_scoring.h"

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

static const char *get_string(const cJSON *object, const char *key)
{
  cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
  return cJSON_IsString(value) ? value->valuestring : NULL;
}

int main(int argc, char **argv)
{
  const char *fixture_path = argc > 1 ? argv[1] : "shared/test-data/location-resolution-cases.json";

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
    const char *name = get_string(test_case, "name");
    const char *query = get_string(test_case, "query");
    cJSON *results = cJSON_GetObjectItemCaseSensitive(test_case, "results");
    const char *expected_resolved_name = get_string(test_case, "expectedResolvedName");
    const char *expected_timezone = get_string(test_case, "expectedTimezone");

    location_query_parts_t query_parts;
    location_scoring_parse_query(query, &query_parts);

    cJSON *best = location_scoring_select_best_result(results, &query_parts);

    char resolved_name[256] = "";
    const char *actual_timezone = NULL;

    if (best != NULL) {
      location_scoring_format_label(
        get_string(best, "name"),
        get_string(best, "admin1"),
        get_string(best, "country_code"),
        resolved_name,
        sizeof(resolved_name)
      );
      actual_timezone = get_string(best, "timezone");
    }

    bool name_ok = expected_resolved_name == NULL
      ? (best == NULL)
      : (strcmp(resolved_name, expected_resolved_name) == 0);
    bool timezone_ok = expected_timezone == NULL
      || (actual_timezone != NULL && strcmp(actual_timezone, expected_timezone) == 0);

    if (name_ok && timezone_ok) {
      printf("PASS %s: %s\n", name != NULL ? name : "(unnamed)", resolved_name);
    } else {
      printf(
        "FAIL %s: expected name=%s timezone=%s, got name=%s timezone=%s\n",
        name != NULL ? name : "(unnamed)",
        expected_resolved_name != NULL ? expected_resolved_name : "(none)",
        expected_timezone != NULL ? expected_timezone : "(none)",
        resolved_name,
        actual_timezone != NULL ? actual_timezone : "(none)"
      );
      failures++;
    }
  }

  cJSON_Delete(cases);

  printf("%d/%d location cases passed\n", total - failures, total);
  return failures > 0 ? 1 : 0;
}
