#include "weather_app.hpp"

#include <cstdio>

#include "esp_brookesia.hpp"
#include "esp_lib_utils.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "watch_app_icons.hpp"
#include "watch_connectivity.hpp"
#include "watch_weather.hpp"

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "WeatherApp"

namespace esp_brookesia::apps {
namespace {

constexpr const char *APP_NAME = "Weather";
constexpr int SAFE_TOP = 24;
constexpr int SAFE_SIDE = 22;
constexpr uint32_t WEATHER_TIMER_IDLE_MS = 3000;
constexpr uint32_t WEATHER_TIMER_BUSY_MS = 500;

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
    lv_obj_set_style_pad_row(card, 10, 0);
    return card;
}

} // namespace

WeatherApp *WeatherApp::_instance = nullptr;

WeatherApp *WeatherApp::requestInstance()
{
    if (_instance == nullptr) {
        _instance = new WeatherApp();
    }
    return _instance;
}

WeatherApp::WeatherApp():
    systems::phone::App(APP_NAME, watch_app_icon_weather_48(), true, true, true)
{
}

bool WeatherApp::run(void)
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

    make_label(title_row, "Weather", &lv_font_montserrat_30, 0xFFFFFF);
    lv_obj_t *back_btn = lv_button_create(title_row);
    ESP_UTILS_CHECK_NULL_RETURN(back_btn, false, "Create back button failed");
    lv_obj_set_size(back_btn, 92, 44);
    style_button(back_btn, 0x232833);
    lv_obj_add_event_cb(back_btn, onBackClicked, LV_EVENT_CLICKED, this);
    lv_obj_center(make_label(back_btn, "Back", &lv_font_montserrat_18, 0xFFFFFF));

    lv_obj_t *main_card = make_card(root, 210);
    ESP_UTILS_CHECK_NULL_RETURN(main_card, false, "Create weather main card failed");
    _main_label = make_label(main_card, "--", &lv_font_montserrat_48, 0xFFFFFF);
    lv_obj_set_width(_main_label, LV_PCT(100));
    lv_obj_set_style_text_align(_main_label, LV_TEXT_ALIGN_CENTER, 0);

    _detail_label = make_label(main_card, "Tap Refresh", &lv_font_montserrat_18, 0xAEB7C6);
    lv_obj_set_width(_detail_label, LV_PCT(100));
    lv_obj_set_style_text_align(_detail_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(_detail_label, LV_LABEL_LONG_WRAP);

    _network_label = make_label(main_card, "", &lv_font_montserrat_16, 0x69D2FF);
    lv_obj_set_width(_network_label, LV_PCT(100));
    lv_obj_set_style_text_align(_network_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(_network_label, LV_LABEL_LONG_WRAP);

    lv_obj_t *info_card = make_card(root, 128);
    ESP_UTILS_CHECK_NULL_RETURN(info_card, false, "Create weather info card failed");
    _sync_label = make_label(info_card, "", &lv_font_montserrat_16, 0xD7DCE5);
    lv_obj_set_width(_sync_label, LV_PCT(100));
    lv_obj_set_style_text_align(_sync_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(_sync_label, LV_LABEL_LONG_WRAP);

    lv_obj_t *refresh_btn = lv_button_create(root);
    ESP_UTILS_CHECK_NULL_RETURN(refresh_btn, false, "Create refresh button failed");
    lv_obj_set_width(refresh_btn, LV_PCT(100));
    lv_obj_set_height(refresh_btn, 54);
    style_button(refresh_btn, 0x1B6BFF);
    lv_obj_add_event_cb(refresh_btn, onRefreshClicked, LV_EVENT_CLICKED, this);
    _refresh_label = make_label(refresh_btn, "Refresh", &lv_font_montserrat_20, 0xFFFFFF);
    lv_obj_center(_refresh_label);

    refreshUi();
    _timer = lv_timer_create(onTimer, WEATHER_TIMER_IDLE_MS, this);
    startRefresh();
    return true;
}

bool WeatherApp::back(void)
{
    if (_timer != nullptr) {
        lv_timer_delete(_timer);
        _timer = nullptr;
    }
    clearObjects();
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

bool WeatherApp::close(void)
{
    if (_timer != nullptr) {
        lv_timer_delete(_timer);
        _timer = nullptr;
    }
    clearObjects();
    return true;
}

void WeatherApp::clearObjects()
{
    _network_label = nullptr;
    _main_label = nullptr;
    _detail_label = nullptr;
    _sync_label = nullptr;
    _refresh_label = nullptr;
}

void WeatherApp::onBackClicked(lv_event_t *event)
{
    auto *app = static_cast<WeatherApp *>(lv_event_get_user_data(event));
    if (app != nullptr) {
        app->back();
    }
}

void WeatherApp::onRefreshClicked(lv_event_t *event)
{
    auto *app = static_cast<WeatherApp *>(lv_event_get_user_data(event));
    if (app != nullptr) {
        app->startRefresh();
    }
}

void WeatherApp::onTimer(lv_timer_t *timer)
{
    auto *app = static_cast<WeatherApp *>(lv_timer_get_user_data(timer));
    if (app == nullptr) {
        return;
    }
    if (app->_timer != nullptr) {
        lv_timer_set_period(app->_timer, watch::weather_is_running() ? WEATHER_TIMER_BUSY_MS : WEATHER_TIMER_IDLE_MS);
    }
    app->refreshUi();
}

bool WeatherApp::startRefresh()
{
    if (!watch::wifi_is_connected()) {
        refreshUi();
        return false;
    }
    const bool started = watch::weather_refresh_async();
    refreshUi();
    return started;
}

void WeatherApp::refreshUi()
{
    char wifi[96] = {};
    watch::wifi_status_text(wifi, sizeof(wifi));

    watch::WeatherSnapshot snapshot;
    const bool has_snapshot = watch::weather_snapshot(&snapshot);
    const bool running = watch::weather_is_running() || (has_snapshot && snapshot.running);
    const bool ok = has_snapshot && snapshot.valid && snapshot.ok;

    char main[32] = "-- C";
    char detail[256] = {};
    char sync[192] = {};

    if (!watch::wifi_is_connected()) {
        std::snprintf(detail, sizeof(detail), "WiFi not connected");
        std::snprintf(sync, sizeof(sync), "Connect WiFi in Settings, then refresh.");
    } else if (running && !ok) {
        std::snprintf(detail, sizeof(detail), "Loading private OTA backend weather...");
        std::snprintf(sync, sizeof(sync), "GET /api/v1/weather?compact=true");
    } else if (ok) {
        std::snprintf(main, sizeof(main), "%.1f C", snapshot.temperature_c);
        if (snapshot.rain_prob >= 0) {
            std::snprintf(
                detail,
                sizeof(detail),
                "%s  %s\nFeels %.1f C  Humidity %.0f%%\nWind %s %.1fkm/h gust %.1f\nToday %.1f/%.1f C  Rain %d%%",
                snapshot.city[0] != '\0' ? snapshot.city : "-",
                snapshot.condition[0] != '\0' ? snapshot.condition : "-",
                snapshot.apparent_c,
                snapshot.humidity,
                snapshot.wind_dir[0] != '\0' ? snapshot.wind_dir : "-",
                snapshot.wind_kmh,
                snapshot.gust_kmh,
                snapshot.temp_max_c,
                snapshot.temp_min_c,
                snapshot.rain_prob
            );
        } else {
            std::snprintf(
                detail,
                sizeof(detail),
                "%s  %s\nFeels %.1f C  Humidity %.0f%%\nWind %s %.1fkm/h gust %.1f\nPrecipitation %.1fmm",
                snapshot.city[0] != '\0' ? snapshot.city : "-",
                snapshot.condition[0] != '\0' ? snapshot.condition : "-",
                snapshot.apparent_c,
                snapshot.humidity,
                snapshot.wind_dir[0] != '\0' ? snapshot.wind_dir : "-",
                snapshot.wind_kmh,
                snapshot.gust_kmh,
                snapshot.precipitation_mm
            );
        }
        std::snprintf(
            sync,
            sizeof(sync),
            "Source: %s\nTime: %s\nTZ: %s",
            snapshot.source[0] != '\0' ? snapshot.source : "-",
            snapshot.current_time[0] != '\0' ? snapshot.current_time : "-",
            snapshot.timezone[0] != '\0' ? snapshot.timezone : "-"
        );
    } else {
        std::snprintf(detail, sizeof(detail), "Tap Refresh");
        std::snprintf(sync, sizeof(sync), "private OTA backend weather");
    }

    if (_main_label != nullptr) {
        lv_label_set_text(_main_label, main);
        lv_obj_set_style_text_color(_main_label, lv_color_hex(ok ? 0xFFFFFF : (running ? 0xFFCC33 : 0xFFFFFF)), 0);
    }
    if (_detail_label != nullptr) {
        lv_label_set_text(_detail_label, detail);
    }
    if (_network_label != nullptr) {
        lv_label_set_text(_network_label, wifi);
    }
    if (_sync_label != nullptr) {
        lv_label_set_text(_sync_label, sync);
    }
    if (_refresh_label != nullptr) {
        const bool recent = ok && snapshot.last_refresh_us > 0 && (esp_timer_get_time() - snapshot.last_refresh_us) < 3000000;
        lv_label_set_text(_refresh_label, running ? "Loading..." : (recent ? "OK" : "Refresh"));
    }
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, WeatherApp, APP_NAME, []()
{
    return std::shared_ptr<WeatherApp>(WeatherApp::requestInstance(), [](WeatherApp *) {});
})

} // namespace esp_brookesia::apps
