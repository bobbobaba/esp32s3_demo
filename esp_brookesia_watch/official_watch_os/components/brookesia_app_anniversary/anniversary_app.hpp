#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"
#include "watch_anniversary.hpp"

namespace esp_brookesia::apps {

class AnniversaryApp: public systems::phone::App {
public:
    static AnniversaryApp *requestInstance();
    ~AnniversaryApp() override = default;

protected:
    AnniversaryApp();

    bool run(void) override;
    bool back(void) override;
    bool close(void) override;

private:
    static void onBackClicked(lv_event_t *event);
    static void onPrevClicked(lv_event_t *event);
    static void onNextClicked(lv_event_t *event);
    static void onYearMinusClicked(lv_event_t *event);
    static void onYearPlusClicked(lv_event_t *event);
    static void onMonthMinusClicked(lv_event_t *event);
    static void onMonthPlusClicked(lv_event_t *event);
    static void onDayMinusClicked(lv_event_t *event);
    static void onDayPlusClicked(lv_event_t *event);
    static void onEnableClicked(lv_event_t *event);
    static void onRepeatClicked(lv_event_t *event);
    static void onNameClicked(lv_event_t *event);
    static void onHomeClicked(lv_event_t *event);
    static void onTimer(lv_timer_t *timer);

    void selectSlot(int delta);
    void adjustDate(int year_delta, int month_delta, int day_delta);
    void toggleEnabled();
    void toggleRepeat();
    void cycleName();
    void setHomeSlot();
    void loadCurrent();
    void saveCurrent();
    void refresh();
    void clearObjects();

    static AnniversaryApp *_instance;

    lv_timer_t *_timer = nullptr;
    lv_obj_t *_slot_label = nullptr;
    lv_obj_t *_name_label = nullptr;
    lv_obj_t *_date_label = nullptr;
    lv_obj_t *_delta_label = nullptr;
    lv_obj_t *_mode_label = nullptr;
    lv_obj_t *_sync_label = nullptr;
    lv_obj_t *_enable_label = nullptr;
    lv_obj_t *_repeat_label = nullptr;
    lv_obj_t *_name_button_label = nullptr;
    lv_obj_t *_home_button_label = nullptr;
    int _slot = 0;
    watch::AnniversaryItem _item = {};
};

} // namespace esp_brookesia::apps
