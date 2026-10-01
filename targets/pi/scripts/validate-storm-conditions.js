const assert = require('assert');
const { buildConditions } = require('../../../shared/logic/storm-conditions');

// Cases live in shared/test-data/ so the ESP32-P4 port of this logic can be
// checked against the exact same fixtures. Each case only asserts the fields
// listed under `expected`.
const cases = require('../../../shared/test-data/storm-conditions-cases.json');

function assertSubset(actual, expected, path) {
  for (const [key, value] of Object.entries(expected)) {
    const here = `${path}.${key}`;
    if (value && typeof value === 'object' && !Array.isArray(value)) {
      assertSubset(actual?.[key], value, here);
    } else {
      assert.deepStrictEqual(actual?.[key], value, `${here}: expected ${JSON.stringify(value)}, got ${JSON.stringify(actual?.[key])}`);
    }
  }
}

for (const testCase of cases) {
  const result = buildConditions(testCase.cfg, testCase.current, testCase.hourly);

  if (testCase.expected === null) {
    assert.strictEqual(result, null, `${testCase.name}: expected null`);
  } else {
    const { alerts, alertLabels, ...rest } = testCase.expected;
    assert.deepStrictEqual(result.alerts.map((a) => a.kind), alerts, `${testCase.name}: alerts`);
    if (alertLabels) {
      assert.deepStrictEqual(result.alerts.map((a) => a.label), alertLabels, `${testCase.name}: alert labels`);
    }
    assertSubset(result, rest, testCase.name);
  }
  console.log(`PASS ${testCase.name}`);
}
