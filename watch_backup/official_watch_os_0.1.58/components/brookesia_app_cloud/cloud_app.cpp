#include "cloud_app.hpp"

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

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "CloudApp"

namespace esp_brookesia::apps {
namespace {

constexpr const char *APP_NAME = "Cloud";
constexpr int SAFE_TOP = 24;
constexpr int SAFE_SIDE = 22;
constexpr const char *LOGIN_URL = "http://<OTA_SERVER>/DUDUSERVER/api/v1/auth/login";
constexpr const char *DEVICE_STATUS_URL = "http://<OTA_SERVER>/DUDUSERVER/api/v1/device-status";
constexpr const char *DEVICE_CONFIG_URL = "http://<OTA_SERVER>/DUDUSERVER/api/v1/device-config";
constexpr const char *WEATHER_URL = "http://<OTA_SERVER>/DUDUSERVER/api/v1/weather?compact=true";
constexpr const char *USERNAME = "admin";
constexpr const char *PASSWORD = "<OTA_PASSWORD>";
constexpr const char *USER_AGENT = "esp32-s3-watch-cloud";
constexpr int HTTP_TIMEOUT_MS = 12000;
constexpr size_t MAX_HTTP_BODY = 32 * 1024;
constexpr uint32_t CLOUD_TASK_STACK_SIZE = 8192;
constexpr uint32_t CLOUD_TIMER_IDLE_MS = 3000;
constexpr uint32_t CLOUD_TIMER_BUSY_MS = 500;

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

int json_int(cJSON *object, const char *name, int fallback = 0)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (cJSON_IsNumber(item)) {
        return item->valueint;
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

bool json_bool(cJSON *object, const char *name, bool fallback = false)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (cJSON_IsBool(item)) {
        return cJSON_IsTrue(item);
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
    cJSON *item = cJSON_GetObjectItemCaseSensitive(current, "temperature_c");
    if (!cJSON_IsNumber(item)) {
        item = cJSON_GetObjectItemCaseSensitive(current, "temperature_2m");
    }
    if (!cJSON_IsNumber(item)) {
        return "--C";
    }
    char text[24] = {};
    std::snprintf(text, sizeof(text), "%.1fC", item->valuedouble);
    return text;
}

} // namespace

CloudApp *CloudApp::_instance = nullptr;

CloudApp *CloudApp::requestInstance()
{
    if (_instance == nullptr) {
        _instance = new CloudApp();
    }
    return _instance;
}

CloudApp::CloudApp():
    systems::phone::App(APP_NAME, watch_app_icon_cloud_48(), true, true, true)
{
    _mutex = xSemaphoreCreateMutex();
}

bool CloudApp::run(void)
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

    make_label(title_row, "Cloud", &lv_font_montserrat_30, 0xFFFFFF);
    lv_obj_t *back_btn = lv_button_create(title_row);
    lv_obj_set_size(back_btn, 92, 44);
    style_button(back_btn, 0x232833);
    lv_obj_add_event_cb(back_btn, onBackClicked, LV_EVENT_CLICKED, this);
    lv_obj_center(make_label(back_btn, "Back", &lv_font_montserrat_18, 0xFFFFFF));

    lv_obj_t *status_card = make_card(root, 138);
    _status_label = make_label(status_card, "Idle", &lv_font_montserrat_36, 0xFFFFFF);
    lv_obj_set_width(_status_label, LV_PCT(100));
    lv_obj_set_style_text_align(_status_label, LV_TEXT_ALIGN_CENTER, 0);

