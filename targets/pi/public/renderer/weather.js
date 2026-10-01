const FALLBACK_WEATHER = {
  temp: 52,
  high: 61,
  low: 48,
  code: 2,
  is_day: true,
  thundersnow: false
};
const WEATHER_REFRESH_INTERVAL_MS = 10 * 60 * 1000;
const VIEW_MODES = {
  CLOCK: 'clock',
  DIGITAL: 'digital',
  FORECAST: 'forecast',
  CONDITIONS: 'conditions',
  MESSAGE: 'message',
  SETTINGS: 'settings',
  WIFI: 'wifi'
};

let currentViewMode = VIEW_MODES.CLOCK;
let homeViewMode = VIEW_MODES.DIGITAL;
let weatherAlertActive = false;
let conditionsLoaded = false;
// The weather views to the left of home, nearest first. Snapshotted when
// leaving home so an alert that starts or clears mid-visit doesn't reshuffle
// the row under the user's finger -- the new order applies from the next
// swipe out of home.
let weatherRow = [VIEW_MODES.FORECAST, VIEW_MODES.CONDITIONS];

function currentWeatherRowOrder() {
  // During an alert, conditions jumps ahead of the forecast so one swipe
  // from home lands on it; the forecast is still one more swipe away.
  return weatherAlertActive
    ? [VIEW_MODES.CONDITIONS, VIEW_MODES.FORECAST]
    : [VIEW_MODES.FORECAST, VIEW_MODES.CONDITIONS];
}

function setWeatherStatus(message) {
  const el = document.getElementById('weather-status');
  if (!el) return;

  if (!message) {
    el.hidden = true;
    el.textContent = '';
    return;
  }

  el.hidden = false;
  el.textContent = message;
}

function formatAgeMs(ageMs) {
  if (!Number.isFinite(ageMs) || ageMs < 0) return '';

  const minuteMs = 60 * 1000;
  const hourMs = 60 * minuteMs;
  const dayMs = 24 * hourMs;

  if (ageMs >= dayMs) return `${Math.round(ageMs / dayMs)}d ago`;
  if (ageMs >= hourMs) return `${Math.round(ageMs / hourMs)}h ago`;

  const minutes = Math.max(1, Math.round(ageMs / minuteMs));
  return `${minutes}m ago`;
}

function getHomeFallbackForecastItems() {
  return Array.from({ length: 5 }, () => FALLBACK_WEATHER);
}

function renderWeather(current) {
  const icon = document.getElementById('current-icon');
  const temp = document.getElementById('current-temp');
  const highLow = document.getElementById('high-low');

  if (!icon || !temp || !highLow) return;

  const iconKey = mapIconFromMeteo(current.code, current.is_day, current.thundersnow);

  icon.src = `assets/icons/${iconKey}.svg`;
  temp.textContent = `${current.temp}°`;
  highLow.textContent = `H:${current.high}°  L:${current.low}°`;
}

function renderDigitalWeather(current, forecast) {
  const tempEl = document.getElementById('digital-current-temp');
  const iconEl = document.getElementById('digital-current-icon');
  const summaryEl = document.getElementById('digital-current-summary');
  const highLowEl = document.getElementById('digital-current-high-low');
  const forecastList = document.getElementById('digital-forecast-list');
  if (!tempEl || !iconEl || !summaryEl || !highLowEl || !forecastList) return;

  const safeCurrent = current || FALLBACK_WEATHER;
  const items = Array.isArray(forecast) && forecast.length > 0
    ? forecast.slice(0, 5)
    : getHomeFallbackForecastItems();

  const currentIconKey = mapIconFromMeteo(safeCurrent.code, safeCurrent.is_day, safeCurrent.thundersnow);
  tempEl.textContent = `${safeCurrent.temp}°`;
  iconEl.src = `assets/icons/${currentIconKey}.svg`;
  summaryEl.textContent = describeMeteo(safeCurrent.code, safeCurrent.is_day, safeCurrent.thundersnow);
  highLowEl.textContent = `H:${safeCurrent.high}°  L:${safeCurrent.low}°`;

  forecastList.replaceChildren();

  items.forEach((item, index) => {
    const iconKey = mapIconFromMeteo(item.code, item.is_day, item.thundersnow);
    const card = document.createElement('div');
    card.className = 'digital-forecast-item';
    card.innerHTML = `
      <div class="digital-forecast-day">${getForecastDayLabel(index + 1).toUpperCase()}</div>
      <img class="digital-forecast-icon" src="assets/icons/${iconKey}.svg" alt="Forecast icon for ${getForecastDayLabel(index + 1)}"/>
      <div class="digital-forecast-temp">${item.high}°</div>
    `;
    forecastList.appendChild(card);
  });
}

