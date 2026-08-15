#include "sdkconfig.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "bsp/esp32_p4_wifi6_touch_lcd_xc.h"

#include "app_runtime.h"
#include "app_benchmark.h"
#include "app_parity_lab.h"
#include "app_ui.h"
#include "bsp_shims.h"
#include "connectivity.h"
#include "device_config.h"
#include "message_service.h"
#include "weather_client.h"

static const char *TAG = "rwd_esp32_p4";

static const uint32_t UI_HEARTBEAT_LOCK_TIMEOUT_MS = 250;
static const uint32_t INITIAL_UI_LOCK_TIMEOUT_MS = 1000;
static const uint32_t RUNTIME_LOCK_TIMEOUT_MS = 250;
static const uint32_t WIFI_CONNECT_TIMEOUT_MS = 30000;
static const uint32_t TIME_SYNC_TIMEOUT_MS = 20000;
static const uint32_t WEATHER_REFRESH_INTERVAL_MS = 10 * 60 * 1000;
static const uint32_t WEATHER_RETRY_INTERVAL_MS = 20000;

static device_config_t s_device_config;
static app_runtime_state_t s_runtime_state;
static SemaphoreHandle_t s_runtime_mutex;

static void log_device_clock(const char *context)
{
  time_t now = time(NULL);
  if (now < 1704067200) {
    ESP_LOGI(TAG, "%s: clock not synchronized yet", context);
    return;
  }

  struct tm local_time = {0};
  struct tm utc_time = {0};
  char local_buf[32] = {0};
  char utc_buf[32] = {0};

  localtime_r(&now, &local_time);
  gmtime_r(&now, &utc_time);
  strftime(local_buf, sizeof(local_buf), "%Y-%m-%d %H:%M:%S %Z", &local_time);
  strftime(utc_buf, sizeof(utc_buf), "%Y-%m-%d %H:%M:%S UTC", &utc_time);

  ESP_LOGI(TAG, "%s: local=%s utc=%s", context, local_buf, utc_buf);
}

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

static esp_err_t init_nvs(void)
{
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }

  return err;
}

static void runtime_state_seed(void)
{
  memset(&s_runtime_state, 0, sizeof(s_runtime_state));
  s_runtime_state.wifi_configured = s_device_config.wifi_ready;
  copy_text(
    s_runtime_state.weather_status,
    sizeof(s_runtime_state.weather_status),
    !s_device_config.wifi_ready
      ? "No Wi-Fi profile found. Tap 'Set Up Wi-Fi' to connect."
      : (s_device_config.location_ready
          ? "Stored Wi-Fi profile found. Connecting for time sync and live weather..."
          : "Stored Wi-Fi profile found. Finish location setup for local weather.")
  );
}

static void runtime_state_update(
  bool wifi_connected,
  bool time_synced,
  bool weather_fetch_in_progress,
  const app_weather_snapshot_t *weather,
  bool mark_weather_stale,
  const char *status_text
)
{
  if (s_runtime_mutex == NULL) {
    return;
  }

  if (xSemaphoreTake(s_runtime_mutex, pdMS_TO_TICKS(RUNTIME_LOCK_TIMEOUT_MS)) != pdTRUE) {
    ESP_LOGW(TAG, "Timed out waiting for runtime state lock");
    return;
  }

  s_runtime_state.wifi_configured = s_device_config.wifi_ready;
  s_runtime_state.wifi_connected = wifi_connected;
  s_runtime_state.time_synced = time_synced;
  s_runtime_state.weather_fetch_in_progress = weather_fetch_in_progress;

  if (weather != NULL) {
    s_runtime_state.weather = *weather;
  }

  if (mark_weather_stale && s_runtime_state.weather.valid) {
    s_runtime_state.weather.stale = true;
  }

  copy_text(s_runtime_state.weather_status, sizeof(s_runtime_state.weather_status), status_text);
  xSemaphoreGive(s_runtime_mutex);
}

