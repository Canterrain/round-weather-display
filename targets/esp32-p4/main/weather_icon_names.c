#include "weather_icon_names.h"

#include <ctype.h>
#include <string.h>

static const char *const WEATHER_ICON_NAMES[] = {
  "clear-day",
  "clear-night",
  "cloudy",
  "fog",
  "partlycloudy-day",
  "partlycloudy-night",
  "rain",
  "showers-day",
  "showers-night",
  "sleet",
  "snow",
  "thunderstorm",
  "thundersnow"
};

static bool strings_equal_ignore_case(const char *a, const char *b)
{
  if (a == NULL || b == NULL) {
    return false;
  }

  while (*a != '\0' && *b != '\0') {
    if (tolower((unsigned char) *a) != tolower((unsigned char) *b)) {
      return false;
    }
    a++;
    b++;
  }

  return *a == '\0' && *b == '\0';
}

const char *weather_icon_fallback_name(void)
{
  return "partlycloudy-day";
}

bool weather_icon_name_is_known(const char *icon_name)
{
  if (icon_name == NULL || icon_name[0] == '\0') {
    return false;
  }

  size_t icon_count = sizeof(WEATHER_ICON_NAMES) / sizeof(WEATHER_ICON_NAMES[0]);
  for (size_t i = 0; i < icon_count; ++i) {
    if (strings_equal_ignore_case(icon_name, WEATHER_ICON_NAMES[i])) {
      return true;
    }
  }

  return false;
}

const char *weather_icon_name_or_fallback(const char *icon_name)
{
  return weather_icon_name_is_known(icon_name) ? icon_name : weather_icon_fallback_name();
}

const char *weather_icon_name_for_conditions(int code, bool is_day, bool thundersnow)
{
  if (thundersnow) {
    return "thundersnow";
  }

  if (code == 0) {
    return is_day ? "clear-day" : "clear-night";
  }

  if (code == 1 || code == 2) {
    return is_day ? "partlycloudy-day" : "partlycloudy-night";
  }

  if (code == 3) {
    return "cloudy";
  }

  if (code == 45 || code == 48) {
    return "fog";
  }

  if (code >= 51 && code <= 57) {
    return is_day ? "showers-day" : "showers-night";
  }

  if (code >= 61 && code <= 65) {
    return "rain";
  }

  if (code == 66 || code == 67) {
    return "sleet";
  }

  if (code >= 71 && code <= 77) {
    return "snow";
  }

  if (code >= 80 && code <= 82) {
    return is_day ? "showers-day" : "showers-night";
  }

  if (code == 85 || code == 86) {
    return "snow";
  }

  if (code == 95 || code == 96 || code == 99) {
    return "thunderstorm";
  }

  return "cloudy";
}

const char *weather_icon_name_for_snapshot(const app_weather_snapshot_t *weather)
{
  if (weather == NULL || !weather->valid) {
    return weather_icon_fallback_name();
  }

  if (weather_icon_name_is_known(weather->icon_name)) {
    return weather->icon_name;
  }

  return weather_icon_name_for_conditions(weather->code, weather->is_day, weather->thundersnow);
}
