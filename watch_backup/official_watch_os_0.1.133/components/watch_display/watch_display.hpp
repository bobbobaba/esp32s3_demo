#pragma once

#include <cstddef>

#include "esp_err.h"
#include "lvgl.h"

namespace watch {

struct DisplaySettings {
    int screen_timeout_s = 15;
    int brightness_percent = 55;
    bool raise_wake_enabled = false;
};

esp_err_t display_settings_init();
DisplaySettings display_get_settings();
esp_err_t display_set_screen_timeout_s(int seconds);
esp_err_t display_set_brightness_percent(int percent);
esp_err_t display_set_raise_wake_enabled(bool enabled);
void display_notify_activity();
void display_wake();
void display_sleep();
bool display_is_on();
void display_tick(lv_indev_t *indev);
void display_status_text(char *buffer, size_t buffer_size);

} // namespace watch
