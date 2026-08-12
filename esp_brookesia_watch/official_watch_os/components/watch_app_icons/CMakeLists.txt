set(BROOKESIA_SPEAKER_ICON_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../brookesia_core/systems/speaker/assets/images/middle")

idf_component_register(
    SRCS
        "watch_app_icons.cpp"
        "${BROOKESIA_SPEAKER_ICON_DIR}/speaker_image_middle_quick_settings_settings_48_48.c"
        "${BROOKESIA_SPEAKER_ICON_DIR}/speaker_image_middle_quick_settings_wifi_48_48.c"
        "${BROOKESIA_SPEAKER_ICON_DIR}/speaker_image_middle_quick_settings_volume_high_48_48.c"
        "${BROOKESIA_SPEAKER_ICON_DIR}/speaker_image_middle_quick_settings_brightness_auto_48_48.c"
        "${BROOKESIA_SPEAKER_ICON_DIR}/speaker_image_middle_quick_settings_lock_48_48.c"
        "${BROOKESIA_SPEAKER_ICON_DIR}/speaker_image_middle_quick_settings_bluetooth_48_48.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl
)
