#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "freertos/FreeRTOS.h"

#include "esp_err.h"

#include "device_config.h"

typedef struct {
  char ssid[DEVICE_CONFIG_WIFI_LEN];
  int8_t rssi;
  bool requires_password;
} connectivity_scan_result_t;

esp_err_t connectivity_prepare_runtime(void);
esp_err_t connectivity_init(const device_config_t *config);
esp_err_t connectivity_scan_networks(
  connectivity_scan_result_t *results,
  size_t max_results,
  size_t *out_result_count
);
esp_err_t connectivity_wait_for_wifi(TickType_t timeout_ticks);
bool connectivity_is_wifi_connected(void);
bool connectivity_get_local_ipv4(char *buffer, size_t buffer_size);
bool connectivity_is_time_valid(void);
void connectivity_apply_timezone(const char *timezone);
esp_err_t connectivity_sync_time(const char *timezone, TickType_t timeout_ticks);
