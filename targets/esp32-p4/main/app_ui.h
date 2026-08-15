#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "app_runtime.h"
#include "device_config.h"

esp_err_t app_ui_show_boot_screen(const device_config_t *config, bool touch_ready);
esp_err_t app_ui_update_runtime(
  const device_config_t *config,
  const app_runtime_state_t *runtime,
  uint32_t uptime_seconds
);
