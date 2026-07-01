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
  MESSAGE: 'message'
};

let currentViewMode = VIEW_MODES.CLOCK;
let defaultHomeViewMode = VIEW_MODES.DIGITAL;
let lastHomeViewMode = VIEW_MODES.DIGITAL;

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

function setViewMode(mode) {
  const appShell = document.querySelector('.app-shell');
  const clockView = document.querySelector('.view-clock');
  const digitalView = document.querySelector('.view-digital');
  const forecastView = document.querySelector('.view-forecast');
  const messageView = document.querySelector('.view-message');
  if (!appShell || !clockView || !digitalView || !forecastView || !messageView) return;

  if (mode === VIEW_MODES.FORECAST) {
    currentViewMode = VIEW_MODES.FORECAST;
  } else if (mode === VIEW_MODES.DIGITAL) {
    currentViewMode = VIEW_MODES.DIGITAL;
  } else if (mode === VIEW_MODES.MESSAGE) {
    currentViewMode = VIEW_MODES.MESSAGE;
  } else {
    currentViewMode = VIEW_MODES.CLOCK;
  }

  if (currentViewMode === VIEW_MODES.CLOCK || currentViewMode === VIEW_MODES.DIGITAL) {
    lastHomeViewMode = currentViewMode;
  }

  appShell.classList.toggle('mode-clock', currentViewMode === VIEW_MODES.CLOCK);
  appShell.classList.toggle('mode-digital', currentViewMode === VIEW_MODES.DIGITAL);
  appShell.classList.toggle('mode-forecast', currentViewMode === VIEW_MODES.FORECAST);
  appShell.classList.toggle('mode-message', currentViewMode === VIEW_MODES.MESSAGE);
  clockView.setAttribute('aria-hidden', String(currentViewMode !== VIEW_MODES.CLOCK));
  digitalView.setAttribute('aria-hidden', String(currentViewMode !== VIEW_MODES.DIGITAL));
  forecastView.setAttribute('aria-hidden', String(currentViewMode !== VIEW_MODES.FORECAST));
  messageView.setAttribute('aria-hidden', String(currentViewMode !== VIEW_MODES.MESSAGE));
}

function setupSwipeNavigation() {
  const stage = document.querySelector('.clock-stage');
  if (!stage) return;

  let startX = null;
  let startY = null;

  function begin(x, y) {
    startX = x;
    startY = y;
  }

  function end(x, y) {
    if (startX == null || startY == null) return;

    const deltaX = x - startX;
    const deltaY = y - startY;
    const absX = Math.abs(deltaX);
    const absY = Math.abs(deltaY);

    if (absX >= 70 && absX > absY * 1.5) {
      if (currentViewMode === VIEW_MODES.CLOCK) {
        if (deltaX < 0) setViewMode(VIEW_MODES.FORECAST);
        if (deltaX > 0) {
          lastHomeViewMode = currentViewMode;
          setViewMode(VIEW_MODES.MESSAGE);
        }
      } else if (currentViewMode === VIEW_MODES.DIGITAL) {
        if (deltaX > 0) {
          lastHomeViewMode = currentViewMode;
          setViewMode(VIEW_MODES.MESSAGE);
        }
      } else if (currentViewMode === VIEW_MODES.FORECAST && deltaX > 0) {
        setViewMode(VIEW_MODES.CLOCK);
      } else if (currentViewMode === VIEW_MODES.MESSAGE && deltaX < 0) {
        setViewMode(lastHomeViewMode);
      }
    } else if (absY >= 70 && absY > absX * 1.5) {
      if (currentViewMode === VIEW_MODES.CLOCK && deltaY > 0) {
        setViewMode(VIEW_MODES.DIGITAL);
      } else if (currentViewMode === VIEW_MODES.DIGITAL && deltaY < 0) {
        setViewMode(VIEW_MODES.CLOCK);
      }
    }

    startX = null;
    startY = null;
  }

  stage.addEventListener('touchstart', (event) => {
    const touch = event.changedTouches[0];
    if (!touch) return;
    begin(touch.clientX, touch.clientY);
  }, { passive: true });

  stage.addEventListener('touchend', (event) => {
    const touch = event.changedTouches[0];
    if (!touch) return;
    end(touch.clientX, touch.clientY);
  }, { passive: true });

  stage.addEventListener('mousedown', (event) => {
    if (event.button !== 0) return;
    begin(event.clientX, event.clientY);
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

    defaultHomeViewMode = data.defaultClockFace === 'analog'
      ? VIEW_MODES.CLOCK
      : VIEW_MODES.DIGITAL;
    lastHomeViewMode = defaultHomeViewMode;
  } catch (error) {
    console.error('Failed to load app config:', error);
  }
}

window.appView = {
  VIEW_MODES,
  getCurrentViewMode: () => currentViewMode,
  getLastHomeViewMode: () => lastHomeViewMode,
  returnToLastHome: () => setViewMode(lastHomeViewMode),
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
      return;
    }

    renderWeather(data.current);
    renderForecast(data.forecast);
    renderDigitalWeather(data.current, data.forecast);

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
  setWeatherStatus('');
  setupSwipeNavigation();
  await fetchAppConfig();
  setViewMode(defaultHomeViewMode);
  fetchWeather();
  setInterval(fetchWeather, WEATHER_REFRESH_INTERVAL_MS);
}

initializeWeatherUi();
