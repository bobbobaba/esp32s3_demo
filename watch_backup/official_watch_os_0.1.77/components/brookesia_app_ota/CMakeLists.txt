idf_component_register(
    SRCS "ota_app.cpp"
    INCLUDE_DIRS "."
    REQUIRES brookesia_core watch_app_icons watch_connectivity esp_http_client app_update esp_wifi esp_timer esp_system esp_partition espressif__cjson waveshare__esp32_s3_touch_amoled_2_06
    WHOLE_ARCHIVE
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
