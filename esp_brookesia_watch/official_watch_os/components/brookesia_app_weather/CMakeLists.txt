idf_component_register(
    SRCS "weather_app.cpp"
    INCLUDE_DIRS "."
    REQUIRES brookesia_core watch_app_icons watch_connectivity watch_weather esp_timer
    WHOLE_ARCHIVE
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
