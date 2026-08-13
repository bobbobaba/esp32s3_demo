idf_component_register(
    SRCS "cloud_app.cpp"
    INCLUDE_DIRS "."
    REQUIRES brookesia_core watch_app_icons watch_connectivity esp_http_client espressif__cjson freertos esp_timer
    WHOLE_ARCHIVE
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
