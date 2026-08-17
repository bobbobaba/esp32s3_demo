#include "watch_sensor.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"

namespace watch {
namespace {

constexpr const char *TAG = "watch_sensor";
constexpr uint32_t I2C_SPEED_HZ = 400000;
constexpr int I2C_TIMEOUT_MS = 200;
constexpr float ACC_FULL_SCALE_G = 8.0f;
constexpr float GYRO_FULL_SCALE_DPS = 512.0f;
constexpr float SENSOR_COUNTS = 32768.0f;
constexpr float FILTER_ALPHA = 0.22f;
constexpr float TILT_DEADZONE_DEG = 14.0f;
constexpr float TILT_EXIT_DEADZONE_DEG = 9.0f;
constexpr float STILL_GYRO_DPS = 6.0f;
constexpr float STILL_ACC_DELTA_G = 0.08f;

enum class Chip {
    None,
    QMI8658,
};

i2c_master_dev_handle_t s_dev = nullptr;
Chip s_chip = Chip::None;
uint8_t s_addr = 0;
bool s_init_attempted = false;
bool s_filter_valid = false;
float s_pitch_filtered = 0.0f;
float s_roll_filtered = 0.0f;
bool s_calibrated = false;
float s_pitch_neutral = 0.0f;
float s_roll_neutral = 0.0f;
char s_latched_command[16] = "STOP";

esp_err_t read_reg(uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(s_dev, &reg, sizeof(reg), value, sizeof(*value), I2C_TIMEOUT_MS);
}

esp_err_t read_regs(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, sizeof(reg), data, len, I2C_TIMEOUT_MS);
}

esp_err_t write_reg(uint8_t reg, uint8_t value)
{
    const uint8_t data[] = {reg, value};
    return i2c_master_transmit(s_dev, data, sizeof(data), I2C_TIMEOUT_MS);
}

esp_err_t try_add_device(uint8_t addr)
{
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    ESP_RETURN_ON_FALSE(bus != nullptr, ESP_FAIL, TAG, "I2C bus unavailable");
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = I2C_SPEED_HZ,
    };
    return i2c_master_bus_add_device(bus, &cfg, &s_dev);
}

int16_t le_i16(const uint8_t *p)
{
    return static_cast<int16_t>(static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));
}

float low_pass(float previous, float current)
{
    return previous + (current - previous) * FILTER_ALPHA;
}

void classify_motion(SensorState &state)
{
    const float acc_delta = std::fabs(state.acc_mag_g - 1.0f);
    state.motion_score = static_cast<int>(state.gyro_mag_dps * 10.0f + acc_delta * 1000.0f);
    state.still = state.gyro_mag_dps < STILL_GYRO_DPS && acc_delta < STILL_ACC_DELTA_G;
    if (state.still) {
        std::snprintf(state.motion_label, sizeof(state.motion_label), "%s", "STILL");
    } else if (state.motion_score > 700) {
        std::snprintf(state.motion_label, sizeof(state.motion_label), "%s", "SHAKE");
    } else {
        std::snprintf(state.motion_label, sizeof(state.motion_label), "%s", "MOVE");
    }
}

void classify_robot_command(SensorState &state)
{
    const float pitch = state.pitch_control_deg;
    const float roll = state.roll_control_deg;
    const float active = std::strcmp(s_latched_command, "STOP") == 0 ? TILT_DEADZONE_DEG : TILT_EXIT_DEADZONE_DEG;
    if (!state.still && state.motion_score > 700) {
        std::snprintf(state.robot_command, sizeof(state.robot_command), "%s", "HOLD");
    } else if (pitch > TILT_DEADZONE_DEG && std::fabs(pitch) >= std::fabs(roll)) {
        std::snprintf(state.robot_command, sizeof(state.robot_command), "%s", "FORWARD");
    } else if (pitch < -TILT_DEADZONE_DEG && std::fabs(pitch) >= std::fabs(roll)) {
        std::snprintf(state.robot_command, sizeof(state.robot_command), "%s", "BACK");
    } else if (roll > TILT_DEADZONE_DEG) {
        std::snprintf(state.robot_command, sizeof(state.robot_command), "%s", "RIGHT");
    } else if (roll < -TILT_DEADZONE_DEG) {
        std::snprintf(state.robot_command, sizeof(state.robot_command), "%s", "LEFT");
    } else if (std::fabs(pitch) < active && std::fabs(roll) < active) {
        std::snprintf(state.robot_command, sizeof(state.robot_command), "%s", "STOP");
    } else {
        std::snprintf(state.robot_command, sizeof(state.robot_command), "%s", s_latched_command);
    }
    std::snprintf(s_latched_command, sizeof(s_latched_command), "%s", state.robot_command);
}

} // namespace

