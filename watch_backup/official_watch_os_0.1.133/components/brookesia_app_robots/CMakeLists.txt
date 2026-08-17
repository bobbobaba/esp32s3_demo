idf_component_register(
    SRCS "robots_app.cpp" "robot_image.cpp"
    INCLUDE_DIRS "."
    REQUIRES brookesia_core watch_app_icons watch_connectivity watch_display watch_private_config esp_http_client espressif__cjson freertos
    WHOLE_ARCHIVE
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
