#include "weather_app.hpp"

#include <cstdio>
#include <cstring>
#include <string>

#include "cJSON.h"
#include "esp_brookesia.hpp"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_lib_utils.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "watch_app_icons.hpp"
#include "watch_connectivity.hpp"
#include "watch_time.hpp"

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "WeatherApp"

namespace esp_brookesia::apps {
namespace {

constexpr const char *APP_NAME = "Weather";
constexpr int SAFE_TOP = 24;
constexpr int SAFE_SIDE = 22;
constexpr const char *LOGIN_URL = "http://<OTA_SERVER>/DUDUSERVER/api/v1/auth/login";
constexpr const char *WEATHER_URL = "http://<OTA_SERVER>/DUDUSERVER/api/v1/weather?compact=true";
constexpr const char *USER_AGENT = "esp32-s3-watch-weather";
constexpr int HTTP_TIMEOUT_MS = 12000;
constexpr size_t MAX_HTTP_BODY = 24 * 1024;
constexpr uint32_t WEATHER_TASK_STACK_SIZE = 8192;
constexpr uint32_t WEATHER_TIMER_IDLE_MS = 3000;
constexpr uint32_t WEATHER_TIMER_BUSY_MS = 500;

struct HttpResponse {
    int status = 0;
    std::string body;
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

esp_err_t http_event(esp_http_client_event_t *event)
{
    auto *body = static_cast<std::string *>(event->user_data);
    if (event->event_id == HTTP_EVENT_ON_DATA && body != nullptr && event->data != nullptr && event->data_len > 0) {
        if (body->size() + static_cast<size_t>(event->data_len) > MAX_HTTP_BODY) {
            return ESP_FAIL;
        }
        body->append(static_cast<const char *>(event->data), event->data_len);
    }
    return ESP_OK;
}

bool http_request(esp_http_client_method_t method, const char *url, const char *token, const char *body, HttpResponse &response)
{
    response = {};
    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = HTTP_TIMEOUT_MS;
    config.user_agent = USER_AGENT;
    config.event_handler = http_event;
    config.user_data = &response.body;
    config.keep_alive_enable = false;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        return false;
    }
    esp_http_client_set_method(client, method);
    if (token != nullptr && token[0] != '\0') {
        std::string auth = "Bearer ";
        auth += token;
        esp_http_client_set_header(client, "Authorization", auth.c_str());
    }
    if (body != nullptr && body[0] != '\0') {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, body, std::strlen(body));
    }
    esp_err_t err = esp_http_client_perform(client);
    response.status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    return err == ESP_OK && response.status >= 200 && response.status < 300;
}

std::string json_string(cJSON *object, const char *name, const char *fallback = "")
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (cJSON_IsString(item) && item->valuestring != nullptr) {
        return item->valuestring;
    }
    return fallback;
}

double json_double(cJSON *object, const char *name, double fallback = 0.0)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (cJSON_IsNumber(item)) {
        return item->valuedouble;
    }
    return fallback;
}

int json_int(cJSON *object, const char *name, int fallback = 0)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (cJSON_IsNumber(item)) {
        return item->valueint;
    }
    return fallback;
}

std::string login_token()
{
    HttpResponse response;
    const char *body = "{\"username\":\"admin\",\"password\":\"<OTA_PASSWORD>\"}";
    if (!http_request(HTTP_METHOD_POST, LOGIN_URL, nullptr, body, response)) {
        ESP_LOGW(ESP_UTILS_LOG_TAG, "login failed: http=%d", response.status);
        return {};
    }
    cJSON *root = cJSON_Parse(response.body.c_str());
    if (root == nullptr) {
        return {};
    }
    std::string token = json_string(root, "access_token");
    cJSON_Delete(root);
    return token;
}

std::string temp_text(cJSON *current)
{
    const double temp = json_double(current, "temperature_2m", json_double(current, "temperature_c", -1000.0));
    if (temp < -200.0) {
        return "-- C";
    }
    char text[24] = {};
    std::snprintf(text, sizeof(text), "%.1f C", temp);
    return text;
}

