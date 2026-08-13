#include "ota_app.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "bsp/esp-bsp.h"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_brookesia.hpp"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_lib_utils.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/idf_additions.h"
#include "lvgl.h"
#include "watch_app_icons.hpp"
#include "watch_connectivity.hpp"
#include "watch_private_config.hpp"

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "OtaApp"

namespace esp_brookesia::apps {
namespace {
constexpr const char *APP_NAME = "OTA";
constexpr int SAFE_TOP = 24;
constexpr int SAFE_SIDE = 22;
constexpr const char *PROJECT_ID = "esp32-s3-watch";
constexpr const char *HW_MODEL = "waveshare-esp32-s3-touch-amoled-2.06";
constexpr const char *LOGIN_URL = WATCH_PRIVATE_DUDUSERVER_API_BASE_URL "/auth/login";
constexpr const char *CONFIG_URL = WATCH_PRIVATE_DUDUSERVER_API_BASE_URL "/device-config";
constexpr const char *STATUS_URL = WATCH_PRIVATE_DUDUSERVER_API_BASE_URL "/device-status";
constexpr const char *USERNAME = WATCH_PRIVATE_USERNAME;
constexpr const char *PASSWORD = WATCH_PRIVATE_PASSWORD;
constexpr const char *USER_AGENT = "esp32-s3-watch-ota";
constexpr int HTTP_TIMEOUT_MS = 15000;
constexpr size_t MAX_HTTP_BODY = 32 * 1024;
constexpr size_t OTA_BUFFER_SIZE = 4 * 1024;
constexpr uint32_t OTA_TASK_STACK_SIZE = 20 * 1024;
constexpr uint32_t OTA_UI_TIMER_IDLE_MS = 500;
constexpr uint32_t OTA_UI_TIMER_BUSY_MS = 250;
constexpr int OTA_PROGRESS_UI_STEP = 2;
constexpr uint32_t OTA_STARTUP_REPORT_TASK_STACK_SIZE = 8 * 1024;
constexpr size_t APP_DESC_OFFSET = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);
constexpr size_t APP_DESC_HEADER_BYTES = APP_DESC_OFFSET + sizeof(esp_app_desc_t);

struct HttpResponse {
    int status = 0;
    std::string body;
};

struct TaskRequest {
    OtaApp *app = nullptr;
    OtaApp::Operation operation = OtaApp::Operation::Check;
};

lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
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
    lv_obj_set_style_pad_all(card, 14, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 7, 0);
    return card;
}

void style_button(lv_obj_t *button, uint32_t bg)
{
    lv_obj_set_style_radius(button, 24, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
}

const char *current_version()
{
    const esp_app_desc_t *desc = esp_app_get_description();
    return (desc != nullptr && desc->version[0] != '\0') ? desc->version : "unknown";
}

void mark_running_app_valid()
{
    const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err != ESP_OK) {
        ESP_LOGW(ESP_UTILS_LOG_TAG, "mark running app valid skipped/failed: %s", esp_err_to_name(err));
    }
}

std::string json_escape(const char *text)
{
    std::string out;
    if (text == nullptr) {
        return out;
    }
    for (const char *p = text; *p != '\0'; ++p) {
        if (*p == '\\' || *p == '"') {
            out.push_back('\\');
        }
        if (*p == '\n') {
            out += "\\n";
        } else if (*p == '\r') {
            out += "\\r";
        } else {
            out.push_back(*p);
        }
    }
    return out;
}

std::string device_id()
{
    uint8_t mac[6] = {};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
        return "watch-unknown";
    }
    char id[32] = {};
    std::snprintf(id, sizeof(id), "watch-%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return id;
}

std::string partition_label(const esp_partition_t *partition)
{
    return partition != nullptr ? partition->label : "unknown";
}

std::string partition_summary()
{
    return "Run " + partition_label(esp_ota_get_running_partition()) +
           " / Boot " + partition_label(esp_ota_get_boot_partition()) +
           " / Next " + partition_label(esp_ota_get_next_update_partition(nullptr));
}

std::array<int, 8> parse_version(const char *version)
{
    std::array<int, 8> out = {};
    size_t idx = 0;
    int value = 0;
    bool in_num = false;
    for (const char *p = version; p != nullptr && *p != '\0' && idx < out.size(); ++p) {
        if (std::isdigit(static_cast<unsigned char>(*p))) {
            value = value * 10 + (*p - '0');
            in_num = true;
        } else if (in_num) {
            out[idx++] = value;
            value = 0;
            in_num = false;
        }
    }
    if (in_num && idx < out.size()) {
        out[idx] = value;
    }
    return out;
}