function getForecastDayLabel(offsetFromToday) {
  const date = new Date();
  date.setDate(date.getDate() + offsetFromToday);
  return date.toLocaleDateString('en-US', { weekday: 'short' });
}

function renderForecast(forecast) {
  const forecastList = document.getElementById('forecast-list');
  const tomorrowIcon = document.getElementById('forecast-tomorrow-icon');
  const tomorrowTemps = document.getElementById('forecast-tomorrow-temps');
  if (!forecastList || !tomorrowIcon || !tomorrowTemps) return;

  const items = Array.isArray(forecast) && forecast.length > 0
    ? forecast.slice(0, 5)
    : Array.from({ length: 5 }, () => FALLBACK_WEATHER);

  const tomorrow = items[0] || FALLBACK_WEATHER;
  const tomorrowIconKey = mapIconFromMeteo(tomorrow.code, tomorrow.is_day, tomorrow.thundersnow);
  tomorrowIcon.src = `assets/icons/${tomorrowIconKey}.svg`;
  tomorrowTemps.textContent = `${tomorrow.high}° / ${tomorrow.low}°`;

  forecastList.replaceChildren();

  items.slice(1, 5).forEach((item, index) => {
    const row = document.createElement('div');
    const iconKey = mapIconFromMeteo(item.code, item.is_day, item.thundersnow);
    row.className = 'forecast-row';
    row.innerHTML = `
      <div class="forecast-day">${getForecastDayLabel(index + 2)}</div>
      <img class="forecast-icon" src="assets/icons/${iconKey}.svg" alt="Forecast icon for ${getForecastDayLabel(index + 2)}"/>
      <div class="forecast-temps">${item.high}° / ${item.low}°</div>
    `;
    forecastList.appendChild(row);
  });
}

const VIEW_LAYER_SELECTORS = {
  [VIEW_MODES.CLOCK]: '.view-clock',
  [VIEW_MODES.DIGITAL]: '.view-digital',
  [VIEW_MODES.FORECAST]: '.view-forecast',
  [VIEW_MODES.CONDITIONS]: '.view-conditions',
  [VIEW_MODES.MESSAGE]: '.view-message',
  [VIEW_MODES.SETTINGS]: '.view-settings',
  [VIEW_MODES.WIFI]: '.view-wifi'
};

function buildConditionsDialTicks() {
  const group = document.getElementById('conditions-dial-ticks');
  if (!group || group.childElementCount > 0) return;

  const ns = 'http://www.w3.org/2000/svg';
  for (let deg = 0; deg < 360; deg += 15) {
    const major = deg % 90 === 0;
    // Cardinal letters sit where the major ticks would be.
    if (major) continue;
    const rad = (deg * Math.PI) / 180;
    const outer = 172;
    const inner = deg % 45 === 0 ? 156 : 164;
    const line = document.createElementNS(ns, 'line');
    line.setAttribute('x1', String(200 + outer * Math.sin(rad)));
    line.setAttribute('y1', String(200 - outer * Math.cos(rad)));
    line.setAttribute('x2', String(200 + inner * Math.sin(rad)));
    line.setAttribute('y2', String(200 - inner * Math.cos(rad)));
    line.setAttribute('class', deg % 45 === 0 ? 'conditions-dial-tick major' : 'conditions-dial-tick');
    group.appendChild(line);
  }
}

// Step the font down until the text fits its box on one line; wrap only as
// a last resort. Measured rather than guessed from length because the Pi's
// fallback font is noticeably wider than Avenir Next.
function fitTextToWidth(el, maxPx, minPx) {
  el.classList.remove('is-wrapped');
  let size = maxPx;
  el.style.fontSize = `${size}px`;
  while (el.scrollWidth > el.clientWidth && size > minPx) {
    size -= 1;
    el.style.fontSize = `${size}px`;
  }
  if (el.scrollWidth > el.clientWidth) el.classList.add('is-wrapped');
}

