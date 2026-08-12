#include "watch_storage.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <sys/statvfs.h>

#include "bsp/esp-bsp.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_littlefs.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "watch_audio.hpp"
#include "watch_connectivity.hpp"

namespace watch {
namespace {

constexpr const char *TAG = "watch_storage";
constexpr const char *LITTLEFS_LABEL = "littlefs_data";
constexpr const char *LITTLEFS_BASE_PATH = "/littlefs";
constexpr const char *SD_LOG_FILE = "/sdcard/logs/watch.log";
constexpr const char *SD_STANDARD_DIRS[] = {
    "/sdcard/music",
    "/sdcard/watchfaces",
    "/sdcard/apps",
    "/sdcard/apps/assets",
    "/sdcard/apps/data",
    "/sdcard/cache",
    "/sdcard/ota",
    "/sdcard/ota/downloads",
    "/sdcard/logs",
    "/sdcard/exports",
};
bool s_sd_mount_attempted = false;
bool s_sd_mounted = false;
esp_err_t s_sd_mount_err = ESP_OK;
bool s_littlefs_mount_attempted = false;
bool s_littlefs_mounted = false;
esp_err_t s_littlefs_mount_err = ESP_OK;
SemaphoreHandle_t s_storage_mutex = nullptr;
StaticSemaphore_t s_storage_mutex_buffer;

SemaphoreHandle_t storage_mutex()
{
    if (s_storage_mutex == nullptr) {
        static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
        portENTER_CRITICAL(&mux);
        if (s_storage_mutex == nullptr) {
            s_storage_mutex = xSemaphoreCreateMutexStatic(&s_storage_mutex_buffer);
        }
        portEXIT_CRITICAL(&mux);
    }
    return s_storage_mutex;
}

bool storage_lock()
{
    SemaphoreHandle_t mutex = storage_mutex();
    if (mutex == nullptr) {
        ESP_LOGE(TAG, "Storage mutex allocation failed");
        return false;
    }
    xSemaphoreTake(mutex, portMAX_DELAY);
    return true;
}

void storage_unlock()
{
    if (s_storage_mutex != nullptr) {
        xSemaphoreGive(s_storage_mutex);
    }
}

void append_text(char *buffer, size_t buffer_size, const char *text)
{
    if ((buffer == nullptr) || (buffer_size == 0) || (text == nullptr)) {
        return;
    }
    size_t used = std::strlen(buffer);
    if (used >= buffer_size - 1) {
        return;
    }
    std::snprintf(buffer + used, buffer_size - used, "%s", text);
}

esp_err_t ensure_dir_unlocked(const char *path)
{
    struct stat st = {};
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return ESP_OK;
        }
        ESP_LOGW(TAG, "SD standard path exists but is not dir: %s", path);
        return ESP_FAIL;
    }
    if (mkdir(path, 0775) == 0) {
        ESP_LOGI(TAG, "Created SD dir: %s", path);
        return ESP_OK;
    }
    if (errno == EEXIST) {
        return ESP_OK;
    }
    ESP_LOGW(TAG, "Create SD dir failed: %s errno=%d", path, errno);
    return ESP_FAIL;
}

esp_err_t ensure_standard_dirs_unlocked()
{
    esp_err_t first_err = ESP_OK;
    for (const char *path : SD_STANDARD_DIRS) {
        esp_err_t err = ensure_dir_unlocked(path);
        if ((err != ESP_OK) && (first_err == ESP_OK)) {
            first_err = err;
        }
    }
    return first_err;
}

} // namespace

