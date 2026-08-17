#include "codex_monitor_app.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "cJSON.h"
#include "esp_brookesia.hpp"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "esp_lib_utils.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "widgets/ime/lv_ime_pinyin.h"
#include "watch_app_icons.hpp"
#include "watch_connectivity.hpp"
#include "watch_audio.hpp"
#include "watch_fonts.hpp"
#include "watch_private_config.hpp"

namespace esp_brookesia::apps {
namespace {
constexpr const char *APP_NAME = "Codex";
constexpr size_t MAX_BODY = 24 * 1024;
constexpr size_t MAX_HTTP_BODY = 24 * 1024;
constexpr size_t CODex_VOICE_SAMPLE_RATE = 16000;
constexpr size_t CODex_VOICE_SECONDS = 4;
constexpr size_t CODex_VOICE_BYTES = CODex_VOICE_SAMPLE_RATE * CODex_VOICE_SECONDS * sizeof(int16_t);
struct HttpBody { std::string data; int status = 0; };
esp_err_t event_cb(esp_http_client_event_t *event) {
    auto *body = static_cast<HttpBody *>(event->user_data);
    if (event->event_id == HTTP_EVENT_ON_DATA && body && event->data && body->data.size() + event->data_len <= MAX_BODY) body->data.append(static_cast<const char *>(event->data), event->data_len);
    return ESP_OK;
}
bool request(const char *url, const char *token, HttpBody &body, const char *post_body = nullptr) {
    esp_http_client_config_t cfg = {}; cfg.url = url; cfg.timeout_ms = 12000; cfg.event_handler = event_cb; cfg.user_data = &body; cfg.keep_alive_enable = false;
    auto *client = esp_http_client_init(&cfg); if (!client) return false;
    if (post_body) { esp_http_client_set_method(client, HTTP_METHOD_POST); esp_http_client_set_header(client, "Content-Type", "application/json"); esp_http_client_set_post_field(client, post_body, std::strlen(post_body)); }
    if (token && token[0]) { std::string auth = "Bearer "; auth += token; esp_http_client_set_header(client, "Authorization", auth.c_str()); }
    const esp_err_t err = esp_http_client_perform(client); body.status = esp_http_client_get_status_code(client); esp_http_client_cleanup(client);
    return err == ESP_OK && body.status >= 200 && body.status < 300;
}

bool request_audio(const char *token, const uint8_t *audio, size_t audio_size, HttpBody &body)
{
    if (!token || !token[0] || !audio || audio_size == 0) return false;
    constexpr const char *boundary = "----codexAudioBoundary";
    const std::string prefix = std::string("--") + boundary + "\r\n"
        "Content-Disposition: form-data; name=\"device_id\"\r\n\r\nesp32-s3-watch\r\n--" + boundary + "\r\n"
        "Content-Disposition: form-data; name=\"source\"\r\n\r\ncodex\r\n--" + boundary + "\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"codex.wav\"\r\n"
        "Content-Type: audio/wav\r\n\r\n";
    const std::string suffix = std::string("\r\n--") + boundary + "--\r\n";
    esp_http_client_config_t config = {};
    config.url = WATCH_PRIVATE_DUDUSERVER_API_BASE_URL "/ai-call/audio";
    config.timeout_ms = 30000;
    config.event_handler = event_cb;
    config.user_data = &body;
    config.keep_alive_enable = false;
    auto *client = esp_http_client_init(&config);
    if (!client) return false;
    std::string auth = "Bearer "; auth += token;
    std::string content_type = std::string("multipart/form-data; boundary=") + boundary;
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Authorization", auth.c_str());
    esp_http_client_set_header(client, "Content-Type", content_type.c_str());
    const int total = static_cast<int>(prefix.size() + audio_size + suffix.size());
    bool ok = esp_http_client_open(client, total) == ESP_OK;
    if (ok) ok = esp_http_client_write(client, prefix.data(), prefix.size()) == static_cast<int>(prefix.size());
    if (ok) ok = esp_http_client_write(client, reinterpret_cast<const char *>(audio), audio_size) == static_cast<int>(audio_size);
    if (ok) ok = esp_http_client_write(client, suffix.data(), suffix.size()) == static_cast<int>(suffix.size());
    if (ok) {
        esp_http_client_fetch_headers(client);
        body.status = esp_http_client_get_status_code(client);
        char buffer[512];
        for (;;) {
            const int read = esp_http_client_read(client, buffer, sizeof(buffer));
            if (read <= 0) break;
            if (body.data.size() + static_cast<size_t>(read) > MAX_HTTP_BODY) { ok = false; break; }
            body.data.append(buffer, read);
        }
        ok = ok && body.status >= 200 && body.status < 300;
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ok;
}

void write_wav_header(uint8_t *data, size_t pcm_size)
{
    if (!data || pcm_size < 44) return;
    const uint32_t data_size = static_cast<uint32_t>(pcm_size - 44);
    const uint32_t rate = CODex_VOICE_SAMPLE_RATE;
    const uint32_t byte_rate = rate * 2;
    std::memcpy(data, "RIFF", 4); std::memcpy(data + 8, "WAVEfmt ", 8);
    const uint32_t riff_size = data_size + 36; const uint32_t fmt_size = 16;
    const uint16_t format = 1, channels = 1, bits = 16, block_align = 2;
    std::memcpy(data + 4, &riff_size, 4); std::memcpy(data + 16, &fmt_size, 4);
    std::memcpy(data + 20, &format, 2); std::memcpy(data + 22, &channels, 2);
    std::memcpy(data + 24, &rate, 4); std::memcpy(data + 28, &byte_rate, 4);
    std::memcpy(data + 32, &block_align, 2); std::memcpy(data + 34, &bits, 2);
    std::memcpy(data + 36, "data", 4); std::memcpy(data + 40, &data_size, 4);
}

void describe_audio_error(const HttpBody &body, char *status, size_t status_size)
{
    const char *detail = nullptr;
    cJSON *root = cJSON_Parse(body.data.c_str());
    cJSON *item = root ? cJSON_GetObjectItemCaseSensitive(root, "detail") : nullptr;
    if (cJSON_IsString(item) && item->valuestring) detail = item->valuestring;
    if (detail && std::strcmp(detail, "no speech detected") == 0) {
        std::snprintf(status, status_size, "%s", "No speech detected. Try again");
    } else if (detail && std::strcmp(detail, "audio too quiet") == 0) {
        std::snprintf(status, status_size, "%s", "Voice too quiet. Speak closer");
    } else if (detail && std::strcmp(detail, "audio too short") == 0) {
        std::snprintf(status, status_size, "%s", "Recording was too short. Retry");
    } else {
        std::snprintf(status, status_size, "Voice request failed (%d)", body.status);
    }
    cJSON_Delete(root);
}
std::string login() {
    HttpBody body; if (!request(WATCH_PRIVATE_DUDUSERVER_API_BASE_URL "/auth/login", nullptr, body, WATCH_PRIVATE_LOGIN_BODY)) return {};
    cJSON *root = cJSON_Parse(body.data.c_str()); cJSON *token = root ? cJSON_GetObjectItemCaseSensitive(root, "access_token") : nullptr;
    std::string result = cJSON_IsString(token) && token->valuestring ? token->valuestring : ""; cJSON_Delete(root); return result;
}
lv_obj_t *text(lv_obj_t *p, const char *s, const lv_font_t *f, uint32_t c) { auto *o = lv_label_create(p); lv_label_set_text(o, s); watch_display::apply_text_font(o, s, f); lv_obj_set_style_text_color(o, lv_color_hex(c), 0); return o; }
void scroll_history_to_latest(void *user_data)
{
    auto *history = static_cast<lv_obj_t *>(user_data);
    if (history && lv_obj_is_valid(history)) {
        lv_obj_update_layout(history);
        lv_obj_scroll_to_y(history, LV_COORD_MAX, LV_ANIM_OFF);
    }
}

uint32_t state_color(const char *state)
{
    if (!state) return 0xAEB7C6;
    if (std::strstr(state, "waiting") || std::strstr(state, "choice")) return 0xFFD166;
    if (std::strstr(state, "run") || std::strstr(state, "claim")) return 0x69D2FF;
    if (std::strstr(state, "complete") || std::strstr(state, "done")) return 0x76E0B3;
    if (std::strstr(state, "fail") || std::strstr(state, "error")) return 0xFF6B6B;
    return 0xAEB7C6;
}

bool same_text(const char *lhs, const char *rhs)
{
    return lhs && rhs && std::strcmp(lhs, rhs) == 0;
}

bool is_continue_text(const char *text)
{
    return same_text(text, "继续") || same_text(text, "Continue") || same_text(text, "continue");
}

bool is_stop_text(const char *text)
{
    return same_text(text, "停止") || same_text(text, "结束测试") ||
           same_text(text, "Stop") || same_text(text, "stop");
}

bool transcript_contains_message(const char *transcript, const char *message)
{
    if (!transcript || !message || !message[0]) return false;
    char copy[601] = {};
    std::snprintf(copy, sizeof(copy), "%s", transcript);
    char *save = nullptr;
    for (char *line = strtok_r(copy, "\n", &save); line != nullptr; line = strtok_r(nullptr, "\n", &save)) {
        const char *body = nullptr;
        if (std::strncmp(line, "你：", std::strlen("你：")) == 0) body = line + std::strlen("你：");
        else if (std::strncmp(line, "You:", std::strlen("You:")) == 0) body = line + std::strlen("You:");
        if (!body) continue;
        while (*body == ' ') ++body;
        if (std::strcmp(body, message) == 0) return true;
    }
    return false;
}

std::string latest_codex_reply(const char *transcript)
{
    if (!transcript || !transcript[0]) return {};
    char copy[601] = {};
    std::snprintf(copy, sizeof(copy), "%s", transcript);
    std::string latest;
    char *save = nullptr;
    for (char *line = strtok_r(copy, "\n", &save); line != nullptr; line = strtok_r(nullptr, "\n", &save)) {
        const char *body = nullptr;
        if (std::strncmp(line, "Codex：", std::strlen("Codex：")) == 0) body = line + std::strlen("Codex：");
        else if (std::strncmp(line, "Codex:", std::strlen("Codex:")) == 0) body = line + std::strlen("Codex:");
        if (body) {
            while (*body == ' ') ++body;
            latest = body;
        }
    }
    return latest;
}

lv_obj_t *small_button(lv_obj_t *parent, const char *label, uint32_t color, lv_event_cb_t cb, void *user_data, int width = 74, int height = 36)
{
    auto *button = lv_button_create(parent);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, user_data);
    auto *label_obj = text(button, label, &lv_font_montserrat_12, 0xFFFFFF);
    lv_obj_center(label_obj);
    lv_label_set_long_mode(label_obj, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label_obj, LV_PCT(88));
    return button;
}
} // namespace

CodexMonitorApp *CodexMonitorApp::_instance = nullptr;
CodexMonitorApp *CodexMonitorApp::requestInstance() { if (!_instance) _instance = new CodexMonitorApp(); return _instance; }
CodexMonitorApp::CodexMonitorApp() : systems::phone::App(APP_NAME, watch_app_icon_codex_48(), true, true, true) {}
bool CodexMonitorApp::run(void) {
    auto *screen = lv_scr_act(); ESP_UTILS_CHECK_NULL_RETURN(screen, false, "Invalid screen"); lv_obj_set_style_bg_color(screen, lv_color_hex(0x07080B), 0); lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    auto *root = lv_obj_create(screen); lv_obj_remove_style_all(root); lv_obj_set_size(root, LV_PCT(100), LV_PCT(100)); lv_obj_set_style_pad_left(root, 14, 0); lv_obj_set_style_pad_right(root, 14, 0); lv_obj_set_style_pad_top(root, 12, 0); lv_obj_set_style_pad_row(root, 5, 0); lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    _status = text(root, _status_text, &lv_font_montserrat_12, 0xAEB7C6); lv_obj_set_width(_status, LV_PCT(100)); lv_obj_set_height(_status, 16); lv_label_set_long_mode(_status, LV_LABEL_LONG_DOT);
    _list_page = lv_obj_create(root); lv_obj_remove_style_all(_list_page); lv_obj_set_width(_list_page, LV_PCT(100)); lv_obj_set_flex_grow(_list_page, 1); lv_obj_set_flex_flow(_list_page, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(_list_page, 8, 0); lv_obj_set_scroll_dir(_list_page, LV_DIR_VER);
    _detail_page = lv_obj_create(root); lv_obj_remove_style_all(_detail_page); lv_obj_set_width(_detail_page, LV_PCT(100)); lv_obj_set_flex_grow(_detail_page, 1); lv_obj_set_flex_flow(_detail_page, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(_detail_page, 10, 0); lv_obj_add_flag(_detail_page, LV_OBJ_FLAG_HIDDEN);
    auto *row = lv_obj_create(root); lv_obj_remove_style_all(row); lv_obj_set_width(row, LV_PCT(100)); lv_obj_set_height(row, 34); lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_column(row, 6, 0);
    for (int i = 0; i < 2; ++i) { auto *b = lv_button_create(row); lv_obj_set_size(b, LV_PCT(49), 32); lv_obj_set_style_radius(b, 8, 0); lv_obj_set_style_bg_color(b, lv_color_hex(i ? 0x2B303B : 0x315C9E), 0); lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0); lv_obj_add_event_cb(b, i ? onBack : onRefresh, LV_EVENT_CLICKED, this); lv_obj_center(text(b, i ? "Back" : "Refresh", &lv_font_montserrat_12, 0xFFFFFF)); }
    showList(); _timer = lv_timer_create(onTimer, 300, this); requestRefresh(); return true;
}
bool CodexMonitorApp::back(void) { ++_generation; closeInput(); closeVoiceOverlay(); if (_timer) { lv_timer_delete(_timer); _timer = nullptr; } _status = _list_page = _detail_page = nullptr; ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Close failed"); return true; }
void CodexMonitorApp::onRefresh(lv_event_t *e) { if (auto *a = static_cast<CodexMonitorApp *>(lv_event_get_user_data(e))) a->requestRefresh(); }
void CodexMonitorApp::onBack(lv_event_t *e) { if (auto *a = static_cast<CodexMonitorApp *>(lv_event_get_user_data(e))) a->back(); }
void CodexMonitorApp::onTask(lv_event_t *e) { auto *ctx = static_cast<TaskCardContext *>(lv_event_get_user_data(e)); if (ctx && ctx->app) ctx->app->selectTask(ctx->index); }
void CodexMonitorApp::onDetailBack(lv_event_t *e) { if (auto *a = static_cast<CodexMonitorApp *>(lv_event_get_user_data(e))) a->showList(); }
void CodexMonitorApp::onChoice(lv_event_t *e) { auto *ctx = static_cast<ChoiceContext *>(lv_event_get_user_data(e)); if (ctx && ctx->app) ctx->app->submitChoice(ctx->option); }
void CodexMonitorApp::onInput(lv_event_t *e) { if (auto *a = static_cast<CodexMonitorApp *>(lv_event_get_user_data(e))) a->showInput(); }
void CodexMonitorApp::onInputSend(lv_event_t *e) { if (auto *a = static_cast<CodexMonitorApp *>(lv_event_get_user_data(e))) a->submitText(a->_input_textarea ? lv_textarea_get_text(a->_input_textarea) : ""); }
void CodexMonitorApp::onInputCancel(lv_event_t *e) { if (auto *a = static_cast<CodexMonitorApp *>(lv_event_get_user_data(e))) a->closeInput(); }
void CodexMonitorApp::onKeyboardReady(lv_event_t *e) { if (auto *a = static_cast<CodexMonitorApp *>(lv_event_get_user_data(e))) a->submitText(a->_input_textarea ? lv_textarea_get_text(a->_input_textarea) : ""); }
void CodexMonitorApp::onInputFocused(lv_event_t *e) {
    auto *app = static_cast<CodexMonitorApp *>(lv_event_get_user_data(e));
    if (!app || !app->_input_sheet || !app->_input_keyboard) return;
    // LVGL's K9 map has three letter rows plus a fourth row of pinyin
    // combinations.  The compact sheet clipped that fourth row.
    lv_obj_set_height(app->_input_sheet, LV_PCT(100));
    lv_obj_set_style_radius(app->_input_sheet, 0, 0);
    // Keep the input method geometry out of the sheet's flex flow.  The
    // candidate strip sits directly above a lower keyboard and never crosses
    // the Send row.
    lv_obj_add_flag(app->_input_keyboard, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(app->_input_keyboard, LV_PCT(100), 160);
    lv_obj_align(app->_input_keyboard, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_move_foreground(app->_input_keyboard);
    if (app->_input_candidates) {
        lv_obj_add_flag(app->_input_candidates, LV_OBJ_FLAG_FLOATING);
        lv_obj_set_size(app->_input_candidates, LV_PCT(100), 34);
        lv_obj_align_to(app->_input_candidates, app->_input_keyboard, LV_ALIGN_OUT_TOP_MID, 0, -4);
        lv_obj_set_style_bg_color(app->_input_candidates, lv_color_hex(0x182235), 0);
        lv_obj_set_style_bg_opa(app->_input_candidates, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(app->_input_candidates, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_text_color(app->_input_candidates, lv_color_hex(0xFFFFFF), LV_PART_ITEMS);
        const auto checked_items = static_cast<lv_style_selector_t>(LV_PART_ITEMS) | static_cast<lv_style_selector_t>(LV_STATE_CHECKED);
        lv_obj_set_style_text_color(app->_input_candidates, lv_color_hex(0xFFFFFF), checked_items);
        lv_obj_set_style_bg_color(app->_input_candidates, lv_color_hex(0x315C9E), checked_items);
        lv_obj_set_style_bg_opa(app->_input_candidates, LV_OPA_COVER, checked_items);
        lv_obj_set_style_border_width(app->_input_candidates, 1, 0);
        lv_obj_set_style_border_color(app->_input_candidates, lv_color_hex(0x47648E), 0);
        lv_obj_move_foreground(app->_input_candidates);
    }
    lv_obj_clear_flag(app->_input_keyboard, LV_OBJ_FLAG_HIDDEN);
    if (app->_input_ime) lv_obj_clear_flag(app->_input_ime, LV_OBJ_FLAG_HIDDEN);
}
void CodexMonitorApp::onKeyboardMode(lv_event_t *e) {
    auto *app = static_cast<CodexMonitorApp *>(lv_event_get_user_data(e));
    if (!app || !app->_input_ime) return;
    const bool k9 = lv_event_get_target(e) == app->_k9_mode_button;
    lv_ime_pinyin_set_mode(app->_input_ime, k9 ? LV_IME_PINYIN_MODE_K9 : LV_IME_PINYIN_MODE_K26);
    if (!k9 && app->_input_keyboard) lv_keyboard_set_mode(app->_input_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_set_style_bg_color(app->_k9_mode_button, lv_color_hex(k9 ? 0x315C9E : 0x27303E), 0);
    lv_obj_set_style_bg_color(app->_full_mode_button, lv_color_hex(k9 ? 0x27303E : 0x315C9E), 0);
}
void CodexMonitorApp::onPhrase(lv_event_t *e) { auto *ctx = static_cast<PhraseContext *>(lv_event_get_user_data(e)); if (ctx && ctx->app && ctx->text) ctx->app->submitText(ctx->text); }
void CodexMonitorApp::onVoice(lv_event_t *e) { if (auto *a = static_cast<CodexMonitorApp *>(lv_event_get_user_data(e))) a->startVoiceInput(); }
void CodexMonitorApp::onTimer(lv_timer_t *t) { if (auto *a = static_cast<CodexMonitorApp *>(lv_timer_get_user_data(t))) a->flush(); }
void CodexMonitorApp::requestRefresh() {
    if (_task) return;
    if (!_mutex) _mutex = xSemaphoreCreateMutex();
    ++_generation;
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) == pdTRUE) { std::snprintf(_status_text, sizeof(_status_text), "%s", "Loading tasks..."); _dirty = true; xSemaphoreGive(_mutex); }
    if (xTaskCreateWithCaps(task, "codex_monitor", 8192, this, 1, &_task, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) _task = nullptr;
}
void CodexMonitorApp::submitChoice(uint8_t option) {
    if (_task || _awaiting_reply || _selected < 0 || _selected >= _task_count || option >= _tasks[_selected].option_count || !_tasks[_selected].controlled) return;
    _pending_option = option;
    std::snprintf(_pending_message, sizeof(_pending_message), "%s", _tasks[_selected].options[option]);
    std::snprintf(_pending_task_id, sizeof(_pending_task_id), "%s", _tasks[_selected].id);
    std::snprintf(_last_sent_task_id, sizeof(_last_sent_task_id), "%s", _tasks[_selected].id);
    std::snprintf(_last_sent_message, sizeof(_last_sent_message), "%s", _pending_message);
    _awaiting_reply = true;
    std::snprintf(_status_text, sizeof(_status_text), "%s", "Sending command..."); _dirty = true; _content_dirty = true;
    showVoiceOverlay("Sending command...", 0x7EDFA7);
    if (xTaskCreateWithCaps(commandTask, "codex_command", 8192, this, 1, &_task, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) _task = nullptr;
}
void CodexMonitorApp::submitText(const char *message) {
    if (_task || _awaiting_reply || message == nullptr || message[0] == '\0') return;
    if (_selected < 0 || _selected >= _task_count || !_tasks[_selected].controlled) {
        for (uint8_t i = 0; i < _task_count; ++i) {
            if (_tasks[i].controlled) {
                _selected = i;
                break;
            }
        }
    }
    if (_selected < 0 || _selected >= _task_count || !_tasks[_selected].controlled) return;
    std::snprintf(_pending_message, sizeof(_pending_message), "%s", message);
    std::snprintf(_pending_task_id, sizeof(_pending_task_id), "%s", _tasks[_selected].id);
    std::snprintf(_last_sent_task_id, sizeof(_last_sent_task_id), "%s", _tasks[_selected].id);
    std::snprintf(_last_sent_message, sizeof(_last_sent_message), "%s", _pending_message);
    closeInput();
    _awaiting_reply = true;
    std::snprintf(_status_text, sizeof(_status_text), "%s", "Sending message..."); _dirty = true; _content_dirty = true;
    showVoiceOverlay("Sending message...", 0x7EDFA7);
    if (xTaskCreateWithCaps(commandTask, "codex_command", 8192, this, 1, &_task, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) _task = nullptr;
}
void CodexMonitorApp::closeInput() {
    if (_input_panel) lv_obj_delete(_input_panel);
    _input_panel = _input_textarea = _input_sheet = _input_status_label = _input_keyboard = _input_ime = _input_candidates = _k9_mode_button = _full_mode_button = nullptr;
}
void CodexMonitorApp::showVoiceOverlay(const char *message, uint32_t color) {
    if (!_voice_overlay) {
        _voice_overlay = lv_obj_create(lv_layer_top());
        lv_obj_set_size(_voice_overlay, 250, 82);
        lv_obj_align(_voice_overlay, LV_ALIGN_CENTER, 0, -8);
        lv_obj_set_style_radius(_voice_overlay, 18, 0);
        lv_obj_set_style_bg_color(_voice_overlay, lv_color_hex(0x101722), 0);
        lv_obj_set_style_bg_opa(_voice_overlay, LV_OPA_90, 0);
        lv_obj_set_style_border_width(_voice_overlay, 1, 0);
        lv_obj_set_style_border_color(_voice_overlay, lv_color_hex(0x35547A), 0);
        lv_obj_set_style_pad_all(_voice_overlay, 12, 0);
        _voice_overlay_label = text(_voice_overlay, "", &lv_font_montserrat_16, color);
        lv_obj_set_width(_voice_overlay_label, LV_PCT(100));
        lv_obj_set_height(_voice_overlay_label, LV_SIZE_CONTENT);
        lv_label_set_long_mode(_voice_overlay_label, LV_LABEL_LONG_WRAP);
        lv_obj_center(_voice_overlay_label);
    }
    if (_voice_overlay_label) {
        lv_label_set_text(_voice_overlay_label, message && message[0] ? message : "Voice...");
        watch_display::apply_text_font(_voice_overlay_label, message && message[0] ? message : "Voice...", &lv_font_montserrat_16);
        lv_obj_set_style_text_color(_voice_overlay_label, lv_color_hex(color), 0);
    }
    lv_obj_move_foreground(_voice_overlay);
}
void CodexMonitorApp::closeVoiceOverlay() {
    if (_voice_overlay) lv_obj_delete(_voice_overlay);
    _voice_overlay = _voice_overlay_label = nullptr;
}
void CodexMonitorApp::showInput() {
    if (_input_panel || _selected < 0 || _selected >= _task_count || _task || _awaiting_reply) return;
    _input_panel = lv_obj_create(lv_layer_top());
    lv_obj_set_size(_input_panel, LV_PCT(100), LV_PCT(100));
    lv_obj_center(_input_panel);
    lv_obj_set_style_bg_color(_input_panel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(_input_panel, LV_OPA_50, 0);
    lv_obj_set_style_border_width(_input_panel, 0, 0);
    lv_obj_set_style_radius(_input_panel, 0, 0);
    lv_obj_set_style_pad_all(_input_panel, 0, 0);

    _input_sheet = lv_obj_create(_input_panel);
    lv_obj_set_width(_input_sheet, LV_PCT(100));
    lv_obj_set_height(_input_sheet, LV_PCT(100));
    lv_obj_align(_input_sheet, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(_input_sheet, lv_color_hex(0x111723), 0);
    lv_obj_set_style_bg_opa(_input_sheet, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_input_sheet, 1, 0);
    lv_obj_set_style_border_color(_input_sheet, lv_color_hex(0x344158), 0);
    lv_obj_set_style_radius(_input_sheet, 0, 0);
    lv_obj_set_style_pad_all(_input_sheet, 10, 0);
    // The phone status bar is a separate overlay.  Keep the input sheet's
    // controls below it when the sheet expands to the full screen.
    lv_obj_set_style_pad_top(_input_sheet, 42, 0);
    lv_obj_set_style_pad_row(_input_sheet, 7, 0);
    lv_obj_set_flex_flow(_input_sheet, LV_FLEX_FLOW_COLUMN);

    text(_input_sheet, "Quick reply", &lv_font_montserrat_14, 0xFFFFFF);
    _input_status_label = text(_input_sheet, "Voice: tap and speak 4s", &lv_font_montserrat_12, 0xAEB7C6);
    lv_obj_set_width(_input_status_label, LV_PCT(100));
    lv_obj_set_height(_input_status_label, 16);
    lv_label_set_long_mode(_input_status_label, LV_LABEL_LONG_DOT);
    auto *modes = lv_obj_create(_input_sheet);
    lv_obj_remove_style_all(modes); lv_obj_set_width(modes, LV_PCT(100)); lv_obj_set_height(modes, 30);
    lv_obj_set_flex_flow(modes, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_column(modes, 5, 0);
    auto add_mode = [&](const char *label, uint32_t color) -> lv_obj_t * {
        auto *button = lv_button_create(modes); lv_obj_set_size(button, LV_PCT(49), 28);
        lv_obj_set_style_radius(button, 7, 0); lv_obj_set_style_bg_color(button, lv_color_hex(color), 0); lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        lv_obj_add_event_cb(button, onKeyboardMode, LV_EVENT_CLICKED, this);
        lv_obj_center(text(button, label, &lv_font_montserrat_12, 0xFFFFFF)); return button;
    };
    _k9_mode_button = add_mode("9-key", 0x315C9E);
    _full_mode_button = add_mode("Full", 0x27303E);
    auto *phrases = lv_obj_create(_input_sheet);
    lv_obj_remove_style_all(phrases); lv_obj_set_width(phrases, LV_PCT(100)); lv_obj_set_height(phrases, 38);
    lv_obj_set_flex_flow(phrases, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_column(phrases, 5, 0);
    static constexpr const char *quick_phrases[] = {"是", "继续", "1", "2", "3"};
    for (uint8_t i = 0; i < 5; ++i) {
        auto *button = lv_button_create(phrases); lv_obj_set_size(button, LV_PCT(19), 36);
        lv_obj_set_style_radius(button, 7, 0); lv_obj_set_style_bg_color(button, lv_color_hex(0x273C5F), 0); lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        _phrase_contexts[i] = {this, quick_phrases[i]}; lv_obj_add_event_cb(button, onPhrase, LV_EVENT_CLICKED, &_phrase_contexts[i]);
        lv_obj_center(text(button, quick_phrases[i], &lv_font_montserrat_12, 0xFFFFFF));
    }

    _input_textarea = lv_textarea_create(_input_sheet);
    lv_obj_set_width(_input_textarea, LV_PCT(100));
    lv_obj_set_height(_input_textarea, 40);
    lv_textarea_set_one_line(_input_textarea, true);
    lv_textarea_set_max_length(_input_textarea, 180);
    lv_textarea_set_placeholder_text(_input_textarea, "Tap to type");
    lv_obj_set_style_text_font(_input_textarea, &lv_font_source_han_sans_sc_14_cjk, 0);
    lv_obj_add_event_cb(_input_textarea, onInputFocused, LV_EVENT_FOCUSED, this);
    lv_obj_add_event_cb(_input_textarea, onInputFocused, LV_EVENT_CLICKED, this);
    auto *actions = lv_obj_create(_input_sheet);
    lv_obj_remove_style_all(actions); lv_obj_set_width(actions, LV_PCT(100)); lv_obj_set_height(actions, 38);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_column(actions, 8, 0);
    auto add_action = [&](const char *label, uint32_t color, lv_event_cb_t callback) {
        auto *button = lv_button_create(actions); lv_obj_set_size(button, LV_PCT(31), 36);
        lv_obj_set_style_radius(button, 8, 0); lv_obj_set_style_bg_color(button, lv_color_hex(color), 0); lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, this); lv_obj_center(text(button, label, &lv_font_montserrat_14, 0xFFFFFF));
    };
    add_action("Cancel", 0x2B303B, onInputCancel); add_action("Voice", 0x2F7D5B, onVoice); add_action("Send", 0x315C9E, onInputSend);
#if LV_USE_KEYBOARD
    _input_keyboard = lv_keyboard_create(_input_sheet);
    lv_obj_set_width(_input_keyboard, LV_PCT(100)); lv_obj_set_height(_input_keyboard, 150);
    lv_obj_set_style_text_font(_input_keyboard, &lv_font_source_han_sans_sc_14_cjk, LV_PART_ITEMS);
    lv_keyboard_set_textarea(_input_keyboard, _input_textarea);
    lv_obj_add_event_cb(_input_keyboard, onKeyboardReady, LV_EVENT_READY, this);
#if LV_USE_IME_PINYIN
    _input_ime = lv_ime_pinyin_create(_input_sheet);
    lv_ime_pinyin_set_keyboard(_input_ime, _input_keyboard);
    lv_ime_pinyin_set_mode(_input_ime, LV_IME_PINYIN_MODE_K9);
    _input_candidates = lv_ime_pinyin_get_cand_panel(_input_ime);
    lv_obj_set_size(_input_candidates, LV_PCT(100), 34);
    lv_obj_set_style_text_font(_input_candidates, &lv_font_source_han_sans_sc_14_cjk, 0);
    // The IME reparents this panel next to the keyboard.  Mark it floating so
    // the sheet's flex layout cannot move it below or behind the keyboard.
    lv_obj_add_flag(_input_candidates, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_style_bg_color(_input_candidates, lv_color_hex(0x182235), 0);
    lv_obj_set_style_bg_opa(_input_candidates, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(_input_candidates, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_color(_input_candidates, lv_color_hex(0xFFFFFF), LV_PART_ITEMS);
    const auto checked_items = static_cast<lv_style_selector_t>(LV_PART_ITEMS) | static_cast<lv_style_selector_t>(LV_STATE_CHECKED);
    lv_obj_set_style_text_color(_input_candidates, lv_color_hex(0xFFFFFF), checked_items);
    lv_obj_set_style_bg_color(_input_candidates, lv_color_hex(0x315C9E), checked_items);
    lv_obj_set_style_bg_opa(_input_candidates, LV_OPA_COVER, checked_items);
    lv_obj_set_style_border_width(_input_candidates, 1, 0);
    lv_obj_set_style_border_color(_input_candidates, lv_color_hex(0x47648E), 0);
#endif
    lv_obj_add_flag(_input_keyboard, LV_OBJ_FLAG_HIDDEN);
#endif
}
void CodexMonitorApp::startVoiceInput() {
    if (_task || _awaiting_reply || _selected < 0 || _selected >= _task_count || !_tasks[_selected].controlled) return;
    if (!_input_panel) showInput();
    showVoiceOverlay("Preparing voice...", 0x7EDFA7);
    if (_input_status_label) {
        lv_label_set_text(_input_status_label, "Preparing voice...");
        lv_obj_set_style_text_color(_input_status_label, lv_color_hex(0x7EDFA7), 0);
    }
    if (!watch::wifi_is_connected()) {
        std::snprintf(_status_text, sizeof(_status_text), "%s", "WiFi offline");
        if (_input_status_label) {
            lv_label_set_text(_input_status_label, "WiFi offline");
            lv_obj_set_style_text_color(_input_status_label, lv_color_hex(0xFFB86B), 0);
        }
        showVoiceOverlay("WiFi offline", 0xFFB86B);
        _dirty = true;
        return;
    }
    std::snprintf(_pending_task_id, sizeof(_pending_task_id), "%s", _tasks[_selected].id);
    std::snprintf(_status_text, sizeof(_status_text), "%s", "Preparing voice...");
    _dirty = true;
    if (xTaskCreateWithCaps(voiceTask, "codex_voice", 12288, this, 1, &_task, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) _task = nullptr;
}
void CodexMonitorApp::commandTask(void *arg) {
    auto *app = static_cast<CodexMonitorApp *>(arg); if (!app) { vTaskDeleteWithCaps(nullptr); return; }
    char status[160] = {}; const int selected = app->_selected; char message[sizeof(app->_pending_message)] = {}; std::snprintf(message, sizeof(message), "%s", app->_pending_message);
    if (selected < 0 || selected >= app->_task_count || message[0] == '\0') std::snprintf(status, sizeof(status), "%s", "Invalid message");
    else {
        const auto token = login();
        const char *action = "reply";
        const char *body_text = message;
        if (is_stop_text(message)) {
            action = "stop";
        } else if (is_continue_text(message)) {
            action = "continue";
            body_text = "";
        }
        cJSON *payload_root = cJSON_CreateObject();
        cJSON_AddStringToObject(payload_root, "task_id", app->_tasks[selected].id);
        cJSON_AddStringToObject(payload_root, "action", action);
        cJSON_AddStringToObject(payload_root, "body", body_text);
        char *payload = cJSON_PrintUnformatted(payload_root);
        HttpBody body;
        const bool sent = !token.empty() && payload && request(WATCH_PRIVATE_DUDUSERVER_API_BASE_URL "/codex-bridge/commands", token.c_str(), body, payload);
        if (payload) cJSON_free(payload);
        cJSON_Delete(payload_root);
        if (!sent) std::snprintf(status, sizeof(status), "Send failed (HTTP %d)", body.status);
        else std::snprintf(status, sizeof(status), "%s", "Sent. Waiting for reply...");
    }
    if (app->_mutex && xSemaphoreTake(app->_mutex, pdMS_TO_TICKS(20)) == pdTRUE) { std::snprintf(app->_status_text, sizeof(app->_status_text), "%s", status); if (std::strncmp(status, "Sent.", 5) != 0) { app->_awaiting_reply = false; app->_content_dirty = true; } app->_dirty = true; xSemaphoreGive(app->_mutex); }
    app->_task = nullptr;
    // The worker claims commands asynchronously. Refresh after it has had time
    // to publish the compact reply, instead of leaving the watch on a spinner.
    app->_reply_poll_count = (status[0] != '\0' && std::strncmp(status, "Sent.", 5) == 0) ? 20 : 0;
    app->_auto_refresh_at_us = app->_reply_poll_count ? esp_timer_get_time() + 3000000 : 0;
    vTaskDeleteWithCaps(nullptr);
}
void CodexMonitorApp::voiceTask(void *arg) {
    auto *app = static_cast<CodexMonitorApp *>(arg);
    if (!app) { vTaskDeleteWithCaps(nullptr); return; }
    char status[160] = {};
    std::string token;
    if (!watch::wifi_is_connected()) {
        std::snprintf(status, sizeof(status), "%s", "WiFi offline");
    } else {
        token = login();
        if (token.empty()) {
            std::snprintf(status, sizeof(status), "%s", "Cloud login failed");
        } else {
            uint8_t *audio = static_cast<uint8_t *>(heap_caps_malloc(CODex_VOICE_BYTES + 44, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            size_t recorded = 0;
            if (!audio) {
                std::snprintf(status, sizeof(status), "%s", "Not enough memory for recording");
            } else if (watch::audio_mic_start(CODex_VOICE_SAMPLE_RATE, 1) != ESP_OK) {
                std::snprintf(status, sizeof(status), "%s", "Microphone unavailable");
                heap_caps_free(audio);
            } else {
                if (app->_mutex && xSemaphoreTake(app->_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                    std::snprintf(app->_status_text, sizeof(app->_status_text), "%s", "Speak now (4 seconds)...");
                    app->_dirty = true;
                    xSemaphoreGive(app->_mutex);
                }
                vTaskDelay(pdMS_TO_TICKS(350));
                while (recorded < CODex_VOICE_BYTES) {
                    size_t read = 0;
                    const size_t want = std::min(static_cast<size_t>(1024), CODex_VOICE_BYTES - recorded);
                    if (watch::audio_mic_read(audio + 44 + recorded, want, &read) != ESP_OK || read == 0) break;
                    recorded += read;
                }
                watch::audio_mic_stop();
                if (recorded < 16000) {
                    std::snprintf(status, sizeof(status), "%s", "Recording too short");
                } else {
                    write_wav_header(audio, recorded + 44);
                    if (app->_mutex && xSemaphoreTake(app->_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                        std::snprintf(app->_status_text, sizeof(app->_status_text), "%s", "Recognizing voice...");
                        app->_dirty = true;
                        xSemaphoreGive(app->_mutex);
                    }
                    HttpBody body;
                    if (!request_audio(token.c_str(), audio, recorded + 44, body)) {
                        describe_audio_error(body, status, sizeof(status));
                    } else {
                        cJSON *response = cJSON_Parse(body.data.c_str());
                        cJSON *transcript = response ? cJSON_GetObjectItemCaseSensitive(response, "transcript_text") : nullptr;
                        const char *text = (cJSON_IsString(transcript) && transcript->valuestring) ? transcript->valuestring : "";
                        if (text[0]) {
                            if (app->_mutex && xSemaphoreTake(app->_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                                std::snprintf(app->_voice_result_text, sizeof(app->_voice_result_text), "%s", text);
                                app->_voice_result_ready = true;
                                xSemaphoreGive(app->_mutex);
                            }
                            std::snprintf(status, sizeof(status), "%s", text);
                        } else {
                            std::snprintf(status, sizeof(status), "%s", "No speech recognized");
                        }
                        cJSON_Delete(response);
                    }
                }
                heap_caps_free(audio);
            }
        }
    }
    if (app->_mutex && xSemaphoreTake(app->_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        std::snprintf(app->_status_text, sizeof(app->_status_text), "%s", status);
        app->_dirty = true;
        xSemaphoreGive(app->_mutex);
    }
    app->_task = nullptr;
    vTaskDeleteWithCaps(nullptr);
}
void CodexMonitorApp::task(void *arg) {
    auto *app = static_cast<CodexMonitorApp *>(arg); if (!app) { vTaskDeleteWithCaps(nullptr); return; } const uint32_t generation = app->_generation; char status[160] = {}; auto *tasks = static_cast<TaskInfo *>(heap_caps_calloc(MAX_TASKS, sizeof(TaskInfo), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)); uint8_t task_count = 0; bool fetched = false;
    if (!tasks) { std::snprintf(status, sizeof(status), "%s", "No memory for task list"); }
    if (tasks && !watch::wifi_is_connected()) std::snprintf(status, sizeof(status), "%s", "WiFi offline");
    else if (tasks) { const auto token = login(); HttpBody body; if (token.empty() || !request(WATCH_PRIVATE_DUDUSERVER_API_BASE_URL "/codex-bridge/tasks?limit=8&compact=1", token.c_str(), body)) std::snprintf(status, sizeof(status), "Request failed (HTTP %d)", body.status);
    else { cJSON *root = cJSON_Parse(body.data.c_str()); cJSON *items = root ? cJSON_GetObjectItemCaseSensitive(root, "tasks") : nullptr; fetched = root != nullptr && cJSON_IsArray(items); std::snprintf(status, sizeof(status), "%s", fetched ? "Cloud Codex tasks" : "Invalid task response"); if (cJSON_IsArray(items)) { cJSON *item = nullptr; cJSON_ArrayForEach(item, items) { if (task_count >= MAX_TASKS) break; auto &out = tasks[task_count]; auto copy = [](char *dst, size_t size, cJSON *value) { std::snprintf(dst, size, "%s", cJSON_IsString(value) && value->valuestring ? value->valuestring : ""); }; copy(out.id, sizeof(out.id), cJSON_GetObjectItemCaseSensitive(item, "task_id")); copy(out.title, sizeof(out.title), cJSON_GetObjectItemCaseSensitive(item, "title")); copy(out.state, sizeof(out.state), cJSON_GetObjectItemCaseSensitive(item, "state")); copy(out.prompt, sizeof(out.prompt), cJSON_GetObjectItemCaseSensitive(item, "choice_prompt")); auto *controlled = cJSON_GetObjectItemCaseSensitive(item, "controlled"); out.controlled = cJSON_IsTrue(controlled); auto *options = cJSON_GetObjectItemCaseSensitive(item, "choice_options"); if (cJSON_IsArray(options)) { cJSON *option = nullptr; cJSON_ArrayForEach(option, options) { if (out.option_count >= 6) break; copy(out.options[out.option_count++], sizeof(out.options[0]), option); } } ++task_count; } } cJSON_Delete(root); } }
    if (app->_mutex && xSemaphoreTake(app->_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        if (generation == app->_generation) {
            std::snprintf(app->_status_text, sizeof(app->_status_text), "%s", status);
            if (fetched && tasks) {
                if (app->_awaiting_reply && app->_selected >= 0 && app->_selected < app->_task_count) {
                    const char *selected_id = app->_tasks[app->_selected].id;
                    const char *old_prompt = app->_tasks[app->_selected].prompt;
                    for (uint8_t i = 0; i < task_count; ++i) {
                        if (std::strcmp(tasks[i].id, selected_id) == 0 && std::strcmp(tasks[i].prompt, old_prompt) != 0 && tasks[i].prompt[0] != '\0') {
                            app->_awaiting_reply = false;
                            app->_auto_refresh_at_us = 0;
                            app->_reply_poll_count = 0;
                            app->_pending_message[0] = '\0';
                            app->_close_progress_overlay = true;
                            std::snprintf(app->_status_text, sizeof(app->_status_text), "%s", "Reply received");
                            break;
                        }
                    }
                }
                std::memcpy(app->_tasks, tasks, sizeof(app->_tasks));
                app->_task_count = task_count;
                app->_content_dirty = true;
            }
            app->_dirty = true;
        }
        xSemaphoreGive(app->_mutex);
    }
    if (tasks) {
        heap_caps_free(tasks);
    }
    app->_task = nullptr;
    vTaskDeleteWithCaps(nullptr);
}
void CodexMonitorApp::flush() {
    if (_close_progress_overlay) {
        _close_progress_overlay = false;
        closeVoiceOverlay();
    }
    if (_selected >= 0 && !_task && _detail_refresh_at_us != 0 && esp_timer_get_time() >= _detail_refresh_at_us) {
        _detail_refresh_at_us = esp_timer_get_time() + 3000000;
        requestRefresh();
    }
    if (_voice_result_ready && _input_textarea) {
        char text[sizeof(_voice_result_text)] = {};
        if (_mutex && xSemaphoreTake(_mutex, 0) == pdTRUE) {
            std::snprintf(text, sizeof(text), "%s", _voice_result_text);
            _voice_result_ready = false;
            _voice_result_text[0] = '\0';
            xSemaphoreGive(_mutex);
        }
        if (text[0]) {
            lv_textarea_set_text(_input_textarea, text);
            lv_textarea_set_cursor_pos(_input_textarea, LV_TEXTAREA_CURSOR_LAST);
        }
    }
    if (_auto_refresh_at_us != 0 && esp_timer_get_time() >= _auto_refresh_at_us && !_task) {
        if (_reply_poll_count > 0) {
            --_reply_poll_count;
            if (_reply_poll_count == 0) {
                _auto_refresh_at_us = 0;
                if (_mutex && xSemaphoreTake(_mutex, 0) == pdTRUE) { std::snprintf(_status_text, sizeof(_status_text), "%s", "No reply yet. Tap Refresh."); _awaiting_reply = false; _dirty = true; _content_dirty = true; xSemaphoreGive(_mutex); }
            } else {
                _auto_refresh_at_us = esp_timer_get_time() + 3000000;
            }
        } else {
            _auto_refresh_at_us = 0;
        }
        requestRefresh();
    }
    if (!_dirty || !_mutex || xSemaphoreTake(_mutex, 0) != pdTRUE) return;
    char s[sizeof(_status_text)]; std::snprintf(s, sizeof(s), "%s", _status_text); _dirty = false; xSemaphoreGive(_mutex);
    if (_status) { lv_label_set_text(_status, s); watch_display::apply_text_font(_status, s, &lv_font_montserrat_12); }
    const bool warn = std::strstr(s, "failed") || std::strstr(s, "offline") || std::strstr(s, "unavailable") || std::strstr(s, "short") || std::strstr(s, "No reply");
    if (_voice_overlay) {
        showVoiceOverlay(s, warn ? 0xFFB86B : 0xEAF2FF);
    }
    if (_input_status_label) {
        lv_label_set_text(_input_status_label, s);
        watch_display::apply_text_font(_input_status_label, s, &lv_font_montserrat_12);
        lv_obj_set_style_text_color(_input_status_label, lv_color_hex(warn ? 0xFFB86B : 0xAEB7C6), 0);
    }
    if (_content_dirty) {
        _content_dirty = false;
        if (_selected < 0) showList(); else showDetail();
    }
}
void CodexMonitorApp::showList()
{
    _selected = -1;
    _detail_refresh_at_us = 0;
    if (!_list_page || !_detail_page) return;
    lv_obj_clean(_list_page);
    lv_obj_clear_flag(_list_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(_detail_page, LV_OBJ_FLAG_HIDDEN);
    auto *quick = lv_obj_create(_list_page);
    lv_obj_remove_style_all(quick);
    lv_obj_set_width(quick, LV_PCT(100));
    lv_obj_set_height(quick, 38);
    lv_obj_set_flex_flow(quick, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(quick, 6, 0);
    small_button(quick, "Refresh", 0x315C9E, onRefresh, this, 92, 36);
    _phrase_contexts[8] = {this, "New"};
    small_button(quick, "New", 0x6A4C93, onPhrase, &_phrase_contexts[8], 76, 36);
    if (_task_count == 0) {
        auto *empty = text(_list_page, "No Codex sessions. Tap Refresh.", &lv_font_montserrat_14, 0xAEB7C6);
        lv_obj_set_width(empty, LV_PCT(100));
        return;
    }
    for (int pass = 0; pass < 2; ++pass) {
        for (uint8_t i = 0; i < _task_count; ++i) {
            if (_tasks[i].controlled != (pass == 0)) continue;
            auto *card = lv_button_create(_list_page);
            lv_obj_set_width(card, LV_PCT(100));
            lv_obj_set_height(card, 82);
            lv_obj_set_style_radius(card, 13, 0);
            lv_obj_set_style_bg_color(card, lv_color_hex(_tasks[i].controlled ? 0x172136 : 0x171B24), 0);
            lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
            lv_obj_set_style_pad_all(card, 9, 0);
            lv_obj_set_style_pad_row(card, 5, 0);
            lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
            _task_contexts[i] = {this, i};
            lv_obj_add_event_cb(card, onTask, LV_EVENT_CLICKED, &_task_contexts[i]);

            auto *title = text(card, _tasks[i].title[0] ? _tasks[i].title : "Codex session", &lv_font_montserrat_14, 0xFFFFFF);
            lv_obj_set_width(title, LV_PCT(100));
            lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);

            char meta[128] = {};
            const bool needs_choice = _tasks[i].option_count > 0;
            std::snprintf(meta, sizeof(meta), "%s  %s  %s",
                          _tasks[i].state[0] ? _tasks[i].state : "unknown",
                          _tasks[i].controlled ? "CONTROL" : "VIEW",
                          needs_choice ? "CHOICE" : "");
            auto *meta_label = text(card, meta, &lv_font_montserrat_12, state_color(_tasks[i].state));
            lv_obj_set_width(meta_label, LV_PCT(100));
            lv_label_set_long_mode(meta_label, LV_LABEL_LONG_DOT);

            const char *preview = _tasks[i].prompt[0] ? _tasks[i].prompt : "Open to view details.";
            auto *preview_label = text(card, preview, &lv_font_montserrat_12, 0xAEB7C6);
            lv_obj_set_width(preview_label, LV_PCT(100));
            lv_label_set_long_mode(preview_label, LV_LABEL_LONG_DOT);
        }
    }
}
void CodexMonitorApp::selectTask(int index) {
    if (index < 0 || index >= _task_count) return;
    _selected = index;
    _detail_refresh_at_us = esp_timer_get_time() + 1500000;
    showDetail();
}
void CodexMonitorApp::showDetail()
{
    if (!_detail_page || !_list_page || _selected < 0 || _selected >= _task_count) return;
    lv_obj_clean(_detail_page);
    lv_obj_add_flag(_list_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(_detail_page, LV_OBJ_FLAG_HIDDEN);

    const auto &task = _tasks[_selected];
    auto *header = lv_obj_create(_detail_page);
    lv_obj_remove_style_all(header);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, 30);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    auto *title = text(header, task.title[0] ? task.title : "Task", &lv_font_montserrat_14, 0xFFFFFF);
    lv_obj_set_width(title, LV_PCT(68));
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    auto *list_button = lv_button_create(header);
    lv_obj_set_size(list_button, 58, 28);
    lv_obj_set_style_radius(list_button, 8, 0);
    lv_obj_set_style_bg_color(list_button, lv_color_hex(0x2B303B), 0);
    lv_obj_set_style_bg_opa(list_button, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(list_button, onDetailBack, LV_EVENT_CLICKED, this);
    lv_obj_center(text(list_button, "List", &lv_font_montserrat_12, 0xFFFFFF));

    const std::string latest_reply = latest_codex_reply(task.prompt);
    auto *reply_card = lv_obj_create(_detail_page);
    lv_obj_set_width(reply_card, LV_PCT(100));
    lv_obj_set_height(reply_card, 44);
    lv_obj_set_style_radius(reply_card, 9, 0);
    lv_obj_set_style_bg_color(reply_card, lv_color_hex(0x183A32), 0);
    lv_obj_set_style_bg_opa(reply_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(reply_card, 0, 0);
    lv_obj_set_style_pad_all(reply_card, 7, 0);
    char reply_text[360] = {};
    if (!latest_reply.empty()) std::snprintf(reply_text, sizeof(reply_text), "Reply: %s", latest_reply.c_str());
    else std::snprintf(reply_text, sizeof(reply_text), "%s", _awaiting_reply ? "Reply: waiting..." : "Reply: no response yet");
    auto *reply_label = text(reply_card, reply_text, &lv_font_montserrat_14, 0xEAF2FF);
    lv_obj_set_width(reply_label, LV_PCT(100));
    lv_label_set_long_mode(reply_label, LV_LABEL_LONG_DOT);

    auto *history = lv_obj_create(_detail_page);
    lv_obj_set_width(history, LV_PCT(100));
    lv_obj_set_flex_grow(history, 1);
    lv_obj_set_style_radius(history, 10, 0);
    lv_obj_set_style_bg_color(history, lv_color_hex(0x11151C), 0);
    lv_obj_set_style_bg_opa(history, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(history, 0, 0);
    lv_obj_set_style_pad_all(history, 8, 0);
    lv_obj_set_style_pad_row(history, 6, 0);
    lv_obj_set_flex_flow(history, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(history, LV_DIR_VER);

    const char *transcript = task.prompt[0] ? task.prompt : "Codex: No response is requested.";
    char transcript_copy[sizeof(task.prompt)] = {};
    std::snprintf(transcript_copy, sizeof(transcript_copy), "%s", transcript);
    char *save = nullptr;
    for (char *line = strtok_r(transcript_copy, "\n", &save); line != nullptr; line = strtok_r(nullptr, "\n", &save)) {
        const bool is_user = std::strncmp(line, "你：", std::strlen("你：")) == 0;
        auto *bubble = lv_obj_create(history);
        lv_obj_set_width(bubble, LV_PCT(100));
        lv_obj_set_height(bubble, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(bubble, 8, 0);
        lv_obj_set_style_bg_color(bubble, lv_color_hex(is_user ? 0x214C86 : 0x202631), 0);
        lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(bubble, 0, 0);
        lv_obj_set_style_pad_all(bubble, 7, 0);
        auto *message = text(bubble, line, &lv_font_montserrat_14, 0xF2F5F8);
        lv_obj_set_width(message, LV_PCT(100));
        lv_obj_set_height(message, LV_SIZE_CONTENT);
        lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
    }
    // Give the tap immediate chat-style feedback while the bridge worker is
    // still claiming the command.  The next published transcript replaces it.
    if (_awaiting_reply && _pending_message[0] != '\0' && std::strstr(task.prompt, _pending_message) == nullptr) {
        auto *sent = lv_obj_create(history);
        lv_obj_set_width(sent, LV_PCT(100));
        lv_obj_set_height(sent, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(sent, 8, 0);
        lv_obj_set_style_bg_color(sent, lv_color_hex(0x214C86), 0);
        lv_obj_set_style_bg_opa(sent, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(sent, 0, 0);
        lv_obj_set_style_pad_all(sent, 7, 0);
        char sent_text[280] = {};
        std::snprintf(sent_text, sizeof(sent_text), "You: %s", _pending_message);
        auto *message = text(sent, sent_text, &lv_font_montserrat_14, 0xF2F5F8);
        lv_obj_set_width(message, LV_PCT(100));
        lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);

        auto *waiting = lv_obj_create(history);
        lv_obj_set_width(waiting, LV_PCT(100));
        lv_obj_set_height(waiting, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(waiting, 8, 0);
        lv_obj_set_style_bg_color(waiting, lv_color_hex(0x202631), 0);
        lv_obj_set_style_bg_opa(waiting, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(waiting, 0, 0);
        lv_obj_set_style_pad_all(waiting, 7, 0);
        text(waiting, "Codex: Working...", &lv_font_montserrat_14, 0xAEB7C6);
    } else if (_awaiting_reply && _auto_refresh_at_us == 0 && std::strstr(task.prompt, _pending_message) != nullptr) {
        _awaiting_reply = false;
        _pending_message[0] = '\0';
        closeVoiceOverlay();
    } else if (_awaiting_reply && _auto_refresh_at_us != 0) {
        auto *waiting = lv_obj_create(history);
        lv_obj_set_width(waiting, LV_PCT(100));
        lv_obj_set_height(waiting, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(waiting, 8, 0);
        lv_obj_set_style_bg_color(waiting, lv_color_hex(0x202631), 0);
        lv_obj_set_style_bg_opa(waiting, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(waiting, 0, 0);
        lv_obj_set_style_pad_all(waiting, 7, 0);
        char wait_text[96] = {};
        std::snprintf(wait_text, sizeof(wait_text), "Codex: Waiting... %u", static_cast<unsigned>(_reply_poll_count));
        text(waiting, wait_text, &lv_font_montserrat_14, 0xAEB7C6);
    }
    // The bubble heights are resolved during the next layout pass.  Queue the
    // scroll after that pass so refreshes always reveal the newest message.
    lv_async_call(scroll_history_to_latest, history);

    auto *actions = lv_obj_create(_detail_page);
    lv_obj_remove_style_all(actions);
    lv_obj_set_width(actions, LV_PCT(100));
    lv_obj_set_height(actions, 84);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(actions, 5, 0);
    lv_obj_set_style_pad_row(actions, 4, 0);
    lv_obj_set_scroll_dir(actions, LV_DIR_NONE);
    lv_obj_set_scrollbar_mode(actions, LV_SCROLLBAR_MODE_OFF);

    if (!task.controlled) {
        small_button(actions, "Refresh", 0x315C9E, onRefresh, this, 96, 40);
        small_button(actions, "List", 0x2B303B, onDetailBack, this, 82, 40);
        return;
    }

    if (task.option_count > 0) {
        small_button(actions, "Refresh", 0x315C9E, onRefresh, this, 78, 38);
        bool pending_shown = transcript_contains_message(task.prompt, _pending_message) || transcript_contains_message(task.prompt, _last_sent_message);
        if (!pending_shown && _last_sent_task_id[0] != '\0' && std::strcmp(_last_sent_task_id, task.id) == 0 && _last_sent_message[0] != '\0') {
            auto *sent = lv_obj_create(history);
            lv_obj_set_width(sent, LV_PCT(100));
            lv_obj_set_height(sent, LV_SIZE_CONTENT);
            lv_obj_set_style_radius(sent, 8, 0);
            lv_obj_set_style_bg_color(sent, lv_color_hex(0x214C86), 0);
            lv_obj_set_style_bg_opa(sent, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(sent, 0, 0);
            lv_obj_set_style_pad_all(sent, 7, 0);
            char sent_text[280] = {};
            std::snprintf(sent_text, sizeof(sent_text), "你：%s", _last_sent_message);
            auto *message = text(sent, sent_text, &lv_font_source_han_sans_sc_14_cjk, 0xF2F5F8);
            lv_obj_set_width(message, LV_PCT(100));
            lv_obj_set_height(message, LV_SIZE_CONTENT);
            lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
        }
        static constexpr const char *fixed_phrases[] = {"是", "继续", "1", "2", "3", "停止"};
        static constexpr const char *fixed_labels[] = {"Yes", "Continue", "1", "2", "3", "Stop"};
        static constexpr uint32_t fixed_colors[] = {0x315C9E, 0x315C9E, 0x273C5F, 0x273C5F, 0x273C5F, 0x82313B};
        for (uint8_t i = 0; i < 6; ++i) {
            _phrase_contexts[i] = {this, fixed_phrases[i]};
            small_button(actions, fixed_labels[i], fixed_colors[i], onPhrase, &_phrase_contexts[i], i == 1 ? 92 : 54, 38);
        }
        small_button(actions, "Voice", 0x2F7D5B, onVoice, this, 70, 38);
        small_button(actions, "Input", 0x2F7D5B, onInput, this, 70, 38);
        return;
    }

    static constexpr const char *common_phrases[] = {"继续", "1", "2", "3", "停止"};
    static constexpr const char *common_labels[] = {"Continue", "1", "2", "3", "Stop"};
    static constexpr uint32_t common_colors[] = {0x315C9E, 0x273C5F, 0x273C5F, 0x273C5F, 0x82313B};
    small_button(actions, "Refresh", 0x315C9E, onRefresh, this, 78, 38);
    bool pending_shown = transcript_contains_message(task.prompt, _pending_message) || transcript_contains_message(task.prompt, _last_sent_message);
    if (!pending_shown && _last_sent_task_id[0] != '\0' && std::strcmp(_last_sent_task_id, task.id) == 0 && _last_sent_message[0] != '\0') {
        auto *sent = lv_obj_create(history);
        lv_obj_set_width(sent, LV_PCT(100));
        lv_obj_set_height(sent, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(sent, 8, 0);
        lv_obj_set_style_bg_color(sent, lv_color_hex(0x214C86), 0);
        lv_obj_set_style_bg_opa(sent, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(sent, 0, 0);
        lv_obj_set_style_pad_all(sent, 7, 0);
        char sent_text[280] = {};
        std::snprintf(sent_text, sizeof(sent_text), "你：%s", _last_sent_message);
        auto *message = text(sent, sent_text, &lv_font_source_han_sans_sc_14_cjk, 0xF2F5F8);
        lv_obj_set_width(message, LV_PCT(100));
        lv_obj_set_height(message, LV_SIZE_CONTENT);
        lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
    }
    for (uint8_t i = 0; i < 5; ++i) {
        _phrase_contexts[i] = {this, common_phrases[i]};
        small_button(actions, common_labels[i], common_colors[i], onPhrase, &_phrase_contexts[i], i == 0 ? 92 : 54, 38);
    }
    small_button(actions, "Voice", 0x2F7D5B, onVoice, this, 70, 38);
    small_button(actions, "Input", 0x2F7D5B, onInput, this, 70, 38);
}
ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, CodexMonitorApp, APP_NAME, []() { return std::shared_ptr<CodexMonitorApp>(CodexMonitorApp::requestInstance(), [](CodexMonitorApp *) {}); })
} // namespace esp_brookesia::apps
