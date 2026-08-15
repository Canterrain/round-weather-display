set(APP_ASSET_SOURCES)
include("${CMAKE_CURRENT_LIST_DIR}/assets/generated_sources.cmake" OPTIONAL)

if(NOT APP_ASSET_SOURCES)
  file(GLOB APP_ASSET_SOURCES "${CMAKE_CURRENT_LIST_DIR}/assets/*.c")
endif()

idf_component_register(
  SRCS
    ${APP_ASSET_SOURCES}
    "main.c"
    "app_benchmark.c"
    "app_parity_lab.c"
    "app_ui.c"
    "connectivity.c"
    "device_config.c"
    "forecast_representative.c"
    "location_lookup.c"
    "location_scoring.c"
    "message_service.c"
    "timezone_table.c"
    "weather_client.c"
    "weather_icon_names.c"
  INCLUDE_DIRS
    "."
  EMBED_TXTFILES
    "web/messages.html"
  REQUIRES
    nvs_flash
    esp_event
    esp_http_client
    esp_http_server
    esp_netif
    esp_timer
    esp_wifi
    json
    lwip
    mbedtls
)