esp_err_t storage_sd_mount()
{
    if (!storage_lock()) {
        return ESP_ERR_NO_MEM;
    }
    if (s_sd_mounted) {
        storage_unlock();
        return ESP_OK;
    }
    s_sd_mount_attempted = true;
    s_sd_mount_err = bsp_sdcard_mount();
    if (s_sd_mount_err == ESP_OK) {
        s_sd_mounted = true;
        ESP_LOGI(TAG, "SD card mounted at %s", BSP_SD_MOUNT_POINT);
        esp_err_t dir_err = ensure_standard_dirs_unlocked();
        if (dir_err != ESP_OK) {
            ESP_LOGW(TAG, "Ensure SD standard dirs failed: %s", esp_err_to_name(dir_err));
        }
    } else {
        ESP_LOGW(TAG, "SD mount failed: %s", esp_err_to_name(s_sd_mount_err));
    }
    storage_unlock();
    return s_sd_mount_err;
}

esp_err_t storage_sd_ensure_standard_dirs()
{
    esp_err_t err = storage_sd_mount();
    if (err != ESP_OK) {
        return err;
    }
    if (!storage_lock()) {
        return ESP_ERR_NO_MEM;
    }
    err = ensure_standard_dirs_unlocked();
    storage_unlock();
    return err;
}

esp_err_t storage_sd_append_log(const char *component, const char *message)
{
    if ((message == nullptr) || (message[0] == '\0')) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = storage_sd_mount();
    if (err != ESP_OK) {
        return err;
    }
    if (!storage_lock()) {
        return ESP_ERR_NO_MEM;
    }
    FILE *fp = std::fopen(SD_LOG_FILE, "a");
    if (fp == nullptr) {
        ESP_LOGW(TAG, "Open SD log failed: %s errno=%d", SD_LOG_FILE, errno);
        storage_unlock();
        return ESP_FAIL;
    }
    const long long uptime_ms = static_cast<long long>(esp_timer_get_time() / 1000);
    std::fprintf(fp, "%lldms [%s] %s\n", uptime_ms, (component != nullptr) ? component : "watch", message);
    const int close_ret = std::fclose(fp);
    storage_unlock();
    return (close_ret == 0) ? ESP_OK : ESP_FAIL;
}

bool storage_sd_is_mounted()
{
    return storage_sd_mount() == ESP_OK;
}

bool storage_sd_cached_mounted()
{
    return s_sd_mounted;
}

esp_err_t storage_sd_format()
{
    ESP_LOGW(TAG, "SD format requested: free=%u internal=%u largest_internal=%u",
             static_cast<unsigned>(esp_get_free_heap_size()),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
    audio_test_stop();
    audio_music_stop();
    audio_mic_stop();
    vTaskDelay(pdMS_TO_TICKS(150));
    wifi_pause_for_heavy_storage();
    vTaskDelay(pdMS_TO_TICKS(500));

    if (!storage_lock()) {
        wifi_resume_after_heavy_storage();
        return ESP_ERR_NO_MEM;
    }

    if (bsp_sdcard != nullptr) {
        bsp_sdcard_unmount();
        bsp_sdcard = nullptr;
    }
    s_sd_mounted = false;
    s_sd_mount_attempted = false;
    s_sd_mount_err = ESP_OK;

    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = 2,
        .allocation_unit_size = 4096,
        .disk_status_check_enable = false,
        .use_one_fat = true,
    };
    const sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    const sdmmc_slot_config_t slot_config = {
        .clk = BSP_SD_CLK,
        .cmd = BSP_SD_CMD,
        .d0 = BSP_SD_D0,
        .d1 = GPIO_NUM_NC,
        .d2 = GPIO_NUM_NC,
        .d3 = GPIO_NUM_NC,
        .d4 = GPIO_NUM_NC,
        .d5 = GPIO_NUM_NC,
        .d6 = GPIO_NUM_NC,
        .d7 = GPIO_NUM_NC,
        .cd = SDMMC_SLOT_NO_CD,
        .wp = SDMMC_SLOT_NO_WP,
        .width = 1,
        .flags = 0,
    };

    esp_err_t err = esp_vfs_fat_sdmmc_mount(BSP_SD_MOUNT_POINT, &host, &slot_config, &mount_config, &bsp_sdcard);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SD mount for format failed: %s free=%u internal=%u largest_internal=%u",
                 esp_err_to_name(err),
                 static_cast<unsigned>(esp_get_free_heap_size()),
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                 static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
        storage_unlock();
        wifi_resume_after_heavy_storage();
        return err;
    }
    s_sd_mounted = true;
    s_sd_mount_attempted = true;
    s_sd_mount_err = ESP_OK;

    if (bsp_sdcard == nullptr) {
        storage_unlock();
        wifi_resume_after_heavy_storage();
        return ESP_ERR_INVALID_STATE;
    }
    esp_vfs_fat_mount_config_t format_config = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 4096,
        .disk_status_check_enable = false,
        .use_one_fat = true,
    };
    err = esp_vfs_fat_sdcard_format_cfg(BSP_SD_MOUNT_POINT, bsp_sdcard, &format_config);
    if (err == ESP_OK) {
        s_sd_mounted = true;
        s_sd_mount_attempted = true;
        s_sd_mount_err = ESP_OK;
        ESP_LOGW(TAG, "SD card formatted at %s", BSP_SD_MOUNT_POINT);
    } else {
        ESP_LOGW(TAG, "SD format failed: %s", esp_err_to_name(err));
        bsp_sdcard_unmount();
        bsp_sdcard = nullptr;
        s_sd_mounted = false;
        s_sd_mount_attempted = false;
        s_sd_mount_err = ESP_OK;
    }
    storage_unlock();
    vTaskDelay(pdMS_TO_TICKS(150));
    wifi_resume_after_heavy_storage();
    return err;
}

