#pragma once

#include <cstddef>

#include "esp_err.h"

namespace watch {

constexpr const char *AUDIO_MUSIC_DEFAULT_DIR = "/sdcard/music";
constexpr size_t AUDIO_MUSIC_MAX_TRACKS = 32;

struct AudioMusicTrack {
    char path[160];
    char title[96];
    size_t size;
};

esp_err_t audio_init();
esp_err_t audio_set_volume(int volume);
int audio_get_volume();
esp_err_t audio_play_test_tone();
esp_err_t audio_play_test_tone_async();
esp_err_t audio_play_test_music();
esp_err_t audio_play_test_music_async();
esp_err_t audio_test_stop();
bool audio_test_is_playing();
esp_err_t audio_mic_start(int sample_rate, int channels);
esp_err_t audio_mic_read(void *buffer, size_t byte_count, size_t *bytes_read);
esp_err_t audio_mic_stop();
esp_err_t audio_music_init();
esp_err_t audio_music_scan(const char *dir, AudioMusicTrack *tracks, size_t max_tracks, size_t *track_count);
esp_err_t audio_music_play_file(const char *path);
esp_err_t audio_music_pause();
esp_err_t audio_music_resume();
esp_err_t audio_music_stop();
bool audio_music_is_playing();
bool audio_music_is_paused();
void audio_status_text(char *buffer, size_t buffer_size);
void audio_music_status_text(char *buffer, size_t buffer_size);

} // namespace watch
