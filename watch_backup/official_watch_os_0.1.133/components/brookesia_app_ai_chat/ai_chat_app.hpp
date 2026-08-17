#pragma once

#include <atomic>
#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "systems/phone/esp_brookesia_phone_app.hpp"

namespace esp_brookesia::apps {

class AiChatApp : public systems::phone::App {
public:
    static AiChatApp *requestInstance();
    static void requestOpenLiveOnNextRun();

protected:
    AiChatApp();
    bool run(void) override;
    bool back(void) override;

private:
    static constexpr uint8_t MAX_MESSAGES = 8;
    struct Message {
        bool user = false;
        char text[321] = {};
    };

    static void onBack(lv_event_t *event);
    static void onInput(lv_event_t *event);
    static void onVoice(lv_event_t *event);
    static void onLive(lv_event_t *event);
    static void onLiveTalk(lv_event_t *event);
    static void onLiveClose(lv_event_t *event);
    static void onSend(lv_event_t *event);
    static void onCancel(lv_event_t *event);
    static void onKeyboardReady(lv_event_t *event);
    static void onTimer(lv_timer_t *timer);
    static void chatTask(void *arg);
    static void realtimeTask(void *arg);

    void showInput();
    void closeInput();
    void submitText();
    void startVoice();
    void showLive();
    void closeLive();
    void startLiveVoice();
    void addMessage(bool user, const char *text);
    void refreshUi();
    void rebuildMessages();

    static AiChatApp *_instance;
    static bool _open_live_on_next_run;
    SemaphoreHandle_t _mutex = nullptr;
    TaskHandle_t _task = nullptr;
    lv_timer_t *_timer = nullptr;
    lv_obj_t *_status_label = nullptr;
    lv_obj_t *_messages_page = nullptr;
    lv_obj_t *_input_panel = nullptr;
    lv_obj_t *_input_textarea = nullptr;
    lv_obj_t *_input_keyboard = nullptr;
    lv_obj_t *_input_ime = nullptr;
    lv_obj_t *_input_candidates = nullptr;
    lv_obj_t *_live_panel = nullptr;
    lv_obj_t *_live_status_label = nullptr;
    lv_obj_t *_live_transcript_label = nullptr;
    lv_obj_t *_live_reply_label = nullptr;
    Message _messages[MAX_MESSAGES] = {};
    uint8_t _message_count = 0;
    char _pending_message[241] = {};
    char _status[96] = "Connect WiFi, then send a message";
    char _live_transcript[321] = {};
    char _live_reply[321] = {};
    bool _running = false;
    bool _voice_request = false;
    bool _live_request = false;
    std::atomic<bool> _live_capturing{false};
    std::atomic<bool> _live_stop_requested{false};
    bool _dirty = true;
    uint32_t _generation = 0;
};

} // namespace esp_brookesia::apps
