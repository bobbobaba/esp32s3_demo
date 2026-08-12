#include "watch_connectivity.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace watch {

namespace {

constexpr const char *TAG = "watch_connectivity";
constexpr const char *NVS_NAMESPACE = "watch_wifi";
constexpr const char *NVS_KEY_SSID = "ssid";
constexpr const char *NVS_KEY_PASS = "pass";
constexpr const char *NVS_KEY_COUNT = "count";
constexpr const char *SETUP_IP = "192.168.4.1";
constexpr const char *SETUP_URL = "http://192.168.4.1/";
constexpr int MAX_WIFI_PROFILES = 8;
constexpr uint32_t WIFI_INTERNAL_HEAP_WARN_BYTES = 24 * 1024;
constexpr uint32_t WIFI_AUTO_TASK_STACK_SIZE = 3072;
constexpr uint32_t WIFI_DNS_TASK_STACK_SIZE = 3072;
constexpr uint32_t WIFI_GUARD_TASK_STACK_SIZE = 2048;
constexpr int64_t PROVISIONING_TIMEOUT_MS = 5LL * 60LL * 1000LL;
constexpr int64_t PROVISIONING_CONNECTED_GRACE_MS = 10LL * 1000LL;

struct WifiCredential {
    std::string ssid;
    std::string password;
};

enum class WifiState {
    Idle,
    Scanning,
    Connecting,
    Connected,
    Failed,
};

bool s_initialized = false;
bool s_wifi_driver_initialized = false;
bool s_wifi_started = false;
bool s_has_credentials = false;
bool s_connected = false;
bool s_provisioning = false;
bool s_autoconnect_running = false;
bool s_manual_disconnect = false;
bool s_ignore_next_assoc_leave = false;
bool s_provision_guard_running = false;
WifiState s_wifi_state = WifiState::Idle;
int s_last_rssi = 0;
int64_t s_connect_started_ms = 0;
int64_t s_provisioning_started_ms = 0;
int64_t s_provisioning_connected_ms = 0;
char s_ssid[33] = {};
char s_ap_ssid[33] = {};
char s_last_error[96] = {};
char s_last_failed_ssid[33] = {};
uint8_t s_last_disconnect_reason = 0;
std::vector<WifiCredential> s_credentials;
httpd_handle_t s_httpd = nullptr;
TaskHandle_t s_dns_task = nullptr;
SemaphoreHandle_t s_wifi_op_mutex = nullptr;
StaticSemaphore_t s_wifi_op_mutex_buffer;
SemaphoreHandle_t s_scan_done_sem = nullptr;
StaticSemaphore_t s_scan_done_sem_buffer;

esp_err_t request_autoconnect(bool skip_last_failed);
esp_err_t ensure_wifi_driver_initialized();
esp_err_t stop_provisioning_internal(const char *reason);

void apply_wifi_power_save(const char *phase)
{
    if (!s_wifi_driver_initialized) {
        return;
    }
    if (s_provisioning) {
        esp_err_t err = esp_wifi_set_ps(WIFI_PS_NONE);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "%s: disable WiFi PS for setup failed: %s", phase != nullptr ? phase : "WiFi PS", esp_err_to_name(err));
        }
        return;
    }
    esp_err_t err = esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "%s: WiFi power save MIN_MODEM enabled", phase != nullptr ? phase : "WiFi PS");
    } else {
        ESP_LOGW(TAG, "%s: enable WiFi PS failed: %s", phase != nullptr ? phase : "WiFi PS", esp_err_to_name(err));
    }
}

void log_wifi_heap(const char *phase)
{
    ESP_LOGI(TAG, "%s: free=%u internal=%u largest_internal=%u",
             phase != nullptr ? phase : "WiFi heap",
             static_cast<unsigned>(esp_get_free_heap_size()),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
}

SemaphoreHandle_t wifi_op_mutex()
{
    if (s_wifi_op_mutex == nullptr) {
        static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
        portENTER_CRITICAL(&mux);
        if (s_wifi_op_mutex == nullptr) {
            s_wifi_op_mutex = xSemaphoreCreateMutexStatic(&s_wifi_op_mutex_buffer);
        }
        portEXIT_CRITICAL(&mux);
    }
    return s_wifi_op_mutex;
}

bool wifi_op_lock(TickType_t timeout)
{
    SemaphoreHandle_t mutex = wifi_op_mutex();
    return (mutex != nullptr) && (xSemaphoreTake(mutex, timeout) == pdTRUE);
}

void wifi_op_unlock()
{
    if (s_wifi_op_mutex != nullptr) {
        xSemaphoreGive(s_wifi_op_mutex);
    }
}

SemaphoreHandle_t scan_done_sem()
{
    if (s_scan_done_sem == nullptr) {
        static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
        portENTER_CRITICAL(&mux);
        if (s_scan_done_sem == nullptr) {
            s_scan_done_sem = xSemaphoreCreateBinaryStatic(&s_scan_done_sem_buffer);
        }
        portEXIT_CRITICAL(&mux);
    }
    return s_scan_done_sem;
}

void clear_scan_done_signal()
{
    SemaphoreHandle_t sem = scan_done_sem();
    if (sem == nullptr) {
        return;
    }
    while (xSemaphoreTake(sem, 0) == pdTRUE) {
    }
}

esp_err_t start_scan_and_wait(const wifi_scan_config_t *scan_config, TickType_t timeout)
{
    clear_scan_done_signal();
    esp_err_t err = esp_wifi_scan_start(scan_config, false);
    if (err != ESP_OK) {
        return err;
    }
    SemaphoreHandle_t sem = scan_done_sem();
    if (sem == nullptr) {
        esp_wifi_scan_stop();
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(sem, timeout) != pdTRUE) {
        esp_wifi_scan_stop();
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

bool ok_or_already(esp_err_t err)
{
    return (err == ESP_OK) || (err == ESP_ERR_INVALID_STATE) || (err == ESP_ERR_WIFI_STATE);
}

void append_text(char *buffer, size_t buffer_size, const char *text)
{
    if ((buffer == nullptr) || (buffer_size == 0) || (text == nullptr)) {
        return;
    }
    size_t used = std::strlen(buffer);
    if (used >= buffer_size - 1) {
        return;
    }
    std::snprintf(buffer + used, buffer_size - used, "%s", text);
}

std::string html_escape(const char *text)
{
    std::string out;
    if (text == nullptr) {
        return out;
    }
    for (const char *p = text; *p != '\0'; ++p) {
        switch (*p) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        default:
            out += *p;
            break;
        }
    }
    return out;
}

int from_hex(char c)
{
    if ((c >= '0') && (c <= '9')) {
        return c - '0';
    }
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if ((c >= 'a') && (c <= 'f')) {
        return c - 'a' + 10;
    }
    return -1;
}

std::string url_decode(const char *data, size_t len)
{
    std::string out;
    out.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        if (data[i] == '+') {
            out.push_back(' ');
        } else if ((data[i] == '%') && (i + 2 < len)) {
            int hi = from_hex(data[i + 1]);
            int lo = from_hex(data[i + 2]);
            if ((hi >= 0) && (lo >= 0)) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
            } else {
                out.push_back(data[i]);
            }
        } else {
            out.push_back(data[i]);
        }
    }
    return out;
}

std::string form_value(const std::string &body, const char *key)
{
    const std::string prefix = std::string(key) + "=";
    size_t pos = 0;
    while (pos < body.size()) {
        size_t end = body.find('&', pos);
        if (end == std::string::npos) {
            end = body.size();
        }
        if (body.compare(pos, prefix.size(), prefix) == 0) {
            return url_decode(body.data() + pos + prefix.size(), end - pos - prefix.size());
        }
        pos = end + 1;
    }
    return {};
}

