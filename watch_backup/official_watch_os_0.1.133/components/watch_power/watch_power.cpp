#include "watch_power.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <limits>

#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "esp_bit_defs.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

namespace watch {

namespace {

constexpr const char *TAG = "watch_power";
constexpr uint8_t AXP2101_I2C_ADDR = 0x34;
constexpr uint32_t AXP2101_I2C_SPEED_HZ = 400000;
constexpr int I2C_TIMEOUT_MS = 200;
constexpr int BATTERY_CACHE_INTERVAL_MS = 30000;
constexpr int64_t POWER_USAGE_SAMPLE_INTERVAL_US = 5LL * 60LL * 1000000LL;
constexpr uint32_t POWER_USAGE_MAGIC = 0x50575231;
constexpr uint16_t POWER_USAGE_VERSION = 1;
constexpr const char *POWER_USAGE_NVS_NAMESPACE = "watch_power";
constexpr const char *POWER_USAGE_NVS_KEY = "usage";
constexpr uint32_t POWER_USAGE_REALTIME_EPOCH_MIN = 1704067200;
constexpr uint8_t HISTORY_FLAG_VALID = BIT(0);
constexpr uint8_t HISTORY_FLAG_EXTERNAL = BIT(1);
constexpr uint8_t HISTORY_FLAG_CHARGING = BIT(2);

enum {
    AXP2101_REG_STATUS1 = 0x00,
    AXP2101_REG_STATUS2 = 0x01,
    AXP2101_REG_INTEN1 = 0x40,
    AXP2101_REG_INTEN2 = 0x41,
    AXP2101_REG_INTEN3 = 0x42,
    AXP2101_REG_INTSTS1 = 0x48,
    AXP2101_REG_INTSTS2 = 0x49,
    AXP2101_REG_INTSTS3 = 0x4A,
    AXP2101_REG_CHARGER_FUEL_GAUGE_CONTROL = 0x18,
    AXP2101_REG_ADC_CHANNEL_ENABLE = 0x30,
    AXP2101_REG_VBAT_H = 0x34,
    AXP2101_REG_VBUS_H = 0x38,
    AXP2101_REG_VSYS_H = 0x3A,
    AXP2101_REG_IPRECHG_CHG_SET = 0x61,
    AXP2101_REG_ICC_CHG_SET = 0x62,
    AXP2101_REG_ITERM_CHG_SET = 0x63,
    AXP2101_REG_CV_CHG_SET = 0x64,
    AXP2101_REG_BATTERY_PERCENTAGE = 0xA4,
};

i2c_master_dev_handle_t s_axp2101 = nullptr;
bool s_init_attempted = false;
bool s_power_cache_task_started = false;
BatteryState s_last_state = {};
portMUX_TYPE s_state_mux = portMUX_INITIALIZER_UNLOCKED;

struct StoredUsageHistoryPoint {
    uint32_t hour_marker = 0;
    uint16_t voltage_mv = 0;
    uint8_t percentage = 0;
    uint8_t flags = 0;
};

struct StoredUsageApp {
    char name[POWER_USAGE_APP_NAME_SIZE] = {};
    uint32_t active_seconds = 0;
};

struct StoredUsage {
    uint32_t magic = POWER_USAGE_MAGIC;
    uint16_t version = POWER_USAGE_VERSION;
    uint16_t history_count = 0;
    uint8_t history_start = 0;
    uint8_t source_known = 0;
    uint8_t source_external = 0;
    uint8_t session_start_percentage = 0;
    uint32_t session_elapsed_seconds = 0;
    StoredUsageHistoryPoint history[POWER_USAGE_HISTORY_POINTS] = {};
    StoredUsageApp apps[POWER_USAGE_APP_BUCKETS] = {};
};

StoredUsage s_usage = {};
bool s_usage_loaded = false;
int64_t s_usage_session_started_us = 0;
int64_t s_foreground_started_us = 0;
int64_t s_next_usage_sample_us = 0;
char s_foreground_app[POWER_USAGE_APP_NAME_SIZE] = {};
portMUX_TYPE s_usage_mux = portMUX_INITIALIZER_UNLOCKED;

esp_err_t axp2101_read_reg(uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(s_axp2101, &reg, sizeof(reg), value, sizeof(*value), I2C_TIMEOUT_MS);
}

esp_err_t axp2101_write_reg(uint8_t reg, uint8_t value)
{
    const uint8_t data[] = {reg, value};
    return i2c_master_transmit(s_axp2101, data, sizeof(data), I2C_TIMEOUT_MS);
}

esp_err_t axp2101_clear_irq_status()
{
    const uint8_t regs[] = {
        AXP2101_REG_INTSTS1,
        AXP2101_REG_INTSTS2,
        AXP2101_REG_INTSTS3,
    };
    for (uint8_t reg : regs) {
        ESP_RETURN_ON_ERROR(axp2101_write_reg(reg, 0xFF), TAG, "clear AXP2101 irq 0x%02x failed", reg);
    }
    return ESP_OK;
}

esp_err_t axp2101_update_reg_bits(uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t current = 0;
    ESP_RETURN_ON_ERROR(axp2101_read_reg(reg, &current), TAG, "read AXP2101 reg 0x%02x failed", reg);
    current = (current & ~mask) | (value & mask);
    return axp2101_write_reg(reg, current);
}

esp_err_t axp2101_read_u14(uint8_t high_reg, uint32_t *value)
{
    uint8_t data[2] = {};
    ESP_RETURN_ON_ERROR(
        i2c_master_transmit_receive(s_axp2101, &high_reg, sizeof(high_reg), data, sizeof(data), I2C_TIMEOUT_MS),
        TAG, "read AXP2101 ADC reg 0x%02x failed", high_reg
    );
    *value = ((uint32_t)(data[0] & 0x3f) << 8) | data[1];
    return ESP_OK;
}

BatteryChargeState convert_charge_state(uint8_t status)
{
    switch (status & 0x07) {
    case 0:
        return BatteryChargeState::Trickle;
    case 1:
        return BatteryChargeState::PreCharge;
    case 2:
        return BatteryChargeState::ConstantCurrent;
    case 3:
        return BatteryChargeState::ConstantVoltage;
    case 4:
        return BatteryChargeState::Full;
    case 5:
        return BatteryChargeState::NotCharging;
    case 6:
    default:
        return BatteryChargeState::Unknown;
    }
}

const char *charge_state_text(BatteryChargeState state)
{
    switch (state) {
    case BatteryChargeState::Trickle:
        return "trickle";
    case BatteryChargeState::PreCharge:
        return "pre-charge";
    case BatteryChargeState::ConstantCurrent:
        return "charging";
    case BatteryChargeState::ConstantVoltage:
        return "charging";
    case BatteryChargeState::Full:
        return "full";
    case BatteryChargeState::NotCharging:
        return "not charging";
    case BatteryChargeState::Unknown:
    default:
        return "unknown";
    }
}

const char *power_source_text(BatteryPowerSource source)
{
    switch (source) {
    case BatteryPowerSource::External:
        return "USB / external";
    case BatteryPowerSource::Battery:
    default:
        return "battery";
    }
}

const char *present_text(bool present)
{
    return present ? "yes" : "no";
}

bool is_charging_state(BatteryChargeState state)
{
    return (state == BatteryChargeState::Trickle) ||
           (state == BatteryChargeState::PreCharge) ||
           (state == BatteryChargeState::ConstantCurrent) ||
           (state == BatteryChargeState::ConstantVoltage);
}

uint32_t decode_charge_current(uint8_t reg_value)
{
    uint8_t code = reg_value & 0x1f;
    if (code <= 8) {
        return code * 25;
    }
    if (code <= 16) {
        return 200 + (code - 8) * 100;
    }
    return 0;
}

uint32_t decode_target_voltage(uint8_t reg_value)
{
    switch (reg_value & 0x07) {
    case 1:
        return 4000;
    case 2:
        return 4100;
    case 3:
        return 4200;
    case 4:
        return 4350;
    case 5:
        return 4400;
    default:
        return 0;
    }
}

esp_err_t read_charge_config(BatteryState *state)
{
    uint8_t charger_control = 0;
    uint8_t precharge = 0;
    uint8_t charge = 0;
    uint8_t termination = 0;
    uint8_t target_voltage = 0;

    ESP_RETURN_ON_ERROR(
        axp2101_read_reg(AXP2101_REG_CHARGER_FUEL_GAUGE_CONTROL, &charger_control),
        TAG,
        "read charger control failed"
    );
    ESP_RETURN_ON_ERROR(
        axp2101_read_reg(AXP2101_REG_IPRECHG_CHG_SET, &precharge), TAG, "read precharge failed"
    );
    ESP_RETURN_ON_ERROR(
        axp2101_read_reg(AXP2101_REG_ICC_CHG_SET, &charge), TAG, "read charge current failed"
    );
    ESP_RETURN_ON_ERROR(
        axp2101_read_reg(AXP2101_REG_ITERM_CHG_SET, &termination), TAG, "read termination current failed"
    );
    ESP_RETURN_ON_ERROR(
        axp2101_read_reg(AXP2101_REG_CV_CHG_SET, &target_voltage), TAG, "read target voltage failed"
    );

    state->has_charge_config = true;
    state->charger_enabled = (charger_control & BIT(1)) != 0;
    state->target_voltage_mv = decode_target_voltage(target_voltage);
    state->charge_current_ma = decode_charge_current(charge);
    state->precharge_current_ma = (precharge & 0x0f) * 25;
    state->termination_current_ma = (termination & 0x0f) * 25;
    return ESP_OK;
}

void store_last_state(const BatteryState &state)
{
    portENTER_CRITICAL(&s_state_mux);
    s_last_state = state;
    portEXIT_CRITICAL(&s_state_mux);
}

BatteryState cached_state()
{
    BatteryState state = {};
    portENTER_CRITICAL(&s_state_mux);
    state = s_last_state;
    portEXIT_CRITICAL(&s_state_mux);
    return state;
}

bool usage_store_is_valid(const StoredUsage &usage)
{
    return usage.magic == POWER_USAGE_MAGIC &&
           usage.version == POWER_USAGE_VERSION &&
           usage.history_count <= POWER_USAGE_HISTORY_POINTS &&
           usage.history_start < POWER_USAGE_HISTORY_POINTS;
}

void usage_normalize_locked()
{
    s_usage.magic = POWER_USAGE_MAGIC;
    s_usage.version = POWER_USAGE_VERSION;
    s_usage.source_known = s_usage.source_known ? 1 : 0;
    s_usage.source_external = s_usage.source_external ? 1 : 0;
    if (s_usage.history_count == 0) {
        s_usage.history_start = 0;
    }
    for (auto &app : s_usage.apps) {
        app.name[sizeof(app.name) - 1] = '\0';
    }
}

void usage_load_state()
{
    StoredUsage loaded = {};
    bool loaded_ok = false;
    nvs_handle_t handle = 0;
    if (nvs_open(POWER_USAGE_NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        size_t length = sizeof(loaded);
        loaded_ok = nvs_get_blob(handle, POWER_USAGE_NVS_KEY, &loaded, &length) == ESP_OK &&
                    length == sizeof(loaded) && usage_store_is_valid(loaded);
        nvs_close(handle);
    }

    portENTER_CRITICAL(&s_usage_mux);
    s_usage = loaded_ok ? loaded : StoredUsage{};
    usage_normalize_locked();
    s_usage_loaded = true;
    s_usage_session_started_us = esp_timer_get_time();
    s_foreground_started_us = 0;
    s_next_usage_sample_us = 0;
    portEXIT_CRITICAL(&s_usage_mux);

    ESP_LOGI(TAG, "power usage history %s", loaded_ok ? "restored" : "started");
}

uint32_t usage_saturating_add(uint32_t current, uint64_t delta)
{
    const uint64_t total = static_cast<uint64_t>(current) + delta;
    return total > std::numeric_limits<uint32_t>::max() ? std::numeric_limits<uint32_t>::max() :
           static_cast<uint32_t>(total);
}

void usage_account_foreground_locked(int64_t now_us)
{
    if ((s_foreground_app[0] == '\0') || (s_foreground_started_us <= 0) || (now_us <= s_foreground_started_us)) {
        return;
    }

    const uint64_t elapsed_s = static_cast<uint64_t>(now_us - s_foreground_started_us) / 1000000ULL;
    if (elapsed_s == 0) {
        return;
    }
    s_foreground_started_us += static_cast<int64_t>(elapsed_s * 1000000ULL);

    for (auto &app : s_usage.apps) {
        if (std::strncmp(app.name, s_foreground_app, sizeof(app.name)) == 0) {
            app.active_seconds = usage_saturating_add(app.active_seconds, elapsed_s);
            return;
        }
    }
}

uint32_t usage_session_elapsed_locked(int64_t now_us)
{
    uint64_t elapsed_s = 0;
    if (s_usage_session_started_us > 0 && now_us > s_usage_session_started_us) {
        elapsed_s = static_cast<uint64_t>(now_us - s_usage_session_started_us) / 1000000ULL;
    }
    return usage_saturating_add(s_usage.session_elapsed_seconds, elapsed_s);
}

uint32_t usage_hour_marker(int64_t now_us)
{
    const time_t wall_time = std::time(nullptr);
    if (wall_time >= static_cast<time_t>(POWER_USAGE_REALTIME_EPOCH_MIN)) {
        return static_cast<uint32_t>(wall_time / 3600);
    }
    return 0x80000000U | static_cast<uint32_t>((now_us / 3600000000LL) & 0x7fffffff);
}

void usage_reset_cycle_locked(bool external_power, uint8_t percentage, int64_t now_us)
{
    char active_app[POWER_USAGE_APP_NAME_SIZE] = {};
    std::memcpy(active_app, s_foreground_app, sizeof(active_app));
    active_app[sizeof(active_app) - 1] = '\0';
    s_usage.source_known = 1;
    s_usage.source_external = external_power ? 1 : 0;
    s_usage.session_start_percentage = percentage;
    s_usage.session_elapsed_seconds = 0;
    std::memset(s_usage.apps, 0, sizeof(s_usage.apps));
    s_usage_session_started_us = now_us;
    if (active_app[0] != '\0') {
        std::memcpy(s_usage.apps[0].name, active_app, sizeof(s_usage.apps[0].name));
        s_usage.apps[0].name[sizeof(s_usage.apps[0].name) - 1] = '\0';
        s_foreground_started_us = now_us;
    }
}

bool usage_write_history_locked(const BatteryState &state, uint32_t hour_marker)
{
    const bool external_power = state.power_source == BatteryPowerSource::External;
    const uint8_t flags = HISTORY_FLAG_VALID |
                          (external_power ? HISTORY_FLAG_EXTERNAL : 0) |
                          (is_charging_state(state.charge_state) ? HISTORY_FLAG_CHARGING : 0);
    StoredUsageHistoryPoint point = {};
    point.hour_marker = hour_marker;
    point.voltage_mv = state.has_voltage_mv ? static_cast<uint16_t>(std::min<uint32_t>(state.voltage_mv, 65535)) : 0;
    point.percentage = state.percentage;
    point.flags = flags;

    if (s_usage.history_count > 0) {
        const size_t newest = (s_usage.history_start + s_usage.history_count - 1) % POWER_USAGE_HISTORY_POINTS;
        if (s_usage.history[newest].hour_marker == hour_marker) {
            s_usage.history[newest] = point;
            return false;
        }
    }

    size_t destination = (s_usage.history_start + s_usage.history_count) % POWER_USAGE_HISTORY_POINTS;
    if (s_usage.history_count == POWER_USAGE_HISTORY_POINTS) {
        destination = s_usage.history_start;
        s_usage.history_start = (s_usage.history_start + 1) % POWER_USAGE_HISTORY_POINTS;
    } else {
        ++s_usage.history_count;
    }
    s_usage.history[destination] = point;
    return true;
}

void usage_save_state()
{
    StoredUsage snapshot = {};
    const int64_t now_us = esp_timer_get_time();
    portENTER_CRITICAL(&s_usage_mux);
    if (!s_usage_loaded) {
        portEXIT_CRITICAL(&s_usage_mux);
        return;
    }
    usage_account_foreground_locked(now_us);
    s_usage.session_elapsed_seconds = usage_session_elapsed_locked(now_us);
    s_usage_session_started_us = now_us;
    snapshot = s_usage;
    portEXIT_CRITICAL(&s_usage_mux);

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(POWER_USAGE_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "open power usage NVS failed: %s", esp_err_to_name(err));
        return;
    }
    err = nvs_set_blob(handle, POWER_USAGE_NVS_KEY, &snapshot, sizeof(snapshot));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "save power usage failed: %s", esp_err_to_name(err));
    }
}

