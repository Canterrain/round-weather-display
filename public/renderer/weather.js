const FALLBACK_WEATHER = {
  temp: 52,
  high: 61,
  low: 48,
  code: 2,
  is_day: true,
  thundersnow: false
};

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

async function fetchWeather() {
  try {
    const response = await fetch('/weather', { cache: 'no-store' });
    const data = await response.json();

    if (!response.ok || data.error || !data.current) {
      renderWeather(FALLBACK_WEATHER);
      return;
    }

    renderWeather(data.current);
  } catch (error) {
    console.error('Weather fetch failed:', error);
    renderWeather(FALLBACK_WEATHER);
  }
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

renderWeather(FALLBACK_WEATHER);
fetchWeather();
setInterval(fetchWeather, 10 * 60 * 1000);