bool version_newer(const char *remote, const char *local)
{
    auto r = parse_version(remote);
    auto l = parse_version(local);
    for (size_t i = 0; i < r.size(); ++i) {
        if (r[i] > l[i]) return true;
        if (r[i] < l[i]) return false;
    }
    return false;
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

bool json_copy_string(cJSON *object, const char *key, char *out, size_t out_size)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsString(item) || item->valuestring == nullptr) {
        return false;
    }
    std::snprintf(out, out_size, "%s", item->valuestring);
    return true;
}

bool parse_token(const std::string &body, char *token, size_t token_size)
{
    cJSON *root = cJSON_ParseWithLength(body.data(), body.size());
    if (root == nullptr) {
        return false;
    }
    bool ok = json_copy_string(root, "access_token", token, token_size);
    cJSON_Delete(root);
    return ok;
}

bool parse_config(const std::string &body, OtaApp::OtaConfig &config)
{
    cJSON *root = cJSON_ParseWithLength(body.data(), body.size());
    if (root == nullptr) {
        return false;
    }
    cJSON *enabled = cJSON_GetObjectItemCaseSensitive(root, "ota_enabled");
    config.enabled = cJSON_IsTrue(enabled);
    json_copy_string(root, "firmware_version", config.version, sizeof(config.version));
    json_copy_string(root, "firmware_url", config.url, sizeof(config.url));
    json_copy_string(root, "ota_log", config.log, sizeof(config.log));
    cJSON_Delete(root);
    return true;
}

bool login(char *token, size_t token_size)
{
    std::string body = "{\"username\":\"";
    body += json_escape(USERNAME);
    body += "\",\"password\":\"";
    body += json_escape(PASSWORD);
    body += "\"}";
    HttpResponse response;
    return http_request(HTTP_METHOD_POST, LOGIN_URL, "", body.c_str(), response) && parse_token(response.body, token, token_size);
}

bool fetch_config(const char *token, OtaApp::OtaConfig &config, int *status_code)
{
    HttpResponse response;
    bool ok = http_request(HTTP_METHOD_GET, CONFIG_URL, token, "", response);
    if (status_code != nullptr) {
        *status_code = response.status;
    }
    return ok && parse_config(response.body, config);
}

bool report_status(const char *token, const char *state, const char *target, const char *partition = "", int progress = -1, int64_t got = -1, int64_t total = -1)
{
    if (token == nullptr || token[0] == '\0') {
        return false;
    }
    std::string body = "{";
    body += "\"project\":\"" + std::string(PROJECT_ID) + "\",";
    body += "\"device_id\":\"" + json_escape(device_id().c_str()) + "\",";
    body += "\"hw_model\":\"" + std::string(HW_MODEL) + "\",";
    body += "\"firmware\":\"" + json_escape(current_version()) + "\",";
    body += "\"ota_state\":\"" + json_escape(state) + "\",";
    body += "\"ota_target\":\"" + json_escape(target) + "\",";
    body += "\"ota_partition\":\"" + json_escape((partition != nullptr && partition[0] != '\0') ? partition : partition_label(esp_ota_get_running_partition()).c_str()) + "\",";
    if (progress >= 0) body += "\"ota_progress\":" + std::to_string(std::clamp(progress, 0, 100)) + ",";
    if (got >= 0) body += "\"ota_bytes_received\":" + std::to_string(got) + ",";
    if (total >= 0) body += "\"ota_bytes_total\":" + std::to_string(total) + ",";
    body += "\"running_partition\":\"" + json_escape(partition_label(esp_ota_get_running_partition()).c_str()) + "\",";
    body += "\"boot_partition\":\"" + json_escape(partition_label(esp_ota_get_boot_partition()).c_str()) + "\",";
    body += "\"next_partition\":\"" + json_escape(partition_label(esp_ota_get_next_update_partition(nullptr)).c_str()) + "\",";
    body += "\"rssi\":" + std::to_string(watch::wifi_rssi_dbm()) + ",";
    body += "\"uptime_ms\":" + std::to_string(static_cast<uint64_t>(esp_timer_get_time() / 1000)) + ",";
    body += "\"heap_free_kb\":" + std::to_string(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024) + ",";
    body += "\"heap_largest_kb\":" + std::to_string(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024) + ",";
    body += "\"psram_free_kb\":" + std::to_string(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024) + ",";
    body += "\"psram_largest_kb\":" + std::to_string(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024) + ",";
    body += "\"reset_reason\":" + std::to_string(static_cast<int>(esp_reset_reason())) + ",";
    body += "\"wifi_connected\":";
    body += watch::wifi_is_connected() ? "true" : "false";
    body += "}";
    HttpResponse response;
    return http_request(HTTP_METHOD_PUT, STATUS_URL, token, body.c_str(), response);
}

