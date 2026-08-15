#include "weather_client.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "forecast_representative.h"
#include "weather_icon_names.h"

static const char *TAG = "rwd_weather";

#define WEATHER_FORECAST_DAYS_TOTAL 6
#define WEATHER_HTTP_TIMEOUT_MS 10000
#define WEATHER_URL_BUFFER_LEN 768

typedef struct {
  char *data;
  size_t length;
} http_response_buffer_t;

static bool is_metric_units(const device_config_t *config)
{
  return config != NULL && strncmp(config->units, "metric", strlen("metric")) == 0;
}

static int snow_temp_threshold(const device_config_t *config)
{
  return is_metric_units(config) ? 1 : 34;
}

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

static void describe_weather_code(
  int code,
  bool is_day,
  bool thundersnow,
  char *out,
  size_t out_size
)
{
  if (out == NULL || out_size == 0) {
    return;
  }

  if (thundersnow) {
    snprintf(out, out_size, "Thundersnow");
    return;
  }

  if (code == 0) {
    snprintf(out, out_size, "Clear");
  } else if (code == 1) {
    snprintf(out, out_size, "%s", is_day ? "Mostly Sunny" : "Mostly Clear");
  } else if (code == 2) {
    snprintf(out, out_size, "Partly Cloudy");
  } else if (code == 3) {
    snprintf(out, out_size, "Cloudy");
  } else if (code == 45 || code == 48) {
    snprintf(out, out_size, "Foggy");
  } else if (code >= 51 && code <= 57) {
    snprintf(out, out_size, "Drizzle");
  } else if (code >= 61 && code <= 65) {
    snprintf(out, out_size, "Rain");
  } else if (code == 66 || code == 67) {
    snprintf(out, out_size, "Sleet");
  } else if (code >= 71 && code <= 77) {
    snprintf(out, out_size, "Snow");
  } else if (code >= 80 && code <= 82) {
    snprintf(out, out_size, "%s", is_day ? "Showers" : "Showers");
  } else if (code == 85 || code == 86) {
    snprintf(out, out_size, "Snow Showers");
  } else if (code == 95 || code == 96 || code == 99) {
    snprintf(out, out_size, "Thunderstorm");
  } else {
    snprintf(out, out_size, "Cloudy");
  }
}

static bool is_thundersnow(const device_config_t *config, int weather_code, int temp_now)
{
  if (weather_code != 95 && weather_code != 96 && weather_code != 99) {
    return false;
  }

  return temp_now <= snow_temp_threshold(config);
}

static esp_err_t append_http_data(http_response_buffer_t *buffer, const char *data, int data_len)
{
  if (buffer == NULL || data == NULL || data_len <= 0) {
    return ESP_OK;
  }

  size_t next_length = buffer->length + (size_t) data_len;
  char *next_data = realloc(buffer->data, next_length + 1);
  if (next_data == NULL) {
    return ESP_ERR_NO_MEM;
  }

  buffer->data = next_data;
  memcpy(buffer->data + buffer->length, data, (size_t) data_len);
  buffer->length = next_length;
  buffer->data[buffer->length] = '\0';
  return ESP_OK;
}

static esp_err_t http_event_handler(esp_http_client_event_t *event)
{
  http_response_buffer_t *buffer = (http_response_buffer_t *) event->user_data;

  switch (event->event_id) {
    case HTTP_EVENT_ON_DATA:
      return append_http_data(buffer, (const char *) event->data, event->data_len);
    default:
      return ESP_OK;
  }
}

static int round_number(const cJSON *value)
{
  if (value == NULL) {
    return 0;
  }

  double raw_value = cJSON_GetNumberValue(value);
  return raw_value >= 0.0 ? (int) (raw_value + 0.5) : (int) (raw_value - 0.5);
}

static cJSON *get_array_item(const cJSON *array, int index)
{
  if (array == NULL || !cJSON_IsArray(array)) {
    return NULL;
  }

  return cJSON_GetArrayItem((cJSON *) array, index);
}

static const char *get_string_array_item(const cJSON *array, int index)
{
  cJSON *value = get_array_item(array, index);
  return cJSON_IsString(value) ? cJSON_GetStringValue(value) : NULL;
}

