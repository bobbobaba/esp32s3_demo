idf_component_register(
    SRCS "watch_quota.cpp"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_http_client espressif__cjson watch_connectivity
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
