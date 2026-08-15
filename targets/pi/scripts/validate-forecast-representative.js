const assert = require('assert');
const {
  pickRepresentativeForecastCode,
  FORECAST_HEURISTIC_THRESHOLDS
} = require('../../../shared/logic/forecast-representative');

// Cases live in shared/test-data/ (not hardcoded here) so the exact same
// fixtures can be run against the ESP32-P4 target's C port of this same
// heuristic (targets/esp32-p4/tests/) -- see forecast_representative.c.
// Keeping the two in parity is the point; if you're editing the heuristic
// and this file's case list, you're doing it in the wrong place.
const cases = require('../../../shared/test-data/forecast-representative-cases.json');

for (const testCase of cases) {
  const result = pickRepresentativeForecastCode(testCase);
  assert.strictEqual(
    result.code,
    testCase.expectedCode,
    `${testCase.name}: expected ${testCase.expectedCode}, got ${result.code}`
  );
  console.log(`PASS ${testCase.name}: ${result.code}`);
}

console.log('Thresholds:', FORECAST_HEURISTIC_THRESHOLDS);
