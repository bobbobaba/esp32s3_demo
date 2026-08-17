#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

namespace watch {

struct WifiScanItem {
    char ssid[33];
    int rssi;
    uint8_t authmode;
    bool saved;
};

esp_err_t connectivity_init();
esp_err_t wifi_start_provisioning();
esp_err_t wifi_stop_provisioning();
esp_err_t wifi_forget_all();
esp_err_t wifi_reconnect_saved();
esp_err_t wifi_save_and_connect(const char *ssid, const char *password);
esp_err_t wifi_pause_for_heavy_storage();
esp_err_t wifi_resume_after_heavy_storage();
esp_err_t wifi_suspend_for_power_save();
esp_err_t wifi_resume_from_power_save();

bool wifi_is_connected();
bool wifi_has_credentials();
bool wifi_is_provisioning();
bool wifi_is_power_save_suspended();
bool time_is_synced();
int wifi_rssi_dbm();
int wifi_provisioning_remaining_s();

void wifi_status_text(char *buffer, size_t buffer_size);
void wifi_short_status_text(char *buffer, size_t buffer_size);
void wifi_diag_text(char *buffer, size_t buffer_size);
void wifi_saved_list_text(char *buffer, size_t buffer_size);
void wifi_scan_results_text(char *buffer, size_t buffer_size);
esp_err_t wifi_scan_items(WifiScanItem *items, size_t max_items, size_t *out_count, char *error_text, size_t error_text_size);
void provisioning_info_text(char *buffer, size_t buffer_size);
void provisioning_qr_text(char *buffer, size_t buffer_size);

} // namespace watch
