#pragma once

#include <cstddef>

#include "esp_err.h"

namespace watch {

constexpr int ANNIVERSARY_MAX_ITEMS = 4;
constexpr size_t ANNIVERSARY_NAME_SIZE = 24;

struct AnniversaryItem {
    bool enabled = false;
    bool repeat_yearly = false;
    int year = 2026;
    int month = 8;
    int day = 11;
    char name[ANNIVERSARY_NAME_SIZE] = "";
};

esp_err_t anniversary_init();
esp_err_t anniversary_get(int index, AnniversaryItem *item);
esp_err_t anniversary_set(int index, const AnniversaryItem *item);
esp_err_t anniversary_get_home_index(int *index);
esp_err_t anniversary_set_home_index(int index);
void anniversary_summary_text(int index, char *buffer, size_t buffer_size);
void anniversary_home_text(char *buffer, size_t buffer_size);
void anniversary_delta_text(const AnniversaryItem *item, char *buffer, size_t buffer_size);
bool anniversary_today_valid();

} // namespace watch
