#include "watch_home_app.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "esp_brookesia.hpp"
#include "esp_app_desc.h"
#include "esp_lib_utils.h"
#include "esp_timer.h"
#include "ai_chat_app.hpp"
#include "lvgl.h"
#include "watch_connectivity.hpp"
#include "watch_app_icons.hpp"
#include "watch_anniversary.hpp"
#include "watch_display.hpp"
#include "watch_power.hpp"
#include "watch_quota.hpp"
#include "watch_weather.hpp"

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "WatchHome"

namespace esp_brookesia::apps {

namespace {
constexpr const char *APP_NAME = "WatchHome";
constexpr int SAFE_TOP = 30;
constexpr int SAFE_SIDE = 24;
constexpr int SAFE_BOTTOM = 24;
constexpr uint32_t HOME_CLOCK_REFRESH_MS = 10000;
constexpr uint32_t HOME_STATUS_REFRESH_MS = 15000;
constexpr uint32_t HOME_WIFI_RECONNECT_MS = 2 * 60 * 1000;
constexpr int64_t HOME_WEATHER_STALE_US = 20LL * 60LL * 1000000LL;
constexpr uint32_t HOME_QUOTA_REFRESH_MS = 15 * 60 * 1000;
volatile bool s_watch_home_active = false;
int s_ai_chat_app_id = -1;

bool set_label_text_if_changed(lv_obj_t *label, const char *text)
{
    if ((label == nullptr) || (text == nullptr)) {
        return false;
    }
    const char *current = lv_label_get_text(label);
    if ((current != nullptr) && (std::strcmp(current, text) == 0)) {
        return false;
    }
    lv_label_set_text(label, text);
    return true;
}
} // namespace

WatchHomeApp *WatchHomeApp::_instance = nullptr;

bool watch_home_is_active()
{
    return s_watch_home_active;
}

void watch_home_set_active(bool active)
{
    s_watch_home_active = active;
}

void watch_home_set_ai_chat_app_id(int app_id)
{
    s_ai_chat_app_id = app_id;
}

WatchHomeApp *WatchHomeApp::requestInstance()
{
    if (_instance == nullptr) {
        _instance = new WatchHomeApp();
    }
    return _instance;
}

WatchHomeApp::WatchHomeApp():
    systems::phone::App(APP_NAME, watch_app_icon_home_48(), true, false, false)
{
}

bool WatchHomeApp::run(void)
{
    watch_home_set_active(true);

    lv_obj_t *screen = lv_scr_act();
    ESP_UTILS_CHECK_NULL_RETURN(screen, false, "Invalid active screen");

    lv_obj_set_style_bg_color(screen, lv_color_hex(0x020304), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    lv_obj_t *root = lv_obj_create(screen);
    ESP_UTILS_CHECK_NULL_RETURN(root, false, "Create root failed");
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root, lv_color_hex(0x020304), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_center(root);

    _battery_label = lv_label_create(root);
    ESP_UTILS_CHECK_NULL_RETURN(_battery_label, false, "Create battery label failed");
    lv_label_set_text(_battery_label, "BAT --");
    lv_obj_set_style_text_color(_battery_label, lv_color_hex(0xB8C0CC), 0);
    lv_obj_set_style_text_font(_battery_label, &lv_font_montserrat_14, 0);
    lv_obj_set_width(_battery_label, 104);
    lv_label_set_long_mode(_battery_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(_battery_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(_battery_label, LV_ALIGN_TOP_LEFT, SAFE_SIDE + 8, SAFE_TOP + 4);

    _status_label = lv_label_create(root);
    ESP_UTILS_CHECK_NULL_RETURN(_status_label, false, "Create status label failed");
    lv_label_set_text(_status_label, "WiFi --");
    lv_obj_set_style_text_color(_status_label, lv_color_hex(0xB8C0CC), 0);
    lv_obj_set_style_text_font(_status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_width(_status_label, 168);
    lv_label_set_long_mode(_status_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(_status_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(_status_label, LV_ALIGN_TOP_RIGHT, -(SAFE_SIDE + 8), SAFE_TOP + 4);

    _time_label = lv_label_create(root);
    ESP_UTILS_CHECK_NULL_RETURN(_time_label, false, "Create time label failed");
    lv_label_set_text(_time_label, "--:--");
    lv_obj_set_style_text_color(_time_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(_time_label, &lv_font_montserrat_48, 0);
    // Keep the clock at a native LVGL font size.  Scaling a label with
    // transform_zoom made the glyph bounds exceed the rounded display safe
    // area and could leave stale horizontal bands after a refresh.
    lv_obj_set_width(_time_label, 300);
    lv_obj_set_style_text_align(_time_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(_time_label, LV_ALIGN_CENTER, 0, -48);

    _date_label = lv_label_create(root);
    ESP_UTILS_CHECK_NULL_RETURN(_date_label, false, "Create date label failed");
    lv_label_set_text(_date_label, "Time not synced");
    lv_obj_set_style_text_color(_date_label, lv_color_hex(0xFF8AA2), 0);
    lv_obj_set_style_text_font(_date_label, &lv_font_montserrat_24, 0);
    lv_obj_align_to(_date_label, _time_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 28);

    _weather_label = lv_label_create(root);
    ESP_UTILS_CHECK_NULL_RETURN(_weather_label, false, "Create weather label failed");
    lv_label_set_text(_weather_label, "weather --");
    lv_obj_set_style_text_color(_weather_label, lv_color_hex(0x69D2FF), 0);
    lv_obj_set_style_text_font(_weather_label, &lv_font_montserrat_18, 0);
    lv_obj_set_width(_weather_label, LV_PCT(100));
    lv_label_set_long_mode(_weather_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(_weather_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align_to(_weather_label, _date_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);

    _anniv_label = lv_label_create(root);
    ESP_UTILS_CHECK_NULL_RETURN(_anniv_label, false, "Create anniversary label failed");
    lv_label_set_text(_anniv_label, "Anniv --");
    lv_obj_set_style_text_color(_anniv_label, lv_color_hex(0xFFCC66), 0);
    lv_obj_set_style_text_font(_anniv_label, &lv_font_montserrat_16, 0);
    lv_obj_set_width(_anniv_label, LV_PCT(88));
    lv_label_set_long_mode(_anniv_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(_anniv_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align_to(_anniv_label, _weather_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);

    _quota_label = lv_label_create(root);
    ESP_UTILS_CHECK_NULL_RETURN(_quota_label, false, "Create quota label failed");
    lv_label_set_text(_quota_label, "Quota --");
    lv_obj_set_style_text_color(_quota_label, lv_color_hex(0xA6F0C6), 0);
    lv_obj_set_style_text_font(_quota_label, &lv_font_montserrat_14, 0);
    lv_obj_set_width(_quota_label, LV_PCT(90));
    lv_label_set_long_mode(_quota_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(_quota_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align_to(_quota_label, _anniv_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);

    lv_obj_t *brand = lv_label_create(root);
    ESP_UTILS_CHECK_NULL_RETURN(brand, false, "Create brand label failed");
    const esp_app_desc_t *app_desc = esp_app_get_description();
    char version_text[40] = {};
    std::snprintf(version_text, sizeof(version_text), "v%s",
                  (app_desc != nullptr) ? app_desc->version : "--");
    lv_label_set_text(brand, version_text);
    lv_obj_set_style_text_color(brand, lv_color_hex(0x8B94A1), 0);
    lv_obj_set_style_text_font(brand, &lv_font_montserrat_16, 0);
    lv_obj_align_to(brand, _quota_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);

    lv_obj_t *live = lv_button_create(root);
    ESP_UTILS_CHECK_NULL_RETURN(live, false, "Create LiveTalk shortcut failed");
    lv_obj_set_size(live, 132, 42);
    lv_obj_set_style_radius(live, 21, 0);
    lv_obj_set_style_bg_color(live, lv_color_hex(0x5A397F), 0);
    lv_obj_set_style_bg_opa(live, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(live, 0, 0);
    lv_obj_add_event_cb(live, onLiveTalkButtonClicked, LV_EVENT_CLICKED, this);
    lv_obj_align(live, LV_ALIGN_BOTTOM_MID, 0, -(SAFE_BOTTOM + 28));
    lv_obj_t *live_label = lv_label_create(live);
    ESP_UTILS_CHECK_NULL_RETURN(live_label, false, "Create LiveTalk label failed");
    lv_label_set_text(live_label, "LiveTalk");
    lv_obj_set_style_text_color(live_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(live_label, &lv_font_montserrat_18, 0);
    lv_obj_center(live_label);

    lv_obj_t *hint = lv_label_create(root);
    ESP_UTILS_CHECK_NULL_RETURN(hint, false, "Create PWR hint failed");
    lv_label_set_text(hint, "PWR: Apps");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x4D5666), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -SAFE_BOTTOM);

    refreshClock();
    if (watch::wifi_is_connected()) {
        watch::weather_refresh_async();
    }
    if (_clock_timer != nullptr) {
        lv_timer_delete(_clock_timer);
        _clock_timer = nullptr;
    }
    _clock_timer = lv_timer_create(onClockTimer, HOME_CLOCK_REFRESH_MS, this);
    _wifi_reconnect_tick = lv_tick_get();
    lv_timer_t *wifi_reconnect_timer = lv_timer_create([](lv_timer_t *timer) {
        auto *app = static_cast<WatchHomeApp *>(lv_timer_get_user_data(timer));
        if ((app != nullptr) && watch::wifi_has_credentials() && !watch::wifi_is_connected()) {
            app->_wifi_reconnect_tick = lv_tick_get();
            ESP_UTILS_LOGI("WatchHome delayed WiFi reconnect requested");
            watch::wifi_reconnect_saved();
        }
        lv_timer_delete(timer);
    }, 8000, this);
    if (wifi_reconnect_timer != nullptr) {
        lv_timer_set_repeat_count(wifi_reconnect_timer, 1);
    }

    return true;
}

bool WatchHomeApp::back(void)
{
    watch_home_set_active(false);
    if (_clock_timer != nullptr) {
        lv_timer_delete(_clock_timer);
        _clock_timer = nullptr;
    }
    ESP_UTILS_LOGI("Back to app launcher");
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

bool WatchHomeApp::close(void)
{
    watch_home_set_active(false);
    if (_clock_timer != nullptr) {
        lv_timer_delete(_clock_timer);
        _clock_timer = nullptr;
    }
    _battery_label = nullptr;
    _status_label = nullptr;
    _time_label = nullptr;
    _date_label = nullptr;
    _weather_label = nullptr;
    _anniv_label = nullptr;
    _quota_label = nullptr;
    return true;
}

void WatchHomeApp::onClockTimer(lv_timer_t *timer)
{
    auto *app = static_cast<WatchHomeApp *>(lv_timer_get_user_data(timer));
    if (app != nullptr) {
        app->refreshClock();
    }
}

void WatchHomeApp::onMenuButtonClicked(lv_event_t *event)
{
    auto *app = static_cast<WatchHomeApp *>(lv_event_get_user_data(event));
    if (app != nullptr) {
        app->back();
    }
}

void WatchHomeApp::onLiveTalkButtonClicked(lv_event_t *event)
{
    auto *app = static_cast<WatchHomeApp *>(lv_event_get_user_data(event));
    if (app == nullptr) {
        return;
    }
    auto *context = app->getSystemContext();
    if ((context == nullptr) || (s_ai_chat_app_id < 0)) {
        ESP_UTILS_LOGW("LiveTalk shortcut: AI Chat app id not ready");
        return;
    }
    AiChatApp::requestOpenLiveOnNextRun();
    watch_home_set_active(false);
    systems::base::Context::AppEventData app_event_data = {
        .id = s_ai_chat_app_id,
        .type = systems::base::Context::AppEventType::START,
        .data = nullptr,
    };
    ESP_UTILS_CHECK_FALSE_EXIT(context->sendAppEvent(&app_event_data), "LiveTalk shortcut failed");
}

void WatchHomeApp::refreshClock()
{
    if ((_time_label == nullptr) || (_date_label == nullptr)) {
        return;
    }

    if (!watch::display_is_on()) {
        return;
    }

    if ((_status_refresh_tick == 0) || (lv_tick_elaps(_status_refresh_tick) >= HOME_STATUS_REFRESH_MS)) {
        _status_refresh_tick = lv_tick_get();
        refreshStatus();
    }

    time_t now = 0;
    time(&now);

    struct tm timeinfo = {};
    localtime_r(&now, &timeinfo);

    char time_text[8] = {};
    char date_text[32] = {};
    if (timeinfo.tm_year < (2024 - 1900)) {
        set_label_text_if_changed(_time_label, "--:--");
        set_label_text_if_changed(_date_label, "Time not synced");
        return;
    }

    strftime(time_text, sizeof(time_text), "%H:%M", &timeinfo);
    strftime(date_text, sizeof(date_text), "%b %d  %a", &timeinfo);
    set_label_text_if_changed(_time_label, time_text);
    set_label_text_if_changed(_date_label, date_text);
}

void WatchHomeApp::refreshStatus()
{
    if (!watch::display_is_on()) {
        return;
    }
    if (_battery_label != nullptr) {
        char battery_text[16] = {};
        const int percent = watch::power_battery_percent();
        if (percent >= 0) {
            snprintf(battery_text, sizeof(battery_text), "%s %d%%", watch::power_is_charging() ? "CHG" : "BAT", percent);
        } else {
            snprintf(battery_text, sizeof(battery_text), "BAT --");
        }
        set_label_text_if_changed(_battery_label, battery_text);
    }

    if (_status_label != nullptr) {
        char status_text[40] = {};
        if (watch::wifi_is_connected()) {
            char wifi[96] = {};
            watch::wifi_status_text(wifi, sizeof(wifi));
            const char *prefix = "Connected: ";
            const char *name = std::strstr(wifi, prefix);
            if (name != nullptr) {
                name += std::strlen(prefix);
                const char *rssi = std::strstr(name, "  RSSI ");
                if (rssi != nullptr) {
                    char ssid[20] = {};
                    const size_t ssid_len = std::min<size_t>(sizeof(ssid) - 1, static_cast<size_t>(rssi - name));
                    std::memcpy(ssid, name, ssid_len);
                    std::snprintf(status_text, sizeof(status_text), "%s %ddBm", ssid, watch::wifi_rssi_dbm());
                } else {
                    std::snprintf(status_text, sizeof(status_text), "%ddBm", watch::wifi_rssi_dbm());
                }
            } else {
                std::snprintf(status_text, sizeof(status_text), "%ddBm", watch::wifi_rssi_dbm());
            }
        } else if (watch::wifi_has_credentials()) {
            std::snprintf(status_text, sizeof(status_text), "WiFi saved");
        } else {
            std::snprintf(status_text, sizeof(status_text), "WiFi off");
        }
        set_label_text_if_changed(_status_label, status_text);
    }

    if (!watch::wifi_is_connected() && watch::wifi_has_credentials()) {
        if (_wifi_reconnect_tick == 0 || lv_tick_elaps(_wifi_reconnect_tick) > HOME_WIFI_RECONNECT_MS) {
            _wifi_reconnect_tick = lv_tick_get();
            ESP_UTILS_LOGI("WatchHome auto WiFi reconnect requested");
            watch::wifi_reconnect_saved();
        }
    }

    if (_weather_label != nullptr) {
        watch::WeatherSnapshot snapshot;
        const bool has_snapshot = watch::weather_snapshot(&snapshot);
        const bool stale = has_snapshot && snapshot.valid &&
            snapshot.last_refresh_us > 0 &&
            (esp_timer_get_time() - snapshot.last_refresh_us) > HOME_WEATHER_STALE_US;

        if (watch::wifi_is_connected() && !watch::weather_is_running()) {
            const bool needs_refresh = !has_snapshot || !snapshot.valid || stale;
            if (needs_refresh && (_weather_refresh_tick == 0 || lv_tick_elaps(_weather_refresh_tick) > 30000)) {
                _weather_refresh_tick = lv_tick_get();
                ESP_UTILS_LOGI("WatchHome weather refresh requested");
                watch::weather_refresh_async();
            }
        }

        if (has_snapshot && snapshot.valid) {
            char weather_text[64] = {};
            watch::weather_format_home(snapshot, weather_text, sizeof(weather_text));
            set_label_text_if_changed(_weather_label, weather_text);
        } else if (watch::weather_is_running()) {
            set_label_text_if_changed(_weather_label, "weather loading...");
        } else if (!watch::wifi_is_connected()) {
            set_label_text_if_changed(_weather_label, "weather offline");
        } else {
            set_label_text_if_changed(_weather_label, "weather --");
        }
    }

    if (_anniv_label != nullptr) {
        char anniv_text[64] = {};
        watch::anniversary_home_text(anniv_text, sizeof(anniv_text));
        set_label_text_if_changed(_anniv_label, anniv_text);
    }

    if (_quota_label != nullptr) {
        if (watch::wifi_is_connected() && !watch::quota_refresh_is_running()) {
            const bool should_refresh = _quota_refresh_tick == 0 || lv_tick_elaps(_quota_refresh_tick) > HOME_QUOTA_REFRESH_MS;
            if (should_refresh) {
                _quota_refresh_tick = lv_tick_get();
                watch::quota_refresh_async();
            }
        }
        char quota_text[96] = {};
        watch::quota_home_text(quota_text, sizeof(quota_text));
        set_label_text_if_changed(_quota_label, quota_text);
    }
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, WatchHomeApp, APP_NAME, []()
{
    return std::shared_ptr<WatchHomeApp>(WatchHomeApp::requestInstance(), [](WatchHomeApp *) {});
})

} // namespace esp_brookesia::apps