static void runtime_state_snapshot(app_runtime_state_t *out_runtime)
{
  if (out_runtime == NULL) {
    return;
  }

  memset(out_runtime, 0, sizeof(*out_runtime));
  out_runtime->wifi_configured = s_device_config.wifi_ready;

  if (s_runtime_mutex == NULL) {
    return;
  }

  if (xSemaphoreTake(s_runtime_mutex, pdMS_TO_TICKS(RUNTIME_LOCK_TIMEOUT_MS)) != pdTRUE) {
    return;
  }

  *out_runtime = s_runtime_state;
  xSemaphoreGive(s_runtime_mutex);

  app_message_snapshot_t message_snapshot = {0};
  uint32_t unread_count = 0;
  bool has_important_message = false;
  if (message_service_get_snapshot(
        s_device_config.device_id,
        &message_snapshot,
        &unread_count,
        &has_important_message
      ) == ESP_OK) {
    out_runtime->message = message_snapshot;
    out_runtime->unread_message_count = unread_count;
    out_runtime->has_important_message = has_important_message;
  }
}

static bool runtime_state_has_valid_weather(void)
{
  bool has_valid_weather = false;

  if (s_runtime_mutex == NULL) {
    return false;
  }

  if (xSemaphoreTake(s_runtime_mutex, pdMS_TO_TICKS(RUNTIME_LOCK_TIMEOUT_MS)) != pdTRUE) {
    return false;
  }

  has_valid_weather = s_runtime_state.weather.valid;
  xSemaphoreGive(s_runtime_mutex);
  return has_valid_weather;
}

static void ui_heartbeat_task(void *arg)
{
  (void) arg;

  const TickType_t delay_ticks = pdMS_TO_TICKS(1000);
  uint32_t uptime_seconds = 0;

  while (true) {
    vTaskDelay(delay_ticks);
    uptime_seconds += 1;

    app_runtime_state_t runtime_snapshot;
    runtime_state_snapshot(&runtime_snapshot);

    if (bsp_display_lock(UI_HEARTBEAT_LOCK_TIMEOUT_MS) == ESP_OK) {
      app_ui_update_runtime(&s_device_config, &runtime_snapshot, uptime_seconds);
      bsp_display_unlock();
    }
  }
}

