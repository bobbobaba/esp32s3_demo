#include "calendar_app.hpp"

#include <cstdio>
#include <ctime>

#include "esp_brookesia.hpp"
#include "esp_lib_utils.h"
#include "lvgl.h"
#include "watch_app_icons.hpp"
#include "watch_time.hpp"

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "CalendarApp"

namespace esp_brookesia::apps {
namespace {

constexpr const char *APP_NAME = "Calendar";
constexpr int SAFE_TOP = 24;
constexpr int SAFE_SIDE = 22;
constexpr uint16_t FALLBACK_YEAR = 2026;
constexpr uint8_t FALLBACK_MONTH = 8;
constexpr uint8_t FALLBACK_DAY = 11;

const char *MONTH_NAMES[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
};

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

} // namespace

CalendarApp *CalendarApp::_instance = nullptr;

CalendarApp *CalendarApp::requestInstance()
{
    if (_instance == nullptr) {
        _instance = new CalendarApp();
    }
    return _instance;
}

CalendarApp::CalendarApp():
    systems::phone::App(APP_NAME, watch_app_icon_calendar_48(), true, true, true)
{
}

bool CalendarApp::run(void)
{
    lv_obj_t *screen = lv_scr_act();
    ESP_UTILS_CHECK_NULL_RETURN(screen, false, "Invalid active screen");

    loadToday();

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
    lv_obj_set_style_pad_bottom(root, 20, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(root, 8, 0);

    lv_obj_t *title_row = lv_obj_create(root);
    ESP_UTILS_CHECK_NULL_RETURN(title_row, false, "Create title row failed");
    lv_obj_remove_style_all(title_row);
    lv_obj_set_width(title_row, LV_PCT(100));
    lv_obj_set_height(title_row, 50);
    lv_obj_set_flex_flow(title_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    make_label(title_row, "Calendar", &lv_font_montserrat_30, 0xFFFFFF);
    lv_obj_t *back_btn = lv_button_create(title_row);
    lv_obj_set_size(back_btn, 92, 44);
    style_button(back_btn, 0x232833);
    lv_obj_add_event_cb(back_btn, onBackClicked, LV_EVENT_CLICKED, this);
    lv_obj_center(make_label(back_btn, "Back", &lv_font_montserrat_18, 0xFFFFFF));

    _date_label = make_label(root, "", &lv_font_montserrat_20, 0xD7DCE5);
    lv_obj_set_width(_date_label, LV_PCT(100));
    lv_obj_set_style_text_align(_date_label, LV_TEXT_ALIGN_CENTER, 0);

    _calendar = lv_calendar_create(root);
    ESP_UTILS_CHECK_NULL_RETURN(_calendar, false, "Create calendar failed");
    lv_obj_set_size(_calendar, LV_PCT(100), 300);
    lv_obj_set_style_radius(_calendar, 24, 0);
    lv_obj_set_style_bg_color(_calendar, lv_color_hex(0x15171D), 0);
    lv_obj_set_style_bg_opa(_calendar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_calendar, 1, 0);
    lv_obj_set_style_border_color(_calendar, lv_color_hex(0x2B303A), 0);
    lv_obj_set_style_text_color(_calendar, lv_color_hex(0xEAF0FA), 0);
    lv_obj_set_style_pad_all(_calendar, 10, 0);
    lv_obj_add_event_cb(_calendar, onCalendarEvent, LV_EVENT_VALUE_CHANGED, this);

    lv_calendar_set_today_date(_calendar, _today.year, _today.month, _today.day);
    lv_calendar_set_month_shown(_calendar, _today.year, _today.month);
    _highlighted[0] = _today;
    lv_calendar_set_highlighted_dates(_calendar, _highlighted, 1);
#if LV_USE_CALENDAR_HEADER_ARROW
    lv_calendar_add_header_arrow(_calendar);
#endif

    _selected_label = make_label(root, "", &lv_font_montserrat_16, 0xAEB7C6);
    lv_obj_set_width(_selected_label, LV_PCT(100));
    lv_obj_set_style_text_align(_selected_label, LV_TEXT_ALIGN_CENTER, 0);

    _sync_label = make_label(root, "", &lv_font_montserrat_12, 0x717B8C);
    lv_obj_set_width(_sync_label, LV_PCT(100));
    lv_obj_set_style_text_align(_sync_label, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *today_btn = lv_button_create(root);
    lv_obj_set_width(today_btn, LV_PCT(100));
    lv_obj_set_height(today_btn, 48);
    style_button(today_btn, 0x1B6BFF);
    lv_obj_add_event_cb(today_btn, onTodayClicked, LV_EVENT_CLICKED, this);
    lv_obj_center(make_label(today_btn, "Today", &lv_font_montserrat_18, 0xFFFFFF));

    updateLabels();
    _timer = lv_timer_create(onTimer, 60000, this);
    return true;
}

bool CalendarApp::back(void)
{
    if (_timer != nullptr) {
        lv_timer_delete(_timer);
        _timer = nullptr;
    }
    clearObjects();
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

bool CalendarApp::close(void)
{
    if (_timer != nullptr) {
        lv_timer_delete(_timer);
        _timer = nullptr;
    }
    clearObjects();
    return true;
}

void CalendarApp::clearObjects()
{
    _calendar = nullptr;
    _date_label = nullptr;
    _selected_label = nullptr;
    _sync_label = nullptr;
}

void CalendarApp::onBackClicked(lv_event_t *event)
{
    auto *app = static_cast<CalendarApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->back();
}

void CalendarApp::onTodayClicked(lv_event_t *event)
{
    auto *app = static_cast<CalendarApp *>(lv_event_get_user_data(event));
    if ((app == nullptr) || (app->_calendar == nullptr)) {
        return;
    }
    app->loadToday();
    lv_calendar_set_today_date(app->_calendar, app->_today.year, app->_today.month, app->_today.day);
    lv_calendar_set_month_shown(app->_calendar, app->_today.year, app->_today.month);
    app->_selected = app->_today;
    app->_highlighted[0] = app->_today;
    lv_calendar_set_highlighted_dates(app->_calendar, app->_highlighted, 1);
    app->updateLabels();
}

void CalendarApp::onCalendarEvent(lv_event_t *event)
{
    auto *app = static_cast<CalendarApp *>(lv_event_get_user_data(event));
    if ((app == nullptr) || (app->_calendar == nullptr)) {
        return;
    }
    lv_calendar_date_t date = {};
    if (lv_calendar_get_pressed_date(app->_calendar, &date) == LV_RESULT_OK) {
        app->_selected = date;
        app->updateLabels();
    }
}

void CalendarApp::onTimer(lv_timer_t *timer)
{
    auto *app = static_cast<CalendarApp *>(lv_timer_get_user_data(timer));
    if ((app == nullptr) || (app->_calendar == nullptr)) {
        return;
    }
    lv_calendar_date_t old_today = app->_today;
    app->loadToday();
    if ((old_today.year != app->_today.year) ||
        (old_today.month != app->_today.month) ||
        (old_today.day != app->_today.day)) {
        lv_calendar_set_today_date(app->_calendar, app->_today.year, app->_today.month, app->_today.day);
        app->_highlighted[0] = app->_today;
        lv_calendar_set_highlighted_dates(app->_calendar, app->_highlighted, 1);
    }
    app->updateLabels();
}

void CalendarApp::loadToday()
{
    time_t now = 0;
    time(&now);
    struct tm tm = {};
    localtime_r(&now, &tm);
    _time_valid = watch::time_is_valid();
    if (_time_valid) {
        _today.year = static_cast<uint16_t>(tm.tm_year + 1900);
        _today.month = static_cast<uint8_t>(tm.tm_mon + 1);
        _today.day = static_cast<uint8_t>(tm.tm_mday);
    } else {
        _today.year = FALLBACK_YEAR;
        _today.month = FALLBACK_MONTH;
        _today.day = FALLBACK_DAY;
    }
    if ((_selected.year == 0) || (_selected.month == 0) || (_selected.day == 0)) {
        _selected = _today;
    }
}

void CalendarApp::updateLabels()
{
    if (_date_label != nullptr) {
        char text[48] = {};
        const unsigned month_index = (_today.month >= 1 && _today.month <= 12) ? (_today.month - 1) : 0;
        std::snprintf(text, sizeof(text), "%s %u, %u", MONTH_NAMES[month_index], _today.day, _today.year);
        lv_label_set_text(_date_label, text);
    }
    if (_selected_label != nullptr) {
        char text[48] = {};
        const unsigned month_index = (_selected.month >= 1 && _selected.month <= 12) ? (_selected.month - 1) : 0;
        std::snprintf(text, sizeof(text), "Selected  %s %u, %u", MONTH_NAMES[month_index], _selected.day, _selected.year);
        lv_label_set_text(_selected_label, text);
    }
    if (_sync_label != nullptr) {
        lv_label_set_text(_sync_label, _time_valid ? "System time synced" : "Time not synced, using fallback date");
    }
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, CalendarApp, APP_NAME, []()
{
    return std::shared_ptr<CalendarApp>(CalendarApp::requestInstance(), [](CalendarApp *) {});
})

} // namespace esp_brookesia::apps
