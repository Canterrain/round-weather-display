#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "app_runtime.h"
#include "device_config.h"

esp_err_t message_service_init(const device_config_t *config);
esp_err_t message_service_get_snapshot(
  const char *device_id,
  app_message_snapshot_t *out_snapshot,
  uint32_t *out_unread_count,
  bool *out_has_important
);
esp_err_t message_service_acknowledge_active(const char *device_id);
