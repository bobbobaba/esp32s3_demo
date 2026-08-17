#include "gallery_app.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>

#include "cJSON.h"
#include "esp_brookesia.hpp"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_lib_utils.h"
#include "esp_log.h"
#include "freertos/idf_additions.h"
#include "lvgl.h"
#include "watch_app_icons.hpp"
#include "watch_audio.hpp"
#include "watch_connectivity.hpp"
#include "watch_display.hpp"
#include "watch_fonts.hpp"
#include "watch_private_config.hpp"
#include "watch_storage.hpp"

#ifdef ESP_UTILS_LOG_TAG
#undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "GalleryApp"

namespace esp_brookesia::apps {
namespace {

constexpr const char *APP_NAME = "Gallery";
constexpr const char *USER_AGENT = "esp32-s3-watch-gallery";
constexpr const char *LOGIN_URL = WATCH_PRIVATE_DUDUSERVER_API_BASE_URL "/auth/login";
constexpr const char *PHOTOS_URL = WATCH_PRIVATE_DUDUSERVER_API_BASE_URL "/photos?limit=8&thumb=320";
constexpr const char *VIDEOS_URL = WATCH_PRIVATE_DUDUSERVER_API_BASE_URL "/videos?limit=8";
constexpr const char *PHOTO_DIR = "/sdcard/photos";
constexpr const char *VIDEO_DIR = "/sdcard/videos";
constexpr size_t MAX_BODY = 48 * 1024;
constexpr size_t HTTP_BUFFER = 4096;
constexpr size_t MAX_IMAGE_BYTES = 512 * 1024;
constexpr size_t MAX_VIDEO_BYTES = 3 * 1024 * 1024;
constexpr size_t MAX_VIDEO_CACHE_BYTES = MAX_VIDEO_BYTES;
constexpr size_t MAX_AUDIO_BYTES = 4 * 1024 * 1024;
constexpr size_t MAX_PHOTO_DIMENSION = 320;
constexpr size_t MAX_VIDEO_DIMENSION = 160;
constexpr uint32_t TASK_STACK = 12 * 1024;

struct HttpResponse {
    std::string body;
    int status = 0;
    bool overflow = false;
};

void copy_text(char *dst, size_t size, const char *src)
{
    if (dst != nullptr && size > 0) {
        std::snprintf(dst, size, "%s", src != nullptr ? src : "");
    }
}

std::string json_string(cJSON *object, const char *key, const char *fallback = "")
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(item) && item->valuestring != nullptr ? item->valuestring : fallback;
}

size_t json_size(cJSON *object, const char *key, size_t fallback = 0)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsNumber(item) && item->valuedouble >= 0 ? static_cast<size_t>(item->valuedouble) : fallback;
}

int json_int(cJSON *object, const char *key, int fallback = 0)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}

esp_err_t http_event(esp_http_client_event_t *event)
{
    if (event == nullptr || event->event_id != HTTP_EVENT_ON_DATA || event->user_data == nullptr ||
        event->data == nullptr || event->data_len <= 0) {
        return ESP_OK;
    }
    auto *response = static_cast<HttpResponse *>(event->user_data);
    if (response->body.size() + static_cast<size_t>(event->data_len) > MAX_BODY) {
        response->overflow = true;
        return ESP_OK;
    }
    response->body.append(static_cast<const char *>(event->data), static_cast<size_t>(event->data_len));
    return ESP_OK;
}

bool http_request(esp_http_client_method_t method, const char *url, const char *token,
                  const char *post_body, HttpResponse &response)
{
    response = {};
    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = 30000;
    config.user_agent = USER_AGENT;
    config.event_handler = http_event;
    config.user_data = &response;
    config.keep_alive_enable = false;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        return false;
    }
    esp_http_client_set_method(client, method);
    if (token != nullptr && token[0] != '\0') {
        std::string auth = "Bearer ";
        auth += token;
        esp_http_client_set_header(client, "Authorization", auth.c_str());
    }
    if (post_body != nullptr) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, post_body, std::strlen(post_body));
    }
    const esp_err_t err = esp_http_client_perform(client);
    response.status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    return err == ESP_OK && !response.overflow && response.status >= 200 && response.status < 300;
}

std::string login_token()
{
    HttpResponse response;
    if (!http_request(HTTP_METHOD_POST, LOGIN_URL, nullptr, WATCH_PRIVATE_LOGIN_BODY, response)) {
        return {};
    }
    cJSON *root = cJSON_Parse(response.body.c_str());
    if (root == nullptr) {
        return {};
    }
    std::string token = json_string(root, "access_token");
    cJSON_Delete(root);
    return token;
}

std::string absolute_url(const std::string &url)
{
    if (url.empty()) {
        return {};
    }
    if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0) {
        return url;
    }
    if (url[0] == '/') {
        return std::string(WATCH_PRIVATE_SERVER_BASE_URL) + url;
    }
    return std::string(WATCH_PRIVATE_SERVER_BASE_URL) + "/" + url;
}

lv_obj_t *label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *object = lv_label_create(parent);
    lv_label_set_text(object, text != nullptr ? text : "");
    watch_display::apply_text_font(object, text != nullptr ? text : "", font);
    lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_text_opa(object, LV_OPA_COVER, 0);
    return object;
}

