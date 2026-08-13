#pragma once

#include <cstddef>

#include "esp_err.h"

namespace watch {

esp_err_t storage_sd_mount();
esp_err_t storage_sd_format();
esp_err_t storage_sd_ensure_standard_dirs();
esp_err_t storage_sd_append_log(const char *component, const char *message);
bool storage_sd_is_mounted();
bool storage_sd_cached_mounted();
void storage_sd_status_text(char *buffer, size_t buffer_size);
void storage_sd_list_text(char *buffer, size_t buffer_size, const char *path = nullptr);

esp_err_t storage_littlefs_mount();
bool storage_littlefs_is_mounted();
void storage_diag_text(char *buffer, size_t buffer_size);
void storage_littlefs_status_text(char *buffer, size_t buffer_size);
void storage_littlefs_list_text(char *buffer, size_t buffer_size, const char *path = nullptr);

} // namespace watch
