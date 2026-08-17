#include "cloud_app.hpp"

#include <algorithm>
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
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "watch_fonts.hpp"
#include "watch_app_icons.hpp"
#include "watch_connectivity.hpp"
#include "watch_private_config.hpp"

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "CloudApp"

namespace esp_brookesia::apps {
namespace {

constexpr const char *APP_NAME = "Cloud";
constexpr int SAFE_TOP = 24;
constexpr int SAFE_SIDE = 22;
constexpr const char *LOGIN_URL = WATCH_PRIVATE_DUDUSERVER_API_BASE_URL "/auth/login";
constexpr const char *DEVICE_STATUS_URL = WATCH_PRIVATE_DUDUSERVER_API_BASE_URL "/device-status";
constexpr const char *DEVICE_CONFIG_URL = WATCH_PRIVATE_DUDUSERVER_API_BASE_URL "/device-config";
constexpr const char *WEATHER_URL = WATCH_PRIVATE_DUDUSERVER_API_BASE_URL "/weather?compact=true";
constexpr const char *SERVER_STATUS_URL = WATCH_PRIVATE_DUDUSERVER_API_BASE_URL "/server-status";
constexpr const char *USERNAME = WATCH_PRIVATE_USERNAME;
constexpr const char *PASSWORD = WATCH_PRIVATE_PASSWORD;
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

void CloudApp::createMetricWidgets(lv_obj_t *parent)
{
    static const char *const names[METRIC_COUNT] = {"CPU", "MEM", "DISK", "GPU"};
    static const uint32_t colors[METRIC_COUNT] = {0x4DA3FF, 0x47E0C0, 0xFFB84D, 0xB48CFF};

    lv_obj_remove_style_all(parent);
    lv_obj_set_width(parent, LV_PCT(100));
    lv_obj_set_height(parent, 114);
    lv_obj_set_style_radius(parent, 24, 0);
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x15171D), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(parent, 1, 0);
    lv_obj_set_style_border_color(parent, lv_color_hex(0x2B303A), 0);
    lv_obj_set_style_pad_left(parent, 8, 0);
    lv_obj_set_style_pad_right(parent, 8, 0);
    lv_obj_set_style_pad_top(parent, 7, 0);
    lv_obj_set_style_pad_bottom(parent, 5, 0);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    for (int index = 0; index < METRIC_COUNT; ++index) {
        lv_obj_t *item = lv_obj_create(parent);
        lv_obj_remove_style_all(item);
        lv_obj_set_height(item, 100);
        lv_obj_set_flex_grow(item, 1);

        lv_obj_t *arc = lv_arc_create(item);
        lv_obj_set_size(arc, 72, 72);
        lv_obj_align(arc, LV_ALIGN_TOP_MID, 0, 0);
        lv_arc_set_range(arc, 0, 100);
        lv_arc_set_rotation(arc, 0);
        lv_arc_set_bg_angles(arc, 135, 405);
        lv_arc_set_value(arc, 0);
        lv_obj_set_style_arc_width(arc, 7, LV_PART_MAIN);
        lv_obj_set_style_arc_width(arc, 7, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(arc, lv_color_hex(0x2B303A), LV_PART_MAIN);
        lv_obj_set_style_arc_color(arc, lv_color_hex(colors[index]), LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);

        lv_obj_t *value = make_label(item, "--", &lv_font_montserrat_16, 0xFFFFFF);
        lv_obj_set_width(value, LV_SIZE_CONTENT);
        lv_obj_align(value, LV_ALIGN_TOP_MID, 0, 26);

        lv_obj_t *name = make_label(item, names[index], &lv_font_montserrat_12, 0xAEB7C6);
        lv_obj_set_width(name, LV_PCT(100));
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, 0);

        _metric_arcs[index] = arc;
        _metric_value_labels[index] = value;
    }
}

void CloudApp::setMetricValues(int cpu, int memory, int disk, int gpu)
{
    if (_mutex != nullptr && xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        _metric_values[0] = cpu;
        _metric_values[1] = memory;
        _metric_values[2] = disk;
        _metric_values[3] = gpu;
        xSemaphoreGive(_mutex);
    }
}

namespace {

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
    const char *body = WATCH_PRIVATE_LOGIN_BODY;
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
    lv_obj_set_style_pad_row(root, 8, 0);
    lv_obj_set_scrollbar_mode(root, LV_SCROLLBAR_MODE_AUTO);

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

