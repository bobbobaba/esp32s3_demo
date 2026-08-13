#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"
#include "watch_alarm.hpp"

namespace esp_brookesia::apps {

class AlarmApp: public systems::phone::App {
public:
    static AlarmApp *requestInstance();
    ~AlarmApp() override = default;

protected:
    AlarmApp();

    bool run(void) override;
    bool back(void) override;
    bool close(void) override;

private:
    static void onBackClicked(lv_event_t *event);
    static void onHourMinusClicked(lv_event_t *event);
    static void onHourPlusClicked(lv_event_t *event);
    static void onMinuteMinusClicked(lv_event_t *event);
    static void onMinutePlusClicked(lv_event_t *event);
    static void onEnableClicked(lv_event_t *event);
    static void onRepeatClicked(lv_event_t *event);
    static void onStopClicked(lv_event_t *event);
    static void onTimer(lv_timer_t *timer);

    void adjustTime(int hour_delta, int minute_delta);
    void setAlarmFromState(bool enabled, bool repeat_daily);
    void refresh();
    void playRingPulse();
    void clearObjects();

    static AlarmApp *_instance;

    lv_timer_t *_timer = nullptr;
    lv_obj_t *_time_label = nullptr;
    lv_obj_t *_state_label = nullptr;
    lv_obj_t *_enable_label = nullptr;
    lv_obj_t *_repeat_label = nullptr;
    lv_obj_t *_stop_button = nullptr;
    lv_obj_t *_ring_label = nullptr;
    uint32_t _last_ring_tick = 0;
};

} // namespace esp_brookesia::apps
