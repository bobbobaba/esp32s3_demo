#include "sensors_app.hpp"

#include <cstdio>

#include "esp_brookesia.hpp"
#include "esp_heap_caps.h"
#include "esp_lib_utils.h"
#include "freertos/idf_additions.h"
#include "lvgl.h"
#include "watch_fonts.hpp"
#include "watch_sensor.hpp"
#include "watch_app_icons.hpp"

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "SensorsApp"

namespace esp_brookesia::apps {
namespace {
constexpr const char *APP_NAME = "Sensors";
constexpr int SAFE_TOP = 24;
constexpr int SAFE_SIDE = 22;

lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    watch_display::apply_text_font(label, text, font);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

void style_button(lv_obj_t *button, uint32_t bg)
{
    lv_obj_set_style_radius(button, 24, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
}
} // namespace

SensorsApp *SensorsApp::_instance = nullptr;

SensorsApp *SensorsApp::requestInstance()
{
    if (_instance == nullptr) {
        _instance = new SensorsApp();
    }
    return _instance;
}

SensorsApp::SensorsApp(): systems::phone::App(APP_NAME, watch_app_icon_sensors_48(), true, true, true) {}

bool SensorsApp::run(void)
{
    lv_obj_t *screen = lv_scr_act();
    ESP_UTILS_CHECK_NULL_RETURN(screen, false, "Invalid active screen");
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x07080B), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t *root = lv_obj_create(screen);
    ESP_UTILS_CHECK_NULL_RETURN(root, false, "Create root failed");
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root, lv_color_hex(0x07080B), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(root, SAFE_SIDE, 0);
    lv_obj_set_style_pad_right(root, SAFE_SIDE, 0);
    lv_obj_set_style_pad_top(root, SAFE_TOP, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(root, 14, 0);

    lv_obj_t *title = make_label(root, "Sensors", &lv_font_montserrat_30, 0xFFFFFF);
    lv_obj_set_style_text_letter_space(title, -1, 0);

    _value_label = make_label(root, "Checking sensor...", &lv_font_montserrat_18, 0xAEB7C6);
    lv_obj_set_width(_value_label, LV_PCT(100));
    lv_label_set_long_mode(_value_label, LV_LABEL_LONG_WRAP);

    auto *button_row = lv_obj_create(root);
    lv_obj_remove_style_all(button_row);
    lv_obj_set_width(button_row, LV_PCT(100));
    lv_obj_set_height(button_row, 48);
    lv_obj_set_flex_flow(button_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *refresh_btn = lv_button_create(button_row);
    lv_obj_set_width(refresh_btn, LV_PCT(48));
    lv_obj_set_height(refresh_btn, 48);
    style_button(refresh_btn, 0x1B6BFF);
    lv_obj_add_event_cb(refresh_btn, onRefreshClicked, LV_EVENT_CLICKED, this);
    lv_obj_center(make_label(refresh_btn, "Refresh", &lv_font_montserrat_16, 0xFFFFFF));

    lv_obj_t *cal_btn = lv_button_create(button_row);
    lv_obj_set_width(cal_btn, LV_PCT(48));
    lv_obj_set_height(cal_btn, 48);
    style_button(cal_btn, 0x17A36B);
    lv_obj_add_event_cb(cal_btn, onCalibrateClicked, LV_EVENT_CLICKED, this);
    lv_obj_center(make_label(cal_btn, "Calibrate", &lv_font_montserrat_16, 0xFFFFFF));

    auto *button_row2 = lv_obj_create(root);
    lv_obj_remove_style_all(button_row2);
    lv_obj_set_width(button_row2, LV_PCT(100));
    lv_obj_set_height(button_row2, 48);
    lv_obj_set_flex_flow(button_row2, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button_row2, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *reset_btn = lv_button_create(button_row2);
    lv_obj_set_width(reset_btn, LV_PCT(48));
    lv_obj_set_height(reset_btn, 48);
    style_button(reset_btn, 0x7A4DFF);
    lv_obj_add_event_cb(reset_btn, onResetCalClicked, LV_EVENT_CLICKED, this);
    lv_obj_center(make_label(reset_btn, "Reset Cal", &lv_font_montserrat_16, 0xFFFFFF));

    lv_obj_t *back = lv_button_create(button_row2);
    lv_obj_set_width(back, LV_PCT(48));
    lv_obj_set_height(back, 48);
    style_button(back, 0x232833);
    lv_obj_add_event_cb(back, onBackClicked, LV_EVENT_CLICKED, this);
    lv_obj_center(make_label(back, "Back", &lv_font_montserrat_16, 0xFFFFFF));

    refresh();
    _timer = lv_timer_create(onTimer, 500, this);
    startSensorTask();
    return true;
}

bool SensorsApp::back(void)
{
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

bool SensorsApp::close(void)
{
    if (_timer != nullptr) {
        lv_timer_delete(_timer);
        _timer = nullptr;
    }
    _stop_requested = true;
    _value_label = nullptr;
    return true;
}

void SensorsApp::onBackClicked(lv_event_t *event)
{
    auto *app = static_cast<SensorsApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->back();
}

void SensorsApp::onRefreshClicked(lv_event_t *event)
{
    auto *app = static_cast<SensorsApp *>(lv_event_get_user_data(event));
    if (app != nullptr) {
        app->_force_refresh = true;
        app->refresh();
    }
}

void SensorsApp::onCalibrateClicked(lv_event_t *event)
{
    auto *app = static_cast<SensorsApp *>(lv_event_get_user_data(event));
    if (app != nullptr) {
        const char *message = nullptr;
        if (watch::sensor_calibrate_neutral() == ESP_OK) {
            message = "Calibrated neutral pose.\nReading sensor...";
        } else {
            message = "Calibration failed.\nCheck IMU.";
        }
        if ((app->_text_mutex == nullptr) || (xSemaphoreTake(app->_text_mutex, pdMS_TO_TICKS(50)) == pdTRUE)) {
            std::snprintf(app->_sensor_text, sizeof(app->_sensor_text), "%s", message);
            if (app->_text_mutex != nullptr) {
                xSemaphoreGive(app->_text_mutex);
            }
        }
        app->_force_refresh = true;
        app->refresh();
    }
}

void SensorsApp::onResetCalClicked(lv_event_t *event)
{
    auto *app = static_cast<SensorsApp *>(lv_event_get_user_data(event));
    if (app != nullptr) {
        watch::sensor_reset_calibration();
        if ((app->_text_mutex == nullptr) || (xSemaphoreTake(app->_text_mutex, pdMS_TO_TICKS(50)) == pdTRUE)) {
            std::snprintf(app->_sensor_text, sizeof(app->_sensor_text), "%s", "Calibration reset.\nReading sensor...");
            if (app->_text_mutex != nullptr) {
                xSemaphoreGive(app->_text_mutex);
            }
        }
        app->_force_refresh = true;
        app->refresh();
    }
}

void SensorsApp::onTimer(lv_timer_t *timer)
{
    auto *app = static_cast<SensorsApp *>(lv_timer_get_user_data(timer));
    if (app != nullptr) app->refresh();
}

void SensorsApp::sensorTask(void *arg)
{
    auto *app = static_cast<SensorsApp *>(arg);
    if (app == nullptr) {
        vTaskDeleteWithCaps(nullptr);
        return;
    }
    while (!app->_stop_requested) {
        char text[sizeof(app->_sensor_text)] = {};
        watch::sensor_status_text(text, sizeof(text));
        if ((app->_text_mutex == nullptr) || (xSemaphoreTake(app->_text_mutex, pdMS_TO_TICKS(50)) == pdTRUE)) {
            std::snprintf(app->_sensor_text, sizeof(app->_sensor_text), "%s", text);
            if (app->_text_mutex != nullptr) {
                xSemaphoreGive(app->_text_mutex);
            }
        }
        app->_force_refresh = false;
        for (int i = 0; i < 5 && !app->_stop_requested && !app->_force_refresh; ++i) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    app->_task = nullptr;
    vTaskDeleteWithCaps(nullptr);
}

void SensorsApp::startSensorTask()
{
    _stop_requested = false;
    _force_refresh = true;
    if (_text_mutex == nullptr) {
        _text_mutex = xSemaphoreCreateMutex();
    }
    if (_task == nullptr) {
        xTaskCreateWithCaps(sensorTask, "sensor_status", 4096, this, 3, &_task, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
}

void SensorsApp::refresh()
{
    if (_value_label == nullptr) return;
    char text[sizeof(_sensor_text)] = {};
    if ((_text_mutex == nullptr) || (xSemaphoreTake(_text_mutex, 0) == pdTRUE)) {
        std::snprintf(text, sizeof(text), "%s", _sensor_text);
        if (_text_mutex != nullptr) {
            xSemaphoreGive(_text_mutex);
        }
    } else {
        std::snprintf(text, sizeof(text), "%s", "Reading sensor...");
    }
    lv_label_set_text(_value_label, text);
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, SensorsApp, APP_NAME, []()
{
    return std::shared_ptr<SensorsApp>(SensorsApp::requestInstance(), [](SensorsApp *) {});
})

} // namespace esp_brookesia::apps
