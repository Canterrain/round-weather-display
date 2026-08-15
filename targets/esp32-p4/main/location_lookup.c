#include "location_lookup.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "location_scoring.h"

#define LOCATION_LOOKUP_URL_BUFFER_LEN 512
#define LOCATION_LOOKUP_HTTP_TIMEOUT_MS 10000
#define LOCATION_LOOKUP_MAX_RESULTS 10

typedef struct {
  char *data;
  size_t length;
} http_response_buffer_t;

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

static void copy_string(char *dest, size_t dest_size, const char *src)
{
  if (dest == NULL || dest_size == 0) {
    return;
  }

  if (src == NULL) {
    dest[0] = '\0';
    return;
  }

  snprintf(dest, dest_size, "%s", src);
}

static void copy_uppercase_string(char *dest, size_t dest_size, const char *src)
{
  if (dest == NULL || dest_size == 0) {
    return;
  }

  if (src == NULL) {
    dest[0] = '\0';
    return;
  }

  size_t index = 0;
  while (src[index] != '\0' && index + 1 < dest_size) {
    dest[index] = (char) toupper((unsigned char) src[index]);
    index++;
  }
  dest[index] = '\0';
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

esp_err_t location_lookup_resolve(const char *query, location_lookup_result_t *out_result)
{
  if (query == NULL || query[0] == '\0' || out_result == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  location_query_parts_t query_parts;
  location_scoring_parse_query(query, &query_parts);
  if (query_parts.name[0] == '\0') {
    return ESP_ERR_INVALID_ARG;
  }

  char encoded_query[128];
  url_encode_component(query_parts.name, encoded_query, sizeof(encoded_query));
  char country_code_filter[8];
  copy_uppercase_string(country_code_filter, sizeof(country_code_filter), query_parts.country);

  char url[LOCATION_LOOKUP_URL_BUFFER_LEN];
  snprintf(
    url,
    sizeof(url),
    "https://geocoding-api.open-meteo.com/v1/search"
    "?name=%s"
    "&count=%d"
    "&language=en"
    "&format=json%s%s",
    encoded_query,
    LOCATION_LOOKUP_MAX_RESULTS,
    query_parts.country_is_alpha2 ? "&countryCode=" : "",
    query_parts.country_is_alpha2 ? country_code_filter : ""
  );

  http_response_buffer_t response = {0};
  esp_http_client_config_t http_config = {
    .url = url,
    .event_handler = http_event_handler,
    .user_data = &response,
    .timeout_ms = LOCATION_LOOKUP_HTTP_TIMEOUT_MS,
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

  cJSON *results = cJSON_GetObjectItemCaseSensitive(root, "results");
  cJSON *result = location_scoring_select_best_result(results, &query_parts);
  if (!cJSON_IsObject(result)) {
    cJSON_Delete(root);
    return ESP_ERR_NOT_FOUND;
  }

  cJSON *name = cJSON_GetObjectItemCaseSensitive(result, "name");
  cJSON *admin1 = cJSON_GetObjectItemCaseSensitive(result, "admin1");
  cJSON *country_code = cJSON_GetObjectItemCaseSensitive(result, "country_code");
  cJSON *timezone = cJSON_GetObjectItemCaseSensitive(result, "timezone");
  cJSON *latitude = cJSON_GetObjectItemCaseSensitive(result, "latitude");
  cJSON *longitude = cJSON_GetObjectItemCaseSensitive(result, "longitude");

  if (!cJSON_IsString(name)
      || !cJSON_IsString(timezone)
      || !cJSON_IsNumber(latitude)
      || !cJSON_IsNumber(longitude)) {
    cJSON_Delete(root);
    return ESP_FAIL;
  }

  memset(out_result, 0, sizeof(*out_result));
  location_scoring_format_label(
    name->valuestring,
    cJSON_IsString(admin1) ? admin1->valuestring : NULL,
    cJSON_IsString(country_code) ? country_code->valuestring : NULL,
    out_result->location,
    sizeof(out_result->location)
  );
  copy_string(out_result->timezone, sizeof(out_result->timezone), timezone->valuestring);
  snprintf(out_result->latitude, sizeof(out_result->latitude), "%.4f", cJSON_GetNumberValue(latitude));
  snprintf(out_result->longitude, sizeof(out_result->longitude), "%.4f", cJSON_GetNumberValue(longitude));

  cJSON_Delete(root);
  return ESP_OK;
}
