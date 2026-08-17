idf_component_register(
    SRCS "watch_display.cpp" "lv_font_watch_zh_14.c"
    INCLUDE_DIRS "."
    REQUIRES waveshare__esp32_s3_touch_amoled_2_06 lvgl__lvgl nvs_flash esp_timer watch_sensor watch_connectivity
)
