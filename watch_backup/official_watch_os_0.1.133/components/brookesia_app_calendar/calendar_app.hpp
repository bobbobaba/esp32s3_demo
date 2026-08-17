#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"

namespace esp_brookesia::apps {

class CalendarApp: public systems::phone::App {
public:
    static CalendarApp *requestInstance();
    ~CalendarApp() override = default;

protected:
    CalendarApp();

    bool run(void) override;
    bool back(void) override;
    bool close(void) override;

private:
    static void onBackClicked(lv_event_t *event);
    static void onTodayClicked(lv_event_t *event);
    static void onCalendarEvent(lv_event_t *event);
    static void onTimer(lv_timer_t *timer);

    void loadToday();
    void updateLabels();
    void clearObjects();

    static CalendarApp *_instance;

    lv_timer_t *_timer = nullptr;
    lv_obj_t *_calendar = nullptr;
    lv_obj_t *_date_label = nullptr;
    lv_obj_t *_selected_label = nullptr;
    lv_obj_t *_sync_label = nullptr;
    lv_calendar_date_t _today = {};
    lv_calendar_date_t _selected = {};
    lv_calendar_date_t _highlighted[1] = {};
    bool _time_valid = false;
};

} // namespace esp_brookesia::apps
