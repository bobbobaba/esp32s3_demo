#include "timer_app.hpp"

#include <cstdio>

#include "esp_brookesia.hpp"
#include "esp_lib_utils.h"
#include "lvgl.h"
#include "watch_app_icons.hpp"
#include "watch_audio.hpp"

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "TimerApp"

namespace esp_brookesia::apps {
namespace {

constexpr const char *APP_NAME = "Timer";
constexpr int SAFE_TOP = 24;
constexpr int SAFE_SIDE = 22;

lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
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
    lv_obj_set_height(button, 54);
    lv_obj_set_width(button, LV_PCT(width_pct));
    style_button(button, bg);
    lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, user_data);
    lv_obj_t *label = make_label(button, text, &lv_font_montserrat_18, 0xFFFFFF);
    lv_obj_center(label);
    return label;
}

lv_obj_t *make_row(lv_obj_t *parent, int height)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, height);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 10, 0);
    return row;
}

void format_ms(uint32_t ms, char *buffer, size_t buffer_size)
{
    const uint32_t total_seconds = ms / 1000;
    const uint32_t minutes = total_seconds / 60;
    const uint32_t seconds = total_seconds % 60;
    const uint32_t tenths = (ms / 100) % 10;
    std::snprintf(buffer, buffer_size, "%02lu:%02lu.%lu",
                  static_cast<unsigned long>(minutes),
                  static_cast<unsigned long>(seconds),
                  static_cast<unsigned long>(tenths));
}

} // namespace

TimerApp *TimerApp::_instance = nullptr;

TimerApp *TimerApp::requestInstance()
{
    if (_instance == nullptr) {
        _instance = new TimerApp();
    }
    return _instance;
}

TimerApp::TimerApp():
    systems::phone::App(APP_NAME, watch_app_icon_timer_48(), true, true, true)
{
}

bool TimerApp::run(void)
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

    make_label(title_row, "Timer", &lv_font_montserrat_30, 0xFFFFFF);
    lv_obj_t *back_btn = lv_button_create(title_row);
    lv_obj_set_size(back_btn, 92, 44);
    style_button(back_btn, 0x232833);
    lv_obj_add_event_cb(back_btn, onBackClicked, LV_EVENT_CLICKED, this);
    lv_obj_center(make_label(back_btn, "Back", &lv_font_montserrat_18, 0xFFFFFF));

    lv_obj_t *card = lv_obj_create(root);
    ESP_UTILS_CHECK_NULL_RETURN(card, false, "Create timer card failed");
    lv_obj_remove_style_all(card);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, 190);
    lv_obj_set_style_radius(card, 28, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x15171D), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x2B303A), 0);
    lv_obj_set_style_pad_all(card, 18, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 10, 0);

    _mode_label = make_label(card, "Stopwatch", &lv_font_montserrat_20, 0x69D2FF);
    lv_obj_set_width(_mode_label, LV_PCT(100));
    lv_obj_set_style_text_align(_mode_label, LV_TEXT_ALIGN_CENTER, 0);

    _time_label = make_label(card, "00:00.0", &lv_font_montserrat_48, 0xFFFFFF);
    lv_obj_set_width(_time_label, LV_PCT(100));
    lv_obj_set_style_text_align(_time_label, LV_TEXT_ALIGN_CENTER, 0);

    _hint_label = make_label(card, "Tap Start", &lv_font_montserrat_16, 0xAEB7C6);
    lv_obj_set_width(_hint_label, LV_PCT(100));
    lv_obj_set_style_text_align(_hint_label, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *main_row = make_row(root, 58);
    make_button(main_row, "Mode", 0x232833, onModeClicked, this, 31);
    _start_label = make_button(main_row, "Start", 0x23A559, onStartClicked, this, 31);
    make_button(main_row, "Reset", 0xD9322E, onResetClicked, this, 31);

    lv_obj_t *set_row = make_row(root, 58);
    make_button(set_row, "-1 min", 0x232833, onMinusClicked, this, 48);
    make_button(set_row, "+1 min", 0x1B6BFF, onPlusClicked, this, 48);

    refresh();
    _timer = lv_timer_create(onTimer, 100, this);
    return true;
}

