#include "alarm_app.hpp"

#include <cstdio>

#include "esp_brookesia.hpp"
#include "esp_lib_utils.h"
#include "lvgl.h"
#include "watch_fonts.hpp"
#include "watch_app_icons.hpp"
#include "watch_audio.hpp"

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "AlarmApp"

namespace esp_brookesia::apps {
namespace {

constexpr const char *APP_NAME = "Alarm";
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

lv_obj_t *make_button(lv_obj_t *parent, const char *text, uint32_t bg, lv_event_cb_t cb, void *user_data, int width_pct)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_height(button, 48);
    lv_obj_set_width(button, LV_PCT(width_pct));
    style_button(button, bg);
    lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, user_data);
    lv_obj_t *label = make_label(button, text, &lv_font_montserrat_18, 0xFFFFFF);
    lv_obj_center(label);
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
    lv_obj_set_style_pad_row(card, 10, 0);
    return card;
}

} // namespace

AlarmApp *AlarmApp::_instance = nullptr;

AlarmApp *AlarmApp::requestInstance()
{
    if (_instance == nullptr) {
        _instance = new AlarmApp();
    }
    return _instance;
}

AlarmApp::AlarmApp():
    systems::phone::App(APP_NAME, watch_app_icon_alarm_48(), true, true, true)
{
}

