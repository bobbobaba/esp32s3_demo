idf_component_register(
    SRCS "watch_storage.cpp"
    INCLUDE_DIRS "."
    REQUIRES waveshare__esp32_s3_touch_amoled_2_06 fatfs vfs joltwallet__littlefs watch_audio watch_connectivity esp_timer
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
