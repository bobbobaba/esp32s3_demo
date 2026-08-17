#include "watch_anniversary.hpp"

#include <cstdio>
#include <cstring>
#include <ctime>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "watch_time.hpp"

namespace watch {
namespace {

constexpr const char *TAG = "watch_anniversary";
constexpr const char *NVS_NAMESPACE = "watch_anniv";

AnniversaryItem s_items[ANNIVERSARY_MAX_ITEMS] = {};
SemaphoreHandle_t s_mutex = nullptr;
bool s_init_done = false;
int s_home_index = 0;

const char *default_names[ANNIVERSARY_MAX_ITEMS] = {
    "Together",
    "Birthday",
    "Wedding",
    "Custom",
};

bool is_leap(int year)
{
    return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
}

int days_in_month(int year, int month)
{
    static constexpr int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if ((month < 1) || (month > 12)) {
        return 31;
    }
    if ((month == 2) && is_leap(year)) {
        return 29;
    }
    return days[month - 1];
}

void normalize_item(AnniversaryItem *item)
{
    if (item == nullptr) {
        return;
    }
    if (item->year < 1970) {
        item->year = 1970;
    } else if (item->year > 2099) {
        item->year = 2099;
    }
    if (item->month < 1) {
        item->month = 1;
    } else if (item->month > 12) {
        item->month = 12;
    }
    const int dim = days_in_month(item->year, item->month);
    if (item->day < 1) {
        item->day = 1;
    } else if (item->day > dim) {
        item->day = dim;
    }
    item->name[ANNIVERSARY_NAME_SIZE - 1] = '\0';
    if (item->name[0] == '\0') {
        std::snprintf(item->name, sizeof(item->name), "Anniv");
    }
}

void set_defaults()
{
    for (int i = 0; i < ANNIVERSARY_MAX_ITEMS; ++i) {
        s_items[i].enabled = (i == 0);
        s_items[i].repeat_yearly = false;
        s_items[i].year = 2026;
        s_items[i].month = 8;
        s_items[i].day = 11;
        std::snprintf(s_items[i].name, sizeof(s_items[i].name), "%s", default_names[i]);
    }
}

void lock()
{
    if (s_mutex != nullptr) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
    }
}

void unlock()
{
    if (s_mutex != nullptr) {
        xSemaphoreGive(s_mutex);
    }
}

void key(char *out, size_t size, int index, const char *field)
{
    std::snprintf(out, size, "a%d_%s", index, field);
}

esp_err_t save_one_locked(int index)
{
    if ((index < 0) || (index >= ANNIVERSARY_MAX_ITEMS)) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return err;
    }

