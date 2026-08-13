#include "quota_app.hpp"

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
#include "watch_app_icons.hpp"
#include "watch_connectivity.hpp"
#include "watch_quota.hpp"

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "QuotaApp"

namespace esp_brookesia::apps {
namespace {

constexpr const char *APP_NAME = "Quota";
constexpr int SAFE_TOP = 24;
constexpr int SAFE_SIDE = 22;
constexpr const char *LOGIN_URL = "http://<OTA_SERVER>/DUDUSERVER/api/v1/auth/login";
constexpr const char *USAGE_URL = "http://<OTA_SERVER>/DUDUSERVER/api/v1/api-usage-status";
constexpr const char *USER_AGENT = "esp32-s3-watch-quota";
constexpr int HTTP_TIMEOUT_MS = 12000;
constexpr size_t MAX_HTTP_BODY = 48 * 1024;
constexpr uint32_t QUOTA_TASK_STACK_SIZE = 12 * 1024;
constexpr uint32_t QUOTA_TIMER_IDLE_MS = 3000;
constexpr uint32_t QUOTA_TIMER_BUSY_MS = 500;

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
    lv_obj_set_style_pad_row(card, 8, 0);
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
    if (object == nullptr) {
        return fallback;
    }
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (cJSON_IsString(item) && item->valuestring != nullptr) {
        return item->valuestring;
    }
    return fallback;
}

double json_double(cJSON *object, const char *name, double fallback = 0.0)
{
    if (object == nullptr) {
        return fallback;
    }
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (cJSON_IsNumber(item)) {
        return item->valuedouble;
    }
    return fallback;
}

bool json_number(cJSON *object, const char *name, double *value)
{
    if (object == nullptr || value == nullptr) {
        return false;
    }
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    *value = item->valuedouble;
    return true;
}

double json_first_double(cJSON *object, const char *first, const char *second, const char *third = nullptr, double fallback = 0.0)
{
    double value = fallback;
    if (json_number(object, first, &value)) {
        return value;
    }
    if (json_number(object, second, &value)) {
        return value;
    }
    if (third != nullptr && json_number(object, third, &value)) {
        return value;
    }
    return fallback;
}

cJSON *json_object(cJSON *object, const char *name)
{
    if (object == nullptr) {
        return nullptr;
    }
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsObject(item) ? item : nullptr;
}

cJSON *json_array(cJSON *object, const char *name)
{
    if (object == nullptr) {
        return nullptr;
    }
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsArray(item) ? item : nullptr;
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

void amount_text(char *buffer, size_t size, const char *currency, double value)
{
    if (currency != nullptr && currency[0] != '\0') {
        std::snprintf(buffer, size, "%s %.2f", currency, value);
    } else {
        std::snprintf(buffer, size, "%.2f", value);
    }
}

void compact_number(char *buffer, size_t size, double value)
{
    const double abs_value = value < 0 ? -value : value;
    if (abs_value >= 1000000000.0) {
        std::snprintf(buffer, size, "%.2fB", value / 1000000000.0);
    } else if (abs_value >= 1000000.0) {
        std::snprintf(buffer, size, "%.1fM", value / 1000000.0);
    } else if (abs_value >= 1000.0) {
        std::snprintf(buffer, size, "%.1fK", value / 1000.0);
    } else {
        std::snprintf(buffer, size, "%.0f", value);
    }
}

} // namespace

QuotaApp *QuotaApp::_instance = nullptr;

QuotaApp *QuotaApp::requestInstance()
{
    if (_instance == nullptr) {
        _instance = new QuotaApp();
    }
    return _instance;
}

QuotaApp::QuotaApp():
    systems::phone::App(APP_NAME, watch_app_icon_quota_48(), true, true, true)
{
    _mutex = xSemaphoreCreateMutex();
}

bool QuotaApp::run(void)
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
    lv_obj_set_scrollbar_mode(root, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title_row = lv_obj_create(root);
    ESP_UTILS_CHECK_NULL_RETURN(title_row, false, "Create title row failed");
    lv_obj_remove_style_all(title_row);
    lv_obj_set_width(title_row, LV_PCT(100));
    lv_obj_set_height(title_row, 52);
    lv_obj_set_flex_flow(title_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    make_label(title_row, "Quota", &lv_font_montserrat_30, 0xFFFFFF);
    lv_obj_t *back_btn = lv_button_create(title_row);
    lv_obj_set_size(back_btn, 92, 44);
    style_button(back_btn, 0x232833);
    lv_obj_add_event_cb(back_btn, onBackClicked, LV_EVENT_CLICKED, this);
    lv_obj_center(make_label(back_btn, "Back", &lv_font_montserrat_18, 0xFFFFFF));

    lv_obj_t *main_card = make_card(root, 166);
    _headline_label = make_label(main_card, "CURRENT BALANCE", &lv_font_montserrat_16, 0x8FA3B8);
    lv_obj_set_width(_headline_label, LV_PCT(100));
    lv_obj_set_style_text_align(_headline_label, LV_TEXT_ALIGN_CENTER, 0);

    _title_label = make_label(main_card, "--", &lv_font_montserrat_48, 0xFFFFFF);
    lv_obj_set_width(_title_label, LV_PCT(100));
    lv_obj_set_style_text_align(_title_label, LV_TEXT_ALIGN_CENTER, 0);

    _summary_label = make_label(main_card, "Tap Refresh", &lv_font_montserrat_18, 0x69D2FF);
    lv_obj_set_width(_summary_label, LV_PCT(100));
    lv_obj_set_style_text_align(_summary_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(_summary_label, LV_LABEL_LONG_WRAP);

    lv_obj_t *usage_card = make_card(root, 142);
    _today_label = make_label(usage_card, "Today: --", &lv_font_montserrat_16, 0xD7DCE5);
    lv_obj_set_width(_today_label, LV_PCT(100));
    lv_label_set_long_mode(_today_label, LV_LABEL_LONG_WRAP);
    _total_label = make_label(usage_card, "Total: --", &lv_font_montserrat_16, 0xD7DCE5);
    lv_obj_set_width(_total_label, LV_PCT(100));
    lv_label_set_long_mode(_total_label, LV_LABEL_LONG_WRAP);

    lv_obj_t *models_card = make_card(root, 118);
    _models_label = make_label(models_card, "Models: --", &lv_font_montserrat_14, 0xD7DCE5);
    lv_obj_set_width(_models_label, LV_PCT(100));
    lv_label_set_long_mode(_models_label, LV_LABEL_LONG_WRAP);

    lv_obj_t *detail_card = make_card(root, 132);
    _detail_label = make_label(detail_card, "API usage is synced through DUDUSERVER.", &lv_font_montserrat_14, 0xAEB7C6);
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
    _timer = lv_timer_create(onTimer, QUOTA_TIMER_IDLE_MS, this);
    startRefresh();
    return true;
}

bool QuotaApp::back(void)
{
    if (_timer != nullptr) {
        lv_timer_delete(_timer);
        _timer = nullptr;
    }
    clearObjects();
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

bool QuotaApp::close(void)
{
    if (_timer != nullptr) {
        lv_timer_delete(_timer);
        _timer = nullptr;
    }
    clearObjects();
    return true;
}

void QuotaApp::clearObjects()
{
    _headline_label = nullptr;
    _title_label = nullptr;
    _summary_label = nullptr;
    _today_label = nullptr;
    _total_label = nullptr;
    _models_label = nullptr;
    _detail_label = nullptr;
    _refresh_label = nullptr;
}

void QuotaApp::onBackClicked(lv_event_t *event)
{
    auto *app = static_cast<QuotaApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->back();
}

void QuotaApp::onRefreshClicked(lv_event_t *event)
{
    auto *app = static_cast<QuotaApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->startRefresh();
}

void QuotaApp::onTimer(lv_timer_t *timer)
{
    auto *app = static_cast<QuotaApp *>(lv_timer_get_user_data(timer));
    if (app != nullptr) {
        bool running = false;
        if (app->_mutex != nullptr && xSemaphoreTake(app->_mutex, 0) == pdTRUE) {
            running = app->_running;
            xSemaphoreGive(app->_mutex);
        }
        if (app->_timer != nullptr) {
            lv_timer_set_period(app->_timer, running ? QUOTA_TIMER_BUSY_MS : QUOTA_TIMER_IDLE_MS);
        }
        app->refreshUi();
    }
}

bool QuotaApp::startRefresh()
{
    if (!watch::wifi_is_connected()) {
        setState(false, false, "OFFLINE", "WiFi not connected", "Connect WiFi first, then refresh quota.", "Today: --", "Total: --", "Models: --", "");
        return false;
    }
    if (_mutex != nullptr && xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (_running) {
            xSemaphoreGive(_mutex);
            return false;
        }
        _running = true;
        _title = "Loading";
        _summary = "Reading API usage";
        _today = "Today: --";
        _total = "Total: --";
        _models = "Models: --";
        _detail = "Login and fetch /api-usage-status...";
        xSemaphoreGive(_mutex);
    }
    BaseType_t ok = xTaskCreateWithCaps(
        quotaTask,
        "watch_quota_app",
        QUOTA_TASK_STACK_SIZE,
        this,
        5,
        nullptr,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    if (ok != pdPASS) {
        setState(false, false, "ERROR", "Task create failed", "FreeRTOS could not start quota task.", "Today: --", "Total: --", "Models: --", "");
        return false;
    }
    return true;
}

void QuotaApp::setState(
    bool running,
    bool ok,
    const char *headline,
    const char *title,
    const char *summary,
    const char *today,
    const char *total,
    const char *models,
    const char *detail
)
{
    if (_mutex != nullptr && xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        _running = running;
        _ok = ok;
        _headline = headline != nullptr ? headline : "";
        _title = title != nullptr ? title : "";
        _summary = summary != nullptr ? summary : "";
        _today = today != nullptr ? today : "";
        _total = total != nullptr ? total : "";
        _models = models != nullptr ? models : "";
        _detail = detail != nullptr ? detail : "";
        _last_refresh_us = esp_timer_get_time();
        xSemaphoreGive(_mutex);
    }
}

void QuotaApp::refreshUi()
{
    std::string title;
    std::string summary;
    std::string detail;
    std::string headline;
    std::string today;
    std::string total;
    std::string models;
    bool running = false;
    bool ok = false;
    int64_t last_us = 0;
    if (_mutex != nullptr && xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        headline = _headline;
        title = _title;
        summary = _summary;
        today = _today;
        total = _total;
        models = _models;
        detail = _detail;
        running = _running;
        ok = _ok;
        last_us = _last_refresh_us;
        xSemaphoreGive(_mutex);
    }
    if (_headline_label != nullptr) {
        lv_label_set_text(_headline_label, headline.c_str());
    }
    if (_title_label != nullptr) {
        lv_label_set_text(_title_label, title.c_str());
        lv_obj_set_style_text_color(_title_label, lv_color_hex(ok ? 0x23D18B : (running ? 0xFFCC33 : 0xFFFFFF)), 0);
    }
    if (_summary_label != nullptr) {
        lv_label_set_text(_summary_label, summary.c_str());
    }
    if (_today_label != nullptr) {
        lv_label_set_text(_today_label, today.c_str());
    }
    if (_total_label != nullptr) {
        lv_label_set_text(_total_label, total.c_str());
    }
    if (_models_label != nullptr) {
        lv_label_set_text(_models_label, models.c_str());
    }
    if (_detail_label != nullptr) {
        lv_label_set_text(_detail_label, detail.c_str());
    }
    if (_refresh_label != nullptr) {
        const bool recent = last_us > 0 && (esp_timer_get_time() - last_us) < 3000000;
        lv_label_set_text(_refresh_label, running ? "Loading..." : (recent ? (ok ? "OK" : "Retry") : "Refresh"));
    }
}

void QuotaApp::quotaTask(void *arg)
{
    auto *app = static_cast<QuotaApp *>(arg);
    if (app == nullptr) {
        vTaskDeleteWithCaps(nullptr);
        return;
    }
    std::string token = login_token();
    if (token.empty()) {
        app->setState(false, false, "NO DATA", "Login failed", "Check DUDUSERVER login/network.", "Today: --", "Total: --", "Models: --", "");
        vTaskDeleteWithCaps(nullptr);
        return;
    }
    HttpResponse response;
    if (!http_request(HTTP_METHOD_GET, USAGE_URL, token.c_str(), nullptr, response)) {
        char detail[96] = {};
        std::snprintf(detail, sizeof(detail), "GET api-usage-status failed, http=%d", response.status);
        app->setState(false, false, "NO DATA", "Quota endpoint not ready", "", "Today: --", "Total: --", "Models: --", detail);
        vTaskDeleteWithCaps(nullptr);
        return;
    }
    cJSON *root = cJSON_Parse(response.body.c_str());
    if (root == nullptr) {
        app->setState(false, false, "ERROR", "Invalid JSON", "Server returned invalid api usage JSON.", "Today: --", "Total: --", "Models: --", "");
        vTaskDeleteWithCaps(nullptr);
        return;
    }

    const std::string source = json_string(root, "source", "ccswitch");
    const std::string provider = json_string(root, "provider", "-");
    const std::string account = json_string(root, "account", "-");
    const std::string plan = json_string(root, "plan", "-");
    const std::string status = json_string(root, "status", "unknown");
    const std::string currency = json_string(root, "currency", "");
    const double limit = json_double(root, "limit_amount", 0.0);
    const double used = json_double(root, "used_amount", 0.0);
    const double remaining = json_double(root, "remaining_amount", 0.0);
    double percent = json_double(root, "used_percent", 0.0);
    if (percent <= 0.0 && limit > 0.0) {
        percent = (used * 100.0) / limit;
    }
    const std::string reset = json_string(root, "reset_at", "-");
    const std::string note = json_string(root, "note", "");
    const std::string updated = json_string(root, "updated_at", "-");
    cJSON *raw = json_object(root, "raw");
    cJSON *usage_response = json_object(raw, "usage_response");
    cJSON *usage = json_object(raw, "usage");
    if (usage == nullptr) {
        usage = json_object(usage_response, "usage");
    }
    cJSON *today_usage = json_object(usage, "today");
    cJSON *total_usage = json_object(usage, "total");
    cJSON *model_stats = json_array(raw, "model_stats");
    if (model_stats == nullptr) {
        model_stats = json_array(usage_response, "model_stats");
    }
    const std::string mode = json_string(usage_response, "mode", "-");
    const std::string raw_month = json_string(raw, "month", "");
    const std::string month_start = json_string(raw, "month_start", "");
    const std::string month_end = json_string(raw, "month_end", "");
    const std::string latest_rollup = json_string(raw, "latest_rollup_date", "");
    const double rpm = json_double(usage, "rpm", 0.0);
    const double tpm = json_double(usage, "tpm", 0.0);
    const double avg_ms = json_double(usage, "average_duration_ms", 0.0);

    const bool has_metric = limit > 0.0 || used > 0.0 || remaining > 0.0 || percent > 0.0;
    if (status == "not_synced" || (!has_metric && status != "unlimited" && status != "usage_only")) {
        char detail[384] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "Source: %s\nStatus: %s\nUpdated: %s\n%s",
            source.c_str(),
            status.c_str(),
            updated.c_str(),
            note.empty() ? "Waiting for a real local usage sync. No fake quota values are shown." : note.c_str()
        );
        app->setState(false, false, "NO REAL DATA", "No real quota data", "", "Today: --", "Total: --", "Models: --", detail);
        cJSON_Delete(root);
        vTaskDeleteWithCaps(nullptr);
        return;
    }

    const bool balance_only = status == "balance_only" || (limit <= 0.0 && remaining > 0.0 && used <= 0.0);

    char remaining_text[32] = {};
    char used_text[32] = {};
    char limit_text[32] = {};
    amount_text(remaining_text, sizeof(remaining_text), currency.c_str(), remaining);
    amount_text(used_text, sizeof(used_text), currency.c_str(), used);
    amount_text(limit_text, sizeof(limit_text), currency.c_str(), limit);

    char headline[48] = {};
    char title[48] = {};
    if (balance_only) {
        std::snprintf(headline, sizeof(headline), "CURRENT BALANCE");
        std::snprintf(title, sizeof(title), "%s", remaining_text);
    } else if (limit > 0.0) {
        std::snprintf(headline, sizeof(headline), "MONTHLY USAGE");
        std::snprintf(title, sizeof(title), "%.0f%%", percent);
    } else {
        std::snprintf(headline, sizeof(headline), status == "usage_only" ? "MONTHLY COST" : "USAGE");
        std::snprintf(title, sizeof(title), "%s", used_text);
    }

    char summary[128] = {};
    if (balance_only) {
        std::snprintf(summary, sizeof(summary), "%s · balance %s", provider.c_str(), remaining_text);
    } else if (limit > 0.0) {
        std::snprintf(summary, sizeof(summary), "%s · remain %s", provider.c_str(), remaining_text);
    } else {
        std::snprintf(summary, sizeof(summary), "%s · real usage, no quota limit", provider.c_str());
    }

    char today_tokens[24] = {};
    char total_tokens[24] = {};
    char today_cost[32] = {};
    char total_cost[32] = {};
    char rpm_text[16] = {};
    char tpm_text[24] = {};
    compact_number(today_tokens, sizeof(today_tokens), json_double(today_usage, "total_tokens", 0.0));
    compact_number(total_tokens, sizeof(total_tokens), json_double(total_usage, "total_tokens", 0.0));
    compact_number(rpm_text, sizeof(rpm_text), rpm);
    compact_number(tpm_text, sizeof(tpm_text), tpm);
    const double today_cost_value = json_first_double(today_usage, "actual_cost", "total_cost", "cost_usd", 0.0);
    const double total_cost_value = json_first_double(total_usage, "actual_cost", "total_cost", "cost_usd", 0.0);
    amount_text(today_cost, sizeof(today_cost), currency.c_str(), today_cost_value);
    amount_text(total_cost, sizeof(total_cost), currency.c_str(), total_cost_value);

    char today[192] = {};
    std::snprintf(
        today,
        sizeof(today),
        "Today  Req %.0f  Tok %s\nCost %s  RPM %s  TPM %s",
        json_double(today_usage, "requests", 0.0),
        today_tokens,
        today_cost,
        rpm_text,
        tpm_text
    );

    char total[192] = {};
    std::snprintf(
        total,
        sizeof(total),
        "Total  Req %.0f  Tok %s\nCost %s  Avg %.1fs",
        json_double(total_usage, "requests", 0.0),
        total_tokens,
        total_cost,
        avg_ms / 1000.0
    );

    char models[256] = {};
    std::snprintf(models, sizeof(models), "Models: not provided");
    if (model_stats != nullptr && cJSON_GetArraySize(model_stats) > 0) {
        char line[96] = {};
        std::snprintf(models, sizeof(models), "Models");
        const int count = cJSON_GetArraySize(model_stats);
        for (int i = 0; i < count && i < 3; ++i) {
            cJSON *item = cJSON_GetArrayItem(model_stats, i);
            if (!cJSON_IsObject(item)) {
                continue;
            }
            char request_text[16] = {};
            char model_cost[28] = {};
            compact_number(request_text, sizeof(request_text), json_double(item, "requests", 0.0));
            amount_text(
                model_cost,
                sizeof(model_cost),
                currency.c_str(),
                json_double(item, "actual_cost", json_double(item, "total_cost", json_double(item, "cost_usd", 0.0)))
            );
            std::snprintf(
                line,
                sizeof(line),
                "\n%d %s  %s req  %s",
                i + 1,
                json_string(item, "model", "-").c_str(),
                request_text,
                model_cost
            );
            std::strncat(models, line, sizeof(models) - std::strlen(models) - 1);
        }
    }

    char detail[512] = {};
    if (balance_only) {
        std::snprintf(
            detail,
            sizeof(detail),
            "Source: %s\nAccount: %s\nPlan: %s\nStatus: %s  Mode: %s\nCycle: %s %s..%s\nLatest: %s\nUpdated: %s",
            source.c_str(),
            account.c_str(),
            plan.c_str(),
            status.c_str(),
            mode.c_str(),
            raw_month.c_str(),
            month_start.c_str(),
            month_end.empty() ? reset.c_str() : month_end.c_str(),
            latest_rollup.empty() ? "-" : latest_rollup.c_str(),
            updated.c_str()
        );
    } else if (limit > 0.0) {
        std::snprintf(
            detail,
            sizeof(detail),
            "Source: %s\nAccount: %s\nPlan: %s\nStatus: %s  Mode: %s\nUsed: %s / %s\nRemain: %s\nCycle: %s %s..%s\nUpdated: %s",
            source.c_str(),
            account.c_str(),
            plan.c_str(),
            status.c_str(),
            mode.c_str(),
            used_text,
            limit_text,
            remaining_text,
            raw_month.c_str(),
            month_start.c_str(),
            month_end.empty() ? reset.c_str() : month_end.c_str(),
            updated.c_str()
        );
    } else {
        std::snprintf(
            detail,
            sizeof(detail),
            "Source: %s\nAccount: %s\nPlan: %s\nStatus: %s  Mode: %s\nMonth used: %s\nLimit: not set in ccswitch\nLatest: %s\nUpdated: %s",
            source.c_str(),
            account.c_str(),
            plan.c_str(),
            status.c_str(),
            mode.c_str(),
            used_text,
            latest_rollup.empty() ? reset.c_str() : latest_rollup.c_str(),
            updated.c_str()
        );
    }

    const bool ok = status != "not_synced" && status != "error";
    if (ok) {
        watch::QuotaHomeSnapshot snapshot = {};
        snapshot.valid = true;
        std::snprintf(snapshot.provider, sizeof(snapshot.provider), "%s", provider.c_str());
        std::snprintf(snapshot.currency, sizeof(snapshot.currency), "%s", currency.c_str());
        std::snprintf(snapshot.status, sizeof(snapshot.status), "%s", status.c_str());
        snapshot.balance = remaining;
        snapshot.today_cost = today_cost_value;
        snapshot.today_tokens = json_double(today_usage, "total_tokens", 0.0);
        snapshot.total_cost = total_cost_value;
        snapshot.total_tokens = json_double(total_usage, "total_tokens", 0.0);
        std::snprintf(snapshot.updated_at, sizeof(snapshot.updated_at), "%s", updated.c_str());
        watch::quota_home_set(&snapshot);
    }
    app->setState(false, ok, headline, title, summary, today, total, models, detail);
    cJSON_Delete(root);
    vTaskDeleteWithCaps(nullptr);
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, QuotaApp, APP_NAME, []()
{
    return std::shared_ptr<QuotaApp>(QuotaApp::requestInstance(), [](QuotaApp *) {});
})

} // namespace esp_brookesia::apps
