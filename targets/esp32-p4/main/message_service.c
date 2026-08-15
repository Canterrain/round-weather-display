#include "message_service.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"

#include "cJSON.h"

#include "connectivity.h"

#define MESSAGE_SERVICE_PORT 80
#define MESSAGE_MAX_RECORDS 16
#define MESSAGE_MAX_ACKS 16
#define MESSAGE_MAX_TARGETS 8
#define MESSAGE_ISO_LEN 32
#define MESSAGE_MAX_HTTP_BODY 16384
#define MESSAGE_DISCOVERY_PORT 41234
#define MESSAGE_DISCOVERY_VERSION 1
#define MESSAGE_DISCOVERY_TIMEOUT_MS 3000
#define MESSAGE_HEARTBEAT_INTERVAL_MS 5000
#define MESSAGE_HEARTBEAT_STALE_MS 15000
#define MESSAGE_CLIENT_REFRESH_INTERVAL_MS 15000
#define MESSAGE_RUNTIME_POLL_INTERVAL_MS 250
#define MESSAGE_PROXY_TIMEOUT_MS 4000
#define MESSAGE_URL_LEN 1024
#define MESSAGE_HUB_URL_LEN 128
#define MESSAGE_LOCAL_IP_LEN 32

static const char *TAG = "rwd_messages";

typedef struct {
  bool in_use;
  bool active;
  bool important;
  char id[APP_MESSAGE_ID_LEN];
  char text[APP_MESSAGE_TEXT_LEN];
  char sender[APP_MESSAGE_SENDER_LEN];
  char target[DEVICE_CONFIG_STR_LEN];
  char created_at[MESSAGE_ISO_LEN];
  char expires_at[MESSAGE_ISO_LEN];
  time_t created_at_epoch;
  time_t expires_at_epoch;
  bool has_expires_at;
  char acknowledged_by[MESSAGE_MAX_ACKS][DEVICE_CONFIG_STR_LEN];
  size_t acknowledged_count;
} message_record_t;

typedef struct {
  char device_id[DEVICE_CONFIG_STR_LEN];
  char room_name[DEVICE_CONFIG_STR_LEN];
} message_target_t;

typedef enum {
  MESSAGE_ROLE_SINGLE = 0,
  MESSAGE_ROLE_SHARED_DISCOVERING,
  MESSAGE_ROLE_SHARED_HUB,
  MESSAGE_ROLE_SHARED_CLIENT,
} message_runtime_role_t;

typedef struct {
  char sharing_mode[DEVICE_CONFIG_SHORT_STR_LEN];
  message_runtime_role_t role;
  char device_id[DEVICE_CONFIG_STR_LEN];
  char room_name[DEVICE_CONFIG_STR_LEN];
  char local_ip[MESSAGE_LOCAL_IP_LEN];
  char local_hub_url[MESSAGE_HUB_URL_LEN];
  char hub_url[MESSAGE_HUB_URL_LEN];
  char hub_device_id[DEVICE_CONFIG_STR_LEN];
  char hub_room_name[DEVICE_CONFIG_STR_LEN];
  int64_t last_hub_heartbeat_ms;
  int64_t last_heartbeat_sent_ms;
  int64_t last_client_refresh_ms;
  int64_t discovery_deadline_ms;
  int socket_fd;
} message_runtime_state_t;

typedef struct {
  bool valid;
  uint32_t unread_count;
  bool has_important;
  app_message_snapshot_t snapshot;
} message_client_cache_t;

typedef struct {
  char device_id[DEVICE_CONFIG_STR_LEN];
  char room_name[DEVICE_CONFIG_STR_LEN];
} request_known_device_t;

typedef struct {
  char *data;
  size_t length;
  size_t capacity;
} http_response_buffer_t;

static SemaphoreHandle_t s_message_mutex;
static device_config_t s_config_snapshot;
static httpd_handle_t s_http_server;
static message_record_t s_messages[MESSAGE_MAX_RECORDS];
static message_target_t s_targets[MESSAGE_MAX_TARGETS];
static size_t s_target_count;
static message_runtime_state_t s_runtime;
static message_client_cache_t s_client_cache;
static TaskHandle_t s_runtime_task_handle;

static void copy_text(char *dest, size_t dest_size, const char *src)
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

static int compare_hub_priority(const char *device_id_a, const char *device_id_b)
{
  return strcmp(device_id_a != NULL ? device_id_a : "", device_id_b != NULL ? device_id_b : "");
}

static int64_t now_ms(void)
{
  return esp_timer_get_time() / 1000;
}

static const char *message_role_name(message_runtime_role_t role)
{
  switch (role) {
    case MESSAGE_ROLE_SINGLE:
      return "single";
    case MESSAGE_ROLE_SHARED_DISCOVERING:
      return "shared-discovering";
    case MESSAGE_ROLE_SHARED_HUB:
      return "shared-hub";
    case MESSAGE_ROLE_SHARED_CLIENT:
      return "shared-client";
    default:
      return "single";
  }
}

static bool runtime_is_shared_locked(void)
{
  return strings_equal_ignore_case(s_runtime.sharing_mode, "shared");
}

static bool runtime_should_proxy_locked(void)
{
  return runtime_is_shared_locked()
    && s_runtime.role == MESSAGE_ROLE_SHARED_CLIENT
    && s_runtime.hub_url[0] != '\0'
    && strcmp(s_runtime.hub_url, s_runtime.local_hub_url) != 0;
}

static void clear_client_cache_locked(void)
{
  memset(&s_client_cache, 0, sizeof(s_client_cache));
}

static void update_known_target(const char *device_id, const char *room_name)
{
  if (device_id == NULL || device_id[0] == '\0' || strcmp(device_id, "all") == 0) {
    return;
  }

  for (size_t index = 0; index < s_target_count; ++index) {
    if (strcmp(s_targets[index].device_id, device_id) == 0) {
      if (room_name != NULL && room_name[0] != '\0') {
        copy_text(s_targets[index].room_name, sizeof(s_targets[index].room_name), room_name);
      }
      return;
    }
  }

  if (s_target_count >= MESSAGE_MAX_TARGETS) {
    return;
  }

  copy_text(s_targets[s_target_count].device_id, sizeof(s_targets[s_target_count].device_id), device_id);
  copy_text(
    s_targets[s_target_count].room_name,
    sizeof(s_targets[s_target_count].room_name),
    (room_name != NULL && room_name[0] != '\0') ? room_name : device_id
  );
  s_target_count++;
}

static int64_t days_from_civil(int year, unsigned month, unsigned day)
{
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = (unsigned) (year - era * 400);
  const unsigned doy = (153 * (month + (month > 2 ? (unsigned) -3 : 9)) + 2) / 5 + day - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int64_t) doe - 719468;
}

static bool parse_iso_time(const char *iso, time_t *out_epoch)
{
  if (iso == NULL || out_epoch == NULL) {
    return false;
  }

  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;

  if (sscanf(iso, "%4d-%2d-%2dT%2d:%2d:%2d", &year, &month, &day, &hour, &minute, &second) != 6) {
    return false;
  }

  if (month < 1 || month > 12 || day < 1 || day > 31
      || hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 60) {
    return false;
  }

  int64_t days = days_from_civil(year, (unsigned) month, (unsigned) day);
  int64_t seconds_since_epoch = (days * 86400) + (hour * 3600) + (minute * 60) + second;
  *out_epoch = (time_t) seconds_since_epoch;
  return true;
}

static void format_iso_time(time_t epoch, char *out, size_t out_size)
{
  if (out == NULL || out_size == 0) {
    return;
  }

  struct tm utc_time = {0};
  if (gmtime_r(&epoch, &utc_time) == NULL) {
    out[0] = '\0';
    return;
  }

  strftime(out, out_size, "%Y-%m-%dT%H:%M:%SZ", &utc_time);
}

static void format_message_meta_values(
  const char *sender,
  const char *created_at_iso,
  char *out,
  size_t out_size
)
{
  if (out == NULL || out_size == 0) {
    return;
  }

  out[0] = '\0';

  char time_label[16] = "";
  time_t created_at_epoch = 0;
  if (created_at_iso != NULL && created_at_iso[0] != '\0' && parse_iso_time(created_at_iso, &created_at_epoch)) {
    struct tm local_time = {0};
    if (localtime_r(&created_at_epoch, &local_time) != NULL) {
      strftime(time_label, sizeof(time_label), "%I:%M %p", &local_time);
      if (time_label[0] == '0') {
        memmove(time_label, time_label + 1, strlen(time_label));
      }
    }
  }

  if (sender != NULL && sender[0] != '\0' && time_label[0] != '\0') {
    snprintf(out, out_size, "-%s, %s", sender, time_label);
  } else if (sender != NULL && sender[0] != '\0') {
    snprintf(out, out_size, "-%s", sender);
  } else if (time_label[0] != '\0') {
    snprintf(out, out_size, "%s", time_label);
  }
}

static void format_message_meta(const message_record_t *record, char *out, size_t out_size)
{
  if (record == NULL) {
    if (out != NULL && out_size > 0) {
      out[0] = '\0';
    }
    return;
  }

  format_message_meta_values(record->sender, record->created_at, out, out_size);
}

static bool is_message_expired(const message_record_t *record)
{
  if (record == NULL || !record->has_expires_at || record->expires_at_epoch <= 0) {
    return false;
  }

  return record->expires_at_epoch <= time(NULL);
}

static bool is_visible_to_device(const message_record_t *record, const char *device_id)
{
  if (record == NULL || !record->in_use || !record->active || is_message_expired(record)) {
    return false;
  }

  if (strcmp(record->target, "all") == 0) {
    return true;
  }

  return device_id != NULL && device_id[0] != '\0' && strcmp(record->target, device_id) == 0;
}