std::string nvs_slot_key(const char *prefix, int index)
{
    char key[12] = {};
    std::snprintf(key, sizeof(key), "%s%d", prefix, index);
    return key;
}

bool has_credential(const std::string &ssid)
{
    return std::any_of(s_credentials.begin(), s_credentials.end(), [&](const WifiCredential &cred) {
        return cred.ssid == ssid;
    });
}

void update_has_credentials()
{
    s_has_credentials = !s_credentials.empty();
    if (!s_has_credentials && !s_connected) {
        s_ssid[0] = '\0';
    }
}

const char *disconnect_reason_text(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_AUTH_FAIL:
        return "auth failed";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        return "password handshake timeout";
    case WIFI_REASON_NO_AP_FOUND:
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
        return "AP not found";
    case WIFI_REASON_BEACON_TIMEOUT:
        return "signal lost";
    default:
        return "disconnected";
    }
}

esp_err_t persist_credentials()
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    if (!s_credentials.empty()) {
        err = nvs_set_str(handle, NVS_KEY_SSID, s_credentials.front().ssid.c_str());
        if (err == ESP_OK) {
            err = nvs_set_str(handle, NVS_KEY_PASS, s_credentials.front().password.c_str());
        }
    } else {
        nvs_erase_key(handle, NVS_KEY_SSID);
        nvs_erase_key(handle, NVS_KEY_PASS);
    }

    if (err == ESP_OK) {
        err = nvs_set_u8(handle, NVS_KEY_COUNT, static_cast<uint8_t>(s_credentials.size()));
    }

    for (int i = 0; (err == ESP_OK) && (i < MAX_WIFI_PROFILES); ++i) {
        const std::string ssid_key = nvs_slot_key("ssid", i);
        const std::string pass_key = nvs_slot_key("pass", i);
        if (i < static_cast<int>(s_credentials.size())) {
            err = nvs_set_str(handle, ssid_key.c_str(), s_credentials[i].ssid.c_str());
            if (err == ESP_OK) {
                err = nvs_set_str(handle, pass_key.c_str(), s_credentials[i].password.c_str());
            }
        } else {
            nvs_erase_key(handle, ssid_key.c_str());
            nvs_erase_key(handle, pass_key.c_str());
        }
    }

    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t load_credentials()
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        s_credentials.clear();
        s_has_credentials = false;
        return err;
    }

    s_credentials.clear();

    // Backward-compatible migration from the previous single-WiFi storage.
    char old_ssid[33] = {};
    char old_pass[65] = {};
    size_t old_ssid_len = sizeof(old_ssid);
    if (nvs_get_str(handle, NVS_KEY_SSID, old_ssid, &old_ssid_len) == ESP_OK && old_ssid[0] != '\0') {
        size_t old_pass_len = sizeof(old_pass);
        nvs_get_str(handle, NVS_KEY_PASS, old_pass, &old_pass_len);
        s_credentials.push_back({old_ssid, old_pass});
    }

    uint8_t count = 0;
    nvs_get_u8(handle, NVS_KEY_COUNT, &count);
    count = std::min<uint8_t>(count, MAX_WIFI_PROFILES);
    for (int i = 0; i < count; ++i) {
        char ssid[33] = {};
        char pass[65] = {};
        size_t ssid_len = sizeof(ssid);
        if (nvs_get_str(handle, nvs_slot_key("ssid", i).c_str(), ssid, &ssid_len) != ESP_OK || ssid[0] == '\0') {
            continue;
        }
        size_t pass_len = sizeof(pass);
        nvs_get_str(handle, nvs_slot_key("pass", i).c_str(), pass, &pass_len);
        if (!has_credential(ssid)) {
            s_credentials.push_back({ssid, pass});
        }
    }
    nvs_close(handle);

    update_has_credentials();
    if (s_has_credentials && s_ssid[0] == '\0') {
        std::snprintf(s_ssid, sizeof(s_ssid), "%s", s_credentials.front().ssid.c_str());
    }
    return s_has_credentials ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t save_credentials(const std::string &ssid, const std::string &password)
{
    load_credentials();

    s_credentials.erase(
        std::remove_if(s_credentials.begin(), s_credentials.end(), [&](const WifiCredential &cred) {
            return cred.ssid == ssid;
        }),
        s_credentials.end()
    );
    s_credentials.insert(s_credentials.begin(), {ssid, password});
    if (s_credentials.size() > MAX_WIFI_PROFILES) {
        s_credentials.resize(MAX_WIFI_PROFILES);
    }

    esp_err_t err = persist_credentials();
    if (err == ESP_OK) {
        std::snprintf(s_ssid, sizeof(s_ssid), "%s", ssid.c_str());
        update_has_credentials();
        s_last_error[0] = '\0';
        s_last_failed_ssid[0] = '\0';
        s_last_disconnect_reason = 0;
    }
    return err;
}

