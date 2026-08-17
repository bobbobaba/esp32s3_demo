#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"

namespace esp_brookesia::apps {

class CounterApp : public systems::phone::App {
public:
    static CounterApp *requestInstance();
    ~CounterApp() override = default;

protected:
    CounterApp();
    bool run(void) override;
    bool back(void) override;

private:
    static void onPlus(lv_event_t *event);
    static void onMinus(lv_event_t *event);
    static void onReset(lv_event_t *event);
    static void onBack(lv_event_t *event);
    void updateLabel();

    static CounterApp *_instance;
    lv_obj_t *_value_label = nullptr;
    int32_t _value = 0;
};

} // namespace esp_brookesia::apps
