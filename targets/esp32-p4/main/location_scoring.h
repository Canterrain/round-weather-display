#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "cJSON.h"

/* Pure C port of shared/logic/open-meteo-location.js -- parses a
 * "City, State, Country" query and scores Open-Meteo geocoding results
 * against it, the same way the Pi target does. Deliberately has zero
 * ESP-IDF dependencies (only cJSON + the C standard library) so it can be
 * compiled natively on a dev machine for the parity tests in
 * targets/esp32-p4/tests/, which check this against the exact same
 * fixtures (shared/test-data/location-resolution-cases.json) the JS
 * implementation is tested against. If you change the scoring here, change
 * it in the JS too (or vice versa) and re-run both test suites -- see
 * targets/esp32-p4/tests/README.md.
 */

#define LOCATION_SCORING_STR_LEN 64

typedef struct {
  char name[LOCATION_SCORING_STR_LEN];
  char state[LOCATION_SCORING_STR_LEN];
  char country[8];
  bool country_is_alpha2;
  char state_name[LOCATION_SCORING_STR_LEN];
} location_query_parts_t;

/* Trims and splits a "Name, State, Country" query on commas; resolves a
 * 2-letter US state abbreviation to its full name in state_name. */
void location_scoring_parse_query(const char *raw_query, location_query_parts_t *out_parts);

/* Picks the highest-scoring entry from an Open-Meteo /v1/search "results"
 * array. Returns NULL if results is empty/not an array. */
cJSON *location_scoring_select_best_result(cJSON *results, const location_query_parts_t *query_parts);

/* Joins whichever of {name, admin1, country_code} are non-empty with ", ",
 * matching shared/logic/open-meteo-location.js's formatResolvedLocation
 * exactly (`[name, admin1, country_code].filter(Boolean).join(', ')`). */
void location_scoring_format_label(
  const char *name,
  const char *admin1,
  const char *country_code,
  char *output,
  size_t output_size
);
