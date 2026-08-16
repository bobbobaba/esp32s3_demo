idf_component_register(
    SRCS "ai_chat_app.cpp"
    INCLUDE_DIRS "." "../watch_display"
    REQUIRES brookesia_core watch_app_icons watch_connectivity watch_private_config watch_audio watch_storage esp_http_client espressif__cjson freertos esp_timer tcp_transport mbedtls
    WHOLE_ARCHIVE
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
