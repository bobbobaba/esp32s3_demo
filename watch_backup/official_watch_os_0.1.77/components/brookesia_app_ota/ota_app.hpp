#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "systems/phone/esp_brookesia_phone_app.hpp"

namespace esp_brookesia::apps {

void ota_report_startup_async();

class OtaApp: public systems::phone::App {
public:
    static OtaApp *requestInstance();
    ~OtaApp() override = default;

    enum class Operation {
        Check,
        Update,
    };

    struct OtaConfig {
        bool enabled = false;
        char version[32] = {};
        char url[192] = {};
        char log[192] = {};
    };

protected:
    OtaApp();

    bool run(void) override;
    bool back(void) override;

private:
    static void onBackClicked(lv_event_t *event);
    static void onCheckClicked(lv_event_t *event);
    static void onUpdateClicked(lv_event_t *event);
    static void onTimer(lv_timer_t *timer);
    static void taskEntry(void *arg);

    void startOperation(Operation operation);
    void runOperation(Operation operation);
    void setStatus(const char *text);
    void setButtons(bool check_enabled, bool update_enabled);
    void setProgress(int download_percent, int write_percent);
    void setDetail(const char *text);
    void updateLabels(const OtaConfig *config);
    void flushUiState();

    static OtaApp *_instance;

    lv_obj_t *_status_label = nullptr;
    lv_obj_t *_current_label = nullptr;
    lv_obj_t *_remote_label = nullptr;
    lv_obj_t *_partition_label = nullptr;
    lv_obj_t *_detail_label = nullptr;
    lv_obj_t *_log_label = nullptr;
    lv_obj_t *_download_label = nullptr;
    lv_obj_t *_write_label = nullptr;
    lv_obj_t *_download_bar = nullptr;
    lv_obj_t *_write_bar = nullptr;
    lv_obj_t *_check_btn = nullptr;
    lv_obj_t *_update_btn = nullptr;
    lv_timer_t *_timer = nullptr;
    TaskHandle_t _task = nullptr;
    SemaphoreHandle_t _state_mutex = nullptr;
    OtaConfig _latest = {};
    OtaConfig _ui_config = {};
    char _ui_status[128] = "Tap Check Update";
    char _ui_detail[160] = "Idle";
    int _ui_download_percent = 0;
    int _ui_write_percent = 0;
    bool _ui_has_config = false;
    bool _ui_check_enabled = true;
    bool _ui_update_enabled = false;
    bool _ui_dirty = true;
    bool _update_available = false;
    bool _operation_running = false;
    char _token[384] = {};
};

} // namespace esp_brookesia::apps
