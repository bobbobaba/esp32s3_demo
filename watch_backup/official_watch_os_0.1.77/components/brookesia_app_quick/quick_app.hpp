#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"

namespace esp_brookesia::apps {

class QuickApp: public systems::phone::App {
public:
    static QuickApp *requestInstance();
    ~QuickApp() override = default;

protected:
    QuickApp();

    bool run(void) override;
    bool back(void) override;

private:
    static void onBackClicked(lv_event_t *event);
    static void onRefreshClicked(lv_event_t *event);
    static void onBrightnessChanged(lv_event_t *event);
    static void onVolumeChanged(lv_event_t *event);
    static void onTimer(lv_timer_t *timer);

    void refresh();

    static QuickApp *_instance;

    lv_timer_t *_timer = nullptr;
    lv_obj_t *_wifi_label = nullptr;
    lv_obj_t *_battery_label = nullptr;
    lv_obj_t *_brightness_label = nullptr;
    lv_obj_t *_volume_label = nullptr;
};

} // namespace esp_brookesia::apps
