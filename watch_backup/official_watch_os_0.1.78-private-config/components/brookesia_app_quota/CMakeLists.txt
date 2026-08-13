idf_component_register(
    SRCS "quota_app.cpp"
    INCLUDE_DIRS "."
    REQUIRES brookesia_core watch_app_icons watch_connectivity watch_quota watch_private_config esp_http_client espressif__cjson freertos
    WHOLE_ARCHIVE
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