esp_err_t sensor_init()
{
    if (s_chip != Chip::None) {
        return ESP_OK;
    }
    if (s_init_attempted) {
        return ESP_ERR_NOT_FOUND;
    }
    s_init_attempted = true;

    const uint8_t qmi_addresses[] = {0x6a, 0x6b};
    for (uint8_t addr : qmi_addresses) {
        if (try_add_device(addr) != ESP_OK) {
            s_dev = nullptr;
            continue;
        }
        uint8_t whoami = 0;
        if (read_reg(0x00, &whoami) == ESP_OK) {
            ESP_LOGI(TAG, "QMI probe 0x%02x whoami=0x%02x", addr, whoami);
            if ((whoami == 0x05) || (whoami == 0xfc) || (whoami != 0x00 && whoami != 0xff)) {
                s_chip = Chip::QMI8658;
                s_addr = addr;
                write_reg(0x02, 0x60); // CTRL1: auto address increment, normal defaults
                write_reg(0x03, 0x23); // CTRL2: accel +-8g, ODR conservative
                write_reg(0x04, 0x53); // CTRL3: gyro +-512dps, ODR conservative
                write_reg(0x08, 0x03); // CTRL7: enable accel + gyro
                return ESP_OK;
            }
        }
        i2c_master_bus_rm_device(s_dev);
        s_dev = nullptr;
    }

    ESP_LOGW(TAG, "No QMI8658 compatible IMU detected");
    return ESP_ERR_NOT_FOUND;
}

esp_err_t sensor_read(SensorState *state)
{
    ESP_RETURN_ON_FALSE(state != nullptr, ESP_ERR_INVALID_ARG, TAG, "Invalid sensor state pointer");
    ESP_RETURN_ON_ERROR(sensor_init(), TAG, "sensor init failed");

    SensorState next = {};
    next.available = true;
    next.address = s_addr;
    std::snprintf(next.chip, sizeof(next.chip), "QMI8658");

    uint8_t temp_data[2] = {};
    if (read_regs(0x33, temp_data, sizeof(temp_data)) == ESP_OK) {
        next.temp_raw = le_i16(temp_data);
        next.temp_c = static_cast<float>(next.temp_raw) / 256.0f;
    }

    uint8_t data[12] = {};
    ESP_RETURN_ON_ERROR(read_regs(0x35, data, sizeof(data)), TAG, "read QMI8658 data failed");
    next.acc_raw[0] = le_i16(&data[0]);
    next.acc_raw[1] = le_i16(&data[2]);
    next.acc_raw[2] = le_i16(&data[4]);
    next.gyro_raw[0] = le_i16(&data[6]);
    next.gyro_raw[1] = le_i16(&data[8]);
    next.gyro_raw[2] = le_i16(&data[10]);
    for (int i = 0; i < 3; ++i) {
        next.acc_g[i] = static_cast<float>(next.acc_raw[i]) * ACC_FULL_SCALE_G / SENSOR_COUNTS;
        next.gyro_dps[i] = static_cast<float>(next.gyro_raw[i]) * GYRO_FULL_SCALE_DPS / SENSOR_COUNTS;
    }
    next.roll_deg = std::atan2(next.acc_g[1], next.acc_g[2]) * 57.2957795f;
    next.pitch_deg = std::atan2(-next.acc_g[0], std::sqrt(next.acc_g[1] * next.acc_g[1] + next.acc_g[2] * next.acc_g[2])) * 57.2957795f;
    if (!s_filter_valid) {
        s_pitch_filtered = next.pitch_deg;
        s_roll_filtered = next.roll_deg;
        s_filter_valid = true;
    } else {
        s_pitch_filtered = low_pass(s_pitch_filtered, next.pitch_deg);
        s_roll_filtered = low_pass(s_roll_filtered, next.roll_deg);
    }
    next.pitch_filtered_deg = s_pitch_filtered;
    next.roll_filtered_deg = s_roll_filtered;
    next.calibrated = s_calibrated;
    next.pitch_control_deg = next.pitch_filtered_deg - (s_calibrated ? s_pitch_neutral : 0.0f);
    next.roll_control_deg = next.roll_filtered_deg - (s_calibrated ? s_roll_neutral : 0.0f);
    next.acc_mag_g = std::sqrt(next.acc_g[0] * next.acc_g[0] + next.acc_g[1] * next.acc_g[1] + next.acc_g[2] * next.acc_g[2]);
    next.gyro_mag_dps = std::sqrt(next.gyro_dps[0] * next.gyro_dps[0] + next.gyro_dps[1] * next.gyro_dps[1] + next.gyro_dps[2] * next.gyro_dps[2]);
    classify_motion(next);
    classify_robot_command(next);
    *state = next;
    return ESP_OK;
}