// Curved-text version: shrink the font until the text spans no more than
// maxLength px along its arc, which keeps it near the top of the circle.
function fitTextToArc(textPathEl, maxPx, minPx, maxLength) {
  const textEl = textPathEl?.parentNode;
  if (!textEl || typeof textEl.getComputedTextLength !== 'function') return;
  let size = maxPx;
  textEl.style.fontSize = `${size}px`;
  while (textEl.getComputedTextLength() > maxLength && size > minPx) {
    size -= 1;
    textEl.style.fontSize = `${size}px`;
  }
}

function setText(id, text) {
  const el = document.getElementById(id);
  if (el) el.textContent = text;
}

// The wording (headline, pressure/rain descriptions) is written by
// shared/logic/storm-conditions.js so every target says the same thing;
// this only places it and applies the alert styling.
function renderConditions(conditions) {
  const face = document.querySelector('.conditions-face');
  if (!face) return;
  buildConditionsDialTicks();
  if (conditions) conditionsLoaded = true;

  const alerts = Array.isArray(conditions?.alerts) ? conditions.alerts : [];
  const alertKinds = new Set(alerts.map((alert) => alert.kind));

  weatherAlertActive = alerts.length > 0;
  document.querySelectorAll('[data-weather-edge-indicator]').forEach((edge) => {
    edge.classList.toggle('has-alert', weatherAlertActive);
  });

  const units = conditions?.units || {};
  const summary = conditions?.summary || {};
  const wind = conditions?.wind || {};
  const pressure = conditions?.pressure || {};
  const precip = conditions?.precip || {};
  const windAlert = alertKinds.has('gusts') || alertKinds.has('wind');
  face.classList.toggle('has-alert', weatherAlertActive);
  face.classList.toggle('has-wind-alert', windAlert);
  face.classList.toggle('has-change', summary.changing === true);

  const headlineEl = document.getElementById('conditions-headline');
  if (headlineEl) {
    headlineEl.textContent = summary.headline || 'Weather unavailable';
    fitTextToArc(headlineEl, 32, 22, 600);
  }
  const detailEl = document.getElementById('conditions-detail');
  if (detailEl) {
    detailEl.textContent = summary.detail || '';
    fitTextToArc(detailEl, 19, 15, 500);
  }
  setText('conditions-change-label', summary.changeLabel || '\u00a0');

  setText('conditions-wind-speed', Number.isFinite(wind.speed) ? String(wind.speed) : '--');
  setText('conditions-wind-unit', units.wind || '');
  setText('conditions-wind-from', wind.from || '\u00a0');

  const gustEl = document.getElementById('conditions-wind-gust');
  if (gustEl) {
    gustEl.textContent = Number.isFinite(wind.gust) ? `gusts to ${wind.gust} ${units.wind || ''}`.trim() : '\u00a0';
    gustEl.classList.toggle('is-alert', windAlert);
  }
  fitTextToWidth(document.getElementById('conditions-wind-from'), 22, 16);
  if (gustEl) fitTextToWidth(gustEl, 22, 16);

  const flow = document.getElementById('conditions-wind-flow');
  if (flow) {
    const hasDirection = Number.isFinite(wind.direction);
    // SVG elements ignore the `hidden` attribute, so toggle display instead.
    flow.style.display = hasDirection ? '' : 'none';
    if (hasDirection) flow.setAttribute('transform', `rotate(${wind.direction} 200 200)`);
  }

  // Columns are a glance-able summary: one word or two per column. The
  // explanation belongs in the headline, which says what matters when it
  // matters.
  const fit = (id, maxPx, minPx) => fitTextToWidth(document.getElementById(id), maxPx, minPx);
  const outlook = wind.outlook || {};
  setText('conditions-wind-outlook-status', outlook.status || '--');
  fit('conditions-wind-outlook-status', 27, 18);
  document.querySelector('.conditions-col-wind')?.classList.toggle('is-alert', windAlert);

  setText('conditions-pressure-status', pressure.status || '--');
  fit('conditions-pressure-status', 27, 18);
  document.querySelector('.conditions-col-pressure')?.classList.toggle('is-alert', alertKinds.has('pressure'));

  const isSnow = precip.label === 'Snow';
  setText('conditions-precip-label', precip.label || 'Rain');
  setText('conditions-precip-status', precip.status || '--');
  document.getElementById('conditions-precip-icon-rain')?.style.setProperty('display', isSnow ? 'none' : '');
  document.getElementById('conditions-precip-icon-snow')?.style.setProperty('display', isSnow ? '' : 'none');
  fit('conditions-precip-status', 27, 18);
  document.querySelector('.conditions-col-precip')?.classList.toggle('is-alert', alertKinds.has('precip') || alertKinds.has('thunder'));
}