esp_err_t forget_credentials(const std::string &ssid)
{
    load_credentials();

    const size_t old_size = s_credentials.size();
    s_credentials.erase(
        std::remove_if(s_credentials.begin(), s_credentials.end(), [&](const WifiCredential &cred) {
            return cred.ssid == ssid;
        }),
        s_credentials.end()
    );
    if (s_credentials.size() == old_size) {
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t err = persist_credentials();
    if (err == ESP_OK) {
        update_has_credentials();
        if (std::strcmp(s_ssid, ssid.c_str()) == 0) {
            s_connected = false;
            s_last_rssi = 0;
            s_ssid[0] = '\0';
            s_last_failed_ssid[0] = '\0';
            s_last_disconnect_reason = 0;
            std::snprintf(s_last_error, sizeof(s_last_error), "Forgot WiFi: %s", ssid.c_str());
            s_manual_disconnect = !s_has_credentials;
            if (s_wifi_driver_initialized) {
                esp_wifi_disconnect();
            }
            if (s_has_credentials) {
                request_autoconnect(false);
            } else {
                s_wifi_state = WifiState::Idle;
            }
        }
    }
    return err;
}

esp_err_t forget_all_credentials()
{
    load_credentials();
    s_credentials.clear();

    esp_err_t err = persist_credentials();
    if (err != ESP_OK) {
        return err;
    }

    update_has_credentials();
    s_connected = false;
    s_last_rssi = 0;
    s_ssid[0] = '\0';
    s_last_failed_ssid[0] = '\0';
    s_last_disconnect_reason = 0;
    std::snprintf(s_last_error, sizeof(s_last_error), "All WiFi forgotten");
    s_wifi_state = WifiState::Idle;
    s_manual_disconnect = true;
    if (s_wifi_driver_initialized) {
        esp_wifi_disconnect();
    }
    return ESP_OK;
}

esp_err_t ensure_wifi_started()
{
    ESP_RETURN_ON_ERROR(ensure_wifi_driver_initialized(), TAG, "WiFi driver init failed");
    if (s_wifi_started) {
        return ESP_OK;
    }
    esp_err_t err = esp_wifi_start();
    if ((err == ESP_OK) || (err == ESP_ERR_WIFI_STATE)) {
        s_wifi_started = true;
        apply_wifi_power_save("WiFi started");
        return ESP_OK;
    }
    return err;
}

esp_err_t connect_to_credential(const WifiCredential &credential)
{
    ESP_RETURN_ON_ERROR(ensure_wifi_driver_initialized(), TAG, "WiFi driver init failed");
    wifi_config_t sta_config = {};
    std::memcpy(sta_config.sta.ssid, credential.ssid.c_str(), std::min(sizeof(sta_config.sta.ssid), credential.ssid.size()));
    std::memcpy(sta_config.sta.password, credential.password.c_str(), std::min(sizeof(sta_config.sta.password), credential.password.size()));
    sta_config.sta.threshold.authmode = WIFI_AUTH_OPEN;

    wifi_mode_t mode = WIFI_MODE_STA;
    if (s_provisioning) {
        mode = WIFI_MODE_APSTA;
    }
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(mode), TAG, "set WiFi mode failed");
    apply_wifi_power_save("WiFi connect mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &sta_config), TAG, "set STA config failed");
    ESP_RETURN_ON_ERROR(ensure_wifi_started(), TAG, "start WiFi failed");

    s_manual_disconnect = false;
    s_ignore_next_assoc_leave = true;
    esp_wifi_disconnect();
    std::snprintf(s_ssid, sizeof(s_ssid), "%s", credential.ssid.c_str());
    s_wifi_state = WifiState::Connecting;
    s_connected = false;
    s_last_error[0] = '\0';
    s_last_failed_ssid[0] = '\0';
    s_last_disconnect_reason = 0;
    s_connect_started_ms = esp_timer_get_time() / 1000;

    esp_err_t err = esp_wifi_connect();
    if ((err == ESP_OK) || (err == ESP_ERR_WIFI_STATE)) {
        ESP_LOGI(TAG, "Connecting WiFi: %s", s_ssid);
        return ESP_OK;
    }
    s_wifi_state = WifiState::Failed;
    std::snprintf(s_last_error, sizeof(s_last_error), "Connect start failed: %s", esp_err_to_name(err));
    return err;
}

esp_err_t connect_saved()
{
    if (!s_has_credentials) {
        load_credentials();
    }
    if (s_credentials.empty()) {
        s_wifi_state = WifiState::Idle;
        return ESP_ERR_NOT_FOUND;
    }
    return connect_to_credential(s_credentials.front());
}

void autoconnect_task(void *arg)
{
    const bool skip_last_failed = reinterpret_cast<intptr_t>(arg) != 0;
    s_autoconnect_running = true;

    load_credentials();
    if (s_credentials.empty()) {
        s_wifi_state = WifiState::Idle;
        s_autoconnect_running = false;
        vTaskDeleteWithCaps(nullptr);
        return;
    }

    if (ensure_wifi_driver_initialized() != ESP_OK) {
        s_wifi_state = WifiState::Failed;
        std::snprintf(s_last_error, sizeof(s_last_error), "WiFi driver init failed");
        s_autoconnect_running = false;
        vTaskDeleteWithCaps(nullptr);
        return;
    }

    wifi_mode_t mode = s_provisioning ? WIFI_MODE_APSTA : WIFI_MODE_STA;
    esp_wifi_set_mode(mode);
    apply_wifi_power_save("WiFi auto mode");
    ensure_wifi_started();

    s_wifi_state = WifiState::Scanning;
    wifi_scan_config_t scan_config = {};
    esp_err_t err = ESP_ERR_TIMEOUT;
    if (wifi_op_lock(pdMS_TO_TICKS(5000))) {
        err = start_scan_and_wait(&scan_config, pdMS_TO_TICKS(8000));
        wifi_op_unlock();
    } else {
        ESP_LOGW(TAG, "Auto WiFi scan skipped: operation lock busy");
    }

    const WifiCredential *selected = nullptr;
    int selected_rssi = -1000;
    if (err == ESP_OK) {
        uint16_t count = 0;
        esp_wifi_scan_get_ap_num(&count);
        count = std::min<uint16_t>(count, 24);
        std::vector<wifi_ap_record_t> records(count);
        esp_wifi_scan_get_ap_records(&count, records.data());

        for (int i = 0; i < count; ++i) {
            const char *seen_ssid = reinterpret_cast<const char *>(records[i].ssid);
            if ((seen_ssid == nullptr) || (seen_ssid[0] == '\0')) {
                continue;
            }
            if (skip_last_failed && (std::strcmp(seen_ssid, s_last_failed_ssid) == 0)) {
                continue;
            }
            for (const auto &credential : s_credentials) {
                if ((credential.ssid == seen_ssid) && (records[i].rssi > selected_rssi)) {
                    selected = &credential;
                    selected_rssi = records[i].rssi;
                }
            }
        }
    }

    if (selected == nullptr) {
        for (const auto &credential : s_credentials) {
            if (!skip_last_failed || (credential.ssid != s_last_failed_ssid)) {
                selected = &credential;
                break;
            }
        }
    }

    if (selected != nullptr) {
        connect_to_credential(*selected);
    } else {
        s_wifi_state = WifiState::Failed;
        std::snprintf(s_last_error, sizeof(s_last_error), "No saved WiFi available");
    }

    s_autoconnect_running = false;
    vTaskDeleteWithCaps(nullptr);
}

esp_err_t request_autoconnect(bool skip_last_failed)
{
    if (s_autoconnect_running) {
        return ESP_OK;
    }
    if (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) < WIFI_INTERNAL_HEAP_WARN_BYTES) {
        std::snprintf(s_last_error, sizeof(s_last_error), "Auto WiFi skipped: low task memory");
        ESP_LOGW(TAG, "start WiFi autoconnect skipped: internal=%u largest_internal=%u",
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                 static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreateWithCaps(
            autoconnect_task,
            "wifi_auto",
            WIFI_AUTO_TASK_STACK_SIZE,
            reinterpret_cast<void *>(static_cast<intptr_t>(skip_last_failed ? 1 : 0)),
            4,
            nullptr,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        ) != pdPASS) {
        ESP_LOGE(TAG, "start WiFi autoconnect task failed: no memory");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void wifi_event_handler(void *, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            auto *event = static_cast<wifi_event_sta_disconnected_t *>(event_data);
            const uint8_t reason = event != nullptr ? event->reason : 0;
            s_connected = false;
            s_last_rssi = 0;
            if (s_ignore_next_assoc_leave && (reason == WIFI_REASON_ASSOC_LEAVE)) {
                s_ignore_next_assoc_leave = false;
                ESP_LOGI(TAG, "Ignore expected WiFi assoc leave before reconnect");
                return;
            }
            s_ignore_next_assoc_leave = false;
            if (s_manual_disconnect) {
                s_manual_disconnect = false;
                s_last_disconnect_reason = reason;
                s_last_failed_ssid[0] = '\0';
                if (!s_has_credentials) {
                    s_ssid[0] = '\0';
                    s_wifi_state = WifiState::Idle;
                }
                ESP_LOGI(TAG, "WiFi disconnected by user");
                return;
            }
            s_last_disconnect_reason = reason;
            std::snprintf(s_last_failed_ssid, sizeof(s_last_failed_ssid), "%s", s_ssid);
            std::snprintf(
                s_last_error, sizeof(s_last_error), "%s: %s (%u)",
                s_ssid[0] != '\0' ? s_ssid : "WiFi", disconnect_reason_text(s_last_disconnect_reason),
                static_cast<unsigned>(s_last_disconnect_reason)
            );

            if (s_credentials.size() > 1) {
                request_autoconnect(true);
            } else {
                s_wifi_state = WifiState::Failed;
            }
        } else if (event_id == WIFI_EVENT_STA_CONNECTED) {
            auto *event = static_cast<wifi_event_sta_connected_t *>(event_data);
            std::snprintf(s_ssid, sizeof(s_ssid), "%s", event->ssid);
            s_has_credentials = true;
            s_wifi_state = WifiState::Connecting;
        } else if (event_id == WIFI_EVENT_SCAN_DONE) {
            SemaphoreHandle_t sem = scan_done_sem();
            if (sem != nullptr) {
                xSemaphoreGive(sem);
            }
        }
    } else if ((event_base == IP_EVENT) && (event_id == IP_EVENT_STA_GOT_IP)) {
        wifi_ap_record_t ap = {};
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            s_last_rssi = ap.rssi;
            std::snprintf(s_ssid, sizeof(s_ssid), "%s", ap.ssid);
        }
        s_connected = true;
        s_wifi_state = WifiState::Connected;
        apply_wifi_power_save("WiFi got IP");
        if (s_provisioning) {
            s_provisioning_connected_ms = esp_timer_get_time() / 1000;
        }
        s_connect_started_ms = 0;
        s_last_error[0] = '\0';
        s_last_failed_ssid[0] = '\0';
        s_last_disconnect_reason = 0;
    }
}