void startup_report_task(void *)
{
    mark_running_app_valid();
    for (int attempt = 0; attempt < 8; ++attempt) {
        vTaskDelay(pdMS_TO_TICKS(attempt == 0 ? 8000 : 10000));
        if (!watch::wifi_is_connected()) {
            continue;
        }
        char token[384] = {};
        if (!login(token, sizeof(token))) {
            continue;
        }
        const std::string running = partition_label(esp_ota_get_running_partition());
        const std::string boot = partition_label(esp_ota_get_boot_partition());
        char state[96] = {};
        std::snprintf(state, sizeof(state), "启动完成：run=%s boot=%s", running.c_str(), boot.c_str());
        report_status(token, state, current_version(), running.c_str());
        break;
    }
    vTaskDeleteWithCaps(nullptr);
}
} // namespace

OtaApp *OtaApp::_instance = nullptr;

void ota_report_startup_async()
{
    static bool started = false;
    if (started) {
        return;
    }
    started = xTaskCreateWithCaps(
        startup_report_task,
        "ota_startup_report",
        OTA_STARTUP_REPORT_TASK_STACK_SIZE,
        nullptr,
        3,
        nullptr,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    ) == pdPASS;
    if (!started) {
        ESP_LOGW(ESP_UTILS_LOG_TAG, "startup report task create failed");
    }
}

OtaApp *OtaApp::requestInstance()
{
    if (_instance == nullptr) {
        _instance = new OtaApp();
    }
    return _instance;
}

OtaApp::OtaApp(): systems::phone::App(APP_NAME, watch_app_icon_ota_48(), true, true, true) {}

bool OtaApp::run(void)
{
    lv_obj_t *screen = lv_scr_act();
    ESP_UTILS_CHECK_NULL_RETURN(screen, false, "Invalid active screen");
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x07080B), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

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
    lv_obj_set_style_pad_row(root, 10, 0);

    make_label(root, "OTA Update", &lv_font_montserrat_30, 0xFFFFFF);
    _status_label = make_label(root, "Tap Check Update", &lv_font_montserrat_16, 0xAEB7C6);
    lv_obj_set_width(_status_label, LV_PCT(100));
    lv_label_set_long_mode(_status_label, LV_LABEL_LONG_WRAP);

    lv_obj_t *info = make_card(root, 176);
    _current_label = make_label(info, "", &lv_font_montserrat_14, 0xD7DCE5);
    _remote_label = make_label(info, "", &lv_font_montserrat_14, 0xD7DCE5);
    _partition_label = make_label(info, "", &lv_font_montserrat_14, 0xAEB7C6);
    _detail_label = make_label(info, "", &lv_font_montserrat_12, 0xAEB7C6);
    _log_label = make_label(info, "", &lv_font_montserrat_12, 0x717B8C);
    lv_obj_set_width(_log_label, LV_PCT(100));
    lv_label_set_long_mode(_log_label, LV_LABEL_LONG_WRAP);

    lv_obj_t *progress = make_card(root, 128);
    _download_label = make_label(progress, "Download 0%", &lv_font_montserrat_14, 0xD7DCE5);
    _download_bar = lv_bar_create(progress);
    lv_obj_set_width(_download_bar, LV_PCT(100));
    lv_bar_set_range(_download_bar, 0, 100);
    _write_label = make_label(progress, "Write 0%", &lv_font_montserrat_14, 0xD7DCE5);
    _write_bar = lv_bar_create(progress);
    lv_obj_set_width(_write_bar, LV_PCT(100));
    lv_bar_set_range(_write_bar, 0, 100);

    _check_btn = lv_button_create(root);
    lv_obj_set_width(_check_btn, LV_PCT(100));
    lv_obj_set_height(_check_btn, 46);
    style_button(_check_btn, 0x1B6BFF);
    lv_obj_add_event_cb(_check_btn, onCheckClicked, LV_EVENT_CLICKED, this);
    lv_obj_center(make_label(_check_btn, "Check Update", &lv_font_montserrat_18, 0xFFFFFF));

    _update_btn = lv_button_create(root);
    lv_obj_set_width(_update_btn, LV_PCT(100));
    lv_obj_set_height(_update_btn, 46);
    style_button(_update_btn, 0x23A559);
    lv_obj_add_event_cb(_update_btn, onUpdateClicked, LV_EVENT_CLICKED, this);
    lv_obj_center(make_label(_update_btn, "Confirm Update", &lv_font_montserrat_18, 0xFFFFFF));

    lv_obj_t *back = lv_button_create(root);
    lv_obj_set_width(back, LV_PCT(100));
    lv_obj_set_height(back, 44);
    style_button(back, 0x232833);
    lv_obj_add_event_cb(back, onBackClicked, LV_EVENT_CLICKED, this);
    lv_obj_center(make_label(back, "Back", &lv_font_montserrat_18, 0xFFFFFF));

    updateLabels(_ui_has_config ? &_ui_config : nullptr);
    setButtons(true, _update_available);
    flushUiState();
    _timer = lv_timer_create(onTimer, OTA_UI_TIMER_IDLE_MS, this);
    return true;
}

