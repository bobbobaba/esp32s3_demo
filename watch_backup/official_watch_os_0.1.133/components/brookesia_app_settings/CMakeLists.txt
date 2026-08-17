idf_component_register(
    SRCS "settings_app.cpp"
    INCLUDE_DIRS "." "../watch_display"
    REQUIRES brookesia_core esp_wifi esp_hw_support esp_app_format app_update heap esp_timer esp_pm waveshare__esp32_s3_touch_amoled_2_06 watch_connectivity watch_display watch_power watch_time watch_sensor watch_storage watch_audio watch_app_icons
    WHOLE_ARCHIVE
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