bool is_ascii_text(const std::string &text)
{
    for (unsigned char ch: text) {
        if (ch < 0x20 || ch > 0x7E) {
            return false;
        }
    }
    return true;
}

std::string weather_text_for_ui(const std::string &text)
{
    struct Mapping {
        const char *zh;
        const char *en;
    };
    static constexpr Mapping mappings[] = {
        {"晴", "Clear"},
        {"主要晴朗", "Mostly clear"},
        {"少云", "Partly cloudy"},
        {"多云", "Cloudy"},
        {"阴", "Overcast"},
        {"雾", "Fog"},
        {"雾凇", "Rime fog"},
        {"毛毛雨", "Drizzle"},
        {"小雨", "Light rain"},
        {"中雨", "Rain"},
        {"大雨", "Heavy rain"},
        {"暴雨", "Rainstorm"},
        {"冻雨", "Freezing rain"},
        {"小雪", "Light snow"},
        {"中雪", "Snow"},
        {"大雪", "Heavy snow"},
        {"雪粒", "Snow grains"},
        {"阵雨", "Showers"},
        {"雷阵雨", "Thunderstorm"},
        {"雷暴", "Thunderstorm"},
    };
    for (const auto &mapping: mappings) {
        if (text == mapping.zh) {
            return mapping.en;
        }
    }
    return is_ascii_text(text) ? text : "Weather";
}

