#include "watch_display.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "watch_connectivity.hpp"
#include "watch_sensor.hpp"

namespace watch {
namespace {

constexpr const char *TAG = "watch_display";
constexpr const char *NVS_NAMESPACE = "watch_display";
constexpr const char *NVS_KEY_TIMEOUT = "timeout";
constexpr const char *NVS_KEY_BRIGHTNESS = "brightness";
constexpr const char *NVS_KEY_RAISE = "raise";
constexpr int DEFAULT_TIMEOUT_S = 15;
constexpr int MIN_TIMEOUT_S = 5;
constexpr int MAX_TIMEOUT_S = 300;
constexpr int DEFAULT_BRIGHTNESS_PERCENT = 55;
constexpr int MIN_BRIGHTNESS_PERCENT = 5;
constexpr int MAX_BRIGHTNESS_PERCENT = 100;
constexpr int MOTION_WAKE_COOLDOWN_MS = 4000;
constexpr int WRIST_GESTURE_ACCEL_DELTA_MIN = 900;
constexpr int WRIST_GESTURE_GYRO_SUM_MIN = 2600;
constexpr int WRIST_GESTURE_SCORE_MIN = 2400;
constexpr int WRIST_GESTURE_WINDOW_MS = 1800;
constexpr int WRIST_STABLE_ACCEL_DELTA_MAX = 950;
constexpr int WRIST_STABLE_GYRO_SUM_MAX = 3600;
constexpr int WRIST_STABLE_REQUIRED_SAMPLES = 2;
constexpr int RUNNING_ACCEL_DELTA_MAX = 9500;
constexpr int RUNNING_GYRO_SUM_MAX = 26000;
constexpr int RUNNING_BLOCK_MS = 900;
constexpr int MOTION_DIAGNOSTIC_INTERVAL_MS = 10000;
constexpr int MOTION_TASK_ACTIVE_INTERVAL_MS = 100;
constexpr int MOTION_TASK_IDLE_INTERVAL_MS = 1000;

DisplaySettings s_settings = {};
bool s_loaded = false;
bool s_display_on = true;
bool s_motion_task_started = false;
volatile bool s_motion_wake_pending = false;
int64_t s_last_activity_ms = 0;
int64_t s_last_motion_wake_ms = 0;
int64_t s_last_motion_diag_ms = 0;
int16_t s_last_acc[3] = {};
bool s_have_acc_sample = false;
bool s_touch_was_pressed = false;
bool s_wrist_candidate = false;
int64_t s_wrist_candidate_until_ms = 0;
int64_t s_running_block_until_ms = 0;
int s_wrist_stable_samples = 0;
int s_last_delta = 0;
int s_last_gyro_sum = 0;
int s_last_motion_score = 0;
esp_err_t s_last_sensor_err = ESP_ERR_INVALID_STATE;
char s_last_wake_reason[64] = "idle";

int clamp_timeout(int seconds)
{
    return std::clamp(seconds, MIN_TIMEOUT_S, MAX_TIMEOUT_S);
}

int clamp_brightness(int percent)
{
    return std::clamp(percent, MIN_BRIGHTNESS_PERCENT, MAX_BRIGHTNESS_PERCENT);
}

bool load_nvs()
{
    if (s_loaded) {
        return true;
    }
    s_settings.screen_timeout_s = DEFAULT_TIMEOUT_S;
    s_settings.brightness_percent = DEFAULT_BRIGHTNESS_PERCENT;
    s_settings.raise_wake_enabled = false;

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        s_loaded = true;
        return false;
    }

    int32_t timeout = 0;
    if (nvs_get_i32(handle, NVS_KEY_TIMEOUT, &timeout) == ESP_OK) {
        s_settings.screen_timeout_s = clamp_timeout(static_cast<int>(timeout));
    }

    int32_t brightness = 0;
    if (nvs_get_i32(handle, NVS_KEY_BRIGHTNESS, &brightness) == ESP_OK) {
        s_settings.brightness_percent = clamp_brightness(static_cast<int>(brightness));
    }

    uint8_t raise = 0;
    if (nvs_get_u8(handle, NVS_KEY_RAISE, &raise) == ESP_OK) {
        s_settings.raise_wake_enabled = raise != 0;
    }

