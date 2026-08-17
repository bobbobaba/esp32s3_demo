#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"

namespace esp_brookesia::apps {

bool watch_home_is_active();
void watch_home_set_active(bool active);
void watch_home_set_ai_chat_app_id(int app_id);

class WatchHomeApp: public systems::phone::App {
public:
    static WatchHomeApp *requestInstance();
    ~WatchHomeApp() override = default;

protected:
    WatchHomeApp();

    bool run(void) override;
    bool back(void) override;
    bool close(void) override;

private:
    static void onClockTimer(lv_timer_t *timer);
    static void onMenuButtonClicked(lv_event_t *event);
    static void onLiveTalkButtonClicked(lv_event_t *event);
    void refreshClock();
    void refreshStatus();

    static WatchHomeApp *_instance;

    lv_obj_t *_battery_label = nullptr;
    lv_obj_t *_status_label = nullptr;
    lv_obj_t *_time_label = nullptr;
    lv_obj_t *_date_label = nullptr;
    lv_obj_t *_weather_label = nullptr;
    lv_obj_t *_anniv_label = nullptr;
    lv_obj_t *_quota_label = nullptr;
    lv_timer_t *_clock_timer = nullptr;
    uint32_t _wifi_reconnect_tick = 0;
    uint32_t _weather_refresh_tick = 0;
    uint32_t _quota_refresh_tick = 0;
    uint32_t _status_refresh_tick = 0;
};

} // namespace esp_brookesia::apps
