#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

namespace watch {

struct SensorState {
    bool available = false;
    char chip[16] = {};
    uint8_t address = 0;
    int16_t acc_raw[3] = {};
    int16_t gyro_raw[3] = {};
};

esp_err_t sensor_init();
esp_err_t sensor_read(SensorState *state);
void sensor_status_text(char *buffer, size_t buffer_size);

} // namespace watch
