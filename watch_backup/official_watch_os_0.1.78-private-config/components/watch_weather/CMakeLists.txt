idf_component_register(
    SRCS "watch_weather.cpp"
    INCLUDE_DIRS "."
    REQUIRES watch_connectivity watch_private_config esp_http_client espressif__cjson esp_timer freertos
)
