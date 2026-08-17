#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "systems/phone/esp_brookesia_phone_app.hpp"
#include "watch_audio.hpp"

namespace esp_brookesia::apps {

class MusicPlayerApp: public systems::phone::App {
public:
    static MusicPlayerApp *requestInstance();
    ~MusicPlayerApp() override = default;

protected:
    MusicPlayerApp();

    bool run(void) override;
    bool back(void) override;
    bool close(void) override;

private:
    struct CloudMusicTrack {
        char title[64];
        char artist[48];
        char url[192];
        char filename[64];
        size_t size;
        bool downloaded;
    };

    struct RowAction {
        MusicPlayerApp *app;
        int index;
        bool cloud;
    };

    enum class CloudOperation {
        FetchList,
        DownloadTrack,
    };

    enum class ViewMode {
        List,
        Player,
    };

    static void onBackClicked(lv_event_t *event);
    static void onPlayClicked(lv_event_t *event);
    static void onStopClicked(lv_event_t *event);
    static void onPrevClicked(lv_event_t *event);
    static void onNextClicked(lv_event_t *event);
    static void onScanClicked(lv_event_t *event);
    static void onLocalPlayClicked(lv_event_t *event);
    static void onCloudActionClicked(lv_event_t *event);
    static void onCloudRefreshClicked(lv_event_t *event);
    static void onListClicked(lv_event_t *event);
    static void onPlayerDownloadClicked(lv_event_t *event);
    static void onVolumeChanged(lv_event_t *event);
    static void onTimer(lv_timer_t *timer);
    static void cloudTaskEntry(void *arg);
    static void playTaskEntry(void *arg);

    void scanDirAppend(const char *dir);
    void scanTracks();
    void playCurrent();
    void playIndex(int index);
    void selectLocalTrack(int index);
    void selectCloudTrack(int index);
    void startLocalPlay(int index);
    void runLocalPlay();
    void selectDelta(int delta);
    void showListPage();
    void showPlayerPage();
    void refresh();
    void rebuildList();
    void setStatus(const char *text, uint32_t color = 0xAEB7C6);
    void startCloudFetch();
    void startCloudDownload(int index, bool play_after_download = false);
    void startCloudTask(CloudOperation op, int index);
    void runCloudFetch();
    void runCloudDownload(int index);
    void setCloudState(bool running, int percent, const char *status);
    void flushCloudState();
    bool cloudTrackLocalPath(const CloudMusicTrack &track, char *path, size_t path_size) const;
    bool cloudTrackDownloaded(const CloudMusicTrack &track) const;
    int findLocalTrackByPath(const char *path) const;
    RowAction *nextRowAction(int index, bool cloud);

    static MusicPlayerApp *_instance;
    lv_obj_t *_root = nullptr;
    lv_obj_t *_list_page = nullptr;
    lv_obj_t *_player_page = nullptr;
    lv_obj_t *_list = nullptr;
    lv_obj_t *_track_label = nullptr;
    lv_obj_t *_status_label = nullptr;
    lv_obj_t *_source_label = nullptr;
    lv_obj_t *_volume_label = nullptr;
    lv_obj_t *_play_label = nullptr;
    lv_obj_t *_play_button = nullptr;
    lv_obj_t *_player_title_label = nullptr;
    lv_obj_t *_player_source_label = nullptr;
    lv_obj_t *_player_status_label = nullptr;
    lv_obj_t *_player_download_button = nullptr;
    lv_obj_t *_player_download_label = nullptr;
    lv_obj_t *_cloud_refresh_button = nullptr;
    lv_obj_t *_volume_slider = nullptr;
    lv_timer_t *_timer = nullptr;
    TaskHandle_t _cloud_task = nullptr;
    TaskHandle_t _play_task = nullptr;
    SemaphoreHandle_t _state_mutex = nullptr;
    watch::AudioMusicTrack _tracks[watch::AUDIO_MUSIC_MAX_TRACKS] = {};
    CloudMusicTrack _cloud_tracks[16] = {};
    RowAction _row_actions[watch::AUDIO_MUSIC_MAX_TRACKS + 16] = {};
    size_t _track_count = 0;
    size_t _cloud_track_count = 0;
    size_t _row_action_count = 0;
    int _current_index = 0;
    int _current_cloud_index = -1;
    int _play_task_index = -1;
    char _play_task_path[160] = {};
    int _cloud_task_index = -1;
    CloudOperation _cloud_operation = CloudOperation::FetchList;
    char _cloud_token[384] = {};
    char _cloud_status[128] = {};
    int _cloud_percent = 0;
    bool _cloud_running = false;
    bool _cloud_dirty = false;
    bool _list_dirty = false;
    bool _current_is_cloud = false;
    bool _download_play_after = false;
    bool _download_play_pending = false;
    char _download_play_path[160] = {};
    ViewMode _view_mode = ViewMode::List;
};

} // namespace esp_brookesia::apps