bool OtaApp::back(void)
{
    if (_operation_running) {
        setStatus("OTA task is running. Please wait.");
        flushUiState();
        return true;
    }
    if (_timer != nullptr) {
        lv_timer_delete(_timer);
        _timer = nullptr;
    }
    _status_label = nullptr;
    _current_label = nullptr;
    _remote_label = nullptr;
    _partition_label = nullptr;
    _detail_label = nullptr;
    _log_label = nullptr;
    _download_label = nullptr;
    _write_label = nullptr;
    _download_bar = nullptr;
    _write_bar = nullptr;
    _check_btn = nullptr;
    _update_btn = nullptr;
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

void OtaApp::onBackClicked(lv_event_t *event)
{
    auto *app = static_cast<OtaApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->back();
}

void OtaApp::onCheckClicked(lv_event_t *event)
{
    auto *app = static_cast<OtaApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->startOperation(Operation::Check);
}

void OtaApp::onUpdateClicked(lv_event_t *event)
{
    auto *app = static_cast<OtaApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->startOperation(Operation::Update);
}

void OtaApp::onTimer(lv_timer_t *timer)
{
    auto *app = static_cast<OtaApp *>(lv_timer_get_user_data(timer));
    if (app != nullptr) {
        if (app->_timer != nullptr) {
            lv_timer_set_period(app->_timer, app->_operation_running ? OTA_UI_TIMER_BUSY_MS : OTA_UI_TIMER_IDLE_MS);
        }
        app->flushUiState();
    }
}

void OtaApp::taskEntry(void *arg)
{
    TaskRequest *request = static_cast<TaskRequest *>(arg);
    if (request != nullptr && request->app != nullptr) {
        request->app->runOperation(request->operation);
        request->app->_operation_running = false;
        request->app->_task = nullptr;
    }
    delete request;
    vTaskDeleteWithCaps(nullptr);
}

void OtaApp::startOperation(Operation operation)
{
    if (_state_mutex == nullptr) {
        _state_mutex = xSemaphoreCreateMutex();
    }
    if (_operation_running) {
        setStatus("Task already running");
        flushUiState();
        return;
    }
    if (_task != nullptr) {
        setStatus("Previous OTA task is closing");
        flushUiState();
        return;
    }
    _operation_running = true;
    setButtons(false, false);
    setStatus(operation == Operation::Check ? "Checking remote version..." : "Starting OTA...");
    setDetail(operation == Operation::Check ? "State: checking remote config" : "State: preparing update");
    flushUiState();

    auto *request = new TaskRequest{.app = this, .operation = operation};
    BaseType_t task_ok = xTaskCreateWithCaps(
        taskEntry,
        operation == Operation::Check ? "watch_ota_check" : "watch_ota_update",
        OTA_TASK_STACK_SIZE,
        request,
        4,
        &_task,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    if (task_ok != pdPASS) {
        ESP_LOGW(
            ESP_UTILS_LOG_TAG,
            "OTA task create failed internal=%u largest_internal=%u psram=%u largest_psram=%u",
            static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
            static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
            static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
            static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM))
        );
        delete request;
        _task = nullptr;
        _operation_running = false;
        char detail[160] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "State: failed / create task / int %luK largest %luK psram %luK",
            static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
            static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
            static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024)
        );
        setStatus("Failed to start OTA task");
        setDetail(detail);
        setButtons(true, _update_available);
        flushUiState();
    }
}

