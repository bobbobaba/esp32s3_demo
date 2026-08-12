#pragma once

#include <cstddef>

#include "esp_err.h"

namespace watch {

struct QuotaHomeSnapshot {
    bool valid = false;
    char provider[32] = "";
    char currency[8] = "";
    double balance = 0.0;
    double today_cost = 0.0;
    double today_tokens = 0.0;
    double total_cost = 0.0;
    double total_tokens = 0.0;
    char updated_at[32] = "";
};

esp_err_t quota_home_set(const QuotaHomeSnapshot *snapshot);
esp_err_t quota_home_get(QuotaHomeSnapshot *snapshot);
void quota_home_text(char *buffer, size_t buffer_size);
bool quota_refresh_is_running();
esp_err_t quota_refresh_async();

} // namespace watch
