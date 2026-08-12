idf_component_register(
    SRCS "watch_audio.cpp"
    INCLUDE_DIRS "."
    REQUIRES waveshare__esp32_s3_touch_amoled_2_06 espressif__esp_codec_dev esp_timer freertos esp_driver_i2s chmorgan__esp-audio-player
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
