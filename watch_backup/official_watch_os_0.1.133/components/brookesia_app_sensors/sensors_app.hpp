#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "systems/phone/esp_brookesia_phone_app.hpp"

namespace esp_brookesia::apps {

class SensorsApp: public systems::phone::App {
public:
    static SensorsApp *requestInstance();
    ~SensorsApp() override = default;

protected:
    SensorsApp();

    bool run(void) override;
    bool back(void) override;
    bool close(void) override;

private:
    static void onBackClicked(lv_event_t *event);
    static void onRefreshClicked(lv_event_t *event);
    static void onCalibrateClicked(lv_event_t *event);
    static void onResetCalClicked(lv_event_t *event);
    static void onTimer(lv_timer_t *timer);
    static void sensorTask(void *arg);
    void startSensorTask();
    void refresh();

    static SensorsApp *_instance;
    lv_obj_t *_value_label = nullptr;
    lv_timer_t *_timer = nullptr;
    TaskHandle_t _task = nullptr;
    SemaphoreHandle_t _text_mutex = nullptr;
    volatile bool _stop_requested = false;
    volatile bool _force_refresh = false;
    char _sensor_text[768] = "Checking sensor...";
};

} // namespace esp_brookesia::apps
