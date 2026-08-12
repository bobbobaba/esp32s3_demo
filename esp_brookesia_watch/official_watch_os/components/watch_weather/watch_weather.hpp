#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

namespace watch {

struct WeatherSnapshot {
    bool valid = false;
    bool running = false;
    bool ok = false;
    int64_t last_refresh_us = 0;
    char city[32] = {};
    char condition[24] = {};
    char wind_dir[8] = {};
    char source[16] = {};
    char current_time[24] = {};
    char timezone[32] = {};
    float temperature_c = 0.0f;
    float apparent_c = 0.0f;
    float temp_min_c = 0.0f;
    float temp_max_c = 0.0f;
    float humidity = 0.0f;
    float wind_kmh = 0.0f;
    float gust_kmh = 0.0f;
    float precipitation_mm = 0.0f;
    int rain_prob = -1;
};

esp_err_t weather_fetch(WeatherSnapshot *snapshot, char *error = nullptr, size_t error_size = 0);
bool weather_refresh_async();
bool weather_snapshot(WeatherSnapshot *snapshot);
bool weather_is_running();
void weather_format_home(const WeatherSnapshot &snapshot, char *buffer, size_t buffer_size);

} // namespace watch

