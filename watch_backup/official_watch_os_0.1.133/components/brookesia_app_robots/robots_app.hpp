#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "systems/phone/esp_brookesia_phone_app.hpp"

namespace esp_brookesia::apps {

class RobotsApp : public systems::phone::App {
public:
    static RobotsApp *requestInstance();
    ~RobotsApp() override = default;

protected:
    RobotsApp();
    bool run(void) override;
    bool back(void) override;

private:
    struct RobotSummary {
        char name[32] = {};
        bool online = false;
        int battery = -1;
        float temp_min = -1.0f;
        float temp_max = -1.0f;
        int motor_errors = -1;
    };

    static void onBack(lv_event_t *event);
    static void onRefresh(lv_event_t *event);
    static void onTimer(lv_timer_t *timer);
    static void taskEntry(void *arg);

    void startRefresh();
    void setStatus(const char *status);
    void setRobots(const RobotSummary *robots, int count, const char *updated_at);
    void refreshUi();
    void rebuildCards();

    static RobotsApp *_instance;
    SemaphoreHandle_t _mutex = nullptr;
    TaskHandle_t _task = nullptr;
    lv_timer_t *_timer = nullptr;
    lv_obj_t *_root = nullptr;
    lv_obj_t *_status_label = nullptr;
    lv_obj_t *_list = nullptr;
    RobotSummary _robots[2] = {};
    int _robot_count = 0;
    char _status[128] = "Tap Refresh";
    char _updated_at[80] = {};
    bool _running = false;
    bool _dirty = true;
};

} // namespace esp_brookesia::apps
