#include "forecast_representative.h"

#include <ctype.h>
#include <stdbool.h>
#include <string.h>

#define FORECAST_DAYTIME_START_HOUR 8
#define FORECAST_DAYTIME_END_HOUR 20
#define FORECAST_THUNDER_HOURS 2
#define FORECAST_THUNDER_WET_HOURS 2
#define FORECAST_THUNDER_PROBABILITY_MODERATE 35.0
#define FORECAST_THUNDER_PROBABILITY 60.0
#define FORECAST_THUNDER_TOTAL_MM 0.5
#define FORECAST_RAIN_HOURS 3
#define FORECAST_RAIN_PROBABILITY 60.0
#define FORECAST_RAIN_TOTAL_MM 1.5
#define FORECAST_SNOW_HOURS 2
#define FORECAST_SNOW_TOTAL_MM 0.5
#define FORECAST_FOG_HOURS 3

typedef struct {
  int sample_count;
  int thunder_hours;
  int wet_hours;
  int rain_hours;
  int shower_hours;
  int snow_hours;
  int freezing_hours;
  int fog_hours;
  int cloudy_hours;
  int partly_hours;
  int clear_hours;
  double total_precipitation;
  double total_snowfall;
  double max_precipitation_probability;
  double average_cloud_cover;
} forecast_daytime_summary_t;

static int forecast_round_number(const cJSON *value)
{
  if (value == NULL) {
    return 0;
  }

  double raw_value = cJSON_GetNumberValue(value);
  return raw_value >= 0.0 ? (int) (raw_value + 0.5) : (int) (raw_value - 0.5);
}

static double forecast_read_number_or_default(const cJSON *value, double fallback)
{
  return cJSON_IsNumber(value) ? cJSON_GetNumberValue(value) : fallback;
}

static cJSON *forecast_get_array_item(const cJSON *array, int index)
{
  if (array == NULL || !cJSON_IsArray(array)) {
    return NULL;
  }

  return cJSON_GetArrayItem((cJSON *) array, index);
}

static const char *forecast_get_string_array_item(const cJSON *array, int index)
{
  cJSON *value = forecast_get_array_item(array, index);
  return cJSON_IsString(value) ? cJSON_GetStringValue(value) : NULL;
}

static int forecast_parse_hour_from_timestamp(const char *timestamp)
{
  if (timestamp == NULL
      || strlen(timestamp) < 13
      || timestamp[10] != 'T'
      || !isdigit((unsigned char) timestamp[11])
      || !isdigit((unsigned char) timestamp[12])) {
    return -1;
  }

  return (timestamp[11] - '0') * 10 + (timestamp[12] - '0');
}

static bool forecast_timestamp_matches_date(const char *timestamp, const char *date)
{
  return timestamp != NULL
      && date != NULL
      && strlen(date) >= 10
      && strncmp(timestamp, date, 10) == 0
      && timestamp[10] == 'T';
}

static int forecast_max_int(int a, int b)
{
  return a > b ? a : b;
}

static bool is_hour_in_daytime(int hour)
{
  return hour >= FORECAST_DAYTIME_START_HOUR && hour <= FORECAST_DAYTIME_END_HOUR;
}

static bool is_thunder_code(int code)
{
  return code == 95 || code == 96 || code == 99;
}

static bool is_drizzle_code(int code)
{
  return code >= 51 && code <= 57;
}

static bool is_rain_code(int code)
{
  return (code >= 61 && code <= 65) || (code >= 80 && code <= 82);
}

static bool is_freezing_code(int code)
{
  return code == 56 || code == 57 || code == 66 || code == 67;
}

static bool is_snow_code(int code)
{
  return (code >= 71 && code <= 77) || code == 85 || code == 86;
}

static bool is_fog_code(int code)
{
  return code == 45 || code == 48;
}

static bool is_cloudy_code(int code)
{
  return code == 3;
}

static bool is_partly_code(int code)
{
  return code == 1 || code == 2;
}

static bool is_clear_code(int code)
{
  return code == 0;
}

