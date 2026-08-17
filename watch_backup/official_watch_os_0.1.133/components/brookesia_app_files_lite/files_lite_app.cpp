#include "files_lite_app.hpp"

#include <cstdio>
#include <cstring>

#include "esp_brookesia.hpp"
#include "esp_err.h"
#include "esp_lib_utils.h"
#include "lvgl.h"
#include "watch_fonts.hpp"
#include "watch_storage.hpp"
#include "watch_app_icons.hpp"

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "FilesLite"

namespace esp_brookesia::apps {
namespace {
constexpr const char *APP_NAME = "Files";
constexpr int SAFE_TOP = 24;
constexpr int SAFE_SIDE = 22;

lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    watch_display::apply_text_font(label, text, font);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

void style_button(lv_obj_t *button, uint32_t bg)
{
    lv_obj_set_style_radius(button, 24, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
}
} // namespace

FilesLiteApp *FilesLiteApp::_instance = nullptr;

FilesLiteApp *FilesLiteApp::requestInstance()
{
    if (_instance == nullptr) {
        _instance = new FilesLiteApp();
    }
    return _instance;
}

FilesLiteApp::FilesLiteApp(): systems::phone::App(APP_NAME, watch_app_icon_files_48(), true, true, true) {}

bool FilesLiteApp::run(void)
{
    lv_obj_t *screen = lv_scr_act();
    ESP_UTILS_CHECK_NULL_RETURN(screen, false, "Invalid active screen");
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x07080B), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t *root = lv_obj_create(screen);
    ESP_UTILS_CHECK_NULL_RETURN(root, false, "Create root failed");
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root, lv_color_hex(0x07080B), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(root, SAFE_SIDE, 0);
    lv_obj_set_style_pad_right(root, SAFE_SIDE, 0);
    lv_obj_set_style_pad_top(root, SAFE_TOP, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(root, 10, 0);
    lv_obj_set_scroll_dir(root, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(root, LV_SCROLLBAR_MODE_AUTO);

    make_label(root, "Files", &lv_font_montserrat_30, 0xFFFFFF);
    _status_label = make_label(root, "Checking LittleFS...", &lv_font_montserrat_16, 0xAEB7C6);
    lv_obj_set_width(_status_label, LV_PCT(100));
    lv_label_set_long_mode(_status_label, LV_LABEL_LONG_WRAP);

    _message_label = make_label(root, "SD data folders: music/watchfaces/apps/cache/ota/logs/exports", &lv_font_montserrat_14, 0x7EE787);
    lv_obj_set_width(_message_label, LV_PCT(100));
    lv_label_set_long_mode(_message_label, LV_LABEL_LONG_WRAP);

    _list_label = make_label(root, "", &lv_font_montserrat_14, 0xD7DCE5);
    lv_obj_set_width(_list_label, LV_PCT(100));
    lv_label_set_long_mode(_list_label, LV_LABEL_LONG_WRAP);

    lv_obj_t *init_sd_btn = lv_button_create(root);
    lv_obj_set_width(init_sd_btn, LV_PCT(100));
    lv_obj_set_height(init_sd_btn, 44);
    style_button(init_sd_btn, 0x2E7D32);
    lv_obj_add_event_cb(init_sd_btn, onInitSdClicked, LV_EVENT_CLICKED, this);
    lv_obj_center(make_label(init_sd_btn, "Init SD folders", &lv_font_montserrat_18, 0xFFFFFF));

    lv_obj_t *music_btn = lv_button_create(root);
    lv_obj_set_width(music_btn, LV_PCT(100));
    lv_obj_set_height(music_btn, 44);
    style_button(music_btn, 0x293062);
    lv_obj_add_event_cb(music_btn, onMusicClicked, LV_EVENT_CLICKED, this);
    lv_obj_center(make_label(music_btn, "SD Music", &lv_font_montserrat_18, 0xFFFFFF));

    lv_obj_t *logs_btn = lv_button_create(root);
    lv_obj_set_width(logs_btn, LV_PCT(100));
    lv_obj_set_height(logs_btn, 44);
    style_button(logs_btn, 0x293062);
    lv_obj_add_event_cb(logs_btn, onLogsClicked, LV_EVENT_CLICKED, this);
    lv_obj_center(make_label(logs_btn, "SD Logs", &lv_font_montserrat_18, 0xFFFFFF));

    lv_obj_t *littlefs_btn = lv_button_create(root);
    lv_obj_set_width(littlefs_btn, LV_PCT(100));
    lv_obj_set_height(littlefs_btn, 44);
    style_button(littlefs_btn, 0x293062);
    lv_obj_add_event_cb(littlefs_btn, onLittleFsClicked, LV_EVENT_CLICKED, this);
    lv_obj_center(make_label(littlefs_btn, "LittleFS", &lv_font_montserrat_18, 0xFFFFFF));

    lv_obj_t *refresh_btn = lv_button_create(root);
    lv_obj_set_width(refresh_btn, LV_PCT(100));
    lv_obj_set_height(refresh_btn, 44);
    style_button(refresh_btn, 0x1B6BFF);
    lv_obj_add_event_cb(refresh_btn, onRefreshClicked, LV_EVENT_CLICKED, this);
    lv_obj_center(make_label(refresh_btn, "Refresh", &lv_font_montserrat_18, 0xFFFFFF));

    lv_obj_t *back = lv_button_create(root);
    lv_obj_set_width(back, LV_PCT(100));
    lv_obj_set_height(back, 44);
    style_button(back, 0x232833);
    lv_obj_add_event_cb(back, onBackClicked, LV_EVENT_CLICKED, this);
    lv_obj_center(make_label(back, "Back", &lv_font_montserrat_18, 0xFFFFFF));

    refresh();
    return true;
}

bool FilesLiteApp::back(void)
{
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

void FilesLiteApp::onBackClicked(lv_event_t *event)
{
    auto *app = static_cast<FilesLiteApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->back();
}

void FilesLiteApp::onRefreshClicked(lv_event_t *event)
{
    auto *app = static_cast<FilesLiteApp *>(lv_event_get_user_data(event));
    if (app != nullptr) app->refresh();
}

void FilesLiteApp::onMusicClicked(lv_event_t *event)
{
    auto *app = static_cast<FilesLiteApp *>(lv_event_get_user_data(event));
    if (app != nullptr) {
        app->_view = FileView::SdMusic;
        app->refresh();
    }
}

void FilesLiteApp::onLogsClicked(lv_event_t *event)
{
    auto *app = static_cast<FilesLiteApp *>(lv_event_get_user_data(event));
    if (app != nullptr) {
        app->_view = FileView::SdLogs;
        app->refresh();
    }
}

void FilesLiteApp::onLittleFsClicked(lv_event_t *event)
{
    auto *app = static_cast<FilesLiteApp *>(lv_event_get_user_data(event));
    if (app != nullptr) {
        app->_view = FileView::LittleFs;
        app->refresh();
    }
}

void FilesLiteApp::onInitSdClicked(lv_event_t *event)
{
    auto *app = static_cast<FilesLiteApp *>(lv_event_get_user_data(event));
    if (app == nullptr) {
        return;
    }
    esp_err_t err = watch::storage_sd_ensure_standard_dirs();
    if (app->_message_label != nullptr) {
        if (err == ESP_OK) {
            app->_view = FileView::Overview;
            lv_label_set_text(app->_message_label, "SD folders ready:\n/music /watchfaces /apps /cache /ota /logs /exports");
        } else {
            char text[128] = {};
            std::snprintf(text, sizeof(text), "Init SD folders failed:\n%s", esp_err_to_name(err));
            lv_label_set_text(app->_message_label, text);
        }
    }
    app->refresh();
}

void FilesLiteApp::refresh()
{
    if (_status_label != nullptr) {
        char littlefs_text[260] = {};
        char sd_text[560] = {};
        char text[900] = {};
        watch::storage_littlefs_status_text(littlefs_text, sizeof(littlefs_text));
        watch::storage_sd_status_text(sd_text, sizeof(sd_text));
        std::snprintf(text, sizeof(text), "%s\n\n%s", littlefs_text, sd_text);
        watch_display::set_label_text(_status_label, text, &lv_font_montserrat_14);
    }
    if (_list_label != nullptr) {
        char sd_list[900] = {};
        char sd_logs[500] = {};
        char littlefs_list[700] = {};
        char text[2200] = {};
        switch (_view) {
        case FileView::SdMusic:
            watch::storage_sd_list_text(sd_list, sizeof(sd_list), "/sdcard/music");
            std::snprintf(text, sizeof(text), "SD music\n%s", sd_list);
            break;
        case FileView::SdLogs:
            watch::storage_sd_list_text(sd_logs, sizeof(sd_logs), "/sdcard/logs");
            std::snprintf(text, sizeof(text), "SD logs\n%s", sd_logs);
            break;
        case FileView::LittleFs:
            watch::storage_littlefs_list_text(littlefs_list, sizeof(littlefs_list));
            std::snprintf(text, sizeof(text), "LittleFS root\n%s", littlefs_list);
            break;
        case FileView::Overview:
        default:
            watch::storage_sd_list_text(sd_list, sizeof(sd_list));
            watch::storage_sd_list_text(sd_logs, sizeof(sd_logs), "/sdcard/logs");
            watch::storage_littlefs_list_text(littlefs_list, sizeof(littlefs_list));
            std::snprintf(text, sizeof(text), "SD root\n%s\n\nSD logs\n%s\n\nLittleFS root\n%s", sd_list, sd_logs, littlefs_list);
            break;
        }
        watch_display::set_label_text(_list_label, text, &lv_font_montserrat_12);
    }
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, FilesLiteApp, APP_NAME, []()
{
    return std::shared_ptr<FilesLiteApp>(FilesLiteApp::requestInstance(), [](FilesLiteApp *) {});
})

} // namespace esp_brookesia::apps
