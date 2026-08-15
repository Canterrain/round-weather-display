#include "device_config.h"

#include <stddef.h>
#include <string.h>

#include "nvs.h"

#define DEVICE_CONFIG_NAMESPACE "rwd_cfg"
#define DEVICE_CONFIG_SETUP_FLOW_VERSION 1

static void copy_string(char *dest, size_t dest_size, const char *src)
{
  if (dest == NULL || dest_size == 0) {
    return;
  }

  if (src == NULL) {
    dest[0] = '\0';
    return;
  }

  size_t copy_len = strlen(src);
  if (copy_len >= dest_size) {
    copy_len = dest_size - 1;
  }

  memcpy(dest, src, copy_len);
  dest[copy_len] = '\0';
}

static esp_err_t load_or_seed_string(
  nvs_handle_t handle,
  const char *key,
  char *dest,
  size_t dest_size,
  const char *fallback,
  bool *dirty
)
{
  size_t required_size = dest_size;
  esp_err_t err = nvs_get_str(handle, key, dest, &required_size);
  if (err == ESP_OK) {
    return ESP_OK;
  }

  if (err != ESP_ERR_NVS_NOT_FOUND) {
    return err;
  }

  copy_string(dest, dest_size, fallback);
  err = nvs_set_str(handle, key, dest);
  if (err == ESP_OK && dirty != NULL) {
    *dirty = true;
  }

  return err;
}

static esp_err_t save_string(nvs_handle_t handle, const char *key, const char *value)
{
  return nvs_set_str(handle, key, value != NULL ? value : "");
}

void device_config_set_defaults(device_config_t *config)
{
  if (config == NULL) {
    return;
  }

  memset(config, 0, sizeof(*config));
  copy_string(config->device_id, sizeof(config->device_id), "clock-esp32-p4");
  copy_string(config->room_name, sizeof(config->room_name), "Clock");
  copy_string(config->location, sizeof(config->location), "Cincinnati,OH,US");
  copy_string(config->timezone, sizeof(config->timezone), "America/New_York");
  copy_string(config->units, sizeof(config->units), "imperial");
  copy_string(config->latitude, sizeof(config->latitude), "39.1031");
  copy_string(config->longitude, sizeof(config->longitude), "-84.5120");
  copy_string(config->message_sharing, sizeof(config->message_sharing), "single");
  copy_string(config->default_clock_face, sizeof(config->default_clock_face), "digital");
  copy_string(config->time_format, sizeof(config->time_format), "12");
  copy_string(config->night_shift_start, sizeof(config->night_shift_start), "22:00");
  copy_string(config->night_shift_end, sizeof(config->night_shift_end), "06:00");
  config->leading_zero_12h = true;
  config->night_shift_enabled = false;
  config->wifi_ready = false;
  config->location_ready = false;
  config->boot_count = 0;
}

