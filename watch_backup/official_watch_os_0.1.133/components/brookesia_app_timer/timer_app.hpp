#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"

namespace esp_brookesia::apps {

class TimerApp: public systems::phone::App {
public:
    static TimerApp *requestInstance();
    ~TimerApp() override = default;

protected:
    TimerApp();

    bool run(void) override;
    bool back(void) override;
    bool close(void) override;

private:
    static void onBackClicked(lv_event_t *event);
    static void onModeClicked(lv_event_t *event);
    static void onStartClicked(lv_event_t *event);
    static void onResetClicked(lv_event_t *event);
    static void onMinusClicked(lv_event_t *event);
    static void onPlusClicked(lv_event_t *event);
    static void onTimer(lv_timer_t *timer);

    void toggleMode();
    void toggleRunning();
    void reset();
    void adjustCountdown(int delta_seconds);
    void refresh();
    void clearObjects();
    uint32_t elapsedMs() const;
    uint32_t remainingMs() const;

    static TimerApp *_instance;

    lv_timer_t *_timer = nullptr;
    lv_obj_t *_time_label = nullptr;
    lv_obj_t *_mode_label = nullptr;
    lv_obj_t *_start_label = nullptr;
    lv_obj_t *_hint_label = nullptr;
    bool _countdown_mode = false;
    bool _running = false;
    bool _expired = false;
    uint32_t _base_elapsed_ms = 0;
    uint32_t _start_tick = 0;
    uint32_t _countdown_total_ms = 5 * 60 * 1000;
    uint32_t _last_beep_tick = 0;
};

} // namespace esp_brookesia::apps