void OtaApp::setStatus(const char *text)
{
    if (_state_mutex == nullptr) {
        _state_mutex = xSemaphoreCreateMutex();
    }
    if ((_state_mutex == nullptr) || (xSemaphoreTake(_state_mutex, pdMS_TO_TICKS(50)) == pdTRUE)) {
        std::snprintf(_ui_status, sizeof(_ui_status), "%s", text != nullptr ? text : "");
        _ui_dirty = true;
        if (_state_mutex != nullptr) {
            xSemaphoreGive(_state_mutex);
        }
    }
}

void OtaApp::setButtons(bool check_enabled, bool update_enabled)
{
    if (_state_mutex == nullptr) {
        _state_mutex = xSemaphoreCreateMutex();
    }
    if ((_state_mutex == nullptr) || (xSemaphoreTake(_state_mutex, pdMS_TO_TICKS(50)) == pdTRUE)) {
        _ui_check_enabled = check_enabled;
        _ui_update_enabled = update_enabled;
        _ui_dirty = true;
        if (_state_mutex != nullptr) {
            xSemaphoreGive(_state_mutex);
        }
    }
}

void OtaApp::setProgress(int download_percent, int write_percent)
{
    if (_state_mutex == nullptr) {
        _state_mutex = xSemaphoreCreateMutex();
    }
    if ((_state_mutex == nullptr) || (xSemaphoreTake(_state_mutex, pdMS_TO_TICKS(50)) == pdTRUE)) {
        _ui_download_percent = std::clamp(download_percent, 0, 100);
        _ui_write_percent = std::clamp(write_percent, 0, 100);
        _ui_dirty = true;
        if (_state_mutex != nullptr) {
            xSemaphoreGive(_state_mutex);
        }
    }
}

void OtaApp::setDetail(const char *text)
{
    if (_state_mutex == nullptr) {
        _state_mutex = xSemaphoreCreateMutex();
    }
    if ((_state_mutex == nullptr) || (xSemaphoreTake(_state_mutex, pdMS_TO_TICKS(50)) == pdTRUE)) {
        std::snprintf(_ui_detail, sizeof(_ui_detail), "%s", text != nullptr ? text : "");
        _ui_dirty = true;
        if (_state_mutex != nullptr) {
            xSemaphoreGive(_state_mutex);
        }
    }
}

void OtaApp::updateLabels(const OtaConfig *config)
{
    if (_state_mutex == nullptr) {
        _state_mutex = xSemaphoreCreateMutex();
    }
    if ((_state_mutex == nullptr) || (xSemaphoreTake(_state_mutex, pdMS_TO_TICKS(50)) == pdTRUE)) {
        if (config != nullptr) {
            _ui_config = *config;
            _ui_has_config = true;
        } else {
            _ui_config = {};
            _ui_has_config = false;
        }
        _ui_dirty = true;
        if (_state_mutex != nullptr) {
            xSemaphoreGive(_state_mutex);
        }
    }
}

