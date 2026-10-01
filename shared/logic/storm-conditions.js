// Storm / changing-conditions summary shown on the Conditions view, plus the
// alert reasons that drive the weather edge indicator and view reordering.
//
// Everything is computed in metric (km/h, hPa, mm -- what we request from
// Open-Meteo) and only converted for display at the end, so each alert
// threshold is defined once. Thresholds can be overridden from config.json;
// wind/gust thresholds follow the thundersnowF/thundersnowC pattern of
// accepting whichever unit matches cfg.units.
const CONDITIONS_LOOKAHEAD_HOURS = 3;
const CONDITIONS_OUTLOOK_HOURS = 6;
const PRESSURE_TREND_HOURS = 3;
const PRESSURE_STEADY_HPA = 1;

const KMH_PER_MPH = 1.609344;
const HPA_PER_INHG = 33.8639;
const MM_PER_INCH = 25.4;

// Wording thresholds (display only; alerts use getStormThresholds).
const MEASURABLE_PRECIP_MM = 0.25; // 0.01 in
const LIKELY_CHANCE = 50;
const POSSIBLE_CHANCE = 20;
const BREEZY_WIND_KMH = 32; // 20 mph
const BREEZY_GUST_KMH = 48; // 30 mph
const MODERATE_PRECIP_MM_HR = 2.5;

const DIRECTION_NAMES = ['north', 'northeast', 'east', 'southeast', 'south', 'southwest', 'west', 'northwest'];

function toDirectionName(deg) {
  if (!Number.isFinite(deg)) return null;
  return DIRECTION_NAMES[Math.round((((deg % 360) + 360) % 360) / 45) % 8];
}

// "6 PM" / "18:00" from an Open-Meteo local time like "2026-03-14T18:00".
function formatHour(isoLocal, cfg) {
  const hour = Number(String(isoLocal || '').slice(11, 13));
  if (!Number.isFinite(hour)) return '';
  if (cfg.timeFormat === '24') return `${String(hour).padStart(2, '0')}:00`;
  const h12 = hour % 12 === 0 ? 12 : hour % 12;
  return `${h12} ${hour < 12 ? 'AM' : 'PM'}`;
}

function countWhere(fromI, toI, predicate) {
  let count = 0;
  for (let i = fromI; i <= toI; i++) {
    if (predicate(i)) count += 1;
  }
  return count;
}

function firstIndexWhere(fromI, toI, predicate) {
  for (let i = fromI; i <= toI; i++) {
    if (predicate(i)) return i;
  }
  return -1;
}

const CARDINALS = ['N', 'NNE', 'NE', 'ENE', 'E', 'ESE', 'SE', 'SSE', 'S', 'SSW', 'SW', 'WSW', 'W', 'WNW', 'NW', 'NNW'];

function toCardinal(deg) {
  if (!Number.isFinite(deg)) return null;
  return CARDINALS[Math.round((((deg % 360) + 360) % 360) / 22.5) % 16];
}

function numberAt(arr, i) {
  if (!Array.isArray(arr) || i < 0 || i >= arr.length || arr[i] == null) return null;
  const v = Number(arr[i]);
  return Number.isFinite(v) ? v : null;
}

function maxOver(arr, fromI, toI) {
  let best = null;
  let bestI = -1;
  for (let i = fromI; i <= toI; i++) {
    const v = numberAt(arr, i);
    if (v != null && (best == null || v > best)) {
      best = v;
      bestI = i;
    }
  }
  return { value: best, index: bestI };
}

function sumOver(arr, fromI, toI) {
  let total = null;
  for (let i = fromI; i <= toI; i++) {
    const v = numberAt(arr, i);
    if (v != null) total = (total ?? 0) + v;
  }
  return total;
}

// The hourly row for the hour we're currently in (current_weather.time is at
// 15-minute resolution, e.g. "…T15:30" belongs to the "…T15:00" row). Both
// are in the same local timezone, so a string match is exact.
function currentHourIndex(times, nowIso) {
  if (!Array.isArray(times) || typeof nowIso !== 'string') return -1;
  return times.indexOf(`${nowIso.slice(0, 13)}:00`);
}