    _summary_label = make_label(status_card, "Tap Refresh", &lv_font_montserrat_18, 0x69D2FF);
    lv_obj_set_width(_summary_label, LV_PCT(100));
    lv_obj_set_style_text_align(_summary_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(_summary_label, LV_LABEL_LONG_WRAP);

    lv_obj_t *detail_card = make_card(root, 236);
    _detail_label = make_label(detail_card, "Cloud monitor reads DUDUSERVER status.", &lv_font_montserrat_16, 0xD7DCE5);
    lv_obj_set_width(_detail_label, LV_PCT(100));
    lv_label_set_long_mode(_detail_label, LV_LABEL_LONG_WRAP);

    lv_obj_t *refresh_btn = lv_button_create(root);
    lv_obj_set_width(refresh_btn, LV_PCT(100));
    lv_obj_set_height(refresh_btn, 54);
    style_button(refresh_btn, 0x1B6BFF);
    lv_obj_add_event_cb(refresh_btn, onRefreshClicked, LV_EVENT_CLICKED, this);
    _refresh_label = make_label(refresh_btn, "Refresh", &lv_font_montserrat_20, 0xFFFFFF);
    lv_obj_center(_refresh_label);

    refreshUi();
    _timer = lv_timer_create(onTimer, CLOUD_TIMER_IDLE_MS, this);
    startRefresh();
    return true;
}

bool CloudApp::back(void)
{
    if (_timer != nullptr) {
        lv_timer_delete(_timer);
        _timer = nullptr;
    }
    clearObjects();
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

bool CloudApp::close(void)
{
    if (_timer != nullptr) {
        lv_timer_delete(_timer);
        _timer = nullptr;
    }
    clearObjects();
    return true;
}

void CloudApp::clearObjects()
{
    _status_label = nullptr;
    _summary_label = nullptr;
    _detail_label = nullptr;
    _refresh_label = nullptr;
}

void CloudApp::onBackClicked(lv_event_t *event)
{
    auto *app = static_cast<CloudApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->back();
}

void CloudApp::onRefreshClicked(lv_event_t *event)
{
    auto *app = static_cast<CloudApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->startRefresh();
}

void CloudApp::onTimer(lv_timer_t *timer)
{
    auto *app = static_cast<CloudApp *>(lv_timer_get_user_data(timer));
    if (app != nullptr) {
        bool running = false;
        if (app->_mutex != nullptr && xSemaphoreTake(app->_mutex, 0) == pdTRUE) {
            running = app->_running;
            xSemaphoreGive(app->_mutex);
        }
        if (app->_timer != nullptr) {
            lv_timer_set_period(app->_timer, running ? CLOUD_TIMER_BUSY_MS : CLOUD_TIMER_IDLE_MS);
        }
        app->refreshUi();
    }
}

bool CloudApp::startRefresh()
{
    if (!watch::wifi_is_connected()) {
        setCloudState(false, false, "Offline", "WiFi not connected", "Connect WiFi first, then refresh cloud status.");
        return false;
    }
    if (_mutex != nullptr && xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (_running) {
            xSemaphoreGive(_mutex);
            return false;
        }
        _running = true;
        _status = "Loading";
        _summary = "Contacting DUDUSERVER";
        _detail = "Login and fetch device/weather/config status...";
        xSemaphoreGive(_mutex);
    }
    auto *app = this;
    BaseType_t ok = xTaskCreateWithCaps(
        cloudTask,
        "watch_cloud_app",
        CLOUD_TASK_STACK_SIZE,
        app,
        5,
        nullptr,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    if (ok != pdPASS) {
        setCloudState(false, false, "Error", "Task create failed", "FreeRTOS could not start cloud monitor task.");
        return false;
    }
    return true;
}

void CloudApp::setCloudState(bool running, bool ok, const char *status, const char *summary, const char *detail)
{
    if (_mutex != nullptr && xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        _running = running;
        _ok = ok;
        _status = status != nullptr ? status : "";
        _summary = summary != nullptr ? summary : "";
        _detail = detail != nullptr ? detail : "";
        _last_refresh_us = esp_timer_get_time();
        xSemaphoreGive(_mutex);
    }
}

void CloudApp::refreshUi()
{
    std::string status;
    std::string summary;
    std::string detail;
    bool running = false;
    bool ok = false;
    int64_t last_us = 0;
    if (_mutex != nullptr && xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        status = _status;
        summary = _summary;
        detail = _detail;
        running = _running;
        ok = _ok;
        last_us = _last_refresh_us;
        xSemaphoreGive(_mutex);
    }
    if (_status_label != nullptr) {
        lv_label_set_text(_status_label, status.c_str());
        lv_obj_set_style_text_color(_status_label, lv_color_hex(ok ? 0x23D18B : (running ? 0xFFCC33 : 0xFFFFFF)), 0);
    }
    if (_summary_label != nullptr) {
        lv_label_set_text(_summary_label, summary.c_str());
    }
    if (_detail_label != nullptr) {
        lv_label_set_text(_detail_label, detail.c_str());
    }
    if (_refresh_label != nullptr) {
        const bool recent = last_us > 0 && (esp_timer_get_time() - last_us) < 3000000;
        lv_label_set_text(_refresh_label, running ? "Loading..." : (recent ? (ok ? "OK" : "Retry") : "Refresh"));
    }
}

void CloudApp::cloudTask(void *arg)
{
    auto *app = static_cast<CloudApp *>(arg);
    if (app == nullptr) {
        vTaskDelete(nullptr);
        return;
    }

    std::string token = login_token();
    if (token.empty()) {
        app->setCloudState(false, false, "Error", "Login failed", "Check DUDUSERVER network/login.");
        vTaskDelete(nullptr);
        return;
    }

    HttpResponse status_response;
    HttpResponse weather_response;
    HttpResponse config_response;
    bool status_ok = http_request(HTTP_METHOD_GET, DEVICE_STATUS_URL, token.c_str(), nullptr, status_response);
    bool weather_ok = http_request(HTTP_METHOD_GET, WEATHER_URL, token.c_str(), nullptr, weather_response);
    bool config_ok = http_request(HTTP_METHOD_GET, DEVICE_CONFIG_URL, token.c_str(), nullptr, config_response);

    cJSON *status_root = status_ok ? cJSON_Parse(status_response.body.c_str()) : nullptr;
    cJSON *weather_root = weather_ok ? cJSON_Parse(weather_response.body.c_str()) : nullptr;
    cJSON *config_root = config_ok ? cJSON_Parse(config_response.body.c_str()) : nullptr;

    const bool online = status_root != nullptr && json_bool(status_root, "online", false);
    const int age = status_root != nullptr ? json_int(status_root, "age_seconds", -1) : -1;
    const int rssi = status_root != nullptr ? json_int(status_root, "rssi", 0) : 0;
    const int heap = status_root != nullptr ? json_int(status_root, "heap_free_kb", 0) : 0;
    const int psram = status_root != nullptr ? json_int(status_root, "psram_free_kb", 0) : 0;
    const std::string firmware = status_root != nullptr ? json_string(status_root, "firmware", "-") : "-";
    const std::string ota_state = status_root != nullptr ? json_string(status_root, "ota_state", "-") : "-";
    const std::string ota_target = status_root != nullptr ? json_string(status_root, "ota_target", "-") : "-";
    const std::string reset = status_root != nullptr ? json_string(status_root, "reset_reason", "-") : "-";
    const int uptime_s = status_root != nullptr ? json_int(status_root, "uptime_ms", 0) / 1000 : 0;

    std::string city = "-";
    std::string weather_text = "-";
    std::string temp = "--C";
    std::string wind = "-";
    if (weather_root != nullptr) {
        city = json_string(weather_root, "city", "-");
        cJSON *current = cJSON_GetObjectItemCaseSensitive(weather_root, "current");
        if (cJSON_IsObject(current)) {
            weather_text = json_string(current, "weather_text", "-");
            temp = temp_text(current);
            char wind_text[32] = {};
            std::snprintf(wind_text, sizeof(wind_text), "%.1fkm/h", json_double(current, "wind_speed_kmh", 0.0));
            wind = wind_text;
        }
    }

    const std::string cloud_target = config_root != nullptr ? json_string(config_root, "firmware_version", "-") : "-";
    const bool ota_enabled = config_root != nullptr && json_bool(config_root, "ota_enabled", false);
    const int interval = config_root != nullptr ? json_int(config_root, "telemetry_report_interval_sec", 0) : 0;

    char summary[128] = {};
    std::snprintf(
        summary,
        sizeof(summary),
        "%s / %s / %s",
        online ? "Device online" : "Device offline",
        weather_ok ? "Weather OK" : "Weather ERR",
        config_ok ? "Config OK" : "Config ERR"
    );

    char detail[512] = {};
    std::snprintf(
        detail,
        sizeof(detail),
        "Device: %s age=%ds rssi=%d\nFW: %s -> %s %s\nOTA: %s target=%s\nMem: heap=%dKB psram=%dKB\nWeather: %s %s %s wind %s\nRun: %ds reset=%s",
        online ? "online" : "offline",
        age,
        rssi,
        firmware.c_str(),
        cloud_target.c_str(),
        ota_enabled ? "OTA on" : "OTA off",
        ota_state.c_str(),
        ota_target.c_str(),
        heap,
        psram,
        city.c_str(),
        weather_text.c_str(),
        temp.c_str(),
        wind.c_str(),
        uptime_s,
        reset.c_str()
    );
    if (interval > 0) {
        std::string with_interval = detail;
        char tail[48] = {};
        std::snprintf(tail, sizeof(tail), "\nReport: every %ds", interval);
        with_interval += tail;
        app->setCloudState(false, status_ok && weather_ok && config_ok, status_ok ? "Online" : "Partial", summary, with_interval.c_str());
    } else {
        app->setCloudState(false, status_ok && weather_ok && config_ok, status_ok ? "Online" : "Partial", summary, detail);
    }

    if (status_root != nullptr) cJSON_Delete(status_root);
    if (weather_root != nullptr) cJSON_Delete(weather_root);
    if (config_root != nullptr) cJSON_Delete(config_root);
    vTaskDelete(nullptr);
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, CloudApp, APP_NAME, []()
{
    return std::shared_ptr<CloudApp>(CloudApp::requestInstance(), [](CloudApp *) {});
})

} // namespace esp_brookesia::apps