void usage_record_sample(const BatteryState &state, bool force)
{
    if (!state.valid || !state.has_percentage) {
        return;
    }

    bool persist = false;
    const int64_t now_us = esp_timer_get_time();
    portENTER_CRITICAL(&s_usage_mux);
    if (!s_usage_loaded) {
        portEXIT_CRITICAL(&s_usage_mux);
        return;
    }

    const bool external_power = state.power_source == BatteryPowerSource::External;
    if (!s_usage.source_known) {
        usage_reset_cycle_locked(external_power, state.percentage, now_us);
        force = true;
        persist = true;
    } else if ((s_usage.source_external != 0) != external_power) {
        usage_account_foreground_locked(now_us);
        usage_reset_cycle_locked(external_power, state.percentage, now_us);
        force = true;
        persist = true;
        ESP_LOGI(TAG, "power source changed: %s", external_power ? "external" : "battery");
    }

    if (force || s_next_usage_sample_us == 0 || now_us >= s_next_usage_sample_us) {
        persist = usage_write_history_locked(state, usage_hour_marker(now_us)) || persist;
        s_next_usage_sample_us = now_us + POWER_USAGE_SAMPLE_INTERVAL_US;
    }
    portEXIT_CRITICAL(&s_usage_mux);

    if (persist) {
        usage_save_state();
    }
}

