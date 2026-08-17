idf_component_register(
    SRCS "gallery_app.cpp"
    INCLUDE_DIRS "." "../watch_display"
    REQUIRES brookesia_core watch_storage watch_display watch_app_icons watch_connectivity watch_audio esp_http_client cjson watch_private_config heap
    WHOLE_ARCHIVE
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