static bool is_acknowledged_for_device(const message_record_t *record, const char *device_id)
{
  if (record == NULL || device_id == NULL || device_id[0] == '\0') {
    return false;
  }

  for (size_t index = 0; index < record->acknowledged_count; ++index) {
    if (strcmp(record->acknowledged_by[index], device_id) == 0) {
      return true;
    }
  }

  return false;
}

static int compare_message_order(const message_record_t *a, const message_record_t *b)
{
  if (a == NULL || b == NULL) {
    return 0;
  }

  if (a->important != b->important) {
    return a->important ? -1 : 1;
  }

  if (a->created_at_epoch == b->created_at_epoch) {
    return strcmp(a->id, b->id);
  }

  return (a->created_at_epoch > b->created_at_epoch) ? -1 : 1;
}

static int collect_visible_message_indexes(
  const char *device_id,
  bool include_dismissed,
  int *indexes,
  size_t max_indexes,
  uint32_t *out_unread_count,
  bool *out_has_important
)
{
  uint32_t unread_count = 0;
  bool has_important = false;
  int count = 0;

  for (size_t index = 0; index < MESSAGE_MAX_RECORDS; ++index) {
    message_record_t *record = &s_messages[index];
    if (!is_visible_to_device(record, device_id)) {
      continue;
    }

    bool acknowledged = is_acknowledged_for_device(record, device_id);
    if (!acknowledged) {
      unread_count++;
      if (record->important) {
        has_important = true;
      }
    }

    if (!include_dismissed && acknowledged) {
      continue;
    }

    if (count < (int) max_indexes) {
      indexes[count++] = (int) index;
    }
  }

  for (int i = 0; i < count; ++i) {
    for (int j = i + 1; j < count; ++j) {
      if (compare_message_order(&s_messages[indexes[i]], &s_messages[indexes[j]]) > 0) {
        int temp = indexes[i];
        indexes[i] = indexes[j];
        indexes[j] = temp;
      }
    }
  }

  if (out_unread_count != NULL) {
    *out_unread_count = unread_count;
  }
  if (out_has_important != NULL) {
    *out_has_important = has_important;
  }

  return count;
}

static int find_message_index_by_id(const char *message_id)
{
  if (message_id == NULL || message_id[0] == '\0') {
    return -1;
  }

  for (size_t index = 0; index < MESSAGE_MAX_RECORDS; ++index) {
    if (s_messages[index].in_use && strcmp(s_messages[index].id, message_id) == 0) {
      return (int) index;
    }
  }

  return -1;
}

static int find_insertion_slot(void)
{
  int oldest_index = 0;

  for (size_t index = 0; index < MESSAGE_MAX_RECORDS; ++index) {
    if (!s_messages[index].in_use) {
      return (int) index;
    }

    if (s_messages[index].created_at_epoch < s_messages[oldest_index].created_at_epoch) {
      oldest_index = (int) index;
    }
  }

  return oldest_index;
}

static void copy_message_record(message_record_t *dest, const message_record_t *src)
{
  if (dest == NULL || src == NULL) {
    return;
  }

  *dest = *src;
}

static bool parse_message_json(cJSON *json, message_record_t *out_record)
{
  if (json == NULL || out_record == NULL) {
    return false;
  }

  cJSON *id_item = cJSON_GetObjectItemCaseSensitive(json, "id");
  cJSON *text_item = cJSON_GetObjectItemCaseSensitive(json, "text");
  cJSON *sender_item = cJSON_GetObjectItemCaseSensitive(json, "sender");
  cJSON *created_item = cJSON_GetObjectItemCaseSensitive(json, "createdAt");
  cJSON *expires_item = cJSON_GetObjectItemCaseSensitive(json, "expiresAt");
  cJSON *target_item = cJSON_GetObjectItemCaseSensitive(json, "target");
  cJSON *priority_item = cJSON_GetObjectItemCaseSensitive(json, "priority");
  cJSON *active_item = cJSON_GetObjectItemCaseSensitive(json, "active");
  cJSON *acks_item = cJSON_GetObjectItemCaseSensitive(json, "acknowledgedBy");

  if (!cJSON_IsString(id_item) || !cJSON_IsString(text_item) || !cJSON_IsString(created_item)) {
    return false;
  }

  memset(out_record, 0, sizeof(*out_record));
  out_record->in_use = true;
  out_record->active = !cJSON_IsBool(active_item) || cJSON_IsTrue(active_item);
  out_record->important = cJSON_IsString(priority_item)
    && strings_equal_ignore_case(priority_item->valuestring, "important");

  copy_text(out_record->id, sizeof(out_record->id), id_item->valuestring);
  copy_text(out_record->text, sizeof(out_record->text), text_item->valuestring);
  copy_text(
    out_record->sender,
    sizeof(out_record->sender),
    cJSON_IsString(sender_item) ? sender_item->valuestring : ""
  );
  copy_text(out_record->created_at, sizeof(out_record->created_at), created_item->valuestring);
  (void) parse_iso_time(out_record->created_at, &out_record->created_at_epoch);
  copy_text(
    out_record->target,
    sizeof(out_record->target),
    cJSON_IsString(target_item) && target_item->valuestring[0] != '\0' ? target_item->valuestring : "all"
  );

  if (cJSON_IsString(expires_item) && expires_item->valuestring[0] != '\0') {
    out_record->has_expires_at = parse_iso_time(expires_item->valuestring, &out_record->expires_at_epoch);
    if (out_record->has_expires_at) {
      copy_text(out_record->expires_at, sizeof(out_record->expires_at), expires_item->valuestring);
    }
  }

  if (cJSON_IsArray(acks_item)) {
    size_t ack_count = 0;
    cJSON *ack_item = NULL;
    cJSON_ArrayForEach(ack_item, acks_item) {
      if (!cJSON_IsString(ack_item) || ack_item->valuestring == NULL || ack_item->valuestring[0] == '\0') {
        continue;
      }
      bool duplicate = false;
      for (size_t index = 0; index < ack_count; ++index) {
        if (strcmp(out_record->acknowledged_by[index], ack_item->valuestring) == 0) {
          duplicate = true;
          break;
        }
      }
      if (duplicate || ack_count >= MESSAGE_MAX_ACKS) {
        continue;
      }
      copy_text(
        out_record->acknowledged_by[ack_count],
        sizeof(out_record->acknowledged_by[ack_count]),
        ack_item->valuestring
      );
      ack_count++;
    }
    out_record->acknowledged_count = ack_count;
  }

  return true;
}

static void merge_message_record_locked(const message_record_t *incoming)
{
  if (incoming == NULL || !incoming->in_use || incoming->id[0] == '\0') {
    return;
  }

  int index = find_message_index_by_id(incoming->id);
  if (index < 0) {
    index = find_insertion_slot();
    memset(&s_messages[index], 0, sizeof(s_messages[index]));
    copy_message_record(&s_messages[index], incoming);
    return;
  }

  message_record_t *existing = &s_messages[index];
  if (!existing->in_use) {
    copy_message_record(existing, incoming);
    return;
  }

  if (existing->text[0] == '\0' && incoming->text[0] != '\0') {
    copy_text(existing->text, sizeof(existing->text), incoming->text);
  }
  if (existing->sender[0] == '\0' && incoming->sender[0] != '\0') {
    copy_text(existing->sender, sizeof(existing->sender), incoming->sender);
  }
  if (existing->target[0] == '\0' && incoming->target[0] != '\0') {
    copy_text(existing->target, sizeof(existing->target), incoming->target);
  }
  if (existing->created_at[0] == '\0' && incoming->created_at[0] != '\0') {
    copy_text(existing->created_at, sizeof(existing->created_at), incoming->created_at);
    existing->created_at_epoch = incoming->created_at_epoch;
  }
  if (!existing->has_expires_at && incoming->has_expires_at) {
    existing->has_expires_at = true;
    existing->expires_at_epoch = incoming->expires_at_epoch;
    copy_text(existing->expires_at, sizeof(existing->expires_at), incoming->expires_at);
  }

  existing->important = existing->important || incoming->important;
  existing->active = existing->active && incoming->active;

  for (size_t index_ack = 0; index_ack < incoming->acknowledged_count; ++index_ack) {
    bool duplicate = false;
    for (size_t existing_ack = 0; existing_ack < existing->acknowledged_count; ++existing_ack) {
      if (strcmp(existing->acknowledged_by[existing_ack], incoming->acknowledged_by[index_ack]) == 0) {
        duplicate = true;
        break;
      }
    }
    if (duplicate || existing->acknowledged_count >= MESSAGE_MAX_ACKS) {
      continue;
    }
    copy_text(
      existing->acknowledged_by[existing->acknowledged_count],
      sizeof(existing->acknowledged_by[existing->acknowledged_count]),
      incoming->acknowledged_by[index_ack]
    );
    existing->acknowledged_count++;
  }
}

static cJSON *message_record_to_json(const message_record_t *record)
{
  if (record == NULL || !record->in_use) {
    return NULL;
  }

  cJSON *message = cJSON_CreateObject();
  if (message == NULL) {
    return NULL;
  }

  cJSON_AddStringToObject(message, "id", record->id);
  cJSON_AddStringToObject(message, "text", record->text);
  cJSON_AddStringToObject(message, "sender", record->sender);
  cJSON_AddStringToObject(message, "createdAt", record->created_at);
  if (record->has_expires_at) {
    cJSON_AddStringToObject(message, "expiresAt", record->expires_at);
  } else {
    cJSON_AddNullToObject(message, "expiresAt");
  }
  cJSON_AddStringToObject(message, "target", record->target);
  cJSON_AddStringToObject(message, "priority", record->important ? "important" : "normal");
  cJSON_AddBoolToObject(message, "active", record->active);
  cJSON *acks = cJSON_AddArrayToObject(message, "acknowledgedBy");
  for (size_t index = 0; index < record->acknowledged_count; ++index) {
    cJSON_AddItemToArray(acks, cJSON_CreateString(record->acknowledged_by[index]));
  }

  return message;
}