esp_err_t read_battery_state_hw(BatteryState *state)
{
    ESP_RETURN_ON_FALSE(state != nullptr, ESP_ERR_INVALID_ARG, TAG, "Invalid battery state pointer");
    ESP_RETURN_ON_FALSE(s_axp2101 != nullptr, ESP_ERR_INVALID_STATE, TAG, "AXP2101 not initialized");

    BatteryState next = {};
    uint8_t status1 = 0;
    uint8_t status2 = 0;
    ESP_RETURN_ON_ERROR(axp2101_read_reg(AXP2101_REG_STATUS1, &status1), TAG, "read status1 failed");
    ESP_RETURN_ON_ERROR(axp2101_read_reg(AXP2101_REG_STATUS2, &status2), TAG, "read status2 failed");

    next.valid = true;
    next.present = (status1 & BIT(3)) != 0;
    next.power_source = (status1 & BIT(5)) != 0 ? BatteryPowerSource::External : BatteryPowerSource::Battery;
    next.charge_state = convert_charge_state(status2);

    ESP_ERROR_CHECK_WITHOUT_ABORT(
        axp2101_update_reg_bits(
            AXP2101_REG_ADC_CHANNEL_ENABLE, BIT(0) | BIT(2) | BIT(3), BIT(0) | BIT(2) | BIT(3)
        )
    );

    uint32_t voltage_mv = 0;
    if (next.present && axp2101_read_u14(AXP2101_REG_VBAT_H, &voltage_mv) == ESP_OK) {
        next.has_voltage_mv = true;
        next.voltage_mv = voltage_mv;
    }
    if (axp2101_read_u14(AXP2101_REG_VBUS_H, &voltage_mv) == ESP_OK) {
        next.has_vbus_voltage_mv = true;
        next.vbus_voltage_mv = voltage_mv;
    }
    if (axp2101_read_u14(AXP2101_REG_VSYS_H, &voltage_mv) == ESP_OK) {
        next.has_system_voltage_mv = true;
        next.system_voltage_mv = voltage_mv;
    }

    uint8_t percentage = 0;
    if (next.present && axp2101_read_reg(AXP2101_REG_BATTERY_PERCENTAGE, &percentage) == ESP_OK && percentage <= 100) {
        next.has_percentage = true;
        next.percentage = percentage;
    }

    esp_err_t config_err = read_charge_config(&next);
    if (config_err != ESP_OK) {
        ESP_LOGW(TAG, "read charge config failed: %s", esp_err_to_name(config_err));
    }

    *state = next;
    return ESP_OK;
}

