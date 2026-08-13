idf_component_register(
    SRCS "watch_alarm.cpp"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