static cJSON *build_store_messages_array_locked(void)
{
  cJSON *messages = cJSON_CreateArray();
  if (messages == NULL) {
    return NULL;
  }

  for (size_t index = 0; index < MESSAGE_MAX_RECORDS; ++index) {
    if (!s_messages[index].in_use) {
      continue;
    }

    cJSON *message = message_record_to_json(&s_messages[index]);
    if (message == NULL) {
      cJSON_Delete(messages);
      return NULL;
    }
    cJSON_AddItemToArray(messages, message);
  }

  return messages;
}

static esp_err_t read_request_body_alloc(httpd_req_t *req, char **out_buffer)
{
  if (req == NULL || out_buffer == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  *out_buffer = NULL;

  if (req->content_len > MESSAGE_MAX_HTTP_BODY) {
    return ESP_ERR_INVALID_SIZE;
  }

  size_t required = (size_t) req->content_len;
  char *buffer = calloc(required + 1, sizeof(char));
  if (buffer == NULL) {
    return ESP_ERR_NO_MEM;
  }

  size_t received = 0;
  while (received < required) {
    int ret = httpd_req_recv(req, buffer + received, required - received);
    if (ret <= 0) {
      free(buffer);
      return ESP_FAIL;
    }
    received += (size_t) ret;
  }

  buffer[received] = '\0';
  *out_buffer = buffer;
  return ESP_OK;
}

static const char *http_status_string(int status_code)
{
  switch (status_code) {
    case 200:
      return "200 OK";
    case 201:
      return "201 Created";
    case 400:
      return "400 Bad Request";
    case 404:
      return "404 Not Found";
    case 409:
      return "409 Conflict";
    case 500:
      return "500 Internal Server Error";
    case 501:
      return "501 Not Implemented";
    case 502:
      return "502 Bad Gateway";
    default:
      return "500 Internal Server Error";
  }
}

static esp_err_t send_json_response(httpd_req_t *req, int status_code, cJSON *json)
{
  if (req == NULL || json == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  char *body = cJSON_PrintUnformatted(json);
  cJSON_Delete(json);
  if (body == NULL) {
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON encode failed");
  }

  httpd_resp_set_status(req, http_status_string(status_code));
  httpd_resp_set_type(req, "application/json");
  esp_err_t err = httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
  free(body);
  return err;
}

static esp_err_t send_error_response(httpd_req_t *req, int status_code, const char *message)
{
  cJSON *json = cJSON_CreateObject();
  if (json == NULL) {
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON allocate failed");
  }

  cJSON_AddStringToObject(json, "error", message != NULL ? message : "Unknown error");
  return send_json_response(req, status_code, json);
}

static bool request_method_has_body(httpd_method_t method)
{
  return method != HTTP_GET && method != HTTP_HEAD;
}

static esp_err_t http_response_buffer_append(
  http_response_buffer_t *buffer,
  const char *data,
  size_t length
)
{
  if (buffer == NULL || data == NULL || length == 0) {
    return ESP_OK;
  }

  size_t required = buffer->length + length + 1;
  if (required > buffer->capacity) {
    size_t next_capacity = buffer->capacity == 0 ? 1024 : buffer->capacity;
    while (next_capacity < required) {
      next_capacity *= 2;
    }
    char *next = realloc(buffer->data, next_capacity);
    if (next == NULL) {
      return ESP_ERR_NO_MEM;
    }
    buffer->data = next;
    buffer->capacity = next_capacity;
  }

  memcpy(buffer->data + buffer->length, data, length);
  buffer->length += length;
  buffer->data[buffer->length] = '\0';
  return ESP_OK;
}

static esp_err_t proxied_http_event_handler(esp_http_client_event_t *event)
{
  if (event == NULL || event->user_data == NULL) {
    return ESP_OK;
  }

  http_response_buffer_t *buffer = (http_response_buffer_t *) event->user_data;
  if (event->event_id == HTTP_EVENT_ON_DATA && event->data != NULL && event->data_len > 0) {
    return http_response_buffer_append(buffer, (const char *) event->data, (size_t) event->data_len);
  }

  return ESP_OK;
}

static esp_err_t perform_http_request(
  const char *url,
  httpd_method_t method,
  const char *body,
  const char *device_id,
  const char *room_name,
  int *out_status_code,
  http_response_buffer_t *out_response
)
{
  if (url == NULL || out_status_code == NULL || out_response == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  memset(out_response, 0, sizeof(*out_response));

  esp_http_client_config_t http_config = {
    .url = url,
    .event_handler = proxied_http_event_handler,
    .user_data = out_response,
    .timeout_ms = MESSAGE_PROXY_TIMEOUT_MS,
    .buffer_size = 1024,
    .crt_bundle_attach = NULL,
  };

  esp_http_client_handle_t client = esp_http_client_init(&http_config);
  if (client == NULL) {
    return ESP_FAIL;
  }

  esp_http_client_set_method(
    client,
    method == HTTP_POST ? HTTP_METHOD_POST
      : method == HTTP_PUT ? HTTP_METHOD_PUT
      : method == HTTP_DELETE ? HTTP_METHOD_DELETE
      : HTTP_METHOD_GET
  );
  esp_http_client_set_header(client, "Content-Type", "application/json");
  if (device_id != NULL && device_id[0] != '\0') {
    esp_http_client_set_header(client, "x-clock-device-id", device_id);
  }
  if (room_name != NULL && room_name[0] != '\0') {
    esp_http_client_set_header(client, "x-clock-room-name", room_name);
  }
  if (request_method_has_body(method) && body != NULL) {
    esp_http_client_set_post_field(client, body, (int) strlen(body));
  }

  esp_err_t err = esp_http_client_perform(client);
  *out_status_code = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);

  if (out_response->data == NULL) {
    out_response->data = calloc(1, sizeof(char));
    if (out_response->data != NULL) {
      out_response->capacity = 1;
    }
  }

  if (err != ESP_OK) {
    free(out_response->data);
    memset(out_response, 0, sizeof(*out_response));
  }

  return err;
}

static void free_http_response_buffer(http_response_buffer_t *buffer)
{
  if (buffer == NULL) {
    return;
  }

  free(buffer->data);
  buffer->data = NULL;
  buffer->length = 0;
  buffer->capacity = 0;
}

static void read_request_known_device(httpd_req_t *req, request_known_device_t *out_device)
{
  if (out_device == NULL) {
    return;
  }

  memset(out_device, 0, sizeof(*out_device));
  if (req == NULL) {
    return;
  }

  (void) httpd_req_get_hdr_value_str(req, "x-clock-device-id", out_device->device_id, sizeof(out_device->device_id));
  (void) httpd_req_get_hdr_value_str(req, "x-clock-room-name", out_device->room_name, sizeof(out_device->room_name));
}

static esp_err_t proxy_request_to_hub(httpd_req_t *req)
{
  char hub_url[MESSAGE_HUB_URL_LEN] = "";
  char device_id[DEVICE_CONFIG_STR_LEN] = "";
  char room_name[DEVICE_CONFIG_STR_LEN] = "";

  xSemaphoreTake(s_message_mutex, portMAX_DELAY);
  if (!runtime_should_proxy_locked()) {
    xSemaphoreGive(s_message_mutex);
    return ESP_ERR_INVALID_STATE;
  }
  copy_text(hub_url, sizeof(hub_url), s_runtime.hub_url);
  copy_text(device_id, sizeof(device_id), s_runtime.device_id);
  copy_text(room_name, sizeof(room_name), s_runtime.room_name);
  xSemaphoreGive(s_message_mutex);

  char *body = NULL;
  if (request_method_has_body(req->method)) {
    esp_err_t read_err = read_request_body_alloc(req, &body);
    if (read_err == ESP_ERR_INVALID_SIZE) {
      return send_error_response(req, 400, "Request body is too large");
    }
    if (read_err != ESP_OK) {
      return send_error_response(req, 400, "Could not read request body");
    }
  }

  char url[MESSAGE_URL_LEN] = "";
  int query_len = httpd_req_get_url_query_len(req);
  if (query_len > 0) {
    char *query = calloc((size_t) query_len + 1, sizeof(char));
    if (query == NULL) {
      free(body);
      return send_error_response(req, 500, "Out of memory");
    }
    if (httpd_req_get_url_query_str(req, query, (size_t) query_len + 1) == ESP_OK) {
      snprintf(url, sizeof(url), "%s%s?%s", hub_url, req->uri, query);
    } else {
      snprintf(url, sizeof(url), "%s%s", hub_url, req->uri);
    }
    free(query);
  } else {
    snprintf(url, sizeof(url), "%s%s", hub_url, req->uri);
  }

  int status_code = 500;
  http_response_buffer_t response = {0};
  esp_err_t request_err = perform_http_request(
    url,
    req->method,
    body,
    device_id,
    room_name,
    &status_code,
    &response
  );
  free(body);

  if (request_err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to proxy message request to %s: %s", url, esp_err_to_name(request_err));
    free_http_response_buffer(&response);
    return send_error_response(req, 502, "Message hub unavailable");
  }

  httpd_resp_set_status(req, http_status_string(status_code));
  httpd_resp_set_type(req, "application/json");
  esp_err_t send_err = httpd_resp_send(
    req,
    response.data != NULL ? response.data : "",
    response.data != NULL ? HTTPD_RESP_USE_STRLEN : 0
  );
  free_http_response_buffer(&response);
  return send_err;
}

static esp_err_t refresh_remote_client_cache(void)
{
  char hub_url[MESSAGE_HUB_URL_LEN] = "";
  char device_id[DEVICE_CONFIG_STR_LEN] = "";
  char room_name[DEVICE_CONFIG_STR_LEN] = "";

  xSemaphoreTake(s_message_mutex, portMAX_DELAY);
  if (!runtime_should_proxy_locked()) {
    clear_client_cache_locked();
    xSemaphoreGive(s_message_mutex);
    return ESP_ERR_INVALID_STATE;
  }
  copy_text(hub_url, sizeof(hub_url), s_runtime.hub_url);
  copy_text(device_id, sizeof(device_id), s_runtime.device_id);
  copy_text(room_name, sizeof(room_name), s_runtime.room_name);
  xSemaphoreGive(s_message_mutex);

  char url[MESSAGE_URL_LEN];
  snprintf(url, sizeof(url), "%s/api/messages?deviceId=%s", hub_url, device_id);

  int status_code = 500;
  http_response_buffer_t response = {0};
  esp_err_t request_err = perform_http_request(url, HTTP_GET, NULL, device_id, room_name, &status_code, &response);
  if (request_err != ESP_OK || status_code < 200 || status_code >= 300 || response.data == NULL) {
    if (request_err != ESP_OK) {
      ESP_LOGW(TAG, "Shared message poll failed: %s", esp_err_to_name(request_err));
    } else {
      ESP_LOGW(TAG, "Shared message poll returned HTTP %d", status_code);
    }
    free_http_response_buffer(&response);
    return request_err != ESP_OK ? request_err : ESP_FAIL;
  }

  cJSON *root = cJSON_Parse(response.data);
  free_http_response_buffer(&response);
  if (root == NULL) {
    return ESP_FAIL;
  }

  message_client_cache_t next_cache = {0};
  cJSON *unread_item = cJSON_GetObjectItemCaseSensitive(root, "unreadCount");
  cJSON *messages_item = cJSON_GetObjectItemCaseSensitive(root, "messages");
  next_cache.valid = true;
  next_cache.unread_count = cJSON_IsNumber(unread_item) ? (uint32_t) unread_item->valuedouble : 0;

  if (cJSON_IsArray(messages_item)) {
    cJSON *message_item = NULL;
    cJSON_ArrayForEach(message_item, messages_item) {
      cJSON *priority_item = cJSON_GetObjectItemCaseSensitive(message_item, "priority");
      bool important = cJSON_IsString(priority_item)
        && strings_equal_ignore_case(priority_item->valuestring, "important");
      if (important) {
        next_cache.has_important = true;
      }

      if (next_cache.snapshot.available) {
        continue;
      }

      cJSON *id_item = cJSON_GetObjectItemCaseSensitive(message_item, "id");
      cJSON *text_item = cJSON_GetObjectItemCaseSensitive(message_item, "text");
      cJSON *sender_item = cJSON_GetObjectItemCaseSensitive(message_item, "sender");
      cJSON *created_item = cJSON_GetObjectItemCaseSensitive(message_item, "createdAt");
      if (!cJSON_IsString(id_item) || !cJSON_IsString(text_item)) {
        continue;
      }

      next_cache.snapshot.available = true;
      next_cache.snapshot.important = important;
      copy_text(next_cache.snapshot.id, sizeof(next_cache.snapshot.id), id_item->valuestring);
      copy_text(next_cache.snapshot.text, sizeof(next_cache.snapshot.text), text_item->valuestring);
      copy_text(
        next_cache.snapshot.sender,
        sizeof(next_cache.snapshot.sender),
        cJSON_IsString(sender_item) ? sender_item->valuestring : ""
      );
      format_message_meta_values(
        next_cache.snapshot.sender,
        cJSON_IsString(created_item) ? created_item->valuestring : "",
        next_cache.snapshot.meta,
        sizeof(next_cache.snapshot.meta)
      );
    }
  }

  cJSON_Delete(root);

  xSemaphoreTake(s_message_mutex, portMAX_DELAY);
  s_client_cache = next_cache;
  s_runtime.last_client_refresh_ms = now_ms();
  xSemaphoreGive(s_message_mutex);
  return ESP_OK;
}

static esp_err_t sync_local_store_to_hub(const char *hub_url)
{
  if (hub_url == NULL || hub_url[0] == '\0') {
    return ESP_ERR_INVALID_ARG;
  }

  cJSON *root = cJSON_CreateObject();
  if (root == NULL) {
    return ESP_ERR_NO_MEM;
  }

  xSemaphoreTake(s_message_mutex, portMAX_DELAY);
  cJSON *messages = build_store_messages_array_locked();
  xSemaphoreGive(s_message_mutex);

  if (messages == NULL) {
    cJSON_Delete(root);
    return ESP_ERR_NO_MEM;
  }
  cJSON_AddItemToObject(root, "messages", messages);

  char *body = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (body == NULL) {
    return ESP_ERR_NO_MEM;
  }

  char url[MESSAGE_URL_LEN];
  snprintf(url, sizeof(url), "%s/api/messages/sync", hub_url);

  int status_code = 500;
  http_response_buffer_t response = {0};
  esp_err_t err = perform_http_request(
    url,
    HTTP_POST,
    body,
    s_runtime.device_id,
    s_runtime.room_name,
    &status_code,
    &response
  );
  free(body);
  free_http_response_buffer(&response);

  if (err != ESP_OK || status_code < 200 || status_code >= 300) {
    return err != ESP_OK ? err : ESP_FAIL;
  }

  return ESP_OK;
}

static void runtime_close_socket_locked(void)
{
  if (s_runtime.socket_fd >= 0) {
    close(s_runtime.socket_fd);
    s_runtime.socket_fd = -1;
  }
}

static void runtime_update_local_address_locked(const char *local_ip)
{
  copy_text(s_runtime.local_ip, sizeof(s_runtime.local_ip), local_ip != NULL ? local_ip : "");
  if (s_runtime.local_ip[0] != '\0') {
    snprintf(
      s_runtime.local_hub_url,
      sizeof(s_runtime.local_hub_url),
      "http://%s:%d",
      s_runtime.local_ip,
      MESSAGE_SERVICE_PORT
    );
  } else {
    s_runtime.local_hub_url[0] = '\0';
  }

  if (s_runtime.role == MESSAGE_ROLE_SINGLE || s_runtime.role == MESSAGE_ROLE_SHARED_HUB) {
    copy_text(s_runtime.hub_url, sizeof(s_runtime.hub_url), s_runtime.local_hub_url);
    copy_text(s_runtime.hub_device_id, sizeof(s_runtime.hub_device_id), s_runtime.device_id);
    copy_text(s_runtime.hub_room_name, sizeof(s_runtime.hub_room_name), s_runtime.room_name);
  }
}

static esp_err_t runtime_send_discovery_payload_locked(const char *type, const struct sockaddr_in *destination)
{
  if (type == NULL || destination == NULL || s_runtime.socket_fd < 0 || s_runtime.local_hub_url[0] == '\0') {
    return ESP_ERR_INVALID_STATE;
  }

  char payload[320];
  int written = snprintf(
    payload,
    sizeof(payload),
    "{\"type\":\"%s\",\"version\":%d,\"deviceId\":\"%s\",\"roomName\":\"%s\",\"hubUrl\":\"%s\"}",
    type,
    MESSAGE_DISCOVERY_VERSION,
    s_runtime.device_id,
    s_runtime.room_name,
    s_runtime.local_hub_url
  );
  if (written <= 0 || written >= (int) sizeof(payload)) {
    return ESP_ERR_INVALID_SIZE;
  }

  ssize_t send_result = sendto(
    s_runtime.socket_fd,
    payload,
    (size_t) written,
    0,
    (const struct sockaddr *) destination,
    sizeof(*destination)
  );
  if (send_result < 0) {
    ESP_LOGW(TAG, "Discovery send failed: errno=%d", errno);
    return ESP_FAIL;
  }

  return ESP_OK;
}

static void runtime_start_discovery_locked(int64_t current_ms)
{
  if (!runtime_is_shared_locked() || s_runtime.socket_fd < 0 || s_runtime.local_hub_url[0] == '\0') {
    return;
  }

  if (s_runtime.role != MESSAGE_ROLE_SHARED_HUB) {
    s_runtime.role = MESSAGE_ROLE_SHARED_DISCOVERING;
    s_runtime.hub_url[0] = '\0';
    s_runtime.hub_device_id[0] = '\0';
    s_runtime.hub_room_name[0] = '\0';
    s_runtime.last_hub_heartbeat_ms = 0;
    clear_client_cache_locked();
  }

  s_runtime.discovery_deadline_ms = current_ms + MESSAGE_DISCOVERY_TIMEOUT_MS;

  struct sockaddr_in destination = {0};
  destination.sin_family = AF_INET;
  destination.sin_port = htons(MESSAGE_DISCOVERY_PORT);
  destination.sin_addr.s_addr = inet_addr("255.255.255.255");

  (void) runtime_send_discovery_payload_locked("weather-clock-message-discovery", &destination);
  ESP_LOGI(TAG, "Shared message discovery started");
}

static void runtime_become_shared_hub_locked(int64_t current_ms)
{
  if (!runtime_is_shared_locked()) {
    return;
  }

  s_runtime.role = MESSAGE_ROLE_SHARED_HUB;
  s_runtime.discovery_deadline_ms = 0;
  s_runtime.last_hub_heartbeat_ms = current_ms;
  copy_text(s_runtime.hub_url, sizeof(s_runtime.hub_url), s_runtime.local_hub_url);
  copy_text(s_runtime.hub_device_id, sizeof(s_runtime.hub_device_id), s_runtime.device_id);
  copy_text(s_runtime.hub_room_name, sizeof(s_runtime.hub_room_name), s_runtime.room_name);
  clear_client_cache_locked();
  update_known_target(s_runtime.device_id, s_runtime.room_name);
  ESP_LOGI(TAG, "Shared message runtime became hub at %s", s_runtime.hub_url);
}

static void runtime_become_shared_client_locked(
  const char *hub_url,
  const char *hub_device_id,
  const char *hub_room_name,
  int64_t current_ms,
  bool *out_should_sync,
  char *out_sync_url,
  size_t out_sync_url_size
)
{
  bool should_sync = false;
  if (s_runtime.role == MESSAGE_ROLE_SHARED_HUB
      && hub_url != NULL
      && hub_url[0] != '\0'
      && strcmp(hub_url, s_runtime.local_hub_url) != 0) {
    should_sync = true;
  }

  s_runtime.role = MESSAGE_ROLE_SHARED_CLIENT;
  s_runtime.discovery_deadline_ms = 0;
  s_runtime.last_hub_heartbeat_ms = current_ms;
  copy_text(s_runtime.hub_url, sizeof(s_runtime.hub_url), hub_url);
  copy_text(s_runtime.hub_device_id, sizeof(s_runtime.hub_device_id), hub_device_id);
  copy_text(
    s_runtime.hub_room_name,
    sizeof(s_runtime.hub_room_name),
    (hub_room_name != NULL && hub_room_name[0] != '\0') ? hub_room_name : hub_device_id
  );
  clear_client_cache_locked();
  update_known_target(s_runtime.hub_device_id, s_runtime.hub_room_name);

  if (out_should_sync != NULL) {
    *out_should_sync = should_sync;
  }
  if (should_sync && out_sync_url != NULL && out_sync_url_size > 0) {
    copy_text(out_sync_url, out_sync_url_size, hub_url);
  }

  ESP_LOGI(
    TAG,
    "Shared message runtime became client of %s (%s)",
    s_runtime.hub_device_id,
    s_runtime.hub_url
  );
}

static esp_err_t runtime_ensure_socket_locked(int64_t current_ms)
{
  if (!runtime_is_shared_locked()) {
    return ESP_OK;
  }

  if (s_runtime.socket_fd >= 0) {
    return ESP_OK;
  }

  int socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (socket_fd < 0) {
    ESP_LOGW(TAG, "Could not create discovery socket: errno=%d", errno);
    return ESP_FAIL;
  }

  int reuse = 1;
  int broadcast = 1;
  struct timeval timeout = {
    .tv_sec = 0,
    .tv_usec = 100 * 1000,
  };

  (void) setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  (void) setsockopt(socket_fd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));
  (void) setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

  struct sockaddr_in bind_addr = {0};
  bind_addr.sin_family = AF_INET;
  bind_addr.sin_port = htons(MESSAGE_DISCOVERY_PORT);
  bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(socket_fd, (const struct sockaddr *) &bind_addr, sizeof(bind_addr)) < 0) {
    ESP_LOGW(TAG, "Could not bind discovery socket: errno=%d", errno);
    close(socket_fd);
    return ESP_FAIL;
  }

  s_runtime.socket_fd = socket_fd;
  runtime_start_discovery_locked(current_ms);
  return ESP_OK;
}

