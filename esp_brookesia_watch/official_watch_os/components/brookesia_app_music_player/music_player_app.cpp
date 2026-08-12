#include "music_player_app.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <strings.h>
#include <string>
#include <sys/stat.h>

#include "cJSON.h"
#include "esp_brookesia.hpp"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_lib_utils.h"
#include "freertos/idf_additions.h"
#include "lvgl.h"
#include "watch_app_icons.hpp"
#include "watch_connectivity.hpp"
#include "watch_storage.hpp"

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "MusicPlayer"

extern "C" {
LV_IMG_DECLARE(ui_img_pattern_png);
}

namespace esp_brookesia::apps {
namespace {
constexpr const char *APP_NAME = "Music";
constexpr const char *LOGIN_URL = "<PRIVATE_OTA_BASE_URL>/api/v1/auth/login";
constexpr const char *MUSIC_LIST_URL = "<PRIVATE_OTA_BASE_URL>/api/v1/music/list";
constexpr const char *PUBLIC_BASE_URL = "<PRIVATE_PUBLIC_BASE_URL>";
constexpr const char *USER_AGENT = "esp32-s3-watch-music";
constexpr size_t MAX_HTTP_BODY = 20 * 1024;
constexpr size_t MAX_DOWNLOAD_BYTES = 25 * 1024 * 1024;
constexpr uint32_t CLOUD_TASK_STACK_SIZE = 16 * 1024;
constexpr uint32_t LOCAL_PLAY_TASK_STACK_SIZE = 14 * 1024;
constexpr size_t CLOUD_DOWNLOAD_BUFFER_SIZE = 4096;

struct HttpResponse {
    std::string body;
    int status = 0;
    bool overflow = false;
};

lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_opa(label, LV_OPA_COVER, 0);
    return label;
}

lv_obj_t *make_pill(lv_obj_t *parent, const char *text, int width, lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, width, 34);
    lv_obj_set_style_radius(button, 18, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_70, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t *label = make_label(button, text, &lv_font_montserrat_14, 0x293062);
    lv_obj_center(label);
    return button;
}

lv_obj_t *make_row_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, 58, 32);
    lv_obj_set_style_radius(button, 16, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x293062), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t *label = make_label(button, text, &lv_font_montserrat_14, 0xFFFFFF);
    lv_obj_center(label);
    return button;
}

void safe_copy(char *dst, size_t dst_size, const char *src)
{
    if ((dst == nullptr) || (dst_size == 0)) {
        return;
    }
    std::snprintf(dst, dst_size, "%s", src != nullptr ? src : "");
}

std::string json_string(cJSON *object, const char *key, const char *fallback = "")
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsString(item) && (item->valuestring != nullptr)) {
        return item->valuestring;
    }
    return fallback;
}

size_t json_size(cJSON *object, const char *key)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsNumber(item) && (item->valuedouble > 0)) {
        return static_cast<size_t>(item->valuedouble);
    }
    return 0;
}

esp_err_t http_event(esp_http_client_event_t *event)
{
    if ((event == nullptr) || (event->event_id != HTTP_EVENT_ON_DATA) || (event->user_data == nullptr)) {
        return ESP_OK;
    }
    auto *response = static_cast<HttpResponse *>(event->user_data);
    if ((event->data == nullptr) || (event->data_len <= 0) || response->overflow) {
        return ESP_OK;
    }
    if (response->body.size() + static_cast<size_t>(event->data_len) > MAX_HTTP_BODY) {
        response->overflow = true;
        return ESP_OK;
    }
    response->body.append(static_cast<const char *>(event->data), static_cast<size_t>(event->data_len));
    return ESP_OK;
}

bool http_request(esp_http_client_method_t method, const char *url, const char *token, const char *body, HttpResponse &response)
{
    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = 12000;
    config.user_agent = USER_AGENT;
    config.event_handler = http_event;
    config.user_data = &response;
    config.keep_alive_enable = false;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        return false;
    }
    esp_http_client_set_method(client, method);
    if ((token != nullptr) && (token[0] != '\0')) {
        std::string auth = "Bearer ";
        auth += token;
        esp_http_client_set_header(client, "Authorization", auth.c_str());
    }
    if (body != nullptr) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, body, std::strlen(body));
    }

    const esp_err_t err = esp_http_client_perform(client);
    response.status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    return (err == ESP_OK) && !response.overflow;
}

bool audio_file_header_supported(const char *path, char *error, size_t error_size)
{
    if ((path == nullptr) || (path[0] == '\0')) {
        if (error != nullptr && error_size > 0) {
            std::snprintf(error, error_size, "Invalid path");
        }
        return false;
    }
    FILE *fp = std::fopen(path, "rb");
    if (fp == nullptr) {
        if (error != nullptr && error_size > 0) {
            std::snprintf(error, error_size, "Open file failed");
        }
        return false;
    }
    uint8_t header[12] = {};
    const size_t read = std::fread(header, 1, sizeof(header), fp);
    std::fclose(fp);
    if (read < 4) {
        if (error != nullptr && error_size > 0) {
            std::snprintf(error, error_size, "Audio file too small");
        }
        return false;
    }
    const char *dot = std::strrchr(path, '.');
    if ((dot != nullptr) && (strcasecmp(dot, ".wav") == 0)) {
        const bool wav = (read >= 12) &&
                         (std::memcmp(header + 0, "RIFF", 4) == 0) &&
                         (std::memcmp(header + 8, "WAVE", 4) == 0);
        if (!wav && error != nullptr && error_size > 0) {
            std::snprintf(error, error_size, "Invalid WAV header");
        }
        return wav;
    }
    if ((dot != nullptr) && (strcasecmp(dot, ".mp3") == 0)) {
        const bool id3 = (read >= 3) && (std::memcmp(header, "ID3", 3) == 0);
        const bool frame = (read >= 2) && (header[0] == 0xFF) && ((header[1] & 0xE0) == 0xE0);
        if (!(id3 || frame) && error != nullptr && error_size > 0) {
            std::snprintf(error, error_size, "Invalid MP3 header");
        }
        return id3 || frame;
    }
    if (error != nullptr && error_size > 0) {
        std::snprintf(error, error_size, "Only MP3/WAV supported");
    }
    return false;
}

std::string login_token()
{
    HttpResponse response;
    constexpr const char *body = "{\"username\":\"admin\",\"password\":\"<PRIVATE_PASSWORD>\"}";
    if (!http_request(HTTP_METHOD_POST, LOGIN_URL, nullptr, body, response)) {
        return {};
    }
    if (response.status < 200 || response.status >= 300) {
        return {};
    }
    cJSON *root = cJSON_Parse(response.body.c_str());
    if (root == nullptr) {
        return {};
    }
    std::string token = json_string(root, "access_token", "");
    cJSON_Delete(root);
    return token;
}