static void summarize_daytime_samples(
  const cJSON *hourly,
  const char *date,
  forecast_daytime_summary_t *out_summary
)
{
  if (out_summary == NULL) {
    return;
  }

  memset(out_summary, 0, sizeof(*out_summary));

  if (!cJSON_IsObject((cJSON *) hourly) || date == NULL || date[0] == '\0') {
    return;
  }

  cJSON *times = cJSON_GetObjectItemCaseSensitive((cJSON *) hourly, "time");
  cJSON *codes = cJSON_GetObjectItemCaseSensitive((cJSON *) hourly, "weathercode");
  cJSON *precip_probs = cJSON_GetObjectItemCaseSensitive((cJSON *) hourly, "precipitation_probability");
  cJSON *precipitations = cJSON_GetObjectItemCaseSensitive((cJSON *) hourly, "precipitation");
  cJSON *rains = cJSON_GetObjectItemCaseSensitive((cJSON *) hourly, "rain");
  cJSON *showers = cJSON_GetObjectItemCaseSensitive((cJSON *) hourly, "showers");
  cJSON *snowfalls = cJSON_GetObjectItemCaseSensitive((cJSON *) hourly, "snowfall");
  cJSON *cloud_cover = cJSON_GetObjectItemCaseSensitive((cJSON *) hourly, "cloud_cover");

  if (!cJSON_IsArray(times)) {
    return;
  }

  int sample_count = cJSON_GetArraySize(times);
  double cloud_cover_total = 0.0;

  for (int i = 0; i < sample_count; ++i) {
    const char *timestamp = forecast_get_string_array_item(times, i);
    if (!forecast_timestamp_matches_date(timestamp, date)) {
      continue;
    }

    int hour = forecast_parse_hour_from_timestamp(timestamp);
    if (!is_hour_in_daytime(hour)) {
      continue;
    }

    int code = forecast_round_number(forecast_get_array_item(codes, i));
    double precipitation_probability = forecast_read_number_or_default(forecast_get_array_item(precip_probs, i), 0.0);
    double precipitation = forecast_read_number_or_default(forecast_get_array_item(precipitations, i), 0.0);
    double rain = forecast_read_number_or_default(forecast_get_array_item(rains, i), 0.0);
    double shower = forecast_read_number_or_default(forecast_get_array_item(showers, i), 0.0);
    double snowfall = forecast_read_number_or_default(forecast_get_array_item(snowfalls, i), 0.0);
    double cloud = forecast_read_number_or_default(forecast_get_array_item(cloud_cover, i), 0.0);
    double wet_amount = precipitation > (rain + shower) ? precipitation : (rain + shower);

    out_summary->sample_count += 1;

    if (is_thunder_code(code)) {
      out_summary->thunder_hours += 1;
    }
    if (is_snow_code(code) || snowfall > 0.0) {
      out_summary->snow_hours += 1;
    }
    if (is_freezing_code(code)) {
      out_summary->freezing_hours += 1;
    }
    if (is_fog_code(code)) {
      out_summary->fog_hours += 1;
    }

    if (is_cloudy_code(code) || cloud >= 75.0) {
      out_summary->cloudy_hours += 1;
    } else if (is_partly_code(code) || cloud >= 35.0) {
      out_summary->partly_hours += 1;
    } else if (is_clear_code(code) || cloud < 35.0) {
      out_summary->clear_hours += 1;
    }

    if (is_rain_code(code) || is_drizzle_code(code) || wet_amount > 0.0) {
      out_summary->wet_hours += 1;
    }
    if ((code >= 80 && code <= 82) || is_drizzle_code(code) || shower > 0.0) {
      out_summary->shower_hours += 1;
    }
    if ((code >= 61 && code <= 65) || rain > 0.0) {
      out_summary->rain_hours += 1;
    }

    out_summary->total_precipitation += precipitation;
    out_summary->total_snowfall += snowfall;
    if (precipitation_probability > out_summary->max_precipitation_probability) {
      out_summary->max_precipitation_probability = precipitation_probability;
    }
    cloud_cover_total += cloud;
  }

  if (out_summary->sample_count > 0) {
    out_summary->average_cloud_cover = cloud_cover_total / out_summary->sample_count;
  }
}

int forecast_pick_representative_code(
  const cJSON *hourly,
  const char *date,
  int daily_code,
  int daily_high,
  int daily_low,
  int snow_threshold
)
{
  forecast_daytime_summary_t summary;
  summarize_daytime_samples(hourly, date, &summary);

  if (summary.sample_count == 0) {
    return daily_code;
  }

  double mid_temp = (daily_high + daily_low) / 2.0;
  bool cold_enough_for_snow = mid_temp <= (double) snow_threshold;

  if (((summary.thunder_hours >= FORECAST_THUNDER_HOURS)
        && ((summary.wet_hours >= FORECAST_THUNDER_WET_HOURS)
            || summary.total_precipitation >= FORECAST_THUNDER_TOTAL_MM
            || summary.max_precipitation_probability >= FORECAST_THUNDER_PROBABILITY_MODERATE))
      || ((summary.thunder_hours >= 1)
          && ((summary.wet_hours >= 1) || summary.total_precipitation > 0.0)
          && summary.max_precipitation_probability >= FORECAST_THUNDER_PROBABILITY)) {
    return 95;
  }

  if (summary.freezing_hours >= 1
      && ((summary.wet_hours >= 2)
          || summary.total_precipitation >= 1.0
          || summary.max_precipitation_probability >= FORECAST_RAIN_PROBABILITY)) {
    return 66;
  }

  if (summary.snow_hours >= FORECAST_SNOW_HOURS
      || summary.total_snowfall >= FORECAST_SNOW_TOTAL_MM
      || (cold_enough_for_snow
          && summary.wet_hours >= FORECAST_RAIN_HOURS
          && summary.max_precipitation_probability >= FORECAST_RAIN_PROBABILITY)) {
    return 73;
  }

  if (summary.wet_hours >= FORECAST_RAIN_HOURS
      || (summary.max_precipitation_probability >= FORECAST_RAIN_PROBABILITY
          && summary.total_precipitation >= FORECAST_RAIN_TOTAL_MM)) {
    return summary.shower_hours >= summary.rain_hours ? 80 : 61;
  }

  if (summary.fog_hours >= FORECAST_FOG_HOURS
      && summary.wet_hours <= 1
      && summary.average_cloud_cover >= 70.0) {
    return 45;
  }

  if (summary.average_cloud_cover >= 70.0
      || summary.cloudy_hours >= forecast_max_int(summary.clear_hours, summary.partly_hours)) {
    return 3;
  }

  if (summary.average_cloud_cover >= 35.0 || summary.partly_hours >= summary.clear_hours) {
    return 2;
  }

  return 0;
}
