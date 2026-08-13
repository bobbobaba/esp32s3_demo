idf_component_register(
    SRCS "anniversary_app.cpp"
    INCLUDE_DIRS "."
    REQUIRES brookesia_core watch_anniversary watch_app_icons
    WHOLE_ARCHIVE
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