std::string build_scan_options()
{
    wifi_scan_config_t scan_config = {};
    esp_err_t err = start_scan_and_wait(&scan_config, pdMS_TO_TICKS(8000));
    if (err != ESP_OK) {
        return "<option value=\"\">Scan unavailable</option>";
    }

    uint16_t count = 0;
    esp_wifi_scan_get_ap_num(&count);
    count = std::min<uint16_t>(count, 16);
    std::vector<wifi_ap_record_t> records(count);
    esp_wifi_scan_get_ap_records(&count, records.data());

    std::string options;
    for (int i = 0; i < count; ++i) {
        if (records[i].ssid[0] == '\0') {
            continue;
        }
        const std::string ssid = html_escape(reinterpret_cast<const char *>(records[i].ssid));
        options += "<option value=\"" + ssid + "\">" + ssid + " (" + std::to_string(records[i].rssi) + " dBm)</option>";
    }
    if (options.empty()) {
        options = "<option value=\"\">No WiFi found</option>";
    }
    return options;
}

std::string build_saved_options()
{
    load_credentials();
    std::string options;
    for (const auto &credential : s_credentials) {
        const std::string ssid = html_escape(credential.ssid.c_str());
        options += "<option value=\"" + ssid + "\">" + ssid + "</option>";
    }
    if (options.empty()) {
        options = "<option value=\"\">No saved WiFi</option>";
    }
    return options;
}

esp_err_t root_get_handler(httpd_req_t *req)
{
    char status[128] = {};
    wifi_status_text(status, sizeof(status));

    std::string page =
        "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Watch WiFi Setup</title><style>"
        "body{font-family:-apple-system,BlinkMacSystemFont,Arial,sans-serif;background:#0b0d12;color:#f5f7fb;margin:24px}"
        ".card{max-width:520px;margin:auto;background:#171b24;border-radius:20px;padding:20px}"
        "input,select,button{width:100%;font-size:18px;margin:8px 0;padding:12px;border-radius:12px;border:0;box-sizing:border-box}"
        "button{background:#1677ff;color:white;font-weight:700}.danger{background:#ff453a}.muted{color:#9aa4b2}.line{height:1px;background:#2a303b;margin:18px 0}</style></head>"
        "<body><div class='card'><h2>Watch WiFi Setup</h2><p class='muted'>Status: ";
    page += html_escape(status);
    page += "</p><p class='muted'>Select WiFi, enter password, then submit. Re-submit the same SSID to overwrite a wrong password.</p>"
            "<p class='muted'>ESP32-S3 only supports 2.4GHz WiFi. If connection fails, check the status reason code shown here and in Settings.</p>"
            "<form method='post' action='/save'><label>SSID</label><select name='ssid'>";
    page += build_scan_options();
    page += "</select><label>Manual SSID / 手动输入 WiFi 名称</label><input name='manual_ssid' placeholder='Use this if scan is unavailable'>"
            "<label>Password</label><input name='pass' type='password' placeholder='WiFi password'>"
            "<button type='submit'>Save / Update and Connect</button></form>"
            "<div class='line'></div><form method='post' action='/forget'><label>Forget saved WiFi</label><select name='ssid'>";
    page += build_saved_options();
    page += "</select><button class='danger' type='submit'>Forget Selected WiFi</button></form>"
            "<form method='post' action='/forget-all'><button class='danger' type='submit'>Forget All / Stop WiFi</button></form>"
            "<p class='muted'>Setup AP: ";
    page += html_escape(s_ap_ssid);
    page += "<br>URL: http://";
    page += SETUP_IP;
    page += "</p></div></body></html>";

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, page.c_str(), page.size());
}

std::string result_page(const char *title, const char *message)
{
    std::string page =
        "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Watch WiFi Setup</title><style>"
        "body{font-family:-apple-system,BlinkMacSystemFont,Arial,sans-serif;background:#0b0d12;color:#f5f7fb;margin:24px}"
        ".card{max-width:520px;margin:auto;background:#171b24;border-radius:20px;padding:20px}"
        "a{display:block;text-align:center;background:#1677ff;color:white;text-decoration:none;font-size:18px;margin:16px 0 0;padding:12px;border-radius:12px;font-weight:700}"
        ".muted{color:#9aa4b2}</style></head><body><div class='card'><h2>";
    page += html_escape(title);
    page += "</h2><p class='muted'>";
    page += html_escape(message);
    page += "</p><a href='/'>Back / Re-enter Password</a></div></body></html>";
    return page;
}

esp_err_t save_post_handler(httpd_req_t *req)
{
    const int len = std::min<int>(req->content_len, 256);
    char body[257] = {};
    int received = 0;
    while (received < len) {
        int ret = httpd_req_recv(req, body + received, len - received);
        if (ret <= 0) {
            return ESP_FAIL;
        }
        received += ret;
    }

    std::string raw(body, received);
    std::string ssid = form_value(raw, "ssid");
    std::string manual_ssid = form_value(raw, "manual_ssid");
    std::string pass = form_value(raw, "pass");
    if (ssid.empty()) {
        ssid = manual_ssid;
    }

    if (ssid.empty() || (ssid.size() > 32) || (pass.size() > 64)) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "Invalid SSID or password length.");
    }

    esp_err_t err = save_credentials(ssid, pass);
    if (err == ESP_OK) {
        err = connect_saved();
    }

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    if (err == ESP_OK) {
        std::string page = result_page("Saved / Updated", "Watch is connecting. If the password was wrong, return and submit the same SSID again with the correct password.");
        return httpd_resp_send(req, page.c_str(), page.size());
    }

    char msg[96] = {};
    std::snprintf(msg, sizeof(msg), "Failed: %s", esp_err_to_name(err));
    std::string page = result_page("Failed", msg);
    return httpd_resp_send(req, page.c_str(), page.size());
}

esp_err_t forget_post_handler(httpd_req_t *req)
{
    const int len = std::min<int>(req->content_len, 128);
    char body[129] = {};
    int received = 0;
    while (received < len) {
        int ret = httpd_req_recv(req, body + received, len - received);
        if (ret <= 0) {
            return ESP_FAIL;
        }
        received += ret;
    }

    std::string ssid = form_value(std::string(body, received), "ssid");
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    if (ssid.empty()) {
        std::string page = result_page("No WiFi selected", "There is no saved WiFi to forget.");
        return httpd_resp_send(req, page.c_str(), page.size());
    }

    esp_err_t err = forget_credentials(ssid);
    if (err == ESP_OK) {
        std::string msg = "Forgot WiFi: " + ssid + ". It will not auto-connect unless you save it again.";
        std::string page = result_page("Forgot", msg.c_str());
        return httpd_resp_send(req, page.c_str(), page.size());
    }

    char msg[96] = {};
    std::snprintf(msg, sizeof(msg), "Forget failed: %s", esp_err_to_name(err));
    std::string page = result_page("Failed", msg);
    return httpd_resp_send(req, page.c_str(), page.size());
}