bool AlarmApp::run(void)
{
    lv_obj_t *screen = lv_scr_act();
    ESP_UTILS_CHECK_NULL_RETURN(screen, false, "Invalid active screen");

    lv_obj_set_style_bg_color(screen, lv_color_hex(0x07080B), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

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
    lv_obj_set_style_pad_row(root, 12, 0);

    lv_obj_t *title_row = lv_obj_create(root);
    ESP_UTILS_CHECK_NULL_RETURN(title_row, false, "Create title row failed");
    lv_obj_remove_style_all(title_row);
    lv_obj_set_width(title_row, LV_PCT(100));
    lv_obj_set_height(title_row, 52);
    lv_obj_set_flex_flow(title_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    make_label(title_row, "Alarm", &lv_font_montserrat_30, 0xFFFFFF);
    lv_obj_t *back_btn = lv_button_create(title_row);
    lv_obj_set_size(back_btn, 92, 44);
    style_button(back_btn, 0x232833);
    lv_obj_add_event_cb(back_btn, onBackClicked, LV_EVENT_CLICKED, this);
    lv_obj_center(make_label(back_btn, "Back", &lv_font_montserrat_18, 0xFFFFFF));

    lv_obj_t *time_card = make_card(root, 184);
    _ring_label = make_label(time_card, "", &lv_font_montserrat_20, 0xFFCC33);
    lv_obj_set_width(_ring_label, LV_PCT(100));
    lv_obj_set_style_text_align(_ring_label, LV_TEXT_ALIGN_CENTER, 0);

    _time_label = make_label(time_card, "--:--", &lv_font_montserrat_48, 0xFFFFFF);
    lv_obj_set_width(_time_label, LV_PCT(100));
    lv_obj_set_style_text_align(_time_label, LV_TEXT_ALIGN_CENTER, 0);

    _state_label = make_label(time_card, "Loading alarm", &lv_font_montserrat_16, 0xAEB7C6);
    lv_obj_set_width(_state_label, LV_PCT(100));
    lv_obj_set_style_text_align(_state_label, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *hour_row = lv_obj_create(root);
    ESP_UTILS_CHECK_NULL_RETURN(hour_row, false, "Create hour row failed");
    lv_obj_remove_style_all(hour_row);
    lv_obj_set_width(hour_row, LV_PCT(100));
    lv_obj_set_height(hour_row, 50);
    lv_obj_set_flex_flow(hour_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(hour_row, 10, 0);
    make_button(hour_row, "Hour -", 0x232833, onHourMinusClicked, this, 48);
    make_button(hour_row, "Hour +", 0x1B6BFF, onHourPlusClicked, this, 48);

    lv_obj_t *minute_row = lv_obj_create(root);
    ESP_UTILS_CHECK_NULL_RETURN(minute_row, false, "Create minute row failed");
    lv_obj_remove_style_all(minute_row);
    lv_obj_set_width(minute_row, LV_PCT(100));
    lv_obj_set_height(minute_row, 50);
    lv_obj_set_flex_flow(minute_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(minute_row, 10, 0);
    make_button(minute_row, "Min -5", 0x232833, onMinuteMinusClicked, this, 48);
    make_button(minute_row, "Min +5", 0x1B6BFF, onMinutePlusClicked, this, 48);

    lv_obj_t *action_card = make_card(root, 194);
    _enable_label = make_button(action_card, "Enable", 0x23A559, onEnableClicked, this, 100);
    _repeat_label = make_button(action_card, "Repeat Daily", 0x232833, onRepeatClicked, this, 100);
    _stop_button = lv_button_create(action_card);
    lv_obj_set_width(_stop_button, LV_PCT(100));
    lv_obj_set_height(_stop_button, 48);
    style_button(_stop_button, 0xD9322E);
    lv_obj_add_event_cb(_stop_button, onStopClicked, LV_EVENT_CLICKED, this);
    lv_obj_center(make_label(_stop_button, "Stop Alarm", &lv_font_montserrat_18, 0xFFFFFF));

    refresh();
    _timer = lv_timer_create(onTimer, 2000, this);
    return true;
}

bool AlarmApp::back(void)
{
    if (_timer != nullptr) {
        lv_timer_delete(_timer);
        _timer = nullptr;
    }
    clearObjects();
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

bool AlarmApp::close(void)
{
    if (_timer != nullptr) {
        lv_timer_delete(_timer);
        _timer = nullptr;
    }
    clearObjects();
    return true;
}

void AlarmApp::clearObjects()
{
    _time_label = nullptr;
    _state_label = nullptr;
    _enable_label = nullptr;
    _repeat_label = nullptr;
    _stop_button = nullptr;
    _ring_label = nullptr;
}

void AlarmApp::onBackClicked(lv_event_t *event)
{
    auto *app = static_cast<AlarmApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->back();
}

void AlarmApp::onHourMinusClicked(lv_event_t *event)
{
    auto *app = static_cast<AlarmApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->adjustTime(-1, 0);
}

void AlarmApp::onHourPlusClicked(lv_event_t *event)
{
    auto *app = static_cast<AlarmApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->adjustTime(1, 0);
}

void AlarmApp::onMinuteMinusClicked(lv_event_t *event)
{
    auto *app = static_cast<AlarmApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->adjustTime(0, -5);
}

void AlarmApp::onMinutePlusClicked(lv_event_t *event)
{
    auto *app = static_cast<AlarmApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->adjustTime(0, 5);
}

void AlarmApp::onEnableClicked(lv_event_t *event)
{
    auto *app = static_cast<AlarmApp *>(lv_event_get_user_data(event));
    if (app == nullptr) return;
    watch::AlarmState state = {};
    watch::alarm_get_state(&state);
    app->setAlarmFromState(!state.enabled, state.repeat_daily);
}

void AlarmApp::onRepeatClicked(lv_event_t *event)
{
    auto *app = static_cast<AlarmApp *>(lv_event_get_user_data(event));
    if (app == nullptr) return;
    watch::AlarmState state = {};
    watch::alarm_get_state(&state);
    app->setAlarmFromState(state.enabled, !state.repeat_daily);
}

void AlarmApp::onStopClicked(lv_event_t *event)
{
    auto *app = static_cast<AlarmApp *>(lv_event_get_user_data(event));
    watch::alarm_stop();
    watch::audio_test_stop();
    if (app != nullptr) app->refresh();
}

void AlarmApp::onTimer(lv_timer_t *timer)
{
    auto *app = static_cast<AlarmApp *>(lv_timer_get_user_data(timer));
    if (app == nullptr) {
        return;
    }
    app->refresh();
    app->playRingPulse();
}

void AlarmApp::adjustTime(int hour_delta, int minute_delta)
{
    watch::AlarmState state = {};
    watch::alarm_get_state(&state);
    int total = state.hour * 60 + state.minute + hour_delta * 60 + minute_delta;
    total %= (24 * 60);
    if (total < 0) {
        total += 24 * 60;
    }
    const int next_hour = total / 60;
    const int next_minute = total % 60;
    if (watch::alarm_set(next_hour, next_minute, state.enabled, state.repeat_daily) != ESP_OK) {
        ESP_UTILS_LOGW("Failed to save alarm time");
    }
    refresh();
}

void AlarmApp::setAlarmFromState(bool enabled, bool repeat_daily)
{
    watch::AlarmState state = {};
    watch::alarm_get_state(&state);
    if (watch::alarm_set(state.hour, state.minute, enabled, repeat_daily) != ESP_OK) {
        ESP_UTILS_LOGW("Failed to save alarm state");
    }
    if (!enabled) {
        watch::alarm_stop();
        watch::audio_test_stop();
    }
    refresh();
}

void AlarmApp::refresh()
{
    watch::AlarmState state = {};
    watch::alarm_get_state(&state);

    if (_time_label != nullptr) {
        char text[16] = {};
        std::snprintf(text, sizeof(text), "%02d:%02d", state.hour, state.minute);
        lv_label_set_text(_time_label, text);
    }
    if (_state_label != nullptr) {
        char text[80] = {};
        std::snprintf(
            text,
            sizeof(text),
            "%s / %s",
            state.enabled ? "Enabled" : "Off",
            state.repeat_daily ? "Daily" : "Once"
        );
        lv_label_set_text(_state_label, text);
    }
    if (_enable_label != nullptr) {
        lv_label_set_text(_enable_label, state.enabled ? "Disable" : "Enable");
    }
    if (_repeat_label != nullptr) {
        lv_label_set_text(_repeat_label, state.repeat_daily ? "Repeat Daily" : "Once");
    }
    if (_stop_button != nullptr) {
        if (state.ringing) {
            lv_obj_clear_flag(_stop_button, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_stop_button, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (_ring_label != nullptr) {
        lv_label_set_text(_ring_label, state.ringing ? "ALARM RINGING" : " ");
    }
}

void AlarmApp::playRingPulse()
{
    watch::AlarmState state = {};
    watch::alarm_get_state(&state);
    if (!state.ringing) {
        return;
    }
    if ((_last_ring_tick == 0) || (lv_tick_elaps(_last_ring_tick) >= 1500)) {
        _last_ring_tick = lv_tick_get();
        esp_err_t err = watch::audio_play_test_tone_async();
        if ((err != ESP_OK) && (err != ESP_ERR_INVALID_STATE)) {
            ESP_UTILS_LOGW("Alarm tone failed: %s", esp_err_to_name(err));
        }
    }
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, AlarmApp, APP_NAME, []()
{
    return std::shared_ptr<AlarmApp>(AlarmApp::requestInstance(), [](AlarmApp *) {});
})

} // namespace esp_brookesia::apps
