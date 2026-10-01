#include "storm_conditions.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Constants mirror shared/logic/storm-conditions.js one for one. */
#define CONDITIONS_LOOKAHEAD_HOURS 3
#define CONDITIONS_OUTLOOK_HOURS 6
#define PRESSURE_TREND_HOURS 3
/* Pressure naturally swings about 1 hPa twice a day (the atmospheric tide),
 * so anything inside this band reads as steady rather than as weather. */
#define PRESSURE_STEADY_HPA 1.5

#define KMH_PER_MPH 1.609344
#define HPA_PER_INHG 33.8639
#define MM_PER_INCH 25.4

#define MEASURABLE_PRECIP_MM 0.25
#define LIKELY_CHANCE 50.0
#define POSSIBLE_CHANCE 20.0
#define BREEZY_WIND_KMH 32.0
#define BREEZY_GUST_KMH 48.0
#define MODERATE_PRECIP_MM_HR 2.5
#define PICKING_UP_GUST_KMH 16.0
#define FRONT_DROP_3H_IMPERIAL 15.0
#define FRONT_DROP_3H_METRIC 8.0
#define DAY_TO_DAY_SWING_IMPERIAL 15.0
#define DAY_TO_DAY_SWING_METRIC 8.0

static const char *const DIRECTION_NAMES[8] = {
  "north", "northeast", "east", "southeast", "south", "southwest", "west", "northwest"
};

static const char *const CARDINALS[16] = {
  "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"
};

/* A value that may be missing -- the JS uses null for this. */
typedef struct {
  bool ok;
  double value;
} opt_t;

typedef struct {
  bool ok;
  double value;
  int index;
} max_result_t;

typedef struct {
  double gust_kmh;
  double wind_kmh;
  double pressure_drop_hpa;
  double heavy_precip_mm_hr;
} thresholds_t;

typedef struct {
  const cJSON *hourly;
  const thresholds_t *t;
} predicate_ctx_t;

typedef bool (*hour_predicate_t)(const predicate_ctx_t *ctx, int i);

/* JS Math.round: halves round up (toward +infinity), unlike C's round(). */
static double js_round(double value)
{
  return floor(value + 0.5);
}

/* Number(v.toFixed(places)) */
static double round_places(double value, int places)
{
  char buffer[48];
  snprintf(buffer, sizeof(buffer), "%.*f", places, value);
  return strtod(buffer, NULL);
}

/* How JS prints a number in a template literal, for the values we produce
 * (integers and short decimals): "8" not "8.0", "0.39" not "0.390000". */
static void format_js_number(double value, char *out, size_t out_size)
{
  if (value == 0.0) {
    value = 0.0; /* no "-0" */
  }
  snprintf(out, out_size, "%.15g", value);
}

static const cJSON *field(const cJSON *object, const char *key)
{
  return object != NULL ? cJSON_GetObjectItemCaseSensitive((cJSON *) object, key) : NULL;
}

static opt_t number_at(const cJSON *array, int i)
{
  opt_t result = {false, 0.0};
  if (!cJSON_IsArray(array) || i < 0 || i >= cJSON_GetArraySize(array)) {
    return result;
  }
  const cJSON *item = cJSON_GetArrayItem((cJSON *) array, i);
  if (!cJSON_IsNumber(item)) {
    return result;
  }
  double value = cJSON_GetNumberValue(item);
  if (!isfinite(value)) {
    return result;
  }
  result.ok = true;
  result.value = value;
  return result;
}

static double or_zero(opt_t value)
{
  return value.ok ? value.value : 0.0;
}

static max_result_t max_over(const cJSON *array, int from_i, int to_i)
{
  max_result_t best = {false, 0.0, -1};
  for (int i = from_i; i <= to_i; ++i) {
    opt_t value = number_at(array, i);
    if (value.ok && (!best.ok || value.value > best.value)) {
      best.ok = true;
      best.value = value.value;
      best.index = i;
    }
  }
  return best;
}

static opt_t sum_over(const cJSON *array, int from_i, int to_i)
{
  opt_t total = {false, 0.0};
  for (int i = from_i; i <= to_i; ++i) {
    opt_t value = number_at(array, i);
    if (value.ok) {
      total.ok = true;
      total.value += value.value;
    }
  }
  return total;
}

static int count_where(const predicate_ctx_t *ctx, int from_i, int to_i, hour_predicate_t predicate)
{
  int count = 0;
  for (int i = from_i; i <= to_i; ++i) {
    if (predicate(ctx, i)) {
      count++;
    }
  }
  return count;
}

