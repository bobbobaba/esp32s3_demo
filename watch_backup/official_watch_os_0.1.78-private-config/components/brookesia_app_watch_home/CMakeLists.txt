idf_component_register(
    SRCS "watch_home_app.cpp"
    INCLUDE_DIRS "."
    REQUIRES brookesia_core watch_connectivity watch_display watch_app_icons watch_power watch_weather watch_anniversary watch_quota
    WHOLE_ARCHIVE
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
