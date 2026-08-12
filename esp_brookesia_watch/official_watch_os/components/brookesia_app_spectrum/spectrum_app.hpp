#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "systems/phone/esp_brookesia_phone_app.hpp"

namespace esp_brookesia::apps {

class SpectrumApp: public systems::phone::App {
public:
    static SpectrumApp *requestInstance();
    ~SpectrumApp() override = default;

protected:
    SpectrumApp();

    bool run(void) override;
    bool back(void) override;
    bool close(void) override;

private:
    static void onBackClicked(lv_event_t *event);
    static void onTimer(lv_timer_t *timer);
    static void audioTask(void *arg);

    bool startAudioTask();
    void requestStopAudioTask();
    void refreshBars();
    void setStatus(const char *text, uint32_t color = 0xAEB7C6);

    static SpectrumApp *_instance;

    lv_timer_t *_timer = nullptr;
    lv_obj_t *_status_label = nullptr;
    lv_obj_t *_bars[40] = {};
    TaskHandle_t _task = nullptr;
    volatile bool _stop_requested = false;
    volatile bool _mic_unavailable = false;
    volatile int _mic_error = 0;
};

} // namespace esp_brookesia::apps