void OtaApp::flushUiState()
{
    OtaConfig config = {};
    char status[sizeof(_ui_status)] = {};
    char detail[sizeof(_ui_detail)] = {};
    int download_percent = 0;
    int write_percent = 0;
    bool has_config = false;
    bool check_enabled = true;
    bool update_enabled = false;

    if (_state_mutex == nullptr) {
        _state_mutex = xSemaphoreCreateMutex();
    }
    if ((_state_mutex != nullptr) && (xSemaphoreTake(_state_mutex, 0) != pdTRUE)) {
        return;
    }

    if (!_ui_dirty) {
        if (_state_mutex != nullptr) {
            xSemaphoreGive(_state_mutex);
        }
        return;
    }
    config = _ui_config;
    std::snprintf(status, sizeof(status), "%s", _ui_status);
    std::snprintf(detail, sizeof(detail), "%s", _ui_detail);
    download_percent = _ui_download_percent;
    write_percent = _ui_write_percent;
    has_config = _ui_has_config;
    check_enabled = _ui_check_enabled;
    update_enabled = _ui_update_enabled;
    _ui_dirty = false;
    if (_state_mutex != nullptr) {
        xSemaphoreGive(_state_mutex);
    }

    char text[256] = {};
    if (_status_label != nullptr) {
        lv_label_set_text(_status_label, status);
    }
    if (_current_label != nullptr) {
        std::snprintf(text, sizeof(text), "Current: %s", current_version());
        lv_label_set_text(_current_label, text);
    }
    if (_remote_label != nullptr) {
        std::snprintf(text, sizeof(text), "Remote: %s", (has_config && config.version[0] != '\0') ? config.version : "--");
        lv_label_set_text(_remote_label, text);
    }
    if (_partition_label != nullptr) {
        lv_label_set_text(_partition_label, partition_summary().c_str());
    }
    if (_detail_label != nullptr) {
        lv_label_set_text(_detail_label, detail);
    }
    if (_log_label != nullptr) {
        lv_label_set_text(_log_label, (has_config && config.log[0] != '\0') ? config.log : CONFIG_URL);
    }
    if (_download_label != nullptr) {
        std::snprintf(text, sizeof(text), "Download %d%%", download_percent);
        lv_label_set_text(_download_label, text);
    }
    if (_download_bar != nullptr) {
        lv_bar_set_value(_download_bar, download_percent, LV_ANIM_OFF);
    }
    if (_write_label != nullptr) {
        std::snprintf(text, sizeof(text), "Write %d%%", write_percent);
        lv_label_set_text(_write_label, text);
    }
    if (_write_bar != nullptr) {
        lv_bar_set_value(_write_bar, write_percent, LV_ANIM_OFF);
    }
    if (_check_btn != nullptr) {
        check_enabled ? lv_obj_clear_state(_check_btn, LV_STATE_DISABLED) : lv_obj_add_state(_check_btn, LV_STATE_DISABLED);
        lv_obj_set_style_opa(_check_btn, check_enabled ? LV_OPA_COVER : LV_OPA_50, 0);
    }
    if (_update_btn != nullptr) {
        update_enabled ? lv_obj_clear_state(_update_btn, LV_STATE_DISABLED) : lv_obj_add_state(_update_btn, LV_STATE_DISABLED);
        lv_obj_set_style_opa(_update_btn, update_enabled ? LV_OPA_COVER : LV_OPA_50, 0);
    }
}

