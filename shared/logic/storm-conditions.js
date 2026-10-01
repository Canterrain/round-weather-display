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
// Pressure naturally swings about 1 hPa twice a day (the atmospheric tide),
// so anything inside this band reads as steady rather than as weather.
const PRESSURE_STEADY_HPA = 1.5;

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
// A wind outlook counts as "picking up" when gusts climb by this much.
const PICKING_UP_GUST_KMH = 16; // 10 mph
// Temperature swings, in the display unit (cfg.units picks F or C). A 15F
// drop inside 3h is a front, not ordinary evening cooling.
const FRONT_DROP_3H = { imperial: 15, metric: 8 };
const DAY_TO_DAY_SWING = { imperial: 15, metric: 8 };

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

// For something happening at fromI: the first later hour from which it has
// stopped for the rest of the window, or -1 if it carries on to the end.
function endIndexWhere(fromI, toI, predicate) {
  let end = -1;
  for (let i = toI; i > fromI; i--) {
    if (predicate(i)) break;
    end = i;
  }
  return end;
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

function buildConditions(cfg, cur, hourly, daily) {
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
  let precipStatus = `No ${precipWord.toLowerCase()}`;
  // Short enough for the narrow rain column; the headline carries timing.
  if ((precipNowMm ?? 0) >= MEASURABLE_PRECIP_MM) precipStatus = `${precipWord}ing`;
  else if (precipLikely) precipStatus = precipChance != null ? `${precipChance}% chance` : 'Likely';
  else if (precipPossible) precipStatus = `${precipChance}% chance`;
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

  const isThunder = (i) => [95, 96, 99].includes(numberAt(hourly.weathercode, i));
  const isWet = (i) => (numberAt(hourly.precipitation, i) ?? 0) >= MEASURABLE_PRECIP_MM;
  const isHeavy = (i) => (numberAt(hourly.precipitation, i) ?? 0) >= t.heavyPrecipMmHr;
  const gustAt = (i) => numberAt(hourly.wind_gusts_10m, i) ?? 0;
  const windAt = (i) => numberAt(hourly.wind_speed_10m, i) ?? 0;
  const isBreezyAt = (i) => windAt(i) >= BREEZY_WIND_KMH || gustAt(i) >= BREEZY_GUST_KMH;
  const at = (i) => formatHour(times[i], cfg);

  // --- Temperature swings ---
  const tempKey = imperial ? 'imperial' : 'metric';
  const tempNow = numberAt(hourly.temperature_2m, nowI);
  let front = null;
  for (let i = nowI + 1; i <= outlookI && tempNow != null; i++) {
    const before = numberAt(hourly.temperature_2m, Math.max(nowI, i - 3));
    const after = numberAt(hourly.temperature_2m, i);
    if (before != null && after != null && before - after >= FRONT_DROP_3H[tempKey]) {
      front = { index: i, drop: Math.round(tempNow - after) };
      break;
    }
  }
  const highToday = numberAt(daily?.temperature_2m_max, 0);
  const highTomorrow = numberAt(daily?.temperature_2m_max, 1);
  let tomorrowSwing = null;
  if (highToday != null && highTomorrow != null) {
    const delta = Math.round(highTomorrow - highToday);
    if (Math.abs(delta) >= DAY_TO_DAY_SWING[tempKey]) {
      tomorrowSwing = { delta, high: Math.round(highTomorrow), today: Math.round(highToday) };
    }
  }

  // --- Wind outlook (what's coming; the compass already shows now) ---
  const gustBase = Math.max(gustNowKmh ?? 0, windKmh ?? 0);
  const breezyNow = (windKmh ?? 0) >= BREEZY_WIND_KMH || (gustNowKmh ?? 0) >= BREEZY_GUST_KMH;
  const windEaseI = breezyNow ? endIndexWhere(nowI, outlookI, isBreezyAt) : -1;
  let windOutlook;
  if (gustPeak.value != null && gustPeak.index > nowI && gustPeak.value >= gustBase + PICKING_UP_GUST_KMH) {
    windOutlook = { status: 'Picking up', detail: `Gusts to ${speed(gustPeak.value)} ${units.wind} by ${at(gustPeak.index)}` };
  } else if (windEaseI >= 0) {
    windOutlook = { status: 'Easing', detail: `Calmer by ${at(windEaseI)}` };
  } else {
    // Only mention gusts if something stronger than now is still to come;
    // the compass already shows the current gusts.
    const strongerLater = gustPeak.value != null && speed(gustPeak.value) > speed(gustBase);
    windOutlook = {
      status: 'Steady',
      detail: strongerLater ? `Gusts up to ${speed(gustPeak.value)} ${units.wind}` : 'No big change'
    };
  }

  // Headline: the single most important thing, in the order people care
  // about it. Things already happening say when they end ("until 8 PM");
  // things on the way say when they start ("by 4 PM"). The detail line
  // carries the runner-up so two alerts both get a sentence.
  const phrases = [];
  for (const alert of alerts) {
    if (alert.kind === 'thunder') {
      const endI = thunderI <= nowI ? endIndexWhere(nowI, outlookI, isThunder) : -1;
      if (thunderI > nowI) phrases.push(`Thunderstorms likely ${when(thunderI)}`);
      else phrases.push(endI >= 0 ? `Thunderstorms until ${at(endI)}` : 'Thunderstorms now');
    }
    if (alert.kind === 'gusts' || alert.kind === 'wind') {
      const word = alert.kind === 'gusts' ? 'Gusts' : 'Winds';
      const peak = alert.kind === 'gusts' ? aheadGust : aheadWind;
      const strongAt = alert.kind === 'gusts' ? (i) => gustAt(i) >= t.gustKmh : (i) => windAt(i) >= t.windKmh;
      const endI = peak.index <= nowI ? endIndexWhere(nowI, outlookI, strongAt) : -1;
      const tail = peak.index > nowI ? when(peak.index) : endI >= 0 ? `until ${at(endI)}` : 'now';
      phrases.push(`${word} up to ${speed(peak.value)} ${units.wind} ${tail}`);
    }
    if (alert.kind === 'precip') {
      const endI = heavyPrecipI <= nowI ? endIndexWhere(nowI, outlookI, isHeavy) : -1;
      if (heavyPrecipI > nowI) phrases.push(`${alert.label} likely ${when(heavyPrecipI)}`);
      else phrases.push(endI >= 0 ? `${alert.label} until ${at(endI)}` : `${alert.label} now`);
    }
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
    const laterThunderI = firstIndexWhere(nowI, outlookI, isThunder);
    const laterGustI = firstIndexWhere(nowI, outlookI, (i) => gustAt(i) >= t.gustKmh);
    const rainEndI = (precipNowMm ?? 0) >= MEASURABLE_PRECIP_MM ? endIndexWhere(nowI, outlookI, isWet) : -1;
    if ((precipNowMm ?? 0) >= MEASURABLE_PRECIP_MM) {
      if (rainEndI >= 0) {
        headline = `${precipWord} ending by ${at(rainEndI)}`;
        detail = `${intensity} ${precipWord.toLowerCase()} until then`;
      } else {
        headline = `${precipWord}ing now`;
        detail = `${intensity} ${precipWord.toLowerCase()} for the next ${CONDITIONS_OUTLOOK_HOURS} hours`;
      }
    } else if (laterThunderI >= 0) {
      headline = `Thunderstorms possible ${when(laterThunderI)}`;
      detail = precipSpell;
    } else if (front) {
      headline = `Turning much colder by ${at(front.index)}`;
      detail = `About ${front.drop}° colder by then`;
    } else if (laterGustI >= 0) {
      headline = 'Windy later';
      detail = `Gusts up to ${speed(gustPeak.value)} ${units.wind} ${when(gustPeak.index)}`;
    } else if (precipLikely) {
      headline = `${precipWord} likely ${wetI >= 0 ? when(wetI) : 'later'}`;
      detail = precipSpell;
    } else if (precipPossible) {
      headline = `Chance of ${precipWord.toLowerCase()} later`;
      detail = `${precipChance}% over the next ${CONDITIONS_OUTLOOK_HOURS} hours`;
    } else if (tomorrowSwing) {
      headline = `Much ${tomorrowSwing.delta < 0 ? 'colder' : 'warmer'} tomorrow`;
      detail = `High of ${tomorrowSwing.high}°, ${tomorrowSwing.today}° today`;
    } else if (pressureTrend === 'falling') {
      // Not alert-worthy, but the divider says "changing", so say why.
      headline = 'Dry for now';
      detail = 'Falling pressure can mean rain later';
    } else if (windEaseI >= 0) {
      headline = `Winds easing by ${at(windEaseI)}`;
      detail = 'Dry for the next 6 hours';
    } else {
      headline = breezyNow ? 'Breezy and dry' : 'Calm and dry';
      detail = `for the next ${CONDITIONS_OUTLOOK_HOURS} hours`;
    }
  }

  // Divider above the columns. "changing" needs something concrete in the
  // forecast; early hints alone (falling pressure, a low chance of rain) only
  // earn "may change", matching the hedged "Can mean ..." wording.
  const changing = alerts.length > 0
    || (precipNowMm ?? 0) >= MEASURABLE_PRECIP_MM || precipLikely
    || windOutlook.status !== 'Steady'
    || front != null || tomorrowSwing != null;
  const mayChange = !changing && (pressureTrend === 'falling' || precipPossible);
  const changeLevel = changing ? 'changing' : mayChange ? 'may-change' : 'steady';
  const changeLabel = {
    changing: 'Conditions changing',
    'may-change': 'Conditions may change',
    steady: 'Conditions steady'
  }[changeLevel];

  return {
    units,
    summary: {
      headline,
      detail,
      alert: alerts.length > 0,
      changing,
      changeLevel,
      changeLabel
    },
    wind: {
      speed: speed(windKmh),
      // Live speed (current_weather) and gusts (hourly) come from different
      // sources and can disagree; a gust at or below the wind speed is noise.
      gust: gustNowKmh != null && gustNowKmh > (windKmh ?? 0) ? speed(gustNowKmh) : null,
      direction: windDir,
      cardinal: toCardinal(windDir),
      from: windDir == null ? null : `from the ${toDirectionName(windDir)}`,
      peakGust: speed(gustPeak.value),
      peakGustTime: gustPeak.index >= 0 ? times[gustPeak.index] : null,
      outlook: windOutlook
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
