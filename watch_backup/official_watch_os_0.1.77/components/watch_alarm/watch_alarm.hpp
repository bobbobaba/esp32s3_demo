#pragma once

#include <cstddef>

#include "esp_err.h"

namespace watch {

struct AlarmState {
    bool enabled = false;
    bool ringing = false;
    bool repeat_daily = true;
    int hour = 7;
    int minute = 30;
    int last_fire_yday = -1;
};

esp_err_t alarm_init();
esp_err_t alarm_set(int hour, int minute, bool enabled, bool repeat_daily);
void alarm_get_state(AlarmState *state);
bool alarm_poll();
void alarm_stop();
void alarm_status_text(char *buffer, size_t buffer_size);

} // namespace watch