esp_err_t sensor_calibrate_neutral()
{
    SensorState state = {};
    ESP_RETURN_ON_ERROR(sensor_read(&state), TAG, "read sensor for calibration failed");
    s_pitch_neutral = state.pitch_filtered_deg;
    s_roll_neutral = state.roll_filtered_deg;
    s_calibrated = true;
    std::snprintf(s_latched_command, sizeof(s_latched_command), "%s", "STOP");
    ESP_LOGI(TAG, "IMU neutral calibrated pitch=%.2f roll=%.2f", s_pitch_neutral, s_roll_neutral);
    return ESP_OK;
}

void sensor_reset_calibration()
{
    s_calibrated = false;
    s_pitch_neutral = 0.0f;
    s_roll_neutral = 0.0f;
    std::snprintf(s_latched_command, sizeof(s_latched_command), "%s", "STOP");
}

void sensor_status_text(char *buffer, size_t buffer_size)
{
    if ((buffer == nullptr) || (buffer_size == 0)) {
        return;
    }
    SensorState state = {};
    esp_err_t err = sensor_read(&state);
    if (err != ESP_OK) {
        std::snprintf(buffer, buffer_size, "IMU: not detected\nBSP_CAPS_IMU: 0\nErr: %s", esp_err_to_name(err));
        return;
    }
    std::snprintf(
        buffer,
        buffer_size,
        "Chip: %s 0x%02x\n"
        "Temp: %.1f C\n"
        "ACC raw: %d, %d, %d\n"
        "ACC g: %.2f, %.2f, %.2f\n"
        "GYRO raw: %d, %d, %d\n"
        "GYRO dps: %.1f, %.1f, %.1f\n"
        "Pitch/Roll: %.1f / %.1f\n"
        "Filtered: %.1f / %.1f\n"
        "Control: %.1f / %.1f %s\n"
        "ACC mag: %.2fg  GYRO: %.1fdps\n"
        "Motion: %s %d\n"
        "Robot: %s",
        state.chip,
        state.address,
        state.temp_c,
        state.acc_raw[0], state.acc_raw[1], state.acc_raw[2],
        state.acc_g[0], state.acc_g[1], state.acc_g[2],
        state.gyro_raw[0], state.gyro_raw[1], state.gyro_raw[2],
        state.gyro_dps[0], state.gyro_dps[1], state.gyro_dps[2],
        state.pitch_deg, state.roll_deg,
        state.pitch_filtered_deg, state.roll_filtered_deg,
        state.pitch_control_deg, state.roll_control_deg, state.calibrated ? "CAL" : "RAW",
        state.acc_mag_g, state.gyro_mag_dps,
        state.motion_label, state.motion_score,
        state.robot_command
    );
}

} // namespace watch
