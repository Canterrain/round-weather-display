#pragma once

#include "cJSON.h"

/* Pure C port of shared/logic/forecast-representative.js -- picks a single
 * representative weather code for a forecast day from Open-Meteo's hourly
 * data, the same way the Pi target does. Deliberately has zero ESP-IDF
 * dependencies (only cJSON + the C standard library) so it can be compiled
 * natively on a dev machine for the parity tests in targets/esp32-p4/tests/,
 * which check this against the exact same fixtures
 * (shared/test-data/forecast-representative-cases.json) the JS
 * implementation is tested against. If you change the heuristic here,
 * change it in the JS too (or vice versa) and re-run both test suites --
 * see targets/esp32-p4/tests/README.md.
 */

int forecast_pick_representative_code(
  const cJSON *hourly,
  const char *date,
  int daily_code,
  int daily_high,
  int daily_low,
  int snow_threshold
);
