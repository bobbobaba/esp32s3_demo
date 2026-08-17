#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "systems/phone/esp_brookesia_phone_app.hpp"

namespace esp_brookesia::apps {
class CodexMonitorApp : public systems::phone::App {
public:
    static CodexMonitorApp *requestInstance();
protected:
    CodexMonitorApp();
    bool run(void) override;
    bool back(void) override;
private:
    static constexpr int MAX_TASKS = 8;
    struct TaskInfo {
        char id[97] = {};
        char title[161] = {};
        char state[33] = {};
        char prompt[601] = {};
        char options[6][161] = {};
        uint8_t option_count = 0;
        bool controlled = false;
    };
    struct TaskCardContext { CodexMonitorApp *app = nullptr; int index = -1; };
    struct ChoiceContext { CodexMonitorApp *app = nullptr; uint8_t option = 0; };
    struct PhraseContext { CodexMonitorApp *app = nullptr; const char *text = nullptr; };
    static void onRefresh(lv_event_t *event);
    static void onBack(lv_event_t *event);
    static void onTask(lv_event_t *event);
    static void onDetailBack(lv_event_t *event);
    static void onChoice(lv_event_t *event);
    static void onInput(lv_event_t *event);
    static void onInputSend(lv_event_t *event);
    static void onInputCancel(lv_event_t *event);
    static void onKeyboardReady(lv_event_t *event);
    static void onInputFocused(lv_event_t *event);
    static void onKeyboardMode(lv_event_t *event);
    static void task(void *arg);
    static void onTimer(lv_timer_t *timer);
    void requestRefresh();
    void flush();
    void selectTask(int index);
    void showList();
    void showDetail();
    void submitChoice(uint8_t option);
    void showInput();
    void closeInput();
    void showVoiceOverlay(const char *message, uint32_t color = 0xEAF2FF);
    void closeVoiceOverlay();
    void submitText(const char *message);
    static void commandTask(void *arg);
    static CodexMonitorApp *_instance;
    lv_obj_t *_status = nullptr;
    lv_obj_t *_list_page = nullptr;
    lv_obj_t *_detail_page = nullptr;
    lv_timer_t *_timer = nullptr;
    TaskHandle_t _task = nullptr;
    SemaphoreHandle_t _mutex = nullptr;
    char _status_text[160] = "Tap Refresh to load Codex tasks";
    TaskInfo _tasks[MAX_TASKS] = {};
    TaskCardContext _task_contexts[MAX_TASKS] = {};
    ChoiceContext _choice_contexts[6] = {};
    PhraseContext _phrase_contexts[12] = {};
    uint8_t _task_count = 0;
    bool _dirty = true;
    // Status updates are frequent while a cloud command is running.  Keep them
    // separate from task content so the chat view is not rebuilt and scrolled.
    bool _content_dirty = true;
    uint32_t _generation = 0;
    int _selected = -1;
    uint8_t _pending_option = 0;
    char _pending_message[241] = {};
    char _pending_task_id[97] = {};
    char _last_sent_message[241] = {};
    char _last_sent_task_id[97] = {};
    char _voice_result_text[241] = {};
    bool _voice_result_ready = false;
    bool _awaiting_reply = false;
    bool _close_progress_overlay = false;
    int64_t _auto_refresh_at_us = 0;
    int64_t _detail_refresh_at_us = 0;
    uint8_t _reply_poll_count = 0;
    lv_obj_t *_input_panel = nullptr;
    lv_obj_t *_input_textarea = nullptr;
    lv_obj_t *_input_sheet = nullptr;
    lv_obj_t *_input_status_label = nullptr;
    lv_obj_t *_input_keyboard = nullptr;
    lv_obj_t *_input_ime = nullptr;
    lv_obj_t *_input_candidates = nullptr;
    lv_obj_t *_k9_mode_button = nullptr;
    lv_obj_t *_full_mode_button = nullptr;
    lv_obj_t *_voice_overlay = nullptr;
    lv_obj_t *_voice_overlay_label = nullptr;
    static void onPhrase(lv_event_t *event);
    static void onVoice(lv_event_t *event);
    static void voiceTask(void *arg);
    void startVoiceInput();
};
} // namespace esp_brookesia::apps
