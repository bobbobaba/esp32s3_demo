/*
 * SPDX-License-Identifier: CC0-1.0
 */

#include <string.h>
#include <stdlib.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lcd_sh8601.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_ft5x06.h"

static const char *TAG = "SETUP_DEVICE";

static const sh8601_lcd_init_cmd_t vendor_specific_init_default[] = {
    /* Waveshare official BSP: bsp/esp32_s3_touch_amoled_2_06, v2.0.0. */
    {0x11, (uint8_t[]){0x00}, 0, 120},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x44, (uint8_t[]){0x01, 0xD1}, 2, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 10},
    {0x63, (uint8_t[]){0xFF}, 1, 10},
    {0x51, (uint8_t[]){0x00}, 1, 10},
    {0x2A, (uint8_t[]){0x00, 0x16, 0x01, 0xAF}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xF5}, 4, 0},
    {0x29, (uint8_t[]){0x00}, 0, 10},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
};

static const sh8601_vendor_config_t vendor_config = {
    .init_cmds = vendor_specific_init_default,
    .init_cmds_size = sizeof(vendor_specific_init_default) / sizeof(vendor_specific_init_default[0]),
    .flags = {
        .use_qspi_interface = 1,
    },
};

static esp_err_t lcd_panel_clear_black(esp_lcd_panel_handle_t panel)
{
    enum {
        LCD_WIDTH = CONFIG_BROOKESIA_HAL_ADAPTOR_DISPLAY_LCD_PANEL_H_RES,
        LCD_HEIGHT = CONFIG_BROOKESIA_HAL_ADAPTOR_DISPLAY_LCD_PANEL_V_RES,
        BYTES_PER_PIXEL = 2, // Board Manager config uses RGB565 / 16 bpp.
        FLUSH_LINES = 16,
    };

    const size_t buf_size = LCD_WIDTH * FLUSH_LINES * BYTES_PER_PIXEL;
    uint8_t *black = heap_caps_calloc(1, buf_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_RETURN_ON_FALSE(black, ESP_ERR_NO_MEM, TAG, "No memory for LCD clear buffer");

    esp_err_t ret = ESP_OK;
    for (int y = 0; y < LCD_HEIGHT; y += FLUSH_LINES) {
        const int y_end = (y + FLUSH_LINES > LCD_HEIGHT) ? LCD_HEIGHT : (y + FLUSH_LINES);
        ret = esp_lcd_panel_draw_bitmap(panel, 0, y, LCD_WIDTH, y_end, black);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to clear LCD area y=%d..%d: %s", y, y_end, esp_err_to_name(ret));
            break;
        }
    }

    heap_caps_free(black);
    return ret;
}

esp_err_t lcd_panel_factory_entry_t(
    esp_lcd_panel_io_handle_t io,
    const esp_lcd_panel_dev_config_t *panel_dev_config,
    esp_lcd_panel_handle_t *ret_panel)
{
    esp_lcd_panel_dev_config_t panel_dev_cfg = {0};
    memcpy(&panel_dev_cfg, panel_dev_config, sizeof(panel_dev_cfg));
    panel_dev_cfg.vendor_config = (void *)&vendor_config;

    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_sh8601(io, &panel_dev_cfg, ret_panel), TAG,
        "Failed to create SH8601 panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(*ret_panel), TAG, "Failed to reset SH8601 panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(*ret_panel), TAG, "Failed to init SH8601 panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(*ret_panel, 0x16, 0), TAG, "Failed to set SH8601 panel gap");
    ESP_RETURN_ON_ERROR(lcd_panel_clear_black(*ret_panel), TAG, "Failed to clear SH8601 full screen");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(*ret_panel, true), TAG, "Failed to turn on SH8601 panel");

    return ESP_OK;
}

esp_err_t lcd_touch_factory_entry_t(
    esp_lcd_panel_io_handle_t io,
    const esp_lcd_touch_config_t *touch_dev_config,
    esp_lcd_touch_handle_t *ret_touch)
{
    /* FT3168 is register-compatible with the ESP-IDF FT5x06 driver. */
    esp_err_t ret = esp_lcd_touch_new_i2c_ft5x06(io, touch_dev_config, ret_touch);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create FT3168 touch driver: %s", esp_err_to_name(ret));
    }
    return ret;
}