std::string basename_from_url(const std::string &url)
{
    size_t end = url.find('?');
    if (end == std::string::npos) {
        end = url.size();
    }
    size_t start = url.rfind('/', end == 0 ? 0 : end - 1);
    start = (start == std::string::npos) ? 0 : start + 1;
    if (start >= end) {
        return "cloud-music.mp3";
    }
    return url.substr(start, end - start);
}

std::string normalize_url(const std::string &url)
{
    if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0) {
        return url;
    }
    if (!url.empty() && url[0] == '/') {
        return std::string(PUBLIC_BASE_URL) + url;
    }
    return std::string(PUBLIC_BASE_URL) + "/private OTA backend/" + url;
}

std::string safe_filename(std::string value, const std::string &fallback_url)
{
    if (value.empty()) {
        value = basename_from_url(fallback_url);
    }
    size_t slash = value.find_last_of("/\\");
    if (slash != std::string::npos) {
        value = value.substr(slash + 1);
    }
    std::string out;
    out.reserve(value.size());
    for (unsigned char c: value) {
        if (std::isalnum(c) || c == '.' || c == '_' || c == '-') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('_');
        }
    }
    if (out.empty()) {
        out = "cloud-music.mp3";
    }
    if (out.find('.') == std::string::npos) {
        out += ".mp3";
    }
    return out;
}

std::string human_size(size_t bytes)
{
    char text[32] = {};
    if (bytes >= 1024 * 1024) {
        std::snprintf(text, sizeof(text), "%.1fMB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    } else if (bytes >= 1024) {
        std::snprintf(text, sizeof(text), "%uKB", static_cast<unsigned>(bytes / 1024));
    } else if (bytes > 0) {
        std::snprintf(text, sizeof(text), "%uB", static_cast<unsigned>(bytes));
    } else {
        std::snprintf(text, sizeof(text), "--");
    }
    return text;
}

const char *display_title(const watch::AudioMusicTrack &track)
{
    return track.title[0] ? track.title : track.path;
}
} // namespace

MusicPlayerApp *MusicPlayerApp::_instance = nullptr;

MusicPlayerApp *MusicPlayerApp::requestInstance()
{
    if (_instance == nullptr) {
        _instance = new MusicPlayerApp();
    }
    return _instance;
}

MusicPlayerApp::MusicPlayerApp():
    systems::phone::App(APP_NAME, watch_app_icon_music_48(), true, true, true)
{
}

