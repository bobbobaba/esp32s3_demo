#include "settings_app.hpp"

#include <cstdio>
#include <cstring>
#include <cstdint>

#include "esp_err.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "esp_app_desc.h"
#include "esp_brookesia.hpp"
#include "esp_heap_caps.h"
#include "esp_lib_utils.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_pm.h"
#include "esp_private/esp_clk.h"
#include "freertos/idf_additions.h"
#include "lvgl.h"
#include "watch_fonts.hpp"
#include "watch_connectivity.hpp"
#include "watch_power.hpp"
#include "watch_time.hpp"
#include "watch_sensor.hpp"
#include "watch_storage.hpp"
#include "watch_audio.hpp"
#include "watch_display.hpp"
#include "watch_app_icons.hpp"

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "Settings"

namespace esp_brookesia::apps {

namespace {
constexpr const char *APP_NAME = "Settings";
constexpr int SAFE_TOP = 24;
constexpr int SAFE_SIDE = 22;
constexpr uint32_t WIFI_SETUP_TASK_STACK_BYTES = 4096;
constexpr uint32_t WIFI_SCAN_TASK_STACK_BYTES = 4096;
constexpr uint32_t STORAGE_STATUS_TASK_STACK_BYTES = 4096;
constexpr uint32_t SETTINGS_TIMER_IDLE_MS = 5000;
constexpr uint32_t SETTINGS_TIMER_BUSY_MS = 500;
constexpr uint32_t STORAGE_REFRESH_INTERVAL_MS = 60000;
constexpr uint32_t DIAGNOSTICS_REFRESH_INTERVAL_MS = 15000;
constexpr uint32_t SD_FORMAT_TASK_STACK_BYTES = 4096;
constexpr uint32_t HEALTH_STATUS_TASK_STACK_BYTES = 6144;
constexpr uint32_t SENSOR_STATUS_TASK_STACK_BYTES = 4096;

void format_storage_result_text(esp_err_t err, char *buffer, size_t buffer_size)
{
    if ((buffer == nullptr) || (buffer_size == 0)) {
        return;
    }
    if (err == ESP_OK) {
        std::snprintf(buffer, buffer_size, "TF card formatted.\nPath: /sdcard");
    } else {
        std::snprintf(buffer, buffer_size, "Format failed: %s", esp_err_to_name(err));
    }
}

bool set_label_text_if_changed(lv_obj_t *label, const char *text)
{
    if ((label == nullptr) || (text == nullptr)) {
        return false;
    }
    const char *current = lv_label_get_text(label);
    if ((current != nullptr) && (std::strcmp(current, text) == 0)) {
        return false;
    }
    lv_label_set_text(label, text);
    return true;
}

void power_diag_text(char *buffer, size_t buffer_size)
{
    if ((buffer == nullptr) || (buffer_size == 0)) {
        return;
    }

    esp_pm_config_t pm_config = {};
    const esp_err_t pm_err = esp_pm_get_configuration(&pm_config);
    const int cpu_mhz = esp_clk_cpu_freq() / 1000000;
    const int64_t uptime_s = esp_timer_get_time() / 1000000;
    const uint32_t free_heap_kb = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024;
    const uint32_t largest_heap_kb = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024;
    const uint32_t psram_free_kb = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024;
    const uint32_t psram_largest_kb = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024;

    char wifi_text[96] = {};
    watch::wifi_status_text(wifi_text, sizeof(wifi_text));

    if (pm_err == ESP_OK) {
        std::snprintf(
            buffer,
            buffer_size,
            "Power diag\n"
            "Display: %s  AP left: %ds\n"
            "WiFi: %s\n"
            "CPU: now %dMHz  PM %d-%dMHz  LS %s\n"
            "Heap: %luK  largest %luK\n"
            "PSRAM: %luK  largest %luK\n"
            "Tasks: %u  Uptime: %llds",
            watch::display_is_on() ? "on" : "off",
            watch::wifi_provisioning_remaining_s(),
            wifi_text,
            cpu_mhz,
            pm_config.min_freq_mhz,
            pm_config.max_freq_mhz,
            pm_config.light_sleep_enable ? "on" : "off",
            static_cast<unsigned long>(free_heap_kb),
            static_cast<unsigned long>(largest_heap_kb),
            static_cast<unsigned long>(psram_free_kb),
            static_cast<unsigned long>(psram_largest_kb),
            static_cast<unsigned>(uxTaskGetNumberOfTasks()),
            static_cast<long long>(uptime_s)
        );
    } else {
        std::snprintf(
            buffer,
            buffer_size,
            "Power diag\n"
            "Display: %s  AP left: %ds\n"
            "WiFi: %s\n"
            "CPU: now %dMHz  PM unavailable: %s\n"
            "Heap: %luK  largest %luK\n"
            "PSRAM: %luK  largest %luK\n"
            "Tasks: %u  Uptime: %llds",
            watch::display_is_on() ? "on" : "off",
            watch::wifi_provisioning_remaining_s(),
            wifi_text,
            cpu_mhz,
            esp_err_to_name(pm_err),
            static_cast<unsigned long>(free_heap_kb),
            static_cast<unsigned long>(largest_heap_kb),
            static_cast<unsigned long>(psram_free_kb),
            static_cast<unsigned long>(psram_largest_kb),
            static_cast<unsigned>(uxTaskGetNumberOfTasks()),
            static_cast<long long>(uptime_s)
        );
    }
}

enum SettingsSection {
    SECTION_WLAN = 0,
    SECTION_DISPLAY,
    SECTION_TIME,
    SECTION_POWER,
    SECTION_HEALTH,
    SECTION_SENSOR,
    SECTION_STORAGE,
    SECTION_AUDIO,
    SECTION_ABOUT,
};

lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    watch_display::apply_text_font(label, text, font);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

lv_obj_t *make_card(lv_obj_t *parent, int height)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, height);
    lv_obj_set_style_radius(card, 24, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x15171D), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x2B303A), 0);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(card, 8, 0);
    return card;
}

void format_elapsed_time(uint32_t seconds, char *buffer, size_t buffer_size)
{
    if ((buffer == nullptr) || (buffer_size == 0)) {
        return;
    }
    const uint32_t hours = seconds / 3600;
    const uint32_t minutes = (seconds % 3600) / 60;
    if (hours > 0) {
        std::snprintf(buffer, buffer_size, "%luh %02lum", static_cast<unsigned long>(hours), static_cast<unsigned long>(minutes));
    } else {
        std::snprintf(buffer, buffer_size, "%lum", static_cast<unsigned long>(minutes));
    }
}

void style_button(lv_obj_t *button, uint32_t bg)
{
    lv_obj_set_style_radius(button, 24, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
}

lv_obj_t *make_section_button(
    lv_obj_t *parent,
    const char *title,
    const char *subtitle,
    const lv_image_dsc_t *icon
)
{
    lv_obj_t *button = lv_button_create(parent);
    if (button == nullptr) {
        return nullptr;
    }
    lv_obj_set_width(button, LV_PCT(100));
    lv_obj_set_height(button, 64);
    lv_obj_set_style_radius(button, 16, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x202632), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(0x303947), 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_pad_left(button, 8, 0);
    lv_obj_set_style_pad_right(button, 12, 0);
    lv_obj_set_style_pad_top(button, 8, 0);
    lv_obj_set_style_pad_bottom(button, 8, 0);
    lv_obj_set_style_pad_column(button, 10, 0);
    lv_obj_set_flex_flow(button, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    if (icon != nullptr) {
        lv_obj_t *image = lv_image_create(button);
        if (image == nullptr) {
            lv_obj_delete(button);
            return nullptr;
        }
        lv_image_set_src(image, icon);
        lv_obj_set_size(image, 48, 48);
    }

    lv_obj_t *text_column = lv_obj_create(button);
    if (text_column == nullptr) {
        lv_obj_delete(button);
        return nullptr;
    }
    lv_obj_remove_style_all(text_column);
    lv_obj_set_height(text_column, LV_PCT(100));
    lv_obj_set_flex_grow(text_column, 1);
    lv_obj_set_flex_flow(text_column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(text_column, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(text_column, 2, 0);

    make_label(text_column, title, &lv_font_montserrat_16, 0xFFFFFF);
    lv_obj_t *sub = make_label(text_column, subtitle, &lv_font_montserrat_12, 0x8D98AA);
    lv_obj_set_width(sub, LV_PCT(100));
    lv_label_set_long_mode(sub, LV_LABEL_LONG_DOT);
    return button;
}

void system_info_text(char *buffer, size_t buffer_size)
{
    if ((buffer == nullptr) || (buffer_size == 0)) {
        return;
    }
    const esp_app_desc_t *desc = esp_app_get_description();
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *boot = esp_ota_get_boot_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);

    uint8_t mac[6] = {};
    char mac_text[32] = "MAC unavailable";
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        std::snprintf(mac_text, sizeof(mac_text), "%02X:%02X:%02X:%02X:%02X:%02X",
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }

    std::snprintf(
        buffer,
        buffer_size,
        "Model: Waveshare AMOLED 2.06\n"
        "Version: %s\n"
        "Project: %s\n"
        "Run: %s  Boot: %s  Next: %s\n"
        "Heap: %lu KB\n"
        "PSRAM: %lu KB\n"
        "Uptime: %llu s\n"
        "MAC: %s",
        (desc != nullptr && desc->version[0] != '\0') ? desc->version : "unknown",
        (desc != nullptr && desc->project_name[0] != '\0') ? desc->project_name : "unknown",
        (running != nullptr) ? running->label : "--",
        (boot != nullptr) ? boot->label : "--",
        (next != nullptr) ? next->label : "--",
        static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024),
        static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
        static_cast<unsigned long long>(esp_timer_get_time() / 1000000),
        mac_text
    );
}

const char *reset_reason_text()
{
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "poweron";
    case ESP_RST_EXT: return "external";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt_wdt";
    case ESP_RST_TASK_WDT: return "task_wdt";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deepsleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO: return "sdio";
    default: return "unknown";
    }
}

void health_diag_text(char *buffer, size_t buffer_size)
{
    if ((buffer == nullptr) || (buffer_size == 0)) {
        return;
    }
    char wifi_text[96] = {};
    char wifi_diag[520] = {};
    char power_text[240] = {};
    char display_text[96] = {};
    char audio_text[160] = {};
    char storage_text[240] = {};
    watch::wifi_status_text(wifi_text, sizeof(wifi_text));
    watch::wifi_diag_text(wifi_diag, sizeof(wifi_diag));
    watch::power_status_text(power_text, sizeof(power_text));
    watch::display_status_text(display_text, sizeof(display_text));
    watch::audio_status_text(audio_text, sizeof(audio_text));
    watch::storage_diag_text(storage_text, sizeof(storage_text));

    const esp_app_desc_t *desc = esp_app_get_description();
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *boot = esp_ota_get_boot_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);

    std::snprintf(
        buffer,
        buffer_size,
        "FW: %s  Run: %s  Boot: %s  Next: %s\n"
        "Reset: %s  Uptime: %llus\n"
        "WiFi: %s\n"
        "%s\n"
        "Power: %s\n"
        "Display: %s\n"
        "Audio: %s\n"
        "%s\n"
        "Heap: free %luK largest %luK\n"
        "PSRAM: free %luK largest %luK\n"
        "Tasks: %u\n"
        "OTA target max: %luK",
        (desc != nullptr && desc->version[0] != '\0') ? desc->version : "unknown",
        (running != nullptr) ? running->label : "--",
        (boot != nullptr) ? boot->label : "--",
        (next != nullptr) ? next->label : "--",
        reset_reason_text(),
        static_cast<unsigned long long>(esp_timer_get_time() / 1000000),
        wifi_text,
        wifi_diag,
        power_text,
        display_text,
        audio_text,
        storage_text,
        static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
        static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
        static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
        static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024),
        static_cast<unsigned>(uxTaskGetNumberOfTasks()),
        static_cast<unsigned long>((next != nullptr ? next->size : 0) / 1024)
    );
}
} // namespace