esp_err_t forget_all_post_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    esp_err_t err = forget_all_credentials();
    if (err == ESP_OK) {
        std::string page = result_page("WiFi stopped", "All saved WiFi passwords were removed. The watch will not auto-connect until you save WiFi again.");
        return httpd_resp_send(req, page.c_str(), page.size());
    }

    char msg[96] = {};
    std::snprintf(msg, sizeof(msg), "Forget all failed: %s", esp_err_to_name(err));
    std::string page = result_page("Failed", msg);
    return httpd_resp_send(req, page.c_str(), page.size());
}

esp_err_t redirect_404_handler(httpd_req_t *req, httpd_err_code_t)
{
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", SETUP_URL);
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, "Open watch WiFi setup page.");
}

void dns_server_task(void *)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGW(TAG, "DNS socket create failed");
        s_dns_task = nullptr;
        vTaskDeleteWithCaps(nullptr);
        return;
    }

    sockaddr_in listen_addr = {};
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    listen_addr.sin_port = htons(53);

    if (bind(sock, reinterpret_cast<sockaddr *>(&listen_addr), sizeof(listen_addr)) < 0) {
        ESP_LOGW(TAG, "DNS bind failed");
        close(sock);
        s_dns_task = nullptr;
        vTaskDeleteWithCaps(nullptr);
        return;
    }

    timeval timeout = {
        .tv_sec = 1,
        .tv_usec = 0,
    };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    uint8_t request[256] = {};
    uint8_t response[512] = {};
    while (s_provisioning) {
        sockaddr_in source_addr = {};
        socklen_t source_len = sizeof(source_addr);
        int len = recvfrom(sock, request, sizeof(request), 0, reinterpret_cast<sockaddr *>(&source_addr), &source_len);
        if (len < 12) {
            continue;
        }

        int question_end = 12;
        while ((question_end < len) && (request[question_end] != 0)) {
            question_end += request[question_end] + 1;
        }
        question_end += 5; // zero label + QTYPE + QCLASS
        if ((question_end > len) || (question_end + 16 > static_cast<int>(sizeof(response)))) {
            continue;
        }

        std::memcpy(response, request, question_end);
        response[2] = 0x81; // response, recursion desired/available
        response[3] = 0x80;
        response[6] = 0x00;
        response[7] = 0x01; // one answer
        response[8] = 0x00;
        response[9] = 0x00;
        response[10] = 0x00;
        response[11] = 0x00;

        int out = question_end;
        response[out++] = 0xC0;
        response[out++] = 0x0C; // pointer to queried name
        response[out++] = 0x00;
        response[out++] = 0x01; // A
        response[out++] = 0x00;
        response[out++] = 0x01; // IN
        response[out++] = 0x00;
        response[out++] = 0x00;
        response[out++] = 0x00;
        response[out++] = 0x00; // TTL
        response[out++] = 0x00;
        response[out++] = 0x04; // IPv4 length
        response[out++] = 192;
        response[out++] = 168;
        response[out++] = 4;
        response[out++] = 1;

        sendto(sock, response, out, 0, reinterpret_cast<sockaddr *>(&source_addr), source_len);
    }
    close(sock);
    ESP_LOGI(TAG, "DNS redirect task stopped");
    s_dns_task = nullptr;
    vTaskDeleteWithCaps(nullptr);
}

esp_err_t start_dns_server()
{
    if (s_dns_task != nullptr) {
        return ESP_OK;
    }
    if (xTaskCreateWithCaps(
            dns_server_task,
            "watch_dns",
            WIFI_DNS_TASK_STACK_SIZE,
            nullptr,
            4,
            &s_dns_task,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        ) != pdPASS) {
        s_dns_task = nullptr;
        ESP_LOGE(TAG, "start DNS task failed: no memory");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void set_captive_portal_dhcp_option()
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (netif == nullptr) {
        return;
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_stop(netif));
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        esp_netif_dhcps_option(
            netif, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI,
            const_cast<char *>(SETUP_URL), std::strlen(SETUP_URL)
        )
    );
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_start(netif));
}

esp_err_t start_httpd()
{
    if (s_httpd != nullptr) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.stack_size = 3072;
    config.max_uri_handlers = 5;
    config.max_open_sockets = 2;
    config.backlog_conn = 1;
    config.lru_purge_enable = true;

    ESP_RETURN_ON_ERROR(httpd_start(&s_httpd, &config), TAG, "start httpd failed");

    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
        .user_ctx = nullptr,
    };
    httpd_uri_t save_uri = {
        .uri = "/save",
        .method = HTTP_POST,
        .handler = save_post_handler,
        .user_ctx = nullptr,
    };
    httpd_uri_t forget_uri = {
        .uri = "/forget",
        .method = HTTP_POST,
        .handler = forget_post_handler,
        .user_ctx = nullptr,
    };
    httpd_uri_t forget_all_uri = {
        .uri = "/forget-all",
        .method = HTTP_POST,
        .handler = forget_all_post_handler,
        .user_ctx = nullptr,
    };
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &root_uri), TAG, "register root failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &save_uri), TAG, "register save failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &forget_uri), TAG, "register forget failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &forget_all_uri), TAG, "register forget all failed");
    ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_register_err_handler(s_httpd, HTTPD_404_NOT_FOUND, redirect_404_handler));
    return ESP_OK;
}

void provision_guard_task(void *)
{
    s_provision_guard_running = true;
    while (s_provisioning) {
        const int64_t now = esp_timer_get_time() / 1000;
        if ((s_provisioning_connected_ms > 0) &&
            ((now - s_provisioning_connected_ms) >= PROVISIONING_CONNECTED_GRACE_MS)) {
            stop_provisioning_internal("WiFi connected");
            break;
        }
        if ((s_provisioning_started_ms > 0) &&
            ((now - s_provisioning_started_ms) >= PROVISIONING_TIMEOUT_MS)) {
            stop_provisioning_internal("setup timeout");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    s_provision_guard_running = false;
    vTaskDeleteWithCaps(nullptr);
}

esp_err_t start_provision_guard()
{
    if (s_provision_guard_running) {
        return ESP_OK;
    }
    if (xTaskCreateWithCaps(
            provision_guard_task,
            "wifi_setup_guard",
            WIFI_GUARD_TASK_STACK_SIZE,
            nullptr,
            3,
            nullptr,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        ) != pdPASS) {
        ESP_LOGW(TAG, "start setup guard failed: no memory");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t stop_provisioning_internal(const char *reason)
{
    if (!s_provisioning) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping WiFi provisioning: %s", reason != nullptr ? reason : "requested");
    if (s_httpd != nullptr) {
        httpd_stop(s_httpd);
        s_httpd = nullptr;
    }

    s_provisioning = false;
    s_provisioning_started_ms = 0;
    s_provisioning_connected_ms = 0;
    s_ap_ssid[0] = '\0';

    if (!s_wifi_driver_initialized) {
        return ESP_OK;
    }

    if (s_has_credentials || s_connected) {
        esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
        if ((err != ESP_OK) && (err != ESP_ERR_WIFI_STATE)) {
            ESP_LOGW(TAG, "set STA mode after setup failed: %s", esp_err_to_name(err));
            return err;
        }
        apply_wifi_power_save("WiFi setup stopped");
        if (!s_connected && s_has_credentials) {
            request_autoconnect(false);
        }
        return ESP_OK;
    }

    esp_wifi_disconnect();
    esp_err_t err = esp_wifi_stop();
    if ((err == ESP_OK) || (err == ESP_ERR_WIFI_NOT_STARTED) || (err == ESP_ERR_WIFI_STATE)) {
        s_wifi_started = false;
        return ESP_OK;
    }
    ESP_LOGW(TAG, "stop WiFi after setup failed: %s", esp_err_to_name(err));
    return err;
}

esp_err_t ensure_wifi_driver_initialized()
{
    ESP_RETURN_ON_ERROR(connectivity_init(), TAG, "connectivity base init failed");
    if (s_wifi_driver_initialized) {
        return ESP_OK;
    }

    log_wifi_heap("WiFi driver init requested");
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "WiFi init failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "WiFi storage mode failed");
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, nullptr));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, nullptr));
    s_wifi_driver_initialized = true;
    log_wifi_heap("WiFi driver init done");
    return ESP_OK;
}

} // namespace