bool MusicPlayerApp::run(void)
{
    if (_timer != nullptr) {
        lv_timer_delete(_timer);
        _timer = nullptr;
    }
    lv_obj_t *screen = lv_scr_act();
    ESP_UTILS_CHECK_NULL_RETURN(screen, false, "Invalid active screen");
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_image_src(screen, &ui_img_pattern_png, 0);
    lv_obj_set_style_bg_image_tiled(screen, true, 0);

    _root = lv_obj_create(screen);
    ESP_UTILS_CHECK_NULL_RETURN(_root, false, "Create root failed");
    lv_obj_remove_style_all(_root);
    lv_obj_set_size(_root, LV_PCT(100), LV_PCT(100));
    lv_obj_remove_flag(_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(_root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(_root, 14, 0);

    _list_page = lv_obj_create(_root);
    lv_obj_remove_style_all(_list_page);
    lv_obj_set_size(_list_page, LV_PCT(100), LV_PCT(100));
    lv_obj_remove_flag(_list_page, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *top = lv_obj_create(_list_page);
    lv_obj_remove_style_all(top);
    lv_obj_set_size(top, LV_PCT(100), 40);
    lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(top, LV_OBJ_FLAG_SCROLLABLE);

    make_pill(top, "Stop", 76, onStopClicked, this);
    make_pill(top, "Scan", 76, onScanClicked, this);
    _cloud_refresh_button = make_pill(top, "Cloud", 84, onCloudRefreshClicked, this);

    _track_label = make_label(_list_page, "Music", &lv_font_montserrat_22, 0x000746);
    lv_obj_set_width(_track_label, LV_PCT(86));
    lv_label_set_long_mode(_track_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(_track_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(_track_label, LV_ALIGN_TOP_MID, 0, 54);

    _source_label = make_label(_list_page, "/sdcard/music + cloud", &lv_font_montserrat_14, 0x6F73A8);
    lv_obj_set_width(_source_label, LV_PCT(86));
    lv_obj_set_style_text_align(_source_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(_source_label, LV_ALIGN_TOP_MID, 0, 82);

    _list = lv_obj_create(_list_page);
    lv_obj_remove_style_all(_list);
    lv_obj_set_size(_list, LV_PCT(92), 292);
    lv_obj_align(_list, LV_ALIGN_TOP_MID, 0, 110);
    lv_obj_set_flex_flow(_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(_list, 8, 0);
    lv_obj_set_scrollbar_mode(_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(_list, LV_OBJ_FLAG_SCROLLABLE);

    _status_label = make_label(_list_page, "Scanning TF card...", &lv_font_montserrat_14, 0x6F73A8);
    lv_obj_set_width(_status_label, LV_PCT(86));
    lv_label_set_long_mode(_status_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(_status_label, LV_ALIGN_BOTTOM_MID, 0, -20);

    _player_page = lv_obj_create(_root);
    lv_obj_remove_style_all(_player_page);
    lv_obj_set_size(_player_page, LV_PCT(100), LV_PCT(100));
    lv_obj_remove_flag(_player_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(_player_page, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *player_top = lv_obj_create(_player_page);
    lv_obj_remove_style_all(player_top);
    lv_obj_set_size(player_top, LV_PCT(100), 42);
    lv_obj_align(player_top, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_set_flex_flow(player_top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(player_top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(player_top, LV_OBJ_FLAG_SCROLLABLE);
    make_pill(player_top, "List", 76, onListClicked, this);
    _player_download_button = make_pill(player_top, "DL", 70, onPlayerDownloadClicked, this);
    _player_download_label = lv_obj_get_child(_player_download_button, 0);
    make_pill(player_top, "Stop", 76, onStopClicked, this);

    _player_title_label = make_label(_player_page, "Select music", &lv_font_montserrat_28, 0x000746);
    lv_obj_set_width(_player_title_label, LV_PCT(86));
    lv_label_set_long_mode(_player_title_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(_player_title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(_player_title_label, LV_ALIGN_TOP_MID, 0, 78);

    _player_source_label = make_label(_player_page, "Local / Cloud", &lv_font_montserrat_16, 0x6F73A8);
    lv_obj_set_width(_player_source_label, LV_PCT(86));
    lv_label_set_long_mode(_player_source_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(_player_source_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(_player_source_label, LV_ALIGN_TOP_MID, 0, 116);

    _player_status_label = make_label(_player_page, "Tap Play", &lv_font_montserrat_14, 0x6F73A8);
    lv_obj_set_width(_player_status_label, LV_PCT(86));
    lv_label_set_long_mode(_player_status_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(_player_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(_player_status_label, LV_ALIGN_TOP_MID, 0, 146);

    lv_obj_t *controls = lv_obj_create(_player_page);
    lv_obj_remove_style_all(controls);
    lv_obj_set_size(controls, LV_PCT(86), 88);
    lv_obj_align(controls, LV_ALIGN_CENTER, 0, 38);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(controls, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(controls, LV_OBJ_FLAG_SCROLLABLE);
    make_pill(controls, "Prev", 78, onPrevClicked, this);
    _play_button = make_pill(controls, "Play", 92, onPlayClicked, this);
    _play_label = lv_obj_get_child(_play_button, 0);
    make_pill(controls, "Next", 78, onNextClicked, this);

    _volume_label = make_label(_player_page, "--%", &lv_font_montserrat_14, 0x6F73A8);
    lv_obj_align(_volume_label, LV_ALIGN_BOTTOM_MID, 0, -34);

    _volume_slider = lv_slider_create(_player_page);
    lv_obj_set_width(_volume_slider, LV_PCT(58));
    lv_obj_set_height(_volume_slider, 8);
    lv_obj_align(_volume_slider, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_slider_set_range(_volume_slider, 0, 100);
    lv_slider_set_value(_volume_slider, watch::audio_get_volume(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(_volume_slider, lv_color_hex(0xD7DAF5), LV_PART_MAIN);
    lv_obj_set_style_bg_color(_volume_slider, lv_color_hex(0x293062), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(_volume_slider, lv_color_hex(0x293062), LV_PART_KNOB);
    lv_obj_add_event_cb(_volume_slider, onVolumeChanged, LV_EVENT_VALUE_CHANGED, this);

    scanTracks();
    rebuildList();
    refresh();
    startCloudFetch();
    _timer = lv_timer_create(onTimer, 800, this);
    return true;
}

bool MusicPlayerApp::back(void)
{
    if (_view_mode == ViewMode::Player) {
        showListPage();
        return true;
    }
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

bool MusicPlayerApp::close(void)
{
    if (_timer != nullptr) {
        lv_timer_delete(_timer);
        _timer = nullptr;
    }
    _root = nullptr;
    _list_page = nullptr;
    _player_page = nullptr;
    _list = nullptr;
    _track_label = nullptr;
    _status_label = nullptr;
    _source_label = nullptr;
    _volume_label = nullptr;
    _play_label = nullptr;
    _play_button = nullptr;
    _player_title_label = nullptr;
    _player_source_label = nullptr;
    _player_status_label = nullptr;
    _player_download_button = nullptr;
    _player_download_label = nullptr;
    _volume_slider = nullptr;
    _cloud_refresh_button = nullptr;
    return true;
}

void MusicPlayerApp::onBackClicked(lv_event_t *event)
{
    auto *app = static_cast<MusicPlayerApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->back();
}

void MusicPlayerApp::onPlayClicked(lv_event_t *event)
{
    auto *app = static_cast<MusicPlayerApp *>(lv_event_get_user_data(event));
    if (app == nullptr) return;
    if (app->_current_is_cloud &&
        app->_current_cloud_index >= 0 &&
        app->_current_cloud_index < static_cast<int>(app->_cloud_track_count) &&
        !app->cloudTrackDownloaded(app->_cloud_tracks[app->_current_cloud_index])) {
        app->startCloudDownload(app->_current_cloud_index, true);
    } else if (watch::audio_test_is_playing()) {
        watch::audio_test_stop();
    } else if (watch::audio_music_is_paused()) {
        watch::audio_music_resume();
    } else if (watch::audio_music_is_playing()) {
        watch::audio_music_pause();
    } else {
        app->playCurrent();
    }
    app->refresh();
}

void MusicPlayerApp::onStopClicked(lv_event_t *event)
{
    auto *app = static_cast<MusicPlayerApp *>(lv_event_get_user_data(event));
    watch::audio_test_stop();
    watch::audio_music_stop();
    if (app != nullptr) app->refresh();
}

void MusicPlayerApp::onPrevClicked(lv_event_t *event)
{
    auto *app = static_cast<MusicPlayerApp *>(lv_event_get_user_data(event));
    if (app != nullptr) {
        app->selectDelta(-1);
        app->refresh();
    }
}

void MusicPlayerApp::onNextClicked(lv_event_t *event)
{
    auto *app = static_cast<MusicPlayerApp *>(lv_event_get_user_data(event));
    if (app != nullptr) {
        app->selectDelta(1);
        app->refresh();
    }
}

void MusicPlayerApp::onScanClicked(lv_event_t *event)
{
    auto *app = static_cast<MusicPlayerApp *>(lv_event_get_user_data(event));
    if (app != nullptr) {
        app->scanTracks();
        app->rebuildList();
        app->refresh();
    }
}

void MusicPlayerApp::onLocalPlayClicked(lv_event_t *event)
{
    auto *action = static_cast<RowAction *>(lv_event_get_user_data(event));
    if ((action == nullptr) || (action->app == nullptr)) {
        return;
    }
    action->app->selectLocalTrack(action->index);
    action->app->refresh();
}

void MusicPlayerApp::onCloudActionClicked(lv_event_t *event)
{
    auto *action = static_cast<RowAction *>(lv_event_get_user_data(event));
    if ((action == nullptr) || (action->app == nullptr)) {
        return;
    }
    action->app->selectCloudTrack(action->index);
}

void MusicPlayerApp::onCloudRefreshClicked(lv_event_t *event)
{
    auto *app = static_cast<MusicPlayerApp *>(lv_event_get_user_data(event));
    if (app != nullptr) {
        app->startCloudFetch();
    }
}

void MusicPlayerApp::onListClicked(lv_event_t *event)
{
    auto *app = static_cast<MusicPlayerApp *>(lv_event_get_user_data(event));
    if (app != nullptr) {
        app->showListPage();
    }
}

void MusicPlayerApp::onPlayerDownloadClicked(lv_event_t *event)
{
    auto *app = static_cast<MusicPlayerApp *>(lv_event_get_user_data(event));
    if (app == nullptr) {
        return;
    }
    if (app->_current_is_cloud &&
        app->_current_cloud_index >= 0 &&
        app->_current_cloud_index < static_cast<int>(app->_cloud_track_count)) {
        app->startCloudDownload(app->_current_cloud_index, true);
    }
}

void MusicPlayerApp::onVolumeChanged(lv_event_t *event)
{
    auto *app = static_cast<MusicPlayerApp *>(lv_event_get_user_data(event));
    lv_obj_t *slider = static_cast<lv_obj_t *>(lv_event_get_target(event));
    if ((app == nullptr) || (slider == nullptr)) return;
    watch::audio_set_volume(static_cast<int>(lv_slider_get_value(slider)));
    app->refresh();
}

void MusicPlayerApp::onTimer(lv_timer_t *timer)
{
    auto *app = static_cast<MusicPlayerApp *>(lv_timer_get_user_data(timer));
    if (app != nullptr) {
        app->flushCloudState();
        app->refresh();
    }
}

void MusicPlayerApp::cloudTaskEntry(void *arg)
{
    auto *app = static_cast<MusicPlayerApp *>(arg);
    if (app != nullptr) {
        if (app->_cloud_operation == CloudOperation::DownloadTrack) {
            app->runCloudDownload(app->_cloud_task_index);
        } else {
            app->runCloudFetch();
        }
        app->_cloud_task = nullptr;
    }
    vTaskDeleteWithCaps(nullptr);
}

void MusicPlayerApp::playTaskEntry(void *arg)
{
    auto *app = static_cast<MusicPlayerApp *>(arg);
    if (app != nullptr) {
        app->runLocalPlay();
        app->_play_task = nullptr;
    }
    vTaskDeleteWithCaps(nullptr);
}

void MusicPlayerApp::scanDirAppend(const char *dir)
{
    if ((_track_count >= watch::AUDIO_MUSIC_MAX_TRACKS) || (dir == nullptr)) {
        return;
    }
    size_t found = 0;
    esp_err_t err = watch::audio_music_scan(dir, &_tracks[_track_count], watch::AUDIO_MUSIC_MAX_TRACKS - _track_count, &found);
    if (err == ESP_OK) {
        _track_count += found;
    } else {
        char log[160] = {};
        std::snprintf(log, sizeof(log), "music scan failed dir=%s err=%s", dir, esp_err_to_name(err));
        watch::storage_sd_append_log("music", log);
    }
}

void MusicPlayerApp::scanTracks()
{
    _track_count = 0;
    _current_index = 0;

    esp_err_t sd_ret = watch::storage_sd_ensure_standard_dirs();
    if (sd_ret == ESP_OK) {
        scanDirAppend(watch::AUDIO_MUSIC_DEFAULT_DIR);
    } else {
        char log[128] = {};
        std::snprintf(log, sizeof(log), "music scan skipped: SD not ready err=%s", esp_err_to_name(sd_ret));
        watch::storage_sd_append_log("music", log);
    }

    for (size_t i = 0; i < _cloud_track_count; ++i) {
        _cloud_tracks[i].downloaded = cloudTrackDownloaded(_cloud_tracks[i]);
    }

    if (_track_count > 0) {
        char text[80] = {};
        std::snprintf(text, sizeof(text), "Local ready: %u file(s)", static_cast<unsigned>(_track_count));
        setStatus(text, 0x293062);
        return;
    }

    char text[120] = {};
    if (sd_ret == ESP_OK) {
        std::snprintf(text, sizeof(text), "No MP3/WAV in /sdcard/music");
        watch::storage_sd_append_log("music", "no audio files in /sdcard/music");
    } else {
        std::snprintf(text, sizeof(text), "TF card not ready: %s", esp_err_to_name(sd_ret));
    }
    setStatus(text, 0xD85C5C);
}

void MusicPlayerApp::playCurrent()
{
    playIndex(_current_index);
}

void MusicPlayerApp::playIndex(int index)
{
    startLocalPlay(index);
}

void MusicPlayerApp::selectLocalTrack(int index)
{
    if ((index < 0) || (index >= static_cast<int>(_track_count))) {
        setStatus("Invalid local track", 0xD85C5C);
        return;
    }
    _current_is_cloud = false;
    _current_cloud_index = -1;
    _current_index = index;
    showPlayerPage();
    startLocalPlay(index);
}

void MusicPlayerApp::selectCloudTrack(int index)
{
    if ((index < 0) || (index >= static_cast<int>(_cloud_track_count))) {
        setStatus("Invalid cloud track", 0xD85C5C);
        return;
    }
    _current_is_cloud = true;
    _current_cloud_index = index;
    showPlayerPage();

    char path[160] = {};
    if (cloudTrackLocalPath(_cloud_tracks[index], path, sizeof(path))) {
        int local_index = findLocalTrackByPath(path);
        if (local_index < 0) {
            scanTracks();
            rebuildList();
            local_index = findLocalTrackByPath(path);
        }
        if (local_index >= 0) {
            _current_index = local_index;
            startLocalPlay(local_index);
            return;
        }
    }
    startCloudDownload(index, true);
}

void MusicPlayerApp::startLocalPlay(int index)
{
    if (_track_count == 0) {
        scanTracks();
        rebuildList();
    }
    if ((_track_count == 0) || (index < 0) || (index >= static_cast<int>(_track_count))) {
        setStatus("No local track", 0xD85C5C);
        return;
    }
    if (_play_task != nullptr) {
        setCloudState(true, 0, "Local play already starting");
        return;
    }
    if (_cloud_running || _cloud_task != nullptr) {
        setCloudState(true, _cloud_percent, "Cloud task running");
        return;
    }
    _current_index = index;
    _play_task_index = index;
    safe_copy(_play_task_path, sizeof(_play_task_path), _tracks[_current_index].path);
    char header_error[64] = {};
    if (!audio_file_header_supported(_play_task_path, header_error, sizeof(header_error))) {
        char text[96] = {};
        std::snprintf(text, sizeof(text), "Cannot play: %s", header_error);
        setStatus(text, 0xD85C5C);
        return;
    }
    setCloudState(true, 0, "Opening local file...");
    if (xTaskCreateWithCaps(
            playTaskEntry,
            "music_local_play",
            LOCAL_PLAY_TASK_STACK_SIZE,
            this,
            4,
            &_play_task,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        ) != pdPASS) {
        _play_task = nullptr;
        char text[144] = {};
        std::snprintf(
            text,
            sizeof(text),
            "Play task failed: int %luK largest %luK",
            static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
            static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024)
        );
        setCloudState(false, 0, text);
    }
}

void MusicPlayerApp::runLocalPlay()
{
    char path[sizeof(_play_task_path)] = {};
    safe_copy(path, sizeof(path), _play_task_path);
    if (path[0] == '\0') {
        setCloudState(false, 0, "No local file");
        return;
    }

    char header_error[64] = {};
    if (!audio_file_header_supported(path, header_error, sizeof(header_error))) {
        char log[192] = {};
        std::snprintf(log, sizeof(log), "play rejected path=%s reason=%s", path, header_error);
        watch::storage_sd_append_log("music", log);
        setCloudState(false, 0, header_error);
        return;
    }

    esp_err_t ret = watch::audio_music_play_file(path);
    if (ret != ESP_OK) {
        char log[192] = {};
        std::snprintf(log, sizeof(log), "play failed path=%s err=%s", path, esp_err_to_name(ret));
        watch::storage_sd_append_log("music", log);
    }
    if (_state_mutex == nullptr) {
        _state_mutex = xSemaphoreCreateMutex();
    }
    if ((_state_mutex == nullptr) || (xSemaphoreTake(_state_mutex, pdMS_TO_TICKS(50)) == pdTRUE)) {
        _list_dirty = true;
        if (_state_mutex != nullptr) {
            xSemaphoreGive(_state_mutex);
        }
    }
    setCloudState(false, (ret == ESP_OK) ? 100 : 0, (ret == ESP_OK) ? "Playing local file" : "Play failed");
}

void MusicPlayerApp::selectDelta(int delta)
{
    if (_track_count == 0) {
        scanTracks();
        rebuildList();
        return;
    }
    _current_index += delta;
    if (_current_index < 0) {
        _current_index = static_cast<int>(_track_count) - 1;
    }
    if (_current_index >= static_cast<int>(_track_count)) {
        _current_index = 0;
    }
    if (watch::audio_music_is_playing() || watch::audio_music_is_paused()) {
        playCurrent();
    }
}

void MusicPlayerApp::showListPage()
{
    _view_mode = ViewMode::List;
    if (_list_page != nullptr) {
        lv_obj_clear_flag(_list_page, LV_OBJ_FLAG_HIDDEN);
    }
    if (_player_page != nullptr) {
        lv_obj_add_flag(_player_page, LV_OBJ_FLAG_HIDDEN);
    }
    refresh();
}

void MusicPlayerApp::showPlayerPage()
{
    _view_mode = ViewMode::Player;
    if (_list_page != nullptr) {
        lv_obj_add_flag(_list_page, LV_OBJ_FLAG_HIDDEN);
    }
    if (_player_page != nullptr) {
        lv_obj_clear_flag(_player_page, LV_OBJ_FLAG_HIDDEN);
    }
    refresh();
}

MusicPlayerApp::RowAction *MusicPlayerApp::nextRowAction(int index, bool cloud)
{
    if (_row_action_count >= (sizeof(_row_actions) / sizeof(_row_actions[0]))) {
        return nullptr;
    }
    RowAction *action = &_row_actions[_row_action_count++];
    action->app = this;
    action->index = index;
    action->cloud = cloud;
    return action;
}

void MusicPlayerApp::rebuildList()
{
    if (_list == nullptr) {
        return;
    }
    lv_obj_clean(_list);
    _row_action_count = 0;

    lv_obj_t *local_title = make_label(_list, "Local /sdcard/music", &lv_font_montserrat_16, 0x293062);
    lv_obj_set_width(local_title, LV_PCT(100));

    if (_track_count == 0) {
        lv_obj_t *empty = make_label(_list, "No local MP3/WAV", &lv_font_montserrat_14, 0x8A8FA8);
        lv_obj_set_width(empty, LV_PCT(100));
    }

    for (size_t i = 0; i < _track_count; ++i) {
        lv_obj_t *row = lv_obj_create(_list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), 44);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        char text[140] = {};
        std::snprintf(text, sizeof(text), "%s\n%s", display_title(_tracks[i]), human_size(_tracks[i].size).c_str());
        lv_obj_t *label = make_label(row, text, &lv_font_montserrat_14, 0x101532);
        lv_obj_set_width(label, 220);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);

        RowAction *action = nextRowAction(static_cast<int>(i), false);
        make_row_button(row, "Play", onLocalPlayClicked, action);
    }

    lv_obj_t *cloud_title = make_label(_list, "Cloud music", &lv_font_montserrat_16, 0x293062);
    lv_obj_set_width(cloud_title, LV_PCT(100));
    lv_obj_set_style_pad_top(cloud_title, 8, 0);

    if (_cloud_track_count == 0) {
        lv_obj_t *empty = make_label(_list, "Tap Cloud to load server list", &lv_font_montserrat_14, 0x8A8FA8);
        lv_obj_set_width(empty, LV_PCT(100));
    }

    for (size_t i = 0; i < _cloud_track_count; ++i) {
        lv_obj_t *row = lv_obj_create(_list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), 46);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        const CloudMusicTrack &track = _cloud_tracks[i];
        char text[160] = {};
        std::snprintf(
            text,
            sizeof(text),
            "%s%s%s\n%s %s",
            track.title,
            track.artist[0] ? " - " : "",
            track.artist,
            human_size(track.size).c_str(),
            track.downloaded ? "Saved" : "Cloud"
        );
        lv_obj_t *label = make_label(row, text, &lv_font_montserrat_14, 0x101532);
        lv_obj_set_width(label, 220);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);

        RowAction *action = nextRowAction(static_cast<int>(i), true);
        make_row_button(row, track.downloaded ? "Play" : "DL", onCloudActionClicked, action);
    }
}

void MusicPlayerApp::refresh()
{
    const bool has_track = (_track_count > 0) && (_current_index >= 0) && (_current_index < static_cast<int>(_track_count));
    const bool playing = watch::audio_music_is_playing();
    const bool paused = watch::audio_music_is_paused();
    const bool has_cloud_selection = _current_is_cloud &&
                                     (_current_cloud_index >= 0) &&
                                     (_current_cloud_index < static_cast<int>(_cloud_track_count));
    const bool cloud_downloaded = has_cloud_selection && cloudTrackDownloaded(_cloud_tracks[_current_cloud_index]);
    const char *status_text = nullptr;
    uint32_t status_color = 0x6F73A8;

    if (_track_label != nullptr) {
        lv_label_set_text(_track_label, "Music Library");
    }
    if (_source_label != nullptr) {
        char text[96] = {};
        std::snprintf(
            text,
            sizeof(text),
            "Local %u  Cloud %u",
            static_cast<unsigned>(_track_count),
            static_cast<unsigned>(_cloud_track_count)
        );
        lv_label_set_text(_source_label, text);
    }
    if (_volume_label != nullptr) {
        char text[32] = {};
        std::snprintf(text, sizeof(text), "Volume %d%%", watch::audio_get_volume());
        lv_label_set_text(_volume_label, text);
    }
    if (_volume_slider != nullptr) {
        lv_slider_set_value(_volume_slider, watch::audio_get_volume(), LV_ANIM_OFF);
    }
    if (_cloud_refresh_button != nullptr) {
        _cloud_running ? lv_obj_add_state(_cloud_refresh_button, LV_STATE_DISABLED) : lv_obj_clear_state(_cloud_refresh_button, LV_STATE_DISABLED);
        lv_obj_set_style_opa(_cloud_refresh_button, _cloud_running ? LV_OPA_50 : LV_OPA_COVER, 0);
    }
    if (_cloud_running || _cloud_status[0] != '\0') {
        status_text = _cloud_status;
        status_color = _cloud_running ? 0x293062 : 0x6F73A8;
    } else if (paused) {
        status_text = "Paused";
        status_color = 0x9C9CD9;
    } else if (playing) {
        status_text = "Playing";
        status_color = 0x293062;
    } else if (!has_track && _cloud_track_count == 0) {
        status_text = "Scan local or load cloud list";
        status_color = 0x8A8FA8;
    } else {
        status_text = "Tap Play or DL";
        status_color = 0x6F73A8;
    }
    if (_status_label != nullptr) {
        lv_label_set_text(_status_label, status_text);
        lv_obj_set_style_text_color(_status_label, lv_color_hex(status_color), 0);
    }
    if (_player_title_label != nullptr) {
        if (has_cloud_selection) {
            lv_label_set_text(_player_title_label, _cloud_tracks[_current_cloud_index].title);
        } else if (has_track) {
            lv_label_set_text(_player_title_label, display_title(_tracks[_current_index]));
        } else {
            lv_label_set_text(_player_title_label, "Select music");
        }
    }
    if (_player_source_label != nullptr) {
        char text[128] = {};
        if (has_cloud_selection) {
            std::string size_text;
            if (_cloud_tracks[_current_cloud_index].size > 0) {
                size_text = " · " + human_size(_cloud_tracks[_current_cloud_index].size);
            }
            std::snprintf(
                text,
                sizeof(text),
                "%s%s%s",
                _cloud_tracks[_current_cloud_index].artist[0] ? _cloud_tracks[_current_cloud_index].artist : "Cloud",
                cloud_downloaded ? " · saved" : " · download first",
                size_text.c_str()
            );
        } else if (has_track) {
            std::snprintf(text, sizeof(text), "Local · %s", human_size(_tracks[_current_index].size).c_str());
        } else {
            std::snprintf(text, sizeof(text), "No selected track");
        }
        lv_label_set_text(_player_source_label, text);
    }
    if (_player_status_label != nullptr) {
        lv_label_set_text(_player_status_label, status_text);
        lv_obj_set_style_text_color(_player_status_label, lv_color_hex(status_color), 0);
    }
    if (_play_label != nullptr) {
        if (has_cloud_selection && !cloud_downloaded) {
            lv_label_set_text(_play_label, _cloud_running ? "Wait" : "DL");
        } else if (paused) {
            lv_label_set_text(_play_label, "Resume");
        } else if (playing) {
            lv_label_set_text(_play_label, "Pause");
        } else {
            lv_label_set_text(_play_label, "Play");
        }
    }
    if (_play_button != nullptr) {
        _cloud_running ? lv_obj_add_state(_play_button, LV_STATE_DISABLED) : lv_obj_clear_state(_play_button, LV_STATE_DISABLED);
        lv_obj_set_style_opa(_play_button, _cloud_running ? LV_OPA_50 : LV_OPA_COVER, 0);
    }
    if (_player_download_button != nullptr) {
        if (has_cloud_selection && !cloud_downloaded) {
            lv_obj_clear_flag(_player_download_button, LV_OBJ_FLAG_HIDDEN);
            _cloud_running ? lv_obj_add_state(_player_download_button, LV_STATE_DISABLED) : lv_obj_clear_state(_player_download_button, LV_STATE_DISABLED);
            lv_obj_set_style_opa(_player_download_button, _cloud_running ? LV_OPA_50 : LV_OPA_COVER, 0);
        } else {
            lv_obj_add_flag(_player_download_button, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void MusicPlayerApp::setStatus(const char *text, uint32_t color)
{
    if (_status_label == nullptr) {
        return;
    }
    lv_label_set_text(_status_label, text != nullptr ? text : "");
    lv_obj_set_style_text_color(_status_label, lv_color_hex(color), 0);
}

void MusicPlayerApp::startCloudFetch()
{
    startCloudTask(CloudOperation::FetchList, -1);
}

void MusicPlayerApp::startCloudDownload(int index, bool play_after_download)
{
    if ((index < 0) || (index >= static_cast<int>(_cloud_track_count))) {
        setCloudState(false, 0, "Invalid cloud track");
        return;
    }
    _download_play_after = play_after_download;
    if (_play_task != nullptr) {
        setCloudState(true, _cloud_percent, "Local play task running");
        return;
    }
    if (_cloud_tracks[index].downloaded) {
        char path[160] = {};
        if (cloudTrackLocalPath(_cloud_tracks[index], path, sizeof(path))) {
            int local_index = -1;
            for (size_t i = 0; i < _track_count; ++i) {
                if (std::strcmp(_tracks[i].path, path) == 0) {
                    local_index = static_cast<int>(i);
                    break;
                }
            }
            if (local_index < 0) {
                scanTracks();
                rebuildList();
                for (size_t i = 0; i < _track_count; ++i) {
                    if (std::strcmp(_tracks[i].path, path) == 0) {
                        local_index = static_cast<int>(i);
                        break;
                    }
                }
            }
            if (local_index >= 0) {
                showPlayerPage();
                playIndex(local_index);
                return;
            }
        }
    }
    startCloudTask(CloudOperation::DownloadTrack, index);
}

void MusicPlayerApp::startCloudTask(CloudOperation op, int index)
{
    if (_state_mutex == nullptr) {
        _state_mutex = xSemaphoreCreateMutex();
    }
    if (_cloud_running || _cloud_task != nullptr) {
        setCloudState(true, _cloud_percent, "Cloud task already running");
        return;
    }
    _cloud_operation = op;
    _cloud_task_index = index;
    setCloudState(true, 0, op == CloudOperation::FetchList ? "Loading cloud list..." : "Download starting...");
    BaseType_t task_ok = xTaskCreateWithCaps(
        cloudTaskEntry,
        "music_cloud",
        CLOUD_TASK_STACK_SIZE,
        this,
        4,
        &_cloud_task,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    if (task_ok != pdPASS) {
        _cloud_task = nullptr;
        ESP_LOGW(
            APP_NAME,
            "cloud task create failed internal=%u largest_internal=%u psram=%u largest_psram=%u",
            static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
            static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
            static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
            static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM))
        );
        setCloudState(false, 0, "Cloud task create failed");
    }
}

void MusicPlayerApp::setCloudState(bool running, int percent, const char *status)
{
    if (_state_mutex == nullptr) {
        _state_mutex = xSemaphoreCreateMutex();
    }
    if ((_state_mutex == nullptr) || (xSemaphoreTake(_state_mutex, pdMS_TO_TICKS(50)) == pdTRUE)) {
        _cloud_running = running;
        _cloud_percent = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
        std::snprintf(_cloud_status, sizeof(_cloud_status), "%s", status != nullptr ? status : "");
        _cloud_dirty = true;
        if (_state_mutex != nullptr) {
            xSemaphoreGive(_state_mutex);
        }
    }
}

void MusicPlayerApp::flushCloudState()
{
    if (_state_mutex == nullptr) {
        _state_mutex = xSemaphoreCreateMutex();
    }
    if ((_state_mutex != nullptr) && (xSemaphoreTake(_state_mutex, 0) != pdTRUE)) {
        return;
    }
    const bool dirty = _cloud_dirty;
    const bool list_dirty = _list_dirty;
    const bool play_pending = _download_play_pending;
    char pending_path[sizeof(_download_play_path)] = {};
    if (play_pending) {
        safe_copy(pending_path, sizeof(pending_path), _download_play_path);
        _download_play_pending = false;
        _download_play_path[0] = '\0';
    }
    _cloud_dirty = false;
    _list_dirty = false;
    if (_state_mutex != nullptr) {
        xSemaphoreGive(_state_mutex);
    }
    if (dirty && !_cloud_running) {
        scanTracks();
    }
    if (list_dirty || (dirty && !_cloud_running)) {
        rebuildList();
    }
    if (play_pending && pending_path[0] != '\0') {
        int local_index = findLocalTrackByPath(pending_path);
        if (local_index < 0) {
            scanTracks();
            rebuildList();
            local_index = findLocalTrackByPath(pending_path);
        }
        if (local_index >= 0) {
            _current_index = local_index;
            showPlayerPage();
            startLocalPlay(local_index);
        } else {
            setCloudState(false, 100, "Saved, rescan needed");
        }
    }
}

bool MusicPlayerApp::cloudTrackLocalPath(const CloudMusicTrack &track, char *path, size_t path_size) const
{
    if ((path == nullptr) || (path_size == 0) || (track.filename[0] == '\0')) {
        return false;
    }
    return std::snprintf(path, path_size, "%s/%s", watch::AUDIO_MUSIC_DEFAULT_DIR, track.filename) > 0;
}

bool MusicPlayerApp::cloudTrackDownloaded(const CloudMusicTrack &track) const
{
    char path[160] = {};
    if (!cloudTrackLocalPath(track, path, sizeof(path))) {
        return false;
    }
    struct stat st = {};
    return (stat(path, &st) == 0) && S_ISREG(st.st_mode) && (st.st_size > 0);
}

int MusicPlayerApp::findLocalTrackByPath(const char *path) const
{
    if ((path == nullptr) || (path[0] == '\0')) {
        return -1;
    }
    for (size_t i = 0; i < _track_count; ++i) {
        if (std::strcmp(_tracks[i].path, path) == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void MusicPlayerApp::runCloudFetch()
{
    if (!watch::wifi_is_connected()) {
        watch::storage_sd_append_log("music", "cloud list skipped: WiFi not connected");
        setCloudState(false, 0, "WiFi not connected");
        return;
    }
    setCloudState(true, 5, "Login cloud...");
    std::string token = login_token();
    if (token.empty()) {
        setCloudState(false, 0, "Cloud login failed");
        watch::storage_sd_append_log("music", "cloud music login failed");
        return;
    }
    safe_copy(_cloud_token, sizeof(_cloud_token), token.c_str());

    setCloudState(true, 20, "Fetch cloud list...");
    HttpResponse response;
    if (!http_request(HTTP_METHOD_GET, MUSIC_LIST_URL, token.c_str(), nullptr, response)) {
        char msg[96] = {};
        std::snprintf(msg, sizeof(msg), "Cloud list HTTP failed");
        setCloudState(false, 0, msg);
        watch::storage_sd_append_log("music", msg);
        return;
    }
    if (response.status < 200 || response.status >= 300) {
        char msg[96] = {};
        std::snprintf(msg, sizeof(msg), "Cloud list HTTP %d", response.status);
        setCloudState(false, 0, msg);
        watch::storage_sd_append_log("music", msg);
        return;
    }

    cJSON *root = cJSON_Parse(response.body.c_str());
    if (root == nullptr) {
        setCloudState(false, 0, "Cloud list JSON invalid");
        watch::storage_sd_append_log("music", "cloud music JSON parse failed");
        return;
    }
    cJSON *tracks = cJSON_GetObjectItemCaseSensitive(root, "tracks");
    if (!cJSON_IsArray(tracks)) {
        tracks = cJSON_GetObjectItemCaseSensitive(root, "music");
    }
    if (!cJSON_IsArray(tracks)) {
        cJSON_Delete(root);
        setCloudState(false, 0, "Cloud list missing tracks");
        watch::storage_sd_append_log("music", "cloud music JSON missing tracks");
        return;
    }

    constexpr size_t max_parsed_tracks = 16;
    std::unique_ptr<CloudMusicTrack[]> parsed(new (std::nothrow) CloudMusicTrack[max_parsed_tracks]());
    if (!parsed) {
        cJSON_Delete(root);
        setCloudState(false, 0, "Cloud list no memory");
        watch::storage_sd_append_log("music", "cloud music parse buffer allocation failed");
        return;
    }
    size_t count = 0;
    cJSON *item = nullptr;
    cJSON_ArrayForEach(item, tracks) {
        if ((count >= max_parsed_tracks) || !cJSON_IsObject(item)) {
            continue;
        }
        std::string url = normalize_url(json_string(item, "url", ""));
        if (url.empty() || (url == std::string(PUBLIC_BASE_URL) + "/private OTA backend/")) {
            continue;
        }
        std::string title = json_string(item, "title", "");
        std::string artist = json_string(item, "artist", "");
        std::string filename = safe_filename(json_string(item, "filename", ""), url);
        if (title.empty()) {
            title = filename;
        }
        safe_copy(parsed[count].title, sizeof(parsed[count].title), title.c_str());
        safe_copy(parsed[count].artist, sizeof(parsed[count].artist), artist.c_str());
        safe_copy(parsed[count].url, sizeof(parsed[count].url), url.c_str());
        safe_copy(parsed[count].filename, sizeof(parsed[count].filename), filename.c_str());
        parsed[count].size = json_size(item, "size");
        parsed[count].downloaded = cloudTrackDownloaded(parsed[count]);
        ++count;
    }
    cJSON_Delete(root);

    if (_state_mutex == nullptr) {
        _state_mutex = xSemaphoreCreateMutex();
    }
    if ((_state_mutex == nullptr) || (xSemaphoreTake(_state_mutex, pdMS_TO_TICKS(200)) == pdTRUE)) {
        std::memset(_cloud_tracks, 0, sizeof(_cloud_tracks));
        for (size_t i = 0; i < count; ++i) {
            _cloud_tracks[i] = parsed[i];
        }
        _cloud_track_count = count;
        _list_dirty = true;
        if (_state_mutex != nullptr) {
            xSemaphoreGive(_state_mutex);
        }
    }
    char msg[96] = {};
    std::snprintf(msg, sizeof(msg), count > 0 ? "Cloud list: %u track(s)" : "Cloud list empty", static_cast<unsigned>(count));
    setCloudState(false, 100, msg);
    watch::storage_sd_append_log("music", msg);
}

void MusicPlayerApp::runCloudDownload(int index)
{
    if (!watch::wifi_is_connected()) {
        watch::storage_sd_append_log("music", "cloud download skipped: WiFi not connected");
        setCloudState(false, 0, "WiFi not connected");
        return;
    }
    esp_err_t err = watch::storage_sd_ensure_standard_dirs();
    if (err != ESP_OK) {
        char msg[96] = {};
        std::snprintf(msg, sizeof(msg), "TF not ready: %s", esp_err_to_name(err));
        watch::storage_sd_append_log("music", msg);
        setCloudState(false, 0, msg);
        return;
    }

    CloudMusicTrack track = {};
    if ((index < 0) || (index >= static_cast<int>(_cloud_track_count))) {
        setCloudState(false, 0, "Invalid cloud track");
        return;
    }
    track = _cloud_tracks[index];

    char final_path[160] = {};
    char tmp_path[176] = {};
    if (!cloudTrackLocalPath(track, final_path, sizeof(final_path))) {
        setCloudState(false, 0, "Invalid filename");
        return;
    }
    std::snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", final_path);

    watch::audio_music_stop();
    watch::audio_test_stop();

    char msg[128] = {};
    std::snprintf(msg, sizeof(msg), "Downloading %s...", track.title);
    setCloudState(true, 0, msg);

    esp_http_client_config_t config = {};
    config.url = track.url;
    config.timeout_ms = 30000;
    config.user_agent = USER_AGENT;
    config.keep_alive_enable = false;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        setCloudState(false, 0, "HTTP init failed");
        watch::storage_sd_append_log("music", "cloud download HTTP init failed");
        return;
    }
    esp_http_client_set_method(client, HTTP_METHOD_GET);
    if (_cloud_token[0] == '\0') {
        std::string token = login_token();
        if (token.empty()) {
            esp_http_client_cleanup(client);
            setCloudState(false, 0, "Cloud login failed");
            watch::storage_sd_append_log("music", "cloud download login failed");
            return;
        }
        safe_copy(_cloud_token, sizeof(_cloud_token), token.c_str());
    }
    std::string auth = "Bearer ";
    auth += _cloud_token;
    esp_http_client_set_header(client, "Authorization", auth.c_str());
    err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        std::snprintf(msg, sizeof(msg), "Open failed: %s", esp_err_to_name(err));
        setCloudState(false, 0, msg);
        watch::storage_sd_append_log("music", msg);
        return;
    }

    const int64_t content_length = esp_http_client_fetch_headers(client);
    const int http_status = esp_http_client_get_status_code(client);
    if (http_status < 200 || http_status >= 300 || content_length <= 0) {
        esp_http_client_cleanup(client);
        std::snprintf(msg, sizeof(msg), "Cloud HTTP %d len %lld", http_status, static_cast<long long>(content_length));
        setCloudState(false, 0, msg);
        watch::storage_sd_append_log("music", msg);
        return;
    }
    if (content_length > static_cast<int64_t>(MAX_DOWNLOAD_BYTES)) {
        esp_http_client_cleanup(client);
        setCloudState(false, 0, "Cloud file too large");
        watch::storage_sd_append_log("music", "cloud download rejected: file too large");
        return;
    }

    FILE *fp = std::fopen(tmp_path, "wb");
    if (fp == nullptr) {
        esp_http_client_cleanup(client);
        setCloudState(false, 0, "Open SD file failed");
        watch::storage_sd_append_log("music", "open cloud tmp file failed");
        return;
    }

    auto *buffer = static_cast<uint8_t *>(
        heap_caps_malloc(CLOUD_DOWNLOAD_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
    );
    if (buffer == nullptr) {
        buffer = static_cast<uint8_t *>(heap_caps_malloc(CLOUD_DOWNLOAD_BUFFER_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    if (buffer == nullptr) {
        std::fclose(fp);
        esp_http_client_cleanup(client);
        std::remove(tmp_path);
        setCloudState(false, 0, "Download no memory");
        watch::storage_sd_append_log("music", "cloud download buffer allocation failed");
        return;
    }
    int64_t total_read = 0;
    int last_percent = -1;
    err = ESP_OK;
    while (true) {
        const int read = esp_http_client_read(client, reinterpret_cast<char *>(buffer), CLOUD_DOWNLOAD_BUFFER_SIZE);
        if (read < 0) {
            err = ESP_FAIL;
            break;
        }
        if (read == 0) {
            if (esp_http_client_is_complete_data_received(client)) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (std::fwrite(buffer, 1, read, fp) != static_cast<size_t>(read)) {
            err = ESP_FAIL;
            break;
        }
        total_read += read;
        int percent = static_cast<int>((total_read * 100) / content_length);
        percent = std::clamp(percent, 0, 100);
        if ((percent == 100) || (last_percent < 0) || ((percent - last_percent) >= 2)) {
            last_percent = percent;
            std::snprintf(msg, sizeof(msg), "Download %d%%", percent);
            setCloudState(true, percent, msg);
        }
    }
    std::free(buffer);
    const int close_ret = std::fclose(fp);
    esp_http_client_cleanup(client);

    if ((err != ESP_OK) || (close_ret != 0) || (total_read != content_length)) {
        std::remove(tmp_path);
        std::snprintf(msg, sizeof(msg), "Download failed %lld/%lld", static_cast<long long>(total_read), static_cast<long long>(content_length));
        setCloudState(false, last_percent < 0 ? 0 : last_percent, msg);
        watch::storage_sd_append_log("music", msg);
        return;
    }

    std::remove(final_path);
    if (std::rename(tmp_path, final_path) != 0) {
        setCloudState(false, 100, "Save cloud file failed");
        watch::storage_sd_append_log("music", "rename cloud music tmp failed");
        return;
    }

    if (_state_mutex == nullptr) {
        _state_mutex = xSemaphoreCreateMutex();
    }
    if ((_state_mutex == nullptr) || (xSemaphoreTake(_state_mutex, pdMS_TO_TICKS(200)) == pdTRUE)) {
        if ((index >= 0) && (index < static_cast<int>(_cloud_track_count))) {
            _cloud_tracks[index].downloaded = true;
        }
        _list_dirty = true;
        if (_state_mutex != nullptr) {
            xSemaphoreGive(_state_mutex);
        }
    }
    if (_download_play_after) {
        if (_state_mutex == nullptr) {
            _state_mutex = xSemaphoreCreateMutex();
        }
        if ((_state_mutex == nullptr) || (xSemaphoreTake(_state_mutex, pdMS_TO_TICKS(200)) == pdTRUE)) {
            safe_copy(_download_play_path, sizeof(_download_play_path), final_path);
            _download_play_pending = true;
            if (_state_mutex != nullptr) {
                xSemaphoreGive(_state_mutex);
            }
        }
    }
    std::snprintf(msg, sizeof(msg), "Saved: %s", track.filename);
    setCloudState(false, 100, msg);
    watch::storage_sd_append_log("music", msg);
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, MusicPlayerApp, APP_NAME, []()
{
    return std::shared_ptr<MusicPlayerApp>(MusicPlayerApp::requestInstance(), [](MusicPlayerApp *) {});
})

} // namespace esp_brookesia::apps