static bool fill_temperature_range(
  const cJSON *daily,
  int index,
  int *high_out,
  int *low_out
)
{
  cJSON *highs = cJSON_GetObjectItemCaseSensitive((cJSON *) daily, "temperature_2m_max");
  cJSON *lows = cJSON_GetObjectItemCaseSensitive((cJSON *) daily, "temperature_2m_min");
  cJSON *high_value = get_array_item(highs, index);
  cJSON *low_value = get_array_item(lows, index);
  if (!cJSON_IsNumber(high_value) || !cJSON_IsNumber(low_value)) {
    return false;
  }

  *high_out = round_number(high_value);
  *low_out = round_number(low_value);
  return true;
}

static int read_daily_weather_code(const cJSON *daily, int index, bool *ok_out)
{
  cJSON *codes = cJSON_GetObjectItemCaseSensitive((cJSON *) daily, "weathercode");
  cJSON *code_value = get_array_item(codes, index);
  if (!cJSON_IsNumber(code_value)) {
    *ok_out = false;
    return 3;
  }

  *ok_out = true;
  return round_number(code_value);
}

static void url_encode_component(const char *input, char *output, size_t output_size)
{
  static const char HEX[] = "0123456789ABCDEF";
  size_t out_index = 0;

  if (output == NULL || output_size == 0) {
    return;
  }

  if (input == NULL) {
    output[0] = '\0';
    return;
  }

  for (size_t i = 0; input[i] != '\0' && out_index + 1 < output_size; ++i) {
    unsigned char ch = (unsigned char) input[i];
    bool safe = isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~';

    if (safe) {
      output[out_index++] = (char) ch;
      continue;
    }

    if (out_index + 3 >= output_size) {
      break;
    }

    output[out_index++] = '%';
    output[out_index++] = HEX[(ch >> 4) & 0x0F];
    output[out_index++] = HEX[ch & 0x0F];
  }

  output[out_index] = '\0';
}

