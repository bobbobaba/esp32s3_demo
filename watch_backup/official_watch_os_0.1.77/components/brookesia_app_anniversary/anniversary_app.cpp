#include "anniversary_app.hpp"

#include <cstdio>
#include <cstring>

#include "esp_brookesia.hpp"
#include "esp_lib_utils.h"
#include "lvgl.h"
#include "watch_app_icons.hpp"

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "AnnivApp"

namespace esp_brookesia::apps {
namespace {

constexpr const char *APP_NAME = "Anniv";
constexpr int SAFE_TOP = 24;
constexpr int SAFE_SIDE = 22;

const char *NAMES[] = {
    "Together",
    "Birthday",
    "Wedding",
    "Custom",
    "Family",
    "Meet Day",
};

bool is_leap(int year)
{
    return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
}

int days_in_month(int year, int month)
{
    static constexpr int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if ((month < 1) || (month > 12)) {
        return 31;
    }
    if ((month == 2) && is_leap(year)) {
        return 29;
    }
    return days[month - 1];
}

void clamp_date(watch::AnniversaryItem *item)
{
    if (item == nullptr) {
        return;
    }
    if (item->year < 1970) {
        item->year = 1970;
    } else if (item->year > 2099) {
        item->year = 2099;
    }
    if (item->month < 1) {
        item->month = 1;
    } else if (item->month > 12) {
        item->month = 12;
    }
    const int dim = days_in_month(item->year, item->month);
    if (item->day < 1) {
        item->day = 1;
    } else if (item->day > dim) {
        item->day = dim;
    }
}

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

lv_obj_t *make_button(lv_obj_t *parent, const char *text, uint32_t bg, lv_event_cb_t cb, void *user_data, int width_pct)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_width(button, LV_PCT(width_pct));
    lv_obj_set_height(button, 46);
    style_button(button, bg);
    lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, user_data);
    lv_obj_t *label = make_label(button, text, &lv_font_montserrat_16, 0xFFFFFF);
    lv_obj_center(label);
    return label;
}

lv_obj_t *make_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 48);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 10, 0);
    return row;
}

} // namespace

AnniversaryApp *AnniversaryApp::_instance = nullptr;

AnniversaryApp *AnniversaryApp::requestInstance()
{
    if (_instance == nullptr) {
        _instance = new AnniversaryApp();
    }
    return _instance;
}

AnniversaryApp::AnniversaryApp():
    systems::phone::App(APP_NAME, watch_app_icon_anniversary_48(), true, true, true)
{
}