/* For something happening at from_i: the first later hour from which it has
 * stopped for the rest of the window, or -1 if it carries on to the end. */
static int end_index_where(const predicate_ctx_t *ctx, int from_i, int to_i, hour_predicate_t predicate)
{
  int end = -1;
  for (int i = to_i; i > from_i; --i) {
    if (predicate(ctx, i)) {
      break;
    }
    end = i;
  }
  return end;
}

static int first_index_where(const predicate_ctx_t *ctx, int from_i, int to_i, hour_predicate_t predicate)
{
  for (int i = from_i; i <= to_i; ++i) {
    if (predicate(ctx, i)) {
      return i;
    }
  }
  return -1;
}

static bool is_thunder_code(opt_t code)
{
  return code.ok && (code.value == 95 || code.value == 96 || code.value == 99);
}

static double hourly_at(const predicate_ctx_t *ctx, const char *key, int i)
{
  return or_zero(number_at(field(ctx->hourly, key), i));
}

static bool pred_thunder(const predicate_ctx_t *ctx, int i)
{
  return is_thunder_code(number_at(field(ctx->hourly, "weathercode"), i));
}

static bool pred_wet(const predicate_ctx_t *ctx, int i)
{
  return hourly_at(ctx, "precipitation", i) >= MEASURABLE_PRECIP_MM;
}

static bool pred_heavy(const predicate_ctx_t *ctx, int i)
{
  return hourly_at(ctx, "precipitation", i) >= ctx->t->heavy_precip_mm_hr;
}

static bool pred_breezy(const predicate_ctx_t *ctx, int i)
{
  return hourly_at(ctx, "wind_speed_10m", i) >= BREEZY_WIND_KMH
    || hourly_at(ctx, "wind_gusts_10m", i) >= BREEZY_GUST_KMH;
}

static bool pred_gust_strong(const predicate_ctx_t *ctx, int i)
{
  return hourly_at(ctx, "wind_gusts_10m", i) >= ctx->t->gust_kmh;
}

static bool pred_wind_strong(const predicate_ctx_t *ctx, int i)
{
  return hourly_at(ctx, "wind_speed_10m", i) >= ctx->t->wind_kmh;
}

static void to_lower_copy(const char *src, char *out, size_t out_size)
{
  size_t i = 0;
  for (; src[i] != '\0' && i + 1 < out_size; ++i) {
    out[i] = (char) tolower((unsigned char) src[i]);
  }
  out[i] = '\0';
}

static double normalize_degrees(double deg)
{
  return fmod(fmod(deg, 360.0) + 360.0, 360.0);
}

/* "6 PM" / "18:00" from an Open-Meteo local time like "2026-03-14T18:00". */
static void format_hour(const cJSON *times, int i, bool time_24h, char *out, size_t out_size)
{
  out[0] = '\0';
  const cJSON *item = cJSON_IsArray(times) ? cJSON_GetArrayItem((cJSON *) times, i) : NULL;
  const char *iso = cJSON_IsString(item) ? cJSON_GetStringValue(item) : NULL;
  if (iso == NULL || strlen(iso) < 13 || !isdigit((unsigned char) iso[11]) || !isdigit((unsigned char) iso[12])) {
    return;
  }

  int hour = (iso[11] - '0') * 10 + (iso[12] - '0');
  if (time_24h) {
    snprintf(out, out_size, "%02d:00", hour);
    return;
  }
  int h12 = hour % 12 == 0 ? 12 : hour % 12;
  snprintf(out, out_size, "%d %s", h12, hour < 12 ? "AM" : "PM");
}

/* The hourly row for the hour we're currently in (current_weather.time is at
 * 15-minute resolution, e.g. "...T15:30" belongs to the "...T15:00" row). */
static int current_hour_index(const cJSON *times, const char *now_iso)
{
  if (!cJSON_IsArray(times) || now_iso == NULL || strlen(now_iso) < 13) {
    return -1;
  }

  char wanted[20];
  snprintf(wanted, sizeof(wanted), "%.13s:00", now_iso);
  int index = 0;
  const cJSON *item = NULL;
  cJSON_ArrayForEach(item, times) {
    if (cJSON_IsString(item) && strcmp(cJSON_GetStringValue(item), wanted) == 0) {
      return index;
    }
    index++;
  }
  return -1;
}

