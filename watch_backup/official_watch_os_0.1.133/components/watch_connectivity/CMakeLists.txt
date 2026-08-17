idf_component_register(
    SRCS "watch_connectivity.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_netif esp_event nvs_flash esp_http_server lwip esp_timer freertos
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