static void runtime_reset_network_locked(void)
{
  runtime_close_socket_locked();
  s_runtime.local_ip[0] = '\0';
  s_runtime.local_hub_url[0] = '\0';
  s_runtime.hub_url[0] = '\0';
  s_runtime.hub_device_id[0] = '\0';
  s_runtime.hub_room_name[0] = '\0';
  s_runtime.discovery_deadline_ms = 0;
  s_runtime.last_hub_heartbeat_ms = 0;
  s_runtime.last_heartbeat_sent_ms = 0;
  s_runtime.last_client_refresh_ms = 0;
  clear_client_cache_locked();

  if (runtime_is_shared_locked()) {
    s_runtime.role = MESSAGE_ROLE_SHARED_DISCOVERING;
  } else {
    s_runtime.role = MESSAGE_ROLE_SINGLE;
  }
}

static void handle_hub_signal_locked(
  const char *hub_url,
  const char *hub_device_id,
  const char *hub_room_name,
  int64_t current_ms,
  bool *out_should_sync,
  char *out_sync_url,
  size_t out_sync_url_size
)
{
  if (hub_url == NULL || hub_url[0] == '\0' || hub_device_id == NULL || hub_device_id[0] == '\0') {
    return;
  }

  if (strcmp(hub_device_id, s_runtime.device_id) == 0) {
    return;
  }

  update_known_target(hub_device_id, hub_room_name);

  if (s_runtime.role == MESSAGE_ROLE_SHARED_HUB) {
    if (compare_hub_priority(hub_device_id, s_runtime.device_id) < 0) {
      runtime_become_shared_client_locked(
        hub_url,
        hub_device_id,
        hub_room_name,
        current_ms,
        out_should_sync,
        out_sync_url,
        out_sync_url_size
      );
    }
    return;
  }

  if (s_runtime.hub_device_id[0] == '\0'
      || compare_hub_priority(hub_device_id, s_runtime.hub_device_id) < 0
      || s_runtime.role == MESSAGE_ROLE_SHARED_DISCOVERING) {
    runtime_become_shared_client_locked(
      hub_url,
      hub_device_id,
      hub_room_name,
      current_ms,
      out_should_sync,
      out_sync_url,
      out_sync_url_size
    );
  } else if (strcmp(hub_device_id, s_runtime.hub_device_id) == 0) {
    s_runtime.last_hub_heartbeat_ms = current_ms;
  }
}