esp_err_t connectivity_init()
{
    if (s_initialized) {
        return ESP_OK;
    }

    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS init failed without erase: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_netif_init();
    if (!ok_or_already(err)) {
        return err;
    }

    err = esp_event_loop_create_default();
    if (!ok_or_already(err)) {
        return err;
    }

    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    load_credentials();
    s_initialized = true;

    if (s_has_credentials) {
        s_wifi_state = WifiState::Idle;
        ESP_LOGI(TAG, "Saved WiFi profiles loaded: %d; boot autoconnect deferred", static_cast<int>(s_credentials.size()));
    } else {
        s_wifi_state = WifiState::Idle;
        ESP_LOGI(TAG, "No saved WiFi; WiFi start deferred");
    }

    return ESP_OK;
}

esp_err_t wifi_start_provisioning()
{
    ESP_RETURN_ON_ERROR(connectivity_init(), TAG, "connectivity init failed");
    log_wifi_heap("WiFi provisioning requested");
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < WIFI_INTERNAL_HEAP_WARN_BYTES) {
        ESP_LOGW(TAG, "Low internal heap before setup; provisioning may fail");
    }
    ESP_RETURN_ON_ERROR(ensure_wifi_driver_initialized(), TAG, "WiFi driver init failed");

    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    std::snprintf(s_ap_ssid, sizeof(s_ap_ssid), "Watch-Setup-%02X%02X", mac[4], mac[5]);

    wifi_config_t ap_config = {};
    std::memcpy(ap_config.ap.ssid, s_ap_ssid, std::min(sizeof(ap_config.ap.ssid), std::strlen(s_ap_ssid)));
    ap_config.ap.ssid_len = std::strlen(s_ap_ssid);
    ap_config.ap.channel = 6;
    ap_config.ap.max_connection = 1;
    ap_config.ap.authmode = WIFI_AUTH_OPEN;

    s_provisioning = true;
    s_provisioning_started_ms = esp_timer_get_time() / 1000;
    s_provisioning_connected_ms = 0;
    s_manual_disconnect = true;
    s_wifi_state = WifiState::Idle;
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "set APSTA mode failed: %s; fallback to AP-only setup", esp_err_to_name(err));
        err = esp_wifi_set_mode(WIFI_MODE_AP);
        if (err != ESP_OK) {
            s_provisioning = false;
            s_provisioning_started_ms = 0;
            ESP_LOGE(TAG, "set AP mode failed: %s", esp_err_to_name(err));
            return err;
        }
    }
    apply_wifi_power_save("WiFi setup mode");
    err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (err != ESP_OK) {
        s_provisioning = false;
        s_provisioning_started_ms = 0;
        ESP_LOGE(TAG, "set AP config failed: %s", esp_err_to_name(err));
        return err;
    }
    err = ensure_wifi_started();
    if (err != ESP_OK) {
        s_provisioning = false;
        s_provisioning_started_ms = 0;
        ESP_LOGE(TAG, "start WiFi failed: %s", esp_err_to_name(err));
        return err;
    }
    set_captive_portal_dhcp_option();
    err = start_httpd();
    if (err != ESP_OK) {
        s_provisioning = false;
        s_provisioning_started_ms = 0;
        ESP_LOGE(TAG, "start setup server failed: %s", esp_err_to_name(err));
        return err;
    }
    err = start_dns_server();
    if (err != ESP_OK) {
        s_provisioning = false;
        s_provisioning_started_ms = 0;
        if (s_httpd != nullptr) {
            httpd_stop(s_httpd);
            s_httpd = nullptr;
        }
        ESP_LOGE(TAG, "start DNS redirect failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(start_provision_guard());

    ESP_LOGI(TAG, "WiFi provisioning started: %s http://%s", s_ap_ssid, SETUP_IP);
    log_wifi_heap("WiFi provisioning active");
    return ESP_OK;
}

esp_err_t wifi_stop_provisioning()
{
    ESP_RETURN_ON_ERROR(connectivity_init(), TAG, "connectivity init failed");
    return stop_provisioning_internal("requested");
}

esp_err_t wifi_forget_all()
{
    ESP_RETURN_ON_ERROR(connectivity_init(), TAG, "connectivity init failed");
    return forget_all_credentials();
}

esp_err_t wifi_reconnect_saved()
{
    ESP_RETURN_ON_ERROR(connectivity_init(), TAG, "connectivity init failed");
    load_credentials();
    if (s_credentials.empty()) {
        s_wifi_state = WifiState::Idle;
        std::snprintf(s_last_error, sizeof(s_last_error), "No saved WiFi");
        return ESP_ERR_NOT_FOUND;
    }
    s_manual_disconnect = false;
    s_last_failed_ssid[0] = '\0';
    s_last_disconnect_reason = 0;
    s_last_error[0] = '\0';
    return request_autoconnect(false);
}

esp_err_t wifi_save_and_connect(const char *ssid, const char *password)
{
    ESP_RETURN_ON_ERROR(connectivity_init(), TAG, "connectivity init failed");
    if ((ssid == nullptr) || (ssid[0] == '\0') || (std::strlen(ssid) > 32)) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((password != nullptr) && (std::strlen(password) > 64)) {
        return ESP_ERR_INVALID_ARG;
    }

    std::string ssid_text(ssid);
    std::string password_text(password != nullptr ? password : "");
    ESP_RETURN_ON_ERROR(save_credentials(ssid_text, password_text), TAG, "save WiFi failed");
    return connect_to_credential({ssid_text, password_text});
}

esp_err_t wifi_pause_for_heavy_storage()
{
    if (!s_initialized) {
        return ESP_OK;
    }
    if (!s_wifi_driver_initialized) {
        return ESP_OK;
    }
    if (!wifi_op_lock(pdMS_TO_TICKS(5000))) {
        ESP_LOGW(TAG, "pause WiFi lock busy before storage");
    }
    s_manual_disconnect = true;
    s_connected = false;
    s_last_rssi = 0;
    s_wifi_state = WifiState::Idle;
    esp_wifi_disconnect();
    esp_err_t err = esp_wifi_stop();
    if ((err == ESP_OK) || (err == ESP_ERR_WIFI_NOT_STARTED) || (err == ESP_ERR_WIFI_STATE)) {
        s_wifi_started = false;
        ESP_LOGI(TAG, "WiFi paused for heavy storage operation");
        wifi_op_unlock();
        return ESP_OK;
    }
    ESP_LOGW(TAG, "pause WiFi failed: %s", esp_err_to_name(err));
    wifi_op_unlock();
    return err;
}

esp_err_t wifi_resume_after_heavy_storage()
{
    if (!s_initialized) {
        return ESP_OK;
    }
    if (!s_wifi_driver_initialized) {
        return ESP_OK;
    }
    wifi_mode_t mode = s_provisioning ? WIFI_MODE_APSTA : WIFI_MODE_STA;
    esp_err_t err = esp_wifi_set_mode(mode);
    if ((err != ESP_OK) && (err != ESP_ERR_WIFI_STATE)) {
        ESP_LOGW(TAG, "resume WiFi mode failed: %s", esp_err_to_name(err));
        return err;
    }
    apply_wifi_power_save("WiFi resume mode");
    err = ensure_wifi_started();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "resume WiFi start failed: %s", esp_err_to_name(err));
        return err;
    }
    s_manual_disconnect = false;
    if (!s_provisioning && s_has_credentials) {
        return request_autoconnect(false);
    }
    ESP_LOGI(TAG, "WiFi resumed after heavy storage operation");
    return ESP_OK;
}

