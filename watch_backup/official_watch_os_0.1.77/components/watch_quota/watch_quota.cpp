#include "watch_quota.hpp"

#include <cstdio>
#include <cstring>
#include <string>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "watch_connectivity.hpp"

namespace watch {
namespace {

constexpr const char *TAG = "watch_quota";
constexpr const char *NVS_NAMESPACE = "watch_quota";
constexpr const char *LOGIN_URL = "http://<OTA_SERVER>/DUDUSERVER/api/v1/auth/login";
constexpr const char *USAGE_URL = "http://<OTA_SERVER>/DUDUSERVER/api/v1/api-usage-status";
constexpr const char *USER_AGENT = "esp32-s3-watch-quota-home";
constexpr int HTTP_TIMEOUT_MS = 12000;
constexpr size_t MAX_HTTP_BODY = 48 * 1024;
constexpr uint32_t REFRESH_TASK_STACK_SIZE = 12 * 1024;

SemaphoreHandle_t s_mutex = nullptr;
bool s_init_done = false;
QuotaHomeSnapshot s_snapshot = {};
TaskHandle_t s_refresh_task = nullptr;

struct HttpResponse {
    int status = 0;
    std::string body;
};

void lock()
{
    if (s_mutex != nullptr) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
    }
}

void unlock()
{
    if (s_mutex != nullptr) {
        xSemaphoreGive(s_mutex);
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

std::string login_token()
{
    HttpResponse response;
    const char *body = "{\"username\":\"admin\",\"password\":\"<OTA_PASSWORD>\"}";
    if (!http_request(HTTP_METHOD_POST, LOGIN_URL, nullptr, body, response)) {
        ESP_LOGW(TAG, "quota home login failed: http=%d", response.status);
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

void normalize(QuotaHomeSnapshot *snapshot)
{
    if (snapshot == nullptr) {
        return;
    }
    snapshot->provider[sizeof(snapshot->provider) - 1] = '\0';
    snapshot->status[sizeof(snapshot->status) - 1] = '\0';
    snapshot->currency[sizeof(snapshot->currency) - 1] = '\0';
    snapshot->updated_at[sizeof(snapshot->updated_at) - 1] = '\0';
    if (snapshot->provider[0] == '\0') {
        std::snprintf(snapshot->provider, sizeof(snapshot->provider), "Quota");
    }
    if (snapshot->currency[0] == '\0') {
        std::snprintf(snapshot->currency, sizeof(snapshot->currency), "$");
    }
    if (snapshot->status[0] == '\0') {
        std::snprintf(snapshot->status, sizeof(snapshot->status), "unknown");
    }
}

void load_double(nvs_handle_t handle, const char *key, double *value)
{
    if (value == nullptr) {
        return;
    }
    size_t len = sizeof(*value);
    nvs_get_blob(handle, key, value, &len);
}

void refresh_task(void *)
{
    if (!wifi_is_connected()) {
        ESP_LOGW(TAG, "quota home refresh skipped: WiFi not connected");
        s_refresh_task = nullptr;
        vTaskDeleteWithCaps(nullptr);
        return;
    }
    std::string token = login_token();
    if (token.empty()) {
        s_refresh_task = nullptr;
        vTaskDeleteWithCaps(nullptr);
        return;
    }
    HttpResponse response;
    if (!http_request(HTTP_METHOD_GET, USAGE_URL, token.c_str(), nullptr, response)) {
        ESP_LOGW(TAG, "quota home fetch failed: http=%d", response.status);
        s_refresh_task = nullptr;
        vTaskDeleteWithCaps(nullptr);
        return;
    }
    cJSON *root = cJSON_Parse(response.body.c_str());
    if (root == nullptr) {
        ESP_LOGW(TAG, "quota home JSON parse failed");
        s_refresh_task = nullptr;
        vTaskDeleteWithCaps(nullptr);
        return;
    }
    cJSON *raw = json_object(root, "raw");
    cJSON *usage_response = json_object(raw, "usage_response");
    cJSON *usage = json_object(raw, "usage");
    if (usage == nullptr) {
        usage = json_object(usage_response, "usage");
    }
    cJSON *today = json_object(usage, "today");
    cJSON *total = json_object(usage, "total");

    QuotaHomeSnapshot snapshot = {};
    snapshot.valid = true;
    std::snprintf(snapshot.provider, sizeof(snapshot.provider), "%s", json_string(root, "provider", "Quota").c_str());
    std::snprintf(snapshot.status, sizeof(snapshot.status), "%s", json_string(root, "status", "unknown").c_str());
    std::snprintf(snapshot.currency, sizeof(snapshot.currency), "%s", json_string(root, "currency", "USD").c_str());
    snapshot.balance = json_double(root, "remaining_amount", 0.0);
    snapshot.today_cost = json_first_double(today, "actual_cost", "total_cost", "cost_usd", 0.0);
    snapshot.today_tokens = json_double(today, "total_tokens", 0.0);
    snapshot.total_cost = json_first_double(total, "actual_cost", "total_cost", "cost_usd", 0.0);
    snapshot.total_tokens = json_double(total, "total_tokens", 0.0);
    std::snprintf(snapshot.updated_at, sizeof(snapshot.updated_at), "%s", json_string(root, "updated_at", "").c_str());
    cJSON_Delete(root);

    quota_home_set(&snapshot);
    ESP_LOGI(TAG, "quota home refresh ok balance=%.2f today_cost=%.2f", snapshot.balance, snapshot.today_cost);
    s_refresh_task = nullptr;
    vTaskDeleteWithCaps(nullptr);
}

esp_err_t init_locked()
{
    if (s_init_done) {
        return ESP_OK;
    }
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_OK) {
        uint8_t valid = 0;
        nvs_get_u8(handle, "valid", &valid);
        s_snapshot.valid = valid != 0;
        size_t len = sizeof(s_snapshot.provider);
        nvs_get_str(handle, "provider", s_snapshot.provider, &len);
        len = sizeof(s_snapshot.currency);
        nvs_get_str(handle, "currency", s_snapshot.currency, &len);
        len = sizeof(s_snapshot.status);
        nvs_get_str(handle, "status", s_snapshot.status, &len);
        len = sizeof(s_snapshot.updated_at);
        nvs_get_str(handle, "updated", s_snapshot.updated_at, &len);
        load_double(handle, "balance", &s_snapshot.balance);
        load_double(handle, "today_c", &s_snapshot.today_cost);
        load_double(handle, "today_t", &s_snapshot.today_tokens);
        load_double(handle, "total_c", &s_snapshot.total_cost);
        load_double(handle, "total_t", &s_snapshot.total_tokens);
        nvs_close(handle);
        normalize(&s_snapshot);
    }
    s_init_done = true;
    return ESP_OK;
}

} // namespace

esp_err_t quota_home_get(QuotaHomeSnapshot *snapshot)
{
    ESP_RETURN_ON_FALSE(snapshot != nullptr, ESP_ERR_INVALID_ARG, TAG, "Invalid snapshot");
    if (s_mutex == nullptr) {
        s_mutex = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_mutex != nullptr, ESP_ERR_NO_MEM, TAG, "mutex alloc failed");
    }
    lock();
    esp_err_t err = init_locked();
    if (err == ESP_OK) {
        *snapshot = s_snapshot;
    }
    unlock();
    return err;
}

esp_err_t quota_home_set(const QuotaHomeSnapshot *snapshot)
{
    ESP_RETURN_ON_FALSE(snapshot != nullptr, ESP_ERR_INVALID_ARG, TAG, "Invalid snapshot");
    if (s_mutex == nullptr) {
        s_mutex = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_mutex != nullptr, ESP_ERR_NO_MEM, TAG, "mutex alloc failed");
    }
    lock();
    s_snapshot = *snapshot;
    s_snapshot.valid = true;
    normalize(&s_snapshot);

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, "valid", 1);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(handle, "provider", s_snapshot.provider);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(handle, "currency", s_snapshot.currency);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(handle, "status", s_snapshot.status);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(handle, "updated", s_snapshot.updated_at);
    }
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, "balance", &s_snapshot.balance, sizeof(s_snapshot.balance));
    }
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, "today_c", &s_snapshot.today_cost, sizeof(s_snapshot.today_cost));
    }
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, "today_t", &s_snapshot.today_tokens, sizeof(s_snapshot.today_tokens));
    }
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, "total_c", &s_snapshot.total_cost, sizeof(s_snapshot.total_cost));
    }
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, "total_t", &s_snapshot.total_tokens, sizeof(s_snapshot.total_tokens));
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    if (handle != 0) {
        nvs_close(handle);
    }
    unlock();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "save quota home cache failed: %s", esp_err_to_name(err));
    }
    return err;
}

