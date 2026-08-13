#pragma once

#include <cstddef>
#include <ctime>

#include "esp_err.h"

namespace watch {

struct TimeState {
    bool system_valid = false;
    bool rtc_available = false;
    bool rtc_valid = false;
    bool sntp_started = false;
    bool sntp_synced = false;
    time_t now = 0;
    time_t rtc_time = 0;
};

esp_err_t time_service_init();
esp_err_t time_sync_from_rtc();
esp_err_t time_write_rtc_from_system();
esp_err_t time_start_sntp_if_needed();
bool time_is_valid();
void time_poll();
esp_err_t time_get_state(TimeState *state);
void time_status_text(char *buffer, size_t buffer_size);

} // namespace watch