static thresholds_t storm_thresholds(const storm_conditions_config_t *config)
{
  bool imperial = !config->metric;
  thresholds_t t;
  t.gust_kmh = imperial
    ? (isfinite(config->gust_override) ? config->gust_override : 40.0) * KMH_PER_MPH
    : (isfinite(config->gust_override) ? config->gust_override : 64.0);
  t.wind_kmh = imperial
    ? (isfinite(config->wind_override) ? config->wind_override : 25.0) * KMH_PER_MPH
    : (isfinite(config->wind_override) ? config->wind_override : 40.0);
  t.pressure_drop_hpa = isfinite(config->pressure_drop_hpa_override) ? config->pressure_drop_hpa_override : 3.0;
  t.heavy_precip_mm_hr = isfinite(config->precip_mm_hr_override) ? config->precip_mm_hr_override : 7.6;
  return t;
}

void storm_conditions_default_config(storm_conditions_config_t *config)
{
  if (config == NULL) {
    return;
  }
  memset(config, 0, sizeof(*config));
  config->gust_override = NAN;
  config->wind_override = NAN;
  config->pressure_drop_hpa_override = NAN;
  config->precip_mm_hr_override = NAN;
}

const char *storm_alert_kind_name(storm_alert_kind_t kind)
{
  switch (kind) {
    case STORM_ALERT_THUNDER: return "thunder";
    case STORM_ALERT_GUSTS: return "gusts";
    case STORM_ALERT_WIND: return "wind";
    case STORM_ALERT_PRESSURE: return "pressure";
    case STORM_ALERT_PRECIP: return "precip";
  }
  return "";
}

const char *storm_change_level_name(storm_change_level_t level)
{
  switch (level) {
    case STORM_CHANGE_CHANGING: return "changing";
    case STORM_CHANGE_MAY_CHANGE: return "may-change";
    case STORM_CHANGE_STEADY: return "steady";
  }
  return "";
}

static void add_alert(storm_conditions_t *out, storm_alert_kind_t kind, const char *label)
{
  if (out->alert_count >= STORM_MAX_ALERTS) {
    return;
  }
  out->alerts[out->alert_count].kind = kind;
  snprintf(out->alerts[out->alert_count].label, sizeof(out->alerts[0].label), "%s", label);
  out->alert_count++;
}

