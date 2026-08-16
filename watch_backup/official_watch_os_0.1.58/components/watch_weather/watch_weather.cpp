#include "watch_weather.hpp"

#include <cstdio>
#include <cstring>
#include <string>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "watch_connectivity.hpp"

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "WatchWeather"

namespace watch {
namespace {

constexpr const char *LOGIN_URL = "http://<OTA_SERVER>/DUDUSERVER/api/v1/auth/login";
constexpr const char *WEATHER_URL = "http://<OTA_SERVER>/DUDUSERVER/api/v1/weather?compact=true";
constexpr const char *USER_AGENT = "esp32-s3-watch-weather";
constexpr int HTTP_TIMEOUT_MS = 12000;
constexpr size_t MAX_HTTP_BODY = 24 * 1024;
constexpr uint32_t WEATHER_HOME_TASK_STACK_SIZE = 8192;

SemaphoreHandle_t s_mutex = nullptr;
WeatherSnapshot s_snapshot;

struct HttpResponse {
    int status = 0;
    std::string body;
};

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

esp_err_t fill_snapshot_from_response(const std::string &body, WeatherSnapshot *snapshot)
{
    cJSON *root = cJSON_Parse(body.c_str());
    if (root == nullptr) {
        return ESP_FAIL;
    }

    const std::string city = json_string(root, "city", "-");
    const std::string provider = json_string(root, "provider", "-");
    const std::string timezone = json_string(root, "timezone", "-");
    cJSON *current = cJSON_GetObjectItemCaseSensitive(root, "current");
    cJSON *daily = cJSON_GetObjectItemCaseSensitive(root, "daily");
    cJSON *today = cJSON_IsArray(daily) ? cJSON_GetArrayItem(daily, 0) : nullptr;

    if (snapshot != nullptr) {
        std::memset(snapshot, 0, sizeof(*snapshot));
        snapshot->valid = true;
        snapshot->ok = true;
        std::snprintf(snapshot->city, sizeof(snapshot->city), "%s", city.c_str());
        std::snprintf(snapshot->source, sizeof(snapshot->source), "%s", provider.c_str());
        std::snprintf(snapshot->timezone, sizeof(snapshot->timezone), "%s", timezone.c_str());
    }

    if (cJSON_IsObject(current) && snapshot != nullptr) {
        snapshot->temperature_c = static_cast<float>(json_double(current, "temperature_2m", json_double(current, "temperature_c", 0.0)));
        snapshot->apparent_c = static_cast<float>(json_double(current, "apparent_temperature", 0.0));
        snapshot->humidity = static_cast<float>(json_double(current, "relative_humidity_2m", 0.0));
        snapshot->wind_kmh = static_cast<float>(json_double(current, "wind_speed_10m", json_double(current, "wind_speed_kmh", 0.0)));
        snapshot->gust_kmh = static_cast<float>(json_double(current, "wind_gusts_10m", 0.0));
        snapshot->precipitation_mm = static_cast<float>(json_double(current, "precipitation", 0.0));
        snapshot->rain_prob = json_int(current, "precipitation_probability", -1);
        std::snprintf(snapshot->condition, sizeof(snapshot->condition), "%s", weather_text_for_ui(json_string(current, "weather_text", "-")).c_str());
        std::snprintf(snapshot->wind_dir, sizeof(snapshot->wind_dir), "%s", wind_text_for_ui(json_string(current, "wind_direction_text", "-")).c_str());
        std::snprintf(snapshot->current_time, sizeof(snapshot->current_time), "%s", json_string(current, "time", "-").c_str());
    }

    if (cJSON_IsObject(today) && snapshot != nullptr) {
        snapshot->temp_max_c = static_cast<float>(json_double(today, "temperature_2m_max", snapshot->temperature_c));
        snapshot->temp_min_c = static_cast<float>(json_double(today, "temperature_2m_min", snapshot->temperature_c));
        if (snapshot->rain_prob < 0) {
            snapshot->rain_prob = json_int(today, "precipitation_probability_max", -1);
        }
    }

    if (snapshot != nullptr) {
        snapshot->last_refresh_us = esp_timer_get_time();
    }
    cJSON_Delete(root);
    return ESP_OK;
}

void update_cache_from_snapshot(const WeatherSnapshot &snapshot, bool running, bool ok)
{
    if (s_mutex != nullptr && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        s_snapshot = snapshot;
        s_snapshot.running = running;
        s_snapshot.ok = ok;
        xSemaphoreGive(s_mutex);
    }
}

void refresh_task(void *arg)
{
    (void)arg;
    WeatherSnapshot snapshot;
    std::memset(&snapshot, 0, sizeof(snapshot));

    if (!watch::wifi_is_connected()) {
        if (s_mutex != nullptr && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
            s_snapshot.running = false;
            s_snapshot.ok = false;
            s_snapshot.valid = false;
            s_snapshot.last_refresh_us = esp_timer_get_time();
            xSemaphoreGive(s_mutex);
        }
        vTaskDelete(nullptr);
        return;
    }

    std::string token = login_token();
    if (token.empty()) {
        ESP_LOGW(ESP_UTILS_LOG_TAG, "weather refresh login failed");
        if (s_mutex != nullptr && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
            s_snapshot.running = false;
            s_snapshot.ok = false;
            s_snapshot.valid = false;
            s_snapshot.last_refresh_us = esp_timer_get_time();
            xSemaphoreGive(s_mutex);
        }
        vTaskDelete(nullptr);
        return;
    }

    HttpResponse response;
    bool ok = http_request(HTTP_METHOD_GET, WEATHER_URL, token.c_str(), nullptr, response);
    if (!ok) {
        ESP_LOGW(ESP_UTILS_LOG_TAG, "weather refresh HTTP failed: %d", response.status);
        if (s_mutex != nullptr && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
            s_snapshot.running = false;
            s_snapshot.ok = false;
            s_snapshot.valid = false;
            s_snapshot.last_refresh_us = esp_timer_get_time();
            xSemaphoreGive(s_mutex);
        }
        vTaskDelete(nullptr);
        return;
    }

    if (fill_snapshot_from_response(response.body, &snapshot) != ESP_OK) {
        ESP_LOGW(ESP_UTILS_LOG_TAG, "weather refresh JSON parse failed");
        if (s_mutex != nullptr && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
            s_snapshot.running = false;
            s_snapshot.ok = false;
            s_snapshot.valid = false;
            s_snapshot.last_refresh_us = esp_timer_get_time();
            xSemaphoreGive(s_mutex);
        }
        vTaskDelete(nullptr);
        return;
    }

    ESP_LOGI(ESP_UTILS_LOG_TAG, "weather refresh ok city=%s temp=%.1f rain=%d", snapshot.city, snapshot.temperature_c, snapshot.rain_prob);
    update_cache_from_snapshot(snapshot, false, true);
    vTaskDelete(nullptr);
}

} // namespace

esp_err_t weather_fetch(WeatherSnapshot *snapshot, char *error, size_t error_size)
{
    if (snapshot == nullptr) {
        if (error != nullptr && error_size > 0) {
            std::snprintf(error, error_size, "invalid snapshot");
        }
        return ESP_ERR_INVALID_ARG;
    }
    if (!watch::wifi_is_connected()) {
        if (error != nullptr && error_size > 0) {
            std::snprintf(error, error_size, "WiFi not connected");
        }
        return ESP_ERR_INVALID_STATE;
    }

    std::string token = login_token();
    if (token.empty()) {
        if (error != nullptr && error_size > 0) {
            std::snprintf(error, error_size, "login failed");
        }
        return ESP_FAIL;
    }

    HttpResponse response;
    if (!http_request(HTTP_METHOD_GET, WEATHER_URL, token.c_str(), nullptr, response)) {
        if (error != nullptr && error_size > 0) {
            std::snprintf(error, error_size, "weather http %d", response.status);
        }
        return ESP_FAIL;
    }

    esp_err_t err = fill_snapshot_from_response(response.body, snapshot);
    if (err != ESP_OK) {
        if (error != nullptr && error_size > 0) {
            std::snprintf(error, error_size, "weather json parse failed");
        }
        return err;
    }

    if (s_mutex != nullptr && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        s_snapshot = *snapshot;
        s_snapshot.running = false;
        s_snapshot.ok = true;
        xSemaphoreGive(s_mutex);
    }
    return ESP_OK;
}

bool weather_refresh_async()
{
    if (s_mutex == nullptr) {
        s_mutex = xSemaphoreCreateMutex();
    }
    if (s_mutex != nullptr && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (s_snapshot.running) {
            xSemaphoreGive(s_mutex);
            return false;
        }
        s_snapshot.running = true;
        xSemaphoreGive(s_mutex);
    }
    const bool created = xTaskCreateWithCaps(
        refresh_task,
        "watch_weather",
        WEATHER_HOME_TASK_STACK_SIZE,
        nullptr,
        5,
        nullptr,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    ) == pdPASS;
    if (!created && s_mutex != nullptr && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        s_snapshot.running = false;
        s_snapshot.ok = false;
        s_snapshot.last_refresh_us = esp_timer_get_time();
        xSemaphoreGive(s_mutex);
        ESP_LOGW(ESP_UTILS_LOG_TAG, "weather task create failed");
    }
    return created;
}

bool weather_snapshot(WeatherSnapshot *snapshot)
{
    if (snapshot == nullptr) {
        return false;
    }
    if (s_mutex != nullptr && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        *snapshot = s_snapshot;
        xSemaphoreGive(s_mutex);
        return true;
    }
    return false;
}

bool weather_is_running()
{
    bool running = false;
    if (s_mutex != nullptr && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        running = s_snapshot.running;
        xSemaphoreGive(s_mutex);
    }
    return running;
}

void weather_format_home(const WeatherSnapshot &snapshot, char *buffer, size_t buffer_size)
{
    if (buffer == nullptr || buffer_size == 0) {
        return;
    }
    if (!snapshot.valid) {
        std::snprintf(buffer, buffer_size, "weather --");
        return;
    }
    const int rain = snapshot.rain_prob >= 0 ? snapshot.rain_prob : 0;
    const int low = static_cast<int>(snapshot.temp_min_c + 0.5f);
    const int current = static_cast<int>(snapshot.temperature_c + 0.5f);
    const int high = static_cast<int>(snapshot.temp_max_c + 0.5f);
    std::snprintf(buffer, buffer_size, "%s %d%%  %d/%d/%d", snapshot.condition[0] != '\0' ? snapshot.condition : "weather", rain, low, current, high);
}

} // namespace watch
