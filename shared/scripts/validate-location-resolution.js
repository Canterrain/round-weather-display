#!/usr/bin/env node

const assert = require('assert');
const path = require('path');

const fixtures = require(path.join(__dirname, '..', 'test-data', 'location-resolution-cases.json'));
const {
  normalizeGeocodeResult,
  pickBestGeocodeResult
} = require('../logic/open-meteo-location');

for (const testCase of fixtures) {
  const best = pickBestGeocodeResult(testCase.results, testCase.query);
  const normalized = normalizeGeocodeResult(best);

  assert.strictEqual(
    normalized.resolvedName,
    testCase.expectedResolvedName,
    `${testCase.name}: expected resolved name ${testCase.expectedResolvedName}, got ${normalized.resolvedName}`
  );
  assert.strictEqual(
    normalized.timezone,
    testCase.expectedTimezone,
    `${testCase.name}: expected timezone ${testCase.expectedTimezone}, got ${normalized.timezone}`
  );

  console.log(`PASS ${testCase.name}: ${normalized.resolvedName}`);
}