bool storm_conditions_build(
  const storm_conditions_config_t *config,
  const cJSON *current,
  const cJSON *hourly,
  const cJSON *daily,
  storm_conditions_t *out
)
{
  if (config == NULL || out == NULL) {
    return false;
  }
  memset(out, 0, sizeof(*out));

  bool imperial = !config->metric;
  const cJSON *times = field(hourly, "time");
  const cJSON *now_time = field(current, "time");
  int now_i = current_hour_index(times, cJSON_IsString(now_time) ? cJSON_GetStringValue(now_time) : NULL);
  if (now_i < 0) {
    return false;
  }

  int last_i = cJSON_GetArraySize((cJSON *) times) - 1;
  int ahead_i = now_i + CONDITIONS_LOOKAHEAD_HOURS < last_i ? now_i + CONDITIONS_LOOKAHEAD_HOURS : last_i;
  int outlook_i = now_i + CONDITIONS_OUTLOOK_HOURS < last_i ? now_i + CONDITIONS_OUTLOOK_HOURS : last_i;

  const cJSON *gusts = field(hourly, "wind_gusts_10m");
  const cJSON *winds = field(hourly, "wind_speed_10m");
  const cJSON *pressures = field(hourly, "pressure_msl");
  const cJSON *precipitation = field(hourly, "precipitation");

  /* Wind: live speed/direction from current_weather, gusts from the current
   * hourly row since current_weather doesn't carry them. */
  const cJSON *live_speed = field(current, "windspeed");
  const cJSON *live_dir = field(current, "winddirection");
  opt_t wind_kmh = cJSON_IsNumber(live_speed)
    ? (opt_t) {true, cJSON_GetNumberValue(live_speed)}
    : number_at(winds, now_i);
  opt_t wind_dir = cJSON_IsNumber(live_dir) ? (opt_t) {true, cJSON_GetNumberValue(live_dir)} : (opt_t) {false, 0.0};
  opt_t gust_now_kmh = number_at(gusts, now_i);
  max_result_t gust_peak = max_over(gusts, now_i, outlook_i);

  /* Pressure tendency over 3h; before 03:00 there's no "3h ago" row, so fall
   * back to the forecast change over the next 3h. */
  opt_t pressure_now = number_at(pressures, now_i);
  opt_t pressure_change = {false, 0.0};
  opt_t past_p = number_at(pressures, now_i - PRESSURE_TREND_HOURS);
  if (pressure_now.ok && past_p.ok) {
    pressure_change = (opt_t) {true, pressure_now.value - past_p.value};
  } else {
    opt_t future_p = number_at(pressures, now_i + PRESSURE_TREND_HOURS);
    if (pressure_now.ok && future_p.ok) {
      pressure_change = (opt_t) {true, future_p.value - pressure_now.value};
    }
  }
  const char *pressure_trend = NULL;
  if (pressure_change.ok) {
    if (pressure_change.value <= -PRESSURE_STEADY_HPA) {
      pressure_trend = "falling";
    } else if (pressure_change.value >= PRESSURE_STEADY_HPA) {
      pressure_trend = "rising";
    } else {
      pressure_trend = "steady";
    }
  }
  bool pressure_falling = pressure_trend != NULL && strcmp(pressure_trend, "falling") == 0;

  opt_t precip_now_mm = number_at(precipitation, now_i);
  opt_t precip_outlook_mm = sum_over(precipitation, now_i, outlook_i);
  max_result_t chance_max = max_over(field(hourly, "precipitation_probability"), now_i, outlook_i);

  /* Alerts look at now through the next few hours. */
  thresholds_t t = storm_thresholds(config);
  predicate_ctx_t ctx = {hourly, &t};
  max_result_t ahead_gust = max_over(gusts, now_i, ahead_i);
  max_result_t ahead_wind_hourly = max_over(winds, now_i, ahead_i);
  max_result_t ahead_wind = or_zero(wind_kmh) >= (ahead_wind_hourly.ok ? ahead_wind_hourly.value : 0.0)
    ? (max_result_t) {wind_kmh.ok, wind_kmh.value, now_i}
    : ahead_wind_hourly;
  max_result_t ahead_precip = max_over(precipitation, now_i, ahead_i);
  max_result_t ahead_snow = max_over(field(hourly, "snowfall"), now_i, ahead_i);
  int thunder_i = first_index_where(&ctx, now_i, ahead_i, pred_thunder);
  int heavy_precip_i = first_index_where(&ctx, now_i, ahead_i, pred_heavy);

  if (thunder_i >= 0) {
    add_alert(out, STORM_ALERT_THUNDER, "Thunderstorms");
  }
  if (ahead_gust.ok && ahead_gust.value >= t.gust_kmh) {
    add_alert(out, STORM_ALERT_GUSTS, "Strong gusts");
  } else if (ahead_wind.ok && ahead_wind.value >= t.wind_kmh) {
    add_alert(out, STORM_ALERT_WIND, "High winds");
  }
  if (pressure_change.ok && pressure_change.value <= -t.pressure_drop_hpa) {
    add_alert(out, STORM_ALERT_PRESSURE, "Falling pressure");
  }
  if (ahead_precip.ok && ahead_precip.value >= t.heavy_precip_mm_hr) {
    add_alert(out, STORM_ALERT_PRECIP, ahead_snow.ok && ahead_snow.value > 0 ? "Heavy snow" : "Heavy rain");
  }

#define SPEED(kmh) ((int) js_round(imperial ? (kmh) / KMH_PER_MPH : (kmh)))
  snprintf(out->wind_unit, sizeof(out->wind_unit), "%s", imperial ? "mph" : "km/h");
  snprintf(out->pressure_unit, sizeof(out->pressure_unit), "%s", imperial ? "inHg" : "hPa");
  snprintf(out->precip_unit, sizeof(out->precip_unit), "%s", imperial ? "in" : "mm");

  /* --- Plain-language wording --- */
  bool snow_expected = or_zero(sum_over(field(hourly, "snowfall"), now_i, outlook_i)) > 0;
  const char *precip_word = snow_expected ? "Snow" : "Rain";
  char precip_word_lower[8];
  to_lower_copy(precip_word, precip_word_lower, sizeof(precip_word_lower));

  const char *pressure_status = NULL;
  const char *pressure_meaning = NULL;
  if (pressure_change.ok) {
    if (pressure_change.value <= -t.pressure_drop_hpa) {
      pressure_status = "Dropping fast";
      pressure_meaning = "Can mean a storm is coming";
    } else if (pressure_falling) {
      pressure_status = "Dropping";
      pressure_meaning = "Can mean clouds or rain";
    } else if (strcmp(pressure_trend, "rising") == 0) {
      pressure_status = "Rising";
      pressure_meaning = "Can mean clearer weather";
    } else {
      pressure_status = "Steady";
      pressure_meaning = "Weather likely to stay the same";
    }
  }

  char chance_text[24] = "";
  if (chance_max.ok) {
    format_js_number(chance_max.value, chance_text, sizeof(chance_text));
  }
  int wet_i = first_index_where(&ctx, now_i, outlook_i, pred_wet);
  bool precip_likely = wet_i >= 0 || (chance_max.ok ? chance_max.value : 0.0) >= LIKELY_CHANCE;
  bool precip_possible = !precip_likely && (chance_max.ok ? chance_max.value : 0.0) >= POSSIBLE_CHANCE;
  bool raining_now = or_zero(precip_now_mm) >= MEASURABLE_PRECIP_MM;

  /* Short enough for the narrow rain column; the headline carries timing. */
  if (raining_now) {
    snprintf(out->precip_status, sizeof(out->precip_status), "%sing", precip_word);
  } else if (precip_likely) {
    if (chance_max.ok) {
      snprintf(out->precip_status, sizeof(out->precip_status), "%s%% chance", chance_text);
    } else {
      snprintf(out->precip_status, sizeof(out->precip_status), "Likely");
    }
  } else if (precip_possible) {
    snprintf(out->precip_status, sizeof(out->precip_status), "%s%% chance", chance_text);
  } else {
    snprintf(out->precip_status, sizeof(out->precip_status), "No %s", precip_word_lower);
  }

  int wet_hours = count_where(&ctx, now_i, outlook_i, pred_wet);
  max_result_t peak_rate = max_over(precipitation, now_i, outlook_i);
  double peak_rate_mm = peak_rate.ok ? peak_rate.value : 0.0;
  const char *intensity = peak_rate_mm >= t.heavy_precip_mm_hr
    ? "Heavy"
    : peak_rate_mm >= MODERATE_PRECIP_MM_HR ? "Steady" : "Light";
  char precip_spell[STORM_TEXT_LEN] = "";
  bool has_precip_spell = wet_hours > 0;
  if (has_precip_spell) {
    snprintf(precip_spell, sizeof(precip_spell), "%s %s for about %d hour%s",
      intensity, precip_word_lower, wet_hours, wet_hours == 1 ? "" : "s");
  }

  char outlook_end_hour[12];
  format_hour(times, outlook_i, config->time_24h, outlook_end_hour, sizeof(outlook_end_hour));
  if (or_zero(precip_outlook_mm) >= MEASURABLE_PRECIP_MM) {
    char amount[24];
    format_js_number(
      round_places(imperial ? precip_outlook_mm.value / MM_PER_INCH : precip_outlook_mm.value, imperial ? 2 : 1),
      amount, sizeof(amount)
    );
    out->has_precip_detail = true;
    snprintf(out->precip_detail, sizeof(out->precip_detail), "About %s %s by %s", amount, out->precip_unit, outlook_end_hour);
  } else if (precip_likely || precip_possible) {
    out->has_precip_detail = true;
    snprintf(out->precip_detail, sizeof(out->precip_detail), "Light, if any");
  }

  /* --- Temperature swings --- */
  const cJSON *temps = field(hourly, "temperature_2m");
  double front_drop_3h = imperial ? FRONT_DROP_3H_IMPERIAL : FRONT_DROP_3H_METRIC;
  opt_t temp_now = number_at(temps, now_i);
  bool has_front = false;
  int front_index = -1;
  int front_drop = 0;
  for (int i = now_i + 1; i <= outlook_i && temp_now.ok; ++i) {
    opt_t before = number_at(temps, (i - 3) > now_i ? i - 3 : now_i);
    opt_t after = number_at(temps, i);
    if (before.ok && after.ok && before.value - after.value >= front_drop_3h) {
      has_front = true;
      front_index = i;
      front_drop = (int) js_round(temp_now.value - after.value);
      break;
    }
  }
  const cJSON *daily_max = field(daily, "temperature_2m_max");
  opt_t high_today = number_at(daily_max, 0);
  opt_t high_tomorrow = number_at(daily_max, 1);
  bool has_swing = false;
  int swing_delta = 0;
  int swing_high = 0;
  int swing_today = 0;
  if (high_today.ok && high_tomorrow.ok) {
    swing_delta = (int) js_round(high_tomorrow.value - high_today.value);
    if (abs(swing_delta) >= (imperial ? DAY_TO_DAY_SWING_IMPERIAL : DAY_TO_DAY_SWING_METRIC)) {
      has_swing = true;
      swing_high = (int) js_round(high_tomorrow.value);
      swing_today = (int) js_round(high_today.value);
    }
  }

  /* --- Wind outlook (what's coming; the compass already shows now) --- */
  double gust_base = or_zero(gust_now_kmh) > or_zero(wind_kmh) ? or_zero(gust_now_kmh) : or_zero(wind_kmh);
  bool breezy_now = or_zero(wind_kmh) >= BREEZY_WIND_KMH || or_zero(gust_now_kmh) >= BREEZY_GUST_KMH;
  int wind_ease_i = breezy_now ? end_index_where(&ctx, now_i, outlook_i, pred_breezy) : -1;
  char hour_text[12];
  if (gust_peak.ok && gust_peak.index > now_i && gust_peak.value >= gust_base + PICKING_UP_GUST_KMH) {
    format_hour(times, gust_peak.index, config->time_24h, hour_text, sizeof(hour_text));
    snprintf(out->outlook_status, sizeof(out->outlook_status), "Picking up");
    snprintf(out->outlook_detail, sizeof(out->outlook_detail), "Gusts to %d %s by %s",
      SPEED(gust_peak.value), out->wind_unit, hour_text);
  } else if (wind_ease_i >= 0) {
    format_hour(times, wind_ease_i, config->time_24h, hour_text, sizeof(hour_text));
    snprintf(out->outlook_status, sizeof(out->outlook_status), "Easing");
    snprintf(out->outlook_detail, sizeof(out->outlook_detail), "Calmer by %s", hour_text);
  } else {
    /* Only mention gusts if something stronger than now is still to come. */
    bool stronger_later = gust_peak.ok && SPEED(gust_peak.value) > SPEED(gust_base);
    snprintf(out->outlook_status, sizeof(out->outlook_status), "Steady");
    if (stronger_later) {
      snprintf(out->outlook_detail, sizeof(out->outlook_detail), "Gusts up to %d %s", SPEED(gust_peak.value), out->wind_unit);
    } else {
      snprintf(out->outlook_detail, sizeof(out->outlook_detail), "No big change");
    }
  }

  /* `when(i)`: "now" or "by 4 PM" */
#define WHEN(i, buf) do { \
    if ((i) <= now_i) { snprintf((buf), sizeof(buf), "now"); } \
    else { char h_[12]; format_hour(times, (i), config->time_24h, h_, sizeof(h_)); snprintf((buf), sizeof(buf), "by %s", h_); } \
  } while (0)

  /* Headline: the single most important thing. Things already happening say
   * when they end ("until 8 PM"); things on the way say when they start. */
  char phrases[STORM_MAX_ALERTS][STORM_TEXT_LEN];
  int phrase_count = 0;
  for (int a = 0; a < out->alert_count; ++a) {
    char *phrase = phrases[phrase_count];
    char tail[24];
    storm_alert_kind_t kind = out->alerts[a].kind;
    if (kind == STORM_ALERT_THUNDER) {
      int end_i = thunder_i <= now_i ? end_index_where(&ctx, now_i, outlook_i, pred_thunder) : -1;
      if (thunder_i > now_i) {
        WHEN(thunder_i, tail);
        snprintf(phrase, STORM_TEXT_LEN, "Thunderstorms likely %s", tail);
      } else if (end_i >= 0) {
        format_hour(times, end_i, config->time_24h, hour_text, sizeof(hour_text));
        snprintf(phrase, STORM_TEXT_LEN, "Thunderstorms until %s", hour_text);
      } else {
        snprintf(phrase, STORM_TEXT_LEN, "Thunderstorms now");
      }
    } else if (kind == STORM_ALERT_GUSTS || kind == STORM_ALERT_WIND) {
      bool is_gusts = kind == STORM_ALERT_GUSTS;
      max_result_t peak = is_gusts ? ahead_gust : ahead_wind;
      int end_i = peak.index <= now_i
        ? end_index_where(&ctx, now_i, outlook_i, is_gusts ? pred_gust_strong : pred_wind_strong)
        : -1;
      if (peak.index > now_i) {
        WHEN(peak.index, tail);
      } else if (end_i >= 0) {
        format_hour(times, end_i, config->time_24h, hour_text, sizeof(hour_text));
        snprintf(tail, sizeof(tail), "until %s", hour_text);
      } else {
        snprintf(tail, sizeof(tail), "now");
      }
      snprintf(phrase, STORM_TEXT_LEN, "%s up to %d %s %s", is_gusts ? "Gusts" : "Winds", SPEED(peak.value), out->wind_unit, tail);
    } else if (kind == STORM_ALERT_PRECIP) {
      int end_i = heavy_precip_i <= now_i ? end_index_where(&ctx, now_i, outlook_i, pred_heavy) : -1;
      if (heavy_precip_i > now_i) {
        WHEN(heavy_precip_i, tail);
        snprintf(phrase, STORM_TEXT_LEN, "%s likely %s", out->alerts[a].label, tail);
      } else if (end_i >= 0) {
        format_hour(times, end_i, config->time_24h, hour_text, sizeof(hour_text));
        snprintf(phrase, STORM_TEXT_LEN, "%s until %s", out->alerts[a].label, hour_text);
      } else {
        snprintf(phrase, STORM_TEXT_LEN, "%s now", out->alerts[a].label);
      }
    } else {
      snprintf(phrase, STORM_TEXT_LEN, "Pressure dropping fast");
    }
    phrase_count++;
  }

  if (phrase_count > 0) {
    snprintf(out->headline, sizeof(out->headline), "%s", phrases[0]);
    if (phrase_count > 1) {
      out->has_detail = true;
      snprintf(out->detail, sizeof(out->detail), "%s", phrases[1]);
    } else if (out->alerts[0].kind == STORM_ALERT_PRESSURE) {
      out->has_detail = true;
      snprintf(out->detail, sizeof(out->detail), "%s", pressure_meaning);
    }
  } else {
    /* Nothing alert-worthy in the next few hours, but the headline still
     * mustn't say "calm" when something is coming later in the outlook. */
    int later_thunder_i = first_index_where(&ctx, now_i, outlook_i, pred_thunder);
    int later_gust_i = first_index_where(&ctx, now_i, outlook_i, pred_gust_strong);
    int rain_end_i = raining_now ? end_index_where(&ctx, now_i, outlook_i, pred_wet) : -1;
    char tail[24];
    out->has_detail = true;
    if (raining_now) {
      if (rain_end_i >= 0) {
        format_hour(times, rain_end_i, config->time_24h, hour_text, sizeof(hour_text));
        snprintf(out->headline, sizeof(out->headline), "%s ending by %s", precip_word, hour_text);
        snprintf(out->detail, sizeof(out->detail), "%s %s until then", intensity, precip_word_lower);
      } else {
        snprintf(out->headline, sizeof(out->headline), "%sing now", precip_word);
        snprintf(out->detail, sizeof(out->detail), "%s %s for the next %d hours", intensity, precip_word_lower, CONDITIONS_OUTLOOK_HOURS);
      }
    } else if (later_thunder_i >= 0) {
      WHEN(later_thunder_i, tail);
      snprintf(out->headline, sizeof(out->headline), "Thunderstorms possible %s", tail);
      out->has_detail = has_precip_spell;
      snprintf(out->detail, sizeof(out->detail), "%s", precip_spell);
    } else if (has_front) {
      format_hour(times, front_index, config->time_24h, hour_text, sizeof(hour_text));
      snprintf(out->headline, sizeof(out->headline), "Turning much colder by %s", hour_text);
      snprintf(out->detail, sizeof(out->detail), "About %d\xc2\xb0 colder by then", front_drop);
    } else if (later_gust_i >= 0) {
      WHEN(gust_peak.index, tail);
      snprintf(out->headline, sizeof(out->headline), "Windy later");
      snprintf(out->detail, sizeof(out->detail), "Gusts up to %d %s %s", SPEED(gust_peak.value), out->wind_unit, tail);
    } else if (precip_likely) {
      if (wet_i >= 0) {
        WHEN(wet_i, tail);
      } else {
        snprintf(tail, sizeof(tail), "later");
      }
      snprintf(out->headline, sizeof(out->headline), "%s likely %s", precip_word, tail);
      out->has_detail = has_precip_spell;
      snprintf(out->detail, sizeof(out->detail), "%s", precip_spell);
    } else if (precip_possible) {
      snprintf(out->headline, sizeof(out->headline), "Chance of %s later", precip_word_lower);
      snprintf(out->detail, sizeof(out->detail), "%s%% over the next %d hours", chance_text, CONDITIONS_OUTLOOK_HOURS);
    } else if (has_swing) {
      snprintf(out->headline, sizeof(out->headline), "Much %s tomorrow", swing_delta < 0 ? "colder" : "warmer");
      snprintf(out->detail, sizeof(out->detail), "High of %d\xc2\xb0, %d\xc2\xb0 today", swing_high, swing_today);
    } else if (pressure_falling) {
      snprintf(out->headline, sizeof(out->headline), "Dry for now");
      snprintf(out->detail, sizeof(out->detail), "Falling pressure can mean rain later");
    } else if (wind_ease_i >= 0) {
      format_hour(times, wind_ease_i, config->time_24h, hour_text, sizeof(hour_text));
      snprintf(out->headline, sizeof(out->headline), "Winds easing by %s", hour_text);
      snprintf(out->detail, sizeof(out->detail), "Dry for the next 6 hours");
    } else {
      snprintf(out->headline, sizeof(out->headline), "%s", breezy_now ? "Breezy and dry" : "Calm and dry");
      snprintf(out->detail, sizeof(out->detail), "for the next %d hours", CONDITIONS_OUTLOOK_HOURS);
    }
  }