    nvs_close(handle);
    s_loaded = true;
    return true;
}

esp_err_t save_nvs()
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_i32(handle, NVS_KEY_TIMEOUT, s_settings.screen_timeout_s);
    if (err == ESP_OK) {
        err = nvs_set_i32(handle, NVS_KEY_BRIGHTNESS, s_settings.brightness_percent);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, NVS_KEY_RAISE, s_settings.raise_wake_enabled ? 1 : 0);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

int64_t now_ms()
{
    return esp_timer_get_time() / 1000;
}

int motion_delta(const SensorState &state)
{
    int delta = 0;
    for (int i = 0; i < 3; ++i) {
        delta += std::abs(state.acc_raw[i] - s_last_acc[i]);
    }
    return delta;
}

int motion_gyro_sum(const SensorState &state)
{
    int sum = 0;
    for (int i = 0; i < 3; ++i) {
        sum += std::abs(state.gyro_raw[i]);
    }
    return sum;
}

bool touch_active(lv_indev_t *indev)
{
    return (indev != nullptr) && (lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED);
}

void reset_wrist_candidate()
{
    s_wrist_candidate = false;
    s_wrist_candidate_until_ms = 0;
    s_wrist_stable_samples = 0;
}

void process_motion_sample(const SensorState &sensor)
{
    const int64_t now = now_ms();
    int delta = 0;
    int gyro_sum = 0;
    int motion_score = 0;
    if (s_have_acc_sample) {
        delta = motion_delta(sensor);
        gyro_sum = motion_gyro_sum(sensor);
        motion_score = delta + (gyro_sum / 2);
    }
    for (int i = 0; i < 3; ++i) {
        s_last_acc[i] = sensor.acc_raw[i];
    }
    s_have_acc_sample = true;
    s_last_delta = delta;
    s_last_gyro_sum = gyro_sum;
    s_last_motion_score = motion_score;

    if ((now - s_last_motion_diag_ms) >= MOTION_DIAGNOSTIC_INTERVAL_MS) {
        s_last_motion_diag_ms = now;
        ESP_LOGI(
            TAG,
            "IMU wake sample acc=%d,%d,%d gyro=%d,%d,%d delta=%d gyro_sum=%d score=%d candidate=%d stable=%d block_ms=%lld",
            sensor.acc_raw[0], sensor.acc_raw[1], sensor.acc_raw[2],
            sensor.gyro_raw[0], sensor.gyro_raw[1], sensor.gyro_raw[2],
            delta, gyro_sum, motion_score,
            s_wrist_candidate ? 1 : 0,
            s_wrist_stable_samples,
            static_cast<long long>(std::max<int64_t>(0, s_running_block_until_ms - now))
        );
    }

    const bool running_like_motion =
        (delta >= RUNNING_ACCEL_DELTA_MAX) ||
        (gyro_sum >= RUNNING_GYRO_SUM_MAX);
    if (running_like_motion) {
        s_running_block_until_ms = now + RUNNING_BLOCK_MS;
        reset_wrist_candidate();
        std::snprintf(s_last_wake_reason, sizeof(s_last_wake_reason), "blocked: running");
        return;
    }

    const bool blocked_by_running = now < s_running_block_until_ms;
    const bool wrist_gesture_started =
        !blocked_by_running &&
        (
            (delta >= WRIST_GESTURE_ACCEL_DELTA_MIN) ||
            (gyro_sum >= WRIST_GESTURE_GYRO_SUM_MIN) ||
            (motion_score >= WRIST_GESTURE_SCORE_MIN)
        );
    if (wrist_gesture_started) {
        s_wrist_candidate = true;
        s_wrist_candidate_until_ms = now + WRIST_GESTURE_WINDOW_MS;
        s_wrist_stable_samples = 0;
        std::snprintf(s_last_wake_reason, sizeof(s_last_wake_reason), "candidate");
    }

    if (s_wrist_candidate && (now > s_wrist_candidate_until_ms)) {
        reset_wrist_candidate();
        std::snprintf(s_last_wake_reason, sizeof(s_last_wake_reason), "expired");
    }

    const bool wrist_is_stable =
        s_wrist_candidate &&
        !blocked_by_running &&
        (delta <= WRIST_STABLE_ACCEL_DELTA_MAX) &&
        (gyro_sum <= WRIST_STABLE_GYRO_SUM_MAX);
    if (wrist_is_stable) {
        s_wrist_stable_samples = std::min(s_wrist_stable_samples + 1, WRIST_STABLE_REQUIRED_SAMPLES);
    } else if (!blocked_by_running) {
        s_wrist_stable_samples = 0;
    }

    if (
        (s_wrist_stable_samples >= WRIST_STABLE_REQUIRED_SAMPLES) &&
        (now - s_last_motion_wake_ms >= MOTION_WAKE_COOLDOWN_MS)
    ) {
        s_last_motion_wake_ms = now;
        reset_wrist_candidate();
        std::snprintf(s_last_wake_reason, sizeof(s_last_wake_reason), "woke");
        ESP_LOGI(TAG, "Wrist wake delta=%d gyro_sum=%d score=%d", delta, gyro_sum, motion_score);
        s_motion_wake_pending = true;
    }
}

void motion_task(void *)
{
    while (true) {
        load_nvs();
        if (!s_settings.raise_wake_enabled || s_display_on) {
            reset_wrist_candidate();
            s_have_acc_sample = false;
            s_last_sensor_err = s_settings.raise_wake_enabled ? ESP_ERR_INVALID_STATE : ESP_ERR_INVALID_STATE;
            vTaskDelay(pdMS_TO_TICKS(MOTION_TASK_IDLE_INTERVAL_MS));
            continue;
        }

        SensorState sensor = {};
        s_last_sensor_err = sensor_read(&sensor);
        if (s_last_sensor_err == ESP_OK) {
            process_motion_sample(sensor);
        } else {
            std::snprintf(s_last_wake_reason, sizeof(s_last_wake_reason), "imu: %s", esp_err_to_name(s_last_sensor_err));
        }
        vTaskDelay(pdMS_TO_TICKS(MOTION_TASK_ACTIVE_INTERVAL_MS));
    }
}

void start_motion_task_if_needed()
{
    if (s_motion_task_started) {
        return;
    }
    s_motion_task_started = xTaskCreate(motion_task, "wrist_wake", 4096, nullptr, 3, nullptr) == pdPASS;
    if (!s_motion_task_started) {
        ESP_LOGW(TAG, "Failed to start wrist wake task");
    }
}

} // namespace

