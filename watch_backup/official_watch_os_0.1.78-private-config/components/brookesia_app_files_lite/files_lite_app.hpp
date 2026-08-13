#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"

namespace esp_brookesia::apps {

class FilesLiteApp: public systems::phone::App {
public:
    static FilesLiteApp *requestInstance();
    ~FilesLiteApp() override = default;

protected:
    FilesLiteApp();

    bool run(void) override;
    bool back(void) override;

private:
    static void onBackClicked(lv_event_t *event);
    static void onRefreshClicked(lv_event_t *event);
    static void onInitSdClicked(lv_event_t *event);
    static void onMusicClicked(lv_event_t *event);
    static void onLogsClicked(lv_event_t *event);
    static void onLittleFsClicked(lv_event_t *event);
    void refresh();

    static FilesLiteApp *_instance;
    enum class FileView {
        Overview,
        SdMusic,
        SdLogs,
        LittleFs,
    };
    FileView _view = FileView::Overview;
    lv_obj_t *_status_label = nullptr;
    lv_obj_t *_message_label = nullptr;
    lv_obj_t *_list_label = nullptr;
};

} // namespace esp_brookesia::apps