SettingsApp *SettingsApp::_instance = nullptr;

SettingsApp *SettingsApp::requestInstance()
{
    if (_instance == nullptr) {
        _instance = new SettingsApp();
    }
    return _instance;
}

SettingsApp::SettingsApp():
    systems::phone::App(APP_NAME, watch_app_icon_settings_48(), true, true, true)
{
}

bool SettingsApp::run(void)
{
    lv_obj_t *screen = lv_scr_act();
    ESP_UTILS_CHECK_NULL_RETURN(screen, false, "Invalid active screen");

    _settings_menu_card = nullptr;
    _settings_root = nullptr;
    _section_card_count = 0;
    _current_section = -1;
    _sd_format_armed = false;
    _storage_refresh_tick = 0;
    _storage_retry_interval_ms = STORAGE_REFRESH_INTERVAL_MS;
    _diagnostics_refresh_tick = 0;
    ++_page_generation;

    lv_obj_set_style_bg_color(screen, lv_color_hex(0x07080B), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    lv_obj_t *root = lv_obj_create(screen);
    ESP_UTILS_CHECK_NULL_RETURN(root, false, "Create root failed");
    _settings_root = root;
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root, lv_color_hex(0x07080B), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(root, SAFE_SIDE, 0);
    lv_obj_set_style_pad_right(root, SAFE_SIDE, 0);
    lv_obj_set_style_pad_top(root, SAFE_TOP, 0);
    lv_obj_set_style_pad_bottom(root, 24, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(root, 12, 0);
    lv_obj_set_scroll_dir(root, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(root, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t *title_row = lv_obj_create(root);
    ESP_UTILS_CHECK_NULL_RETURN(title_row, false, "Create title row failed");
    lv_obj_remove_style_all(title_row);
    lv_obj_set_width(title_row, LV_PCT(100));
    lv_obj_set_height(title_row, 52);
    lv_obj_set_flex_flow(title_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title = make_label(title_row, "Settings", &lv_font_montserrat_30, 0xFFFFFF);
    lv_obj_set_style_text_letter_space(title, 0, 0);

    lv_obj_t *back_btn = lv_button_create(title_row);
    ESP_UTILS_CHECK_NULL_RETURN(back_btn, false, "Create back button failed");
    lv_obj_set_size(back_btn, 92, 44);
    style_button(back_btn, 0x232833);
    lv_obj_add_event_cb(back_btn, onBackClicked, LV_EVENT_CLICKED, this);
    lv_obj_t *back_label = make_label(back_btn, "Back", &lv_font_montserrat_18, 0xFFFFFF);
    lv_obj_center(back_label);

    _settings_menu_card = make_card(root, 760);
    make_label(_settings_menu_card, "Settings Menu", &lv_font_montserrat_20, 0xFFFFFF);
    lv_obj_t *menu_hint = make_label(_settings_menu_card, "Choose a category. Back returns here first.", &lv_font_montserrat_12, 0x717B8C);
    lv_obj_set_width(menu_hint, LV_PCT(100));
    lv_label_set_long_mode(menu_hint, LV_LABEL_LONG_WRAP);

    auto add_section = [&](const char *title_text, const char *subtitle, const lv_image_dsc_t *icon, SettingsSection section) {
        lv_obj_t *button = make_section_button(_settings_menu_card, title_text, subtitle, icon);
        ESP_UTILS_CHECK_NULL_RETURN(button, false, "Create settings section button failed");
        lv_obj_add_event_cb(
            button,
            onSettingsSectionClicked,
            LV_EVENT_CLICKED,
            reinterpret_cast<void *>(static_cast<uintptr_t>(section))
        );
        return true;
    };
    ESP_UTILS_CHECK_FALSE_RETURN(add_section("System Health", "Diagnostics: heap, WiFi, SD, audio, OTA", watch_app_icon_quick_48(), SECTION_HEALTH), false, "Add Health section failed");
    ESP_UTILS_CHECK_FALSE_RETURN(add_section("WLAN", "Scan, connect, QR setup, saved WiFi", watch_app_icon_cloud_48(), SECTION_WLAN), false, "Add WLAN section failed");
    ESP_UTILS_CHECK_FALSE_RETURN(add_section("Display", "Brightness, timeout, raise wake", watch_app_icon_settings_48(), SECTION_DISPLAY), false, "Add Display section failed");
    ESP_UTILS_CHECK_FALSE_RETURN(add_section("Time / RTC", "SNTP sync and RTC restore", watch_app_icon_timer_48(), SECTION_TIME), false, "Add Time section failed");
    ESP_UTILS_CHECK_FALSE_RETURN(add_section("Battery / Power", "Usage, hourly history, charge cycle", watch_app_icon_power_48(), SECTION_POWER), false, "Add Power section failed");
    ESP_UTILS_CHECK_FALSE_RETURN(add_section("Sensor / IMU", "Motion and attitude data", watch_app_icon_sensors_48(), SECTION_SENSOR), false, "Add Sensor section failed");
    ESP_UTILS_CHECK_FALSE_RETURN(add_section("Storage", "LittleFS, TF card, format", watch_app_icon_files_48(), SECTION_STORAGE), false, "Add Storage section failed");
    ESP_UTILS_CHECK_FALSE_RETURN(add_section("Audio", "Volume and audio service status", watch_app_icon_audio_48(), SECTION_AUDIO), false, "Add Audio section failed");
    ESP_UTILS_CHECK_FALSE_RETURN(add_section("About", "Firmware, run/boot/next partition, heap, MAC", watch_app_icon_home_48(), SECTION_ABOUT), false, "Add About section failed");

    showSettingsMenu();
    _timer = lv_timer_create(onTimer, SETTINGS_TIMER_IDLE_MS, this);
    return true;
}

bool SettingsApp::back(void)
{
    if (_current_section >= 0) {
        showSettingsMenu();
        return true;
    }
    ESP_UTILS_LOGI("Back to app launcher");
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

bool SettingsApp::close(void)
{
    ++_page_generation;
    if (_timer != nullptr) {
        lv_timer_delete(_timer);
        _timer = nullptr;
    }
    clearSectionCards();
    _settings_root = nullptr;
    _settings_menu_card = nullptr;
    for (int i = 0; i < MAX_SECTION_CARDS; ++i) {
        _section_cards[i] = nullptr;
        _section_ids[i] = 0;
    }
    _section_card_count = 0;
    _current_section = -1;
    _wifi_value_label = nullptr;
    _wifi_setup_label = nullptr;
    _wifi_qr_card = nullptr;
    _wifi_qr = nullptr;
    _wifi_saved_label = nullptr;
    _wifi_scan_label = nullptr;
    closeWifiPasswordDialog();
    for (int i = 0; i < MAX_WIFI_SCAN_BUTTONS; ++i) {
        _wifi_network_buttons[i] = nullptr;
        _wifi_network_labels[i] = nullptr;
    }
    _time_value_label = nullptr;
    _time_action_label = nullptr;
    _battery_value_label = nullptr;
    _battery_percent_label = nullptr;
    _battery_fill = nullptr;
    _power_cycle_label = nullptr;
    _power_chart = nullptr;
    _power_discharge_series = nullptr;
    _power_charge_series = nullptr;
    _power_chart_detail_label = nullptr;
    _power_rank_title_label = nullptr;
    for (int i = 0; i < POWER_RANK_LABELS; ++i) {
        _power_rank_labels[i] = nullptr;
    }
    _power_diag_label = nullptr;
    _health_value_label = nullptr;
    _sensor_value_label = nullptr;
    _storage_value_label = nullptr;
    _audio_value_label = nullptr;
    _brightness_value_label = nullptr;
    _screen_timeout_label = nullptr;
    _raise_wake_switch = nullptr;
    _display_status_label = nullptr;
    _about_value_label = nullptr;
    _storage_status_task = nullptr;
    _sd_format_task = nullptr;
    _health_status_task = nullptr;
    _sensor_status_task = nullptr;
    return true;
}

void SettingsApp::onBackClicked(lv_event_t *event)
{
    auto *app = static_cast<SettingsApp *>(lv_event_get_user_data(event));
    if (app != nullptr) {
        app->back();
    }
}

void SettingsApp::onBrightnessChanged(lv_event_t *event)
{
    auto *app = static_cast<SettingsApp *>(lv_event_get_user_data(event));
    lv_obj_t *slider = static_cast<lv_obj_t *>(lv_event_get_target(event));
    if ((app == nullptr) || (slider == nullptr)) {
        return;
    }
    int value = static_cast<int>(lv_slider_get_value(slider));
    static int last_brightness = -1;
    if (last_brightness != value) {
        bsp_display_brightness_set(value);
        last_brightness = value;
    }

    if (app->_brightness_value_label != nullptr) {
        char text[16] = {};
        std::snprintf(text, sizeof(text), "%d%%", value);
        lv_label_set_text(app->_brightness_value_label, text);
    }

    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        watch::display_set_brightness_percent(value);
    }
}

void SettingsApp::onRefreshClicked(lv_event_t *event)
{
    auto *app = static_cast<SettingsApp *>(lv_event_get_user_data(event));
    if (app != nullptr) {
        if (app->_current_section == SECTION_STORAGE) {
            app->requestStorageRefresh(true);
        }
        if (app->_current_section == SECTION_POWER) {
            app->_diagnostics_refresh_tick = 0;
        }
        app->requestDiagnosticsRefresh(true);
        app->refreshStatus(false);
    }
}

void SettingsApp::onSettingsSectionClicked(lv_event_t *event)
{
    if (_instance == nullptr) {
        return;
    }
    int section = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
    _instance->showSettingsSection(section);
}

void SettingsApp::registerSectionCard(int section, lv_obj_t *card)
{
    if ((card == nullptr) || (_section_card_count >= MAX_SECTION_CARDS)) {
        return;
    }
    _section_ids[_section_card_count] = section;
    _section_cards[_section_card_count] = card;
    ++_section_card_count;
}

void SettingsApp::clearSectionCards()
{
    closeWifiPasswordDialog();
    for (int i = 0; i < _section_card_count; ++i) {
        if (_section_cards[i] != nullptr) {
            lv_obj_delete(_section_cards[i]);
            _section_cards[i] = nullptr;
        }
        _section_ids[i] = 0;
    }
    _section_card_count = 0;
    _wifi_value_label = nullptr;
    _wifi_setup_label = nullptr;
    _wifi_qr_card = nullptr;
    _wifi_qr = nullptr;
    _wifi_saved_label = nullptr;
    _wifi_scan_label = nullptr;
    for (int i = 0; i < MAX_WIFI_SCAN_BUTTONS; ++i) {
        _wifi_network_buttons[i] = nullptr;
        _wifi_network_labels[i] = nullptr;
    }
    _time_value_label = nullptr;
    _time_action_label = nullptr;
    _battery_value_label = nullptr;
    _battery_percent_label = nullptr;
    _battery_fill = nullptr;
    _power_cycle_label = nullptr;
    _power_chart = nullptr;
    _power_discharge_series = nullptr;
    _power_charge_series = nullptr;
    _power_chart_detail_label = nullptr;
    _power_rank_title_label = nullptr;
    for (int i = 0; i < POWER_RANK_LABELS; ++i) {
        _power_rank_labels[i] = nullptr;
    }
    _power_diag_label = nullptr;
    _health_value_label = nullptr;
    _sensor_value_label = nullptr;
    _storage_value_label = nullptr;
    _audio_value_label = nullptr;
    _brightness_value_label = nullptr;
    _screen_timeout_label = nullptr;
    _raise_wake_switch = nullptr;
    _display_status_label = nullptr;
    _about_value_label = nullptr;
    _storage_refresh_tick = 0;
    _sd_format_armed = false;
}

bool SettingsApp::createSectionCards(int section)
{
    if (_settings_root == nullptr) {
        return false;
    }

    auto add_button = [&](lv_obj_t *parent, const char *text, uint32_t color, lv_event_cb_t cb) -> bool {
        lv_obj_t *button = lv_button_create(parent);
        ESP_UTILS_CHECK_NULL_RETURN(button, false, "Create settings action button failed");
        lv_obj_set_width(button, LV_PCT(100));
        lv_obj_set_height(button, 44);
        style_button(button, color);
        lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, this);
        lv_obj_center(make_label(button, text, &lv_font_montserrat_16, 0xFFFFFF));
        return true;
    };

    auto add_row_button = [&](lv_obj_t *row, const char *text, uint32_t color, lv_event_cb_t cb) -> bool {
        lv_obj_t *button = lv_button_create(row);
        ESP_UTILS_CHECK_NULL_RETURN(button, false, "Create settings row button failed");
        lv_obj_set_width(button, LV_PCT(48));
        lv_obj_set_height(button, 44);
        style_button(button, color);
        lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, this);
        lv_obj_center(make_label(button, text, &lv_font_montserrat_16, 0xFFFFFF));
        return true;
    };

    switch (section) {
    case SECTION_WLAN: {
        lv_obj_t *wifi_card = make_card(_settings_root, 354);
        ESP_UTILS_CHECK_NULL_RETURN(wifi_card, false, "Create WLAN card failed");
        make_label(wifi_card, "WLAN", &lv_font_montserrat_20, 0xFFFFFF);
        _wifi_value_label = make_label(wifi_card, "Checking WiFi...", &lv_font_montserrat_16, 0xAEB7C6);
        _wifi_setup_label = make_label(wifi_card, "Tap setup to create Watch-Setup AP", &lv_font_montserrat_14, 0x717B8C);
        ESP_UTILS_CHECK_FALSE_RETURN(add_button(wifi_card, "Start WiFi Setup", 0x1B6BFF, onStartWifiSetupClicked), false, "Add WiFi setup failed");
        ESP_UTILS_CHECK_FALSE_RETURN(add_button(wifi_card, "Keyboard WiFi", 0x6F45FF, onManualWifiClicked), false, "Add WiFi keyboard failed");
        ESP_UTILS_CHECK_FALSE_RETURN(add_button(wifi_card, "Forget WiFi", 0xD9322E, onForgetWifiClicked), false, "Add WiFi forget failed");

        lv_obj_t *wifi_action_row = lv_obj_create(wifi_card);
        ESP_UTILS_CHECK_NULL_RETURN(wifi_action_row, false, "Create WiFi action row failed");
        lv_obj_remove_style_all(wifi_action_row);
        lv_obj_set_width(wifi_action_row, LV_PCT(100));
        lv_obj_set_height(wifi_action_row, 46);
        lv_obj_set_flex_flow(wifi_action_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(wifi_action_row, 10, 0);
        ESP_UTILS_CHECK_FALSE_RETURN(add_row_button(wifi_action_row, "Reconnect", 0x23A559, onReconnectWifiClicked), false, "Add reconnect failed");
        ESP_UTILS_CHECK_FALSE_RETURN(add_row_button(wifi_action_row, "Scan", 0x232833, onScanWifiClicked), false, "Add scan failed");

        registerSectionCard(section, wifi_card);

        _wifi_qr_card = make_card(_settings_root, 208);
        ESP_UTILS_CHECK_NULL_RETURN(_wifi_qr_card, false, "Create WiFi QR card failed");
        make_label(_wifi_qr_card, "WiFi Setup QR", &lv_font_montserrat_20, 0xFFFFFF);
        lv_obj_t *qr_hint = make_label(_wifi_qr_card, "Start setup, connect phone to Watch-Setup AP, then scan/open this URL QR.", &lv_font_montserrat_12, 0xAEB7C6);
        lv_obj_set_width(qr_hint, LV_PCT(100));
        lv_label_set_long_mode(qr_hint, LV_LABEL_LONG_WRAP);
#if LV_USE_QRCODE
        _wifi_qr = lv_qrcode_create(_wifi_qr_card);
        ESP_UTILS_CHECK_NULL_RETURN(_wifi_qr, false, "Create WiFi QR failed");
        lv_qrcode_set_size(_wifi_qr, 112);
        lv_qrcode_set_dark_color(_wifi_qr, lv_color_hex(0x111111));
        lv_qrcode_set_light_color(_wifi_qr, lv_color_hex(0xFFFFFF));
        lv_qrcode_set_data(_wifi_qr, "http://192.168.4.1/");
#else
        make_label(_wifi_qr_card, "QR disabled in LVGL config", &lv_font_montserrat_14, 0xFFB020);
#endif
        lv_obj_add_flag(_wifi_qr_card, LV_OBJ_FLAG_HIDDEN);
        registerSectionCard(section, _wifi_qr_card);

        lv_obj_t *saved_card = make_card(_settings_root, 120);
        ESP_UTILS_CHECK_NULL_RETURN(saved_card, false, "Create saved WiFi card failed");
        make_label(saved_card, "Saved WiFi", &lv_font_montserrat_20, 0xFFFFFF);
        _wifi_saved_label = make_label(saved_card, "No saved WiFi", &lv_font_montserrat_14, 0xAEB7C6);
        lv_obj_set_width(_wifi_saved_label, LV_PCT(100));
        lv_label_set_long_mode(_wifi_saved_label, LV_LABEL_LONG_WRAP);
        registerSectionCard(section, saved_card);

        lv_obj_t *scan_card = make_card(_settings_root, 380);
        ESP_UTILS_CHECK_NULL_RETURN(scan_card, false, "Create WiFi scan card failed");
        make_label(scan_card, "Nearby WiFi", &lv_font_montserrat_20, 0xFFFFFF);
        _wifi_scan_label = make_label(scan_card, "Tap Scan, then tap a network to enter password on watch.", &lv_font_montserrat_14, 0xAEB7C6);
        lv_obj_set_width(_wifi_scan_label, LV_PCT(100));
        lv_label_set_long_mode(_wifi_scan_label, LV_LABEL_LONG_WRAP);
        for (int i = 0; i < MAX_WIFI_SCAN_BUTTONS; ++i) {
            _wifi_network_buttons[i] = lv_button_create(scan_card);
            ESP_UTILS_CHECK_NULL_RETURN(_wifi_network_buttons[i], false, "Create WiFi network button failed");
            lv_obj_set_width(_wifi_network_buttons[i], LV_PCT(100));
            lv_obj_set_height(_wifi_network_buttons[i], 36);
            style_button(_wifi_network_buttons[i], 0x232833);
            lv_obj_add_event_cb(_wifi_network_buttons[i], onScanNetworkClicked, LV_EVENT_CLICKED, this);
            _wifi_network_labels[i] = make_label(_wifi_network_buttons[i], "", &lv_font_montserrat_14, 0xFFFFFF);
            lv_obj_center(_wifi_network_labels[i]);
            lv_obj_add_flag(_wifi_network_buttons[i], LV_OBJ_FLAG_HIDDEN);
        }
        registerSectionCard(section, scan_card);
        updateWifiScanButtons();
        return true;
    }
    case SECTION_DISPLAY: {
        lv_obj_t *card = make_card(_settings_root, 264);
        ESP_UTILS_CHECK_NULL_RETURN(card, false, "Create display card failed");
        make_label(card, "Display", &lv_font_montserrat_20, 0xFFFFFF);
        _brightness_value_label = make_label(card, "Brightness --%", &lv_font_montserrat_16, 0xAEB7C6);
        lv_obj_t *slider = lv_slider_create(card);
        ESP_UTILS_CHECK_NULL_RETURN(slider, false, "Create brightness slider failed");
        lv_obj_set_width(slider, LV_PCT(100));
        lv_slider_set_range(slider, 5, 100);
        lv_slider_set_value(slider, watch::display_get_settings().brightness_percent, LV_ANIM_OFF);
        lv_obj_add_event_cb(slider, onBrightnessChanged, LV_EVENT_VALUE_CHANGED, this);
        lv_obj_add_event_cb(slider, onBrightnessChanged, LV_EVENT_RELEASED, this);
        lv_obj_add_event_cb(slider, onBrightnessChanged, LV_EVENT_PRESS_LOST, this);
        _screen_timeout_label = make_label(card, "Screen timeout --s", &lv_font_montserrat_16, 0xAEB7C6);
        lv_obj_t *timeout_slider = lv_slider_create(card);
        ESP_UTILS_CHECK_NULL_RETURN(timeout_slider, false, "Create timeout slider failed");
        lv_obj_set_width(timeout_slider, LV_PCT(100));
        lv_slider_set_range(timeout_slider, 5, 120);
        lv_slider_set_value(timeout_slider, watch::display_get_settings().screen_timeout_s, LV_ANIM_OFF);
        lv_obj_add_event_cb(timeout_slider, onScreenTimeoutChanged, LV_EVENT_VALUE_CHANGED, this);
        lv_obj_add_event_cb(timeout_slider, onScreenTimeoutChanged, LV_EVENT_RELEASED, this);
        lv_obj_add_event_cb(timeout_slider, onScreenTimeoutChanged, LV_EVENT_PRESS_LOST, this);
        lv_obj_t *raise_row = lv_obj_create(card);
        ESP_UTILS_CHECK_NULL_RETURN(raise_row, false, "Create raise wake row failed");
        lv_obj_remove_style_all(raise_row);
        lv_obj_set_width(raise_row, LV_PCT(100));
        lv_obj_set_height(raise_row, 42);
        lv_obj_set_flex_flow(raise_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(raise_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        make_label(raise_row, "Raise wake", &lv_font_montserrat_16, 0xD7DCE5);
        _raise_wake_switch = lv_switch_create(raise_row);
        ESP_UTILS_CHECK_NULL_RETURN(_raise_wake_switch, false, "Create raise wake switch failed");
        lv_obj_set_size(_raise_wake_switch, 58, 32);
        if (watch::display_get_settings().raise_wake_enabled) {
            lv_obj_add_state(_raise_wake_switch, LV_STATE_CHECKED);
        }
        lv_obj_add_event_cb(_raise_wake_switch, onRaiseWakeChanged, LV_EVENT_VALUE_CHANGED, this);
        _display_status_label = make_label(card, "Display status pending", &lv_font_montserrat_12, 0x717B8C);
        lv_obj_set_width(_display_status_label, LV_PCT(100));
        lv_label_set_long_mode(_display_status_label, LV_LABEL_LONG_WRAP);
        registerSectionCard(section, card);
        return true;
    }
    case SECTION_TIME: {
        lv_obj_t *card = make_card(_settings_root, 214);
        ESP_UTILS_CHECK_NULL_RETURN(card, false, "Create time card failed");
        make_label(card, "Time / RTC", &lv_font_montserrat_20, 0xFFFFFF);
        _time_value_label = make_label(card, "Checking time...", &lv_font_montserrat_14, 0xAEB7C6);
        lv_obj_set_width(_time_value_label, LV_PCT(100));
        lv_label_set_long_mode(_time_value_label, LV_LABEL_LONG_WRAP);
        _time_action_label = make_label(card, "Use WiFi for SNTP sync. RTC keeps time after reboot.", &lv_font_montserrat_12, 0x717B8C);
        lv_obj_set_width(_time_action_label, LV_PCT(100));
        lv_label_set_long_mode(_time_action_label, LV_LABEL_LONG_WRAP);
        lv_obj_t *row = lv_obj_create(card);
        ESP_UTILS_CHECK_NULL_RETURN(row, false, "Create time row failed");
        lv_obj_remove_style_all(row);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 46);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(row, 10, 0);
        ESP_UTILS_CHECK_FALSE_RETURN(add_row_button(row, "Sync NTP", 0x1B6BFF, onSyncNtpClicked), false, "Add NTP failed");
        ESP_UTILS_CHECK_FALSE_RETURN(add_row_button(row, "RTC -> Sys", 0x232833, onRtcToSystemClicked), false, "Add RTC failed");
        registerSectionCard(section, card);
        return true;
    }
    case SECTION_POWER: {
        lv_obj_t *summary_card = make_card(_settings_root, 220);
        ESP_UTILS_CHECK_NULL_RETURN(summary_card, false, "Create battery summary card failed");
        lv_obj_t *summary_header = lv_obj_create(summary_card);
        ESP_UTILS_CHECK_NULL_RETURN(summary_header, false, "Create battery summary header failed");
        lv_obj_remove_style_all(summary_header);
        lv_obj_set_width(summary_header, LV_PCT(100));
        lv_obj_set_height(summary_header, 48);
        lv_obj_set_flex_flow(summary_header, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(summary_header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(summary_header, 10, 0);
        lv_obj_t *summary_image = lv_image_create(summary_header);
        ESP_UTILS_CHECK_NULL_RETURN(summary_image, false, "Create power monitor icon failed");
        lv_image_set_src(summary_image, watch_app_icon_power_48());
        lv_obj_set_size(summary_image, 48, 48);
        lv_obj_t *summary_title_column = lv_obj_create(summary_header);
        ESP_UTILS_CHECK_NULL_RETURN(summary_title_column, false, "Create battery summary title failed");
        lv_obj_remove_style_all(summary_title_column);
        lv_obj_set_height(summary_title_column, LV_PCT(100));
        lv_obj_set_flex_grow(summary_title_column, 1);
        lv_obj_set_flex_flow(summary_title_column, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(summary_title_column, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        make_label(summary_title_column, "Energy", &lv_font_montserrat_20, 0xFFFFFF);
        make_label(summary_title_column, "Battery monitor", &lv_font_montserrat_12, 0x8D98AA);
        lv_obj_t *summary_row = lv_obj_create(summary_card);
        ESP_UTILS_CHECK_NULL_RETURN(summary_row, false, "Create battery summary row failed");
        lv_obj_remove_style_all(summary_row);
        lv_obj_set_width(summary_row, LV_PCT(100));
        lv_obj_set_height(summary_row, 76);
        lv_obj_set_flex_flow(summary_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(summary_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(summary_row, 12, 0);

        lv_obj_t *battery_icon = lv_obj_create(summary_row);
        ESP_UTILS_CHECK_NULL_RETURN(battery_icon, false, "Create battery icon failed");
        lv_obj_remove_style_all(battery_icon);
        lv_obj_set_size(battery_icon, 96, 64);
        lv_obj_t *battery_shell = lv_obj_create(battery_icon);
        lv_obj_remove_style_all(battery_shell);
        lv_obj_set_size(battery_shell, 82, 48);
        lv_obj_align(battery_shell, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_radius(battery_shell, 8, 0);
        lv_obj_set_style_border_width(battery_shell, 3, 0);
        lv_obj_set_style_border_color(battery_shell, lv_color_hex(0xAEB7C6), 0);
        lv_obj_set_style_bg_opa(battery_shell, LV_OPA_TRANSP, 0);
        _battery_fill = lv_obj_create(battery_shell);
        ESP_UTILS_CHECK_NULL_RETURN(_battery_fill, false, "Create battery fill failed");
        lv_obj_remove_style_all(_battery_fill);
        lv_obj_set_size(_battery_fill, 64, 34);
        lv_obj_align(_battery_fill, LV_ALIGN_LEFT_MID, 4, 0);
        lv_obj_set_style_radius(_battery_fill, 4, 0);
        lv_obj_set_style_bg_color(_battery_fill, lv_color_hex(0x31C16B), 0);
        lv_obj_set_style_bg_opa(_battery_fill, LV_OPA_COVER, 0);
        lv_obj_t *battery_tip = lv_obj_create(battery_icon);
        lv_obj_remove_style_all(battery_tip);
        lv_obj_set_size(battery_tip, 8, 20);
        lv_obj_align(battery_tip, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_radius(battery_tip, 3, 0);
        lv_obj_set_style_bg_color(battery_tip, lv_color_hex(0xAEB7C6), 0);
        lv_obj_set_style_bg_opa(battery_tip, LV_OPA_COVER, 0);

        lv_obj_t *battery_meta = lv_obj_create(summary_row);
        ESP_UTILS_CHECK_NULL_RETURN(battery_meta, false, "Create battery details failed");
        lv_obj_remove_style_all(battery_meta);
        lv_obj_set_height(battery_meta, LV_PCT(100));
        lv_obj_set_flex_grow(battery_meta, 1);
        lv_obj_set_flex_flow(battery_meta, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(battery_meta, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        _battery_percent_label = make_label(battery_meta, "--%", &lv_font_montserrat_28, 0xFFFFFF);
        _battery_value_label = make_label(battery_meta, "Checking battery...", &lv_font_montserrat_14, 0xAEB7C6);
        lv_obj_set_width(_battery_value_label, LV_PCT(100));
        lv_label_set_long_mode(_battery_value_label, LV_LABEL_LONG_DOT);
        _power_cycle_label = make_label(summary_card, "Checking current charge cycle...", &lv_font_montserrat_14, 0xAEB7C6);
        lv_obj_set_width(_power_cycle_label, LV_PCT(100));
        lv_label_set_long_mode(_power_cycle_label, LV_LABEL_LONG_WRAP);
        registerSectionCard(section, summary_card);

        lv_obj_t *chart_card = make_card(_settings_root, 252);
        ESP_UTILS_CHECK_NULL_RETURN(chart_card, false, "Create power chart card failed");
        make_label(chart_card, "24-hour charge and drain", &lv_font_montserrat_20, 0xFFFFFF);
        _power_chart_detail_label = make_label(chart_card, "Collecting hourly hardware samples...", &lv_font_montserrat_12, 0xAEB7C6);
        lv_obj_set_width(_power_chart_detail_label, LV_PCT(100));
        lv_label_set_long_mode(_power_chart_detail_label, LV_LABEL_LONG_DOT);
        _power_chart = lv_chart_create(chart_card);
        ESP_UTILS_CHECK_NULL_RETURN(_power_chart, false, "Create power chart failed");
        lv_obj_set_width(_power_chart, LV_PCT(100));
        lv_obj_set_height(_power_chart, 142);
        lv_chart_set_type(_power_chart, LV_CHART_TYPE_LINE);
        lv_chart_set_point_count(_power_chart, watch::POWER_USAGE_HISTORY_POINTS);
        lv_chart_set_axis_range(_power_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
        lv_chart_set_div_line_count(_power_chart, 4, 3);
        lv_obj_set_style_bg_color(_power_chart, lv_color_hex(0x0E1117), 0);
        lv_obj_set_style_bg_opa(_power_chart, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(_power_chart, 0, 0);
        lv_obj_set_style_pad_all(_power_chart, 10, 0);
        _power_discharge_series = lv_chart_add_series(_power_chart, lv_color_hex(0x3B8CFF), LV_CHART_AXIS_PRIMARY_Y);
        _power_charge_series = lv_chart_add_series(_power_chart, lv_color_hex(0x31C16B), LV_CHART_AXIS_PRIMARY_Y);
        ESP_UTILS_CHECK_NULL_RETURN(_power_discharge_series, false, "Create discharge series failed");
        ESP_UTILS_CHECK_NULL_RETURN(_power_charge_series, false, "Create charge series failed");
        lv_chart_set_all_values(_power_chart, _power_discharge_series, LV_CHART_POINT_NONE);
        lv_chart_set_all_values(_power_chart, _power_charge_series, LV_CHART_POINT_NONE);
        lv_obj_t *chart_footer = lv_obj_create(chart_card);
        lv_obj_remove_style_all(chart_footer);
        lv_obj_set_width(chart_footer, LV_PCT(100));
        lv_obj_set_height(chart_footer, 16);
        lv_obj_set_flex_flow(chart_footer, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(chart_footer, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        make_label(chart_footer, "24h ago", &lv_font_montserrat_12, 0x717B8C);
        make_label(chart_footer, "12h", &lv_font_montserrat_12, 0x717B8C);
        make_label(chart_footer, "Now", &lv_font_montserrat_12, 0x717B8C);
        registerSectionCard(section, chart_card);

        lv_obj_t *rank_card = make_card(_settings_root, 242);
        ESP_UTILS_CHECK_NULL_RETURN(rank_card, false, "Create power rank card failed");
        make_label(rank_card, "Energy usage ranking", &lv_font_montserrat_20, 0xFFFFFF);
        _power_rank_title_label = make_label(rank_card, "Estimated foreground load", &lv_font_montserrat_12, 0x717B8C);
        lv_obj_set_width(_power_rank_title_label, LV_PCT(100));
        for (int i = 0; i < POWER_RANK_LABELS; ++i) {
            _power_rank_labels[i] = make_label(rank_card, "No foreground app time yet", &lv_font_montserrat_14, 0xD7DCE5);
            lv_obj_set_width(_power_rank_labels[i], LV_PCT(100));
            lv_label_set_long_mode(_power_rank_labels[i], LV_LABEL_LONG_DOT);
        }
        registerSectionCard(section, rank_card);

        lv_obj_t *diagnostic_card = make_card(_settings_root, 226);
        ESP_UTILS_CHECK_NULL_RETURN(diagnostic_card, false, "Create power diagnostics card failed");
        make_label(diagnostic_card, "Power diagnostics", &lv_font_montserrat_20, 0xFFFFFF);
        _power_diag_label = make_label(diagnostic_card, "Checking power diagnostics...", &lv_font_montserrat_12, 0x717B8C);
        lv_obj_set_width(_power_diag_label, LV_PCT(100));
        lv_label_set_long_mode(_power_diag_label, LV_LABEL_LONG_WRAP);
        registerSectionCard(section, diagnostic_card);
        return true;
    }
    case SECTION_HEALTH: {
        lv_obj_t *card = make_card(_settings_root, 560);
        ESP_UTILS_CHECK_NULL_RETURN(card, false, "Create health card failed");
        make_label(card, "System Health", &lv_font_montserrat_20, 0xFFFFFF);
        _health_value_label = make_label(card, "Checking health...", &lv_font_montserrat_12, 0xAEB7C6);
        lv_obj_set_width(_health_value_label, LV_PCT(100));
        lv_label_set_long_mode(_health_value_label, LV_LABEL_LONG_WRAP);
        registerSectionCard(section, card);
        return true;
    }
    case SECTION_SENSOR: {
        lv_obj_t *card = make_card(_settings_root, 260);
        ESP_UTILS_CHECK_NULL_RETURN(card, false, "Create sensor card failed");
        make_label(card, "Sensor / IMU", &lv_font_montserrat_20, 0xFFFFFF);
        _sensor_value_label = make_label(card, "Checking sensor...", &lv_font_montserrat_14, 0xAEB7C6);
        lv_obj_set_width(_sensor_value_label, LV_PCT(100));
        lv_label_set_long_mode(_sensor_value_label, LV_LABEL_LONG_WRAP);
        registerSectionCard(section, card);
        return true;
    }
    case SECTION_STORAGE: {
        lv_obj_t *card = make_card(_settings_root, 228);
        ESP_UTILS_CHECK_NULL_RETURN(card, false, "Create storage card failed");
        make_label(card, "Storage", &lv_font_montserrat_20, 0xFFFFFF);
        _storage_value_label = make_label(card, "Checking storage...", &lv_font_montserrat_14, 0xAEB7C6);
        lv_obj_set_width(_storage_value_label, LV_PCT(100));
        lv_label_set_long_mode(_storage_value_label, LV_LABEL_LONG_WRAP);
        ESP_UTILS_CHECK_FALSE_RETURN(add_button(card, "Format TF (tap twice)", 0xD9322E, onFormatSdClicked), false, "Add format failed");
        registerSectionCard(section, card);
        requestStorageRefresh(true);
        return true;
    }
    case SECTION_AUDIO: {
        lv_obj_t *card = make_card(_settings_root, 154);
        ESP_UTILS_CHECK_NULL_RETURN(card, false, "Create audio card failed");
        make_label(card, "Audio", &lv_font_montserrat_20, 0xFFFFFF);
        _audio_value_label = make_label(card, "Checking audio...", &lv_font_montserrat_14, 0xAEB7C6);
        lv_obj_set_width(_audio_value_label, LV_PCT(100));
        lv_label_set_long_mode(_audio_value_label, LV_LABEL_LONG_WRAP);
        registerSectionCard(section, card);
        return true;
    }
    case SECTION_ABOUT: {
        lv_obj_t *card = make_card(_settings_root, 232);
        ESP_UTILS_CHECK_NULL_RETURN(card, false, "Create about card failed");
        make_label(card, "About", &lv_font_montserrat_20, 0xFFFFFF);
        _about_value_label = make_label(card, "Checking system...", &lv_font_montserrat_14, 0xAEB7C6);
        lv_obj_set_width(_about_value_label, LV_PCT(100));
        lv_label_set_long_mode(_about_value_label, LV_LABEL_LONG_WRAP);
        registerSectionCard(section, card);
        return true;
    }
    default:
        return false;
    }
}

void SettingsApp::showSettingsMenu()
{
    clearSectionCards();
    ++_page_generation;
    _current_section = -1;
    _sd_format_armed = false;
    if (_settings_menu_card != nullptr) {
        lv_obj_clear_flag(_settings_menu_card, LV_OBJ_FLAG_HIDDEN);
        lv_obj_t *root = lv_obj_get_parent(_settings_menu_card);
        if (root != nullptr) {
            lv_obj_scroll_to_y(root, 0, LV_ANIM_OFF);
        }
    }
}

void SettingsApp::showSettingsSection(int section)
{
    clearSectionCards();
    ++_page_generation;
    _current_section = section;
    _diagnostics_refresh_tick = 0;
    _sd_format_armed = false;
    if (_settings_menu_card != nullptr) {
        lv_obj_add_flag(_settings_menu_card, LV_OBJ_FLAG_HIDDEN);
        lv_obj_t *root = lv_obj_get_parent(_settings_menu_card);
        if (root != nullptr) {
            lv_obj_scroll_to_y(root, 0, LV_ANIM_OFF);
        }
    }
    if (!createSectionCards(section)) {
        showSettingsMenu();
        return;
    }
    requestDiagnosticsRefresh(true);
    refreshStatus(false);
}

void SettingsApp::onStartWifiSetupClicked(lv_event_t *event)
{
    auto *app = static_cast<SettingsApp *>(lv_event_get_user_data(event));
    if (app == nullptr) {
        return;
    }
    ESP_LOGI("SettingsApp", "Start WiFi Setup clicked");

    if (app->_async_mutex == nullptr) {
        app->_async_mutex = xSemaphoreCreateMutex();
    }
    if ((app->_async_mutex == nullptr) || (xSemaphoreTake(app->_async_mutex, pdMS_TO_TICKS(50)) == pdTRUE)) {
        std::snprintf(app->_wifi_setup_text, sizeof(app->_wifi_setup_text), "%s", "Starting WiFi setup...");
        app->_wifi_setup_dirty = true;
        if (app->_async_mutex != nullptr) {
            xSemaphoreGive(app->_async_mutex);
        }
    }
    app->flushAsyncUi();
    if (app->_wifi_provision_task == nullptr) {
        BaseType_t task_ok = xTaskCreateWithCaps(
            wifiProvisionTask,
            "wifi_setup",
            WIFI_SETUP_TASK_STACK_BYTES,
            app,
            3,
            &app->_wifi_provision_task,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        );
        if (task_ok != pdPASS && app->_wifi_setup_label != nullptr) {
            app->_wifi_provision_task = nullptr;
            char text[128] = {};
            std::snprintf(
                text,
                sizeof(text),
                "Setup failed: no task memory. int %luK largest %luK",
                static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024)
            );
            lv_label_set_text(app->_wifi_setup_label, text);
        }
    }
}

void SettingsApp::onForgetWifiClicked(lv_event_t *event)
{
    auto *app = static_cast<SettingsApp *>(lv_event_get_user_data(event));
    if (app == nullptr) {
        return;
    }

    esp_err_t err = watch::wifi_forget_all();
    if (app->_wifi_setup_label != nullptr) {
        if (err == ESP_OK) {
            lv_label_set_text(app->_wifi_setup_label, "Saved WiFi cleared. Start setup to connect again.");
        } else {
            char text[96] = {};
            std::snprintf(text, sizeof(text), "Forget failed: %s", esp_err_to_name(err));
            lv_label_set_text(app->_wifi_setup_label, text);
        }
    }
    app->refreshStatus();
}

void SettingsApp::onReconnectWifiClicked(lv_event_t *event)
{
    auto *app = static_cast<SettingsApp *>(lv_event_get_user_data(event));
    if (app == nullptr) {
        return;
    }

    esp_err_t err = watch::wifi_reconnect_saved();
    if (app->_wifi_setup_label != nullptr) {
        if (err == ESP_OK) {
            lv_label_set_text(app->_wifi_setup_label, "Scanning saved WiFi and reconnecting...");
        } else {
            char text[96] = {};
            std::snprintf(text, sizeof(text), "Reconnect failed: %s", esp_err_to_name(err));
            lv_label_set_text(app->_wifi_setup_label, text);
        }
    }
    app->refreshStatus();
}

void SettingsApp::onManualWifiClicked(lv_event_t *event)
{
    auto *app = static_cast<SettingsApp *>(lv_event_get_user_data(event));
    if (app == nullptr) {
        return;
    }
    app->showWifiPasswordDialog("", true);
}

void SettingsApp::onScanWifiClicked(lv_event_t *event)
{
    auto *app = static_cast<SettingsApp *>(lv_event_get_user_data(event));
    if (app == nullptr) {
        return;
    }
    if (app->_wifi_scan_task != nullptr) {
        if (app->_wifi_scan_label != nullptr) {
            lv_label_set_text(app->_wifi_scan_label, "Scanning...");
        }
        return;
    }

    if (app->_async_mutex == nullptr) {
        app->_async_mutex = xSemaphoreCreateMutex();
    }
    if ((app->_async_mutex == nullptr) || (xSemaphoreTake(app->_async_mutex, pdMS_TO_TICKS(50)) == pdTRUE)) {
        std::snprintf(app->_wifi_scan_text, sizeof(app->_wifi_scan_text), "%s", "Scanning...");
        app->_wifi_scan_dirty = true;
        if (app->_async_mutex != nullptr) {
            xSemaphoreGive(app->_async_mutex);
        }
    }
    app->flushAsyncUi();

    BaseType_t task_ok = xTaskCreateWithCaps(
        wifiScanTask,
        "wifi_scan_ui",
        WIFI_SCAN_TASK_STACK_BYTES,
        app,
        3,
        &app->_wifi_scan_task,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    if (task_ok != pdPASS) {
        app->_wifi_scan_task = nullptr;
        std::snprintf(
            app->_wifi_scan_text,
            sizeof(app->_wifi_scan_text),
            "Scan failed: no task memory. int %luK largest %luK",
            static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
            static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024)
        );
        app->_wifi_scan_count = 0;
        if (app->_wifi_scan_label != nullptr) {
            set_label_text_if_changed(app->_wifi_scan_label, app->_wifi_scan_text);
        }
        app->updateWifiScanButtons();
    }
}

void SettingsApp::onScanNetworkClicked(lv_event_t *event)
{
    auto *app = static_cast<SettingsApp *>(lv_event_get_user_data(event));
    lv_obj_t *target = static_cast<lv_obj_t *>(lv_event_get_target(event));
    if ((app == nullptr) || (target == nullptr)) {
        return;
    }
    for (int i = 0; i < MAX_WIFI_SCAN_BUTTONS; ++i) {
        if ((app->_wifi_network_buttons[i] == target) && (app->_wifi_scan_items[i].ssid[0] != '\0')) {
            app->showWifiPasswordDialog(app->_wifi_scan_items[i].ssid, false);
            return;
        }
    }
}

void SettingsApp::onWifiDialogTextareaFocused(lv_event_t *event)
{
    auto *app = static_cast<SettingsApp *>(lv_event_get_user_data(event));
    lv_obj_t *target = static_cast<lv_obj_t *>(lv_event_get_target(event));
    if ((app == nullptr) || (target == nullptr) || (app->_wifi_keyboard == nullptr)) {
        return;
    }
#if LV_USE_KEYBOARD
    lv_keyboard_set_textarea(app->_wifi_keyboard, target);
#endif
}

void SettingsApp::onWifiDialogConnectClicked(lv_event_t *event)
{
    auto *app = static_cast<SettingsApp *>(lv_event_get_user_data(event));
    if (app == nullptr) {
        return;
    }
    const char *ssid = (app->_wifi_ssid_textarea != nullptr) ? lv_textarea_get_text(app->_wifi_ssid_textarea) : app->_wifi_selected_ssid;
    if ((ssid == nullptr) || (ssid[0] == '\0')) {
        if (app->_wifi_setup_label != nullptr) {
            lv_label_set_text(app->_wifi_setup_label, "Connect failed: SSID is empty");
        }
        return;
    }
    const char *password = (app->_wifi_password_textarea != nullptr) ? lv_textarea_get_text(app->_wifi_password_textarea) : "";
    esp_err_t err = watch::wifi_save_and_connect(ssid, password);
    if (app->_wifi_setup_label != nullptr) {
        if (err == ESP_OK) {
            char text[96] = {};
            std::snprintf(text, sizeof(text), "Connecting: %s", ssid);
            lv_label_set_text(app->_wifi_setup_label, text);
        } else {
            char text[96] = {};
            std::snprintf(text, sizeof(text), "Connect failed: %s", esp_err_to_name(err));
            lv_label_set_text(app->_wifi_setup_label, text);
        }
    }
    app->closeWifiPasswordDialog();
    app->refreshStatus(false);
}

void SettingsApp::onWifiDialogCancelClicked(lv_event_t *event)
{
    auto *app = static_cast<SettingsApp *>(lv_event_get_user_data(event));
    if (app != nullptr) {
        app->closeWifiPasswordDialog();
    }
}

void SettingsApp::onSyncNtpClicked(lv_event_t *event)
{
    auto *app = static_cast<SettingsApp *>(lv_event_get_user_data(event));
    if (app == nullptr) {
        return;
    }

    esp_err_t err = watch::time_start_sntp_if_needed();
    if (app->_time_action_label != nullptr) {
        if (err == ESP_OK) {
            lv_label_set_text(app->_time_action_label, "SNTP started. Keep WiFi connected; RTC will be updated after sync.");
        } else if (err == ESP_ERR_INVALID_STATE) {
            lv_label_set_text(app->_time_action_label, "WiFi offline. Connect WiFi before SNTP sync.");
        } else {
            char text[96] = {};
            std::snprintf(text, sizeof(text), "SNTP failed: %s", esp_err_to_name(err));
            lv_label_set_text(app->_time_action_label, text);
        }
    }
    app->refreshStatus();
}

void SettingsApp::onRtcToSystemClicked(lv_event_t *event)
{
    auto *app = static_cast<SettingsApp *>(lv_event_get_user_data(event));
    if (app == nullptr) {
        return;
    }

    esp_err_t err = watch::time_sync_from_rtc();
    if (app->_time_action_label != nullptr) {
        if (err == ESP_OK) {
            lv_label_set_text(app->_time_action_label, "System time loaded from RTC.");
        } else {
            char text[96] = {};
            std::snprintf(text, sizeof(text), "RTC sync failed: %s", esp_err_to_name(err));
            lv_label_set_text(app->_time_action_label, text);
        }
    }
    app->refreshStatus();
}

void SettingsApp::onFormatSdClicked(lv_event_t *event)
{
    auto *app = static_cast<SettingsApp *>(lv_event_get_user_data(event));
    if (app == nullptr) {
        return;
    }
    if (!app->_sd_format_armed) {
        app->_sd_format_armed = true;
        if (app->_storage_value_label != nullptr) {
            set_label_text_if_changed(app->_storage_value_label, "Danger: tap Format TF again to erase and format the SD card.");
            lv_obj_set_style_text_color(app->_storage_value_label, lv_color_hex(0xD85C5C), 0);
        }
        return;
    }

    const int64_t now_ms = esp_timer_get_time() / 1000;
    if ((app->_sd_format_last_ms > 0) && ((now_ms - app->_sd_format_last_ms) < 5000)) {
        if (app->_storage_value_label != nullptr) {
            set_label_text_if_changed(app->_storage_value_label, "Format is cooling down. Wait 5 seconds before retry.");
        }
        return;
    }
    app->_sd_format_last_ms = now_ms;
    app->_sd_format_armed = false;
    if (app->_storage_value_label != nullptr) {
        set_label_text_if_changed(app->_storage_value_label, "Formatting TF card...");
    }
    if (app->_sd_format_task != nullptr) {
        return;
    }
    BaseType_t task_ok = xTaskCreateWithCaps(
        sdFormatTask, "sd_format", SD_FORMAT_TASK_STACK_BYTES, app, 1,
        &app->_sd_format_task, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (task_ok != pdPASS) {
        app->_sd_format_task = nullptr;
        set_label_text_if_changed(app->_storage_value_label, "Format failed: no task memory");
    }
}

void SettingsApp::onScreenTimeoutChanged(lv_event_t *event)
{
    auto *app = static_cast<SettingsApp *>(lv_event_get_user_data(event));
    lv_obj_t *slider = static_cast<lv_obj_t *>(lv_event_get_target(event));
    if ((app == nullptr) || (slider == nullptr)) {
        return;
    }
    const int value = static_cast<int>(lv_slider_get_value(slider));
    app->_pending_screen_timeout_s = value;
    if (app->_screen_timeout_label != nullptr) {
        char text[32] = {};
        std::snprintf(text, sizeof(text), "Screen timeout %ds", value);
        set_label_text_if_changed(app->_screen_timeout_label, text);
    }
    const lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST) {
        return;
    }
    watch::display_set_screen_timeout_s(value);
    app->refreshStatus();
}

void SettingsApp::onRaiseWakeChanged(lv_event_t *event)
{
    auto *app = static_cast<SettingsApp *>(lv_event_get_user_data(event));
    lv_obj_t *sw = static_cast<lv_obj_t *>(lv_event_get_target(event));
    if ((app == nullptr) || (sw == nullptr)) {
        return;
    }
    watch::display_set_raise_wake_enabled(lv_obj_has_state(sw, LV_STATE_CHECKED));
    app->refreshStatus();
}

void SettingsApp::onTimer(lv_timer_t *timer)
{
    auto *app = static_cast<SettingsApp *>(lv_timer_get_user_data(timer));
    if (app != nullptr) {
        const bool busy = (app->_wifi_provision_task != nullptr) || (app->_wifi_scan_task != nullptr) ||
            (app->_sd_format_task != nullptr) || (app->_health_status_task != nullptr) ||
            (app->_sensor_status_task != nullptr) ||
            app->_wifi_setup_dirty || app->_wifi_scan_dirty;
        if (app->_timer != nullptr) {
            lv_timer_set_period(app->_timer, busy ? SETTINGS_TIMER_BUSY_MS : SETTINGS_TIMER_IDLE_MS);
        }
        app->flushAsyncUi();
        if (app->_current_section == SECTION_STORAGE) {
            const bool due = (app->_storage_refresh_tick == 0) ||
                (lv_tick_elaps(app->_storage_refresh_tick) >= STORAGE_REFRESH_INTERVAL_MS);
            if (due) {
                app->requestStorageRefresh(false);
            }
        }
        app->requestDiagnosticsRefresh(false);
        app->refreshStatus(false);
    }
}

void SettingsApp::wifiScanTask(void *arg)
{
    auto *app = static_cast<SettingsApp *>(arg);
    if (app == nullptr) {
        vTaskDeleteWithCaps(nullptr);
        return;
    }
    char text[sizeof(app->_wifi_scan_text)] = {};
    watch::WifiScanItem items[MAX_WIFI_SCAN_BUTTONS] = {};
    size_t count = 0;
    char error_text[96] = {};
    esp_err_t err = watch::wifi_scan_items(items, MAX_WIFI_SCAN_BUTTONS, &count, error_text, sizeof(error_text));
    if (err != ESP_OK) {
        std::snprintf(text, sizeof(text), "%s", error_text[0] != '\0' ? error_text : esp_err_to_name(err));
    } else if (count == 0) {
        std::snprintf(text, sizeof(text), "No nearby 2.4G WiFi found");
    } else {
        text[0] = '\0';
        for (size_t i = 0; i < count; ++i) {
            char line[80] = {};
            std::snprintf(
                line, sizeof(line), "%s%.24s  %ddBm%s",
                (text[0] == '\0') ? "" : "\n",
                items[i].ssid,
                items[i].rssi,
                items[i].saved ? "  saved" : ""
            );
            size_t used = std::strlen(text);
            if (used < sizeof(text) - 1) {
                std::snprintf(text + used, sizeof(text) - used, "%s", line);
            }
        }
    }
    if ((app->_async_mutex == nullptr) || (xSemaphoreTake(app->_async_mutex, pdMS_TO_TICKS(50)) == pdTRUE)) {
        std::snprintf(app->_wifi_scan_text, sizeof(app->_wifi_scan_text), "%s", text);
        app->_wifi_scan_count = (err == ESP_OK) ? count : 0;
        for (int i = 0; i < MAX_WIFI_SCAN_BUTTONS; ++i) {
            app->_wifi_scan_items[i] = (i < static_cast<int>(count)) ? items[i] : watch::WifiScanItem{};
        }
        app->_wifi_scan_dirty = true;
        if (app->_async_mutex != nullptr) {
            xSemaphoreGive(app->_async_mutex);
        }
    }
    app->_wifi_scan_task = nullptr;
    vTaskDeleteWithCaps(nullptr);
}

void SettingsApp::wifiProvisionTask(void *arg)
{
    auto *app = static_cast<SettingsApp *>(arg);
    if (app == nullptr) {
        vTaskDeleteWithCaps(nullptr);
        return;
    }

    ESP_LOGI("SettingsApp", "WiFi setup task started");
    char text[sizeof(app->_wifi_setup_text)] = {};
    esp_err_t err = watch::wifi_start_provisioning();
    if (err == ESP_OK) {
        watch::provisioning_info_text(text, sizeof(text));
        ESP_LOGI("SettingsApp", "WiFi provisioning started");
    } else {
        std::snprintf(text, sizeof(text), "Setup failed: %s", esp_err_to_name(err));
        ESP_LOGW("SettingsApp", "WiFi provisioning failed: %s", esp_err_to_name(err));
    }

    if ((app->_async_mutex == nullptr) || (xSemaphoreTake(app->_async_mutex, pdMS_TO_TICKS(50)) == pdTRUE)) {
        std::snprintf(app->_wifi_setup_text, sizeof(app->_wifi_setup_text), "%s", text);
        app->_wifi_setup_dirty = true;
        if (app->_async_mutex != nullptr) {
            xSemaphoreGive(app->_async_mutex);
        }
    }
    app->_wifi_provision_task = nullptr;
    ESP_LOGI("SettingsApp", "WiFi setup task finished");
    vTaskDeleteWithCaps(nullptr);
}

void SettingsApp::storageStatusTask(void *arg)
{
    auto *app = static_cast<SettingsApp *>(arg);
    if (app == nullptr) {
        vTaskDeleteWithCaps(nullptr);
        return;
    }
    const uint32_t generation = app->_page_generation;

    char littlefs[192] = {};
    char sd[320] = {};
    char text[sizeof(app->_storage_status_text)] = {};
    watch::storage_littlefs_status_text(littlefs, sizeof(littlefs));
    vTaskDelay(pdMS_TO_TICKS(1));
    watch::storage_sd_status_text(sd, sizeof(sd));
    std::snprintf(text, sizeof(text), "%s\n\nSD: %s", littlefs, sd);

    if ((generation == app->_page_generation) &&
        ((app->_async_mutex == nullptr) || (xSemaphoreTake(app->_async_mutex, pdMS_TO_TICKS(50)) == pdTRUE))) {
        std::snprintf(app->_storage_status_text, sizeof(app->_storage_status_text), "%s", text);
        app->_storage_status_dirty = true;
        app->_storage_status_error = false;
        app->_storage_retry_interval_ms = (std::strstr(sd, "not mounted") != nullptr) ? 300000 : STORAGE_REFRESH_INTERVAL_MS;
        if (app->_async_mutex != nullptr) {
            xSemaphoreGive(app->_async_mutex);
        }
    }
    app->_storage_status_task = nullptr;
    vTaskDeleteWithCaps(nullptr);
}

void SettingsApp::sdFormatTask(void *arg)
{
    auto *app = static_cast<SettingsApp *>(arg);
    if (app == nullptr) {
        vTaskDeleteWithCaps(nullptr);
        return;
    }
    const uint32_t generation = app->_page_generation;
    const esp_err_t err = watch::storage_sd_format();
    char text[128] = {};
    format_storage_result_text(err, text, sizeof(text));
    if ((generation == app->_page_generation) &&
        ((app->_async_mutex == nullptr) || (xSemaphoreTake(app->_async_mutex, pdMS_TO_TICKS(50)) == pdTRUE))) {
        std::snprintf(app->_storage_status_text, sizeof(app->_storage_status_text), "%s", text);
        app->_storage_status_dirty = true;
        app->_storage_status_error = (err != ESP_OK);
        if (app->_async_mutex != nullptr) {
            xSemaphoreGive(app->_async_mutex);
        }
    }
    app->_sd_format_task = nullptr;
    vTaskDeleteWithCaps(nullptr);
}

void SettingsApp::healthStatusTask(void *arg)
{
    auto *app = static_cast<SettingsApp *>(arg);
    if (app == nullptr) {
        vTaskDeleteWithCaps(nullptr);
        return;
    }
    const uint32_t generation = app->_page_generation;

    char text[sizeof(app->_health_status_text)] = {};
    health_diag_text(text, sizeof(text));
    if ((generation == app->_page_generation) &&
        ((app->_async_mutex == nullptr) || (xSemaphoreTake(app->_async_mutex, pdMS_TO_TICKS(50)) == pdTRUE))) {
        std::snprintf(app->_health_status_text, sizeof(app->_health_status_text), "%s", text);
        app->_health_status_dirty = true;
        if (app->_async_mutex != nullptr) {
            xSemaphoreGive(app->_async_mutex);
        }
    }
    app->_health_status_task = nullptr;
    vTaskDeleteWithCaps(nullptr);
}

void SettingsApp::sensorStatusTask(void *arg)
{
    auto *app = static_cast<SettingsApp *>(arg);
    if (app == nullptr) {
        vTaskDeleteWithCaps(nullptr);
        return;
    }
    const uint32_t generation = app->_page_generation;

    char text[sizeof(app->_sensor_status_text)] = {};
    watch::sensor_status_text(text, sizeof(text));
    if ((generation == app->_page_generation) &&
        ((app->_async_mutex == nullptr) || (xSemaphoreTake(app->_async_mutex, pdMS_TO_TICKS(50)) == pdTRUE))) {
        std::snprintf(app->_sensor_status_text, sizeof(app->_sensor_status_text), "%s", text);
        app->_sensor_status_dirty = true;
        if (app->_async_mutex != nullptr) {
            xSemaphoreGive(app->_async_mutex);
        }
    }
    app->_sensor_status_task = nullptr;
    vTaskDeleteWithCaps(nullptr);
}

void SettingsApp::requestDiagnosticsRefresh(bool force)
{
    const bool show_health = _health_value_label != nullptr;
    const bool show_sensor = _sensor_value_label != nullptr;
    if (!show_health && !show_sensor) {
        return;
    }

    const uint32_t now = lv_tick_get();
    if (!force && (_diagnostics_refresh_tick != 0) &&
        (lv_tick_elaps(_diagnostics_refresh_tick) < DIAGNOSTICS_REFRESH_INTERVAL_MS)) {
        return;
    }
    _diagnostics_refresh_tick = now;
    if (_async_mutex == nullptr) {
        _async_mutex = xSemaphoreCreateMutex();
    }

    if (show_health && (_health_status_task == nullptr)) {
        if ((_async_mutex == nullptr) || (xSemaphoreTake(_async_mutex, pdMS_TO_TICKS(50)) == pdTRUE)) {
            std::snprintf(_health_status_text, sizeof(_health_status_text), "%s", "Refreshing health...");
            _health_status_dirty = true;
            if (_async_mutex != nullptr) {
                xSemaphoreGive(_async_mutex);
            }
        }
        if (xTaskCreateWithCaps(healthStatusTask, "health_stat", HEALTH_STATUS_TASK_STACK_BYTES, this, 1,
                                &_health_status_task, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
            _health_status_task = nullptr;
            std::snprintf(_health_status_text, sizeof(_health_status_text), "%s", "Health refresh failed: no task memory.");
            _health_status_dirty = true;
        }
    }

    if (show_sensor && (_sensor_status_task == nullptr)) {
        if ((_async_mutex == nullptr) || (xSemaphoreTake(_async_mutex, pdMS_TO_TICKS(50)) == pdTRUE)) {
            std::snprintf(_sensor_status_text, sizeof(_sensor_status_text), "%s", "Refreshing sensor...");
            _sensor_status_dirty = true;
            if (_async_mutex != nullptr) {
                xSemaphoreGive(_async_mutex);
            }
        }
        if (xTaskCreateWithCaps(sensorStatusTask, "sensor_stat", SENSOR_STATUS_TASK_STACK_BYTES, this, 1,
                                &_sensor_status_task, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
            _sensor_status_task = nullptr;
            std::snprintf(_sensor_status_text, sizeof(_sensor_status_text), "%s", "Sensor refresh failed: no task memory.");
            _sensor_status_dirty = true;
        }
    }
}

void SettingsApp::requestStorageRefresh(bool force)
{
    if (_current_section != SECTION_STORAGE) {
        return;
    }
    if ((_storage_status_task != nullptr) || (_sd_format_task != nullptr)) {
        return;
    }
    const uint32_t now = lv_tick_get();
    if (!force && (_storage_refresh_tick != 0) &&
        (lv_tick_elaps(_storage_refresh_tick) < _storage_retry_interval_ms)) {
        return;
    }
    _storage_refresh_tick = now;
    if (_async_mutex == nullptr) {
        _async_mutex = xSemaphoreCreateMutex();
    }
    if ((_async_mutex == nullptr) || (xSemaphoreTake(_async_mutex, pdMS_TO_TICKS(50)) == pdTRUE)) {
        if (force || _storage_status_text[0] == '\0') {
            std::snprintf(_storage_status_text, sizeof(_storage_status_text), "%s", "Checking storage...");
            _storage_status_dirty = true;
            _storage_status_error = false;
        }
        if (_async_mutex != nullptr) {
            xSemaphoreGive(_async_mutex);
        }
    }
    BaseType_t ok = xTaskCreateWithCaps(
        storageStatusTask,
        "storage_stat",
        STORAGE_STATUS_TASK_STACK_BYTES,
        this,
        1,
        &_storage_status_task,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    if (ok != pdPASS) {
        _storage_status_task = nullptr;
        char text[sizeof(_storage_status_text)] = {};
        std::snprintf(
            text,
            sizeof(text),
            "Storage refresh failed: no task memory.\nint %luK largest %luK",
            static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
            static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024)
        );
        std::snprintf(_storage_status_text, sizeof(_storage_status_text), "%s", text);
        _storage_status_dirty = true;
        _storage_status_error = true;
    }
}

void SettingsApp::flushAsyncUi()
{
    char wifi_setup_text[sizeof(_wifi_setup_text)] = {};
    char wifi_scan_text[sizeof(_wifi_scan_text)] = {};
    char storage_status_text[sizeof(_storage_status_text)] = {};
    char health_status_text[sizeof(_health_status_text)] = {};
    char sensor_status_text[sizeof(_sensor_status_text)] = {};
    watch::WifiScanItem scan_items[MAX_WIFI_SCAN_BUTTONS] = {};
    size_t scan_count = 0;
    bool wifi_setup_dirty = false;
    bool wifi_scan_dirty = false;
    bool storage_status_dirty = false;
    bool health_status_dirty = false;
    bool sensor_status_dirty = false;
    bool storage_status_error = false;
    if ((_async_mutex != nullptr) && (xSemaphoreTake(_async_mutex, 0) == pdTRUE)) {
        wifi_setup_dirty = _wifi_setup_dirty;
        if (wifi_setup_dirty) {
            std::snprintf(wifi_setup_text, sizeof(wifi_setup_text), "%s", _wifi_setup_text);
            _wifi_setup_dirty = false;
        }
        wifi_scan_dirty = _wifi_scan_dirty;
        if (wifi_scan_dirty) {
            std::snprintf(wifi_scan_text, sizeof(wifi_scan_text), "%s", _wifi_scan_text);
            scan_count = _wifi_scan_count;
            for (int i = 0; i < MAX_WIFI_SCAN_BUTTONS; ++i) {
                scan_items[i] = _wifi_scan_items[i];
            }
            _wifi_scan_dirty = false;
        }
        storage_status_dirty = _storage_status_dirty;
        if (storage_status_dirty) {
            std::snprintf(storage_status_text, sizeof(storage_status_text), "%s", _storage_status_text);
            storage_status_error = _storage_status_error;
            _storage_status_dirty = false;
        }
        health_status_dirty = _health_status_dirty;
        if (health_status_dirty) {
            std::snprintf(health_status_text, sizeof(health_status_text), "%s", _health_status_text);
            _health_status_dirty = false;
        }
        sensor_status_dirty = _sensor_status_dirty;
        if (sensor_status_dirty) {
            std::snprintf(sensor_status_text, sizeof(sensor_status_text), "%s", _sensor_status_text);
            _sensor_status_dirty = false;
        }
        xSemaphoreGive(_async_mutex);
    }
    if (wifi_setup_dirty && (_wifi_setup_label != nullptr)) {
        if (watch::wifi_is_provisioning()) {
            showProvisioningInfo();
        } else {
            set_label_text_if_changed(_wifi_setup_label, wifi_setup_text);
        }
    }
    if (wifi_scan_dirty && (_wifi_scan_label != nullptr)) {
        set_label_text_if_changed(_wifi_scan_label, wifi_scan_text);
        _wifi_scan_count = scan_count;
        for (int i = 0; i < MAX_WIFI_SCAN_BUTTONS; ++i) {
            _wifi_scan_items[i] = scan_items[i];
        }
        updateWifiScanButtons();
    }
    if (storage_status_dirty && (_storage_value_label != nullptr)) {
        set_label_text_if_changed(_storage_value_label, storage_status_text);
        lv_obj_set_style_text_color(_storage_value_label, lv_color_hex(storage_status_error ? 0xD85C5C : 0xAEB7C6), 0);
    }
    if (health_status_dirty && (_health_value_label != nullptr)) {
        set_label_text_if_changed(_health_value_label, health_status_text);
    }
    if (sensor_status_dirty && (_sensor_value_label != nullptr)) {
        set_label_text_if_changed(_sensor_value_label, sensor_status_text);
    }
}

void SettingsApp::showProvisioningInfo()
{
    char text[96] = {};
    watch::provisioning_info_text(text, sizeof(text));
    if (_wifi_setup_label != nullptr) {
        set_label_text_if_changed(_wifi_setup_label, text);
    }
    if (_wifi_qr_card != nullptr) {
        if (watch::wifi_is_provisioning()) {
            lv_obj_clear_flag(_wifi_qr_card, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_wifi_qr_card, LV_OBJ_FLAG_HIDDEN);
        }
    }
#if LV_USE_QRCODE
    if (_wifi_qr != nullptr) {
        if (watch::wifi_is_provisioning()) {
            char qr_text[96] = {};
            watch::provisioning_qr_text(qr_text, sizeof(qr_text));
            lv_obj_clear_flag(_wifi_qr, LV_OBJ_FLAG_HIDDEN);
            lv_qrcode_set_data(_wifi_qr, qr_text);
        } else {
            lv_obj_add_flag(_wifi_qr, LV_OBJ_FLAG_HIDDEN);
        }
    }
#endif
}

void SettingsApp::updateWifiScanButtons()
{
    for (int i = 0; i < MAX_WIFI_SCAN_BUTTONS; ++i) {
        if (_wifi_network_buttons[i] == nullptr) {
            continue;
        }
        if (i >= static_cast<int>(_wifi_scan_count) || _wifi_scan_items[i].ssid[0] == '\0') {
            lv_obj_add_flag(_wifi_network_buttons[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        char text[72] = {};
        std::snprintf(
            text,
            sizeof(text),
            "%s  %ddBm%s",
            _wifi_scan_items[i].ssid,
            _wifi_scan_items[i].rssi,
            _wifi_scan_items[i].saved ? "  saved" : ""
        );
        if (_wifi_network_labels[i] != nullptr) {
            set_label_text_if_changed(_wifi_network_labels[i], text);
        }
        lv_obj_clear_flag(_wifi_network_buttons[i], LV_OBJ_FLAG_HIDDEN);
    }
}

void SettingsApp::closeWifiPasswordDialog()
{
    if (_wifi_dialog != nullptr) {
        lv_obj_delete(_wifi_dialog);
    }
    _wifi_dialog = nullptr;
    _wifi_ssid_textarea = nullptr;
    _wifi_password_textarea = nullptr;
    _wifi_keyboard = nullptr;
}

void SettingsApp::showWifiPasswordDialog(const char *ssid, bool allow_ssid_edit)
{
    if (!allow_ssid_edit && ((ssid == nullptr) || (ssid[0] == '\0'))) {
        return;
    }
    closeWifiPasswordDialog();
    std::snprintf(_wifi_selected_ssid, sizeof(_wifi_selected_ssid), "%s", ssid != nullptr ? ssid : "");

    _wifi_dialog = lv_obj_create(lv_layer_top());
    if (_wifi_dialog == nullptr) {
        return;
    }
    lv_obj_remove_style_all(_wifi_dialog);
    lv_obj_set_size(_wifi_dialog, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(_wifi_dialog, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(_wifi_dialog, LV_OPA_70, 0);
    lv_obj_set_flex_flow(_wifi_dialog, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_wifi_dialog, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *card = lv_obj_create(_wifi_dialog);
    if (card == nullptr) {
        closeWifiPasswordDialog();
        return;
    }
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 380, 462);
    lv_obj_set_style_radius(card, 24, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x15171D), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(card, 14, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 8, 0);

    make_label(card, allow_ssid_edit ? "Keyboard WiFi" : "Connect WiFi", &lv_font_montserrat_22, 0xFFFFFF);
    if (allow_ssid_edit) {
        _wifi_ssid_textarea = lv_textarea_create(card);
        lv_obj_set_width(_wifi_ssid_textarea, LV_PCT(100));
        lv_obj_set_height(_wifi_ssid_textarea, 46);
        lv_textarea_set_one_line(_wifi_ssid_textarea, true);
        lv_textarea_set_max_length(_wifi_ssid_textarea, 32);
        lv_textarea_set_placeholder_text(_wifi_ssid_textarea, "SSID / WiFi name");
        if (_wifi_selected_ssid[0] != '\0') {
            lv_textarea_set_text(_wifi_ssid_textarea, _wifi_selected_ssid);
        }
        lv_obj_set_style_text_font(_wifi_ssid_textarea, &lv_font_montserrat_16, 0);
        lv_obj_add_event_cb(_wifi_ssid_textarea, onWifiDialogTextareaFocused, LV_EVENT_FOCUSED, this);
        lv_obj_add_event_cb(_wifi_ssid_textarea, onWifiDialogTextareaFocused, LV_EVENT_CLICKED, this);
    } else {
        char ssid_text[80] = {};
        std::snprintf(ssid_text, sizeof(ssid_text), "SSID: %s", _wifi_selected_ssid);
        lv_obj_t *ssid_label = make_label(card, ssid_text, &lv_font_montserrat_14, 0xAEB7C6);
        lv_obj_set_width(ssid_label, LV_PCT(100));
        lv_label_set_long_mode(ssid_label, LV_LABEL_LONG_DOT);
    }

    _wifi_password_textarea = lv_textarea_create(card);
    lv_obj_set_width(_wifi_password_textarea, LV_PCT(100));
    lv_obj_set_height(_wifi_password_textarea, 46);
    lv_textarea_set_one_line(_wifi_password_textarea, true);
    lv_textarea_set_password_mode(_wifi_password_textarea, true);
    lv_textarea_set_max_length(_wifi_password_textarea, 64);
    lv_textarea_set_placeholder_text(_wifi_password_textarea, "Password, leave empty for open WiFi");
    lv_obj_set_style_text_font(_wifi_password_textarea, &lv_font_montserrat_16, 0);
    lv_obj_add_event_cb(_wifi_password_textarea, onWifiDialogTextareaFocused, LV_EVENT_FOCUSED, this);
    lv_obj_add_event_cb(_wifi_password_textarea, onWifiDialogTextareaFocused, LV_EVENT_CLICKED, this);

    lv_obj_t *row = lv_obj_create(card);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 44);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 10, 0);

    lv_obj_t *connect = lv_button_create(row);
    lv_obj_set_width(connect, LV_PCT(48));
    lv_obj_set_height(connect, 42);
    style_button(connect, 0x1B6BFF);
    lv_obj_add_event_cb(connect, onWifiDialogConnectClicked, LV_EVENT_CLICKED, this);
    lv_obj_center(make_label(connect, "Connect", &lv_font_montserrat_16, 0xFFFFFF));

    lv_obj_t *cancel = lv_button_create(row);
    lv_obj_set_width(cancel, LV_PCT(48));
    lv_obj_set_height(cancel, 42);
    style_button(cancel, 0x232833);
    lv_obj_add_event_cb(cancel, onWifiDialogCancelClicked, LV_EVENT_CLICKED, this);
    lv_obj_center(make_label(cancel, "Cancel", &lv_font_montserrat_16, 0xFFFFFF));

#if LV_USE_KEYBOARD
    _wifi_keyboard = lv_keyboard_create(card);
    lv_obj_set_width(_wifi_keyboard, LV_PCT(100));
    lv_obj_set_height(_wifi_keyboard, 250);
    lv_keyboard_set_textarea(_wifi_keyboard, allow_ssid_edit && _wifi_ssid_textarea != nullptr ? _wifi_ssid_textarea : _wifi_password_textarea);
#endif
}

void SettingsApp::refreshStatus(bool include_storage)
{
    const bool diagnostics_due = (_diagnostics_refresh_tick == 0) ||
        (lv_tick_elaps(_diagnostics_refresh_tick) >= DIAGNOSTICS_REFRESH_INTERVAL_MS);
    if (_wifi_value_label != nullptr) {
        char text[96] = {};
        watch::wifi_status_text(text, sizeof(text));
        set_label_text_if_changed(_wifi_value_label, text);
    }

    if (_wifi_setup_label != nullptr) {
        showProvisioningInfo();
    }

    if (_brightness_value_label != nullptr) {
        char text[16] = {};
        std::snprintf(text, sizeof(text), "%d%%", bsp_display_brightness_get());
        char full[32] = {};
        std::snprintf(full, sizeof(full), "Brightness %s", text);
        set_label_text_if_changed(_brightness_value_label, full);
    }

    if (_screen_timeout_label != nullptr) {
        char text[32] = {};
        auto settings = watch::display_get_settings();
        std::snprintf(text, sizeof(text), "Screen timeout %ds", settings.screen_timeout_s);
        set_label_text_if_changed(_screen_timeout_label, text);
    }

    if (_display_status_label != nullptr) {
        char text[96] = {};
        watch::display_status_text(text, sizeof(text));
        set_label_text_if_changed(_display_status_label, text);
    }

    if (_raise_wake_switch != nullptr) {
        auto settings = watch::display_get_settings();
        if (settings.raise_wake_enabled) {
            lv_obj_add_state(_raise_wake_switch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(_raise_wake_switch, LV_STATE_CHECKED);
        }
    }

    if (_wifi_saved_label != nullptr) {
        char text[192] = {};
        watch::wifi_saved_list_text(text, sizeof(text));
        set_label_text_if_changed(_wifi_saved_label, text);
    }

    if (_time_value_label != nullptr) {
        char text[160] = {};
        watch::time_status_text(text, sizeof(text));
        set_label_text_if_changed(_time_value_label, text);
    }

    if ((_battery_value_label != nullptr) || (_battery_percent_label != nullptr) || (_power_chart != nullptr)) {
        watch::BatteryState battery = {};
        watch::PowerUsageSnapshot usage = {};
        const bool battery_ok = watch::power_get_battery_state(&battery) == ESP_OK;
        const bool usage_ok = watch::power_get_usage_snapshot(&usage) == ESP_OK;
        const int percentage = (battery_ok && battery.has_percentage) ? battery.percentage : -1;
        const bool external_power = battery_ok && battery.power_source == watch::BatteryPowerSource::External;
        const bool charging = battery_ok && watch::power_is_charging();

        if (_battery_percent_label != nullptr) {
            char text[16] = {};
            std::snprintf(text, sizeof(text), percentage >= 0 ? "%d%%" : "--%%", percentage >= 0 ? percentage : 0);
            set_label_text_if_changed(_battery_percent_label, text);
        }

        if (_battery_value_label != nullptr) {
            char text[96] = {};
            if (!battery_ok) {
                std::snprintf(text, sizeof(text), "Battery sensor is starting...");
            } else if (battery.has_voltage_mv) {
                std::snprintf(
                    text,
                    sizeof(text),
                    "%s | %s | %lu.%02luV",
                    charging ? "Charging" : (external_power ? "External power" : "On battery"),
                    external_power ? "USB" : "Battery",
                    static_cast<unsigned long>(battery.voltage_mv / 1000),
                    static_cast<unsigned long>((battery.voltage_mv % 1000) / 10)
                );
            } else {
                std::snprintf(text, sizeof(text), "%s", charging ? "Charging" : "Battery status available");
            }
            set_label_text_if_changed(_battery_value_label, text);
        }

        if (_battery_fill != nullptr) {
            const int display_percentage = percentage >= 0 ? percentage : 0;
            const int fill_width = 4 + (display_percentage * 68) / 100;
            const uint32_t fill_color = external_power || display_percentage > 35 ? 0x31C16B :
                                        (display_percentage > 15 ? 0xF4B740 : 0xE24D4D);
            lv_obj_set_width(_battery_fill, fill_width);
            lv_obj_set_style_bg_color(_battery_fill, lv_color_hex(fill_color), 0);
        }

        if (_power_cycle_label != nullptr) {
            char text[120] = {};
            char elapsed[24] = {};
            if (!usage_ok || percentage < 0) {
                std::snprintf(text, sizeof(text), "Collecting current charge-cycle data...");
            } else {
                format_elapsed_time(usage.session_elapsed_seconds, elapsed, sizeof(elapsed));
                const int change = percentage - usage.session_start_percentage;
                std::snprintf(
                    text,
                    sizeof(text),
                    "%s: %s | %u%% to %d%% (%+d%%)",
                    usage.external_power ? "This charge" : "Since last unplug",
                    elapsed,
                    usage.session_start_percentage,
                    percentage,
                    change
                );
            }
            set_label_text_if_changed(_power_cycle_label, text);
        }

        if ((_power_chart != nullptr) && (_power_discharge_series != nullptr) && (_power_charge_series != nullptr)) {
            for (size_t index = 0; index < watch::POWER_USAGE_HISTORY_POINTS; ++index) {
                lv_chart_set_series_value_by_id(_power_chart, _power_discharge_series, index, LV_CHART_POINT_NONE);
                lv_chart_set_series_value_by_id(_power_chart, _power_charge_series, index, LV_CHART_POINT_NONE);
            }
            const size_t history_count = usage_ok && usage.history_count <= watch::POWER_USAGE_HISTORY_POINTS ?
                                         usage.history_count : 0;
            const size_t offset = watch::POWER_USAGE_HISTORY_POINTS - history_count;
            for (size_t index = 0; index < history_count; ++index) {
                const watch::PowerUsageHistoryPoint &point = usage.history[index];
                if (!point.valid) {
                    continue;
                }
                lv_chart_set_series_value_by_id(
                    _power_chart,
                    point.external_power ? _power_charge_series : _power_discharge_series,
                    offset + index,
                    point.percentage
                );
            }
            lv_chart_refresh(_power_chart);
            if (_power_chart_detail_label != nullptr) {
                char text[112] = {};
                std::snprintf(
                    text,
                    sizeof(text),
                    "%u/24 hourly samples | Blue: drain  Green: charge",
                    static_cast<unsigned>(history_count)
                );
                set_label_text_if_changed(_power_chart_detail_label, text);
            }
        }

        if (_power_rank_title_label != nullptr) {
            set_label_text_if_changed(
                _power_rank_title_label,
                external_power ? "Estimated foreground load during this charge" :
                                 "Estimated foreground load since last unplug"
            );
        }
        for (int index = 0; index < POWER_RANK_LABELS; ++index) {
            if (_power_rank_labels[index] == nullptr) {
                continue;
            }
            char text[96] = {};
            if (usage_ok && static_cast<size_t>(index) < usage.app_count) {
                char elapsed[24] = {};
                format_elapsed_time(usage.apps[index].active_seconds, elapsed, sizeof(elapsed));
                std::snprintf(
                    text,
                    sizeof(text),
                    "%d. %.20s  %s active",
                    index + 1,
                    usage.apps[index].name,
                    elapsed
                );
            } else if (index == 0) {
                std::snprintf(text, sizeof(text), "No foreground app time recorded yet");
            }
            set_label_text_if_changed(_power_rank_labels[index], text);
        }
    }

    if (_power_diag_label != nullptr) {
        char text[560] = {};
        power_diag_text(text, sizeof(text));
        set_label_text_if_changed(_power_diag_label, text);
    }

    if (_health_value_label != nullptr) {
        set_label_text_if_changed(_health_value_label, _health_status_text);
    }

    if (_sensor_value_label != nullptr) {
        set_label_text_if_changed(_sensor_value_label, _sensor_status_text);
    }

    if (include_storage && (_storage_value_label != nullptr)) {
        set_label_text_if_changed(_storage_value_label, _storage_status_text);
        requestStorageRefresh(false);
    }

    if (_audio_value_label != nullptr) {
        char text[160] = {};
        watch::audio_status_text(text, sizeof(text));
        set_label_text_if_changed(_audio_value_label, text);
    }

    if (_about_value_label != nullptr) {
        char text[384] = {};
        system_info_text(text, sizeof(text));
        set_label_text_if_changed(_about_value_label, text);
    }

    if (diagnostics_due && ((_battery_value_label != nullptr) || (_power_diag_label != nullptr))) {
        _diagnostics_refresh_tick = lv_tick_get();
    }
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, SettingsApp, APP_NAME, []()
{
    return std::shared_ptr<SettingsApp>(SettingsApp::requestInstance(), [](SettingsApp *) {});
})

} // namespace esp_brookesia::apps
