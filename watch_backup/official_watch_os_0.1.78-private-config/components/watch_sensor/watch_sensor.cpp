#include "watch_sensor.hpp"

#include <cstdio>
#include <cstring>

#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"

namespace watch {
namespace {

constexpr const char *TAG = "watch_sensor";
constexpr uint32_t I2C_SPEED_HZ = 400000;
constexpr int I2C_TIMEOUT_MS = 200;

enum class Chip {
    None,
    QMI8658,
};

i2c_master_dev_handle_t s_dev = nullptr;
Chip s_chip = Chip::None;
uint8_t s_addr = 0;
bool s_init_attempted = false;

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

    uint8_t data[12] = {};
    ESP_RETURN_ON_ERROR(read_regs(0x35, data, sizeof(data)), TAG, "read QMI8658 data failed");
    next.acc_raw[0] = le_i16(&data[0]);
    next.acc_raw[1] = le_i16(&data[2]);
    next.acc_raw[2] = le_i16(&data[4]);
    next.gyro_raw[0] = le_i16(&data[6]);
    next.gyro_raw[1] = le_i16(&data[8]);
    next.gyro_raw[2] = le_i16(&data[10]);
    *state = next;
    return ESP_OK;
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
        "Chip: %s 0x%02x\nACC raw: %d, %d, %d\nGYRO raw: %d, %d, %d",
        state.chip,
        state.address,
        state.acc_raw[0], state.acc_raw[1], state.acc_raw[2],
        state.gyro_raw[0], state.gyro_raw[1], state.gyro_raw[2]
    );
}

} // namespace watch