esp_err_t weather_client_fetch(
  const device_config_t *config,
  app_weather_snapshot_t *out_snapshot
)
{
  if (config == NULL || out_snapshot == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  if (config->latitude[0] == '\0' || config->longitude[0] == '\0') {
    return ESP_ERR_INVALID_STATE;
  }

  char encoded_timezone[96];
  url_encode_component(config->timezone, encoded_timezone, sizeof(encoded_timezone));

  char temperature_unit[16];
  snprintf(
    temperature_unit,
    sizeof(temperature_unit),
    "%s",
    strings_equal_ignore_case(config->units, "metric") ? "celsius" : "fahrenheit"
  );

  char url[WEATHER_URL_BUFFER_LEN];
  snprintf(
    url,
    sizeof(url),
    "https://api.open-meteo.com/v1/forecast"
    "?latitude=%s"
    "&longitude=%s"
    "&current_weather=true"
    "&hourly=weathercode,precipitation_probability,precipitation,rain,showers,snowfall,cloud_cover"
    "&daily=temperature_2m_max,temperature_2m_min,weathercode"
    "&temperature_unit=%s"
    "&timezone=%s"
    "&forecast_days=%d",
    config->latitude,
    config->longitude,
    temperature_unit,
    encoded_timezone[0] != '\0' ? encoded_timezone : "UTC",
    WEATHER_FORECAST_DAYS_TOTAL
  );

  http_response_buffer_t response = {0};
  esp_http_client_config_t http_config = {
    .url = url,
    .event_handler = http_event_handler,
    .user_data = &response,
    .timeout_ms = WEATHER_HTTP_TIMEOUT_MS,
    .buffer_size = 2048,
    .crt_bundle_attach = esp_crt_bundle_attach,
  };

  esp_http_client_handle_t client = esp_http_client_init(&http_config);
  if (client == NULL) {
    return ESP_FAIL;
  }

  esp_err_t err = esp_http_client_perform(client);
  int status_code = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);

  if (err != ESP_OK) {
    free(response.data);
    return err;
  }

  if (status_code < 200 || status_code >= 300) {
    free(response.data);
    return ESP_FAIL;
  }

  cJSON *root = cJSON_Parse(response.data);
  free(response.data);
  if (root == NULL) {
    return ESP_FAIL;
  }

  cJSON *current_weather = cJSON_GetObjectItemCaseSensitive(root, "current_weather");
  cJSON *daily = cJSON_GetObjectItemCaseSensitive(root, "daily");
  cJSON *hourly = cJSON_GetObjectItemCaseSensitive(root, "hourly");
  if (!cJSON_IsObject(current_weather) || !cJSON_IsObject(daily)) {
    cJSON_Delete(root);
    return ESP_FAIL;
  }

  cJSON *current_temperature = cJSON_GetObjectItemCaseSensitive(current_weather, "temperature");
  cJSON *current_code = cJSON_GetObjectItemCaseSensitive(current_weather, "weathercode");
  cJSON *current_is_day = cJSON_GetObjectItemCaseSensitive(current_weather, "is_day");
  if (!cJSON_IsNumber(current_temperature) || !cJSON_IsNumber(current_code) || !cJSON_IsNumber(current_is_day)) {
    cJSON_Delete(root);
    return ESP_FAIL;
  }

  memset(out_snapshot, 0, sizeof(*out_snapshot));

  int current_temp = round_number(current_temperature);
  int today_high = current_temp;
  int today_low = current_temp;
  bool have_today_range = fill_temperature_range(daily, 0, &today_high, &today_low);
  int live_weather_code = round_number(current_code);
  bool thundersnow = is_thundersnow(config, live_weather_code, current_temp);

  out_snapshot->valid = true;
  out_snapshot->stale = false;
  out_snapshot->updated_at = time(NULL);
  out_snapshot->temp = current_temp;
  out_snapshot->high = have_today_range ? (today_high > current_temp ? today_high : current_temp) : current_temp;
  out_snapshot->low = have_today_range ? (today_low < current_temp ? today_low : current_temp) : current_temp;
  out_snapshot->code = live_weather_code;
  out_snapshot->is_day = round_number(current_is_day) == 1;
  out_snapshot->thundersnow = thundersnow;
  describe_weather_code(
    live_weather_code,
    out_snapshot->is_day,
    thundersnow,
    out_snapshot->summary,
    sizeof(out_snapshot->summary)
  );
  snprintf(
    out_snapshot->icon_name,
    sizeof(out_snapshot->icon_name),
    "%s",
    weather_icon_name_for_conditions(live_weather_code, out_snapshot->is_day, thundersnow)
  );

  cJSON *daily_dates = cJSON_GetObjectItemCaseSensitive(daily, "time");
  int forecast_snow_threshold = snow_temp_threshold(config);

  for (int i = 0; i < APP_FORECAST_DAYS; ++i) {
    int high = 0;
    int low = 0;
    bool code_ok = false;
    int code = 3;
    const char *date = get_string_array_item(daily_dates, i + 1);

    if (!fill_temperature_range(daily, i + 1, &high, &low)) {
      cJSON_Delete(root);
      return ESP_FAIL;
    }

    code = read_daily_weather_code(daily, i + 1, &code_ok);
    if (!code_ok) {
      cJSON_Delete(root);
      return ESP_FAIL;
    }

    code = forecast_pick_representative_code(
      hourly,
      date,
      code,
      high,
      low,
      forecast_snow_threshold
    );

    bool forecast_thundersnow = is_thundersnow(config, code, (high + low) / 2);

    out_snapshot->forecast[i].high = high;
    out_snapshot->forecast[i].low = low;
    out_snapshot->forecast[i].code = code;
    out_snapshot->forecast[i].is_day = true;
    out_snapshot->forecast[i].thundersnow = forecast_thundersnow;
    describe_weather_code(
      code,
      true,
      forecast_thundersnow,
      out_snapshot->forecast[i].summary,
      sizeof(out_snapshot->forecast[i].summary)
    );
    snprintf(
      out_snapshot->forecast[i].icon_name,
      sizeof(out_snapshot->forecast[i].icon_name),
      "%s",
      weather_icon_name_for_conditions(code, true, forecast_thundersnow)
    );
  }

  cJSON_Delete(root);
  ESP_LOGI(TAG, "Fetched live weather: %dF/%dF/%dF code=%d", out_snapshot->temp, out_snapshot->high, out_snapshot->low, out_snapshot->code);
  return ESP_OK;
}
