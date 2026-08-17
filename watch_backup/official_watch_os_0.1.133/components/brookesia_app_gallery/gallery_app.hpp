#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "systems/phone/esp_brookesia_phone_app.hpp"

namespace esp_brookesia::apps {

class GalleryApp : public systems::phone::App {
public:
    static GalleryApp *requestInstance();
    ~GalleryApp() override = default;

protected:
    GalleryApp();
    bool run(void) override;
    bool back(void) override;

private:
    static constexpr size_t MAX_CLOUD_ITEMS = 8;
    static constexpr size_t MAX_LOCAL_ITEMS = 16;

    enum class View : uint8_t { Photos, Videos, CloudPhotos, CloudVideos };
    enum class CloudAction : uint8_t { Open, Save };

    struct CloudItem {
        int id = 0;
        bool video = false;
        uint16_t width = 128;
        uint16_t height = 128;
        uint8_t fps = 6;
        size_t size_bytes = 0;
        size_t audio_size_bytes = 0;
        char title[80] = {};
        char download_url[360] = {};
        char audio_download_url[360] = {};
    };

    struct ActionBinding {
        GalleryApp *app = nullptr;
        uint8_t index = 0;
        CloudAction action = CloudAction::Open;
    };

    struct LocalItem {
        bool video = false;
        uint16_t width = 128;
        uint16_t height = 128;
        uint8_t fps = 6;
        char path[180] = {};
        char audio_path[180] = {};
        char title[80] = {};
    };

    struct LocalActionBinding {
        GalleryApp *app = nullptr;
        uint8_t index = 0;
    };

    struct DeleteConfirmBinding {
        GalleryApp *app = nullptr;
        bool confirmed = false;
        bool video = false;
    };

    static void onPhotosClicked(lv_event_t *event);
    static void onVideosClicked(lv_event_t *event);
    static void onRefreshClicked(lv_event_t *event);
    static void onBackClicked(lv_event_t *event);
    static void onCloudActionClicked(lv_event_t *event);
    static void onLocalOpenClicked(lv_event_t *event);
    static void onLocalSelectModeClicked(lv_event_t *event);
    static void onLocalSelectionChanged(lv_event_t *event);
    static void onDeleteSelectedLocalMediaClicked(lv_event_t *event);
    static void onDeleteConfirmClicked(lv_event_t *event);
    static void onViewerBackClicked(lv_event_t *event);
    static void listTask(void *arg);
    static void downloadTask(void *arg);
    static void onTimer(lv_timer_t *timer);
    static void onVideoTimer(lv_timer_t *timer);

    void refresh();
    void requestRefresh();
    void flushAsyncUi();
    void setView(View view);
    void rebuildMediaList();
    void rebuildLocalMediaList(bool video);
    void startCloudAction(uint8_t index, CloudAction action);
    void openLocalMedia(uint8_t index);
    void toggleLocalSelectMode(bool video);
    void toggleLocalSelection(uint8_t index, bool video);
    void showDeleteSelectedLocalMediaConfirm(bool video);
    void deleteSelectedLocalMedia(bool video);
    size_t selectedLocalMediaCount(bool video) const;
    void showMedia(const char *path, bool video, uint16_t width, uint16_t height, uint8_t fps, const char *title,
                   const char *audio_path = nullptr);
    void closeViewer();
    void nextVideoFrame();
    bool startVideoAudio();
    bool ensureFrameBuffer(size_t bytes);
    bool cacheVideoFrames(size_t bytes);
    void releaseVideoCache();
    void updateAsyncStatus(const char *status);
    void updateAsyncProgress(const char *phase, int percent, int64_t received, int64_t total, bool active);
    bool downloadRemoteFile(const std::string &url, const std::string &token, const char *temp_path,
                            const char *phase, int progress_start, int progress_span, size_t max_bytes);

    static GalleryApp *_instance;
    View _view = View::Photos;
    lv_obj_t *_status_label = nullptr;
    lv_obj_t *_progress_bar = nullptr;
    lv_obj_t *_list_label = nullptr;
    lv_obj_t *_media_list = nullptr;
    lv_obj_t *_delete_confirm = nullptr;
    TaskHandle_t _list_task = nullptr;
    TaskHandle_t _download_task = nullptr;
    SemaphoreHandle_t _mutex = nullptr;
    CloudItem _cloud_items[MAX_CLOUD_ITEMS] = {};
    ActionBinding _action_bindings[MAX_CLOUD_ITEMS * 2] = {};
    size_t _cloud_item_count = 0;
    LocalItem _local_items[MAX_LOCAL_ITEMS] = {};
    LocalActionBinding _local_action_bindings[MAX_LOCAL_ITEMS] = {};
    DeleteConfirmBinding _delete_confirm_bindings[2] = {};
    size_t _local_item_count = 0;
    uint32_t _local_video_selection = 0;
    uint32_t _local_photo_selection = 0;
    bool _local_video_select_mode = false;
    bool _local_photo_select_mode = false;
    CloudItem _download_item = {};
    CloudAction _download_action = CloudAction::Open;
    bool _preview_pending = false;
    bool _preview_video = false;
    uint16_t _preview_width = 128;
    uint16_t _preview_height = 128;
    uint8_t _preview_fps = 6;
    char _preview_path[180] = {};
    char _preview_audio_path[180] = {};
    char _preview_title[80] = {};
    char _status_text[560] = "Checking SD card...";
    char _list_text[1800] = "Loading media list...";
    int _progress_percent = 0;
    bool _progress_active = false;
    bool _dirty = true;
    bool _items_dirty = true;
    uint32_t _generation = 0;
    lv_timer_t *_timer = nullptr;

    lv_obj_t *_viewer = nullptr;
    lv_obj_t *_viewer_image = nullptr;
    lv_obj_t *_viewer_detail = nullptr;
    lv_timer_t *_video_timer = nullptr;
    FILE *_video_file = nullptr;
    uint8_t *_frame_buffer = nullptr;
    size_t _frame_capacity = 0;
    uint8_t *_video_cache = nullptr;
    size_t _video_cache_size = 0;
    lv_image_dsc_t _frame_image = {};
    uint16_t _video_width = 128;
    uint16_t _video_height = 128;
    uint8_t _video_fps = 6;
    unsigned _video_frame_index = 0;
    unsigned _video_frame_count = 0;
    char _video_audio_path[180] = {};
};

} // namespace esp_brookesia::apps