static void process_discovery_packet(
  const char *payload_json,
  const struct sockaddr_in *source_addr,
  int64_t current_ms
)
{
  if (payload_json == NULL || source_addr == NULL) {
    return;
  }

  cJSON *root = cJSON_Parse(payload_json);
  if (root == NULL) {
    return;
  }

  cJSON *version_item = cJSON_GetObjectItemCaseSensitive(root, "version");
  cJSON *type_item = cJSON_GetObjectItemCaseSensitive(root, "type");
  cJSON *device_id_item = cJSON_GetObjectItemCaseSensitive(root, "deviceId");
  cJSON *room_name_item = cJSON_GetObjectItemCaseSensitive(root, "roomName");
  cJSON *hub_url_item = cJSON_GetObjectItemCaseSensitive(root, "hubUrl");

  if (!cJSON_IsNumber(version_item)
      || version_item->valueint != MESSAGE_DISCOVERY_VERSION
      || !cJSON_IsString(type_item)
      || !cJSON_IsString(device_id_item)) {
    cJSON_Delete(root);
    return;
  }

  const char *packet_type = type_item->valuestring;
  const char *packet_device_id = device_id_item->valuestring;
  const char *packet_room_name = cJSON_IsString(room_name_item) ? room_name_item->valuestring : packet_device_id;
  const char *packet_hub_url = cJSON_IsString(hub_url_item) ? hub_url_item->valuestring : "";

  bool should_sync = false;
  char sync_url[MESSAGE_HUB_URL_LEN] = "";
  bool should_reply = false;

  xSemaphoreTake(s_message_mutex, portMAX_DELAY);
  if (strcmp(packet_device_id, s_runtime.device_id) != 0) {
    update_known_target(packet_device_id, packet_room_name);
  }

  if (strings_equal_ignore_case(packet_type, "weather-clock-message-discovery")) {
    should_reply = (s_runtime.role == MESSAGE_ROLE_SHARED_HUB);
  } else if (strings_equal_ignore_case(packet_type, "weather-clock-message-hub")
             || strings_equal_ignore_case(packet_type, "weather-clock-message-heartbeat")) {
    handle_hub_signal_locked(
      packet_hub_url,
      packet_device_id,
      packet_room_name,
      current_ms,
      &should_sync,
      sync_url,
      sizeof(sync_url)
    );
  }
  xSemaphoreGive(s_message_mutex);

  if (should_reply) {
    xSemaphoreTake(s_message_mutex, portMAX_DELAY);
    (void) runtime_send_discovery_payload_locked("weather-clock-message-hub", source_addr);
    xSemaphoreGive(s_message_mutex);
  }

  if (should_sync && sync_url[0] != '\0') {
    (void) sync_local_store_to_hub(sync_url);
  }

  cJSON_Delete(root);
}

static void runtime_init_locked(const device_config_t *config)
{
  memset(&s_runtime, 0, sizeof(s_runtime));
  s_runtime.socket_fd = -1;

  copy_text(
    s_runtime.sharing_mode,
    sizeof(s_runtime.sharing_mode),
    strings_equal_ignore_case(config->message_sharing, "shared") ? "shared" : "single"
  );
  copy_text(s_runtime.device_id, sizeof(s_runtime.device_id), config->device_id);
  copy_text(s_runtime.room_name, sizeof(s_runtime.room_name), config->room_name);
  s_runtime.role = runtime_is_shared_locked() ? MESSAGE_ROLE_SHARED_DISCOVERING : MESSAGE_ROLE_SINGLE;
  if (!runtime_is_shared_locked()) {
    copy_text(s_runtime.hub_device_id, sizeof(s_runtime.hub_device_id), s_runtime.device_id);
    copy_text(s_runtime.hub_room_name, sizeof(s_runtime.hub_room_name), s_runtime.room_name);
  }

  clear_client_cache_locked();
}

