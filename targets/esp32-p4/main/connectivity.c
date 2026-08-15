#include "connectivity.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_event.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "esp_wifi.h"

#include "timezone_table.h"

static const char *TAG = "rwd_connectivity";

#define WIFI_CONNECTED_BIT BIT0
#define VALID_CLOCK_EPOCH 1704067200

static EventGroupHandle_t s_wifi_event_group;
static esp_netif_t *s_wifi_netif;
static bool s_wifi_initialized;
static bool s_wifi_driver_initialized;
static bool s_wifi_started;
static bool s_wifi_event_handlers_registered;
static bool s_wifi_auto_connect_enabled;
static bool s_sntp_initialized;
static bool s_runtime_prepared;

/* Looks up the real POSIX TZ string (DST rule and all) for an IANA zone
 * name via TIMEZONE_MAPPINGS (targets/esp32-p4/tools/generate_timezone_table.py,
 * derived from the host's own tzdata at generation time -- ESP-IDF/newlib
 * has no zoneinfo database on-device, so a raw IANA name like "Asia/Tokyo"
 * passed straight to setenv("TZ", ...) does not resolve to correct
 * offset/DST behavior). Falls back to the raw string only for the
 * essentially-never-hit case of a zone added to IANA's database after this
 * table was last generated. */
static const char *resolve_timezone(const char *timezone)
{
  if (timezone == NULL || timezone[0] == '\0') {
    return "UTC0";
  }

  for (size_t i = 0; i < TIMEZONE_MAPPINGS_COUNT; ++i) {
    if (strcmp(timezone, TIMEZONE_MAPPINGS[i].iana_name) == 0) {
      return TIMEZONE_MAPPINGS[i].posix_name;
    }
  }

  ESP_LOGW(TAG, "No POSIX TZ mapping for '%s' (table may be stale); DST/offset may be wrong", timezone);
  return timezone;
}

void connectivity_apply_timezone(const char *timezone)
{
  const char *resolved_timezone = resolve_timezone(timezone);
  setenv("TZ", resolved_timezone, 1);
  tzset();
  ESP_LOGI(TAG, "Applied timezone '%s' as '%s'", timezone != NULL ? timezone : "", resolved_timezone);
}

static void connectivity_event_handler(
  void *arg,
  esp_event_base_t event_base,
  int32_t event_id,
  void *event_data
)
{
  (void) arg;
  (void) event_data;

  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    if (s_wifi_auto_connect_enabled) {
      ESP_LOGI(TAG, "Starting Wi-Fi station connection");
      (void) esp_wifi_connect();
    }
    return;
  }

  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    if (s_wifi_auto_connect_enabled) {
      ESP_LOGW(TAG, "Wi-Fi disconnected, retrying");
      (void) esp_wifi_connect();
    }
    return;
  }

  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
    wifi_event_sta_scan_done_t *scan_done = (wifi_event_sta_scan_done_t *) event_data;
    if (scan_done != NULL) {
      ESP_LOGI(
        TAG,
        "Wi-Fi scan done: status=%u ap_count=%u scan_id=%u",
        (unsigned) scan_done->status,
        (unsigned) scan_done->number,
        (unsigned) scan_done->scan_id
      );
    } else {
      ESP_LOGI(TAG, "Wi-Fi scan done");
    }
    return;
  }

  if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    ESP_LOGI(TAG, "Wi-Fi connected and IP acquired");
  }
}

esp_err_t connectivity_prepare_runtime(void)
{
  if (s_runtime_prepared) {
    return ESP_OK;
  }

  esp_err_t err = esp_netif_init();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    return err;
  }

  err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    return err;
  }

  s_runtime_prepared = true;
  return ESP_OK;
}

