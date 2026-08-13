idf_component_register(
    SRCS "music_player_app.cpp"
    INCLUDE_DIRS "."
    REQUIRES brookesia_core brookesia_app_squareline_demo watch_audio watch_storage watch_app_icons watch_connectivity esp_http_client espressif__cjson
    WHOLE_ARCHIVE
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