void storage_sd_status_text(char *buffer, size_t buffer_size)
{
    if ((buffer == nullptr) || (buffer_size == 0)) {
        return;
    }
    esp_err_t err = storage_sd_mount();
    if (err != ESP_OK) {
        std::snprintf(buffer, buffer_size, "SD: not mounted\nErr: %s\nPath: %s", esp_err_to_name(err), BSP_SD_MOUNT_POINT);
        return;
    }
    struct statvfs fs = {};
    unsigned long long card_total_mb = 0;
    int sector_size = 0;
    int sector_count = 0;
    int real_freq_khz = 0;
    const char *card_type = "SD";
    char card_name[9] = {};
    if (bsp_sdcard != nullptr) {
        sector_count = bsp_sdcard->csd.capacity;
        sector_size = bsp_sdcard->csd.sector_size;
        real_freq_khz = bsp_sdcard->real_freq_khz;
        if (sector_count > 0 && sector_size > 0) {
            card_total_mb = (static_cast<unsigned long long>(sector_count) * static_cast<unsigned long long>(sector_size)) / (1024ULL * 1024ULL);
        }
        card_type = bsp_sdcard->is_mmc ? "MMC" : (bsp_sdcard->is_sdio ? "SDIO" : "SD");
        std::snprintf(card_name, sizeof(card_name), "%s", bsp_sdcard->cid.name);
    }
    if (statvfs(BSP_SD_MOUNT_POINT, &fs) == 0) {
        unsigned long total_kb = static_cast<unsigned long>((fs.f_blocks * fs.f_frsize) / 1024);
        unsigned long free_kb = static_cast<unsigned long>((fs.f_bfree * fs.f_frsize) / 1024);
        std::snprintf(
            buffer,
            buffer_size,
            "SD: mounted\nPath: %s\nCard: %s %s\nRaw: %llu MB\nFS: %lu KB total, %lu KB free\nSector: %d x %d\nFreq: %d kHz",
            BSP_SD_MOUNT_POINT,
            card_type,
            card_name[0] != '\0' ? card_name : "-",
            card_total_mb,
            total_kb,
            free_kb,
            sector_count,
            sector_size,
            real_freq_khz
        );
    } else {
        std::snprintf(
            buffer,
            buffer_size,
            "SD: mounted\nPath: %s\nCard: %s %s\nRaw: %llu MB\nFS size unavailable",
            BSP_SD_MOUNT_POINT,
            card_type,
            card_name[0] != '\0' ? card_name : "-",
            card_total_mb
        );
    }
}

