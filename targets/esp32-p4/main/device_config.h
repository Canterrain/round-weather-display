#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define DEVICE_CONFIG_STR_LEN 64
#define DEVICE_CONFIG_WIFI_LEN 33
#define DEVICE_CONFIG_WIFI_PASS_LEN 65
#define DEVICE_CONFIG_SHORT_STR_LEN 16
#define DEVICE_CONFIG_TIME_STR_LEN 6
#define DEVICE_CONFIG_COORD_LEN 16

typedef struct {
  char device_id[DEVICE_CONFIG_STR_LEN];
  char room_name[DEVICE_CONFIG_STR_LEN];
  char location[DEVICE_CONFIG_STR_LEN];
  char timezone[DEVICE_CONFIG_STR_LEN];
  char wifi_ssid[DEVICE_CONFIG_WIFI_LEN];
  char wifi_password[DEVICE_CONFIG_WIFI_PASS_LEN];
  char message_sharing[DEVICE_CONFIG_SHORT_STR_LEN];
  char default_clock_face[DEVICE_CONFIG_SHORT_STR_LEN];
  char time_format[DEVICE_CONFIG_SHORT_STR_LEN];
  char units[DEVICE_CONFIG_SHORT_STR_LEN];
  char latitude[DEVICE_CONFIG_COORD_LEN];
  char longitude[DEVICE_CONFIG_COORD_LEN];
  char night_shift_start[DEVICE_CONFIG_TIME_STR_LEN];
  char night_shift_end[DEVICE_CONFIG_TIME_STR_LEN];
  bool leading_zero_12h;
  bool night_shift_enabled;
  bool wifi_ready;
  bool location_ready;
  uint32_t boot_count;
} device_config_t;

void device_config_set_defaults(device_config_t *config);
esp_err_t device_config_load_or_init(device_config_t *config);
esp_err_t device_config_save(device_config_t *config);