void quota_home_text(char *buffer, size_t buffer_size)
{
    if ((buffer == nullptr) || (buffer_size == 0)) {
        return;
    }
    QuotaHomeSnapshot snapshot = {};
    if (quota_home_get(&snapshot) != ESP_OK || !snapshot.valid) {
        std::snprintf(buffer, buffer_size, "Quota --");
        return;
    }
    if (snapshot.today_tokens <= 0.0 && snapshot.total_tokens <= 0.0) {
        std::snprintf(buffer, buffer_size, quota_refresh_is_running() ? "Quota loading..." : "Quota --");
        return;
    }
    char today_tokens[16] = {};
    compact_number(today_tokens, sizeof(today_tokens), snapshot.today_tokens);
    const char *currency = snapshot.currency;
    const char *symbol = (std::strcmp(currency, "USD") == 0) ? "$" : currency;
    const bool usage_only = std::strcmp(snapshot.status, "usage_only") == 0;
    if ((usage_only || snapshot.balance <= 0.0) && snapshot.total_cost > 0.0) {
        std::snprintf(
            buffer,
            buffer_size,
            "M %s%.2f  T %s%.2f/%s tok",
            symbol,
            snapshot.total_cost,
            symbol,
            snapshot.today_cost,
            today_tokens
        );
    } else {
        std::snprintf(
            buffer,
            buffer_size,
            "%s %.2f  T %s%.2f/%s tok",
            symbol,
            snapshot.balance,
            symbol,
            snapshot.today_cost,
            today_tokens
        );
    }
}

bool quota_refresh_is_running()
{
    return s_refresh_task != nullptr;
}

esp_err_t quota_refresh_async()
{
    if (s_refresh_task != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    BaseType_t ok = xTaskCreateWithCaps(
        refresh_task,
        "quota_home",
        REFRESH_TASK_STACK_SIZE,
        nullptr,
        4,
        &s_refresh_task,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    if (ok != pdPASS) {
        s_refresh_task = nullptr;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

} // namespace watch
