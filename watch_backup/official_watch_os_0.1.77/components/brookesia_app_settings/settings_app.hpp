#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "systems/phone/esp_brookesia_phone_app.hpp"
#include "watch_connectivity.hpp"

namespace esp_brookesia::apps {

class SettingsApp: public systems::phone::App {
public:
    static SettingsApp *requestInstance();
    ~SettingsApp() override = default;

protected:
    SettingsApp();

    bool run(void) override;
    bool back(void) override;
    bool close(void) override;

private:
    static void onBackClicked(lv_event_t *event);
    static void onBrightnessChanged(lv_event_t *event);
    static void onRefreshClicked(lv_event_t *event);
    static void onStartWifiSetupClicked(lv_event_t *event);
    static void onForgetWifiClicked(lv_event_t *event);
    static void onReconnectWifiClicked(lv_event_t *event);
    static void onManualWifiClicked(lv_event_t *event);
    static void onScanWifiClicked(lv_event_t *event);
    static void onSyncNtpClicked(lv_event_t *event);
    static void onRtcToSystemClicked(lv_event_t *event);
    static void onFormatSdClicked(lv_event_t *event);
    static void onScreenTimeoutChanged(lv_event_t *event);
    static void onRaiseWakeChanged(lv_event_t *event);
    static void onTimer(lv_timer_t *timer);
    static void onScanNetworkClicked(lv_event_t *event);
    static void onWifiDialogTextareaFocused(lv_event_t *event);
    static void onWifiDialogConnectClicked(lv_event_t *event);
    static void onWifiDialogCancelClicked(lv_event_t *event);
    static void onSettingsSectionClicked(lv_event_t *event);
    static void wifiScanTask(void *arg);
    static void wifiProvisionTask(void *arg);

    void refreshStatus(bool include_storage = true);
    void showProvisioningInfo();
    void flushAsyncUi();
    void updateWifiScanButtons();
    void showWifiPasswordDialog(const char *ssid, bool allow_ssid_edit = false);
    void closeWifiPasswordDialog();
    void registerSectionCard(int section, lv_obj_t *card);
    void clearSectionCards();
    bool createSectionCards(int section);
    void showSettingsMenu();
    void showSettingsSection(int section);

    static SettingsApp *_instance;
    static constexpr int MAX_WIFI_SCAN_BUTTONS = 6;
    static constexpr int MAX_SECTION_CARDS = 12;

    lv_obj_t *_settings_root = nullptr;
    lv_obj_t *_settings_menu_card = nullptr;
    lv_obj_t *_section_cards[MAX_SECTION_CARDS] = {};
    int _section_ids[MAX_SECTION_CARDS] = {};
    int _section_card_count = 0;
    int _current_section = -1;
    lv_obj_t *_wifi_value_label = nullptr;
    lv_obj_t *_wifi_setup_label = nullptr;
    lv_obj_t *_wifi_qr_card = nullptr;
    lv_obj_t *_wifi_qr = nullptr;
    lv_obj_t *_wifi_saved_label = nullptr;
    lv_obj_t *_wifi_scan_label = nullptr;
    lv_obj_t *_wifi_network_buttons[MAX_WIFI_SCAN_BUTTONS] = {};
    lv_obj_t *_wifi_network_labels[MAX_WIFI_SCAN_BUTTONS] = {};
    lv_obj_t *_wifi_dialog = nullptr;
    lv_obj_t *_wifi_ssid_textarea = nullptr;
    lv_obj_t *_wifi_password_textarea = nullptr;
    lv_obj_t *_wifi_keyboard = nullptr;
    lv_obj_t *_time_value_label = nullptr;
    lv_obj_t *_time_action_label = nullptr;
    lv_obj_t *_battery_value_label = nullptr;
    lv_obj_t *_power_diag_label = nullptr;
    lv_obj_t *_health_value_label = nullptr;
    lv_obj_t *_sensor_value_label = nullptr;
    lv_obj_t *_storage_value_label = nullptr;
    lv_obj_t *_audio_value_label = nullptr;
    lv_obj_t *_brightness_value_label = nullptr;
    lv_obj_t *_screen_timeout_label = nullptr;
    lv_obj_t *_raise_wake_switch = nullptr;
    lv_obj_t *_display_status_label = nullptr;
    lv_obj_t *_about_value_label = nullptr;
    lv_timer_t *_timer = nullptr;
    TaskHandle_t _wifi_provision_task = nullptr;
    TaskHandle_t _wifi_scan_task = nullptr;
    SemaphoreHandle_t _async_mutex = nullptr;
    char _wifi_setup_text[160] = "";
    char _wifi_scan_text[512] = "Tap Scan to list nearby 2.4G WiFi.";
    watch::WifiScanItem _wifi_scan_items[MAX_WIFI_SCAN_BUTTONS] = {};
    size_t _wifi_scan_count = 0;
    char _wifi_selected_ssid[33] = "";
    bool _wifi_setup_dirty = false;
    bool _wifi_scan_dirty = true;
    bool _sd_format_armed = false;
    int64_t _sd_format_last_ms = 0;
};

} // namespace esp_brookesia::apps