bool AnniversaryApp::run(void)
{
    lv_obj_t *screen = lv_scr_act();
    ESP_UTILS_CHECK_NULL_RETURN(screen, false, "Invalid active screen");

    loadCurrent();

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
    lv_obj_set_scrollbar_mode(root, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t *title_row = lv_obj_create(root);
    ESP_UTILS_CHECK_NULL_RETURN(title_row, false, "Create title row failed");
    lv_obj_remove_style_all(title_row);
    lv_obj_set_width(title_row, LV_PCT(100));
    lv_obj_set_height(title_row, 52);
    lv_obj_set_flex_flow(title_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    make_label(title_row, "Anniv", &lv_font_montserrat_30, 0xFFFFFF);
    lv_obj_t *back_btn = lv_button_create(title_row);
    lv_obj_set_size(back_btn, 92, 44);
    style_button(back_btn, 0x232833);
    lv_obj_add_event_cb(back_btn, onBackClicked, LV_EVENT_CLICKED, this);
    lv_obj_center(make_label(back_btn, "Back", &lv_font_montserrat_18, 0xFFFFFF));

    lv_obj_t *summary_card = make_card(root, 170);
    _slot_label = make_label(summary_card, "Slot", &lv_font_montserrat_16, 0xAEB7C6);
    _name_label = make_label(summary_card, "Name", &lv_font_montserrat_24, 0xFFFFFF);
    _delta_label = make_label(summary_card, "--", &lv_font_montserrat_36, 0xFF5C8A);
    lv_obj_set_width(_delta_label, LV_PCT(100));
    lv_obj_set_style_text_align(_delta_label, LV_TEXT_ALIGN_CENTER, 0);
    _date_label = make_label(summary_card, "Date", &lv_font_montserrat_16, 0xAEB7C6);
    _mode_label = make_label(summary_card, "Mode", &lv_font_montserrat_14, 0x717B8C);

    lv_obj_t *slot_row = make_row(root);
    make_button(slot_row, "Prev", 0x232833, onPrevClicked, this, 48);
    make_button(slot_row, "Next", 0x1B6BFF, onNextClicked, this, 48);

    lv_obj_t *date_card = make_card(root, 164);
    lv_obj_t *year_row = make_row(date_card);
    make_button(year_row, "Year -", 0x232833, onYearMinusClicked, this, 48);
    make_button(year_row, "Year +", 0x232833, onYearPlusClicked, this, 48);
    lv_obj_t *month_row = make_row(date_card);
    make_button(month_row, "Month -", 0x232833, onMonthMinusClicked, this, 48);
    make_button(month_row, "Month +", 0x232833, onMonthPlusClicked, this, 48);
    lv_obj_t *day_row = make_row(date_card);
    make_button(day_row, "Day -", 0x232833, onDayMinusClicked, this, 48);
    make_button(day_row, "Day +", 0x232833, onDayPlusClicked, this, 48);

    lv_obj_t *action_card = make_card(root, 230);
    _enable_label = make_button(action_card, "Enable", 0x23A559, onEnableClicked, this, 100);
    _repeat_label = make_button(action_card, "Yearly", 0x232833, onRepeatClicked, this, 100);
    _name_button_label = make_button(action_card, "Name", 0x232833, onNameClicked, this, 100);
    _home_button_label = make_button(action_card, "Show Home", 0x8E5CFF, onHomeClicked, this, 100);

    _sync_label = make_label(root, "", &lv_font_montserrat_12, 0x717B8C);
    lv_obj_set_width(_sync_label, LV_PCT(100));
    lv_label_set_long_mode(_sync_label, LV_LABEL_LONG_WRAP);

    refresh();
    _timer = lv_timer_create(onTimer, 60000, this);
    return true;
}

bool AnniversaryApp::back(void)
{
    if (_timer != nullptr) {
        lv_timer_delete(_timer);
        _timer = nullptr;
    }
    clearObjects();
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

bool AnniversaryApp::close(void)
{
    if (_timer != nullptr) {
        lv_timer_delete(_timer);
        _timer = nullptr;
    }
    clearObjects();
    return true;
}

void AnniversaryApp::clearObjects()
{
    _slot_label = nullptr;
    _name_label = nullptr;
    _date_label = nullptr;
    _delta_label = nullptr;
    _mode_label = nullptr;
    _sync_label = nullptr;
    _enable_label = nullptr;
    _repeat_label = nullptr;
    _name_button_label = nullptr;
    _home_button_label = nullptr;
}

void AnniversaryApp::onBackClicked(lv_event_t *event)
{
    auto *app = static_cast<AnniversaryApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->back();
}

void AnniversaryApp::onPrevClicked(lv_event_t *event)
{
    auto *app = static_cast<AnniversaryApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->selectSlot(-1);
}

void AnniversaryApp::onNextClicked(lv_event_t *event)
{
    auto *app = static_cast<AnniversaryApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->selectSlot(1);
}

void AnniversaryApp::onYearMinusClicked(lv_event_t *event)
{
    auto *app = static_cast<AnniversaryApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->adjustDate(-1, 0, 0);
}

void AnniversaryApp::onYearPlusClicked(lv_event_t *event)
{
    auto *app = static_cast<AnniversaryApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->adjustDate(1, 0, 0);
}

void AnniversaryApp::onMonthMinusClicked(lv_event_t *event)
{
    auto *app = static_cast<AnniversaryApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->adjustDate(0, -1, 0);
}

void AnniversaryApp::onMonthPlusClicked(lv_event_t *event)
{
    auto *app = static_cast<AnniversaryApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->adjustDate(0, 1, 0);
}

void AnniversaryApp::onDayMinusClicked(lv_event_t *event)
{
    auto *app = static_cast<AnniversaryApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->adjustDate(0, 0, -1);
}

void AnniversaryApp::onDayPlusClicked(lv_event_t *event)
{
    auto *app = static_cast<AnniversaryApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->adjustDate(0, 0, 1);
}

void AnniversaryApp::onEnableClicked(lv_event_t *event)
{
    auto *app = static_cast<AnniversaryApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->toggleEnabled();
}

void AnniversaryApp::onRepeatClicked(lv_event_t *event)
{
    auto *app = static_cast<AnniversaryApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->toggleRepeat();
}

void AnniversaryApp::onNameClicked(lv_event_t *event)
{
    auto *app = static_cast<AnniversaryApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->cycleName();
}

void AnniversaryApp::onHomeClicked(lv_event_t *event)
{
    auto *app = static_cast<AnniversaryApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->setHomeSlot();
}

void AnniversaryApp::onTimer(lv_timer_t *timer)
{
    auto *app = static_cast<AnniversaryApp *>(lv_timer_get_user_data(timer));
    if (app != nullptr) app->refresh();
}

void AnniversaryApp::selectSlot(int delta)
{
    _slot = (_slot + delta) % watch::ANNIVERSARY_MAX_ITEMS;
    if (_slot < 0) {
        _slot += watch::ANNIVERSARY_MAX_ITEMS;
    }
    loadCurrent();
    refresh();
}

void AnniversaryApp::adjustDate(int year_delta, int month_delta, int day_delta)
{
    _item.year += year_delta;
    _item.month += month_delta;
    while (_item.month < 1) {
        _item.month += 12;
        _item.year -= 1;
    }
    while (_item.month > 12) {
        _item.month -= 12;
        _item.year += 1;
    }
    _item.day += day_delta;
    while (_item.day < 1) {
        _item.month -= 1;
        if (_item.month < 1) {
            _item.month = 12;
            _item.year -= 1;
        }
        _item.day += days_in_month(_item.year, _item.month);
    }
    while (_item.day > days_in_month(_item.year, _item.month)) {
        _item.day -= days_in_month(_item.year, _item.month);
        _item.month += 1;
        if (_item.month > 12) {
            _item.month = 1;
            _item.year += 1;
        }
    }
    clamp_date(&_item);
    saveCurrent();
    refresh();
}

void AnniversaryApp::toggleEnabled()
{
    _item.enabled = !_item.enabled;
    saveCurrent();
    refresh();
}

void AnniversaryApp::toggleRepeat()
{
    _item.repeat_yearly = !_item.repeat_yearly;
    saveCurrent();
    refresh();
}

void AnniversaryApp::cycleName()
{
    int index = 0;
    for (int i = 0; i < static_cast<int>(sizeof(NAMES) / sizeof(NAMES[0])); ++i) {
        if (std::strcmp(_item.name, NAMES[i]) == 0) {
            index = i + 1;
            break;
        }
    }
    index %= static_cast<int>(sizeof(NAMES) / sizeof(NAMES[0]));
    std::snprintf(_item.name, sizeof(_item.name), "%s", NAMES[index]);
    saveCurrent();
    refresh();
}

void AnniversaryApp::setHomeSlot()
{
    if (watch::anniversary_set_home_index(_slot) != ESP_OK) {
        ESP_UTILS_LOGW("Failed to set home anniversary slot %d", _slot);
    }
    refresh();
}

void AnniversaryApp::loadCurrent()
{
    if (watch::anniversary_get(_slot, &_item) != ESP_OK) {
        _item = watch::AnniversaryItem{};
        std::snprintf(_item.name, sizeof(_item.name), "Anniv");
    }
}

void AnniversaryApp::saveCurrent()
{
    if (watch::anniversary_set(_slot, &_item) != ESP_OK) {
        ESP_UTILS_LOGW("Failed to save anniversary slot %d", _slot);
    }
}

void AnniversaryApp::refresh()
{
    char text[96] = {};
    if (_slot_label != nullptr) {
        std::snprintf(text, sizeof(text), "Slot %d / %d", _slot + 1, watch::ANNIVERSARY_MAX_ITEMS);
        lv_label_set_text(_slot_label, text);
    }
    if (_name_label != nullptr) {
        lv_label_set_text(_name_label, _item.name);
    }
    if (_date_label != nullptr) {
        std::snprintf(text, sizeof(text), "%04d-%02d-%02d", _item.year, _item.month, _item.day);
        lv_label_set_text(_date_label, text);
    }
    if (_delta_label != nullptr) {
        char delta[48] = {};
        watch::anniversary_delta_text(&_item, delta, sizeof(delta));
        lv_label_set_text(_delta_label, delta);
    }
    if (_mode_label != nullptr) {
        std::snprintf(text, sizeof(text), "%s / %s", _item.enabled ? "Enabled" : "Off", _item.repeat_yearly ? "Yearly" : "Once");
        lv_label_set_text(_mode_label, text);
    }
    if (_enable_label != nullptr) {
        lv_label_set_text(_enable_label, _item.enabled ? "Disable" : "Enable");
    }
    if (_repeat_label != nullptr) {
        lv_label_set_text(_repeat_label, _item.repeat_yearly ? "Repeat: Yearly" : "Repeat: Once");
    }
    if (_name_button_label != nullptr) {
        lv_label_set_text(_name_button_label, "Change Name");
    }
    if (_home_button_label != nullptr) {
        int home_index = 0;
        watch::anniversary_get_home_index(&home_index);
        lv_label_set_text(_home_button_label, home_index == _slot ? "Shown on Home" : "Show on Home");
    }
    if (_sync_label != nullptr) {
        lv_label_set_text(
            _sync_label,
            watch::anniversary_today_valid() ?
            "D- means days remaining. D+ means days passed." :
            "Time not synced. Connect WiFi or sync RTC first."
        );
    }
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, AnniversaryApp, APP_NAME, []()
{
    return std::shared_ptr<AnniversaryApp>(AnniversaryApp::requestInstance(), [](AnniversaryApp *) {});
})

} // namespace esp_brookesia::apps