void power_cache_task(void *)
{
    while (true) {
        BatteryState next = {};
        esp_err_t err = read_battery_state_hw(&next);
        if (err == ESP_OK) {
            store_last_state(next);
            usage_record_sample(next, false);
        } else {
            ESP_LOGW(TAG, "battery cache refresh failed: %s", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(BATTERY_CACHE_INTERVAL_MS));
    }
}

void start_power_cache_task()
{
    if (!s_power_cache_task_started) {
        s_power_cache_task_started = xTaskCreateWithCaps(
            power_cache_task,
            "power_cache",
            3072,
            nullptr,
            3,
            nullptr,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        ) == pdPASS;
        if (!s_power_cache_task_started) {
            ESP_LOGW(TAG, "Failed to start power cache task");
        }
    }
}

} // namespace

esp_err_t power_init()
{
    if (s_axp2101 != nullptr) {
        start_power_cache_task();
        return ESP_OK;
    }
    s_init_attempted = true;

    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    ESP_RETURN_ON_FALSE(bus != nullptr, ESP_FAIL, TAG, "BSP I2C bus unavailable");

    const i2c_device_config_t axp2101_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP2101_I2C_ADDR,
        .scl_speed_hz = AXP2101_I2C_SPEED_HZ,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &axp2101_config, &s_axp2101);
    if (err != ESP_OK) {
        s_axp2101 = nullptr;
        ESP_LOGW(TAG, "AXP2101 add device failed: %s", esp_err_to_name(err));
        return err;
    }

    uint8_t status = 0;
    err = axp2101_read_reg(AXP2101_REG_STATUS1, &status);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "AXP2101 probe failed: %s", esp_err_to_name(err));
        i2c_master_bus_rm_device(s_axp2101);
        s_axp2101 = nullptr;
        return err;
    }

    ESP_LOGI(TAG, "AXP2101 detected at 0x%02x", AXP2101_I2C_ADDR);

    ESP_RETURN_ON_ERROR(axp2101_clear_irq_status(), TAG, "clear AXP2101 irq status failed");
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        axp2101_update_reg_bits(
            AXP2101_REG_INTEN1,
            0xFF,
            0x00
        )
    );
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        axp2101_update_reg_bits(
            AXP2101_REG_INTEN2,
            BIT(3) | BIT(2) | BIT(1) | BIT(0),
            BIT(3) | BIT(2) | BIT(1) | BIT(0)
        )
    );
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        axp2101_update_reg_bits(
            AXP2101_REG_INTEN3,
            0xFF,
            0x00
        )
    );
    ESP_LOGI(TAG, "AXP2101 PKEY IRQ enabled");
    usage_load_state();
    BatteryState initial = {};
    if (read_battery_state_hw(&initial) == ESP_OK) {
        store_last_state(initial);
        usage_record_sample(initial, true);
    }
    start_power_cache_task();
    return ESP_OK;
}

