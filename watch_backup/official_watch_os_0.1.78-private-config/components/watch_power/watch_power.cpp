#include "watch_power.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "esp_bit_defs.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace watch {

namespace {

constexpr const char *TAG = "watch_power";
constexpr uint8_t AXP2101_I2C_ADDR = 0x34;
constexpr uint32_t AXP2101_I2C_SPEED_HZ = 400000;
constexpr int I2C_TIMEOUT_MS = 200;
constexpr int BATTERY_CACHE_INTERVAL_MS = 10000;

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
    BatteryState initial = {};
    if (read_battery_state_hw(&initial) == ESP_OK) {
        store_last_state(initial);
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
