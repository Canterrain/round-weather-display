#!/usr/bin/env node

const {
  buildGeocodeSearchUrl,
  normalizeGeocodeResult,
  parseLocationQuery,
  pickBestGeocodeResult
} = require('../logic/open-meteo-location');

async function fetchJsonWithTimeout(url, timeoutMs = 10000) {
  if (typeof fetch !== 'function') {
    throw new Error('Global fetch is required. Use Node.js 18+.');
  }

  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), timeoutMs);

  try {
    const response = await fetch(url, { signal: controller.signal });
    const data = await response.json();
    return { response, data };
  } finally {
    clearTimeout(timer);
  }
}

async function main() {
  const location = process.argv.slice(2).join(' ').trim();
  if (!location) {
    throw new Error('Usage: node shared/scripts/resolve-location.js "City,ST,CC"');
  }

  const query = parseLocationQuery(location);
  const url = buildGeocodeSearchUrl(query);
  const { response, data } = await fetchJsonWithTimeout(url, 10000);

  if (!response.ok || !data || !Array.isArray(data.results) || data.results.length === 0) {
    throw new Error(`Geocoding failed for location "${location}"`);
  }

  const best = pickBestGeocodeResult(data.results, query);
  const normalized = normalizeGeocodeResult(best);
  process.stdout.write(`${JSON.stringify(normalized)}\n`);
}

main().catch((error) => {
  console.error(error.message || String(error));
  process.exit(1);
});