function setViewMode(mode) {
  const appShell = document.querySelector('.app-shell');
  if (!appShell) return;

  currentViewMode = Object.prototype.hasOwnProperty.call(VIEW_LAYER_SELECTORS, mode)
    ? mode
    : VIEW_MODES.CLOCK;

  for (const [viewMode, selector] of Object.entries(VIEW_LAYER_SELECTORS)) {
    appShell.classList.toggle(`mode-${viewMode}`, currentViewMode === viewMode);
    document.querySelector(selector)?.setAttribute('aria-hidden', String(currentViewMode !== viewMode));
  }
}

function setupSwipeNavigation() {
  const stage = document.querySelector('.clock-stage');
  if (!stage) return;

  let startX = null;
  let startY = null;

  // Swipes that start on a [data-swipe-exempt] element (scrolling lists,
  // on-screen keyboards, sliders) belong to that element, not navigation --
  // otherwise scrolling the WiFi network list would bounce you back home.
  function begin(x, y, target) {
    if (target instanceof Element && target.closest('[data-swipe-exempt]')) {
      startX = null;
      startY = null;
      return;
    }
    startX = x;
    startY = y;
  }

  function end(x, y) {
    if (startX == null || startY == null) return;

    const deltaX = x - startX;
    const deltaY = y - startY;
    const absX = Math.abs(deltaX);
    const absY = Math.abs(deltaY);

    const isHome = currentViewMode === VIEW_MODES.CLOCK || currentViewMode === VIEW_MODES.DIGITAL;

    // Both clock faces share one set of gestures -- only the face picked at
    // setup is reachable, so there's no analog/digital swipe anymore.
    // Every screen is a fixed neighbour of home and returns with the
    // opposite swipe:
    //   right -> messages
    //   left  -> forecast -> conditions (conditions first during an alert)
    //   down  -> Settings (sits above home; Wi-Fi is a page within it)
    // Swipe up from home is intentionally unused (reserved for a future view
    // below home).
    const rowIndex = weatherRow.indexOf(currentViewMode);
    if (absX >= 70 && absX > absY * 1.5) {
      if (isHome) {
        if (deltaX < 0) {
          weatherRow = currentWeatherRowOrder();
          setViewMode(weatherRow[0]);
        }
        if (deltaX > 0) setViewMode(VIEW_MODES.MESSAGE);
      } else if (rowIndex >= 0) {
        if (deltaX < 0 && rowIndex < weatherRow.length - 1) setViewMode(weatherRow[rowIndex + 1]);
        if (deltaX > 0) setViewMode(rowIndex === 0 ? homeViewMode : weatherRow[rowIndex - 1]);
      } else if (currentViewMode === VIEW_MODES.MESSAGE && deltaX < 0) {
        setViewMode(homeViewMode);
      }
    } else if (absY >= 70 && absY > absX * 1.5) {
      if (isHome && deltaY > 0) {
        setViewMode(VIEW_MODES.SETTINGS);
      } else if ((currentViewMode === VIEW_MODES.SETTINGS || currentViewMode === VIEW_MODES.WIFI) && deltaY < 0) {
        setViewMode(homeViewMode);
      }
    }

    startX = null;
    startY = null;
  }

  stage.addEventListener('touchstart', (event) => {
    const touch = event.changedTouches[0];
    if (!touch) return;
    begin(touch.clientX, touch.clientY, event.target);
  }, { passive: true });

  stage.addEventListener('touchend', (event) => {
    const touch = event.changedTouches[0];
    if (!touch) return;
    end(touch.clientX, touch.clientY);
  }, { passive: true });

  stage.addEventListener('mousedown', (event) => {
    if (event.button !== 0) return;
    begin(event.clientX, event.clientY, event.target);
  });

  stage.addEventListener('mouseup', (event) => {
    if (event.button !== 0) return;
    end(event.clientX, event.clientY);
  });
}

async function fetchAppConfig() {
  try {
    const response = await fetch('/config', { cache: 'no-store' });
    const data = await response.json();
    if (!response.ok || data.error) return;

    homeViewMode = data.defaultClockFace === 'analog'
      ? VIEW_MODES.CLOCK
      : VIEW_MODES.DIGITAL;
  } catch (error) {
    console.error('Failed to load app config:', error);
  }
}

window.appView = {
  VIEW_MODES,
  getCurrentViewMode: () => currentViewMode,
  returnToHome: () => setViewMode(homeViewMode),
  setViewMode
};

