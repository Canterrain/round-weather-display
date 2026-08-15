const OPEN_METEO_GEOCODING_URL = 'https://geocoding-api.open-meteo.com/v1/search';
const DEFAULT_GEOCODE_RESULT_COUNT = 10;

const US_STATE = {
  AL: 'Alabama',
  AK: 'Alaska',
  AZ: 'Arizona',
  AR: 'Arkansas',
  CA: 'California',
  CO: 'Colorado',
  CT: 'Connecticut',
  DE: 'Delaware',
  FL: 'Florida',
  GA: 'Georgia',
  HI: 'Hawaii',
  ID: 'Idaho',
  IL: 'Illinois',
  IN: 'Indiana',
  IA: 'Iowa',
  KS: 'Kansas',
  KY: 'Kentucky',
  LA: 'Louisiana',
  ME: 'Maine',
  MD: 'Maryland',
  MA: 'Massachusetts',
  MI: 'Michigan',
  MN: 'Minnesota',
  MS: 'Mississippi',
  MO: 'Missouri',
  MT: 'Montana',
  NE: 'Nebraska',
  NV: 'Nevada',
  NH: 'New Hampshire',
  NJ: 'New Jersey',
  NM: 'New Mexico',
  NY: 'New York',
  NC: 'North Carolina',
  ND: 'North Dakota',
  OH: 'Ohio',
  OK: 'Oklahoma',
  OR: 'Oregon',
  PA: 'Pennsylvania',
  RI: 'Rhode Island',
  SC: 'South Carolina',
  SD: 'South Dakota',
  TN: 'Tennessee',
  TX: 'Texas',
  UT: 'Utah',
  VT: 'Vermont',
  VA: 'Virginia',
  WA: 'Washington',
  WV: 'West Virginia',
  WI: 'Wisconsin',
  WY: 'Wyoming',
  DC: 'District of Columbia'
};

function normalizeStateName(value) {
  if (typeof value !== 'string') return null;
  const trimmed = value.trim();
  if (!trimmed) return null;

  if (trimmed.length === 2 && US_STATE[trimmed.toUpperCase()]) {
    return US_STATE[trimmed.toUpperCase()];
  }

  return trimmed;
}

function parseLocationQuery(location) {
  const raw = typeof location === 'string' ? location.trim() : '';
  const parts = raw.split(',').map((part) => part.trim()).filter(Boolean);
  const name = parts[0] || raw;
  const state = parts.length >= 2 ? normalizeStateName(parts[1]) : null;
  const country = parts.length >= 3 ? parts[2] : null;
  const countryCode = /^[A-Za-z]{2}$/.test(country || '') ? country.toUpperCase() : null;

  return { raw, name, state, countryCode };
}

function buildGeocodeSearchUrl(locationOrQuery, options = {}) {
  const query = typeof locationOrQuery === 'string'
    ? parseLocationQuery(locationOrQuery)
    : (locationOrQuery || {});

  if (!query.name) {
    throw new Error('Geocoding requires a non-empty location name');
  }

  const maxResults = Number.isInteger(options.maxResults) && options.maxResults > 0
    ? options.maxResults
    : DEFAULT_GEOCODE_RESULT_COUNT;

  const url = new URL(OPEN_METEO_GEOCODING_URL);
  url.searchParams.set('name', query.name);
  url.searchParams.set('count', String(maxResults));
  url.searchParams.set('language', 'en');
  url.searchParams.set('format', 'json');

  if (query.countryCode) {
    url.searchParams.set('countryCode', query.countryCode);
  }

  return url.toString();
}

function scoreGeocodeResult(result, locationOrQuery) {
  const query = typeof locationOrQuery === 'string'
    ? parseLocationQuery(locationOrQuery)
    : (locationOrQuery || {});

  const wantName = String(query.name || '').trim().toLowerCase();
  const resultName = String(result?.name || '').trim().toLowerCase();
  const resultAdmin1 = String(result?.admin1 || '').trim().toLowerCase();
  const resultCountryCode = String(result?.country_code || '').trim().toUpperCase();

  let score = 0;

  if (resultName === wantName) {
    score += 100;
  } else if (wantName && resultName.includes(wantName)) {
    score += 30;
  }

  if (query.state && resultAdmin1 === query.state.toLowerCase()) {
    score += 80;
  }

  if (query.countryCode && resultCountryCode === query.countryCode) {
    score += 20;
  }

  const population = Number(result?.population) || 0;
  score += Math.min(Math.floor(population / 1000), 25);

  return score;
}

function pickBestGeocodeResult(results, locationOrQuery) {
  if (!Array.isArray(results) || results.length === 0) {
    return null;
  }

  return results.reduce((best, current) => {
    if (!best) return current;
    return scoreGeocodeResult(current, locationOrQuery) > scoreGeocodeResult(best, locationOrQuery)
      ? current
      : best;
  }, null);
}

function formatResolvedLocation(result) {
  return [result?.name, result?.admin1, result?.country_code].filter(Boolean).join(', ');
}

function normalizeGeocodeResult(result) {
  if (!result || typeof result !== 'object') {
    throw new Error('Geocoding result is missing');
  }

  if (typeof result.latitude !== 'number' || typeof result.longitude !== 'number') {
    throw new Error('Geocoding result is missing numeric latitude/longitude');
  }

  return {
    lat: result.latitude,
    lon: result.longitude,
    timezone: result.timezone || 'auto',
    resolvedName: formatResolvedLocation(result)
  };
}

module.exports = {
  DEFAULT_GEOCODE_RESULT_COUNT,
  OPEN_METEO_GEOCODING_URL,
  US_STATE,
  buildGeocodeSearchUrl,
  formatResolvedLocation,
  normalizeGeocodeResult,
  normalizeStateName,
  parseLocationQuery,
  pickBestGeocodeResult,
  scoreGeocodeResult
};