void storage_sd_list_text(char *buffer, size_t buffer_size, const char *path)
{
    if ((buffer == nullptr) || (buffer_size == 0)) {
        return;
    }
    buffer[0] = '\0';
    esp_err_t err = storage_sd_mount();
    if (err != ESP_OK) {
        std::snprintf(buffer, buffer_size, "Insert TF card\nMount failed: %s", esp_err_to_name(err));
        return;
    }
    const char *dir_path = (path != nullptr) ? path : BSP_SD_MOUNT_POINT;
    DIR *dir = opendir(dir_path);
    if (dir == nullptr) {
        std::snprintf(buffer, buffer_size, "Open failed:\n%s", dir_path);
        return;
    }

    append_text(buffer, buffer_size, dir_path);
    append_text(buffer, buffer_size, "\n");
    int count = 0;
    while (dirent *entry = readdir(dir)) {
        if (++count > 18) {
            append_text(buffer, buffer_size, "... more\n");
            break;
        }
        std::string full_path(dir_path);
        if (!full_path.empty() && full_path.back() != '/') {
            full_path += '/';
        }
        full_path += entry->d_name;
        struct stat st = {};
        const bool stat_ok = (stat(full_path.c_str(), &st) == 0);
        const bool is_dir = stat_ok && S_ISDIR(st.st_mode);
        char line[128] = {};
        if (is_dir || (entry->d_type == DT_DIR)) {
            std::snprintf(line, sizeof(line), "[D] %.118s\n", entry->d_name);
        } else if (stat_ok) {
            std::snprintf(line, sizeof(line), "    %.88s  %luB\n", entry->d_name, static_cast<unsigned long>(st.st_size));
        } else {
            std::snprintf(line, sizeof(line), "    %.118s\n", entry->d_name);
        }
        append_text(buffer, buffer_size, line);
    }
    closedir(dir);
    if (count == 0) {
        append_text(buffer, buffer_size, "(empty)");
    }
}

esp_err_t storage_littlefs_mount()
{
    if (!storage_lock()) {
        return ESP_ERR_NO_MEM;
    }
    if (s_littlefs_mounted) {
        storage_unlock();
        return ESP_OK;
    }
    if (esp_littlefs_mounted(LITTLEFS_LABEL)) {
        s_littlefs_mount_attempted = true;
        s_littlefs_mounted = true;
        s_littlefs_mount_err = ESP_OK;
        storage_unlock();
        return ESP_OK;
    }
    if (s_littlefs_mount_attempted && (s_littlefs_mount_err != ESP_OK)) {
        storage_unlock();
        return s_littlefs_mount_err;
    }
    s_littlefs_mount_attempted = true;

    esp_vfs_littlefs_conf_t config = {
        .base_path = LITTLEFS_BASE_PATH,
        .partition_label = LITTLEFS_LABEL,
        .format_if_mount_failed = false,
        .dont_mount = false,
    };
    s_littlefs_mount_err = esp_vfs_littlefs_register(&config);
    if ((s_littlefs_mount_err == ESP_OK) || (s_littlefs_mount_err == ESP_ERR_INVALID_STATE)) {
        s_littlefs_mounted = true;
        s_littlefs_mount_err = ESP_OK;
        ESP_LOGI(TAG, "LittleFS mounted at %s", LITTLEFS_BASE_PATH);
    } else {
        ESP_LOGW(TAG, "LittleFS mount failed: %s", esp_err_to_name(s_littlefs_mount_err));
    }
    storage_unlock();
    return s_littlefs_mount_err;
}

bool storage_littlefs_is_mounted()
{
    return storage_littlefs_mount() == ESP_OK;
}

