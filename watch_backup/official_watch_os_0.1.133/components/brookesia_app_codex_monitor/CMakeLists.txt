idf_component_register(
    SRCS "codex_monitor_app.cpp"
    INCLUDE_DIRS "." "../watch_display"
    REQUIRES brookesia_core watch_app_icons watch_connectivity watch_audio esp_http_client espressif__cjson watch_private_config
    WHOLE_ARCHIVE
)