#undef WHEN

  /* Divider: "changing" needs something concrete in the forecast; early
   * hints alone (falling pressure, a low chance of rain) only "may change". */
  bool changing = out->alert_count > 0
    || raining_now || precip_likely
    || strcmp(out->outlook_status, "Steady") != 0
    || has_front || has_swing;
  bool may_change = !changing && (pressure_falling || precip_possible);
  out->alert = out->alert_count > 0;
  out->changing = changing;
  out->change_level = changing ? STORM_CHANGE_CHANGING : may_change ? STORM_CHANGE_MAY_CHANGE : STORM_CHANGE_STEADY;
  snprintf(out->change_label, sizeof(out->change_label), "%s",
    changing ? "Conditions changing" : may_change ? "Conditions may change" : "Conditions steady");

  /* Wind */
  out->has_speed = wind_kmh.ok;
  out->speed = wind_kmh.ok ? SPEED(wind_kmh.value) : 0;
  /* Live speed and hourly gusts can disagree; a gust at or below the wind
   * speed is noise. */
  out->has_gust = gust_now_kmh.ok && gust_now_kmh.value > or_zero(wind_kmh);
  out->gust = out->has_gust ? SPEED(gust_now_kmh.value) : 0;
  out->has_direction = wind_dir.ok;
  out->direction = wind_dir.value;
  if (wind_dir.ok) {
    double deg = normalize_degrees(wind_dir.value);
    snprintf(out->cardinal, sizeof(out->cardinal), "%s", CARDINALS[(int) js_round(deg / 22.5) % 16]);
    snprintf(out->from, sizeof(out->from), "from the %s", DIRECTION_NAMES[(int) js_round(deg / 45.0) % 8]);
  }
  out->has_peak_gust = gust_peak.ok;
  out->peak_gust = gust_peak.ok ? SPEED(gust_peak.value) : 0;
  if (gust_peak.index >= 0) {
    const cJSON *peak_time = cJSON_GetArrayItem((cJSON *) times, gust_peak.index);
    snprintf(out->peak_gust_time, sizeof(out->peak_gust_time), "%s",
      cJSON_IsString(peak_time) ? cJSON_GetStringValue(peak_time) : "");
  }