esp_err_t device_config_load_or_init(device_config_t *config)
{
  if (config == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  device_config_set_defaults(config);

  nvs_handle_t handle = 0;
  esp_err_t err = nvs_open(DEVICE_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    return err;
  }

  bool dirty = false;

  err = load_or_seed_string(
    handle,
    "device_id",
    config->device_id,
    sizeof(config->device_id),
    config->device_id,
    &dirty
  );
  if (err != ESP_OK) {
    goto exit;
  }

  err = load_or_seed_string(
    handle,
    "room_name",
    config->room_name,
    sizeof(config->room_name),
    config->room_name,
    &dirty
  );
  if (err != ESP_OK) {
    goto exit;
  }

  err = load_or_seed_string(
    handle,
    "location",
    config->location,
    sizeof(config->location),
    config->location,
    &dirty
  );
  if (err != ESP_OK) {
    goto exit;
  }

  err = load_or_seed_string(
    handle,
    "timezone",
    config->timezone,
    sizeof(config->timezone),
    config->timezone,
    &dirty
  );
  if (err != ESP_OK) {
    goto exit;
  }

  err = load_or_seed_string(
    handle,
    "wifi_ssid",
    config->wifi_ssid,
    sizeof(config->wifi_ssid),
    "",
    &dirty
  );
  if (err != ESP_OK) {
    goto exit;
  }

  err = load_or_seed_string(
    handle,
    "wifi_pass",
    config->wifi_password,
    sizeof(config->wifi_password),
    "",
    &dirty
  );
  if (err != ESP_OK) {
    goto exit;
  }

  err = load_or_seed_string(
    handle,
    "msg_share",
    config->message_sharing,
    sizeof(config->message_sharing),
    config->message_sharing,
    &dirty
  );
  if (err != ESP_OK) {
    goto exit;
  }

  err = load_or_seed_string(
    handle,
    "clock_face",
    config->default_clock_face,
    sizeof(config->default_clock_face),
    config->default_clock_face,
    &dirty
  );
  if (err != ESP_OK) {
    goto exit;
  }

  err = load_or_seed_string(
    handle,
    "time_fmt",
    config->time_format,
    sizeof(config->time_format),
    config->time_format,
    &dirty
  );
  if (err != ESP_OK) {
    goto exit;
  }

  err = load_or_seed_string(
    handle,
    "units",
    config->units,
    sizeof(config->units),
    config->units,
    &dirty
  );
  if (err != ESP_OK) {
    goto exit;
  }

  err = load_or_seed_string(
    handle,
    "lat",
    config->latitude,
    sizeof(config->latitude),
    config->latitude,
    &dirty
  );
  if (err != ESP_OK) {
    goto exit;
  }

  err = load_or_seed_string(
    handle,
    "lon",
    config->longitude,
    sizeof(config->longitude),
    config->longitude,
    &dirty
  );
  if (err != ESP_OK) {
    goto exit;
  }

  err = load_or_seed_string(
    handle,
    "night_st",
    config->night_shift_start,
    sizeof(config->night_shift_start),
    config->night_shift_start,
    &dirty
  );
  if (err != ESP_OK) {
    goto exit;
  }

  err = load_or_seed_string(
    handle,
    "night_end",
    config->night_shift_end,
    sizeof(config->night_shift_end),
    config->night_shift_end,
    &dirty
  );
  if (err != ESP_OK) {
    goto exit;
  }

  uint8_t night_shift = 0;
  err = nvs_get_u8(handle, "night", &night_shift);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    err = nvs_set_u8(handle, "night", 0);
    if (err == ESP_OK) {
      dirty = true;
    }
  } else if (err != ESP_OK) {
    goto exit;
  }
  config->night_shift_enabled = (night_shift != 0);

  uint8_t leading_zero = 1;
  err = nvs_get_u8(handle, "lead_zero", &leading_zero);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    err = nvs_set_u8(handle, "lead_zero", 1);
    if (err == ESP_OK) {
      dirty = true;
    }
  } else if (err != ESP_OK) {
    goto exit;
  }
  config->leading_zero_12h = (leading_zero != 0);

  uint8_t location_ready = 0;
  err = nvs_get_u8(handle, "loc_ready", &location_ready);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    err = nvs_set_u8(handle, "loc_ready", 0);
    if (err == ESP_OK) {
      dirty = true;
    }
  } else if (err != ESP_OK) {
    goto exit;
  }
  config->location_ready = (location_ready != 0);

  uint8_t setup_flow_version = 0;
  err = nvs_get_u8(handle, "setup_ver", &setup_flow_version);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    setup_flow_version = 0;
  } else if (err != ESP_OK) {
    goto exit;
  }

  if (setup_flow_version < DEVICE_CONFIG_SETUP_FLOW_VERSION) {
    config->location_ready = false;
    err = nvs_set_u8(handle, "loc_ready", 0);
    if (err != ESP_OK) {
      goto exit;
    }
    err = nvs_set_u8(handle, "setup_ver", DEVICE_CONFIG_SETUP_FLOW_VERSION);
    if (err != ESP_OK) {
      goto exit;
    }
    dirty = true;
  }

  uint32_t boot_count = 0;
  err = nvs_get_u32(handle, "boot_count", &boot_count);
  if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
    goto exit;
  }

  config->boot_count = boot_count + 1;
  err = nvs_set_u32(handle, "boot_count", config->boot_count);
  if (err != ESP_OK) {
    goto exit;
  }
  dirty = true;

  config->wifi_ready = (config->wifi_ssid[0] != '\0');
  config->location_ready = (config->location_ready
    && config->latitude[0] != '\0'
    && config->longitude[0] != '\0'
    && config->timezone[0] != '\0');

  if (dirty) {
    err = nvs_commit(handle);
  }

