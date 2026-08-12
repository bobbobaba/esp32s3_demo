#pragma once

#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "systems/phone/esp_brookesia_phone_app.hpp"

namespace esp_brookesia::apps {

class CloudApp: public systems::phone::App {
public:
    static CloudApp *requestInstance();
    ~CloudApp() override = default;

    void setCloudState(bool running, bool ok, const char *status, const char *summary, const char *detail);

protected:
    CloudApp();

    bool run(void) override;
    bool back(void) override;
    bool close(void) override;

private:
    static void onBackClicked(lv_event_t *event);
    static void onRefreshClicked(lv_event_t *event);
    static void onTimer(lv_timer_t *timer);
    static void cloudTask(void *arg);

    bool startRefresh();
    void refreshUi();
    void clearObjects();

    static CloudApp *_instance;

    SemaphoreHandle_t _mutex = nullptr;
    lv_timer_t *_timer = nullptr;
    lv_obj_t *_status_label = nullptr;
    lv_obj_t *_summary_label = nullptr;
    lv_obj_t *_detail_label = nullptr;
    lv_obj_t *_refresh_label = nullptr;
    std::string _status = "Idle";
    std::string _summary = "Tap Refresh";
    std::string _detail = "Cloud monitor reads private OTA backend status.";
    bool _running = false;
    bool _ok = false;
    int64_t _last_refresh_us = 0;
};

} // namespace esp_brookesia::apps