    char k[16] = {};
    key(k, sizeof(k), index, "en");
    err = nvs_set_u8(handle, k, s_items[index].enabled ? 1 : 0);
    if (err == ESP_OK) {
        key(k, sizeof(k), index, "rep");
        err = nvs_set_u8(handle, k, s_items[index].repeat_yearly ? 1 : 0);
    }
    if (err == ESP_OK) {
        key(k, sizeof(k), index, "yr");
        err = nvs_set_i32(handle, k, s_items[index].year);
    }
    if (err == ESP_OK) {
        key(k, sizeof(k), index, "mon");
        err = nvs_set_i32(handle, k, s_items[index].month);
    }
    if (err == ESP_OK) {
        key(k, sizeof(k), index, "day");
        err = nvs_set_i32(handle, k, s_items[index].day);
    }
    if (err == ESP_OK) {
        key(k, sizeof(k), index, "name");
        err = nvs_set_str(handle, k, s_items[index].name);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

void load_one_locked(nvs_handle_t handle, int index)
{
    char k[16] = {};
    uint8_t u8 = 0;
    int32_t i32 = 0;
    size_t name_len = sizeof(s_items[index].name);

    key(k, sizeof(k), index, "en");
    if (nvs_get_u8(handle, k, &u8) == ESP_OK) {
        s_items[index].enabled = u8 != 0;
    }
    key(k, sizeof(k), index, "rep");
    if (nvs_get_u8(handle, k, &u8) == ESP_OK) {
        s_items[index].repeat_yearly = u8 != 0;
    }
    key(k, sizeof(k), index, "yr");
    if (nvs_get_i32(handle, k, &i32) == ESP_OK) {
        s_items[index].year = static_cast<int>(i32);
    }
    key(k, sizeof(k), index, "mon");
    if (nvs_get_i32(handle, k, &i32) == ESP_OK) {
        s_items[index].month = static_cast<int>(i32);
    }
    key(k, sizeof(k), index, "day");
    if (nvs_get_i32(handle, k, &i32) == ESP_OK) {
        s_items[index].day = static_cast<int>(i32);
    }
    key(k, sizeof(k), index, "name");
    nvs_get_str(handle, k, s_items[index].name, &name_len);
    normalize_item(&s_items[index]);
}

esp_err_t save_home_index_locked()
{
    if (s_home_index < 0) {
        s_home_index = 0;
    } else if (s_home_index >= ANNIVERSARY_MAX_ITEMS) {
        s_home_index = ANNIVERSARY_MAX_ITEMS - 1;
    }
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return err;
    }
    err = nvs_set_i32(handle, "home_idx", s_home_index);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

bool today_tm(struct tm *tm)
{
    if (tm == nullptr) {
        return false;
    }
    time_t now = 0;
    time(&now);
    localtime_r(&now, tm);
    return watch::time_is_valid();
}

time_t date_to_time(int year, int month, int day)
{
    struct tm tm = {};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = 0;
    tm.tm_min = 0;
    tm.tm_sec = 0;
    tm.tm_isdst = -1;
    return mktime(&tm);
}

int days_between(time_t from, time_t to)
{
    constexpr int SECONDS_PER_DAY = 24 * 60 * 60;
    double diff = difftime(to, from);
    if (diff >= 0) {
        return static_cast<int>((diff + SECONDS_PER_DAY / 2) / SECONDS_PER_DAY);
    }
    return -static_cast<int>((-diff + SECONDS_PER_DAY / 2) / SECONDS_PER_DAY);
}

} // namespace

esp_err_t anniversary_init()
{
    if (s_init_done) {
        return ESP_OK;
    }
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    lock();
    set_defaults();
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_OK) {
        for (int i = 0; i < ANNIVERSARY_MAX_ITEMS; ++i) {
            load_one_locked(handle, i);
        }
        int32_t home_index = 0;
        if (nvs_get_i32(handle, "home_idx", &home_index) == ESP_OK) {
            s_home_index = static_cast<int>(home_index);
            if (s_home_index < 0) {
                s_home_index = 0;
            } else if (s_home_index >= ANNIVERSARY_MAX_ITEMS) {
                s_home_index = ANNIVERSARY_MAX_ITEMS - 1;
            }
        }
        nvs_close(handle);
    } else {
        ESP_LOGI(TAG, "No saved anniversary data, using defaults");
    }
    s_init_done = true;
    unlock();
    return ESP_OK;
}

esp_err_t anniversary_get(int index, AnniversaryItem *item)
{
    ESP_RETURN_ON_FALSE((index >= 0) && (index < ANNIVERSARY_MAX_ITEMS), ESP_ERR_INVALID_ARG, TAG, "Invalid index");
    ESP_RETURN_ON_FALSE(item != nullptr, ESP_ERR_INVALID_ARG, TAG, "Invalid item");
    ESP_RETURN_ON_ERROR(anniversary_init(), TAG, "anniversary init failed");
    lock();
    *item = s_items[index];
    unlock();
    return ESP_OK;
}

esp_err_t anniversary_set(int index, const AnniversaryItem *item)
{
    ESP_RETURN_ON_FALSE((index >= 0) && (index < ANNIVERSARY_MAX_ITEMS), ESP_ERR_INVALID_ARG, TAG, "Invalid index");
    ESP_RETURN_ON_FALSE(item != nullptr, ESP_ERR_INVALID_ARG, TAG, "Invalid item");
    ESP_RETURN_ON_ERROR(anniversary_init(), TAG, "anniversary init failed");
    lock();
    s_items[index] = *item;
    normalize_item(&s_items[index]);
    esp_err_t err = save_one_locked(index);
    unlock();
    return err;
}