#undef SPEED

  /* Pressure */
  out->has_pressure = pressure_now.ok;
  if (pressure_now.ok) {
    out->pressure_value = round_places(imperial ? pressure_now.value / HPA_PER_INHG : pressure_now.value, imperial ? 2 : 0);
  }
  out->has_pressure_change = pressure_change.ok;
  if (pressure_change.ok) {
    out->pressure_change = round_places(imperial ? pressure_change.value / HPA_PER_INHG : pressure_change.value, imperial ? 2 : 1);
  }
  snprintf(out->pressure_trend, sizeof(out->pressure_trend), "%s", pressure_trend != NULL ? pressure_trend : "");
  snprintf(out->pressure_status, sizeof(out->pressure_status), "%s", pressure_status != NULL ? pressure_status : "");
  snprintf(out->pressure_meaning, sizeof(out->pressure_meaning), "%s", pressure_meaning != NULL ? pressure_meaning : "");

  /* Precipitation */
  snprintf(out->precip_label, sizeof(out->precip_label), "%s", precip_word);
  out->has_precip_now = precip_now_mm.ok;
  if (precip_now_mm.ok) {
    out->precip_now = round_places(imperial ? precip_now_mm.value / MM_PER_INCH : precip_now_mm.value, imperial ? 2 : 1);
  }
  out->has_precip_outlook = precip_outlook_mm.ok;
  if (precip_outlook_mm.ok) {
    out->precip_outlook = round_places(imperial ? precip_outlook_mm.value / MM_PER_INCH : precip_outlook_mm.value, imperial ? 2 : 1);
  }
  out->outlook_hours = CONDITIONS_OUTLOOK_HOURS;
  out->has_chance = chance_max.ok;
  out->chance = chance_max.value;

  return true;
}