std::string wind_text_for_ui(const std::string &text)
{
    struct Mapping {
        const char *zh;
        const char *en;
    };
    static constexpr Mapping mappings[] = {
        {"北", "N"},
        {"东北", "NE"},
        {"东", "E"},
        {"东南", "SE"},
        {"南", "S"},
        {"西南", "SW"},
        {"西", "W"},
        {"西北", "NW"},
        {"北东北", "NNE"},
        {"东东北", "ENE"},
        {"东东南", "ESE"},
        {"南东南", "SSE"},
        {"南西南", "SSW"},
        {"西西南", "WSW"},
        {"西西北", "WNW"},
        {"北西北", "NNW"},
    };
    for (const auto &mapping: mappings) {
        if (text == mapping.zh) {
            return mapping.en;
        }
    }
    return is_ascii_text(text) ? text : "-";
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
    _mutex = xSemaphoreCreateMutex();
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
    lv_obj_set_size(back_btn, 92, 44);
    style_button(back_btn, 0x232833);
    lv_obj_add_event_cb(back_btn, onBackClicked, LV_EVENT_CLICKED, this);
    lv_obj_center(make_label(back_btn, "Back", &lv_font_montserrat_18, 0xFFFFFF));

    lv_obj_t *main_card = make_card(root, 210);
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
    _sync_label = make_label(info_card, "", &lv_font_montserrat_16, 0xD7DCE5);
    lv_obj_set_width(_sync_label, LV_PCT(100));
    lv_obj_set_style_text_align(_sync_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(_sync_label, LV_LABEL_LONG_WRAP);

    lv_obj_t *refresh_btn = lv_button_create(root);
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
    if (app != nullptr) app->back();
}

void WeatherApp::onRefreshClicked(lv_event_t *event)
{
    auto *app = static_cast<WeatherApp *>(lv_event_get_user_data(event));
    if (app == nullptr) return;
    ++app->_refresh_count;
    app->startRefresh();
}

void WeatherApp::onTimer(lv_timer_t *timer)
{
    auto *app = static_cast<WeatherApp *>(lv_timer_get_user_data(timer));
    if (app != nullptr) {
        bool running = false;
        if (app->_mutex != nullptr && xSemaphoreTake(app->_mutex, 0) == pdTRUE) {
            running = app->_running;
            xSemaphoreGive(app->_mutex);
        }
        if (app->_timer != nullptr) {
            lv_timer_set_period(app->_timer, running ? WEATHER_TIMER_BUSY_MS : WEATHER_TIMER_IDLE_MS);
        }
        app->refreshUi();
    }
}

bool WeatherApp::startRefresh()
{
    if (!watch::wifi_is_connected()) {
        char wifi[96] = {};
        watch::wifi_status_text(wifi, sizeof(wifi));
        setWeatherState(false, false, "-- C", "WiFi not connected", wifi, "Connect WiFi first, then refresh.");
        return false;
    }
    if (_mutex != nullptr && xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (_running) {
            xSemaphoreGive(_mutex);
            return false;
        }
        _running = true;
        _ok = false;
        _main = "-- C";
        _detail = "Loading DUDUSERVER weather...";
        _sync = "Login and fetch /api/v1/weather";
        char wifi[96] = {};
        watch::wifi_status_text(wifi, sizeof(wifi));
        _network = wifi;
        xSemaphoreGive(_mutex);
    }
    if (xTaskCreateWithCaps(
            weatherTask,
            "watch_weather",
            WEATHER_TASK_STACK_SIZE,
            this,
            5,
            nullptr,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        ) != pdPASS) {
        setWeatherState(false, false, "-- C", "Task create failed", "", "FreeRTOS could not start weather task.");
        return false;
    }
    return true;
}

void WeatherApp::setWeatherState(bool running, bool ok, const char *main, const char *detail, const char *network, const char *sync)
{
    if (_mutex != nullptr && xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        _running = running;
        _ok = ok;
        _main = main != nullptr ? main : "";
        _detail = detail != nullptr ? detail : "";
        _network = network != nullptr ? network : "";
        _sync = sync != nullptr ? sync : "";
        _last_refresh_us = esp_timer_get_time();
        xSemaphoreGive(_mutex);
    }
}

void WeatherApp::refreshUi()
{
    std::string main;
    std::string detail;
    std::string network;
    std::string sync;
    bool running = false;
    bool ok = false;
    int64_t last_us = 0;
    if (_mutex != nullptr && xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        main = _main;
        detail = _detail;
        network = _network;
        sync = _sync;
        running = _running;
        ok = _ok;
        last_us = _last_refresh_us;
        xSemaphoreGive(_mutex);
    }

    char wifi[96] = {};
    watch::wifi_status_text(wifi, sizeof(wifi));

    if (_main_label != nullptr) {
        lv_label_set_text(_main_label, main.c_str());
        lv_obj_set_style_text_color(_main_label, lv_color_hex(ok ? 0xFFFFFF : (running ? 0xFFCC33 : 0xFFFFFF)), 0);
    }
    if (_detail_label != nullptr) {
        lv_label_set_text(_detail_label, detail.c_str());
    }
    if (_network_label != nullptr) {
        lv_label_set_text(_network_label, network.empty() ? wifi : network.c_str());
    }
    if (_sync_label != nullptr) {
        lv_label_set_text(_sync_label, sync.c_str());
    }
    if (_refresh_label != nullptr) {
        const bool recent = last_us > 0 && (esp_timer_get_time() - last_us) < 3000000;
        lv_label_set_text(_refresh_label, running ? "Loading..." : (recent ? (ok ? "OK" : "Retry") : "Refresh"));
    }
}

void WeatherApp::weatherTask(void *arg)
{
    auto *app = static_cast<WeatherApp *>(arg);
    if (app == nullptr) {
        vTaskDelete(nullptr);
        return;
    }

    std::string token = login_token();
    if (token.empty()) {
        app->setWeatherState(false, false, "-- C", "Login failed", "", "Check DUDUSERVER login/network.");
        vTaskDelete(nullptr);
        return;
    }

    HttpResponse response;
    if (!http_request(HTTP_METHOD_GET, WEATHER_URL, token.c_str(), nullptr, response)) {
        char detail[96] = {};
        std::snprintf(detail, sizeof(detail), "Weather HTTP failed: %d", response.status);
        app->setWeatherState(false, false, "-- C", detail, "", "GET /api/v1/weather?compact=true failed.");
        vTaskDelete(nullptr);
        return;
    }

    cJSON *root = cJSON_Parse(response.body.c_str());
    if (root == nullptr) {
        app->setWeatherState(false, false, "-- C", "Weather JSON parse failed", "", "Invalid DUDUSERVER response.");
        vTaskDelete(nullptr);
        return;
    }

    const std::string city = json_string(root, "city", "-");
    const std::string provider = json_string(root, "provider", "-");
    const std::string timezone = json_string(root, "timezone", "-");
    cJSON *current = cJSON_GetObjectItemCaseSensitive(root, "current");
    cJSON *daily = cJSON_GetObjectItemCaseSensitive(root, "daily");
    cJSON *today = cJSON_IsArray(daily) ? cJSON_GetArrayItem(daily, 0) : nullptr;

    std::string main = "-- C";
    std::string weather = "-";
    std::string wind_dir = "-";
    double apparent = 0.0;
    double humidity = 0.0;
    double wind = 0.0;
    double gust = 0.0;
    double precipitation = 0.0;
    std::string current_time = "-";
    if (cJSON_IsObject(current)) {
        main = temp_text(current);
        weather = weather_text_for_ui(json_string(current, "weather_text", "-"));
        wind_dir = wind_text_for_ui(json_string(current, "wind_direction_text", "-"));
        apparent = json_double(current, "apparent_temperature", 0.0);
        humidity = json_double(current, "relative_humidity_2m", 0.0);
        wind = json_double(current, "wind_speed_10m", json_double(current, "wind_speed_kmh", 0.0));
        gust = json_double(current, "wind_gusts_10m", 0.0);
        precipitation = json_double(current, "precipitation", 0.0);
        current_time = json_string(current, "time", "-");
    }

    double high = 0.0;
    double low = 0.0;
    int rain_prob = -1;
    std::string today_weather = "";
    if (cJSON_IsObject(today)) {
        high = json_double(today, "temperature_2m_max", 0.0);
        low = json_double(today, "temperature_2m_min", 0.0);
        rain_prob = json_int(today, "precipitation_probability_max", -1);
        today_weather = weather_text_for_ui(json_string(today, "weather_text", ""));
    }

    char detail[256] = {};
    if (rain_prob >= 0) {
        std::snprintf(
            detail,
            sizeof(detail),
            "%s  %s\nFeels %.1f C  Humidity %.0f%%\nWind %s %.1fkm/h gust %.1f\nToday %.1f/%.1f C  Rain %d%%",
            city.c_str(),
            weather.c_str(),
            apparent,
            humidity,
            wind_dir.c_str(),
            wind,
            gust,
            high,
            low,
            rain_prob
        );
    } else {
        std::snprintf(
            detail,
            sizeof(detail),
            "%s  %s\nFeels %.1f C  Humidity %.0f%%\nWind %s %.1fkm/h gust %.1f\nPrecipitation %.1fmm",
            city.c_str(),
            weather.c_str(),
            apparent,
            humidity,
            wind_dir.c_str(),
            wind,
            gust,
            precipitation
        );
    }

    char wifi[96] = {};
    watch::wifi_status_text(wifi, sizeof(wifi));

    char sync[192] = {};
    std::snprintf(
        sync,
        sizeof(sync),
        "Source: %s\nTime: %s\nTZ: %s%s%s",
        provider.c_str(),
        current_time.c_str(),
        timezone.c_str(),
        today_weather.empty() ? "" : "\nToday: ",
        today_weather.c_str()
    );

    app->setWeatherState(false, true, main.c_str(), detail, wifi, sync);
    cJSON_Delete(root);
    vTaskDelete(nullptr);
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, WeatherApp, APP_NAME, []()
{
    return std::shared_ptr<WeatherApp>(WeatherApp::requestInstance(), [](WeatherApp *) {});
})

} // namespace esp_brookesia::apps