static esp_err_t connectivity_prepare_stack(void)
{
  ESP_RETURN_ON_ERROR(connectivity_prepare_runtime(), TAG, "Failed to prepare network runtime");

  if (s_wifi_event_group == NULL) {
    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
      return ESP_ERR_NO_MEM;
    }
  }

  if (s_wifi_netif == NULL) {
    s_wifi_netif = esp_netif_create_default_wifi_sta();
    if (s_wifi_netif == NULL) {
      return ESP_FAIL;
    }
  }

  if (!s_wifi_driver_initialized) {
    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&wifi_init_config), TAG, "esp_wifi_init failed");
    s_wifi_driver_initialized = true;
  }

  return ESP_OK;
}

static esp_err_t connectivity_register_handlers(void)
{
  if (s_wifi_event_handlers_registered) {
    return ESP_OK;
  }

  ESP_RETURN_ON_ERROR(
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &connectivity_event_handler, NULL),
    TAG,
    "Failed to register Wi-Fi event handler"
  );
  ESP_RETURN_ON_ERROR(
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connectivity_event_handler, NULL),
    TAG,
    "Failed to register IP event handler"
  );

  s_wifi_event_handlers_registered = true;
  return ESP_OK;
}

esp_err_t connectivity_init(const device_config_t *config)
{
  if (config == NULL || !config->wifi_ready) {
    return ESP_ERR_INVALID_ARG;
  }

  if (s_wifi_initialized) {
    return ESP_OK;
  }

  ESP_RETURN_ON_ERROR(connectivity_prepare_stack(), TAG, "Failed to prepare Wi-Fi stack");
  ESP_RETURN_ON_ERROR(connectivity_register_handlers(), TAG, "Failed to register Wi-Fi event handlers");

  wifi_config_t wifi_config = {0};
  memcpy(wifi_config.sta.ssid, config->wifi_ssid, strlen(config->wifi_ssid));
  memcpy(wifi_config.sta.password, config->wifi_password, strlen(config->wifi_password));
  wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
  wifi_config.sta.pmf_cfg.capable = true;
  wifi_config.sta.pmf_cfg.required = false;

  s_wifi_auto_connect_enabled = true;
  xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

  ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "esp_wifi_set_mode failed");
  ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "esp_wifi_set_config failed");

  if (!s_wifi_started) {
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "esp_wifi_start failed");
    s_wifi_started = true;
  } else {
    (void) esp_wifi_disconnect();
    ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "esp_wifi_connect failed");
  }

  s_wifi_initialized = true;
  return ESP_OK;
}