bool wifi_is_connected()
{
    return s_connected;
}

bool wifi_has_credentials()
{
    if (!s_has_credentials) {
        load_credentials();
    }
    return s_has_credentials;
}

bool wifi_is_provisioning()
{
    return s_provisioning;
}

bool time_is_synced()
{
    time_t now = 0;
    time(&now);
    struct tm timeinfo = {};
    localtime_r(&now, &timeinfo);
    return timeinfo.tm_year >= (2024 - 1900);
}

int wifi_rssi_dbm()
{
    return s_last_rssi;
}

int wifi_provisioning_remaining_s()
{
    if (!s_provisioning || (s_provisioning_started_ms <= 0)) {
        return 0;
    }
    const int64_t now = esp_timer_get_time() / 1000;
    const int64_t remain = PROVISIONING_TIMEOUT_MS - (now - s_provisioning_started_ms);
    if (remain <= 0) {
        return 0;
    }
    return static_cast<int>((remain + 999) / 1000);
}

void wifi_status_text(char *buffer, size_t buffer_size)
{
    if ((buffer == nullptr) || (buffer_size == 0)) {
        return;
    }
    if (s_provisioning) {
        std::snprintf(
            buffer,
            buffer_size,
            "Setup AP: %s  %ds left",
            s_ap_ssid[0] != '\0' ? s_ap_ssid : "starting",
            wifi_provisioning_remaining_s()
        );
    } else if (s_connected) {
        std::snprintf(buffer, buffer_size, "Connected: %s  RSSI %d", s_ssid, s_last_rssi);
    } else if (s_wifi_state == WifiState::Failed) {
        std::snprintf(buffer, buffer_size, "Failed: %s", s_last_error[0] != '\0' ? s_last_error : "WiFi connect failed");
    } else if (s_wifi_state == WifiState::Scanning) {
        std::snprintf(buffer, buffer_size, "Scanning saved WiFi...");
    } else if (s_wifi_state == WifiState::Connecting) {
        int elapsed_s = 0;
        if (s_connect_started_ms > 0) {
            elapsed_s = static_cast<int>((esp_timer_get_time() / 1000 - s_connect_started_ms) / 1000);
        }
        if (elapsed_s >= 20) {
            std::snprintf(buffer, buffer_size, "Connecting: %s >%ds; check 2.4G/password", s_ssid, elapsed_s);
        } else {
            std::snprintf(buffer, buffer_size, "Connecting: %s", s_ssid);
        }
    } else if (s_has_credentials) {
        std::snprintf(buffer, buffer_size, "Saved WiFi: %d", static_cast<int>(s_credentials.size()));
    } else {
        std::snprintf(buffer, buffer_size, "No saved WiFi");
    }
}

void wifi_short_status_text(char *buffer, size_t buffer_size)
{
    if ((buffer == nullptr) || (buffer_size == 0)) {
        return;
    }
    if (s_connected) {
        std::snprintf(buffer, buffer_size, "WiFi %ddBm", s_last_rssi);
    } else if (s_wifi_state == WifiState::Failed) {
        std::snprintf(buffer, buffer_size, "WiFi fail");
    } else if ((s_wifi_state == WifiState::Connecting) || (s_wifi_state == WifiState::Scanning)) {
        std::snprintf(buffer, buffer_size, "WiFi ...");
    } else if (s_has_credentials) {
        std::snprintf(buffer, buffer_size, "WiFi saved");
    } else {
        std::snprintf(buffer, buffer_size, "WiFi --");
    }
}

void wifi_diag_text(char *buffer, size_t buffer_size)
{
    if ((buffer == nullptr) || (buffer_size == 0)) {
        return;
    }

    load_credentials();
    const char *state = "idle";
    switch (s_wifi_state) {
    case WifiState::Idle:
        state = "idle";
        break;
    case WifiState::Scanning:
        state = "scanning";
        break;
    case WifiState::Connecting:
        state = "connecting";
        break;
    case WifiState::Connected:
        state = "connected";
        break;
    case WifiState::Failed:
        state = "failed";
        break;
    }

    int elapsed_s = 0;
    if (s_connect_started_ms > 0) {
        elapsed_s = static_cast<int>((esp_timer_get_time() / 1000 - s_connect_started_ms) / 1000);
    }

    std::snprintf(
        buffer,
        buffer_size,
        "WiFi diag\n"
        "State: %s  Connected: %s  Provision: %s\n"
        "SSID: %s  RSSI: %d  Saved: %u\n"
        "Last fail: %s  reason %u %s\n"
        "Connecting: %ds  Auto: %s  ManualDisc: %s\n"
        "Heap int/largest: %u/%u",
        state,
        s_connected ? "yes" : "no",
        s_provisioning ? "yes" : "no",
        s_ssid[0] != '\0' ? s_ssid : "--",
        s_last_rssi,
        static_cast<unsigned>(s_credentials.size()),
        s_last_error[0] != '\0' ? s_last_error : "--",
        static_cast<unsigned>(s_last_disconnect_reason),
        disconnect_reason_text(s_last_disconnect_reason),
        elapsed_s,
        s_autoconnect_running ? "yes" : "no",
        s_manual_disconnect ? "yes" : "no",
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
        static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL))
    );
}

void wifi_saved_list_text(char *buffer, size_t buffer_size)
{
    if ((buffer == nullptr) || (buffer_size == 0)) {
        return;
    }

    load_credentials();
    if (s_credentials.empty()) {
        std::snprintf(buffer, buffer_size, "No saved WiFi");
        return;
    }

    size_t used = 0;
    for (size_t i = 0; i < s_credentials.size(); ++i) {
        const auto &cred = s_credentials[i];
        int written = std::snprintf(
            buffer + used, buffer_size - used, "%s%zu. %s",
            (i == 0) ? "" : "\n", i + 1, cred.ssid.c_str()
        );
        if (written < 0) {
            break;
        }
        if (static_cast<size_t>(written) >= buffer_size - used) {
            used = buffer_size - 1;
            break;
        }
        used += static_cast<size_t>(written);
    }
}

