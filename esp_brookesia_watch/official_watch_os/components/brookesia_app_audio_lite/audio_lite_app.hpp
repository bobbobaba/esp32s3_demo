#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"

namespace esp_brookesia::apps {

class AudioLiteApp: public systems::phone::App {
public:
    static AudioLiteApp *requestInstance();
    ~AudioLiteApp() override = default;

protected:
    AudioLiteApp();

    bool run(void) override;
    bool back(void) override;

private:
    static void onBackClicked(lv_event_t *event);
    static void onTestClicked(lv_event_t *event);
    static void onVolumeChanged(lv_event_t *event);
    void refresh();

    static AudioLiteApp *_instance;
    lv_obj_t *_status_label = nullptr;
    lv_obj_t *_volume_label = nullptr;
};

} // namespace esp_brookesia::apps
