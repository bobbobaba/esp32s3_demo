#include "level_app.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "esp_brookesia.hpp"
#include "esp_lib_utils.h"
#include "lvgl.h"
#include "watch_app_icons.hpp"
#include "watch_display.hpp"
#include "watch_fonts.hpp"
#include "watch_sensor.hpp"

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "LevelApp"

namespace esp_brookesia::apps {
namespace {

constexpr const char *APP_NAME = "Level";
constexpr int SAFE_TOP = 24;
constexpr int SAFE_SIDE = 22;
constexpr int FIELD_SIZE = 250;
constexpr int BUBBLE_SIZE = 42;
constexpr int BUBBLE_TRAVEL = 88;
constexpr float DISPLAY_LIMIT_DEG = 20.0f;
constexpr float LEVEL_THRESHOLD_DEG = 1.0f;
constexpr float NEAR_THRESHOLD_DEG = 3.0f;
constexpr float LEVEL_FILTER_ALPHA = 0.14f;
constexpr float MIN_GRAVITY_G = 0.72f;
constexpr float MAX_GRAVITY_G = 1.28f;

float radians_to_degrees(float radians)
{
    return radians * 57.2957795f;
}

// Keep both axes in [-90, 90]. atan2(Y, Z) used by the motion controller
// spans 360 degrees and jumps near +/-180 when the watch face is upward.
float level_axis_angle(float numerator, float axis_a, float axis_b)
{
    return radians_to_degrees(std::atan2(numerator, std::sqrt(axis_a * axis_a + axis_b * axis_b)));
}

lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    watch_display::apply_text_font(label, text, font);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_letter_space(label, 0, 0);
    return label;
}

lv_obj_t *make_rule(lv_obj_t *parent, int width, int height, uint32_t color)
{
    lv_obj_t *rule = lv_obj_create(parent);
    lv_obj_remove_style_all(rule);
    lv_obj_set_size(rule, width, height);
    lv_obj_set_style_bg_color(rule, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
    lv_obj_clear_flag(rule, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(rule);
    return rule;
}

lv_obj_t *make_metric(lv_obj_t *parent, const char *name, lv_obj_t **value)
{
    lv_obj_t *metric = lv_obj_create(parent);
    lv_obj_remove_style_all(metric);
    lv_obj_set_width(metric, LV_PCT(48));
    lv_obj_set_height(metric, 44);
    lv_obj_set_flex_flow(metric, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(metric, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(metric, 1, 0);

    make_label(metric, name, &lv_font_montserrat_12, 0x7F8A9B);
    *value = make_label(metric, "+0.0 deg", &lv_font_montserrat_20, 0xFFFFFF);
    return metric;
}

lv_obj_t *make_button(
    lv_obj_t *parent,
    const char *text,
    uint32_t color,
    uint32_t pressed_color,
    lv_event_cb_t callback,
    void *user_data
)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_width(button, LV_PCT(31));
    lv_obj_set_height(button, 50);
    lv_obj_set_style_radius(button, 16, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(pressed_color), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(0x3A4554), 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
    lv_obj_center(make_label(button, text, &lv_font_montserrat_16, 0xFFFFFF));
    return button;
}

} // namespace

LevelApp *LevelApp::_instance = nullptr;

LevelApp *LevelApp::requestInstance()
{
    if (_instance == nullptr) {
        _instance = new LevelApp();
    }
    return _instance;
}

LevelApp::LevelApp():
    systems::phone::App(APP_NAME, watch_app_icon_level_48(), true, true, true)
{
}

bool LevelApp::run(void)
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
    lv_obj_set_style_pad_bottom(root, 18, 0);
    lv_obj_set_style_pad_row(root, 8, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title_row = lv_obj_create(root);
    ESP_UTILS_CHECK_NULL_RETURN(title_row, false, "Create title row failed");
    lv_obj_remove_style_all(title_row);
    lv_obj_set_width(title_row, LV_PCT(100));
    lv_obj_set_height(title_row, 44);
    lv_obj_set_flex_flow(title_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    make_label(title_row, "Level", &lv_font_montserrat_28, 0xFFFFFF);

    _status_chip = lv_obj_create(title_row);
    ESP_UTILS_CHECK_NULL_RETURN(_status_chip, false, "Create status chip failed");
    lv_obj_remove_style_all(_status_chip);
    lv_obj_set_size(_status_chip, 86, 34);
    lv_obj_set_style_radius(_status_chip, 17, 0);
    lv_obj_set_style_bg_color(_status_chip, lv_color_hex(0x1F7A62), 0);
    lv_obj_set_style_bg_opa(_status_chip, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_status_chip, 1, 0);
    lv_obj_set_style_border_color(_status_chip, lv_color_hex(0x4AB895), 0);
    lv_obj_clear_flag(_status_chip, LV_OBJ_FLAG_SCROLLABLE);
    _status_label = make_label(_status_chip, "LEVEL", &lv_font_montserrat_14, 0xFFFFFF);
    lv_obj_center(_status_label);

    lv_obj_t *metric_row = lv_obj_create(root);
    ESP_UTILS_CHECK_NULL_RETURN(metric_row, false, "Create metric row failed");
    lv_obj_remove_style_all(metric_row);
    lv_obj_set_width(metric_row, LV_PCT(100));
    lv_obj_set_height(metric_row, 44);
    lv_obj_set_flex_flow(metric_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(metric_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    make_metric(metric_row, "PITCH", &_pitch_value);
    make_metric(metric_row, "ROLL", &_roll_value);

    lv_obj_t *field = lv_obj_create(root);
    ESP_UTILS_CHECK_NULL_RETURN(field, false, "Create level field failed");
    lv_obj_remove_style_all(field);
    lv_obj_set_size(field, FIELD_SIZE, FIELD_SIZE);
    lv_obj_set_style_radius(field, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(field, lv_color_hex(0x0E1117), 0);
    lv_obj_set_style_bg_opa(field, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(field, 2, 0);
    lv_obj_set_style_border_color(field, lv_color_hex(0x2B303A), 0);
    lv_obj_set_style_clip_corner(field, true, 0);
    lv_obj_clear_flag(field, LV_OBJ_FLAG_SCROLLABLE);

    make_rule(field, 176, 2, 0x252C36);
    make_rule(field, 2, 176, 0x252C36);

    lv_obj_t *outer_target = lv_obj_create(field);
    lv_obj_remove_style_all(outer_target);
    lv_obj_set_size(outer_target, 116, 116);
    lv_obj_set_style_radius(outer_target, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(outer_target, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(outer_target, 1, 0);
    lv_obj_set_style_border_color(outer_target, lv_color_hex(0x303947), 0);
    lv_obj_clear_flag(outer_target, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(outer_target);

    lv_obj_t *inner_target = lv_obj_create(field);
    lv_obj_remove_style_all(inner_target);
    lv_obj_set_size(inner_target, 58, 58);
    lv_obj_set_style_radius(inner_target, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(inner_target, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(inner_target, 2, 0);
    lv_obj_set_style_border_color(inner_target, lv_color_hex(0x3D4A59), 0);
    lv_obj_clear_flag(inner_target, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(inner_target);

    lv_obj_t *center_dot = lv_obj_create(field);
    lv_obj_remove_style_all(center_dot);
    lv_obj_set_size(center_dot, 6, 6);
    lv_obj_set_style_radius(center_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(center_dot, lv_color_hex(0xAEB7C6), 0);
    lv_obj_set_style_bg_opa(center_dot, LV_OPA_COVER, 0);
    lv_obj_clear_flag(center_dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(center_dot);

    _bubble = lv_obj_create(field);
    ESP_UTILS_CHECK_NULL_RETURN(_bubble, false, "Create bubble failed");
    lv_obj_remove_style_all(_bubble);
    lv_obj_set_size(_bubble, BUBBLE_SIZE, BUBBLE_SIZE);
    lv_obj_set_style_radius(_bubble, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(_bubble, lv_color_hex(0x31C16B), 0);
    lv_obj_set_style_bg_opa(_bubble, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_bubble, 3, 0);
    lv_obj_set_style_border_color(_bubble, lv_color_hex(0xA6F0C6), 0);
    lv_obj_clear_flag(_bubble, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(_bubble);

    _reference_label = make_label(root, "Factory reference", &lv_font_montserrat_14, 0x7F8A9B);
    lv_obj_set_width(_reference_label, LV_PCT(100));
    lv_obj_set_style_text_align(_reference_label, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *button_row = lv_obj_create(root);
    ESP_UTILS_CHECK_NULL_RETURN(button_row, false, "Create button row failed");
    lv_obj_remove_style_all(button_row);
    lv_obj_set_width(button_row, LV_PCT(100));
    lv_obj_set_height(button_row, 50);
    lv_obj_set_flex_flow(button_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    make_button(button_row, "Zero", 0x1F7A62, 0x175847, onZeroClicked, this);
    make_button(button_row, "Reset", 0x3C4657, 0x2B303A, onResetClicked, this);
    make_button(button_row, "Back", 0x232833, 0x171B22, onBackClicked, this);

    _screen = screen;
    _zeroed = false;
    _last_sample_valid = false;
    _level_filter_valid = false;
    _last_activity_tick = lv_tick_get();
    watch::display_notify_activity();
    refreshSensor();
    _timer = lv_timer_create(onTimer, 80, this);
    if (_timer == nullptr) {
        stopUi();
        return false;
    }
    return true;
}

bool LevelApp::back(void)
{
    stopUi();
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

bool LevelApp::close(void)
{
    stopUi();
    return true;
}

void LevelApp::onBackClicked(lv_event_t *event)
{
    auto *app = static_cast<LevelApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->back();
}

void LevelApp::onZeroClicked(lv_event_t *event)
{
    auto *app = static_cast<LevelApp *>(lv_event_get_user_data(event));
    if ((app == nullptr) || !app->_last_sample_valid) {
        return;
    }
    app->_pitch_zero_deg = app->_last_pitch_deg;
    app->_roll_zero_deg = app->_last_roll_deg;
    app->_zeroed = true;
    app->updateDisplay(0.0f, 0.0f);
}

void LevelApp::onResetClicked(lv_event_t *event)
{
    auto *app = static_cast<LevelApp *>(lv_event_get_user_data(event));
    if (app == nullptr) {
        return;
    }
    app->_pitch_zero_deg = 0.0f;
    app->_roll_zero_deg = 0.0f;
    app->_zeroed = false;
    if (app->_last_sample_valid) {
        app->updateDisplay(app->_last_pitch_deg, app->_last_roll_deg);
    }
}

void LevelApp::onTimer(lv_timer_t *timer)
{
    auto *app = static_cast<LevelApp *>(lv_timer_get_user_data(timer));
    if ((app == nullptr) || (app->_screen == nullptr) || (lv_scr_act() != app->_screen)) {
        return;
    }
    if (lv_tick_elaps(app->_last_activity_tick) >= 1000) {
        app->_last_activity_tick = lv_tick_get();
        watch::display_notify_activity();
    }
    app->refreshSensor();
}

void LevelApp::refreshSensor()
{
    watch::SensorState state = {};
    if (watch::sensor_read(&state) != ESP_OK) {
        _last_sample_valid = false;
        showSensorError();
        return;
    }

    const float gravity_g = state.acc_mag_g;
    if (!std::isfinite(gravity_g) || gravity_g < MIN_GRAVITY_G || gravity_g > MAX_GRAVITY_G) {
        // Ignore hand motion and shocks: acceleration is not a gravity reference then.
        if (!_level_filter_valid) {
            _last_sample_valid = false;
            showSensorError();
            return;
        }
    } else {
        const float pitch_deg = level_axis_angle(-state.acc_g[0], state.acc_g[1], state.acc_g[2]);
        const float roll_deg = level_axis_angle(state.acc_g[1], state.acc_g[0], state.acc_g[2]);
        if (!std::isfinite(pitch_deg) || !std::isfinite(roll_deg)) {
            _last_sample_valid = false;
            showSensorError();
            return;
        }
        if (!_level_filter_valid) {
            _pitch_level_filtered_deg = pitch_deg;
            _roll_level_filtered_deg = roll_deg;
            _level_filter_valid = true;
        } else {
            _pitch_level_filtered_deg += (pitch_deg - _pitch_level_filtered_deg) * LEVEL_FILTER_ALPHA;
            _roll_level_filtered_deg += (roll_deg - _roll_level_filtered_deg) * LEVEL_FILTER_ALPHA;
        }
    }

    _last_pitch_deg = _pitch_level_filtered_deg;
    _last_roll_deg = _roll_level_filtered_deg;
    _last_sample_valid = true;
    updateDisplay(
        _last_pitch_deg - (_zeroed ? _pitch_zero_deg : 0.0f),
        _last_roll_deg - (_zeroed ? _roll_zero_deg : 0.0f)
    );
}

void LevelApp::updateDisplay(float pitch_deg, float roll_deg)
{
    if ((_bubble == nullptr) || (_pitch_value == nullptr) || (_roll_value == nullptr) ||
            (_status_chip == nullptr) || (_reference_label == nullptr)) {
        return;
    }

    char value[24] = {};
    std::snprintf(value, sizeof(value), "%+.1f deg", pitch_deg);
    lv_label_set_text(_pitch_value, value);
    std::snprintf(value, sizeof(value), "%+.1f deg", roll_deg);
    lv_label_set_text(_roll_value, value);

    float x = std::clamp(roll_deg / DISPLAY_LIMIT_DEG, -1.0f, 1.0f) * BUBBLE_TRAVEL;
    float y = std::clamp(pitch_deg / DISPLAY_LIMIT_DEG, -1.0f, 1.0f) * BUBBLE_TRAVEL;
    const float radius = std::sqrt(x * x + y * y);
    if (radius > BUBBLE_TRAVEL) {
        const float scale = static_cast<float>(BUBBLE_TRAVEL) / radius;
        x *= scale;
        y *= scale;
    }
    lv_obj_align(_bubble, LV_ALIGN_CENTER, static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y)));

    const float tilt = std::sqrt(pitch_deg * pitch_deg + roll_deg * roll_deg);
    const char *status = "TILT";
    uint32_t chip_color = 0x8A3D43;
    uint32_t chip_border = 0xD36B72;
    uint32_t bubble_color = 0xE35D6A;
    uint32_t bubble_border = 0xFFB0B6;
    if (tilt <= LEVEL_THRESHOLD_DEG) {
        status = "LEVEL";
        chip_color = 0x1F7A62;
        chip_border = 0x4AB895;
        bubble_color = 0x31C16B;
        bubble_border = 0xA6F0C6;
    } else if (tilt <= NEAR_THRESHOLD_DEG) {
        status = "NEAR";
        chip_color = 0x7A5A1D;
        chip_border = 0xC99330;
        bubble_color = 0xF0B64A;
        bubble_border = 0xFFE0A3;
    }

    if (_status_label != nullptr) {
        lv_label_set_text(_status_label, status);
    }
    lv_obj_set_style_bg_color(_status_chip, lv_color_hex(chip_color), 0);
    lv_obj_set_style_border_color(_status_chip, lv_color_hex(chip_border), 0);
    lv_obj_set_style_bg_color(_bubble, lv_color_hex(bubble_color), 0);
    lv_obj_set_style_border_color(_bubble, lv_color_hex(bubble_border), 0);
    lv_label_set_text(_reference_label, _zeroed ? "Zero reference" : "Factory reference");
}

void LevelApp::showSensorError()
{
    if ((_bubble == nullptr) || (_pitch_value == nullptr) || (_roll_value == nullptr) ||
            (_status_chip == nullptr) || (_reference_label == nullptr)) {
        return;
    }
    lv_label_set_text(_pitch_value, "--");
    lv_label_set_text(_roll_value, "--");
    lv_obj_align(_bubble, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(_bubble, lv_color_hex(0x596273), 0);
    lv_obj_set_style_border_color(_bubble, lv_color_hex(0x8D98AA), 0);
    lv_obj_set_style_bg_color(_status_chip, lv_color_hex(0x596273), 0);
    lv_obj_set_style_border_color(_status_chip, lv_color_hex(0x8D98AA), 0);
    if (_status_label != nullptr) {
        lv_label_set_text(_status_label, "NO IMU");
    }
    lv_label_set_text(_reference_label, "Sensor unavailable");
}

void LevelApp::stopUi()
{
    if (_timer != nullptr) {
        lv_timer_delete(_timer);
        _timer = nullptr;
    }
    _screen = nullptr;
    _bubble = nullptr;
    _pitch_value = nullptr;
    _roll_value = nullptr;
    _status_chip = nullptr;
    _status_label = nullptr;
    _reference_label = nullptr;
    _last_sample_valid = false;
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, LevelApp, APP_NAME, []()
{
    return std::shared_ptr<LevelApp>(LevelApp::requestInstance(), [](LevelApp *) {});
})

} // namespace esp_brookesia::apps