static void weather_service_task(void *arg)
{
  (void) arg;

  const TickType_t retry_delay_ticks = pdMS_TO_TICKS(WEATHER_RETRY_INTERVAL_MS);
  const TickType_t refresh_delay_ticks = pdMS_TO_TICKS(WEATHER_REFRESH_INTERVAL_MS);
  const TickType_t wifi_timeout_ticks = pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS);
  const TickType_t time_sync_timeout_ticks = pdMS_TO_TICKS(TIME_SYNC_TIMEOUT_MS);
  bool connectivity_started = false;
  bool reported_missing_wifi_profile = false;

  while (true) {
    bool time_synced = connectivity_is_time_valid();

    if (!s_device_config.wifi_ready) {
      if (!reported_missing_wifi_profile) {
        ESP_LOGI(TAG, "No stored Wi-Fi profile is available yet; waiting for setup");
        reported_missing_wifi_profile = true;
      }

      runtime_state_update(
        false,
        time_synced,
        false,
        NULL,
        false,
        "No Wi-Fi profile found. Tap 'Set Up Wi-Fi' to connect."
      );
      vTaskDelay(retry_delay_ticks);
      continue;
    }

    reported_missing_wifi_profile = false;

    if (!connectivity_started) {
      ESP_LOGI(TAG, "Initializing Wi-Fi station for SSID '%s'", s_device_config.wifi_ssid);
      runtime_state_update(
        false,
        time_synced,
        false,
        NULL,
        false,
        "Connecting to Wi-Fi..."
      );

      esp_err_t err = connectivity_init(&s_device_config);
      if (err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi init failed: %s", esp_err_to_name(err));
        runtime_state_update(
          false,
          time_synced,
          false,
          NULL,
          true,
          "Wi-Fi setup failed. Retrying..."
        );
        vTaskDelay(retry_delay_ticks);
        continue;
      }

      connectivity_started = true;
    }

    if (!connectivity_is_wifi_connected()) {
      ESP_LOGI(TAG, "Waiting up to %u ms for Wi-Fi connection", WIFI_CONNECT_TIMEOUT_MS);
      runtime_state_update(
        false,
        time_synced,
        false,
        NULL,
        true,
        "Connecting to Wi-Fi..."
      );

      esp_err_t err = connectivity_wait_for_wifi(wifi_timeout_ticks);
      if (err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi connect timeout: %s", esp_err_to_name(err));
        runtime_state_update(
          false,
          time_synced,
          false,
          NULL,
          true,
          "Still trying to join Wi-Fi..."
        );
        vTaskDelay(retry_delay_ticks);
        continue;
      }

      ESP_LOGI(TAG, "Wi-Fi association completed");
      time_synced = connectivity_is_time_valid();
    }

    if (!time_synced) {
      runtime_state_update(
        true,
        false,
        false,
        NULL,
        false,
        s_device_config.location_ready
          ? "Wi-Fi connected. Syncing time..."
          : "Wi-Fi connected. Syncing time while location is pending..."
      );

      ESP_LOGI(TAG, "Synchronizing clock for timezone '%s'", s_device_config.timezone);
      esp_err_t err = connectivity_sync_time(s_device_config.timezone, time_sync_timeout_ticks);
      if (err != ESP_OK) {
        ESP_LOGW(TAG, "Time sync failed: %s", esp_err_to_name(err));
        runtime_state_update(
          true,
          false,
          false,
          NULL,
          true,
          "Wi-Fi connected. Waiting for network time..."
        );
        vTaskDelay(retry_delay_ticks);
        continue;
      }

      time_synced = true;
      ESP_LOGI(TAG, "Network time synchronization completed");
      log_device_clock("Clock after SNTP sync");
    }

    if (!s_device_config.location_ready) {
      runtime_state_update(
        true,
        true,
        false,
        NULL,
        false,
        "Set your location to enable local weather."
      );
      vTaskDelay(retry_delay_ticks);
      continue;
    }

    runtime_state_update(true, true, true, NULL, false, "Fetching live weather...");
    ESP_LOGI(
      TAG,
      "Requesting live weather for %s,%s (%s units)",
      s_device_config.latitude,
      s_device_config.longitude,
      s_device_config.units
    );

    app_weather_snapshot_t weather_snapshot = {0};
    esp_err_t weather_err = weather_client_fetch(&s_device_config, &weather_snapshot);
    if (weather_err != ESP_OK) {
      bool had_previous_weather = runtime_state_has_valid_weather();
      ESP_LOGW(TAG, "Weather refresh failed: %s", esp_err_to_name(weather_err));
      runtime_state_update(
        true,
        true,
        false,
        NULL,
        true,
        had_previous_weather
          ? "Weather refresh failed. Showing last update until retry."
          : "Weather request failed. Retrying..."
      );
      vTaskDelay(retry_delay_ticks);
      continue;
    }

    runtime_state_update(true, true, false, &weather_snapshot, false, "");
    ESP_LOGI(
      TAG,
      "Live weather updated (%d°, H:%d° L:%d°)",
      weather_snapshot.temp,
      weather_snapshot.high,
      weather_snapshot.low
    );
    vTaskDelay(refresh_delay_ticks);
  }
}

