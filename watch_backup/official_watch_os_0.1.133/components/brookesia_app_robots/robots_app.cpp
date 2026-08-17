#include "robots_app.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "cJSON.h"
#include "esp_brookesia.hpp"
#include "esp_http_client.h"
#include "esp_lib_utils.h"
#include "freertos/idf_additions.h"
#include "lvgl.h"
#include "robot_image.hpp"
#include "watch_app_icons.hpp"
#include "watch_connectivity.hpp"
#include "watch_display.hpp"
#include "watch_fonts.hpp"
#include "watch_private_config.hpp"

namespace esp_brookesia::apps {
namespace {
constexpr const char *APP_NAME = "Robots";
constexpr const char *LOGIN_URL = WATCH_PRIVATE_DUDUSERVER_API_BASE_URL "/auth/login";
constexpr const char *ROBOT_STATUS_URL = WATCH_PRIVATE_DUDUSERVER_API_BASE_URL "/robot-status";
constexpr const char *USER_AGENT = "esp32-s3-watch-robots";
constexpr int SAFE_TOP = 24;
constexpr int SAFE_SIDE = 18;
constexpr size_t MAX_HTTP_BODY = 48 * 1024;
constexpr int ROBOT_IMAGE_SIZE = 186;
constexpr int ROBOT_IMAGE_SIDE_SIZE = 152;

struct HttpResponse {
    int status = 0;
    std::string body;
};

esp_err_t http_event(esp_http_client_event_t *event)
{
    auto *body = static_cast<std::string *>(event->user_data);
    if (event->event_id == HTTP_EVENT_ON_DATA && body && event->data && event->data_len > 0) {
        if (body->size() + static_cast<size_t>(event->data_len) > MAX_HTTP_BODY) return ESP_FAIL;
        body->append(static_cast<const char *>(event->data), event->data_len);
    }
    return ESP_OK;
}

bool http_request(esp_http_client_method_t method, const char *url, const char *token, HttpResponse &response)
{
    response = {};
    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = 12000;
    config.user_agent = USER_AGENT;
    config.event_handler = http_event;
    config.user_data = &response.body;
    config.keep_alive_enable = false;
    auto *client = esp_http_client_init(&config);
    if (!client) return false;
    esp_http_client_set_method(client, method);
    if (token && token[0]) {
        std::string auth = "Bearer ";
        auth += token;
        esp_http_client_set_header(client, "Authorization", auth.c_str());
    }
    const esp_err_t err = esp_http_client_perform(client);
    response.status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    return err == ESP_OK && response.status >= 200 && response.status < 300;
}

std::string login_token()
{
    HttpResponse response;
    esp_http_client_config_t config = {};
    config.url = LOGIN_URL;
    config.timeout_ms = 12000;
    config.user_agent = USER_AGENT;
    config.event_handler = http_event;
    config.user_data = &response.body;
    config.keep_alive_enable = false;
    auto *client = esp_http_client_init(&config);
    if (!client) return {};
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, WATCH_PRIVATE_LOGIN_BODY, std::strlen(WATCH_PRIVATE_LOGIN_BODY));
    const bool ok = esp_http_client_perform(client) == ESP_OK;
    response.status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (!ok || response.status < 200 || response.status >= 300) return {};
    cJSON *root = cJSON_Parse(response.body.c_str());
    cJSON *token = root ? cJSON_GetObjectItemCaseSensitive(root, "access_token") : nullptr;
    std::string value = cJSON_IsString(token) && token->valuestring ? token->valuestring : "";
    cJSON_Delete(root);
    return value;
}

std::string json_string(cJSON *root, const char *key, const char *fallback = "")
{
    cJSON *item = root ? cJSON_GetObjectItemCaseSensitive(root, key) : nullptr;
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : fallback;
}

double json_number(cJSON *root, const char *key, double fallback = -1.0)
{
    cJSON *item = root ? cJSON_GetObjectItemCaseSensitive(root, key) : nullptr;
    return cJSON_IsNumber(item) ? item->valuedouble : fallback;
}

bool json_bool(cJSON *root, const char *key)
{
    cJSON *item = root ? cJSON_GetObjectItemCaseSensitive(root, key) : nullptr;
    return cJSON_IsTrue(item) || (cJSON_IsNumber(item) && item->valueint != 0);
}

bool links_have_online(cJSON *robot)
{
    const char *keys[] = {"wireless", "wired"};
    for (const char *key: keys) {
        cJSON *links = robot ? cJSON_GetObjectItemCaseSensitive(robot, key) : nullptr;
        if (!cJSON_IsArray(links)) continue;
        cJSON *link = nullptr;
        cJSON_ArrayForEach(link, links) {
            if (!cJSON_IsObject(link)) continue;
            if (json_bool(link, "ssh_ok") || json_bool(link, "verified")) return true;
        }
    }
    return false;
}

void scan_motor_temperatures(cJSON *telemetry, float &min_temp, float &max_temp)
{
    cJSON *motors = telemetry ? cJSON_GetObjectItemCaseSensitive(telemetry, "motors") : nullptr;
    if (!cJSON_IsArray(motors)) return;
    cJSON *motor = nullptr;
    cJSON_ArrayForEach(motor, motors) {
        if (!cJSON_IsObject(motor)) continue;
        const double casing = json_number(
            motor, "temperature_casing",
            json_number(motor, "temp_casing", json_number(motor, "casing_temp", -1))
        );
        const double winding = json_number(
            motor, "temperature_winding",
            json_number(motor, "temp_winding", json_number(motor, "winding_temp", -1))
        );
        const double value = casing >= 0 ? casing : winding;
        if (value < 0) continue;
        if (min_temp < 0 || value < min_temp) min_temp = static_cast<float>(value);
        if (max_temp < 0 || value > max_temp) max_temp = static_cast<float>(value);
    }
}

lv_obj_t *label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color)
{
    auto *object = lv_label_create(parent);
    lv_label_set_text(object, text);
    watch_display::apply_text_font(object, text, font);
    lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
    return object;
}

