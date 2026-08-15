#pragma once

#include "esp_err.h"

/*
 * These helpers are documented by the current Waveshare XC BSP and implemented
 * in the upstream component, but they are not surfaced in the public header we
 * are including from this scaffold.
 */
esp_err_t bsp_display_brightness_init(void);
esp_err_t bsp_display_brightness_set(int brightness_percent);
int bsp_display_brightness_get(void);
esp_err_t bsp_display_backlight_on(void);
esp_err_t bsp_display_backlight_off(void);
