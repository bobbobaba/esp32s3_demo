#include "watch_time.hpp"

#include <cstdio>
#include <cstring>
#include <sys/time.h>

#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "watch_connectivity.hpp"

namespace watch {
namespace {

constexpr const char *TAG = "watch_time";
constexpr uint8_t PCF85063_ADDR = 0x51;
constexpr uint32_t I2C_SPEED_HZ = 400000;
constexpr int I2C_TIMEOUT_MS = 200;

enum {
    PCF85063_REG_CTRL1 = 0x00,
    PCF85063_REG_SECONDS = 0x04,
};

i2c_master_dev_handle_t s_rtc = nullptr;
bool s_init_done = false;
bool s_rtc_available = false;
bool s_sntp_started = false;
bool s_sntp_synced = false;

uint8_t bcd_to_bin(uint8_t value)
{
    return static_cast<uint8_t>(((value >> 4) * 10) + (value & 0x0f));
}

uint8_t bin_to_bcd(int value)
{
    return static_cast<uint8_t>(((value / 10) << 4) | (value % 10));
}

bool tm_is_valid(const struct tm &tm)
{
    return (tm.tm_year >= (2024 - 1900)) && (tm.tm_year <= (2099 - 1900)) &&
           (tm.tm_mon >= 0) && (tm.tm_mon <= 11) &&
           (tm.tm_mday >= 1) && (tm.tm_mday <= 31) &&
           (tm.tm_hour >= 0) && (tm.tm_hour <= 23) &&
           (tm.tm_min >= 0) && (tm.tm_min <= 59) &&
           (tm.tm_sec >= 0) && (tm.tm_sec <= 59);
}

esp_err_t rtc_read_regs(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(s_rtc, &reg, sizeof(reg), data, len, I2C_TIMEOUT_MS);
}

esp_err_t rtc_write_regs(uint8_t reg, const uint8_t *data, size_t len)
{
    uint8_t buffer[16] = {};
    ESP_RETURN_ON_FALSE(len + 1 <= sizeof(buffer), ESP_ERR_INVALID_SIZE, TAG, "RTC write too large");
    buffer[0] = reg;
    std::memcpy(&buffer[1], data, len);
    return i2c_master_transmit(s_rtc, buffer, len + 1, I2C_TIMEOUT_MS);
}

esp_err_t rtc_init()
{
    if (s_rtc_available) {
        return ESP_OK;
    }
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    ESP_RETURN_ON_FALSE(bus != nullptr, ESP_FAIL, TAG, "I2C bus unavailable");

    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PCF85063_ADDR,
        .scl_speed_hz = I2C_SPEED_HZ,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &s_rtc);
    if (err != ESP_OK) {
        s_rtc = nullptr;
        ESP_LOGW(TAG, "PCF85063 add device failed: %s", esp_err_to_name(err));
        return err;
    }

    uint8_t ctrl1 = 0;
    err = rtc_read_regs(PCF85063_REG_CTRL1, &ctrl1, sizeof(ctrl1));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "PCF85063 probe failed: %s", esp_err_to_name(err));
        i2c_master_bus_rm_device(s_rtc);
        s_rtc = nullptr;
        return err;
    }
    s_rtc_available = true;
    ESP_LOGI(TAG, "PCF85063 detected at 0x%02x", PCF85063_ADDR);
    return ESP_OK;
}

esp_err_t rtc_read_time(time_t *out)
{
    ESP_RETURN_ON_FALSE(out != nullptr, ESP_ERR_INVALID_ARG, TAG, "Invalid time pointer");
    ESP_RETURN_ON_ERROR(rtc_init(), TAG, "RTC init failed");

    uint8_t data[7] = {};
    ESP_RETURN_ON_ERROR(rtc_read_regs(PCF85063_REG_SECONDS, data, sizeof(data)), TAG, "RTC read time failed");

    struct tm tm = {};
    tm.tm_sec = bcd_to_bin(data[0] & 0x7f);
    tm.tm_min = bcd_to_bin(data[1] & 0x7f);
    tm.tm_hour = bcd_to_bin(data[2] & 0x3f);
    tm.tm_mday = bcd_to_bin(data[3] & 0x3f);
    tm.tm_mon = bcd_to_bin(data[5] & 0x1f) - 1;
    tm.tm_year = bcd_to_bin(data[6]) + 100; // 20xx
    tm.tm_isdst = -1;

    ESP_RETURN_ON_FALSE(tm_is_valid(tm), ESP_ERR_INVALID_RESPONSE, TAG, "RTC time invalid");
    time_t value = mktime(&tm);
    ESP_RETURN_ON_FALSE(value > 0, ESP_ERR_INVALID_RESPONSE, TAG, "RTC mktime failed");
    *out = value;
    return ESP_OK;
}