bool TimerApp::back(void)
{
    if (_timer != nullptr) {
        lv_timer_delete(_timer);
        _timer = nullptr;
    }
    clearObjects();
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

bool TimerApp::close(void)
{
    if (_timer != nullptr) {
        lv_timer_delete(_timer);
        _timer = nullptr;
    }
    clearObjects();
    return true;
}

void TimerApp::clearObjects()
{
    _time_label = nullptr;
    _mode_label = nullptr;
    _start_label = nullptr;
    _hint_label = nullptr;
}

void TimerApp::onBackClicked(lv_event_t *event)
{
    auto *app = static_cast<TimerApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->back();
}

void TimerApp::onModeClicked(lv_event_t *event)
{
    auto *app = static_cast<TimerApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->toggleMode();
}

void TimerApp::onStartClicked(lv_event_t *event)
{
    auto *app = static_cast<TimerApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->toggleRunning();
}

void TimerApp::onResetClicked(lv_event_t *event)
{
    auto *app = static_cast<TimerApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->reset();
}

void TimerApp::onMinusClicked(lv_event_t *event)
{
    auto *app = static_cast<TimerApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->adjustCountdown(-60);
}

void TimerApp::onPlusClicked(lv_event_t *event)
{
    auto *app = static_cast<TimerApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->adjustCountdown(60);
}

void TimerApp::onTimer(lv_timer_t *timer)
{
    auto *app = static_cast<TimerApp *>(lv_timer_get_user_data(timer));
    if (app == nullptr) {
        return;
    }
    if (app->_countdown_mode && app->_running && (app->remainingMs() == 0)) {
        app->_running = false;
        app->_base_elapsed_ms = app->_countdown_total_ms;
        app->_expired = true;
    }
    if (app->_expired && ((app->_last_beep_tick == 0) || (lv_tick_elaps(app->_last_beep_tick) >= 1500))) {
        app->_last_beep_tick = lv_tick_get();
        esp_err_t err = watch::audio_play_test_tone_async();
        if ((err != ESP_OK) && (err != ESP_ERR_INVALID_STATE)) {
            ESP_UTILS_LOGW("Timer tone failed: %s", esp_err_to_name(err));
        }
    }
    app->refresh();
}

void TimerApp::toggleMode()
{
    _countdown_mode = !_countdown_mode;
    _running = false;
    _expired = false;
    _base_elapsed_ms = 0;
    _start_tick = 0;
    _last_beep_tick = 0;
    watch::audio_test_stop();
    refresh();
}

void TimerApp::toggleRunning()
{
    if (_expired) {
        reset();
    }
    if (_running) {
        _base_elapsed_ms = elapsedMs();
        _running = false;
    } else {
        _start_tick = lv_tick_get();
        _running = true;
    }
    refresh();
}

void TimerApp::reset()
{
    _running = false;
    _expired = false;
    _base_elapsed_ms = 0;
    _start_tick = 0;
    _last_beep_tick = 0;
    watch::audio_test_stop();
    refresh();
}

void TimerApp::adjustCountdown(int delta_seconds)
{
    if (_running) {
        return;
    }
    int32_t total = static_cast<int32_t>(_countdown_total_ms / 1000) + delta_seconds;
    if (total < 60) total = 60;
    if (total > 99 * 60) total = 99 * 60;
    _countdown_total_ms = static_cast<uint32_t>(total) * 1000;
    if (_countdown_mode) {
        _base_elapsed_ms = 0;
        _expired = false;
    }
    refresh();
}

uint32_t TimerApp::elapsedMs() const
{
    if (!_running) {
        return _base_elapsed_ms;
    }
    return _base_elapsed_ms + lv_tick_elaps(_start_tick);
}

uint32_t TimerApp::remainingMs() const
{
    const uint32_t elapsed = elapsedMs();
    if (elapsed >= _countdown_total_ms) {
        return 0;
    }
    return _countdown_total_ms - elapsed;
}

void TimerApp::refresh()
{
    if (_time_label != nullptr) {
        char text[24] = {};
        format_ms(_countdown_mode ? remainingMs() : elapsedMs(), text, sizeof(text));
        lv_label_set_text(_time_label, text);
    }
    if (_mode_label != nullptr) {
        lv_label_set_text(_mode_label, _countdown_mode ? "Countdown" : "Stopwatch");
    }
    if (_start_label != nullptr) {
        lv_label_set_text(_start_label, _running ? "Pause" : "Start");
    }
    if (_hint_label != nullptr) {
        if (_expired) {
            lv_label_set_text(_hint_label, "Time up. Reset to stop tone.");
        } else if (_countdown_mode) {
            char text[40] = {};
            std::snprintf(text, sizeof(text), "Target %lu min",
                          static_cast<unsigned long>(_countdown_total_ms / 60000));
            lv_label_set_text(_hint_label, text);
        } else {
            lv_label_set_text(_hint_label, _running ? "Running" : "Ready");
        }
    }
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, TimerApp, APP_NAME, []()
{
    return std::shared_ptr<TimerApp>(TimerApp::requestInstance(), [](TimerApp *) {});
})

} // namespace esp_brookesia::apps
