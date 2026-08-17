idf_component_register(
    SRCS "quick_app.cpp"
    INCLUDE_DIRS "." "../watch_display"
    REQUIRES brookesia_core waveshare__esp32_s3_touch_amoled_2_06 watch_app_icons watch_audio watch_connectivity watch_power
    WHOLE_ARCHIVE
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