void style_button(lv_obj_t *button, uint32_t color)
{
    lv_obj_set_style_radius(button, 18, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
}

lv_obj_t *metric_line(lv_obj_t *parent, const char *key, const char *value, uint32_t value_color)
{
    auto *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 34);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(row, 0, 0);
    lv_obj_set_style_pad_right(row, 0, 0);

    auto *key_label = label(row, key, &lv_font_montserrat_12, 0x8793A7);
    lv_obj_set_width(key_label, 82);
    lv_label_set_long_mode(key_label, LV_LABEL_LONG_DOT);

    auto *value_label = label(row, value, &lv_font_montserrat_18, value_color);
    lv_obj_set_width(value_label, 92);
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_DOT);
    return row;
}
} // namespace

RobotsApp *RobotsApp::_instance = nullptr;

RobotsApp *RobotsApp::requestInstance()
{
    if (!_instance) _instance = new RobotsApp();
    return _instance;
}

RobotsApp::RobotsApp(): systems::phone::App(APP_NAME, watch_app_icon_robots_48(), true, true, true)
{
    _mutex = xSemaphoreCreateMutex();
}

bool RobotsApp::run()
{
    lv_obj_t *screen = lv_scr_act();
    ESP_UTILS_CHECK_NULL_RETURN(screen, false, "Invalid screen");
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x07080B), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    _root = lv_obj_create(screen);
    lv_obj_remove_style_all(_root);
    lv_obj_set_size(_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_left(_root, SAFE_SIDE, 0);
    lv_obj_set_style_pad_right(_root, SAFE_SIDE, 0);
    lv_obj_set_style_pad_top(_root, SAFE_TOP, 0);
    lv_obj_set_style_pad_bottom(_root, 18, 0);
    lv_obj_set_flex_flow(_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(_root, 8, 0);

    auto *top = lv_obj_create(_root);
    lv_obj_remove_style_all(top);
    lv_obj_set_size(top, LV_PCT(100), 44);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    label(top, "Robots", &lv_font_montserrat_28, 0xFFFFFF);
    auto *actions = lv_obj_create(top);
    lv_obj_remove_style_all(actions);
    lv_obj_set_size(actions, 140, 40);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(actions, 6, 0);
    auto *sync = lv_button_create(actions);
    lv_obj_set_size(sync, 58, 36);
    style_button(sync, 0x1B6BFF);
    lv_obj_add_event_cb(sync, onRefresh, LV_EVENT_CLICKED, this);
    lv_obj_center(label(sync, "Sync", &lv_font_montserrat_14, 0xFFFFFF));
    auto *back = lv_button_create(actions);
    lv_obj_set_size(back, 70, 36);
    style_button(back, 0x232833);
    lv_obj_add_event_cb(back, onBack, LV_EVENT_CLICKED, this);
    lv_obj_center(label(back, "Back", &lv_font_montserrat_14, 0xFFFFFF));

    _status_label = label(_root, _status, &lv_font_montserrat_14, 0x8FA3B8);
    lv_obj_set_width(_status_label, LV_PCT(100));
    lv_label_set_long_mode(_status_label, LV_LABEL_LONG_DOT);

    _list = lv_obj_create(_root);
    lv_obj_remove_style_all(_list);
    lv_obj_set_width(_list, LV_PCT(100));
    lv_obj_set_flex_grow(_list, 1);
    lv_obj_set_flex_flow(_list, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(_list, 0, 0);
    lv_obj_set_scroll_dir(_list, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(_list, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(_list, LV_SCROLLBAR_MODE_OFF);

    _timer = lv_timer_create(onTimer, 500, this);
    rebuildCards();
    startRefresh();
    return true;
}

bool RobotsApp::back()
{
    if (_timer) {
        lv_timer_delete(_timer);
        _timer = nullptr;
    }
    _root = _status_label = _list = nullptr;
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Close failed");
    return true;
}

void RobotsApp::onBack(lv_event_t *event)
{
    if (auto *app = static_cast<RobotsApp *>(lv_event_get_user_data(event))) app->back();
}

void RobotsApp::onRefresh(lv_event_t *event)
{
    if (auto *app = static_cast<RobotsApp *>(lv_event_get_user_data(event))) app->startRefresh();
}

void RobotsApp::onTimer(lv_timer_t *timer)
{
    if (auto *app = static_cast<RobotsApp *>(lv_timer_get_user_data(timer))) app->refreshUi();
}

void RobotsApp::startRefresh()
{
    if (_running) return;
    if (!watch::wifi_is_connected()) {
        setStatus("WiFi not connected");
        return;
    }
    _running = true;
    setStatus("Loading robots...");
    if (xTaskCreateWithCaps(taskEntry, "robots_app", 12288, this, 2, &_task, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        _running = false;
        _task = nullptr;
        setStatus("Task create failed");
    }
}

void RobotsApp::setStatus(const char *status)
{
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(30)) == pdTRUE) {
        std::snprintf(_status, sizeof(_status), "%s", status ? status : "");
        _dirty = true;
        xSemaphoreGive(_mutex);
    }
}

void RobotsApp::setRobots(const RobotSummary *robots, int count, const char *updated_at)
{
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        _robot_count = std::max(0, std::min(count, 2));
        for (int i = 0; i < _robot_count; ++i) _robots[i] = robots[i];
        std::snprintf(_updated_at, sizeof(_updated_at), "%s", updated_at ? updated_at : "");
        _running = false;
        _dirty = true;
        xSemaphoreGive(_mutex);
    }
}

void RobotsApp::taskEntry(void *arg)
{
    auto *app = static_cast<RobotsApp *>(arg);
    if (!app) {
        vTaskDeleteWithCaps(nullptr);
        return;
    }
    RobotSummary robots[2] = {};
    int count = 0;
    char status[128] = {};
    char updated[80] = {};
    std::string token = login_token();
    if (token.empty()) {
        std::snprintf(status, sizeof(status), "%s", "Cloud login failed");
    } else {
        HttpResponse response;
        if (!http_request(HTTP_METHOD_GET, ROBOT_STATUS_URL, token.c_str(), response)) {
            std::snprintf(status, sizeof(status), "Cloud fetch failed %d", response.status);
        } else {
            cJSON *root = cJSON_Parse(response.body.c_str());
            cJSON *raw = root ? cJSON_GetObjectItemCaseSensitive(root, "raw") : nullptr;
            cJSON *items = raw ? cJSON_GetObjectItemCaseSensitive(raw, "robots") : nullptr;
            std::snprintf(updated, sizeof(updated), "%s", json_string(root, "updated_at", "").c_str());
            if (!cJSON_IsArray(items)) {
                std::snprintf(status, sizeof(status), "%s", "No robot data synced");
            } else {
                const int n = std::min(cJSON_GetArraySize(items), 2);
                for (int i = 0; i < n; ++i) {
                    cJSON *item = cJSON_GetArrayItem(items, i);
                    if (!cJSON_IsObject(item)) continue;
                    cJSON *identity = cJSON_GetObjectItemCaseSensitive(item, "identity");
                    cJSON *telemetry = cJSON_GetObjectItemCaseSensitive(item, "telemetry");
                    auto &r = robots[count++];
                    std::snprintf(r.name, sizeof(r.name), "%s", json_string(item, "name", json_string(item, "id", "Robot").c_str()).c_str());
                    r.online = json_bool(item, "watch_online") || json_bool(item, "matched") ||
                               json_bool(identity, "verified") || json_bool(telemetry, "lowstate_ok") ||
                               links_have_online(item);
                    const double soc = json_number(telemetry, "battery_soc", -1);
                    if (soc >= 0) r.battery = static_cast<int>(soc + 0.5);
                    r.temp_max = static_cast<float>(json_number(telemetry, "max_joint_temp_casing", -1));
                    scan_motor_temperatures(telemetry, r.temp_min, r.temp_max);
                    const double errors = json_number(telemetry, "motor_error_count", -1);
                    if (errors >= 0) r.motor_errors = static_cast<int>(errors);
                }
                std::snprintf(status, sizeof(status), "%s", json_string(root, "summary", count ? "Robot data ready" : "No robots").c_str());
            }
            cJSON_Delete(root);
        }
    }
    app->setStatus(status);
    app->setRobots(robots, count, updated);
    app->_task = nullptr;
    vTaskDeleteWithCaps(nullptr);
}

void RobotsApp::refreshUi()
{
    bool dirty = false;
    if (_mutex && xSemaphoreTake(_mutex, 0) == pdTRUE) {
        dirty = _dirty;
        _dirty = false;
        xSemaphoreGive(_mutex);
    }
    if (!dirty) return;
    if (_status_label && lv_obj_is_valid(_status_label)) {
        watch_display::set_label_text(_status_label, _status, &lv_font_montserrat_14);
    }
    rebuildCards();
}

void RobotsApp::rebuildCards()
{
    if (!_list || !lv_obj_is_valid(_list)) return;
    lv_obj_clean(_list);
    for (int i = 0; i < _robot_count; ++i) {
        const auto &r = _robots[i];
        auto *page = lv_obj_create(_list);
        lv_obj_remove_style_all(page);
        lv_obj_set_width(page, LV_PCT(100));
        lv_obj_set_height(page, LV_PCT(100));
        lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_style_pad_left(page, 6, 0);
        lv_obj_set_style_pad_right(page, 6, 0);
        lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_top(page, 2, 0);
        lv_obj_set_style_pad_bottom(page, 2, 0);
        lv_obj_set_style_pad_row(page, 8, 0);
        lv_obj_set_flex_align(page, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        auto *panel = lv_obj_create(page);
        lv_obj_remove_style_all(panel);
        lv_obj_set_width(panel, LV_PCT(100));
        lv_obj_set_height(panel, 310);
        lv_obj_set_style_radius(panel, 30, 0);
        lv_obj_set_style_bg_color(panel, lv_color_hex(0x0F1420), 0);
        lv_obj_set_style_bg_grad_color(panel, lv_color_hex(0x18243A), 0);
        lv_obj_set_style_bg_grad_dir(panel, LV_GRAD_DIR_HOR, 0);
        lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
        lv_obj_set_style_clip_corner(panel, true, 0);
        lv_obj_set_style_pad_left(panel, 12, 0);
        lv_obj_set_style_pad_right(panel, 12, 0);
        lv_obj_set_style_pad_top(panel, 14, 0);
        lv_obj_set_style_pad_bottom(panel, 14, 0);
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(panel, 10, 0);

        auto *img_box = lv_obj_create(panel);
        lv_obj_remove_style_all(img_box);
        lv_obj_set_size(img_box, ROBOT_IMAGE_SIDE_SIZE, 250);
        lv_obj_set_flex_flow(img_box, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(img_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        auto *img = lv_image_create(img_box);
        lv_image_set_src(img, &watch_robot_g1_cutout_186);
        lv_image_set_scale(img, 212);
        lv_obj_center(img);

        auto *info = lv_obj_create(panel);
        lv_obj_remove_style_all(info);
        lv_obj_set_width(info, LV_PCT(100));
        lv_obj_set_height(info, 250);
        lv_obj_set_flex_grow(info, 1);
        lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(info, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_row(info, 8, 0);

        auto *name_row = lv_obj_create(info);
        lv_obj_remove_style_all(name_row);
        lv_obj_set_width(name_row, LV_PCT(100));
        lv_obj_set_height(name_row, 34);
        lv_obj_set_flex_flow(name_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(name_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        auto *name_label = label(name_row, r.name, &lv_font_watch_zh_14, 0xFFFFFF);
        lv_obj_set_width(name_label, 138);
        lv_label_set_long_mode(name_label, LV_LABEL_LONG_DOT);
        auto *dot = lv_obj_create(name_row);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 10, 10);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(r.online ? 0x72E39B : 0xFF5A66), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);

        char line[64] = {};
        metric_line(info, "STATUS", r.online ? "ONLINE" : "OFFLINE", r.online ? 0x72E39B : 0xFF6B6B);
        if (r.battery >= 0) std::snprintf(line, sizeof(line), "%d%%", r.battery);
        else std::snprintf(line, sizeof(line), "--");
        metric_line(info, "BATTERY", line, 0xFFFFFF);

        if (r.temp_min >= 0 && r.temp_max >= 0 && r.temp_min != r.temp_max) {
            std::snprintf(line, sizeof(line), "%.0f-%.0fC", r.temp_min, r.temp_max);
        } else if (r.temp_max >= 0) {
            std::snprintf(line, sizeof(line), "%.0fC", r.temp_max);
        } else {
            std::snprintf(line, sizeof(line), "--");
        }
        metric_line(info, "JOINT", line, 0xFFD166);
        if (r.motor_errors >= 0) std::snprintf(line, sizeof(line), "%d", r.motor_errors);
        else std::snprintf(line, sizeof(line), "--");
        metric_line(info, "ERRORS", line, r.motor_errors > 0 ? 0xFF6B6B : 0xFFFFFF);

        char page_hint[20] = {};
        std::snprintf(page_hint, sizeof(page_hint), "%d / %d", i + 1, _robot_count);
        label(page, page_hint, &lv_font_montserrat_12, 0x66758A);
    }
    if (_robot_count == 0) {
        auto *empty = label(_list, "No robot data. Run local sync first.", &lv_font_montserrat_16, 0xAEB7C6);
        lv_obj_set_width(empty, LV_PCT(100));
        lv_label_set_long_mode(empty, LV_LABEL_LONG_WRAP);
    }
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, RobotsApp, APP_NAME, []()
{
    return std::shared_ptr<RobotsApp>(RobotsApp::requestInstance(), [](RobotsApp *) {});
});

} // namespace esp_brookesia::apps
