idf_component_register(
    SRCS "calendar_app.cpp"
    INCLUDE_DIRS "." "../watch_display"
    REQUIRES brookesia_core watch_app_icons watch_time
    WHOLE_ARCHIVE
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
