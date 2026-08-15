#pragma once

#include <stdbool.h>
#include <time.h>

#define APP_STATUS_TEXT_LEN 96
#define APP_WEATHER_SUMMARY_LEN 32
#define APP_WEATHER_ICON_NAME_LEN 24
#define APP_FORECAST_DAYS 5
#define APP_MESSAGE_ID_LEN 40
#define APP_MESSAGE_TEXT_LEN 181
#define APP_MESSAGE_SENDER_LEN 41
#define APP_MESSAGE_META_LEN 80

typedef struct {
  int high;
  int low;
  int code;
  bool is_day;
  bool thundersnow;
  char summary[APP_WEATHER_SUMMARY_LEN];
  char icon_name[APP_WEATHER_ICON_NAME_LEN];
} app_forecast_day_t;

typedef struct {
  bool valid;
  bool stale;
  time_t updated_at;
  int temp;
  int high;
  int low;
  int code;
  bool is_day;
  bool thundersnow;
  char summary[APP_WEATHER_SUMMARY_LEN];
  char icon_name[APP_WEATHER_ICON_NAME_LEN];
  app_forecast_day_t forecast[APP_FORECAST_DAYS];
} app_weather_snapshot_t;

typedef struct {
  bool available;
  bool important;
  char id[APP_MESSAGE_ID_LEN];
  char text[APP_MESSAGE_TEXT_LEN];
  char sender[APP_MESSAGE_SENDER_LEN];
  char meta[APP_MESSAGE_META_LEN];
} app_message_snapshot_t;

typedef struct {
  bool wifi_configured;
  bool wifi_connected;
  bool time_synced;
  bool weather_fetch_in_progress;
  uint32_t unread_message_count;
  bool has_important_message;
  char weather_status[APP_STATUS_TEXT_LEN];
  app_weather_snapshot_t weather;
  app_message_snapshot_t message;
} app_runtime_state_t;
