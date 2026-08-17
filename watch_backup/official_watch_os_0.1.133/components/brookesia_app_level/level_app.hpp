#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"

namespace esp_brookesia::apps {

class LevelApp: public systems::phone::App {
public:
    static LevelApp *requestInstance();
    ~LevelApp() override = default;

protected:
    LevelApp();

    bool run(void) override;
    bool back(void) override;
    bool close(void) override;

private:
    static void onBackClicked(lv_event_t *event);
    static void onZeroClicked(lv_event_t *event);
    static void onResetClicked(lv_event_t *event);
    static void onTimer(lv_timer_t *timer);

    void refreshSensor();
    void updateDisplay(float pitch_deg, float roll_deg);
    void showSensorError();
    void stopUi();

    static LevelApp *_instance;

    lv_obj_t *_screen = nullptr;
    lv_obj_t *_bubble = nullptr;
    lv_obj_t *_pitch_value = nullptr;
    lv_obj_t *_roll_value = nullptr;
    lv_obj_t *_status_chip = nullptr;
    lv_obj_t *_status_label = nullptr;
    lv_obj_t *_reference_label = nullptr;
    lv_timer_t *_timer = nullptr;
    float _last_pitch_deg = 0.0f;
    float _last_roll_deg = 0.0f;
    float _pitch_level_filtered_deg = 0.0f;
    float _roll_level_filtered_deg = 0.0f;
    float _pitch_zero_deg = 0.0f;
    float _roll_zero_deg = 0.0f;
    uint32_t _last_activity_tick = 0;
    bool _last_sample_valid = false;
    bool _level_filter_valid = false;
    bool _zeroed = false;
};

} // namespace esp_brookesia::apps
