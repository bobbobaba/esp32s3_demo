#include "audio_lite_app.hpp"

#include <cstdio>

#include "esp_brookesia.hpp"
#include "esp_lib_utils.h"
#include "lvgl.h"
#include "watch_fonts.hpp"
#include "watch_audio.hpp"
#include "watch_app_icons.hpp"

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "AudioLite"

namespace esp_brookesia::apps {
namespace {
constexpr const char *APP_NAME = "Audio";
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

AudioLiteApp *AudioLiteApp::_instance = nullptr;

AudioLiteApp *AudioLiteApp::requestInstance()
{
    if (_instance == nullptr) {
        _instance = new AudioLiteApp();
    }
    return _instance;
}

AudioLiteApp::AudioLiteApp(): systems::phone::App(APP_NAME, watch_app_icon_audio_48(), true, true, true) {}

bool AudioLiteApp::run(void)
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

    make_label(root, "Audio", &lv_font_montserrat_30, 0xFFFFFF);
    _status_label = make_label(root, "Checking audio...", &lv_font_montserrat_16, 0xAEB7C6);
    lv_obj_set_width(_status_label, LV_PCT(100));
    lv_label_set_long_mode(_status_label, LV_LABEL_LONG_WRAP);

    _volume_label = make_label(root, "--%", &lv_font_montserrat_18, 0xFFFFFF);
    lv_obj_t *slider = lv_slider_create(root);
    lv_obj_set_width(slider, LV_PCT(100));
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, watch::audio_get_volume(), LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, onVolumeChanged, LV_EVENT_VALUE_CHANGED, this);

    lv_obj_t *test = lv_button_create(root);
    lv_obj_set_width(test, LV_PCT(100));
    lv_obj_set_height(test, 52);
    style_button(test, 0x1B6BFF);
    lv_obj_add_event_cb(test, onTestClicked, LV_EVENT_CLICKED, this);
    lv_obj_center(make_label(test, "Play Test Tone", &lv_font_montserrat_18, 0xFFFFFF));

    lv_obj_t *back = lv_button_create(root);
    lv_obj_set_width(back, LV_PCT(100));
    lv_obj_set_height(back, 48);
    style_button(back, 0x232833);
    lv_obj_add_event_cb(back, onBackClicked, LV_EVENT_CLICKED, this);
    lv_obj_center(make_label(back, "Back", &lv_font_montserrat_18, 0xFFFFFF));

    refresh();
    return true;
}

bool AudioLiteApp::back(void)
{
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

void AudioLiteApp::onBackClicked(lv_event_t *event)
{
    auto *app = static_cast<AudioLiteApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->back();
}

void AudioLiteApp::onTestClicked(lv_event_t *event)
{
    auto *app = static_cast<AudioLiteApp *>(lv_event_get_user_data(event));
    if (app == nullptr) return;
    watch::audio_play_test_tone_async();
    app->refresh();
}

void AudioLiteApp::onVolumeChanged(lv_event_t *event)
{
    auto *app = static_cast<AudioLiteApp *>(lv_event_get_user_data(event));
    lv_obj_t *slider = static_cast<lv_obj_t *>(lv_event_get_target(event));
    if ((app == nullptr) || (slider == nullptr)) return;
    watch::audio_set_volume(static_cast<int>(lv_slider_get_value(slider)));
    app->refresh();
}

void AudioLiteApp::refresh()
{
    if (_status_label != nullptr) {
        char text[160] = {};
        watch::audio_status_text(text, sizeof(text));
        lv_label_set_text(_status_label, text);
    }
    if (_volume_label != nullptr) {
        char text[16] = {};
        std::snprintf(text, sizeof(text), "%d%%", watch::audio_get_volume());
        lv_label_set_text(_volume_label, text);
    }
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, AudioLiteApp, APP_NAME, []()
{
    return std::shared_ptr<AudioLiteApp>(AudioLiteApp::requestInstance(), [](AudioLiteApp *) {});
})

} // namespace esp_brookesia::apps