static void message_runtime_task(void *arg)
{
  (void) arg;

  while (true) {
    bool wifi_connected = connectivity_is_wifi_connected();
    char local_ip[MESSAGE_LOCAL_IP_LEN] = "";
    bool have_local_ip = connectivity_get_local_ipv4(local_ip, sizeof(local_ip));
    int socket_fd = -1;
    bool should_sync = false;
    char sync_url[MESSAGE_HUB_URL_LEN] = "";
    bool should_refresh_remote = false;

    int64_t current_ms = now_ms();

    xSemaphoreTake(s_message_mutex, portMAX_DELAY);
    if (!wifi_connected || !have_local_ip) {
      runtime_reset_network_locked();
    } else {
      runtime_update_local_address_locked(local_ip);
      if (runtime_is_shared_locked()) {
        (void) runtime_ensure_socket_locked(current_ms);
      } else {
        runtime_close_socket_locked();
        s_runtime.role = MESSAGE_ROLE_SINGLE;
        copy_text(s_runtime.hub_url, sizeof(s_runtime.hub_url), s_runtime.local_hub_url);
        copy_text(s_runtime.hub_device_id, sizeof(s_runtime.hub_device_id), s_runtime.device_id);
        copy_text(s_runtime.hub_room_name, sizeof(s_runtime.hub_room_name), s_runtime.room_name);
      }

      if (runtime_is_shared_locked()) {
        if (s_runtime.role == MESSAGE_ROLE_SHARED_DISCOVERING
            && s_runtime.discovery_deadline_ms > 0
            && current_ms >= s_runtime.discovery_deadline_ms) {
          runtime_become_shared_hub_locked(current_ms);
        }

        if (s_runtime.role == MESSAGE_ROLE_SHARED_HUB
            && s_runtime.socket_fd >= 0
            && (s_runtime.last_heartbeat_sent_ms == 0
                || current_ms - s_runtime.last_heartbeat_sent_ms >= MESSAGE_HEARTBEAT_INTERVAL_MS)) {
          struct sockaddr_in destination = {0};
          destination.sin_family = AF_INET;
          destination.sin_port = htons(MESSAGE_DISCOVERY_PORT);
          destination.sin_addr.s_addr = inet_addr("255.255.255.255");
          if (runtime_send_discovery_payload_locked("weather-clock-message-heartbeat", &destination) == ESP_OK) {
            s_runtime.last_heartbeat_sent_ms = current_ms;
          }
        }

        if (s_runtime.role == MESSAGE_ROLE_SHARED_CLIENT
            && s_runtime.last_hub_heartbeat_ms > 0
            && current_ms - s_runtime.last_hub_heartbeat_ms > MESSAGE_HEARTBEAT_STALE_MS) {
          runtime_start_discovery_locked(current_ms);
        }

        if (runtime_should_proxy_locked()
            && (s_runtime.last_client_refresh_ms == 0
                || current_ms - s_runtime.last_client_refresh_ms >= MESSAGE_CLIENT_REFRESH_INTERVAL_MS)) {
          should_refresh_remote = true;
        }
      }

      socket_fd = s_runtime.socket_fd;
    }
    xSemaphoreGive(s_message_mutex);

    if (socket_fd >= 0) {
      char packet[384];
      struct sockaddr_in source_addr = {0};
      socklen_t source_len = sizeof(source_addr);
      ssize_t packet_len = recvfrom(
        socket_fd,
        packet,
        sizeof(packet) - 1,
        0,
        (struct sockaddr *) &source_addr,
        &source_len
      );
      if (packet_len > 0) {
        packet[(size_t) packet_len] = '\0';
        process_discovery_packet(packet, &source_addr, now_ms());
      }
    }

    if (should_sync && sync_url[0] != '\0') {
      (void) sync_local_store_to_hub(sync_url);
    }

    if (should_refresh_remote) {
      (void) refresh_remote_client_cache();
    }

    vTaskDelay(pdMS_TO_TICKS(MESSAGE_RUNTIME_POLL_INTERVAL_MS));
  }
}

static esp_err_t message_runtime_handler(httpd_req_t *req)
{
  cJSON *json = cJSON_CreateObject();
  if (json == NULL) {
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON allocate failed");
  }

  xSemaphoreTake(s_message_mutex, portMAX_DELAY);
  cJSON_AddStringToObject(json, "sharingMode", s_runtime.sharing_mode);
  cJSON_AddStringToObject(json, "role", message_role_name(s_runtime.role));
  cJSON_AddStringToObject(json, "deviceId", s_runtime.device_id);
  cJSON_AddStringToObject(json, "roomName", s_runtime.room_name);
  if (s_runtime.hub_url[0] != '\0') {
    cJSON_AddStringToObject(json, "hubUrl", s_runtime.hub_url);
  } else {
    cJSON_AddNullToObject(json, "hubUrl");
  }
  cJSON_AddStringToObject(
    json,
    "hubDeviceId",
    s_runtime.hub_device_id[0] != '\0' ? s_runtime.hub_device_id : s_runtime.device_id
  );
  xSemaphoreGive(s_message_mutex);

  return send_json_response(req, 200, json);
}

static esp_err_t message_targets_handler(httpd_req_t *req)
{
  xSemaphoreTake(s_message_mutex, portMAX_DELAY);
  bool should_proxy = runtime_should_proxy_locked();
  xSemaphoreGive(s_message_mutex);
  if (should_proxy) {
    return proxy_request_to_hub(req);
  }

  xSemaphoreTake(s_message_mutex, portMAX_DELAY);
  cJSON *json = cJSON_CreateObject();
  cJSON *targets = cJSON_AddArrayToObject(json, "targets");
  cJSON *all = cJSON_CreateObject();
  cJSON_AddStringToObject(all, "id", "all");
  cJSON_AddStringToObject(all, "label", "All clocks");
  cJSON_AddItemToArray(targets, all);

  for (size_t index = 0; index < s_target_count; ++index) {
    cJSON *target = cJSON_CreateObject();
    cJSON_AddStringToObject(target, "id", s_targets[index].device_id);
    cJSON_AddStringToObject(target, "label", s_targets[index].room_name);
    cJSON_AddItemToArray(targets, target);
  }
  xSemaphoreGive(s_message_mutex);
  return send_json_response(req, 200, json);
}

static esp_err_t messages_get_handler(httpd_req_t *req)
{
  xSemaphoreTake(s_message_mutex, portMAX_DELAY);
  bool should_proxy = runtime_should_proxy_locked();
  xSemaphoreGive(s_message_mutex);
  if (should_proxy) {
    return proxy_request_to_hub(req);
  }

  char device_id[DEVICE_CONFIG_STR_LEN] = "";
  char include_dismissed[8] = "";
  int indexes[MESSAGE_MAX_RECORDS] = {0};
  uint32_t unread_count = 0;
  bool has_important = false;
  request_known_device_t known_device = {0};
  read_request_known_device(req, &known_device);

  if (httpd_req_get_url_query_len(req) > 0) {
    char query[160] = "";
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
      httpd_query_key_value(query, "deviceId", device_id, sizeof(device_id));
      httpd_query_key_value(query, "includeDismissed", include_dismissed, sizeof(include_dismissed));
    }
  }

  bool include_acknowledged = strings_equal_ignore_case(include_dismissed, "true");

  xSemaphoreTake(s_message_mutex, portMAX_DELAY);
  const char *resolved_device_id = device_id[0] != '\0' ? device_id : s_runtime.device_id;
  if (strcmp(resolved_device_id, "all") != 0) {
    const char *resolved_room_name = strcmp(resolved_device_id, s_runtime.device_id) == 0
      ? s_runtime.room_name
      : (strcmp(resolved_device_id, known_device.device_id) == 0 ? known_device.room_name : "");
    update_known_target(resolved_device_id, resolved_room_name);
  }

  int count = collect_visible_message_indexes(
    resolved_device_id,
    include_acknowledged,
    indexes,
    MESSAGE_MAX_RECORDS,
    &unread_count,
    &has_important
  );

  cJSON *json = cJSON_CreateObject();
  cJSON_AddStringToObject(json, "deviceId", resolved_device_id);
  cJSON_AddNumberToObject(json, "unreadCount", (double) unread_count);
  cJSON *messages = cJSON_AddArrayToObject(json, "messages");

  for (int index = 0; index < count; ++index) {
    cJSON *message = message_record_to_json(&s_messages[indexes[index]]);
    if (message != NULL) {
      cJSON_AddItemToArray(messages, message);
    }
  }
  xSemaphoreGive(s_message_mutex);

  (void) has_important;
  return send_json_response(req, 200, json);
}

