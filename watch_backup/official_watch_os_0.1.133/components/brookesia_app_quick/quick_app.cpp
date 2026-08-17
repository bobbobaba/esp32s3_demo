#include "quick_app.hpp"

#include <cstdio>

#include "bsp/esp-bsp.h"
#include "esp_brookesia.hpp"
#include "esp_lib_utils.h"
#include "lvgl.h"
#include "watch_fonts.hpp"
#include "watch_app_icons.hpp"
#include "watch_audio.hpp"
#include "watch_connectivity.hpp"
#include "watch_power.hpp"

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "QuickApp"

namespace esp_brookesia::apps {
namespace {
constexpr const char *APP_NAME = "Quick";
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

lv_obj_t *make_card(lv_obj_t *parent, int height)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, height);
    lv_obj_set_style_radius(card, 24, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x15171D), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x2B303A), 0);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 8, 0);
    return card;
}

void style_button(lv_obj_t *button, uint32_t bg)
{
    lv_obj_set_style_radius(button, 24, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
}
} // namespace

QuickApp *QuickApp::_instance = nullptr;

QuickApp *QuickApp::requestInstance()
{
    if (_instance == nullptr) {
        _instance = new QuickApp();
    }
    return _instance;
}

QuickApp::QuickApp(): systems::phone::App(APP_NAME, watch_app_icon_quick_48(), true, true, true) {}