async function fetchWeather() {
  try {
    const response = await fetch('/weather', { cache: 'no-store' });
    const data = await response.json();

    if (!response.ok || data.error || !data.current) {
      if (data?.error) {
        console.error('Weather fetch error:', data.error);
      }
      setWeatherStatus('Weather data stale');
      if (!conditionsLoaded) renderConditions(null);
      return;
    }

    renderWeather(data.current);
    renderForecast(data.forecast);
    renderDigitalWeather(data.current, data.forecast);
    renderConditions(data.conditions);

    if (data.stale) {
      const updatedAtMs = Date.parse(data.updatedAt);
      const derivedAgeMs = Number.isFinite(updatedAtMs) ? Date.now() - updatedAtMs : data.staleAgeMs;
      const ageLabel = formatAgeMs(derivedAgeMs);
      setWeatherStatus(ageLabel ? `Weather updated ${ageLabel}` : 'Weather data stale');
    } else {
      setWeatherStatus('');
    }
  } catch (error) {
    console.error('Weather fetch failed:', error);
    setWeatherStatus('Weather data stale');
    if (!conditionsLoaded) renderConditions(null);
  }
}

function describeMeteo(code, isDay, thundersnow) {
  if (thundersnow) return 'Thundersnow';
  if (code === 0) return 'Clear';
  if (code === 1) return isDay ? 'Mostly Sunny' : 'Mostly Clear';
  if (code === 2) return isDay ? 'Partly Cloudy' : 'Partly Cloudy';
  if (code === 3) return 'Cloudy';
  if (code === 45 || code === 48) return 'Foggy';
  if (code >= 51 && code <= 57) return 'Drizzle';
  if (code >= 61 && code <= 65) return 'Rain';
  if (code === 66 || code === 67) return 'Sleet';
  if (code >= 71 && code <= 77) return 'Snow';
  if (code >= 80 && code <= 82) return 'Showers';
  if (code === 85 || code === 86) return 'Snow Showers';
  if (code === 95 || code === 96 || code === 99) return 'Thunderstorm';
  return 'Cloudy';
}

// Map Open-Meteo weathercode (+ is_day) to your icon filenames.
// Your existing names used below:
// clear-day, clear-night, partlycloudy-day, partlycloudy-night, cloudy,
// fog, rain, showers-day, showers-night, sleet, snow, thunderstorm, thundersnow
function mapIconFromMeteo(code, isDay, thundersnow) {
  // If we inferred thundersnow, override everything.
  if (thundersnow) return "thundersnow";

  // 0: Clear sky
  if (code === 0) return isDay ? "clear-day" : "clear-night";

  // 1-2: Mainly clear, partly cloudy
  if (code === 1 || code === 2) return isDay ? "partlycloudy-day" : "partlycloudy-night";

  // 3: Overcast
  if (code === 3) return "cloudy";

  // 45,48: Fog / depositing rime fog
  if (code === 45 || code === 48) return "fog";

  // 51-57: Drizzle (incl freezing drizzle) -> treat as showers
  if (code >= 51 && code <= 57) return isDay ? "showers-day" : "showers-night";

  // 61-65: Rain
  if (code >= 61 && code <= 65) return "rain";

  // 66-67: Freezing rain -> sleet icon (closest you have)
  if (code === 66 || code === 67) return "sleet";

  // 71-77: Snow fall / snow grains
  if (code >= 71 && code <= 77) return "snow";

  // 80-82: Rain showers
  if (code >= 80 && code <= 82) return isDay ? "showers-day" : "showers-night";

  // 85-86: Snow showers
  if (code === 85 || code === 86) return "snow";

  // 95: Thunderstorm (slight/moderate)
  // 96-99: Thunderstorm with hail
  if (code === 95 || code === 96 || code === 99) return "thunderstorm";

  return "cloudy";
}

async function initializeWeatherUi() {
  renderWeather(FALLBACK_WEATHER);
  renderForecast(null);
  renderDigitalWeather(FALLBACK_WEATHER, null);
  // Leave the "Checking the weather…" placeholder until the first fetch.
  buildConditionsDialTicks();
  setWeatherStatus('');
  setupSwipeNavigation();
  await fetchAppConfig();
  setViewMode(homeViewMode);
  fetchWeather();
  setInterval(fetchWeather, WEATHER_REFRESH_INTERVAL_MS);
}

initializeWeatherUi();
