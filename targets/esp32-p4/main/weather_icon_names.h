#pragma once

#include <stdbool.h>

#include "app_runtime.h"

const char *weather_icon_fallback_name(void);
bool weather_icon_name_is_known(const char *icon_name);
const char *weather_icon_name_or_fallback(const char *icon_name);
const char *weather_icon_name_for_conditions(int code, bool is_day, bool thundersnow);
const char *weather_icon_name_for_snapshot(const app_weather_snapshot_t *weather);
