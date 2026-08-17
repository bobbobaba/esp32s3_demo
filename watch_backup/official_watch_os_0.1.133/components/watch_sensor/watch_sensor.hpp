#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

namespace watch {

struct SensorState {
    bool available = false;
    char chip[16] = {};
    uint8_t address = 0;
    int16_t temp_raw = 0;
    float temp_c = 0.0f;
    int16_t acc_raw[3] = {};
    int16_t gyro_raw[3] = {};
    float acc_g[3] = {};
    float gyro_dps[3] = {};
    float pitch_deg = 0.0f;
    float roll_deg = 0.0f;
    float pitch_filtered_deg = 0.0f;
    float roll_filtered_deg = 0.0f;
    float pitch_control_deg = 0.0f;
    float roll_control_deg = 0.0f;
    float acc_mag_g = 0.0f;
    float gyro_mag_dps = 0.0f;
    int motion_score = 0;
    bool still = false;
    bool calibrated = false;
    char motion_label[16] = {};
    char robot_command[16] = {};
};

esp_err_t sensor_init();
esp_err_t sensor_read(SensorState *state);
void sensor_status_text(char *buffer, size_t buffer_size);
esp_err_t sensor_calibrate_neutral();
void sensor_reset_calibration();

} // namespace watch