function getStormThresholds(cfg) {
  const imperial = cfg.units !== 'metric';
  const num = (v) => (typeof v === 'number' && Number.isFinite(v) ? v : null);
  const gustKmh = imperial
    ? (num(cfg.stormGustMph) ?? 40) * KMH_PER_MPH
    : num(cfg.stormGustKmh) ?? 64;
  const windKmh = imperial
    ? (num(cfg.stormWindMph) ?? 25) * KMH_PER_MPH
    : num(cfg.stormWindKmh) ?? 40;

  return {
    gustKmh,
    windKmh,
    pressureDropHpa: num(cfg.stormPressureDropHpa) ?? 3,
    heavyPrecipMmHr: num(cfg.stormPrecipMmHr) ?? 7.6
  };
}

function buildConditions(cfg, cur, hourly) {
  const imperial = cfg.units !== 'metric';
  const times = hourly?.time;
  const nowI = currentHourIndex(times, cur?.time);
  if (nowI < 0) return null;

  const lastI = Array.isArray(times) ? times.length - 1 : nowI;
  const aheadI = Math.min(lastI, nowI + CONDITIONS_LOOKAHEAD_HOURS);
  const outlookI = Math.min(lastI, nowI + CONDITIONS_OUTLOOK_HOURS);

  // Wind: live speed/direction from current_weather (15-min), gusts from the
  // current hourly row since current_weather doesn't carry them.
  const windKmh = Number.isFinite(Number(cur.windspeed)) ? Number(cur.windspeed) : numberAt(hourly.wind_speed_10m, nowI);
  const windDir = Number.isFinite(Number(cur.winddirection)) ? Number(cur.winddirection) : null;
  const gustNowKmh = numberAt(hourly.wind_gusts_10m, nowI);
  const gustPeak = maxOver(hourly.wind_gusts_10m, nowI, outlookI);

  // Pressure tendency over 3h. Hourly data starts at local midnight, so for
  // the first few hours of the day there's no "3h ago" row; fall back to the
  // forecast change over the next 3h, which answers the same question
  // ("is pressure dropping?").
  const pressureNow = numberAt(hourly.pressure_msl, nowI);
  let pressureChange = null;
  const pastP = numberAt(hourly.pressure_msl, nowI - PRESSURE_TREND_HOURS);
  if (pressureNow != null && pastP != null) {
    pressureChange = pressureNow - pastP;
  } else {
    const futureP = numberAt(hourly.pressure_msl, nowI + PRESSURE_TREND_HOURS);
    if (pressureNow != null && futureP != null) pressureChange = futureP - pressureNow;
  }
  let pressureTrend = null;
  if (pressureChange != null) {
    if (pressureChange <= -PRESSURE_STEADY_HPA) pressureTrend = 'falling';
    else if (pressureChange >= PRESSURE_STEADY_HPA) pressureTrend = 'rising';
    else pressureTrend = 'steady';
  }

  const precipNowMm = numberAt(hourly.precipitation, nowI);
  const precipOutlookMm = sumOver(hourly.precipitation, nowI, outlookI);
  const precipChance = maxOver(hourly.precipitation_probability, nowI, outlookI).value;

  // Alerts look at now through the next few hours so the display highlights
  // weather that's arriving, not just weather that's already here.
  const t = getStormThresholds(cfg);
  const alerts = [];
  const aheadGust = maxOver(hourly.wind_gusts_10m, nowI, aheadI);
  const aheadWindHourly = maxOver(hourly.wind_speed_10m, nowI, aheadI);
  const aheadWind = (windKmh ?? 0) >= (aheadWindHourly.value ?? 0)
    ? { value: windKmh, index: nowI }
    : aheadWindHourly;
  const aheadPrecip = maxOver(hourly.precipitation, nowI, aheadI).value;
  const aheadSnow = maxOver(hourly.snowfall, nowI, aheadI).value;
  const thunderI = firstIndexWhere(nowI, aheadI, (i) => [95, 96, 99].includes(numberAt(hourly.weathercode, i)));
  const heavyPrecipI = firstIndexWhere(nowI, aheadI, (i) => (numberAt(hourly.precipitation, i) ?? 0) >= t.heavyPrecipMmHr);

  if (thunderI >= 0) alerts.push({ kind: 'thunder', label: 'Thunderstorms' });
  if (aheadGust.value != null && aheadGust.value >= t.gustKmh) alerts.push({ kind: 'gusts', label: 'Strong gusts' });
  else if (aheadWind.value != null && aheadWind.value >= t.windKmh) alerts.push({ kind: 'wind', label: 'High winds' });
  if (pressureChange != null && pressureChange <= -t.pressureDropHpa) alerts.push({ kind: 'pressure', label: 'Falling pressure' });
  if (aheadPrecip != null && aheadPrecip >= t.heavyPrecipMmHr) {
    alerts.push({ kind: 'precip', label: aheadSnow > 0 ? 'Heavy snow' : 'Heavy rain' });
  }

  const speed = (kmh) => (kmh == null ? null : Math.round(imperial ? kmh / KMH_PER_MPH : kmh));
  const round = (v, places) => (v == null ? null : Number(v.toFixed(places)));
  const pressure = (hpa, places) => (hpa == null ? null : round(imperial ? hpa / HPA_PER_INHG : hpa, places));
  const precip = (mm) => (mm == null ? null : round(imperial ? mm / MM_PER_INCH : mm, imperial ? 2 : 1));
  const units = {
    wind: imperial ? 'mph' : 'km/h',
    pressure: imperial ? 'inHg' : 'hPa',
    precip: imperial ? 'in' : 'mm'
  };

  // --- Plain-language wording ---
  // Written here (not in the renderer) so every target shows identical text
  // and the fixtures cover it.
  const when = (i) => (i <= nowI ? 'now' : `by ${formatHour(times[i], cfg)}`);
  const snowExpected = (sumOver(hourly.snowfall, nowI, outlookI) ?? 0) > 0;
  const precipWord = snowExpected ? 'Snow' : 'Rain';

  let pressureStatus = null;
  let pressureMeaning = null;
  if (pressureChange != null) {
    if (pressureChange <= -t.pressureDropHpa) [pressureStatus, pressureMeaning] = ['Dropping fast', 'Can mean a storm is coming'];
    else if (pressureTrend === 'falling') [pressureStatus, pressureMeaning] = ['Dropping', 'Can mean clouds or rain'];
    else if (pressureTrend === 'rising') [pressureStatus, pressureMeaning] = ['Rising', 'Can mean clearer weather'];
    else [pressureStatus, pressureMeaning] = ['Steady', 'Weather likely to stay the same'];
  }

  const wetI = firstIndexWhere(nowI, outlookI, (i) => (numberAt(hourly.precipitation, i) ?? 0) >= MEASURABLE_PRECIP_MM);
  const precipLikely = wetI >= 0 || (precipChance ?? 0) >= LIKELY_CHANCE;
  const precipPossible = !precipLikely && (precipChance ?? 0) >= POSSIBLE_CHANCE;
  let precipStatus = 'None expected';
  if ((precipNowMm ?? 0) >= MEASURABLE_PRECIP_MM) precipStatus = `${precipWord}ing now`;
  else if (precipLikely) precipStatus = precipChance != null ? `Likely (${precipChance}%)` : 'Likely';
  else if (precipPossible) precipStatus = `Possible (${precipChance}%)`;
  // "Light rain for about 2 hours" -- describes what's coming without
  // repeating the amount already shown in the rain column.
  const wetHours = countWhere(nowI, outlookI, (i) => (numberAt(hourly.precipitation, i) ?? 0) >= MEASURABLE_PRECIP_MM);
  const peakRateMm = maxOver(hourly.precipitation, nowI, outlookI).value ?? 0;
  const intensity = peakRateMm >= t.heavyPrecipMmHr ? 'Heavy' : peakRateMm >= MODERATE_PRECIP_MM_HR ? 'Steady' : 'Light';
  const precipSpell = wetHours > 0
    ? `${intensity} ${precipWord.toLowerCase()} for about ${wetHours} hour${wetHours === 1 ? '' : 's'}`
    : null;

  let precipDetail = null;
  if ((precipOutlookMm ?? 0) >= MEASURABLE_PRECIP_MM) {
    precipDetail = `About ${precip(precipOutlookMm)} ${units.precip} by ${formatHour(times[outlookI], cfg)}`;
  } else if (precipLikely || precipPossible) {
    precipDetail = 'Light, if any';
  }

  // Headline: the single most important thing, in the order people care
  // about it. The detail line carries the runner-up so two alerts both get
  // a sentence; quiet days get reassurance instead of a bare number.
  const phrases = [];
  for (const alert of alerts) {
    if (alert.kind === 'thunder') phrases.push(`Thunderstorms ${thunderI <= nowI ? 'now' : `likely ${when(thunderI)}`}`);
    if (alert.kind === 'gusts') phrases.push(`Gusts up to ${speed(aheadGust.value)} ${units.wind} ${when(aheadGust.index)}`);
    if (alert.kind === 'wind') phrases.push(`Winds up to ${speed(aheadWind.value)} ${units.wind} ${when(aheadWind.index)}`);
    if (alert.kind === 'precip') phrases.push(`${alert.label} ${heavyPrecipI <= nowI ? 'now' : `likely ${when(heavyPrecipI)}`}`);
    if (alert.kind === 'pressure') phrases.push('Pressure dropping fast');
  }

  let headline;
  let detail;
  if (phrases.length > 0) {
    headline = phrases[0];
    detail = phrases[1] || (alerts[0].kind === 'pressure' ? pressureMeaning : null);
  } else {
    // Nothing alert-worthy in the next few hours, but the headline still
    // mustn't say "calm" when something is coming later in the outlook.
    const breezy = (windKmh ?? 0) >= BREEZY_WIND_KMH || (gustNowKmh ?? 0) >= BREEZY_GUST_KMH;
    const laterThunderI = firstIndexWhere(nowI, outlookI, (i) => [95, 96, 99].includes(numberAt(hourly.weathercode, i)));
    const laterGustI = firstIndexWhere(nowI, outlookI, (i) => (numberAt(hourly.wind_gusts_10m, i) ?? 0) >= t.gustKmh);
    if ((precipNowMm ?? 0) >= MEASURABLE_PRECIP_MM) {
      headline = precipStatus;
      detail = precipSpell;
    } else if (laterThunderI >= 0) {
      headline = `Thunderstorms possible ${when(laterThunderI)}`;
      detail = precipSpell;
    } else if (laterGustI >= 0) {
      headline = 'Windy later';
      detail = `Gusts up to ${speed(gustPeak.value)} ${units.wind} ${when(gustPeak.index)}`;
    } else if (precipLikely) {
      headline = `${precipWord} likely ${wetI >= 0 ? when(wetI) : 'later'}`;
      detail = precipSpell;
    } else if (precipPossible) {
      headline = `Chance of ${precipWord.toLowerCase()} later`;
      detail = `${precipChance}% over the next ${CONDITIONS_OUTLOOK_HOURS} hours`;
    } else {
      headline = breezy ? 'Breezy and dry' : 'Calm and dry';
      detail = `for the next ${CONDITIONS_OUTLOOK_HOURS} hours`;
    }
  }

  return {
    units,
    summary: { headline, detail, alert: alerts.length > 0 },
    wind: {
      speed: speed(windKmh),
      // Live speed (current_weather) and gusts (hourly) come from different
      // sources and can disagree; a gust at or below the wind speed is noise.
      gust: gustNowKmh != null && gustNowKmh > (windKmh ?? 0) ? speed(gustNowKmh) : null,
      direction: windDir,
      cardinal: toCardinal(windDir),
      from: windDir == null ? null : `from the ${toDirectionName(windDir)}`,
      peakGust: speed(gustPeak.value),
      peakGustTime: gustPeak.index >= 0 ? times[gustPeak.index] : null
    },
    pressure: {
      value: pressure(pressureNow, imperial ? 2 : 0),
      change: pressure(pressureChange, imperial ? 2 : 1),
      trend: pressureTrend,
      status: pressureStatus,
      meaning: pressureMeaning
    },
    precip: {
      label: precipWord,
      now: precip(precipNowMm),
      outlook: precip(precipOutlookMm),
      outlookHours: CONDITIONS_OUTLOOK_HOURS,
      chance: precipChance,
      status: precipStatus,
      detail: precipDetail
    },
    alerts
  };
}

module.exports = {
  CONDITIONS_LOOKAHEAD_HOURS,
  CONDITIONS_OUTLOOK_HOURS,
  PRESSURE_TREND_HOURS,
  getStormThresholds,
  formatHour,
  buildConditions
};