static esp_err_t messages_post_handler(httpd_req_t *req)
{
  xSemaphoreTake(s_message_mutex, portMAX_DELAY);
  bool should_proxy = runtime_should_proxy_locked();
  xSemaphoreGive(s_message_mutex);
  if (should_proxy) {
    return proxy_request_to_hub(req);
  }

  char *body = NULL;
  esp_err_t read_err = read_request_body_alloc(req, &body);
  if (read_err == ESP_ERR_INVALID_SIZE) {
    return send_error_response(req, 400, "Request body is too large");
  }
  if (read_err != ESP_OK) {
    return send_error_response(req, 400, "Could not read request body");
  }

  cJSON *payload = cJSON_Parse(body);
  free(body);
  if (payload == NULL) {
    return send_error_response(req, 400, "Invalid JSON payload");
  }

  cJSON *text_item = cJSON_GetObjectItemCaseSensitive(payload, "text");
  cJSON *sender_item = cJSON_GetObjectItemCaseSensitive(payload, "sender");
  cJSON *target_item = cJSON_GetObjectItemCaseSensitive(payload, "target");
  cJSON *priority_item = cJSON_GetObjectItemCaseSensitive(payload, "priority");
  cJSON *expires_item = cJSON_GetObjectItemCaseSensitive(payload, "expiresAt");

  if (!cJSON_IsString(text_item) || text_item->valuestring == NULL || text_item->valuestring[0] == '\0') {
    cJSON_Delete(payload);
    return send_error_response(req, 400, "Message text is required");
  }

  if (strlen(text_item->valuestring) >= APP_MESSAGE_TEXT_LEN) {
    cJSON_Delete(payload);
    return send_error_response(req, 400, "Message text must be 180 characters or fewer");
  }

  if (cJSON_IsString(sender_item) && strlen(sender_item->valuestring) >= APP_MESSAGE_SENDER_LEN) {
    cJSON_Delete(payload);
    return send_error_response(req, 400, "Sender must be 40 characters or fewer");
  }

  const char *target_value = (cJSON_IsString(target_item) && target_item->valuestring[0] != '\0')
    ? target_item->valuestring
    : "all";

  if (strlen(target_value) >= DEVICE_CONFIG_STR_LEN) {
    cJSON_Delete(payload);
    return send_error_response(req, 400, "Target is too long");
  }

  bool important = cJSON_IsString(priority_item) && strings_equal_ignore_case(priority_item->valuestring, "important");
  time_t expires_at_epoch = 0;
  bool has_expires_at = false;
  char expires_at[MESSAGE_ISO_LEN] = "";

  if (cJSON_IsString(expires_item) && expires_item->valuestring[0] != '\0') {
    if (!parse_iso_time(expires_item->valuestring, &expires_at_epoch)) {
      cJSON_Delete(payload);
      return send_error_response(req, 400, "expiresAt must use ISO date-time format");
    }
    has_expires_at = true;
    copy_text(expires_at, sizeof(expires_at), expires_item->valuestring);
  }

  time_t now = time(NULL);
  char created_at[MESSAGE_ISO_LEN] = "";
  format_iso_time(now, created_at, sizeof(created_at));

  xSemaphoreTake(s_message_mutex, portMAX_DELAY);
  int slot = find_insertion_slot();
  memset(&s_messages[slot], 0, sizeof(s_messages[slot]));
  s_messages[slot].in_use = true;
  s_messages[slot].active = true;
  s_messages[slot].important = important;
  snprintf(
    s_messages[slot].id,
    sizeof(s_messages[slot].id),
    "msg_%08" PRIx32 "_%04" PRIx32,
    (uint32_t) now,
    esp_random() & 0xffffU
  );
  copy_text(s_messages[slot].text, sizeof(s_messages[slot].text), text_item->valuestring);
  copy_text(
    s_messages[slot].sender,
    sizeof(s_messages[slot].sender),
    cJSON_IsString(sender_item) ? sender_item->valuestring : ""
  );
  copy_text(s_messages[slot].target, sizeof(s_messages[slot].target), target_value);
  copy_text(s_messages[slot].created_at, sizeof(s_messages[slot].created_at), created_at);
  s_messages[slot].created_at_epoch = now;
  s_messages[slot].has_expires_at = has_expires_at;
  s_messages[slot].expires_at_epoch = expires_at_epoch;
  copy_text(s_messages[slot].expires_at, sizeof(s_messages[slot].expires_at), expires_at);
  update_known_target(s_runtime.device_id, s_runtime.room_name);
  if (strcmp(target_value, "all") != 0) {
    update_known_target(target_value, target_value);
  }

  cJSON *response = message_record_to_json(&s_messages[slot]);
  ESP_LOGI(
    TAG,
    "Queued message %s target=%s priority=%s",
    s_messages[slot].id,
    s_messages[slot].target,
    s_messages[slot].important ? "important" : "normal"
  );
  xSemaphoreGive(s_message_mutex);
  cJSON_Delete(payload);

  return send_json_response(req, 201, response);
}

static esp_err_t message_action_handler(httpd_req_t *req)
{
  xSemaphoreTake(s_message_mutex, portMAX_DELAY);
  bool should_proxy = runtime_should_proxy_locked();
  xSemaphoreGive(s_message_mutex);
  if (should_proxy) {
    return proxy_request_to_hub(req);
  }

  const char *uri = req->uri;
  const char *prefix = "/api/messages/";
  size_t prefix_len = strlen(prefix);
  if (strncmp(uri, prefix, prefix_len) != 0) {
    return send_error_response(req, 404, "Message route not found");
  }

  const char *suffix = uri + prefix_len;
  const char *separator = strchr(suffix, '/');
  if (separator == NULL) {
    return send_error_response(req, 404, "Message route not found");
  }

  size_t id_len = (size_t) (separator - suffix);
  if (id_len == 0 || id_len >= APP_MESSAGE_ID_LEN) {
    return send_error_response(req, 400, "Invalid message ID");
  }

  char message_id[APP_MESSAGE_ID_LEN];
  memcpy(message_id, suffix, id_len);
  message_id[id_len] = '\0';

  const char *action = separator + 1;

  xSemaphoreTake(s_message_mutex, portMAX_DELAY);
  int message_index = find_message_index_by_id(message_id);
  if (message_index < 0) {
    xSemaphoreGive(s_message_mutex);
    return send_error_response(req, 404, "Message not found");
  }

  message_record_t *record = &s_messages[message_index];
  if (strcmp(action, "deactivate") == 0) {
    record->active = false;
    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "ok", true);
    cJSON_AddStringToObject(json, "id", message_id);
    ESP_LOGI(TAG, "Deactivated message %s", message_id);
    xSemaphoreGive(s_message_mutex);
    return send_json_response(req, 200, json);
  }

  if (strcmp(action, "ack") != 0) {
    xSemaphoreGive(s_message_mutex);
    return send_error_response(req, 404, "Message route not found");
  }
  xSemaphoreGive(s_message_mutex);

  char *body = NULL;
  esp_err_t read_err = read_request_body_alloc(req, &body);
  if (read_err == ESP_ERR_INVALID_SIZE) {
    return send_error_response(req, 400, "Request body is too large");
  }
  if (read_err != ESP_OK) {
    return send_error_response(req, 400, "Could not read request body");
  }

  cJSON *payload = cJSON_Parse(body);
  free(body);
  if (payload == NULL) {
    return send_error_response(req, 400, "Invalid JSON payload");
  }

  cJSON *device_item = cJSON_GetObjectItemCaseSensitive(payload, "deviceId");
  if (!cJSON_IsString(device_item) || device_item->valuestring == NULL || device_item->valuestring[0] == '\0') {
    cJSON_Delete(payload);
    return send_error_response(req, 400, "deviceId is required");
  }

  request_known_device_t known_device = {0};
  read_request_known_device(req, &known_device);

  xSemaphoreTake(s_message_mutex, portMAX_DELAY);
  message_index = find_message_index_by_id(message_id);
  if (message_index < 0) {
    xSemaphoreGive(s_message_mutex);
    cJSON_Delete(payload);
    return send_error_response(req, 404, "Message not found");
  }

  record = &s_messages[message_index];
  bool already_acknowledged = is_acknowledged_for_device(record, device_item->valuestring);
  if (!already_acknowledged && record->acknowledged_count < MESSAGE_MAX_ACKS) {
    copy_text(
      record->acknowledged_by[record->acknowledged_count],
      sizeof(record->acknowledged_by[record->acknowledged_count]),
      device_item->valuestring
    );
    record->acknowledged_count++;
  }

  const char *resolved_room_name = strcmp(device_item->valuestring, s_runtime.device_id) == 0
    ? s_runtime.room_name
    : (strcmp(device_item->valuestring, known_device.device_id) == 0 ? known_device.room_name : "");
  update_known_target(device_item->valuestring, resolved_room_name);

  cJSON *json = cJSON_CreateObject();
  cJSON_AddBoolToObject(json, "ok", true);
  cJSON_AddStringToObject(json, "id", message_id);
  cJSON_AddStringToObject(json, "deviceId", device_item->valuestring);
  ESP_LOGI(TAG, "Acknowledged message %s for %s", message_id, device_item->valuestring);
  xSemaphoreGive(s_message_mutex);
  cJSON_Delete(payload);
  return send_json_response(req, 200, json);
}

static esp_err_t messages_sync_handler(httpd_req_t *req)
{
  xSemaphoreTake(s_message_mutex, portMAX_DELAY);
  bool should_proxy = runtime_should_proxy_locked();
  bool can_accept_sync = (s_runtime.role == MESSAGE_ROLE_SINGLE || s_runtime.role == MESSAGE_ROLE_SHARED_HUB);
  xSemaphoreGive(s_message_mutex);

  if (should_proxy) {
    return proxy_request_to_hub(req);
  }
  if (!can_accept_sync) {
    return send_error_response(req, 409, "Only the current message hub can accept sync data");
  }

  char *body = NULL;
  esp_err_t read_err = read_request_body_alloc(req, &body);
  if (read_err == ESP_ERR_INVALID_SIZE) {
    return send_error_response(req, 400, "Request body is too large");
  }
  if (read_err != ESP_OK) {
    return send_error_response(req, 400, "Could not read request body");
  }

  cJSON *payload = cJSON_Parse(body);
  free(body);
  if (payload == NULL) {
    return send_error_response(req, 400, "Invalid JSON payload");
  }

  cJSON *messages_item = cJSON_GetObjectItemCaseSensitive(payload, "messages");
  if (!cJSON_IsArray(messages_item)) {
    cJSON_Delete(payload);
    return send_error_response(req, 400, "messages must be an array");
  }

  size_t merged_count = 0;
  xSemaphoreTake(s_message_mutex, portMAX_DELAY);
  cJSON *message_item = NULL;
  cJSON_ArrayForEach(message_item, messages_item) {
    message_record_t incoming = {0};
    if (!parse_message_json(message_item, &incoming)) {
      continue;
    }
    merge_message_record_locked(&incoming);
    merged_count++;
  }
  xSemaphoreGive(s_message_mutex);
  cJSON_Delete(payload);

  cJSON *json = cJSON_CreateObject();
  cJSON_AddBoolToObject(json, "ok", true);
  cJSON_AddNumberToObject(json, "count", (double) merged_count);
  return send_json_response(req, 200, json);
}