exit:
  nvs_close(handle);
  return err;
}

esp_err_t device_config_save(device_config_t *config)
{
  if (config == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  config->wifi_ready = (config->wifi_ssid[0] != '\0');
  config->location_ready = (config->location_ready
    && config->latitude[0] != '\0'
    && config->longitude[0] != '\0'
    && config->timezone[0] != '\0');

  nvs_handle_t handle = 0;
  esp_err_t err = nvs_open(DEVICE_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    return err;
  }

  err = save_string(handle, "device_id", config->device_id);
  if (err != ESP_OK) {
    goto exit;
  }

  err = save_string(handle, "room_name", config->room_name);
  if (err != ESP_OK) {
    goto exit;
  }

  err = save_string(handle, "location", config->location);
  if (err != ESP_OK) {
    goto exit;
  }

  err = save_string(handle, "timezone", config->timezone);
  if (err != ESP_OK) {
    goto exit;
  }

  err = save_string(handle, "wifi_ssid", config->wifi_ssid);
  if (err != ESP_OK) {
    goto exit;
  }

  err = save_string(handle, "wifi_pass", config->wifi_password);
  if (err != ESP_OK) {
    goto exit;
  }

  err = save_string(handle, "msg_share", config->message_sharing);
  if (err != ESP_OK) {
    goto exit;
  }

  err = save_string(handle, "clock_face", config->default_clock_face);
  if (err != ESP_OK) {
    goto exit;
  }

  err = save_string(handle, "time_fmt", config->time_format);
  if (err != ESP_OK) {
    goto exit;
  }

  err = save_string(handle, "units", config->units);
  if (err != ESP_OK) {
    goto exit;
  }

  err = save_string(handle, "lat", config->latitude);
  if (err != ESP_OK) {
    goto exit;
  }

  err = save_string(handle, "lon", config->longitude);
  if (err != ESP_OK) {
    goto exit;
  }

  err = nvs_set_u8(handle, "loc_ready", config->location_ready ? 1 : 0);
  if (err != ESP_OK) {
    goto exit;
  }

  err = nvs_set_u8(handle, "setup_ver", DEVICE_CONFIG_SETUP_FLOW_VERSION);
  if (err != ESP_OK) {
    goto exit;
  }

  err = save_string(handle, "night_st", config->night_shift_start);
  if (err != ESP_OK) {
    goto exit;
  }

  err = save_string(handle, "night_end", config->night_shift_end);
  if (err != ESP_OK) {
    goto exit;
  }

  err = nvs_set_u8(handle, "night", config->night_shift_enabled ? 1 : 0);
  if (err != ESP_OK) {
    goto exit;
  }

  err = nvs_set_u8(handle, "lead_zero", config->leading_zero_12h ? 1 : 0);
  if (err != ESP_OK) {
    goto exit;
  }

  err = nvs_set_u32(handle, "boot_count", config->boot_count);
  if (err != ESP_OK) {
    goto exit;
  }

  err = nvs_commit(handle);

exit:
  nvs_close(handle);
  return err;
}
