idf_component_register(
    SRCS "watch_power.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_i2c esp_timer nvs_flash waveshare__esp32_s3_touch_amoled_2_06
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