void storage_diag_text(char *buffer, size_t buffer_size)
{
    if ((buffer == nullptr) || (buffer_size == 0)) {
        return;
    }
    std::snprintf(
        buffer,
        buffer_size,
        "Storage diag\n"
        "SD: %s  attempted: %s  err: %s\n"
        "LittleFS: %s  attempted: %s  err: %s",
        s_sd_mounted ? "mounted" : "not mounted",
        s_sd_mount_attempted ? "yes" : "no",
        esp_err_to_name(s_sd_mount_err),
        s_littlefs_mounted ? "mounted" : "not mounted",
        s_littlefs_mount_attempted ? "yes" : "no",
        esp_err_to_name(s_littlefs_mount_err)
    );
}

void storage_littlefs_status_text(char *buffer, size_t buffer_size)
{
    if ((buffer == nullptr) || (buffer_size == 0)) {
        return;
    }
    esp_err_t err = storage_littlefs_mount();
    if (err != ESP_OK) {
        std::snprintf(buffer, buffer_size, "LittleFS: not mounted\nErr: %s\nPath: %s", esp_err_to_name(err), LITTLEFS_BASE_PATH);
        return;
    }

    size_t total = 0;
    size_t used = 0;
    if (esp_littlefs_info(LITTLEFS_LABEL, &total, &used) == ESP_OK) {
        std::snprintf(
            buffer,
            buffer_size,
            "LittleFS: mounted\nPath: %s\nTotal: %lu KB\nUsed: %lu KB\nFree: %lu KB",
            LITTLEFS_BASE_PATH,
            static_cast<unsigned long>(total / 1024),
            static_cast<unsigned long>(used / 1024),
            static_cast<unsigned long>((total > used ? total - used : 0) / 1024)
        );
    } else {
        std::snprintf(buffer, buffer_size, "LittleFS: mounted\nPath: %s\nSize: unavailable", LITTLEFS_BASE_PATH);
    }
}

void storage_littlefs_list_text(char *buffer, size_t buffer_size, const char *path)
{
    if ((buffer == nullptr) || (buffer_size == 0)) {
        return;
    }
    buffer[0] = '\0';
    esp_err_t err = storage_littlefs_mount();
    if (err != ESP_OK) {
        std::snprintf(buffer, buffer_size, "LittleFS mount failed: %s", esp_err_to_name(err));
        return;
    }

    const char *dir_path = (path != nullptr) ? path : LITTLEFS_BASE_PATH;
    DIR *dir = opendir(dir_path);
    if (dir == nullptr) {
        std::snprintf(buffer, buffer_size, "Open failed:\n%s", dir_path);
        return;
    }

    append_text(buffer, buffer_size, dir_path);
    append_text(buffer, buffer_size, "\n");
    int count = 0;
    while (dirent *entry = readdir(dir)) {
        if (++count > 24) {
            append_text(buffer, buffer_size, "... more\n");
            break;
        }
        std::string full_path(dir_path);
        if (!full_path.empty() && full_path.back() != '/') {
            full_path += '/';
        }
        full_path += entry->d_name;
        struct stat st = {};
        const bool stat_ok = (stat(full_path.c_str(), &st) == 0);
        const bool is_dir = stat_ok && S_ISDIR(st.st_mode);
        char line[160] = {};
        if (is_dir) {
            std::snprintf(line, sizeof(line), "[D] %.120s\n", entry->d_name);
        } else if (stat_ok) {
            std::snprintf(line, sizeof(line), "    %.96s  %luB\n", entry->d_name, static_cast<unsigned long>(st.st_size));
        } else {
            std::snprintf(line, sizeof(line), "    %.120s\n", entry->d_name);
        }
        append_text(buffer, buffer_size, line);
    }
    closedir(dir);
    if (count == 0) {
        append_text(buffer, buffer_size, "(empty)");
    }
}

} // namespace watch
