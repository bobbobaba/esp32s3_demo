idf_component_register(
    SRCS "watch_time.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_netif esp_event lwip esp_timer freertos waveshare__esp32_s3_touch_amoled_2_06 watch_connectivity
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
