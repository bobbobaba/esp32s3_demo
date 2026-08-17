idf_component_register(
    SRCS "level_app.cpp"
    INCLUDE_DIRS "."
    REQUIRES brookesia_core watch_sensor watch_display watch_app_icons
    WHOLE_ARCHIVE
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
