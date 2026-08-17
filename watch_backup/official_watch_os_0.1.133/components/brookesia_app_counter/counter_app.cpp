#include "counter_app.hpp"

#include <cstdio>

#include "esp_brookesia.hpp"
#include "esp_lib_utils.h"
#include "lvgl.h"
#include "watch_app_icons.hpp"

#ifdef ESP_UTILS_LOG_TAG
#undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "CounterApp"

namespace esp_brookesia::apps {
namespace {
constexpr const char *APP_NAME = "Counter";

lv_obj_t *label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color)
{
    auto *obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    return obj;
}

lv_obj_t *button(lv_obj_t *parent, const char *text, uint32_t color, lv_event_cb_t cb, void *user)
{
    auto *obj = lv_button_create(parent);
    lv_obj_set_height(obj, 52);
    lv_obj_set_width(obj, LV_PCT(31));
    lv_obj_set_style_radius(obj, 22, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, user);
    lv_obj_center(label(obj, text, &lv_font_montserrat_18, 0xFFFFFF));
    return obj;
}
} // namespace

CounterApp *CounterApp::_instance = nullptr;

CounterApp *CounterApp::requestInstance()
{
    if (_instance == nullptr) _instance = new CounterApp();
    return _instance;
}

CounterApp::CounterApp() : systems::phone::App(APP_NAME, watch_app_icon_counter_48(), true, true, true) {}

bool CounterApp::run(void)
{
    auto *screen = lv_scr_act();
    ESP_UTILS_CHECK_NULL_RETURN(screen, false, "Invalid active screen");
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x07080B), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    auto *root = lv_obj_create(screen);
    ESP_UTILS_CHECK_NULL_RETURN(root, false, "Create root failed");
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_left(root, 22, 0);
    lv_obj_set_style_pad_right(root, 22, 0);
    lv_obj_set_style_pad_top(root, 26, 0);
    lv_obj_set_style_pad_row(root, 14, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    label(root, "Counter", &lv_font_montserrat_28, 0xFFFFFF);
    _value_label = label(root, "0", &lv_font_montserrat_48, 0x57D6FF);
    lv_obj_set_width(_value_label, LV_PCT(100));
    lv_obj_set_style_text_align(_value_label, LV_TEXT_ALIGN_CENTER, 0);
    auto *row = lv_obj_create(root);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 60);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 7, 0);
    // Use ASCII '-' because the bundled Montserrat font does not contain the
    // Unicode minus glyph and would render it as a square.
    button(row, "-", 0x354052, onMinus, this);
    button(row, "Reset", 0x7A3E4A, onReset, this);
    button(row, "+", 0x196B5A, onPlus, this);
    auto *back = lv_button_create(root);
    lv_obj_set_width(back, LV_PCT(100));
    lv_obj_set_height(back, 44);
    lv_obj_set_style_radius(back, 20, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x232833), 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(back, onBack, LV_EVENT_CLICKED, this);
    lv_obj_center(label(back, "Back", &lv_font_montserrat_16, 0xFFFFFF));
    return true;
}

bool CounterApp::back(void)
{
    _value_label = nullptr;
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

void CounterApp::updateLabel()
{
    if (_value_label == nullptr) return;
    char text[24];
    std::snprintf(text, sizeof(text), "%ld", static_cast<long>(_value));
    lv_label_set_text(_value_label, text);
}

void CounterApp::onPlus(lv_event_t *event)
{
    auto *app = static_cast<CounterApp *>(lv_event_get_user_data(event));
    if (app && app->_value < 999999) { ++app->_value; app->updateLabel(); }
}
void CounterApp::onMinus(lv_event_t *event)
{
    auto *app = static_cast<CounterApp *>(lv_event_get_user_data(event));
    if (app && app->_value > -999999) { --app->_value; app->updateLabel(); }
}
void CounterApp::onReset(lv_event_t *event)
{
    auto *app = static_cast<CounterApp *>(lv_event_get_user_data(event));
    if (app) { app->_value = 0; app->updateLabel(); }
}
void CounterApp::onBack(lv_event_t *event)
{
    auto *app = static_cast<CounterApp *>(lv_event_get_user_data(event));
    if (app) app->back();
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, CounterApp, APP_NAME, []()
{
    return std::shared_ptr<CounterApp>(CounterApp::requestInstance(), [](CounterApp *) {});
})

} // namespace esp_brookesia::apps