    lv_obj_t *status_card = make_card(root, 126);
    _status_label = make_label(status_card, "Idle", &lv_font_montserrat_36, 0xFFFFFF);
    lv_obj_set_width(_status_label, LV_PCT(100));
    lv_obj_set_style_text_align(_status_label, LV_TEXT_ALIGN_CENTER, 0);

    _summary_label = make_label(status_card, "Tap Refresh", &lv_font_montserrat_18, 0x69D2FF);
    lv_obj_set_width(_summary_label, LV_PCT(100));
    lv_obj_set_style_text_align(_summary_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(_summary_label, LV_LABEL_LONG_WRAP);

    lv_obj_t *metrics_card = make_card(root, 114);
    createMetricWidgets(metrics_card);

    lv_obj_t *detail_card = make_card(root, 300);
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
    for (int index = 0; index < METRIC_COUNT; ++index) {
        _metric_arcs[index] = nullptr;
        _metric_value_labels[index] = nullptr;
    }
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
        _detail = "Login and fetch server/device/weather/config status...";
        xSemaphoreGive(_mutex);
    }
    setMetricValues(-1, -1, -1, -1);
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
    int metric_values[METRIC_COUNT] = {-1, -1, -1, -1};
    if (_mutex != nullptr && xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        status = _status;
        summary = _summary;
        detail = _detail;
        running = _running;
        ok = _ok;
        last_us = _last_refresh_us;
        for (int index = 0; index < METRIC_COUNT; ++index) {
            metric_values[index] = _metric_values[index];
        }
        xSemaphoreGive(_mutex);
    }
    if (_status_label != nullptr) {
        watch_display::set_label_text(_status_label, status.c_str(), &lv_font_montserrat_14);
        lv_obj_set_style_text_color(_status_label, lv_color_hex(ok ? 0x23D18B : (running ? 0xFFCC33 : 0xFFFFFF)), 0);
    }
    if (_summary_label != nullptr) {
        watch_display::set_label_text(_summary_label, summary.c_str(), &lv_font_montserrat_14);
    }
    if (_detail_label != nullptr) {
        watch_display::set_label_text(_detail_label, detail.c_str(), &lv_font_montserrat_12);
    }
    if (_refresh_label != nullptr) {
        const bool recent = last_us > 0 && (esp_timer_get_time() - last_us) < 3000000;
        lv_label_set_text(_refresh_label, running ? "Loading..." : (recent ? (ok ? "OK" : "Retry") : "Refresh"));
    }
    for (int index = 0; index < METRIC_COUNT; ++index) {
        const int value = metric_values[index];
        if (_metric_arcs[index] != nullptr) {
            lv_arc_set_value(_metric_arcs[index], value >= 0 ? std::min(value, 100) : 0);
            lv_obj_set_style_arc_opa(_metric_arcs[index], value >= 0 ? LV_OPA_COVER : LV_OPA_30, LV_PART_INDICATOR);
        }
        if (_metric_value_labels[index] != nullptr) {
            char text[16] = {};
            if (value >= 0) {
                std::snprintf(text, sizeof(text), "%d%%", std::min(value, 100));
            } else {
                std::snprintf(text, sizeof(text), "--");
            }
            watch_display::set_label_text(_metric_value_labels[index], text, &lv_font_montserrat_16);
        }
    }
}

void CloudApp::cloudTask(void *arg)
{
    auto *app = static_cast<CloudApp *>(arg);
    if (app == nullptr) {
        vTaskDeleteWithCaps(nullptr);
        return;
    }

    std::string token = login_token();
    if (token.empty()) {
        app->setCloudState(false, false, "Error", "Login failed", "Check DUDUSERVER network/login.");
        vTaskDeleteWithCaps(nullptr);
        return;
    }

    HttpResponse status_response;
    HttpResponse weather_response;
    HttpResponse config_response;
    HttpResponse server_response;
    bool status_ok = http_request(HTTP_METHOD_GET, DEVICE_STATUS_URL, token.c_str(), nullptr, status_response);
    bool weather_ok = http_request(HTTP_METHOD_GET, WEATHER_URL, token.c_str(), nullptr, weather_response);
    bool config_ok = http_request(HTTP_METHOD_GET, DEVICE_CONFIG_URL, token.c_str(), nullptr, config_response);
    bool server_ok = http_request(HTTP_METHOD_GET, SERVER_STATUS_URL, token.c_str(), nullptr, server_response);

    cJSON *status_root = status_ok ? cJSON_Parse(status_response.body.c_str()) : nullptr;
    cJSON *weather_root = weather_ok ? cJSON_Parse(weather_response.body.c_str()) : nullptr;
    cJSON *config_root = config_ok ? cJSON_Parse(config_response.body.c_str()) : nullptr;
    cJSON *server_root = server_ok ? cJSON_Parse(server_response.body.c_str()) : nullptr;

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

    int server_cpu = -1;
    int server_cores = -1;
    double server_load = -1;
    double server_memory_used = -1;
    double server_memory_total = -1;
    double server_memory_percent = -1;
    double server_disk_used = -1;
    double server_disk_total = -1;
    double server_disk_percent = -1;
    int server_gpu = -1;
    int server_uptime_s = -1;
    std::string gpu_line = "GPU: unavailable";
    std::string service_line = "Services: unavailable";
    if (server_root != nullptr) {
        cJSON *cpu = cJSON_GetObjectItemCaseSensitive(server_root, "cpu");
        cJSON *memory = cJSON_GetObjectItemCaseSensitive(server_root, "memory");
        cJSON *disk = cJSON_GetObjectItemCaseSensitive(server_root, "disk");
        cJSON *gpu = cJSON_GetObjectItemCaseSensitive(server_root, "gpu");
        server_cpu = cpu != nullptr ? json_int(cpu, "usage_percent", -1) : -1;
        server_cores = cpu != nullptr ? json_int(cpu, "cores", -1) : -1;
        server_load = json_double(server_root, "load_1m", -1);
        server_uptime_s = json_int(server_root, "uptime_seconds", -1);
        server_memory_used = memory != nullptr ? json_double(memory, "used_mb", -1) : -1;
        server_memory_total = memory != nullptr ? json_double(memory, "total_mb", -1) : -1;
        server_memory_percent = memory != nullptr ? json_double(memory, "usage_percent", -1) : -1;
        server_disk_used = disk != nullptr ? json_double(disk, "used_gb", -1) : -1;
        server_disk_total = disk != nullptr ? json_double(disk, "total_gb", -1) : -1;
        server_disk_percent = disk != nullptr ? json_double(disk, "usage_percent", -1) : -1;

        if (gpu != nullptr && json_bool(gpu, "available", false)) {
            server_gpu = json_int(gpu, "utilization_percent", -1);
            std::string gpu_name = json_string(gpu, "name", "GPU");
            if (gpu_name.size() > 18) {
                gpu_name.resize(18);
            }
            char gpu_text[160] = {};
            std::snprintf(
                gpu_text,
                sizeof(gpu_text),
                "GPU: %s %.0f%% %.0f/%.0fMB %.0fC",
                gpu_name.c_str(),
                json_double(gpu, "utilization_percent", -1),
                json_double(gpu, "memory_used_mb", -1),
                json_double(gpu, "memory_total_mb", -1),
                json_double(gpu, "temperature_c", -1)
            );
            gpu_line = gpu_text;
        }

        cJSON *services = cJSON_GetObjectItemCaseSensitive(server_root, "services");
        if (cJSON_IsArray(services)) {
            service_line = "Services:";
            int service_count = 0;
            cJSON *service = nullptr;
            cJSON_ArrayForEach(service, services) {
                if (service_count >= 4 || !cJSON_IsObject(service)) {
                    break;
                }
                const std::string name = json_string(service, "name", "?");
                const std::string state = json_string(service, "state", "unknown");
                service_line += service_count == 0 ? " " : " | ";
                service_line += name;
                service_line += ":";
                service_line += state;
                ++service_count;
            }
            if (service_count == 0) {
                service_line = "Services: unavailable";
            }
        }
    }
    app->setMetricValues(
        server_cpu,
        server_memory_percent >= 0 ? static_cast<int>(server_memory_percent) : -1,
        server_disk_percent >= 0 ? static_cast<int>(server_disk_percent) : -1,
        server_gpu
    );

    char summary[128] = {};
    std::snprintf(
        summary,
        sizeof(summary),
        "%s / %s / %s / %s",
        server_ok ? "Server OK" : "Server ERR",
        online ? "Device online" : "Device offline",
        weather_ok ? "Weather OK" : "Weather ERR",
        config_ok ? "Config OK" : "Config ERR"
    );

    char server_line[192] = {};
    std::snprintf(
        server_line,
        sizeof(server_line),
        "Server: CPU %s%% %sc load %s\nMemory: %s/%sMB %s%%\nDisk: %s/%sGB %s%%",
        server_cpu >= 0 ? std::to_string(server_cpu).c_str() : "--",
        server_cores >= 0 ? std::to_string(server_cores).c_str() : "--",
        server_load >= 0 ? std::to_string(server_load).c_str() : "--",
        server_memory_used >= 0 ? std::to_string(static_cast<int>(server_memory_used)).c_str() : "--",
        server_memory_total >= 0 ? std::to_string(static_cast<int>(server_memory_total)).c_str() : "--",
        server_memory_percent >= 0 ? std::to_string(static_cast<int>(server_memory_percent)).c_str() : "--",
        server_disk_used >= 0 ? std::to_string(server_disk_used).c_str() : "--",
        server_disk_total >= 0 ? std::to_string(server_disk_total).c_str() : "--",
        server_disk_percent >= 0 ? std::to_string(static_cast<int>(server_disk_percent)).c_str() : "--"
    );
    std::string detail = server_line;
    detail += "\n";
    detail += gpu_line;
    detail += "\n";
    detail += service_line;
    if (server_uptime_s >= 0) {
        char uptime_text[48] = {};
        std::snprintf(uptime_text, sizeof(uptime_text), "\nServer up: %ds", server_uptime_s);
        detail += uptime_text;
    }
    char device_detail[640] = {};
    std::snprintf(
        device_detail,
        sizeof(device_detail),
        "\nDevice: %s age=%ds rssi=%d\nFW: %s -> %s %s\nOTA: %s target=%s\nWatch mem: heap=%dKB psram=%dKB\nWeather: %s %s %s wind %s\nRun: %ds reset=%s",
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
    detail += device_detail;
    if (interval > 0) {
        char tail[48] = {};
        std::snprintf(tail, sizeof(tail), "\nReport: every %ds", interval);
        detail += tail;
    }
    app->setCloudState(
        false,
        status_ok && server_ok && weather_ok && config_ok,
        status_ok && server_ok ? "Online" : "Partial",
        summary,
        detail.c_str()
    );

    if (status_root != nullptr) cJSON_Delete(status_root);
    if (weather_root != nullptr) cJSON_Delete(weather_root);
    if (config_root != nullptr) cJSON_Delete(config_root);
    if (server_root != nullptr) cJSON_Delete(server_root);
    vTaskDeleteWithCaps(nullptr);
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, CloudApp, APP_NAME, []()
{
    return std::shared_ptr<CloudApp>(CloudApp::requestInstance(), [](CloudApp *) {});
})

} // namespace esp_brookesia::apps