esp_err_t anniversary_get_home_index(int *index)
{
    ESP_RETURN_ON_FALSE(index != nullptr, ESP_ERR_INVALID_ARG, TAG, "Invalid index");
    ESP_RETURN_ON_ERROR(anniversary_init(), TAG, "anniversary init failed");
    lock();
    *index = s_home_index;
    unlock();
    return ESP_OK;
}

esp_err_t anniversary_set_home_index(int index)
{
    ESP_RETURN_ON_FALSE((index >= 0) && (index < ANNIVERSARY_MAX_ITEMS), ESP_ERR_INVALID_ARG, TAG, "Invalid index");
    ESP_RETURN_ON_ERROR(anniversary_init(), TAG, "anniversary init failed");
    lock();
    s_home_index = index;
    esp_err_t err = save_home_index_locked();
    unlock();
    return err;
}

bool anniversary_today_valid()
{
    struct tm tm = {};
    return today_tm(&tm);
}

void anniversary_delta_text(const AnniversaryItem *item, char *buffer, size_t buffer_size)
{
    if ((buffer == nullptr) || (buffer_size == 0)) {
        return;
    }
    if (item == nullptr) {
        std::snprintf(buffer, buffer_size, "--");
        return;
    }
    if (!item->enabled) {
        std::snprintf(buffer, buffer_size, "Off");
        return;
    }

    struct tm today = {};
    if (!today_tm(&today)) {
        std::snprintf(buffer, buffer_size, "Time not synced");
        return;
    }

    const int today_year = today.tm_year + 1900;
    const time_t today_time = date_to_time(today_year, today.tm_mon + 1, today.tm_mday);
    if (item->repeat_yearly) {
        int target_year = today_year;
        int target_day = item->day;
        if ((item->month == 2) && (item->day == 29) && !is_leap(target_year)) {
            target_day = 28;
        }
        time_t target = date_to_time(target_year, item->month, target_day);
        int delta = days_between(today_time, target);
        if (delta < 0) {
            target_year += 1;
            target_day = item->day;
            if ((item->month == 2) && (item->day == 29) && !is_leap(target_year)) {
                target_day = 28;
            }
            target = date_to_time(target_year, item->month, target_day);
            delta = days_between(today_time, target);
        }
        if (delta == 0) {
            std::snprintf(buffer, buffer_size, "Today");
        } else {
            std::snprintf(buffer, buffer_size, "Next in %dd", delta);
        }
        return;
    }

    const time_t target = date_to_time(item->year, item->month, item->day);
    const int delta = days_between(today_time, target);
    if (delta == 0) {
        std::snprintf(buffer, buffer_size, "Today");
    } else if (delta > 0) {
        std::snprintf(buffer, buffer_size, "D-%d", delta);
    } else {
        std::snprintf(buffer, buffer_size, "D+%d", -delta);
    }
}

void anniversary_summary_text(int index, char *buffer, size_t buffer_size)
{
    if ((buffer == nullptr) || (buffer_size == 0)) {
        return;
    }
    AnniversaryItem item = {};
    if (anniversary_get(index, &item) != ESP_OK) {
        std::snprintf(buffer, buffer_size, "Anniv unavailable");
        return;
    }
    char delta[32] = {};
    anniversary_delta_text(&item, delta, sizeof(delta));
    std::snprintf(
        buffer,
        buffer_size,
        "%s\n%04d-%02d-%02d  %s\n%s",
        item.name,
        item.year,
        item.month,
        item.day,
        item.repeat_yearly ? "Yearly" : "Once",
        delta
    );
}

void anniversary_home_text(char *buffer, size_t buffer_size)
{
    if ((buffer == nullptr) || (buffer_size == 0)) {
        return;
    }
    int index = 0;
    if (anniversary_get_home_index(&index) != ESP_OK) {
        std::snprintf(buffer, buffer_size, "Anniv --");
        return;
    }
    AnniversaryItem item = {};
    if (anniversary_get(index, &item) != ESP_OK || !item.enabled) {
        std::snprintf(buffer, buffer_size, "Anniv --");
        return;
    }
    char delta[32] = {};
    anniversary_delta_text(&item, delta, sizeof(delta));
    std::snprintf(buffer, buffer_size, "%s %s", item.name, delta);
}

} // namespace watch