void on_sntp_sync(struct timeval *tv)
{
    s_sntp_synced = true;
    ESP_LOGI(TAG, "SNTP synced");
    if ((tv != nullptr) && (tv->tv_sec > 0)) {
        time_write_rtc_from_system();
    }
}

} // namespace

esp_err_t time_service_init()
{
    if (s_init_done) {
        return ESP_OK;
    }
    s_init_done = true;
    setenv("TZ", "CST-8", 1);
    tzset();

    esp_err_t rtc_err = rtc_init();
    if (rtc_err == ESP_OK) {
        time_sync_from_rtc();
    }
    time_start_sntp_if_needed();
    return ESP_OK;
}

esp_err_t time_sync_from_rtc()
{
    time_t value = 0;
    ESP_RETURN_ON_ERROR(rtc_read_time(&value), TAG, "read RTC failed");
    struct timeval tv = {
        .tv_sec = value,
        .tv_usec = 0,
    };
    return settimeofday(&tv, nullptr) == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t time_write_rtc_from_system()
{
    ESP_RETURN_ON_ERROR(rtc_init(), TAG, "RTC init failed");

    time_t now = 0;
    time(&now);
    struct tm tm = {};
    localtime_r(&now, &tm);
    ESP_RETURN_ON_FALSE(tm_is_valid(tm), ESP_ERR_INVALID_STATE, TAG, "System time invalid");

    uint8_t data[7] = {
        bin_to_bcd(tm.tm_sec),
        bin_to_bcd(tm.tm_min),
        bin_to_bcd(tm.tm_hour),
        bin_to_bcd(tm.tm_mday),
        bin_to_bcd(tm.tm_wday),
        bin_to_bcd(tm.tm_mon + 1),
        bin_to_bcd((tm.tm_year + 1900) % 100),
    };
    return rtc_write_regs(PCF85063_REG_SECONDS, data, sizeof(data));
}

esp_err_t time_start_sntp_if_needed()
{
    if (s_sntp_started || esp_sntp_enabled()) {
        s_sntp_started = true;
        return ESP_OK;
    }
    if (!wifi_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "ntp.aliyun.com");
    esp_sntp_setservername(2, "time.windows.com");
    esp_sntp_set_time_sync_notification_cb(on_sntp_sync);
    esp_sntp_init();
    s_sntp_started = true;
    return ESP_OK;
}

bool time_is_valid()
{
    time_t now = 0;
    time(&now);
    struct tm tm = {};
    localtime_r(&now, &tm);
    return tm.tm_year >= (2024 - 1900);
}

void time_poll()
{
    if (!time_is_valid()) {
        time_sync_from_rtc();
    }
    if (wifi_is_connected()) {
        time_start_sntp_if_needed();
    }
}

esp_err_t time_get_state(TimeState *state)
{
    ESP_RETURN_ON_FALSE(state != nullptr, ESP_ERR_INVALID_ARG, TAG, "Invalid state pointer");
    time_service_init();

    TimeState next = {};
    time(&next.now);
    struct tm tm = {};
    localtime_r(&next.now, &tm);
    next.system_valid = tm.tm_year >= (2024 - 1900);
    next.rtc_available = s_rtc_available || (rtc_init() == ESP_OK);
    if (next.rtc_available && (rtc_read_time(&next.rtc_time) == ESP_OK)) {
        next.rtc_valid = true;
    }
    next.sntp_started = s_sntp_started;
    next.sntp_synced = s_sntp_synced;
    *state = next;
    return ESP_OK;
}

void time_status_text(char *buffer, size_t buffer_size)
{
    if ((buffer == nullptr) || (buffer_size == 0)) {
        return;
    }
    TimeState state = {};
    if (time_get_state(&state) != ESP_OK) {
        std::snprintf(buffer, buffer_size, "Time service unavailable");
        return;
    }

    char system_text[32] = "--";
    char rtc_text[32] = "--";
    struct tm tm = {};
    if (state.system_valid) {
        localtime_r(&state.now, &tm);
        strftime(system_text, sizeof(system_text), "%Y-%m-%d %H:%M", &tm);
    }
    if (state.rtc_valid) {
        localtime_r(&state.rtc_time, &tm);
        strftime(rtc_text, sizeof(rtc_text), "%Y-%m-%d %H:%M", &tm);
    }

    std::snprintf(
        buffer,
        buffer_size,
        "System: %s\nRTC: %s\nSNTP: %s\nWiFi: %s",
        system_text,
        state.rtc_available ? rtc_text : "not detected",
        state.sntp_synced ? "synced" : (state.sntp_started ? "started" : "idle"),
        wifi_is_connected() ? "connected" : "offline"
    );
}

} // namespace watch