void app_main(void)
{
  ESP_ERROR_CHECK(init_nvs());
  ESP_ERROR_CHECK(device_config_load_or_init(&s_device_config));
  connectivity_apply_timezone(s_device_config.timezone);
  log_device_clock("Clock after boot timezone apply");

  s_runtime_mutex = xSemaphoreCreateMutex();
  if (s_runtime_mutex == NULL) {
    ESP_LOGE(TAG, "Failed to allocate runtime mutex");
    return;
  }
  runtime_state_seed();
  ESP_ERROR_CHECK(connectivity_prepare_runtime());
  ESP_ERROR_CHECK(message_service_init(&s_device_config));

  ESP_LOGI(
    TAG,
    "Bootstrapping Round Weather Display on ESP32-P4 (%dx%d, room=%s, device=%s, boot=%lu)",
    BSP_LCD_H_RES,
    BSP_LCD_V_RES,
    s_device_config.room_name,
    s_device_config.device_id,
    (unsigned long) s_device_config.boot_count
  );

  lv_display_t *display = bsp_display_start();
  if (display == NULL) {
    ESP_LOGE(TAG, "Display start failed");
    return;
  }
  (void) display;

  bool touch_ready = (bsp_display_get_input_dev() != NULL);

#if CONFIG_RWD_ENABLE_CLOCK_BENCHMARK
  esp_err_t benchmark_lock_err = bsp_display_lock(INITIAL_UI_LOCK_TIMEOUT_MS);
  if (benchmark_lock_err != ESP_OK) {
    ESP_LOGE(TAG, "Could not lock LVGL during benchmark setup: %s", esp_err_to_name(benchmark_lock_err));
    return;
  }

  ESP_LOGI(TAG, "Clock benchmark mode enabled; normal product UI remains disabled in this build");
  esp_err_t benchmark_err = app_benchmark_start(display);
  bsp_display_unlock();
  if (benchmark_err != ESP_OK) {
    ESP_LOGE(TAG, "Clock benchmark start failed: %s", esp_err_to_name(benchmark_err));
    return;
  }

  esp_err_t benchmark_backlight_err = bsp_display_backlight_on();
  if (benchmark_backlight_err != ESP_OK) {
    ESP_LOGW(TAG, "Backlight enable returned %s", esp_err_to_name(benchmark_backlight_err));
  }

  return;
#endif

#if CONFIG_RWD_ENABLE_ANALOG_PARITY_LAB
  esp_err_t parity_lab_lock_err = bsp_display_lock(INITIAL_UI_LOCK_TIMEOUT_MS);
  if (parity_lab_lock_err != ESP_OK) {
    ESP_LOGE(TAG, "Could not lock LVGL during parity lab setup: %s", esp_err_to_name(parity_lab_lock_err));
    return;
  }

  ESP_LOGI(TAG, "Analog parity lab mode enabled; normal product UI remains disabled in this build");
  esp_err_t parity_lab_err = app_parity_lab_start(display);
  bsp_display_unlock();
  if (parity_lab_err != ESP_OK) {
    ESP_LOGE(TAG, "Analog parity lab start failed: %s", esp_err_to_name(parity_lab_err));
    return;
  }

  esp_err_t parity_lab_backlight_err = bsp_display_backlight_on();
  if (parity_lab_backlight_err != ESP_OK) {
    ESP_LOGW(TAG, "Backlight enable returned %s", esp_err_to_name(parity_lab_backlight_err));
  }

  return;
#endif

  esp_err_t lock_err = bsp_display_lock(INITIAL_UI_LOCK_TIMEOUT_MS);
  if (lock_err != ESP_OK) {
    ESP_LOGE(TAG, "Could not lock LVGL during initial draw: %s", esp_err_to_name(lock_err));
    return;
  }

  ESP_ERROR_CHECK(app_ui_show_boot_screen(&s_device_config, touch_ready));
  bsp_display_unlock();

  esp_err_t backlight_err = bsp_display_backlight_on();
  if (backlight_err != ESP_OK) {
    ESP_LOGW(TAG, "Backlight enable returned %s", esp_err_to_name(backlight_err));
  }

  ESP_LOGI(TAG, "Display ready, touch %s", touch_ready ? "detected" : "not detected");

  BaseType_t ui_task_ok = xTaskCreate(
    ui_heartbeat_task,
    "rwd_ui_heartbeat",
    4096,
    NULL,
    4,
    NULL
  );
  if (ui_task_ok != pdPASS) {
    ESP_LOGW(TAG, "Failed to start UI heartbeat task");
  }

  BaseType_t weather_task_ok = xTaskCreate(
    weather_service_task,
    "rwd_weather",
    8192,
    NULL,
    4,
    NULL
  );
  if (weather_task_ok != pdPASS) {
    ESP_LOGW(TAG, "Failed to start weather service task");
  }
}
