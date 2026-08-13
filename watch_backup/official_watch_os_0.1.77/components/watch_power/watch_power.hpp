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

esp_err_t power_init();
esp_err_t power_get_battery_state(BatteryState *state);
int power_battery_percent();
bool power_is_charging();
bool power_poll_pkey_short_press();
void power_status_text(char *buffer, size_t buffer_size);

} // namespace watch