esp_err_t power_get_battery_state(BatteryState *state)
{
    ESP_RETURN_ON_FALSE(state != nullptr, ESP_ERR_INVALID_ARG, TAG, "Invalid battery state pointer");
    *state = cached_state();
    return state->valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}

int power_battery_percent()
{
    BatteryState state = cached_state();
    if (state.has_percentage) {
        return state.percentage;
    }
    return -1;
}

bool power_is_charging()
{
    return is_charging_state(cached_state().charge_state);
}

esp_err_t power_get_usage_snapshot(PowerUsageSnapshot *snapshot)
{
    ESP_RETURN_ON_FALSE(snapshot != nullptr, ESP_ERR_INVALID_ARG, TAG, "Invalid usage snapshot pointer");
    *snapshot = {};

    const BatteryState state = cached_state();
    const int64_t now_us = esp_timer_get_time();
    portENTER_CRITICAL(&s_usage_mux);
    if (!s_usage_loaded) {
        portEXIT_CRITICAL(&s_usage_mux);
        return ESP_ERR_INVALID_STATE;
    }

    usage_account_foreground_locked(now_us);
    snapshot->valid = state.valid;
    snapshot->external_power = state.power_source == BatteryPowerSource::External;
    snapshot->charging = is_charging_state(state.charge_state);
    snapshot->session_start_percentage = s_usage.session_start_percentage;
    snapshot->session_elapsed_seconds = usage_session_elapsed_locked(now_us);
    snapshot->history_count = s_usage.history_count;
    for (size_t index = 0; index < s_usage.history_count; ++index) {
        const size_t source = (s_usage.history_start + index) % POWER_USAGE_HISTORY_POINTS;
        const StoredUsageHistoryPoint &point = s_usage.history[source];
        PowerUsageHistoryPoint &destination = snapshot->history[index];
        destination.valid = (point.flags & HISTORY_FLAG_VALID) != 0;
        destination.percentage = point.percentage;
        destination.voltage_mv = point.voltage_mv;
        destination.external_power = (point.flags & HISTORY_FLAG_EXTERNAL) != 0;
        destination.charging = (point.flags & HISTORY_FLAG_CHARGING) != 0;
    }

    StoredUsageApp ranked[POWER_USAGE_APP_BUCKETS] = {};
    std::memcpy(ranked, s_usage.apps, sizeof(ranked));
    for (size_t index = 0; index < POWER_USAGE_APP_BUCKETS; ++index) {
        size_t best = index;
        for (size_t candidate = index + 1; candidate < POWER_USAGE_APP_BUCKETS; ++candidate) {
            if (ranked[candidate].active_seconds > ranked[best].active_seconds) {
                best = candidate;
            }
        }
        if (best != index) {
            std::swap(ranked[index], ranked[best]);
        }
        if ((ranked[index].name[0] == '\0') || (ranked[index].active_seconds == 0)) {
            continue;
        }
        PowerUsageApp &destination = snapshot->apps[snapshot->app_count++];
        std::memcpy(destination.name, ranked[index].name, sizeof(destination.name));
        destination.name[sizeof(destination.name) - 1] = '\0';
        destination.active_seconds = ranked[index].active_seconds;
    }
    portEXIT_CRITICAL(&s_usage_mux);
    return ESP_OK;
}

