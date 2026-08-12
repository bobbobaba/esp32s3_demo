#pragma once

#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "systems/phone/esp_brookesia_phone_app.hpp"

namespace esp_brookesia::apps {

class QuotaApp: public systems::phone::App {
public:
    static QuotaApp *requestInstance();
    ~QuotaApp() override = default;

    void setState(
        bool running,
        bool ok,
        const char *headline,
        const char *title,
        const char *summary,
        const char *today,
        const char *total,
        const char *models,
        const char *detail
    );

protected:
    QuotaApp();

    bool run(void) override;
    bool back(void) override;
    bool close(void) override;

private:
    static void onBackClicked(lv_event_t *event);
    static void onRefreshClicked(lv_event_t *event);
    static void onTimer(lv_timer_t *timer);
    static void quotaTask(void *arg);

    bool startRefresh();
    void refreshUi();
    void clearObjects();

    static QuotaApp *_instance;

    SemaphoreHandle_t _mutex = nullptr;
    lv_timer_t *_timer = nullptr;
    lv_obj_t *_headline_label = nullptr;
    lv_obj_t *_title_label = nullptr;
    lv_obj_t *_summary_label = nullptr;
    lv_obj_t *_today_label = nullptr;
    lv_obj_t *_total_label = nullptr;
    lv_obj_t *_models_label = nullptr;
    lv_obj_t *_detail_label = nullptr;
    lv_obj_t *_refresh_label = nullptr;
    std::string _headline = "CURRENT BALANCE";
    std::string _title = "Quota";
    std::string _summary = "Tap Refresh";
    std::string _today = "Today: --";
    std::string _total = "Total: --";
    std::string _models = "Models: --";
    std::string _detail = "API usage is synced through private OTA backend.";
    bool _running = false;
    bool _ok = false;
    int64_t _last_refresh_us = 0;
};

} // namespace esp_brookesia::apps