void wifi_scan_results_text(char *buffer, size_t buffer_size)
{
    if ((buffer == nullptr) || (buffer_size == 0)) {
        return;
    }
    buffer[0] = '\0';

    WifiScanItem items[8] = {};
    size_t count = 0;
    char error_text[96] = {};
    esp_err_t err = wifi_scan_items(items, 8, &count, error_text, sizeof(error_text));
    if (err != ESP_OK) {
        std::snprintf(buffer, buffer_size, "%s", error_text[0] != '\0' ? error_text : esp_err_to_name(err));
        return;
    }

    if (count == 0) {
        std::snprintf(buffer, buffer_size, "No nearby 2.4G WiFi found");
        return;
    }

    for (size_t i = 0; i < count; ++i) {
        char line[80] = {};
        std::snprintf(
            line, sizeof(line), "%s%.24s  %ddBm%s",
            (buffer[0] == '\0') ? "" : "\n",
            items[i].ssid,
            items[i].rssi,
            items[i].saved ? "  saved" : ""
        );
        append_text(buffer, buffer_size, line);
    }

    if (buffer[0] == '\0') {
        std::snprintf(buffer, buffer_size, "No visible SSID");
    }
}

esp_err_t wifi_scan_items(WifiScanItem *items, size_t max_items, size_t *out_count, char *error_text, size_t error_text_size)
{
    if (out_count != nullptr) {
        *out_count = 0;
    }
    if ((items == nullptr) || (max_items == 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((error_text != nullptr) && (error_text_size > 0)) {
        error_text[0] = '\0';
    }

    esp_err_t err = connectivity_init();
    if (err != ESP_OK) {
        if (error_text != nullptr) {
            std::snprintf(error_text, error_text_size, "WiFi init failed: %s", esp_err_to_name(err));
        }
        return err;
    }

    err = ensure_wifi_driver_initialized();
    if (err != ESP_OK) {
        if (error_text != nullptr) {
            std::snprintf(error_text, error_text_size, "WiFi driver init failed: %s", esp_err_to_name(err));
        }
        return err;
    }

    wifi_mode_t mode = s_provisioning ? WIFI_MODE_APSTA : WIFI_MODE_STA;
    err = esp_wifi_set_mode(mode);
    if ((err != ESP_OK) && (err != ESP_ERR_WIFI_STATE)) {
        if (error_text != nullptr) {
            std::snprintf(error_text, error_text_size, "Set WiFi mode failed: %s", esp_err_to_name(err));
        }
        return err;
    }
    apply_wifi_power_save("WiFi scan mode");

    err = ensure_wifi_started();
    if (err != ESP_OK) {
        if (error_text != nullptr) {
            std::snprintf(error_text, error_text_size, "Start WiFi failed: %s", esp_err_to_name(err));
        }
        return err;
    }

    if ((s_wifi_state == WifiState::Connecting) && !s_connected) {
        if (error_text != nullptr) {
            std::snprintf(error_text, error_text_size, "Scan skipped: WiFi is connecting");
        }
        return ESP_ERR_WIFI_STATE;
    }
    if (s_autoconnect_running) {
        if (error_text != nullptr) {
            std::snprintf(error_text, error_text_size, "Scan skipped: auto WiFi is running");
        }
        return ESP_ERR_WIFI_STATE;
    }
    if (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) < WIFI_INTERNAL_HEAP_WARN_BYTES) {
        std::snprintf(s_last_error, sizeof(s_last_error), "Scan skipped: low task memory");
        if (error_text != nullptr) {
            std::snprintf(error_text, error_text_size, "Scan skipped: low memory");
        }
        return ESP_ERR_NO_MEM;
    }

    s_manual_disconnect = true;
    s_wifi_state = WifiState::Scanning;
    s_last_error[0] = '\0';
    for (int i = 0; (i < 10) && s_autoconnect_running; ++i) {
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    if (!s_provisioning) {
        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(250));
    }

    wifi_scan_config_t scan_config = {};
    scan_config.show_hidden = false;
    ESP_LOGI(TAG, "WiFi scan begin: free=%u internal=%u largest_internal=%u",
             static_cast<unsigned>(esp_get_free_heap_size()),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
    if (!wifi_op_lock(pdMS_TO_TICKS(5000))) {
        s_wifi_state = WifiState::Failed;
        std::snprintf(s_last_error, sizeof(s_last_error), "Scan failed: WiFi busy");
        if (error_text != nullptr) {
            std::snprintf(error_text, error_text_size, "Scan failed: WiFi busy");
        }
        return ESP_ERR_TIMEOUT;
    }

    constexpr int max_attempts = 5;
    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        err = start_scan_and_wait(&scan_config, pdMS_TO_TICKS(8000));
        if (err == ESP_OK) {
            break;
        }
        ESP_LOGW(TAG, "WiFi scan attempt %d/%d failed: %s free=%u internal=%u largest_internal=%u",
                 attempt + 1,
                 max_attempts,
                 esp_err_to_name(err),
                 static_cast<unsigned>(esp_get_free_heap_size()),
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                 static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
        esp_wifi_scan_stop();
        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(350 + attempt * 200));
    }
    wifi_op_unlock();
    if (err != ESP_OK) {
        s_wifi_state = WifiState::Failed;
        std::snprintf(s_last_error, sizeof(s_last_error), "Scan failed: %s", esp_err_to_name(err));
        if (error_text != nullptr) {
            std::snprintf(error_text, error_text_size, "Scan failed: %s", esp_err_to_name(err));
        }
        return err;
    }

    uint16_t record_count = 0;
    esp_wifi_scan_get_ap_num(&record_count);
    record_count = std::min<uint16_t>(record_count, 24);
    std::vector<wifi_ap_record_t> records(record_count);
    if (record_count > 0) {
        esp_wifi_scan_get_ap_records(&record_count, records.data());
    }
    load_credentials();

    std::vector<WifiScanItem> unique;
    unique.reserve(record_count);
    for (uint16_t i = 0; i < record_count; ++i) {
        if (records[i].ssid[0] == '\0') {
            continue;
        }
        const char *ssid = reinterpret_cast<const char *>(records[i].ssid);
        auto found = std::find_if(unique.begin(), unique.end(), [&](const WifiScanItem &item) {
            return std::strcmp(item.ssid, ssid) == 0;
        });
        if (found != unique.end()) {
            if (records[i].rssi > found->rssi) {
                found->rssi = records[i].rssi;
                found->authmode = static_cast<uint8_t>(records[i].authmode);
            }
            continue;
        }
        WifiScanItem item = {};
        std::snprintf(item.ssid, sizeof(item.ssid), "%s", ssid);
        item.rssi = records[i].rssi;
        item.authmode = static_cast<uint8_t>(records[i].authmode);
        item.saved = has_credential(ssid);
        unique.push_back(item);
    }

    std::sort(unique.begin(), unique.end(), [](const WifiScanItem &a, const WifiScanItem &b) {
        if (a.saved != b.saved) {
            return a.saved > b.saved;
        }
        return a.rssi > b.rssi;
    });

    const size_t count = std::min(max_items, unique.size());
    for (size_t i = 0; i < count; ++i) {
        items[i] = unique[i];
    }
    if (out_count != nullptr) {
        *out_count = count;
    }
    s_wifi_state = s_connected ? WifiState::Connected : WifiState::Idle;
    return ESP_OK;
}

void provisioning_info_text(char *buffer, size_t buffer_size)
{
    if ((buffer == nullptr) || (buffer_size == 0)) {
        return;
    }
    if (!s_provisioning) {
        std::snprintf(buffer, buffer_size, "Tap Start WiFi Setup");
        return;
    }
    std::snprintf(buffer, buffer_size, "AP: %s\nOpen: http://%s", s_ap_ssid, SETUP_IP);
}

void provisioning_qr_text(char *buffer, size_t buffer_size)
{
    if ((buffer == nullptr) || (buffer_size == 0)) {
        return;
    }
    std::snprintf(buffer, buffer_size, "%s", SETUP_URL);
}

} // namespace watch
