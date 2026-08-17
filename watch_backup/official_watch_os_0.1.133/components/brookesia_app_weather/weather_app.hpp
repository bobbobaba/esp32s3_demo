#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"

namespace esp_brookesia::apps {

class WeatherApp: public systems::phone::App {
public:
    static WeatherApp *requestInstance();
    ~WeatherApp() override = default;

protected:
    WeatherApp();

    bool run(void) override;
    bool back(void) override;
    bool close(void) override;

private:
    static void onBackClicked(lv_event_t *event);
    static void onRefreshClicked(lv_event_t *event);
    static void onTimer(lv_timer_t *timer);

    bool startRefresh();
    void refreshUi();
    void clearObjects();

    static WeatherApp *_instance;

    lv_timer_t *_timer = nullptr;
    lv_obj_t *_network_label = nullptr;
    lv_obj_t *_main_label = nullptr;
    lv_obj_t *_detail_label = nullptr;
    lv_obj_t *_sync_label = nullptr;
    lv_obj_t *_refresh_label = nullptr;
};

} // namespace esp_brookesia::apps