void style_button(lv_obj_t *button, uint32_t color, int width = LV_PCT(100), int height = 42)
{
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_radius(button, 18, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
}

void set_label(lv_obj_t *object, const char *text, const lv_font_t *font)
{
    if (object == nullptr) {
        return;
    }
    lv_label_set_text(object, text != nullptr ? text : "");
    watch_display::apply_text_font(object, text != nullptr ? text : "", font);
}

bool file_exists(const char *path)
{
    struct stat st = {};
    return path != nullptr && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

bool commit_temp_file(const char *temp_path, const char *final_path)
{
    if (temp_path == nullptr || final_path == nullptr) {
        return false;
    }
    if (std::rename(temp_path, final_path) == 0) {
        return true;
    }

    // FatFS can reject a rename when a prior file with the same name is open
    // or has not been fully released. Copying within the same mounted volume
    // keeps downloads usable on those cards while retaining the temp file
    // until the final file has been closed successfully.
    const int rename_error = errno;
    FILE *source = std::fopen(temp_path, "rb");
    FILE *destination = std::fopen(final_path, "wb");
    if (source == nullptr || destination == nullptr) {
        if (source != nullptr) std::fclose(source);
        if (destination != nullptr) std::fclose(destination);
        ESP_LOGW(APP_NAME, "rename and copy open failed: errno=%d", rename_error);
        return false;
    }

    uint8_t buffer[1024] = {};
    bool copied = true;
    while (copied) {
        const size_t read = std::fread(buffer, 1, sizeof(buffer), source);
        if (read > 0 && std::fwrite(buffer, 1, read, destination) != read) {
            copied = false;
            break;
        }
        if (read < sizeof(buffer)) {
            copied = std::ferror(source) == 0;
            break;
        }
    }
    const int source_close = std::fclose(source);
    const int destination_close = std::fclose(destination);
    if (!copied || source_close != 0 || destination_close != 0) {
        ESP_LOGW(APP_NAME, "rename fallback copy failed: rename_errno=%d copy_errno=%d", rename_error, errno);
        return false;
    }
    std::remove(temp_path);
    ESP_LOGW(APP_NAME, "used copy fallback after rename errno=%d", rename_error);
    return true;
}

bool has_extension(const char *name, const char *extension)
{
    if (name == nullptr || extension == nullptr) {
        return false;
    }
    const size_t name_length = std::strlen(name);
    const size_t extension_length = std::strlen(extension);
    return name_length >= extension_length &&
           std::strcmp(name + name_length - extension_length, extension) == 0;
}

bool raw_image_dimensions(size_t bytes, uint16_t &width, uint16_t &height)
{
    for (const uint16_t dimension : {uint16_t{320}, uint16_t{160}, uint16_t{128}}) {
        if (bytes == static_cast<size_t>(dimension) * dimension * 2) {
            width = dimension;
            height = dimension;
            return true;
        }
    }
    return false;
}

bool raw_video_dimensions(const char *name, size_t bytes, uint16_t &width, uint16_t &height)
{
    // New downloads persist their dimensions in the filename. Existing files
    // predate that convention, so retain 128x128 as the ambiguous fallback.
    if (has_extension(name, "_160x160.rgb565v")) {
        width = 160;
        height = 160;
    } else if (has_extension(name, "_128x128.rgb565v")) {
        width = 128;
        height = 128;
    } else if (bytes % (static_cast<size_t>(128) * 128 * 2) == 0) {
        width = 128;
        height = 128;
    } else if (bytes % (static_cast<size_t>(160) * 160 * 2) == 0) {
        width = 160;
        height = 160;
    } else {
        return false;
    }
    return bytes >= static_cast<size_t>(width) * height * 2;
}

bool build_local_audio_path(const char *media_path, char *audio_path, size_t audio_path_size)
{
    if (media_path == nullptr || audio_path == nullptr || audio_path_size == 0) {
        return false;
    }
    const char *dot = std::strrchr(media_path, '.');
    if (dot == nullptr) {
        return false;
    }
    const size_t stem_length = static_cast<size_t>(dot - media_path);
    if (stem_length + 5 > audio_path_size) {
        return false;
    }
    std::memcpy(audio_path, media_path, stem_length);
    std::memcpy(audio_path + stem_length, ".wav", 5);
    return true;
}

bool wait_for_wifi()
{
    if (watch::wifi_is_connected()) {
        return true;
    }
    if (!watch::wifi_has_credentials()) {
        return false;
    }
    if (watch::wifi_reconnect_saved() != ESP_OK && !watch::wifi_is_connected()) {
        return false;
    }
    for (int attempt = 0; attempt < 40 && !watch::wifi_is_connected(); ++attempt) {
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    return watch::wifi_is_connected();
}

} // namespace

GalleryApp *GalleryApp::_instance = nullptr;

GalleryApp *GalleryApp::requestInstance()
{
    if (_instance == nullptr) {
        _instance = new GalleryApp();
    }
    return _instance;
}

GalleryApp::GalleryApp():
    systems::phone::App(APP_NAME, watch_app_icon_gallery_48(), true, true, true)
{
    _mutex = xSemaphoreCreateMutex();
}

bool GalleryApp::run(void)
{
    _local_video_selection = 0;
    _local_photo_selection = 0;
    _local_video_select_mode = false;
    _local_photo_select_mode = false;
    _delete_confirm = nullptr;
    lv_obj_t *screen = lv_scr_act();
    ESP_UTILS_CHECK_NULL_RETURN(screen, false, "Invalid active screen");
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x07080B), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    lv_obj_t *root = lv_obj_create(screen);
    ESP_UTILS_CHECK_NULL_RETURN(root, false, "Create gallery root failed");
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root, lv_color_hex(0x07080B), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(root, 22, 0);
    lv_obj_set_style_pad_right(root, 22, 0);
    lv_obj_set_style_pad_top(root, 24, 0);
    lv_obj_set_style_pad_bottom(root, 18, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(root, 8, 0);
    lv_obj_set_scroll_dir(root, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(root, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t *title_row = lv_obj_create(root);
    lv_obj_remove_style_all(title_row);
    lv_obj_set_width(title_row, LV_PCT(100));
    lv_obj_set_height(title_row, 46);
    lv_obj_set_flex_flow(title_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    label(title_row, "Gallery", &lv_font_montserrat_28, 0xFFFFFF);
    lv_obj_t *back = lv_button_create(title_row);
    style_button(back, 0x232833, 76, 38);
    lv_obj_add_event_cb(back, onBackClicked, LV_EVENT_CLICKED, this);
    lv_obj_center(label(back, "Back", &lv_font_montserrat_14, 0xFFFFFF));

    lv_obj_t *tabs = lv_obj_create(root);
    lv_obj_remove_style_all(tabs);
    lv_obj_set_width(tabs, LV_PCT(100));
    lv_obj_set_height(tabs, 38);
    lv_obj_set_flex_flow(tabs, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(tabs, 5, 0);
    const char *tab_names[] = {"Photos", "Videos", "Cloud P", "Cloud V"};
    for (int i = 0; i < 4; ++i) {
        lv_obj_t *button = lv_button_create(tabs);
        style_button(button, 0x202633, LV_PCT(25), 34);
        lv_obj_add_event_cb(button, onPhotosClicked, LV_EVENT_CLICKED, this);
        lv_obj_center(label(button, tab_names[i], &lv_font_montserrat_12, 0xD7DCE5));
    }

    _status_label = label(root, "Loading media...", &lv_font_montserrat_14, 0x8FA3B8);
    lv_obj_set_width(_status_label, LV_PCT(100));
    lv_label_set_long_mode(_status_label, LV_LABEL_LONG_WRAP);

    _progress_bar = lv_bar_create(root);
    lv_obj_set_width(_progress_bar, LV_PCT(100));
    lv_obj_set_height(_progress_bar, 8);
    lv_bar_set_range(_progress_bar, 0, 100);
    lv_bar_set_value(_progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(_progress_bar, 4, 0);
    lv_obj_set_style_bg_color(_progress_bar, lv_color_hex(0x252B38), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_progress_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(_progress_bar, lv_color_hex(0x1B6BFF), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(_progress_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(_progress_bar, 4, LV_PART_INDICATOR);
    lv_obj_add_flag(_progress_bar, LV_OBJ_FLAG_HIDDEN);

    _media_list = lv_obj_create(root);
    lv_obj_remove_style_all(_media_list);
    lv_obj_set_width(_media_list, LV_PCT(100));
    lv_obj_set_height(_media_list, 420);
    lv_obj_set_flex_flow(_media_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(_media_list, 7, 0);
    lv_obj_set_scroll_dir(_media_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(_media_list, LV_SCROLLBAR_MODE_AUTO);

    _list_label = label(_media_list, "Loading...", &lv_font_montserrat_14, 0xD7DCE5);
    lv_obj_set_width(_list_label, LV_PCT(100));
    lv_label_set_long_mode(_list_label, LV_LABEL_LONG_WRAP);

    lv_obj_t *refresh = lv_button_create(root);
    style_button(refresh, 0x1B6BFF);
    lv_obj_add_event_cb(refresh, onRefreshClicked, LV_EVENT_CLICKED, this);
    lv_obj_center(label(refresh, "Refresh cloud media", &lv_font_montserrat_16, 0xFFFFFF));

    _view = View::Photos;
    _dirty = true;
    _items_dirty = true;
    _timer = lv_timer_create(onTimer, 250, this);
    requestRefresh();
    return true;
}

bool GalleryApp::back(void)
{
    if (_viewer != nullptr) {
        closeViewer();
        return true;
    }
    if (_delete_confirm != nullptr) {
        lv_msgbox_close(_delete_confirm);
        _delete_confirm = nullptr;
        return true;
    }
    if (_timer != nullptr) {
        lv_timer_delete(_timer);
        _timer = nullptr;
    }
    if (_video_timer != nullptr) {
        lv_timer_delete(_video_timer);
        _video_timer = nullptr;
    }
    if (_video_file != nullptr) {
        std::fclose(_video_file);
        _video_file = nullptr;
    }
    releaseVideoCache();
    if (_frame_buffer != nullptr) {
        heap_caps_free(_frame_buffer);
        _frame_buffer = nullptr;
        _frame_capacity = 0;
    }
    if (_list_task != nullptr) {
        vTaskDelete(_list_task);
        _list_task = nullptr;
    }
    if (_download_task != nullptr) {
        vTaskDelete(_download_task);
        _download_task = nullptr;
    }
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

void GalleryApp::onPhotosClicked(lv_event_t *event)
{
    auto *app = static_cast<GalleryApp *>(lv_event_get_user_data(event));
    if (app == nullptr) return;
    lv_obj_t *target = static_cast<lv_obj_t *>(lv_event_get_target(event));
    const int tab = static_cast<int>(lv_obj_get_index(target));
    if (tab == 1) app->setView(View::Videos);
    else if (tab == 2) app->setView(View::CloudPhotos);
    else if (tab == 3) app->setView(View::CloudVideos);
    else app->setView(View::Photos);
}

void GalleryApp::onVideosClicked(lv_event_t *event)
{
    onPhotosClicked(event);
}

void GalleryApp::onRefreshClicked(lv_event_t *event)
{
    auto *app = static_cast<GalleryApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->requestRefresh();
}

void GalleryApp::onBackClicked(lv_event_t *event)
{
    auto *app = static_cast<GalleryApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->back();
}

void GalleryApp::onViewerBackClicked(lv_event_t *event)
{
    auto *app = static_cast<GalleryApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->closeViewer();
}

void GalleryApp::onCloudActionClicked(lv_event_t *event)
{
    auto *binding = static_cast<ActionBinding *>(lv_event_get_user_data(event));
    if (binding != nullptr && binding->app != nullptr) {
        binding->app->startCloudAction(binding->index, binding->action);
    }
}

void GalleryApp::onLocalOpenClicked(lv_event_t *event)
{
    auto *binding = static_cast<LocalActionBinding *>(lv_event_get_user_data(event));
    if (binding != nullptr && binding->app != nullptr) {
        binding->app->openLocalMedia(binding->index);
    }
}

void GalleryApp::onLocalSelectModeClicked(lv_event_t *event)
{
    auto *app = static_cast<GalleryApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->toggleLocalSelectMode(app->_view == View::Videos);
}

void GalleryApp::onLocalSelectionChanged(lv_event_t *event)
{
    auto *binding = static_cast<LocalActionBinding *>(lv_event_get_user_data(event));
    if (binding != nullptr && binding->app != nullptr) {
        binding->app->toggleLocalSelection(binding->index, binding->app->_view == View::Videos);
    }
}

void GalleryApp::onDeleteSelectedLocalMediaClicked(lv_event_t *event)
{
    auto *app = static_cast<GalleryApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->showDeleteSelectedLocalMediaConfirm(app->_view == View::Videos);
}

void GalleryApp::onDeleteConfirmClicked(lv_event_t *event)
{
    auto *binding = static_cast<DeleteConfirmBinding *>(lv_event_get_user_data(event));
    if (binding == nullptr || binding->app == nullptr) return;
    GalleryApp *app = binding->app;
    const bool confirmed = binding->confirmed;
    if (app->_delete_confirm != nullptr) {
        lv_msgbox_close(app->_delete_confirm);
        app->_delete_confirm = nullptr;
    }
    if (confirmed) app->deleteSelectedLocalMedia(binding->video);
}

void GalleryApp::onTimer(lv_timer_t *timer)
{
    auto *app = static_cast<GalleryApp *>(lv_timer_get_user_data(timer));
    if (app != nullptr) {
        if ((app->_download_task != nullptr) || (app->_video_timer != nullptr)) {
            watch::display_notify_activity();
        }
        app->flushAsyncUi();
    }
}

void GalleryApp::onVideoTimer(lv_timer_t *timer)
{
    auto *app = static_cast<GalleryApp *>(lv_timer_get_user_data(timer));
    if (app != nullptr) app->nextVideoFrame();
}

void GalleryApp::requestRefresh()
{
    if (_list_task != nullptr) return;
    updateAsyncStatus("Checking cloud media...");
    if (xTaskCreateWithCaps(listTask, "gallery_list", TASK_STACK, this, 1, &_list_task,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        _list_task = nullptr;
        updateAsyncStatus("Could not start media list task");
    }
}

void GalleryApp::refresh()
{
    requestRefresh();
}

void GalleryApp::setView(View view)
{
    if (view != View::Videos) {
        _local_video_select_mode = false;
        _local_video_selection = 0;
    }
    if (view != View::Photos) {
        _local_photo_select_mode = false;
        _local_photo_selection = 0;
    }
    _view = view;
    _items_dirty = true;
    _dirty = true;
}

void GalleryApp::updateAsyncStatus(const char *status)
{
    if (_mutex != nullptr && xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        copy_text(_status_text, sizeof(_status_text), status);
        _dirty = true;
        xSemaphoreGive(_mutex);
    }
}

void GalleryApp::updateAsyncProgress(const char *phase, int percent, int64_t received, int64_t total, bool active)
{
    char status[sizeof(_status_text)] = {};
    if (total > 0) {
        std::snprintf(status, sizeof(status), "%s %d%% (%lld/%lld KB)", phase, percent,
                      static_cast<long long>(received / 1024), static_cast<long long>(total / 1024));
    } else if (received > 0) {
        std::snprintf(status, sizeof(status), "%s (%lld KB)", phase, static_cast<long long>(received / 1024));
    } else {
        std::snprintf(status, sizeof(status), "%s", phase);
    }
    if (_mutex != nullptr && xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        copy_text(_status_text, sizeof(_status_text), status);
        _progress_percent = std::clamp(percent, 0, 100);
        _progress_active = active;
        _dirty = true;
        xSemaphoreGive(_mutex);
    }
}

bool GalleryApp::downloadRemoteFile(const std::string &url, const std::string &token, const char *temp_path,
                                    const char *phase, int progress_start, int progress_span, size_t max_bytes)
{
    if (url.empty() || temp_path == nullptr || temp_path[0] == '\0') {
        return false;
    }
    updateAsyncProgress(phase, progress_start, 0, 0, true);

    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.timeout_ms = 60000;
    config.user_agent = USER_AGENT;
    config.keep_alive_enable = false;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        updateAsyncStatus("Media HTTP init failed");
        return false;
    }

    std::string auth = "Bearer " + token;
    esp_http_client_set_header(client, "Authorization", auth.c_str());
    if (esp_http_client_open(client, 0) != ESP_OK) {
        esp_http_client_cleanup(client);
        updateAsyncStatus("Media connection failed");
        return false;
    }

    const int64_t header_length = esp_http_client_fetch_headers(client);
    const int status = esp_http_client_get_status_code(client);
    const int64_t total = header_length > 0 ? header_length : 0;
    if (status < 200 || status >= 300 || (total > 0 && static_cast<uint64_t>(total) > max_bytes)) {
        esp_http_client_cleanup(client);
        char message[96] = {};
        std::snprintf(message, sizeof(message), "Media HTTP %d", status);
        updateAsyncStatus(message);
        return false;
    }

    FILE *file = std::fopen(temp_path, "wb");
    auto *buffer = static_cast<uint8_t *>(heap_caps_malloc(HTTP_BUFFER, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (buffer == nullptr) {
        buffer = static_cast<uint8_t *>(heap_caps_malloc(HTTP_BUFFER, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    if (file == nullptr || buffer == nullptr) {
        if (file != nullptr) std::fclose(file);
        if (buffer != nullptr) heap_caps_free(buffer);
        esp_http_client_cleanup(client);
        std::remove(temp_path);
        updateAsyncStatus("Media storage unavailable");
        return false;
    }

    bool ok = true;
    bool complete = false;
    int64_t received = 0;
    int last_percent = -1;
    while (ok) {
        const int read = esp_http_client_read(client, reinterpret_cast<char *>(buffer), HTTP_BUFFER);
        if (read < 0) {
            ok = false;
            break;
        }
        if (read == 0) {
            if (esp_http_client_is_complete_data_received(client)) {
                complete = true;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (std::fwrite(buffer, 1, static_cast<size_t>(read), file) != static_cast<size_t>(read)) {
            ok = false;
            break;
        }
        received += read;
        if (static_cast<uint64_t>(received) > max_bytes) {
            ok = false;
            break;
        }
        int percent = progress_start;
        if (total > 0) {
            percent += static_cast<int>((received * progress_span) / total);
        } else {
            percent += std::min(progress_span - 1, static_cast<int>(received / (64 * 1024)));
        }
        percent = std::clamp(percent, progress_start, progress_start + progress_span);
        if (percent != last_percent) {
            last_percent = percent;
            updateAsyncProgress(phase, percent, received, total, true);
        }
    }
    const int close_result = std::fclose(file);
    heap_caps_free(buffer);
    esp_http_client_cleanup(client);
    if (!ok || !complete || close_result != 0 || received <= 0 || (total > 0 && received != total)) {
        std::remove(temp_path);
        updateAsyncStatus("Media transfer interrupted");
        return false;
    }
    updateAsyncProgress(phase, progress_start + progress_span, received, total, true);
    return true;
}

void GalleryApp::listTask(void *arg)
{
    auto *app = static_cast<GalleryApp *>(arg);
    if (app == nullptr) vTaskDeleteWithCaps(nullptr);
    if (watch::storage_sd_ensure_standard_dirs() != ESP_OK) {
        app->updateAsyncStatus("SD card not mounted; local media unavailable");
    }

    CloudItem items[MAX_CLOUD_ITEMS] = {};
    size_t count = 0;
    app->updateAsyncStatus("Connecting WiFi...");
    const bool wifi_ready = wait_for_wifi();
    if (wifi_ready) app->updateAsyncStatus("Signing into cloud...");
    std::string token = wifi_ready ? login_token() : std::string();
    bool photos_ok = false;
    bool videos_ok = false;
    if (!token.empty()) {
        app->updateAsyncStatus("Loading cloud photos...");
        HttpResponse photos;
        if (http_request(HTTP_METHOD_GET, PHOTOS_URL, token.c_str(), nullptr, photos)) {
            photos_ok = true;
            cJSON *root = cJSON_Parse(photos.body.c_str());
            cJSON *array = root ? cJSON_GetObjectItemCaseSensitive(root, "photos") : nullptr;
            cJSON *item = nullptr;
            cJSON_ArrayForEach(item, array) {
                if (count >= MAX_CLOUD_ITEMS / 2 || !cJSON_IsObject(item)) continue;
                CloudItem &out = items[count++];
                out.id = json_int(item, "id", 0);
                out.video = false;
                out.width = static_cast<uint16_t>(std::clamp(json_size(item, "width", 320), size_t{16}, MAX_PHOTO_DIMENSION));
                out.height = static_cast<uint16_t>(std::clamp(json_size(item, "height", 320), size_t{16}, MAX_PHOTO_DIMENSION));
                out.size_bytes = json_size(item, "device_download_size_bytes", out.width * out.height * 2);
                copy_text(out.title, sizeof(out.title), json_string(item, "title", "Cloud photo").c_str());
                std::string photo_url = json_string(item, "device_download_url");
                if (photo_url.empty()) photo_url = json_string(item, "rgb565_url");
                photo_url = absolute_url(photo_url);
                copy_text(out.download_url, sizeof(out.download_url), photo_url.c_str());
            }
            if (root) cJSON_Delete(root);
        }
        app->updateAsyncStatus("Loading cloud videos...");
        HttpResponse videos;
        if (http_request(HTTP_METHOD_GET, VIDEOS_URL, token.c_str(), nullptr, videos)) {
            videos_ok = true;
            cJSON *root = cJSON_Parse(videos.body.c_str());
            cJSON *array = root ? cJSON_GetObjectItemCaseSensitive(root, "videos") : nullptr;
            cJSON *item = nullptr;
            cJSON_ArrayForEach(item, array) {
                if (count >= MAX_CLOUD_ITEMS || !cJSON_IsObject(item)) continue;
                CloudItem &out = items[count++];
                out.id = json_int(item, "id", 0);
                out.video = true;
                out.width = static_cast<uint16_t>(std::clamp(json_size(item, "esp32_frames_width", 128), size_t{16}, MAX_VIDEO_DIMENSION));
                out.height = static_cast<uint16_t>(std::clamp(json_size(item, "esp32_frames_height", 128), size_t{16}, MAX_VIDEO_DIMENSION));
                out.fps = static_cast<uint8_t>(std::max<size_t>(1, json_size(item, "esp32_frames_fps", 6)));
                out.size_bytes = json_size(item, "device_download_size_bytes", 0);
                out.audio_size_bytes = json_size(item, "audio_download_size_bytes", 0);
                copy_text(out.title, sizeof(out.title), json_string(item, "title", "Cloud video").c_str());
                copy_text(out.download_url, sizeof(out.download_url),
                          absolute_url(json_string(item, "device_download_url")).c_str());
                copy_text(out.audio_download_url, sizeof(out.audio_download_url),
                          absolute_url(json_string(item, "audio_download_url")).c_str());
            }
            if (root) cJSON_Delete(root);
        }
    }

    if (app->_mutex != nullptr && xSemaphoreTake(app->_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        std::memcpy(app->_cloud_items, items, sizeof(items));
        app->_cloud_item_count = count;
        app->_items_dirty = true;
        char status[160] = {};
        if (!wifi_ready) std::snprintf(status, sizeof(status), "WiFi not connected; local media only");
        else if (token.empty()) std::snprintf(status, sizeof(status), "Cloud login failed; check server connection");
        else if (!photos_ok && !videos_ok) std::snprintf(status, sizeof(status), "Cloud server unavailable; local media only");
        else std::snprintf(status, sizeof(status), "Cloud media: %u item(s)", static_cast<unsigned>(count));
        copy_text(app->_status_text, sizeof(app->_status_text), status);
        app->_dirty = true;
        xSemaphoreGive(app->_mutex);
    }
    app->_list_task = nullptr;
    vTaskDeleteWithCaps(nullptr);
}

void GalleryApp::startCloudAction(uint8_t index, CloudAction action)
{
    if (_download_task != nullptr || index >= _cloud_item_count) return;
    _download_item = _cloud_items[index];
    _download_action = action;
    updateAsyncProgress(action == CloudAction::Open ? "Preparing preview..." : "Preparing save...", 0, 0, 0, true);
    if (xTaskCreateWithCaps(downloadTask, "gallery_download", TASK_STACK, this, 1, &_download_task,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        _download_task = nullptr;
        updateAsyncStatus("Could not start download");
        if (_mutex != nullptr && xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            _progress_active = false;
            _dirty = true;
            xSemaphoreGive(_mutex);
        }
    }
}

void GalleryApp::downloadTask(void *arg)
{
    auto *app = static_cast<GalleryApp *>(arg);
    if (app == nullptr) {
        vTaskDeleteWithCaps(nullptr);
        return;
    }
    CloudItem item = app->_download_item;
    bool ready = true;
    if (!wait_for_wifi()) {
        app->updateAsyncStatus("WiFi is not connected");
        ready = false;
    }
    if (ready && watch::storage_sd_ensure_standard_dirs() != ESP_OK) {
        app->updateAsyncStatus("SD card is not ready");
        ready = false;
    }
    std::string token = ready ? login_token() : std::string();
    if (ready && token.empty()) {
        app->updateAsyncStatus("Cloud login failed");
        ready = false;
    }

    char path[180] = {};
    char audio_path[180] = {};
    char temp[200] = {};
    char audio_temp[200] = {};
    std::string url = item.download_url;
    if (ready && item.video && url.empty()) {
        char transcode_url[220] = {};
        std::snprintf(transcode_url, sizeof(transcode_url), WATCH_PRIVATE_DUDUSERVER_API_BASE_URL "/videos/%d/transcode", item.id);
        HttpResponse response;
        if (!http_request(HTTP_METHOD_POST, transcode_url, token.c_str(), "{}", response)) {
            app->updateAsyncStatus("Video conversion failed");
            ready = false;
        }
        if (ready) {
            cJSON *root = cJSON_Parse(response.body.c_str());
            if (root != nullptr) {
                url = absolute_url(json_string(root, "device_download_url"));
                copy_text(item.audio_download_url, sizeof(item.audio_download_url),
                          absolute_url(json_string(root, "audio_download_url")).c_str());
                cJSON_Delete(root);
            }
        }
    }
    if (ready && url.empty()) {
        app->updateAsyncStatus("Media URL unavailable");
        ready = false;
    }

    bool video_saved = false;
    bool audio_saved = false;
    if (ready) {
        if (item.video) {
            std::snprintf(path, sizeof(path), "%s/cloud_video_%d_%ux%u.rgb565v", VIDEO_DIR, item.id,
                          static_cast<unsigned>(item.width), static_cast<unsigned>(item.height));
        } else {
            std::snprintf(path, sizeof(path), "%s/cloud_photo_%d.rgb565", PHOTO_DIR, item.id);
        }
        std::snprintf(temp, sizeof(temp), "%s.tmp", path);
        const int visual_span = item.video && item.audio_download_url[0] != '\0' ? 70 : 100;
        const bool media_downloaded = app->downloadRemoteFile(
            url, token, temp, item.video ? "Downloading video" : "Downloading image", 0, visual_span,
            item.video ? MAX_VIDEO_BYTES : MAX_IMAGE_BYTES);
        if (!media_downloaded) {
            app->updateAsyncStatus(item.video ? "Video download failed" : "Image download failed");
        } else {
            video_saved = commit_temp_file(temp, path);
            if (!video_saved) {
                std::remove(temp);
                app->updateAsyncStatus("Saving media failed");
            }
        }

        if (video_saved && item.video && item.audio_download_url[0] != '\0') {
            std::snprintf(audio_path, sizeof(audio_path), "%s/cloud_video_%d_%ux%u.wav", VIDEO_DIR, item.id,
                          static_cast<unsigned>(item.width), static_cast<unsigned>(item.height));
            std::snprintf(audio_temp, sizeof(audio_temp), "%s.tmp", audio_path);
            const bool audio_downloaded = app->downloadRemoteFile(item.audio_download_url, token, audio_temp,
                                                                   "Preparing video audio", visual_span,
                                                                   100 - visual_span, MAX_AUDIO_BYTES);
            if (audio_downloaded) {
                audio_saved = commit_temp_file(audio_temp, audio_path);
            }
            if (!audio_saved) std::remove(audio_temp);
        }

        if (video_saved && app->_download_action == CloudAction::Open) {
            if (app->_mutex != nullptr && xSemaphoreTake(app->_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
                copy_text(app->_preview_path, sizeof(app->_preview_path), path);
                copy_text(app->_preview_audio_path, sizeof(app->_preview_audio_path), audio_saved ? audio_path : "");
                copy_text(app->_preview_title, sizeof(app->_preview_title), item.title);
                app->_preview_video = item.video;
                app->_preview_width = item.width;
                app->_preview_height = item.height;
                app->_preview_fps = item.fps;
                app->_preview_pending = true;
                app->_dirty = true;
                xSemaphoreGive(app->_mutex);
            }
            if (item.video && !audio_saved && item.audio_download_url[0] != '\0') {
                app->updateAsyncStatus("Video ready; audio unavailable");
            } else {
                app->updateAsyncStatus("Media ready");
            }
        } else if (video_saved) {
            if (item.video && !audio_saved && item.audio_download_url[0] != '\0') {
                app->updateAsyncStatus("Video saved; audio unavailable");
            } else {
                app->updateAsyncStatus("Media saved to SD");
            }
        }
    }
    if (app->_mutex != nullptr && xSemaphoreTake(app->_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        app->_progress_active = false;
        if (video_saved) app->_items_dirty = true;
        app->_dirty = true;
        xSemaphoreGive(app->_mutex);
    }
    app->_download_task = nullptr;
    vTaskDeleteWithCaps(nullptr);
}

void GalleryApp::flushAsyncUi()
{
    bool dirty = false;
    bool items_dirty = false;
    bool preview = false;
    char status[sizeof(_status_text)] = {};
    char preview_path[sizeof(_preview_path)] = {};
    char preview_audio_path[sizeof(_preview_audio_path)] = {};
    char preview_title[sizeof(_preview_title)] = {};
    bool preview_video = false;
    uint16_t width = 128, height = 128;
    uint8_t fps = 6;
    int progress_percent = 0;
    bool progress_active = false;
    if (_mutex != nullptr && xSemaphoreTake(_mutex, 0) == pdTRUE) {
        dirty = _dirty;
        items_dirty = _items_dirty;
        preview = _preview_pending;
        copy_text(status, sizeof(status), _status_text);
        copy_text(preview_path, sizeof(preview_path), _preview_path);
        copy_text(preview_audio_path, sizeof(preview_audio_path), _preview_audio_path);
        copy_text(preview_title, sizeof(preview_title), _preview_title);
        preview_video = _preview_video;
        width = _preview_width;
        height = _preview_height;
        fps = _preview_fps;
        progress_percent = _progress_percent;
        progress_active = _progress_active;
        _dirty = false;
        _items_dirty = false;
        _preview_pending = false;
        xSemaphoreGive(_mutex);
    }
    if (dirty && _status_label != nullptr) set_label(_status_label, status, &lv_font_montserrat_14);
    if (_progress_bar != nullptr) {
        lv_bar_set_value(_progress_bar, progress_percent, LV_ANIM_OFF);
        if (progress_active) lv_obj_clear_flag(_progress_bar, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(_progress_bar, LV_OBJ_FLAG_HIDDEN);
    }
    if (items_dirty) rebuildMediaList();
    if (preview && file_exists(preview_path)) {
        showMedia(preview_path, preview_video, width, height, fps, preview_title, preview_audio_path);
    }
}

void GalleryApp::rebuildMediaList()
{
    if (_media_list == nullptr) return;
    lv_obj_clean(_media_list);
    if (_view == View::Photos || _view == View::Videos) {
        rebuildLocalMediaList(_view == View::Videos);
        return;
    }
    bool video = _view == View::CloudVideos;
    size_t shown = 0;
    for (size_t i = 0; i < _cloud_item_count; ++i) {
        if (_cloud_items[i].video != video) continue;
        CloudItem &item = _cloud_items[i];
        lv_obj_t *row = lv_obj_create(_media_list);
        lv_obj_remove_style_all(row);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 70);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x15171D), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, 14, 0);
        lv_obj_set_style_pad_left(row, 10, 0);
        lv_obj_set_style_pad_right(row, 8, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_t *title = label(row, item.title, &lv_font_montserrat_14, 0xFFFFFF);
        lv_obj_set_width(title, 0);
        lv_obj_set_flex_grow(title, 1);
        lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
        const uint8_t action_index = static_cast<uint8_t>(i);
        _action_bindings[i * 2] = {this, action_index, CloudAction::Open};
        _action_bindings[i * 2 + 1] = {this, action_index, CloudAction::Save};
        lv_obj_t *actions = lv_obj_create(row);
        lv_obj_remove_style_all(actions);
        lv_obj_set_size(actions, 118, 34);
        lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(actions, 6, 0);
        lv_obj_remove_flag(actions, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *open = lv_button_create(actions);
        style_button(open, 0x1B6BFF, 56, 32);
        lv_obj_add_event_cb(open, onCloudActionClicked, LV_EVENT_CLICKED, &_action_bindings[i * 2]);
        lv_obj_center(label(open, video ? "PLAY" : "VIEW", &lv_font_montserrat_10, 0xFFFFFF));
        lv_obj_t *save = lv_button_create(actions);
        style_button(save, 0x293062, 56, 32);
        lv_obj_add_event_cb(save, onCloudActionClicked, LV_EVENT_CLICKED, &_action_bindings[i * 2 + 1]);
        lv_obj_center(label(save, "SAVE", &lv_font_montserrat_10, 0xFFFFFF));
        ++shown;
    }
    if (shown == 0) {
        _list_label = label(_media_list, video ? "No cloud videos" : "No cloud photos", &lv_font_montserrat_14, 0xD7DCE5);
        lv_obj_set_width(_list_label, LV_PCT(100));
    }
}

void GalleryApp::rebuildLocalMediaList(bool video)
{
    _local_item_count = 0;
    const char *directory = video ? VIDEO_DIR : PHOTO_DIR;
    DIR *dir = opendir(directory);
    if (dir != nullptr) {
        while (_local_item_count < MAX_LOCAL_ITEMS) {
            dirent *entry = readdir(dir);
            if (entry == nullptr) break;
            if (entry->d_name[0] == '.') continue;
            if (video ? !has_extension(entry->d_name, ".rgb565v") : !has_extension(entry->d_name, ".rgb565")) {
                continue;
            }

            char path[180] = {};
            const int path_length = std::snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
            if (path_length <= 0 || static_cast<size_t>(path_length) >= sizeof(path)) continue;
            struct stat st = {};
            if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0) continue;

            uint16_t width = 0;
            uint16_t height = 0;
            const size_t bytes = static_cast<size_t>(st.st_size);
            if (video ? !raw_video_dimensions(entry->d_name, bytes, width, height)
                      : !raw_image_dimensions(bytes, width, height)) {
                continue;
            }

            LocalItem &item = _local_items[_local_item_count++];
            item = {};
            item.video = video;
            item.width = width;
            item.height = height;
            item.fps = 6;
            copy_text(item.path, sizeof(item.path), path);
            std::snprintf(item.title, sizeof(item.title), "%.79s", entry->d_name);
            if (video) build_local_audio_path(item.path, item.audio_path, sizeof(item.audio_path));
        }
        closedir(dir);
    }

    if (_local_item_count == 0) {
        _list_label = label(_media_list, video ? "No local videos" : "No local photos", &lv_font_montserrat_14, 0xD7DCE5);
        lv_obj_set_width(_list_label, LV_PCT(100));
        return;
    }

    const bool selection_active = video ? _local_video_select_mode : _local_photo_select_mode;
    {
        lv_obj_t *selection_row = lv_obj_create(_media_list);
        lv_obj_remove_style_all(selection_row);
        lv_obj_set_width(selection_row, LV_PCT(100));
        lv_obj_set_height(selection_row, 38);
        lv_obj_set_flex_flow(selection_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(selection_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *select_button = lv_button_create(selection_row);
        style_button(select_button, selection_active ? 0x293062 : 0x1B6BFF, 96, 32);
        lv_obj_add_event_cb(select_button, onLocalSelectModeClicked, LV_EVENT_CLICKED, this);
        lv_obj_center(label(select_button, selection_active ? "Cancel" : "Select", &lv_font_montserrat_12, 0xFFFFFF));

        if (selection_active) {
            const size_t selected = selectedLocalMediaCount(video);
            char delete_text[32] = {};
            std::snprintf(delete_text, sizeof(delete_text), "Delete %u", static_cast<unsigned>(selected));
            lv_obj_t *delete_button = lv_button_create(selection_row);
            style_button(delete_button, selected > 0 ? 0xD9322E : 0x3A3F4A, 104, 32);
            lv_obj_add_event_cb(delete_button, onDeleteSelectedLocalMediaClicked, LV_EVENT_CLICKED, this);
            lv_obj_center(label(delete_button, delete_text, &lv_font_montserrat_12, 0xFFFFFF));
        }
    }

    for (size_t i = 0; i < _local_item_count; ++i) {
        LocalItem &item = _local_items[i];
        lv_obj_t *row = lv_obj_create(_media_list);
        lv_obj_remove_style_all(row);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, selection_active ? 74 : 70);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x15171D), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, 14, 0);
        lv_obj_set_style_pad_left(row, 10, 0);
        lv_obj_set_style_pad_right(row, 8, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_t *title = label(row, item.title, &lv_font_montserrat_14, 0xFFFFFF);
        lv_obj_set_width(title, 0);
        lv_obj_set_flex_grow(title, 1);
        lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);

        const uint8_t index = static_cast<uint8_t>(i);
        _local_action_bindings[i] = {this, index};
        if (selection_active) {
            lv_obj_t *selected = lv_checkbox_create(row);
            lv_checkbox_set_text(selected, "");
            lv_obj_set_size(selected, 32, 32);
            const uint32_t selection = video ? _local_video_selection : _local_photo_selection;
            if ((selection & (uint32_t{1} << index)) != 0) {
                lv_obj_add_state(selected, LV_STATE_CHECKED);
            }
            lv_obj_add_event_cb(selected, onLocalSelectionChanged, LV_EVENT_VALUE_CHANGED, &_local_action_bindings[i]);
        }
        lv_obj_t *open = lv_button_create(row);
        style_button(open, 0x1B6BFF, 64, 32);
        lv_obj_add_event_cb(open, onLocalOpenClicked, LV_EVENT_CLICKED, &_local_action_bindings[i]);
        lv_obj_center(label(open, video ? "PLAY" : "VIEW", &lv_font_montserrat_10, 0xFFFFFF));
    }
}

void GalleryApp::openLocalMedia(uint8_t index)
{
    if (index >= _local_item_count) return;
    const LocalItem &item = _local_items[index];
    if (!file_exists(item.path)) {
        updateAsyncStatus("Media file is no longer available");
        _items_dirty = true;
        return;
    }
    showMedia(item.path, item.video, item.width, item.height, item.fps, item.title,
              item.video && file_exists(item.audio_path) ? item.audio_path : nullptr);
}

size_t GalleryApp::selectedLocalMediaCount(bool video) const
{
    size_t count = 0;
    const uint32_t selection = video ? _local_video_selection : _local_photo_selection;
    for (size_t i = 0; i < _local_item_count; ++i) {
        if ((selection & (uint32_t{1} << i)) != 0 && _local_items[i].video == video) {
            ++count;
        }
    }
    return count;
}

void GalleryApp::toggleLocalSelectMode(bool video)
{
    bool &select_mode = video ? _local_video_select_mode : _local_photo_select_mode;
    uint32_t &selection = video ? _local_video_selection : _local_photo_selection;
    select_mode = !select_mode;
    if (!select_mode) {
        selection = 0;
    }
    _items_dirty = true;
    _dirty = true;
}

void GalleryApp::toggleLocalSelection(uint8_t index, bool video)
{
    const bool select_mode = video ? _local_video_select_mode : _local_photo_select_mode;
    if (!select_mode || index >= _local_item_count || _local_items[index].video != video) return;
    uint32_t &selection = video ? _local_video_selection : _local_photo_selection;
    selection ^= uint32_t{1} << index;
    _items_dirty = true;
    _dirty = true;
}

void GalleryApp::showDeleteSelectedLocalMediaConfirm(bool video)
{
    const size_t selected = selectedLocalMediaCount(video);
    if (selected == 0) {
        updateAsyncStatus(video ? "Select local videos to delete" : "Select local photos to delete");
        return;
    }
    if (_delete_confirm != nullptr) return;

    _delete_confirm = lv_msgbox_create(nullptr);
    if (_delete_confirm == nullptr) {
        updateAsyncStatus("Could not open delete confirmation");
        return;
    }
    lv_obj_set_width(_delete_confirm, 320);
    lv_msgbox_add_title(_delete_confirm, video ? "Delete local videos?" : "Delete local photos?");
    if (video) {
        lv_msgbox_add_text_fmt(_delete_confirm, "Delete %u selected video(s) and their local audio files?", static_cast<unsigned>(selected));
    } else {
        lv_msgbox_add_text_fmt(_delete_confirm, "Delete %u selected photo(s)?", static_cast<unsigned>(selected));
    }
    _delete_confirm_bindings[0] = {this, false, video};
    _delete_confirm_bindings[1] = {this, true, video};
    lv_obj_t *cancel = lv_msgbox_add_footer_button(_delete_confirm, "Cancel");
    lv_obj_t *confirm = lv_msgbox_add_footer_button(_delete_confirm, "Delete");
    if (cancel != nullptr) {
        lv_obj_add_event_cb(cancel, onDeleteConfirmClicked, LV_EVENT_CLICKED, &_delete_confirm_bindings[0]);
    }
    if (confirm != nullptr) {
        lv_obj_add_event_cb(confirm, onDeleteConfirmClicked, LV_EVENT_CLICKED, &_delete_confirm_bindings[1]);
    }
    lv_obj_center(_delete_confirm);
}

void GalleryApp::deleteSelectedLocalMedia(bool video)
{
    const char *directory = video ? VIDEO_DIR : PHOTO_DIR;
    const size_t directory_length = std::strlen(directory);
    const uint32_t selection = video ? _local_video_selection : _local_photo_selection;
    size_t removed = 0;
    size_t failed = 0;
    for (size_t i = 0; i < _local_item_count; ++i) {
        if ((selection & (uint32_t{1} << i)) == 0) continue;
        const LocalItem &item = _local_items[i];
        const bool valid_path = item.video == video &&
                                std::strncmp(item.path, directory, directory_length) == 0 &&
                                item.path[directory_length] == '/';
        if (!valid_path || std::remove(item.path) != 0) {
            ++failed;
            continue;
        }
        if (video && item.audio_path[0] != '\0') {
            std::remove(item.audio_path);
        }
        ++removed;
    }
    if (video) {
        _local_video_selection = 0;
        _local_video_select_mode = false;
    } else {
        _local_photo_selection = 0;
        _local_photo_select_mode = false;
    }
    _items_dirty = true;
    _dirty = true;

    char status[96] = {};
    if (failed == 0) {
        std::snprintf(status, sizeof(status), "Deleted %u local %s(s)", static_cast<unsigned>(removed), video ? "video" : "photo");
    } else {
        std::snprintf(status, sizeof(status), "Deleted %u; failed %u", static_cast<unsigned>(removed), static_cast<unsigned>(failed));
    }
    updateAsyncStatus(status);
}

bool GalleryApp::ensureFrameBuffer(size_t bytes)
{
    if (_frame_buffer != nullptr && _frame_capacity >= bytes) return true;
    if (_frame_buffer != nullptr) heap_caps_free(_frame_buffer);
    _frame_buffer = static_cast<uint8_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    _frame_capacity = _frame_buffer != nullptr ? bytes : 0;
    return _frame_buffer != nullptr;
}

void GalleryApp::releaseVideoCache()
{
    if (_video_cache != nullptr) {
        heap_caps_free(_video_cache);
        _video_cache = nullptr;
    }
    _video_cache_size = 0;
}

bool GalleryApp::cacheVideoFrames(size_t bytes)
{
    if (_video_file == nullptr || bytes == 0 || bytes > MAX_VIDEO_CACHE_BYTES) {
        return false;
    }
    auto *cache = static_cast<uint8_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (cache == nullptr) {
        return false;
    }
    std::rewind(_video_file);
    if (std::fread(cache, 1, bytes, _video_file) != bytes) {
        heap_caps_free(cache);
        std::rewind(_video_file);
        return false;
    }
    std::fclose(_video_file);
    _video_file = nullptr;
    _video_cache = cache;
    _video_cache_size = bytes;
    return true;
}

void GalleryApp::showMedia(const char *path, bool video, uint16_t width, uint16_t height, uint8_t fps, const char *title,
                           const char *audio_path)
{
    if (_viewer != nullptr) {
        closeViewer();
    }
    if (!ensureFrameBuffer(static_cast<size_t>(width) * height * 2)) {
        updateAsyncStatus("Not enough PSRAM for media");
        return;
    }
    _viewer = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(_viewer);
    lv_obj_set_size(_viewer, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(_viewer, lv_color_hex(0x07080B), 0);
    lv_obj_set_style_bg_opa(_viewer, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(_viewer, 12, 0);
    lv_obj_set_style_pad_right(_viewer, 12, 0);
    lv_obj_set_style_pad_top(_viewer, 30, 0);
    lv_obj_set_style_pad_bottom(_viewer, 28, 0);
    lv_obj_set_flex_flow(_viewer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(_viewer, 8, 0);
    lv_obj_remove_flag(_viewer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *top = lv_obj_create(_viewer);
    lv_obj_remove_style_all(top);
    lv_obj_set_width(top, LV_PCT(100));
    lv_obj_set_height(top, 38);
    lv_obj_t *top_title = label(top, title != nullptr ? title : "Media", &lv_font_montserrat_16, 0xFFFFFF);
    lv_obj_set_width(top_title, LV_PCT(100));
    lv_obj_set_style_text_align(top_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(top_title, LV_ALIGN_CENTER, 0, 0);
    _viewer_image = lv_image_create(_viewer);
    lv_obj_set_width(_viewer_image, LV_PCT(100));
    lv_obj_set_height(_viewer_image, 0);
    lv_obj_set_flex_grow(_viewer_image, 1);
    lv_image_set_inner_align(_viewer_image, LV_IMAGE_ALIGN_CONTAIN);
    lv_image_set_antialias(_viewer_image, false);
    _viewer_detail = label(_viewer, video ? "Preparing video" : "RGB565 image", &lv_font_montserrat_12, 0x8FA3B8);
    lv_obj_set_width(_viewer_detail, LV_PCT(100));
    lv_obj_set_height(_viewer_detail, 22);
    lv_obj_set_style_text_align(_viewer_detail, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *bottom = lv_obj_create(_viewer);
    lv_obj_remove_style_all(bottom);
    lv_obj_set_width(bottom, LV_PCT(100));
    lv_obj_set_height(bottom, 42);
    lv_obj_t *back = lv_button_create(bottom);
    style_button(back, 0x232833, 132, 42);
    lv_obj_add_event_cb(back, onViewerBackClicked, LV_EVENT_CLICKED, this);
    lv_obj_align(back, LV_ALIGN_CENTER, 0, 0);
    lv_obj_center(label(back, "Back", &lv_font_montserrat_14, 0xFFFFFF));

    lv_obj_update_layout(_viewer);
    const int content_width = std::max(1, static_cast<int>(lv_obj_get_content_width(_viewer_image)));
    const int content_height = std::max(1, static_cast<int>(lv_obj_get_content_height(_viewer_image)));
    const int scale = std::min((content_width * LV_SCALE_NONE) / std::max<int>(1, width),
                               (content_height * LV_SCALE_NONE) / std::max<int>(1, height));
    lv_image_set_scale(_viewer_image, std::clamp(scale, LV_SCALE_NONE, LV_SCALE_NONE * 4));

    _video_width = width;
    _video_height = height;
    _video_fps = std::max<uint8_t>(1, fps);
    _video_audio_path[0] = '\0';
    if (video) {
        _video_file = std::fopen(path, "rb");
        if (_video_file == nullptr) {
            updateAsyncStatus("Open video failed");
            return;
        }
        struct stat st = {};
        const size_t frame_bytes = static_cast<size_t>(width) * height * 2;
        if (stat(path, &st) != 0) {
            std::fclose(_video_file);
            _video_file = nullptr;
            set_label(_viewer_detail, "Video frame package is unavailable", &lv_font_montserrat_12);
            return;
        }
        _video_frame_count = static_cast<unsigned>(st.st_size / frame_bytes);
        if (_video_frame_count == 0) {
            std::fclose(_video_file);
            _video_file = nullptr;
            set_label(_viewer_detail, "Video frame package is incomplete", &lv_font_montserrat_12);
            return;
        }
        _video_frame_index = 0;
        const bool cached = cacheVideoFrames(static_cast<size_t>(_video_frame_count) * frame_bytes);
        _video_timer = lv_timer_create(onVideoTimer, 1000 / _video_fps, this);
        nextVideoFrame();
        if (audio_path != nullptr && audio_path[0] != '\0' && file_exists(audio_path)) {
            copy_text(_video_audio_path, sizeof(_video_audio_path), audio_path);
            if (startVideoAudio()) {
                set_label(_viewer_detail, cached ? "Playing cached video with audio" : "Playing video with audio",
                          &lv_font_montserrat_12);
            } else {
                set_label(_viewer_detail, cached ? "Playing cached video; audio failed" : "Playing video; audio failed",
                          &lv_font_montserrat_12);
            }
        } else {
            set_label(_viewer_detail, cached ? "Playing cached video; no audio track" : "Playing video; no audio track",
                      &lv_font_montserrat_12);
        }
    } else {
        FILE *file = std::fopen(path, "rb");
        const size_t bytes = static_cast<size_t>(width) * height * 2;
        if (file == nullptr || std::fread(_frame_buffer, 1, bytes, file) != bytes) {
            if (file) std::fclose(file);
            updateAsyncStatus("Open image failed");
            return;
        }
        std::fclose(file);
        _frame_image = {};
        _frame_image.header.magic = LV_IMAGE_HEADER_MAGIC;
        _frame_image.header.cf = LV_COLOR_FORMAT_RGB565_SWAPPED;
        _frame_image.header.w = width;
        _frame_image.header.h = height;
        _frame_image.header.stride = width * 2;
        _frame_image.data_size = bytes;
        _frame_image.data = _frame_buffer;
        lv_image_set_src(_viewer_image, &_frame_image);
    }
}

void GalleryApp::closeViewer()
{
    const bool had_viewer = _viewer != nullptr;
    if (had_viewer) watch::audio_music_stop();
    if (_video_timer != nullptr) {
        lv_timer_delete(_video_timer);
        _video_timer = nullptr;
    }
    if (_video_file != nullptr) {
        std::fclose(_video_file);
        _video_file = nullptr;
    }
    releaseVideoCache();
    _viewer = nullptr;
    _viewer_image = nullptr;
    _viewer_detail = nullptr;
    _video_audio_path[0] = '\0';
    if (had_viewer && lv_scr_act() != nullptr) {
        lv_obj_clean(lv_scr_act());
    }
    if (had_viewer) {
        if (_timer != nullptr) {
            lv_timer_delete(_timer);
            _timer = nullptr;
        }
        run();
    }
}

bool GalleryApp::startVideoAudio()
{
    return _video_audio_path[0] != '\0' && file_exists(_video_audio_path) &&
           watch::audio_music_play_file(_video_audio_path) == ESP_OK;
}

void GalleryApp::nextVideoFrame()
{
    if ((_video_file == nullptr && _video_cache == nullptr) || _viewer_image == nullptr || _video_frame_count == 0) return;
    const size_t bytes = static_cast<size_t>(_video_width) * _video_height * 2;
    if (_video_frame_index >= _video_frame_count) {
        _video_frame_index = 0;
        if (_video_file != nullptr) std::rewind(_video_file);
        // Restart exactly when the first frame of the next visual loop is shown.
        startVideoAudio();
    }
    const uint8_t *frame_data = _frame_buffer;
    if (_video_cache != nullptr) {
        const size_t offset = static_cast<size_t>(_video_frame_index) * bytes;
        if (offset + bytes > _video_cache_size) return;
        frame_data = _video_cache + offset;
    } else if (std::fread(_frame_buffer, 1, bytes, _video_file) != bytes) {
        return;
    }
    _frame_image = {};
    _frame_image.header.magic = LV_IMAGE_HEADER_MAGIC;
    _frame_image.header.cf = LV_COLOR_FORMAT_RGB565_SWAPPED;
    _frame_image.header.w = _video_width;
    _frame_image.header.h = _video_height;
    _frame_image.header.stride = _video_width * 2;
    _frame_image.data_size = bytes;
    _frame_image.data = frame_data;
    lv_image_set_src(_viewer_image, &_frame_image);
    lv_obj_invalidate(_viewer_image);
    ++_video_frame_index;
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, GalleryApp, APP_NAME, []()
{
    return std::shared_ptr<GalleryApp>(GalleryApp::requestInstance(), [](GalleryApp *) {});
})

} // namespace esp_brookesia::apps