void power_note_foreground_app(const char *name)
{
    const int64_t now_us = esp_timer_get_time();
    portENTER_CRITICAL(&s_usage_mux);
    if (!s_usage_loaded) {
        portEXIT_CRITICAL(&s_usage_mux);
        return;
    }

    usage_account_foreground_locked(now_us);
    s_foreground_app[0] = '\0';
    s_foreground_started_us = 0;
    if ((name == nullptr) || (name[0] == '\0')) {
        portEXIT_CRITICAL(&s_usage_mux);
        return;
    }

    std::strncpy(s_foreground_app, name, sizeof(s_foreground_app) - 1);
    s_foreground_app[sizeof(s_foreground_app) - 1] = '\0';
    bool found = false;
    for (size_t index = 0; index < POWER_USAGE_APP_BUCKETS - 1; ++index) {
        if (s_usage.apps[index].name[0] == '\0') {
            std::memcpy(s_usage.apps[index].name, s_foreground_app, sizeof(s_usage.apps[index].name));
            s_usage.apps[index].name[sizeof(s_usage.apps[index].name) - 1] = '\0';
            found = true;
            break;
        }
        if (std::strncmp(s_usage.apps[index].name, s_foreground_app, sizeof(s_usage.apps[index].name)) == 0) {
            found = true;
            break;
        }
    }
    if (!found) {
        std::snprintf(s_foreground_app, sizeof(s_foreground_app), "Other apps");
        std::snprintf(s_usage.apps[POWER_USAGE_APP_BUCKETS - 1].name,
                      sizeof(s_usage.apps[POWER_USAGE_APP_BUCKETS - 1].name), "Other apps");
    }
    s_foreground_started_us = now_us;
    portEXIT_CRITICAL(&s_usage_mux);
}

