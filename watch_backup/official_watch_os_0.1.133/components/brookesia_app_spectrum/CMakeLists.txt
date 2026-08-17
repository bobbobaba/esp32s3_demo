idf_component_register(
    SRCS "spectrum_app.cpp"
    INCLUDE_DIRS "." "../watch_display"
    REQUIRES brookesia_core watch_audio watch_app_icons espressif__esp-dsp
    WHOLE_ARCHIVE
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