void OtaApp::runOperation(Operation operation)
{
    auto finish = [this]() {
        setButtons(true, _update_available);
    };

    if (!watch::wifi_is_connected()) {
        _update_available = false;
        setStatus("WiFi not connected. Open Settings/Quick first.");
        setDetail("State: failed / wifi disconnected");
        finish();
        return;
    }

    if (_token[0] == '\0') {
        setStatus("Login OTA server...");
        setDetail("State: login DUDUSERVER");
        if (!login(_token, sizeof(_token))) {
            setStatus("Login failed");
            setDetail("State: failed / login");
            _update_available = false;
            finish();
            return;
        }
    }

    OtaConfig config = {};
    int status = 0;
    setStatus("Fetch remote config...");
    setDetail("State: fetch /api/v1/device-config");
    if (!fetch_config(_token, config, &status)) {
        if (status == 401 || status == 403) {
            _token[0] = '\0';
            if (login(_token, sizeof(_token))) {
                fetch_config(_token, config, &status);
            }
        }
    }
    if (config.version[0] == '\0' || config.url[0] == '\0') {
        char msg[64] = {};
        std::snprintf(msg, sizeof(msg), "Config failed HTTP %d", status);
        setStatus(msg);
        setDetail("State: failed / invalid remote config");
        _update_available = false;
        finish();
        return;
    }

    const bool available = config.enabled && std::strncmp(config.url, "http://", 7) == 0 && version_newer(config.version, current_version());
    _latest = config;
    _update_available = available;
    updateLabels(&config);

    if (operation == Operation::Check) {
        report_status(_token, available ? "发现新版本" : "无更新", config.version);
        setStatus(available ? "New version found. Confirm to update." : "Already latest or OTA disabled.");
        char detail[128] = {};
        std::snprintf(detail, sizeof(detail), "State: %s target=%s", available ? "ready" : "idle", config.version);
        setDetail(detail);
        finish();
        return;
    }

    if (!available) {
        report_status(_token, "无可更新版本", config.version);
        setStatus("No valid update. Check first.");
        setDetail("State: idle / no valid update");
        finish();
        return;
    }

    const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);
    if (next == nullptr) {
        report_status(_token, "OTA失败：无可用分区", config.version);
        setStatus("No OTA partition");
        setDetail("State: failed / no OTA partition");
        finish();
        return;
    }
    const std::string target_partition = partition_label(next);
    report_status(_token, "OTA开始", config.version, target_partition.c_str(), 0, 0, -1);
    setProgress(0, 0);
    setStatus("Open firmware URL...");
    setDetail("State: opening firmware URL");

    esp_http_client_config_t http_config = {};
    http_config.url = config.url;
    http_config.timeout_ms = 30000;
    http_config.user_agent = USER_AGENT;
    http_config.keep_alive_enable = false;
    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == nullptr) {
        report_status(_token, "OTA失败：HTTP初始化失败", config.version, target_partition.c_str());
        setStatus("HTTP init failed");
        setDetail("State: failed / http init");
        finish();
        return;
    }
    esp_http_client_set_method(client, HTTP_METHOD_GET);
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        report_status(_token, "OTA失败：固件连接失败", config.version, target_partition.c_str());
        char msg[96] = {};
        std::snprintf(msg, sizeof(msg), "Firmware URL open failed: %s", esp_err_to_name(err));
        setStatus(msg);
        setDetail("State: failed / firmware open");
        finish();
        return;
    }
    const int64_t content_length = esp_http_client_fetch_headers(client);
    const int http_status = esp_http_client_get_status_code(client);
    if (http_status < 200 || http_status >= 300) {
        esp_http_client_cleanup(client);
        char msg[48] = {};
        std::snprintf(msg, sizeof(msg), "Firmware HTTP %d", http_status);
        report_status(_token, "OTA失败：固件HTTP错误", config.version, target_partition.c_str());
        setStatus(msg);
        setDetail("State: failed / firmware HTTP");
        finish();
        return;
    }
    if (content_length <= 0 || static_cast<uint64_t>(content_length) > next->size) {
        esp_http_client_cleanup(client);
        report_status(_token, "OTA失败：固件大小无效或超过分区", config.version, target_partition.c_str());
        setStatus("Invalid firmware size");
        char detail[128] = {};
        std::snprintf(detail, sizeof(detail), "State: failed / size=%lld partition=%lu",
                      static_cast<long long>(content_length), static_cast<unsigned long>(next->size));
        setDetail(detail);
        finish();
        return;
    }

    auto buffer = std::make_unique<uint8_t[]>(OTA_BUFFER_SIZE);
    if (!buffer) {
        esp_http_client_cleanup(client);
        report_status(_token, "OTA失败：buffer内存不足", config.version, target_partition.c_str());
        setStatus("OTA buffer alloc failed");
        setDetail("State: failed / buffer alloc");
        finish();
        return;
    }
    std::array<uint8_t, APP_DESC_HEADER_BYTES> header = {};
    size_t header_used = 0;
    esp_ota_handle_t handle = 0;
    bool ota_started = false;
    int64_t total_read = 0;
    int last_percent = -1;
    int last_ui_percent = -1;
    setStatus("Downloading and writing...");
    char dl_detail[128] = {};
    std::snprintf(dl_detail, sizeof(dl_detail), "State: downloading target=%s size=%lld",
                  target_partition.c_str(), static_cast<long long>(content_length));
    setDetail(dl_detail);
    while (true) {
        int read = esp_http_client_read(client, reinterpret_cast<char *>(buffer.get()), OTA_BUFFER_SIZE);
        if (read < 0) {
            err = ESP_FAIL;
            break;
        }
        if (read == 0) {
            if (esp_http_client_is_complete_data_received(client)) {
                err = ESP_OK;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        bool chunk_written = false;
        if (!ota_started) {
            size_t copy = std::min(static_cast<size_t>(read), header.size() - header_used);
            std::memcpy(header.data() + header_used, buffer.get(), copy);
            header_used += copy;
            if (header_used < header.size()) {
                total_read += read;
                continue;
            }
            if (header[0] != 0xE9) {
                err = ESP_ERR_OTA_VALIDATE_FAILED;
                break;
            }
            esp_app_desc_t desc = {};
            std::memcpy(&desc, header.data() + APP_DESC_OFFSET, sizeof(desc));
            if (desc.magic_word != ESP_APP_DESC_MAGIC_WORD || std::strcmp(desc.version, config.version) != 0) {
                char version_detail[128] = {};
                std::snprintf(version_detail, sizeof(version_detail), "State: failed / bin=%s server=%s",
                              desc.version, config.version);
                setDetail(version_detail);
                err = ESP_ERR_INVALID_VERSION;
                break;
            }
            err = esp_ota_begin(next, OTA_WITH_SEQUENTIAL_WRITES, &handle);
            if (err != ESP_OK) {
                break;
            }
            ota_started = true;
            err = esp_ota_write(handle, header.data(), header.size());
            if (err != ESP_OK) {
                break;
            }
            if (copy < static_cast<size_t>(read)) {
                err = esp_ota_write(handle, buffer.get() + copy, static_cast<size_t>(read) - copy);
                if (err != ESP_OK) {
                    break;
                }
            }
            chunk_written = true;
        }
        if (!chunk_written) {
            err = esp_ota_write(handle, buffer.get(), read);
            if (err != ESP_OK) {
                break;
            }
        }
        total_read += read;
        int percent = static_cast<int>((total_read * 100) / content_length);
        percent = std::clamp(percent, 0, 100);
        if ((percent == 100) || (last_ui_percent < 0) || ((percent - last_ui_percent) >= OTA_PROGRESS_UI_STEP)) {
            last_ui_percent = percent;
            setProgress(percent, percent);
            char progress_detail[128] = {};
            std::snprintf(progress_detail, sizeof(progress_detail), "State: downloading %lld/%lld bytes",
                          static_cast<long long>(total_read), static_cast<long long>(content_length));
            setDetail(progress_detail);
        }
        if (percent != last_percent) {
            last_percent = percent;
            if (percent == 100 || percent % 5 == 0) {
                report_status(_token, "OTA进度", config.version, target_partition.c_str(), percent, total_read, content_length);
            }
        }
    }

    esp_http_client_cleanup(client);
    if (err != ESP_OK || !ota_started || total_read != content_length) {
        if (ota_started) {
            esp_ota_abort(handle);
        }
        char msg[96] = {};
        std::snprintf(msg, sizeof(msg), "OTA failed: %s %lld/%lld", esp_err_to_name(err), static_cast<long long>(total_read), static_cast<long long>(content_length));
        report_status(_token, msg, config.version, target_partition.c_str());
        setStatus(msg);
        setDetail("State: failed / download or write");
        finish();
        return;
    }

    setProgress(100, 100);
    setStatus("Validate firmware...");
    setDetail("State: verifying esp_ota_end");
    report_status(_token, "OTA校验开始", config.version, target_partition.c_str(), 100, total_read, content_length);
    err = esp_ota_end(handle);
    if (err != ESP_OK) {
        char msg[80] = {};
        std::snprintf(msg, sizeof(msg), "Validate failed: %s", esp_err_to_name(err));
        report_status(_token, msg, config.version, target_partition.c_str());
        setStatus(msg);
        setDetail("State: failed / esp_ota_end");
        finish();
        return;
    }
    report_status(_token, "OTA校验完成", config.version, target_partition.c_str(), 100, total_read, content_length);
    setDetail("State: switching boot partition");
    report_status(_token, "OTA切换启动分区", config.version, target_partition.c_str(), 100, total_read, content_length);
    err = esp_ota_set_boot_partition(next);
    if (err != ESP_OK) {
        char msg[80] = {};
        std::snprintf(msg, sizeof(msg), "Set boot failed: %s", esp_err_to_name(err));
        report_status(_token, msg, config.version, target_partition.c_str());
        setStatus(msg);
        setDetail("State: failed / set boot partition");
        finish();
        return;
    }
    const esp_partition_t *boot = esp_ota_get_boot_partition();
    if (boot != nullptr && std::strcmp(boot->label, next->label) != 0) {
        char msg[96] = {};
        std::snprintf(msg, sizeof(msg), "Boot verify failed: boot=%s target=%s", boot->label, next->label);
        report_status(_token, msg, config.version, target_partition.c_str());
        setStatus(msg);
        setDetail("State: failed / boot partition verify");
        finish();
        return;
    }
    report_status(_token, "OTA完成，准备重启", config.version, target_partition.c_str(), 100, total_read, content_length);
    setStatus("OTA done. Restarting...");
    setDetail("State: completed / restarting");
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, OtaApp, APP_NAME, []()
{
    return std::shared_ptr<OtaApp>(OtaApp::requestInstance(), [](OtaApp *) {});
})

} // namespace esp_brookesia::apps