bool power_poll_pkey_short_press()
{
    if (power_init() != ESP_OK) {
        return false;
    }

    uint8_t sts1 = 0;
    uint8_t sts2 = 0;
    uint8_t sts3 = 0;
    if ((axp2101_read_reg(AXP2101_REG_INTSTS1, &sts1) != ESP_OK) ||
        (axp2101_read_reg(AXP2101_REG_INTSTS2, &sts2) != ESP_OK) ||
        (axp2101_read_reg(AXP2101_REG_INTSTS3, &sts3) != ESP_OK)) {
        return false;
    }

    const bool pkey_positive_edge = (sts2 & BIT(0)) != 0;
    const bool pkey_negative_edge = (sts2 & BIT(1)) != 0;
    const bool long_press = (sts2 & BIT(2)) != 0;
    const bool short_press = (sts2 & BIT(3)) != 0;
    const bool pkey_event = pkey_positive_edge || pkey_negative_edge || short_press || long_press;
    if (!pkey_event) {
        return false;
    }

    ESP_LOGD(TAG, "AXP2101 irq: sts1=0x%02x sts2=0x%02x sts3=0x%02x", sts1, sts2, sts3);
    if (pkey_negative_edge) {
        ESP_LOGI(TAG, "AXP2101 power key press edge");
    }
    if (pkey_positive_edge) {
        ESP_LOGI(TAG, "AXP2101 power key release edge");
    }
    if (long_press) {
        ESP_LOGI(TAG, "AXP2101 power key long press");
    }
    if (short_press) {
        ESP_LOGI(TAG, "AXP2101 power key short press");
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(axp2101_clear_irq_status());
    return short_press || pkey_positive_edge;
}

void power_status_text(char *buffer, size_t buffer_size)
{
    if ((buffer == nullptr) || (buffer_size == 0)) {
        return;
    }

    BatteryState state = cached_state();
    if (!state.valid) {
        std::snprintf(buffer, buffer_size, "AXP2101 cache pending");
        return;
    }

    char percent_text[24] = {};
    if (state.has_percentage) {
        std::snprintf(percent_text, sizeof(percent_text), "%u%%", state.percentage);
    } else {
        std::snprintf(percent_text, sizeof(percent_text), "--%%");
    }

    char vbat_text[20] = {};
    char vbus_text[20] = {};
    char vsys_text[20] = {};
    std::snprintf(
        vbat_text, sizeof(vbat_text), state.has_voltage_mv ? "%lumV" : "--",
        static_cast<unsigned long>(state.voltage_mv)
    );
    std::snprintf(
        vbus_text, sizeof(vbus_text), state.has_vbus_voltage_mv ? "%lumV" : "--",
        static_cast<unsigned long>(state.vbus_voltage_mv)
    );
    std::snprintf(
        vsys_text, sizeof(vsys_text), state.has_system_voltage_mv ? "%lumV" : "--",
        static_cast<unsigned long>(state.system_voltage_mv)
    );

    if (state.has_charge_config) {
        std::snprintf(
            buffer,
            buffer_size,
            "Battery: %s\n"
            "Present: %s\n"
            "Source: %s\n"
            "Charge: %s\n"
            "VBAT: %s\n"
            "VBUS: %s\n"
            "VSYS: %s\n"
            "Charger: %s\n"
            "Target: %lumV\n"
            "Charge I: %lumA\n"
            "Prechg I: %lumA\n"
            "Term I: %lumA",
            percent_text,
            present_text(state.present),
            power_source_text(state.power_source),
            charge_state_text(state.charge_state),
            vbat_text,
            vbus_text,
            vsys_text,
            state.charger_enabled ? "enabled" : "disabled",
            static_cast<unsigned long>(state.target_voltage_mv),
            static_cast<unsigned long>(state.charge_current_ma),
            static_cast<unsigned long>(state.precharge_current_ma),
            static_cast<unsigned long>(state.termination_current_ma)
        );
    } else {
        std::snprintf(
            buffer,
            buffer_size,
            "Battery: %s\n"
            "Present: %s\n"
            "Source: %s\n"
            "Charge: %s\n"
            "VBAT: %s\n"
            "VBUS: %s\n"
            "VSYS: %s",
            percent_text,
            present_text(state.present),
            power_source_text(state.power_source),
            charge_state_text(state.charge_state),
            vbat_text,
            vbus_text,
            vsys_text
        );
    }
}

} // namespace watch
