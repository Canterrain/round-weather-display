# Weather Icon Contract

Canonical weather icon assets live in:

- `shared/assets/icons/`

Canonical icon names are the SVG basenames, currently:

- `clear-day`
- `clear-night`
- `cloudy`
- `fog`
- `partlycloudy-day`
- `partlycloudy-night`
- `rain`
- `showers-day`
- `showers-night`
- `sleet`
- `snow`
- `thunderstorm`
- `thundersnow`

Current-condition icon selection uses the shared canonical names:

- `0` -> `clear-day` or `clear-night`
- `1`, `2` -> `partlycloudy-day` or `partlycloudy-night`
- `3` -> `cloudy`
- `45`, `48` -> `fog`
- `51` through `57` -> `showers-day` or `showers-night`
- `61` through `65` -> `rain`
- `66`, `67` -> `sleet`
- `71` through `77` -> `snow`
- `80` through `82` -> `showers-day` or `showers-night`
- `85`, `86` -> `snow`
- `95`, `96`, `99` -> `thunderstorm`
- any `thundersnow` override -> `thundersnow`

Forecast icon selection uses the same canonical names after the representative daytime forecast heuristic has already chosen the forecast weather code.

ESP32-P4 rendering path:

1. Open-Meteo current weather or representative forecast code
2. canonical shared icon name
3. ESP32 icon lookup adapter
4. generated LVGL image descriptor
5. `lv_image_set_src()`

Pi rendering path:

1. Open-Meteo current weather or representative forecast code
2. canonical shared icon name
3. `shared/assets/icons/<icon-name>.svg`
