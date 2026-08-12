idf_component_register(
    SRCS "watch_anniversary.cpp"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash watch_time
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
