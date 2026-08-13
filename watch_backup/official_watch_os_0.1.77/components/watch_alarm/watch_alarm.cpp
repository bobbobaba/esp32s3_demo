#include "watch_alarm.hpp"

#include <cstdio>
#include <ctime>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace watch {
namespace {

constexpr const char *TAG = "watch_alarm";
constexpr const char *NVS_NAMESPACE = "watch_alarm";
constexpr const char *NVS_KEY_ENABLED = "enabled";
constexpr const char *NVS_KEY_REPEAT = "repeat";
constexpr const char *NVS_KEY_HOUR = "hour";
constexpr const char *NVS_KEY_MINUTE = "minute";
constexpr const char *NVS_KEY_LAST_YDAY = "last_yday";

AlarmState s_state;
SemaphoreHandle_t s_mutex = nullptr;
bool s_init_done = false;

bool normalize_time(int *hour, int *minute)
{
    if ((hour == nullptr) || (minute == nullptr)) {
        return false;
    }
    if ((*hour < 0) || (*hour > 23) || (*minute < 0) || (*minute > 59)) {
        return false;
    }
    return true;
}

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

esp_err_t save_state_locked()
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_u8(handle, NVS_KEY_ENABLED, s_state.enabled ? 1 : 0);
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, NVS_KEY_REPEAT, s_state.repeat_daily ? 1 : 0);
    }
    if (err == ESP_OK) {
        err = nvs_set_i32(handle, NVS_KEY_HOUR, s_state.hour);
    }
    if (err == ESP_OK) {
        err = nvs_set_i32(handle, NVS_KEY_MINUTE, s_state.minute);
    }
    if (err == ESP_OK) {
        err = nvs_set_i32(handle, NVS_KEY_LAST_YDAY, s_state.last_fire_yday);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS save failed: %s", esp_err_to_name(err));
    }
    return err;
}

void load_state_locked()
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No saved alarm, using default 07:30");
        return;
    }

    uint8_t enabled = s_state.enabled ? 1 : 0;
    uint8_t repeat = s_state.repeat_daily ? 1 : 0;
    int32_t hour = s_state.hour;
    int32_t minute = s_state.minute;
    int32_t last_yday = s_state.last_fire_yday;

    if (nvs_get_u8(handle, NVS_KEY_ENABLED, &enabled) == ESP_OK) {
        s_state.enabled = enabled != 0;
    }
    if (nvs_get_u8(handle, NVS_KEY_REPEAT, &repeat) == ESP_OK) {
        s_state.repeat_daily = repeat != 0;
    }
    if ((nvs_get_i32(handle, NVS_KEY_HOUR, &hour) == ESP_OK) && (hour >= 0) && (hour <= 23)) {
        s_state.hour = static_cast<int>(hour);
    }
    if ((nvs_get_i32(handle, NVS_KEY_MINUTE, &minute) == ESP_OK) && (minute >= 0) && (minute <= 59)) {
        s_state.minute = static_cast<int>(minute);
    }
    if (nvs_get_i32(handle, NVS_KEY_LAST_YDAY, &last_yday) == ESP_OK) {
        s_state.last_fire_yday = static_cast<int>(last_yday);
    }
    s_state.ringing = false;
    nvs_close(handle);
}

bool system_time_valid(const struct tm &tm)
{
    return tm.tm_year >= (2024 - 1900);
}

} // namespace

esp_err_t alarm_init()
{
    if (s_init_done) {
        return ESP_OK;
    }
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    lock();
    load_state_locked();
    s_init_done = true;
    unlock();
    return ESP_OK;
}

esp_err_t alarm_set(int hour, int minute, bool enabled, bool repeat_daily)
{
    if (!normalize_time(&hour, &minute)) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(alarm_init(), TAG, "alarm init failed");

    lock();
    const bool time_changed = (s_state.hour != hour) || (s_state.minute != minute);
    s_state.hour = hour;
    s_state.minute = minute;
    s_state.enabled = enabled;
    s_state.repeat_daily = repeat_daily;
    if (time_changed || !enabled) {
        s_state.ringing = false;
    }
    if (time_changed) {
        s_state.last_fire_yday = -1;
    }
    esp_err_t err = save_state_locked();
    unlock();
    return err;
}

void alarm_get_state(AlarmState *state)
{
    if (state == nullptr) {
        return;
    }
    if (alarm_init() != ESP_OK) {
        *state = AlarmState{};
        return;
    }
    lock();
    *state = s_state;
    unlock();
}

bool alarm_poll()
{
    if (alarm_init() != ESP_OK) {
        return false;
    }

    time_t now = 0;
    time(&now);
    struct tm tm = {};
    localtime_r(&now, &tm);
    if (!system_time_valid(tm)) {
        return false;
    }

    bool fired = false;
    lock();
    if (s_state.enabled &&
        !s_state.ringing &&
        (tm.tm_hour == s_state.hour) &&
        (tm.tm_min == s_state.minute) &&
        (s_state.last_fire_yday != tm.tm_yday)) {
        s_state.ringing = true;
        s_state.last_fire_yday = tm.tm_yday;
        if (!s_state.repeat_daily) {
            s_state.enabled = false;
        }
        save_state_locked();
        fired = true;
        ESP_LOGI(TAG, "Alarm fired at %02d:%02d", s_state.hour, s_state.minute);
    }
    unlock();
    return fired;
}

void alarm_stop()
{
    if (alarm_init() != ESP_OK) {
        return;
    }
    lock();
    s_state.ringing = false;
    unlock();
}

void alarm_status_text(char *buffer, size_t buffer_size)
{
    if ((buffer == nullptr) || (buffer_size == 0)) {
        return;
    }
    AlarmState state = {};
    alarm_get_state(&state);
    std::snprintf(
        buffer,
        buffer_size,
        "%02d:%02d  %s\nRepeat: %s%s",
        state.hour,
        state.minute,
        state.enabled ? "Enabled" : "Off",
        state.repeat_daily ? "Daily" : "Once",
        state.ringing ? "\nRINGING" : ""
    );
}

} // namespace watch