esp_err_t display_settings_init()
{
    load_nvs();
    s_last_activity_ms = now_ms();
    s_display_on = true;
    ESP_ERROR_CHECK_WITHOUT_ABORT(bsp_display_brightness_set(s_settings.brightness_percent));
    if (s_settings.raise_wake_enabled) {
        start_motion_task_if_needed();
    }
    return ESP_OK;
}

DisplaySettings display_get_settings()
{
    load_nvs();
    return s_settings;
}

esp_err_t display_set_screen_timeout_s(int seconds)
{
    load_nvs();
    s_settings.screen_timeout_s = clamp_timeout(seconds);
    esp_err_t err = save_nvs();
    ESP_LOGI(TAG, "Screen timeout set to %d s", s_settings.screen_timeout_s);
    return err;
}

esp_err_t display_set_brightness_percent(int percent)
{
    load_nvs();
    s_settings.brightness_percent = clamp_brightness(percent);
    if (s_display_on) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(bsp_display_brightness_set(s_settings.brightness_percent));
    }
    esp_err_t err = save_nvs();
    ESP_LOGI(TAG, "Brightness set to %d%%", s_settings.brightness_percent);
    return err;
}

esp_err_t display_set_raise_wake_enabled(bool enabled)
{
    load_nvs();
    s_settings.raise_wake_enabled = enabled;
    s_wrist_candidate = false;
    s_wrist_candidate_until_ms = 0;
    s_wrist_stable_samples = 0;
    s_have_acc_sample = false;
    std::snprintf(s_last_wake_reason, sizeof(s_last_wake_reason), enabled ? "enabled" : "disabled");
    esp_err_t err = save_nvs();
    if (enabled) {
        start_motion_task_if_needed();
    }
    ESP_LOGI(TAG, "Raise wake %s", enabled ? "enabled" : "disabled");
    return err;
}

void display_notify_activity()
{
    s_last_activity_ms = now_ms();
    if (!s_display_on) {
        display_wake();
    }
}

void display_wake()
{
    if (!s_display_on) {
        load_nvs();
        ESP_ERROR_CHECK_WITHOUT_ABORT(bsp_display_brightness_set(s_settings.brightness_percent));
        s_display_on = true;
        esp_err_t wifi_err = wifi_resume_from_power_save();
        if (wifi_err != ESP_OK) {
            ESP_LOGW(TAG, "Resume WiFi after display wake failed: %s", esp_err_to_name(wifi_err));
        }
    }
    s_last_activity_ms = now_ms();
}

void display_sleep()
{
    if (s_display_on) {
        bsp_display_backlight_off();
        s_display_on = false;
        esp_err_t wifi_err = wifi_suspend_for_power_save();
        if (wifi_err != ESP_OK) {
            ESP_LOGW(TAG, "Pause WiFi for display sleep failed: %s", esp_err_to_name(wifi_err));
        }
    }
}

bool display_is_on()
{
    return s_display_on;
}

void display_tick(lv_indev_t *indev)
{
    load_nvs();

    const int64_t now = now_ms();
    const bool pressed = touch_active(indev);
    if (pressed) {
        if (!s_touch_was_pressed) {
            ESP_LOGD(TAG, "Touch activity detected");
        }
        display_notify_activity();
    }
    s_touch_was_pressed = pressed;

    if (s_display_on) {
        const int timeout_ms = s_settings.screen_timeout_s * 1000;
        if ((timeout_ms > 0) && (now - s_last_activity_ms >= timeout_ms) && !pressed) {
            display_sleep();
        }
    }

    if (!s_display_on && s_motion_wake_pending) {
        s_motion_wake_pending = false;
        display_wake();
    }
}

void display_status_text(char *buffer, size_t buffer_size)
{
    if ((buffer == nullptr) || (buffer_size == 0)) {
        return;
    }
    load_nvs();
    std::snprintf(
        buffer,
        buffer_size,
        "Timeout: %ds  Brightness: %d%%\nRaise wake: %s\nBacklight: %s\nIMU: %s\nMotion: d%d g%d s%d\nWake: %s",
        s_settings.screen_timeout_s,
        s_settings.brightness_percent,
        s_settings.raise_wake_enabled ? "on" : "off",
        s_display_on ? "on" : "off",
        s_settings.raise_wake_enabled ? esp_err_to_name(s_last_sensor_err) : "disabled",
        s_last_delta,
        s_last_gyro_sum,
        s_last_motion_score,
        s_last_wake_reason
    );
}

} // namespace watch