bool QuickApp::run(void)
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
    lv_obj_set_style_pad_bottom(root, 24, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(root, 10, 0);

    make_label(root, "Quick", &lv_font_montserrat_30, 0xFFFFFF);

    lv_obj_t *status_card = make_card(root, 104);
    make_label(status_card, "Status", &lv_font_montserrat_20, 0xFFFFFF);
    _wifi_label = make_label(status_card, "WiFi --", &lv_font_montserrat_16, 0xAEB7C6);
    lv_obj_set_width(_wifi_label, LV_PCT(100));
    _battery_label = make_label(status_card, "Battery --", &lv_font_montserrat_16, 0xAEB7C6);
    lv_obj_set_width(_battery_label, LV_PCT(100));

    lv_obj_t *brightness_card = make_card(root, 104);
    make_label(brightness_card, "Brightness", &lv_font_montserrat_20, 0xFFFFFF);
    _brightness_label = make_label(brightness_card, "--%", &lv_font_montserrat_16, 0xAEB7C6);
    lv_obj_t *brightness_slider = lv_slider_create(brightness_card);
    lv_obj_set_width(brightness_slider, LV_PCT(100));
    lv_slider_set_range(brightness_slider, 5, 100);
    int brightness = bsp_display_brightness_get();
    lv_slider_set_value(brightness_slider, brightness > 0 ? brightness : 80, LV_ANIM_OFF);
    lv_obj_add_event_cb(brightness_slider, onBrightnessChanged, LV_EVENT_VALUE_CHANGED, this);

    lv_obj_t *volume_card = make_card(root, 104);
    make_label(volume_card, "Volume", &lv_font_montserrat_20, 0xFFFFFF);
    _volume_label = make_label(volume_card, "--%", &lv_font_montserrat_16, 0xAEB7C6);
    lv_obj_t *volume_slider = lv_slider_create(volume_card);
    lv_obj_set_width(volume_slider, LV_PCT(100));
    lv_slider_set_range(volume_slider, 0, 100);
    lv_slider_set_value(volume_slider, watch::audio_get_volume(), LV_ANIM_OFF);
    lv_obj_add_event_cb(volume_slider, onVolumeChanged, LV_EVENT_VALUE_CHANGED, this);

    lv_obj_t *button_row = lv_obj_create(root);
    ESP_UTILS_CHECK_NULL_RETURN(button_row, false, "Create button row failed");
    lv_obj_remove_style_all(button_row);
    lv_obj_set_width(button_row, LV_PCT(100));
    lv_obj_set_height(button_row, 50);
    lv_obj_set_flex_flow(button_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(button_row, 10, 0);

    lv_obj_t *refresh_btn = lv_button_create(button_row);
    lv_obj_set_width(refresh_btn, LV_PCT(48));
    lv_obj_set_height(refresh_btn, 48);
    style_button(refresh_btn, 0x1B6BFF);
    lv_obj_add_event_cb(refresh_btn, onRefreshClicked, LV_EVENT_CLICKED, this);
    lv_obj_center(make_label(refresh_btn, "Refresh", &lv_font_montserrat_18, 0xFFFFFF));

    lv_obj_t *back_btn = lv_button_create(button_row);
    lv_obj_set_width(back_btn, LV_PCT(48));
    lv_obj_set_height(back_btn, 48);
    style_button(back_btn, 0x232833);
    lv_obj_add_event_cb(back_btn, onBackClicked, LV_EVENT_CLICKED, this);
    lv_obj_center(make_label(back_btn, "Back", &lv_font_montserrat_18, 0xFFFFFF));

    refresh();
    _timer = lv_timer_create(onTimer, 2000, this);
    return true;
}

bool QuickApp::back(void)
{
    if (_timer != nullptr) {
        lv_timer_delete(_timer);
        _timer = nullptr;
    }
    _wifi_label = nullptr;
    _battery_label = nullptr;
    _brightness_label = nullptr;
    _volume_label = nullptr;
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

void QuickApp::onBackClicked(lv_event_t *event)
{
    auto *app = static_cast<QuickApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->back();
}

void QuickApp::onRefreshClicked(lv_event_t *event)
{
    auto *app = static_cast<QuickApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->refresh();
}

void QuickApp::onBrightnessChanged(lv_event_t *event)
{
    auto *app = static_cast<QuickApp *>(lv_event_get_user_data(event));
    lv_obj_t *slider = static_cast<lv_obj_t *>(lv_event_get_target(event));
    if ((app == nullptr) || (slider == nullptr)) return;
    const int value = static_cast<int>(lv_slider_get_value(slider));
    bsp_display_brightness_set(value);
    app->refresh();
}

void QuickApp::onVolumeChanged(lv_event_t *event)
{
    auto *app = static_cast<QuickApp *>(lv_event_get_user_data(event));
    lv_obj_t *slider = static_cast<lv_obj_t *>(lv_event_get_target(event));
    if ((app == nullptr) || (slider == nullptr)) return;
    watch::audio_set_volume(static_cast<int>(lv_slider_get_value(slider)));
    app->refresh();
}

void QuickApp::onTimer(lv_timer_t *timer)
{
    auto *app = static_cast<QuickApp *>(lv_timer_get_user_data(timer));
    if (app != nullptr) app->refresh();
}

void QuickApp::refresh()
{
    if (_wifi_label != nullptr) {
        char short_status[64] = {};
        watch::wifi_short_status_text(short_status, sizeof(short_status));
        char text[96] = {};
        std::snprintf(text, sizeof(text), "WiFi  %s", short_status);
        lv_label_set_text(_wifi_label, text);
    }
    if (_battery_label != nullptr) {
        const int percent = watch::power_battery_percent();
        char text[48] = {};
        if (percent >= 0) {
            std::snprintf(text, sizeof(text), "%s  %d%%", watch::power_is_charging() ? "Charging" : "Battery", percent);
        } else {
            std::snprintf(text, sizeof(text), "Battery  --%%");
        }
        lv_label_set_text(_battery_label, text);
    }
    if (_brightness_label != nullptr) {
        char text[16] = {};
        std::snprintf(text, sizeof(text), "%d%%", bsp_display_brightness_get());
        lv_label_set_text(_brightness_label, text);
    }
    if (_volume_label != nullptr) {
        char text[16] = {};
        std::snprintf(text, sizeof(text), "%d%%", watch::audio_get_volume());
        lv_label_set_text(_volume_label, text);
    }
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, QuickApp, APP_NAME, []()
{
    return std::shared_ptr<QuickApp>(QuickApp::requestInstance(), [](QuickApp *) {});
})

} // namespace esp_brookesia::apps
