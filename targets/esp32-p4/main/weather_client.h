#pragma once

#include "esp_err.h"

#include "app_runtime.h"
#include "device_config.h"

esp_err_t weather_client_fetch(
  const device_config_t *config,
  app_weather_snapshot_t *out_snapshot
);