extern const uint8_t messages_html_start[] asm("_binary_messages_html_start");
extern const uint8_t messages_html_end[] asm("_binary_messages_html_end");

static esp_err_t messages_page_handler(httpd_req_t *req)
{
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(
    req,
    (const char *) messages_html_start,
    (ssize_t) (messages_html_end - messages_html_start)
  );
}

static esp_err_t start_http_server(void)
{
  if (s_http_server != NULL) {
    return ESP_OK;
  }

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  /* Default is 4096. Several handlers here (proxy_request_to_hub in
   * particular: a 1024-byte url buffer plus other locals) call
   * esp_http_client synchronously from the httpd worker task, and
   * esp_http_client's own internal stack use on top of that reliably
   * overflows the default -- confirmed on-device via an overnight capture
   * that caught three separate "Stack protection fault" panics, all in
   * task "httpd". */
  config.stack_size = 12288;
  config.server_port = MESSAGE_SERVICE_PORT;
  config.uri_match_fn = httpd_uri_match_wildcard;

  esp_err_t err = httpd_start(&s_http_server, &config);
  if (err != ESP_OK) {
    return err;
  }

  const httpd_uri_t runtime_uri = {
    .uri = "/api/message-runtime",
    .method = HTTP_GET,
    .handler = message_runtime_handler,
    .user_ctx = NULL
  };
  const httpd_uri_t targets_uri = {
    .uri = "/api/message-targets",
    .method = HTTP_GET,
    .handler = message_targets_handler,
    .user_ctx = NULL
  };
  const httpd_uri_t messages_get_uri = {
    .uri = "/api/messages",
    .method = HTTP_GET,
    .handler = messages_get_handler,
    .user_ctx = NULL
  };
  const httpd_uri_t messages_post_uri = {
    .uri = "/api/messages",
    .method = HTTP_POST,
    .handler = messages_post_handler,
    .user_ctx = NULL
  };
  const httpd_uri_t sync_uri = {
    .uri = "/api/messages/sync",
    .method = HTTP_POST,
    .handler = messages_sync_handler,
    .user_ctx = NULL
  };
  const httpd_uri_t message_action_uri = {
    .uri = "/api/messages/*",
    .method = HTTP_POST,
    .handler = message_action_handler,
    .user_ctx = NULL
  };

  const httpd_uri_t page_root_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = messages_page_handler,
    .user_ctx = NULL
  };
  const httpd_uri_t page_messages_uri = {
    .uri = "/messages",
    .method = HTTP_GET,
    .handler = messages_page_handler,
    .user_ctx = NULL
  };

  httpd_register_uri_handler(s_http_server, &runtime_uri);
  httpd_register_uri_handler(s_http_server, &targets_uri);
  httpd_register_uri_handler(s_http_server, &messages_get_uri);
  httpd_register_uri_handler(s_http_server, &messages_post_uri);
  httpd_register_uri_handler(s_http_server, &sync_uri);
  httpd_register_uri_handler(s_http_server, &message_action_uri);
  httpd_register_uri_handler(s_http_server, &page_root_uri);
  httpd_register_uri_handler(s_http_server, &page_messages_uri);
  ESP_LOGI(TAG, "Message API ready on port %d", MESSAGE_SERVICE_PORT);
  return ESP_OK;
}

esp_err_t message_service_init(const device_config_t *config)
{
  if (config == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  if (s_message_mutex == NULL) {
    s_message_mutex = xSemaphoreCreateMutex();
    if (s_message_mutex == NULL) {
      return ESP_ERR_NO_MEM;
    }
  }

  xSemaphoreTake(s_message_mutex, portMAX_DELAY);
  s_config_snapshot = *config;
  if (s_target_count == 0) {
    update_known_target(config->device_id, config->room_name);
  }
  runtime_init_locked(config);
  xSemaphoreGive(s_message_mutex);

  if (s_runtime_task_handle == NULL) {
    BaseType_t task_ok = xTaskCreate(
      message_runtime_task,
      "rwd_messages_rt",
      8192,
      NULL,
      4,
      &s_runtime_task_handle
    );
    if (task_ok != pdPASS) {
      return ESP_ERR_NO_MEM;
    }
  }

  return start_http_server();
}

esp_err_t message_service_get_snapshot(
  const char *device_id,
  app_message_snapshot_t *out_snapshot,
  uint32_t *out_unread_count,
  bool *out_has_important
)
{
  if (out_snapshot == NULL || out_unread_count == NULL || out_has_important == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  memset(out_snapshot, 0, sizeof(*out_snapshot));
  *out_unread_count = 0;
  *out_has_important = false;

  if (s_message_mutex == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  xSemaphoreTake(s_message_mutex, portMAX_DELAY);

  if (runtime_should_proxy_locked()) {
    if (s_client_cache.valid) {
      *out_snapshot = s_client_cache.snapshot;
      *out_unread_count = s_client_cache.unread_count;
      *out_has_important = s_client_cache.has_important;
    }
    xSemaphoreGive(s_message_mutex);
    return ESP_OK;
  }

  int indexes[MESSAGE_MAX_RECORDS] = {0};
  int count = collect_visible_message_indexes(
    device_id != NULL && device_id[0] != '\0' ? device_id : s_runtime.device_id,
    false,
    indexes,
    MESSAGE_MAX_RECORDS,
    out_unread_count,
    out_has_important
  );

  if (count > 0) {
    const message_record_t *record = &s_messages[indexes[0]];
    out_snapshot->available = true;
    out_snapshot->important = record->important;
    copy_text(out_snapshot->id, sizeof(out_snapshot->id), record->id);
    copy_text(out_snapshot->text, sizeof(out_snapshot->text), record->text);
    copy_text(out_snapshot->sender, sizeof(out_snapshot->sender), record->sender);
    format_message_meta(record, out_snapshot->meta, sizeof(out_snapshot->meta));
  }

  xSemaphoreGive(s_message_mutex);
  return ESP_OK;
}

esp_err_t message_service_acknowledge_active(const char *device_id)
{
  if (device_id == NULL || device_id[0] == '\0' || s_message_mutex == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  char hub_url[MESSAGE_HUB_URL_LEN] = "";
  char active_message_id[APP_MESSAGE_ID_LEN] = "";
  char runtime_device_id[DEVICE_CONFIG_STR_LEN] = "";
  char runtime_room_name[DEVICE_CONFIG_STR_LEN] = "";

  xSemaphoreTake(s_message_mutex, portMAX_DELAY);
  if (runtime_should_proxy_locked()) {
    if (!s_client_cache.snapshot.available) {
      xSemaphoreGive(s_message_mutex);
      return ESP_ERR_NOT_FOUND;
    }
    copy_text(hub_url, sizeof(hub_url), s_runtime.hub_url);
    copy_text(active_message_id, sizeof(active_message_id), s_client_cache.snapshot.id);
    copy_text(runtime_device_id, sizeof(runtime_device_id), s_runtime.device_id);
    copy_text(runtime_room_name, sizeof(runtime_room_name), s_runtime.room_name);
    xSemaphoreGive(s_message_mutex);

    cJSON *payload = cJSON_CreateObject();
    if (payload == NULL) {
      return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(payload, "deviceId", device_id);
    char *body = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);
    if (body == NULL) {
      return ESP_ERR_NO_MEM;
    }

    char url[MESSAGE_URL_LEN];
    snprintf(url, sizeof(url), "%s/api/messages/%s/ack", hub_url, active_message_id);

    int status_code = 500;
    http_response_buffer_t response = {0};
    esp_err_t request_err = perform_http_request(
      url,
      HTTP_POST,
      body,
      runtime_device_id,
      runtime_room_name,
      &status_code,
      &response
    );
    free(body);
    free_http_response_buffer(&response);
    if (request_err != ESP_OK || status_code < 200 || status_code >= 300) {
      return request_err != ESP_OK ? request_err : ESP_FAIL;
    }

    (void) refresh_remote_client_cache();
    return ESP_OK;
  }

  int indexes[MESSAGE_MAX_RECORDS] = {0};
  uint32_t unread_count = 0;
  bool has_important = false;
  int count = collect_visible_message_indexes(
    device_id,
    false,
    indexes,
    MESSAGE_MAX_RECORDS,
    &unread_count,
    &has_important
  );

  (void) unread_count;
  (void) has_important;

  if (count <= 0) {
    xSemaphoreGive(s_message_mutex);
    return ESP_ERR_NOT_FOUND;
  }

  message_record_t *record = &s_messages[indexes[0]];
  if (!is_acknowledged_for_device(record, device_id) && record->acknowledged_count < MESSAGE_MAX_ACKS) {
    copy_text(record->acknowledged_by[record->acknowledged_count], sizeof(record->acknowledged_by[0]), device_id);
    record->acknowledged_count++;
  }
  update_known_target(device_id, strcmp(device_id, s_runtime.device_id) == 0 ? s_runtime.room_name : device_id);
  ESP_LOGI(TAG, "Acknowledged active message %s for %s", record->id, device_id);

  xSemaphoreGive(s_message_mutex);
  return ESP_OK;
}