esp_err_t connectivity_scan_networks(
  connectivity_scan_result_t *results,
  size_t max_results,
  size_t *out_result_count
)
{
  const TickType_t settle_delay_ticks = pdMS_TO_TICKS(1500);
  const uint32_t max_scan_attempts = 5;
  bool restore_auto_connect = s_wifi_auto_connect_enabled || s_wifi_initialized;
  uint16_t ap_count = 0;
  esp_err_t err = ESP_OK;

  if ((results == NULL && max_results > 0) || out_result_count == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  *out_result_count = 0;

  err = connectivity_prepare_stack();
  if (err != ESP_OK) {
    return err;
  }

  err = connectivity_register_handlers();
  if (err != ESP_OK) {
    return err;
  }

  s_wifi_auto_connect_enabled = false;

  err = esp_wifi_set_mode(WIFI_MODE_STA);
  if (err != ESP_OK) {
    goto cleanup;
  }

  if (!s_wifi_started) {
    err = esp_wifi_start();
    if (err != ESP_OK) {
      goto cleanup;
    }
    s_wifi_started = true;
  }

  for (uint32_t attempt = 1; attempt <= max_scan_attempts; ++attempt) {
    vTaskDelay(settle_delay_ticks);
    ESP_LOGI(TAG, "Starting Wi-Fi scan (attempt %" PRIu32 " of %" PRIu32 ")", attempt, max_scan_attempts);
    err = esp_wifi_scan_start(NULL, true);
    if (err != ESP_OK) {
      goto cleanup;
    }

    err = esp_wifi_scan_get_ap_num(&ap_count);
    if (err != ESP_OK) {
      goto cleanup;
    }

    ESP_LOGI(TAG, "Wi-Fi scan attempt %" PRIu32 " reported %u access points", attempt, ap_count);
    if (ap_count > 0) {
      break;
    }
  }

  if (ap_count == 0 || max_results == 0) {
    ESP_LOGI(TAG, "Wi-Fi scan completed with no usable results");
    err = ESP_OK;
    goto cleanup;
  }

  wifi_ap_record_t *ap_records = calloc(ap_count, sizeof(*ap_records));
  if (ap_records == NULL) {
    err = ESP_ERR_NO_MEM;
    goto cleanup;
  }

  uint16_t fetched_count = ap_count;
  err = esp_wifi_scan_get_ap_records(&fetched_count, ap_records);
  if (err != ESP_OK) {
    free(ap_records);
    goto cleanup;
  }

  size_t stored_count = 0;
  for (uint16_t i = 0; i < fetched_count && stored_count < max_results; ++i) {
    if (ap_records[i].ssid[0] == '\0') {
      continue;
    }

    bool duplicate = false;
    for (size_t existing = 0; existing < stored_count; ++existing) {
      if (strcmp(results[existing].ssid, (const char *) ap_records[i].ssid) == 0) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) {
      continue;
    }

    snprintf(results[stored_count].ssid, sizeof(results[stored_count].ssid), "%s", ap_records[i].ssid);
    results[stored_count].rssi = ap_records[i].rssi;
    results[stored_count].requires_password = (ap_records[i].authmode != WIFI_AUTH_OPEN);
    stored_count++;
  }

  free(ap_records);

  *out_result_count = stored_count;
  ESP_LOGI(TAG, "Wi-Fi scan completed with %u access points, exposing %u entries", ap_count, stored_count);

cleanup:
  s_wifi_auto_connect_enabled = restore_auto_connect;
  if (restore_auto_connect && !connectivity_is_wifi_connected()) {
    (void) esp_wifi_connect();
  }

  return err;
}

esp_err_t connectivity_wait_for_wifi(TickType_t timeout_ticks)
{
  if (s_wifi_event_group == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  EventBits_t bits = xEventGroupWaitBits(
    s_wifi_event_group,
    WIFI_CONNECTED_BIT,
    pdFALSE,
    pdFALSE,
    timeout_ticks
  );

  return (bits & WIFI_CONNECTED_BIT) != 0 ? ESP_OK : ESP_ERR_TIMEOUT;
}

bool connectivity_is_wifi_connected(void)
{
  if (s_wifi_event_group == NULL) {
    return false;
  }

  EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
  return (bits & WIFI_CONNECTED_BIT) != 0;
}

bool connectivity_get_local_ipv4(char *buffer, size_t buffer_size)
{
  if (buffer == NULL || buffer_size == 0 || s_wifi_netif == NULL || !connectivity_is_wifi_connected()) {
    return false;
  }

  esp_netif_ip_info_t ip_info = {0};
  if (esp_netif_get_ip_info(s_wifi_netif, &ip_info) != ESP_OK || ip_info.ip.addr == 0) {
    return false;
  }

  snprintf(
    buffer,
    buffer_size,
    IPSTR,
    IP2STR(&ip_info.ip)
  );
  return true;
}

bool connectivity_is_time_valid(void)
{
  time_t now = time(NULL);
  return now >= VALID_CLOCK_EPOCH;
}

esp_err_t connectivity_sync_time(const char *timezone, TickType_t timeout_ticks)
{
  connectivity_apply_timezone(timezone);

  if (connectivity_is_time_valid()) {
    return ESP_OK;
  }

  if (!s_sntp_initialized) {
    esp_sntp_config_t sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    sntp_config.start = true;
    ESP_RETURN_ON_ERROR(esp_netif_sntp_init(&sntp_config), TAG, "esp_netif_sntp_init failed");
    s_sntp_initialized = true;
  }

  esp_err_t err = esp_netif_sntp_sync_wait(timeout_ticks);
  if (err != ESP_OK) {
    return err;
  }

  return connectivity_is_time_valid() ? ESP_OK : ESP_FAIL;
}
