#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

namespace watch {

enum class BatteryChargeState {
    Unknown,
    Trickle,
    PreCharge,
    ConstantCurrent,
    ConstantVoltage,
    Full,
    NotCharging,
};

enum class BatteryPowerSource {
    Battery,
    External,
};

struct BatteryState {
    bool valid = false;
    bool present = false;
    BatteryPowerSource power_source = BatteryPowerSource::Battery;
    BatteryChargeState charge_state = BatteryChargeState::Unknown;
    bool charger_enabled = false;
    bool has_voltage_mv = false;
    uint32_t voltage_mv = 0;
    bool has_vbus_voltage_mv = false;
    uint32_t vbus_voltage_mv = 0;
    bool has_system_voltage_mv = false;
    uint32_t system_voltage_mv = 0;
    bool has_percentage = false;
    uint8_t percentage = 0;
    bool has_charge_config = false;
    uint32_t target_voltage_mv = 0;
    uint32_t charge_current_ma = 0;
    uint32_t precharge_current_ma = 0;
    uint32_t termination_current_ma = 0;
};

constexpr size_t POWER_USAGE_HISTORY_POINTS = 24;
constexpr size_t POWER_USAGE_APP_BUCKETS = 8;
constexpr size_t POWER_USAGE_APP_NAME_SIZE = 24;

struct PowerUsageHistoryPoint {
    bool valid = false;
    uint8_t percentage = 0;
    uint16_t voltage_mv = 0;
    bool external_power = false;
    bool charging = false;
};

struct PowerUsageApp {
    char name[POWER_USAGE_APP_NAME_SIZE] = {};
    uint32_t active_seconds = 0;
};

struct PowerUsageSnapshot {
    bool valid = false;
    bool external_power = false;
    bool charging = false;
    uint8_t session_start_percentage = 0;
    uint32_t session_elapsed_seconds = 0;
    size_t history_count = 0;
    PowerUsageHistoryPoint history[POWER_USAGE_HISTORY_POINTS] = {};
    size_t app_count = 0;
    PowerUsageApp apps[POWER_USAGE_APP_BUCKETS] = {};
};

esp_err_t power_init();
esp_err_t power_get_battery_state(BatteryState *state);
int power_battery_percent();
bool power_is_charging();
bool power_poll_pkey_short_press();
void power_status_text(char *buffer, size_t buffer_size);
esp_err_t power_get_usage_snapshot(PowerUsageSnapshot *snapshot);
void power_note_foreground_app(const char *name);

} // namespace watch
