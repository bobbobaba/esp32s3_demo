#include "watch_audio.hpp"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <errno.h>
#include <strings.h>
#include <sys/stat.h>

#include "audio_player.h"
#include "bsp/esp-bsp.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace watch {
namespace {

constexpr const char *TAG = "watch_audio";
constexpr int SAMPLE_RATE = 22050;
constexpr int TONE_HZ = 880;
constexpr int TONE_MS = 350;
constexpr int MUSIC_SAMPLE_RATE = 16000;
constexpr int MUSIC_MS = 6000;

esp_codec_dev_handle_t s_speaker = nullptr;
esp_codec_dev_handle_t s_microphone = nullptr;
int s_volume = 80;
esp_err_t s_last_err = ESP_OK;
esp_err_t s_mic_last_err = ESP_OK;
esp_err_t s_music_last_err = ESP_OK;
bool s_speaker_open = false;
bool s_mic_open = false;
bool s_stream_open = false;
bool s_mic_init_attempted = false;
bool s_music_player_init = false;
volatile bool s_music_playing = false;
volatile bool s_music_paused = false;
volatile bool s_test_audio_playing = false;
volatile bool s_test_audio_stop_requested = false;
TaskHandle_t s_test_audio_task = nullptr;
char s_music_path[sizeof(AudioMusicTrack::path)] = {};

enum class TestAudioMode {
    Tone,
    Music,
};

bool is_audio_file(const char *name)
{
    if (name == nullptr) {
        return false;
    }
    const char *dot = std::strrchr(name, '.');
    if (dot == nullptr) {
        return false;
    }
    return (strcasecmp(dot, ".mp3") == 0) || (strcasecmp(dot, ".wav") == 0);
}

void make_title_from_name(const char *name, char *title, size_t title_size)
{
    if ((title == nullptr) || (title_size == 0)) {
        return;
    }
    if (name == nullptr) {
        std::snprintf(title, title_size, "Unknown");
        return;
    }
    size_t len = 0;
    while ((name[len] != '\0') && (len + 1 < title_size)) {
        title[len] = name[len];
        ++len;
    }
    title[len] = '\0';
    for (size_t i = len; i > 0; --i) {
        if (title[i - 1] == '.') {
            title[i - 1] = '\0';
            break;
        }
    }
}

bool join_path(char *out, size_t out_size, const char *dir, const char *name)
{
    if ((out == nullptr) || (out_size == 0) || (dir == nullptr) || (name == nullptr)) {
        return false;
    }
    const size_t dir_len = std::strlen(dir);
    const size_t name_len = std::strlen(name);
    const bool has_slash = (dir_len > 0) && (dir[dir_len - 1] == '/');
    const size_t need = dir_len + (has_slash ? 0 : 1) + name_len + 1;
    if (need > out_size) {
        return false;
    }
    std::memcpy(out, dir, dir_len);
    size_t pos = dir_len;
    if (!has_slash) {
        out[pos++] = '/';
    }
    std::memcpy(out + pos, name, name_len + 1);
    return true;
}

esp_err_t player_mute_cb(AUDIO_PLAYER_MUTE_SETTING setting)
{
    ESP_RETURN_ON_ERROR(audio_init(), TAG, "audio init failed");
    if (!s_speaker_open) {
        return ESP_OK;
    }
    esp_err_t ret = esp_codec_dev_set_out_mute(s_speaker, setting == AUDIO_PLAYER_MUTE);
    if ((ret == ESP_OK) && (setting == AUDIO_PLAYER_UNMUTE)) {
        ret = esp_codec_dev_set_out_vol(s_speaker, s_volume);
    }
    return ret;
}

esp_err_t player_write_cb(void *audio_buffer, size_t len, size_t *bytes_written, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (bytes_written != nullptr) {
        *bytes_written = 0;
    }
    ESP_RETURN_ON_ERROR(audio_init(), TAG, "audio init failed");
    int ret = esp_codec_dev_write(s_speaker, audio_buffer, len);
    if (ret != 0) {
        s_music_last_err = ESP_FAIL;
        ESP_LOGW(TAG, "speaker write failed: ret=%d len=%u", ret, static_cast<unsigned>(len));
        return s_music_last_err;
    }
    if (bytes_written != nullptr) {
        *bytes_written = len;
    }
    return ESP_OK;
}

esp_err_t player_clk_set_cb(uint32_t rate, uint32_t bits_cfg, i2s_slot_mode_t channel)
{
    ESP_RETURN_ON_ERROR(audio_init(), TAG, "audio init failed");
    ESP_LOGI(TAG, "music clk set: rate=%u bits=%u channel=%u",
             static_cast<unsigned>(rate),
             static_cast<unsigned>(bits_cfg),
             static_cast<unsigned>(channel));
    audio_mic_stop();
    if ((s_speaker != nullptr) && s_speaker_open) {
        esp_codec_dev_close(s_speaker);
        s_speaker_open = false;
    }
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = static_cast<uint8_t>(bits_cfg),
        .channel = static_cast<uint8_t>(channel),
        .channel_mask = 0,
        .sample_rate = rate,
        .mclk_multiple = 0,
    };
    int ret = esp_codec_dev_open(s_speaker, &fs);
    if (ret != 0) {
        s_music_last_err = ESP_FAIL;
        ESP_LOGW(TAG, "speaker open failed: %d", ret);
        return s_music_last_err;
    }
    s_speaker_open = true;
    ret = esp_codec_dev_set_out_vol(s_speaker, s_volume);
    if (ret != 0) {
        s_music_last_err = ESP_FAIL;
        ESP_LOGW(TAG, "speaker set volume failed: %d", ret);
        return s_music_last_err;
    }
    ret = esp_codec_dev_set_out_mute(s_speaker, false);
    if (ret != 0) {
        s_music_last_err = ESP_FAIL;
        ESP_LOGW(TAG, "speaker unmute failed: %d", ret);
        return s_music_last_err;
    }
    ESP_LOGI(TAG, "speaker opened for music: vol=%d", s_volume);
    return ESP_OK;
}

void player_event_cb(audio_player_cb_ctx_t *ctx)
{
    if (ctx == nullptr) {
        return;
    }
    switch (ctx->audio_event) {
    case AUDIO_PLAYER_CALLBACK_EVENT_PLAYING:
        ESP_LOGI(TAG, "music event: playing");
        s_music_playing = true;
        s_music_paused = false;
        break;
    case AUDIO_PLAYER_CALLBACK_EVENT_PAUSE:
        ESP_LOGI(TAG, "music event: pause");
        s_music_paused = true;
        break;
    case AUDIO_PLAYER_CALLBACK_EVENT_IDLE:
        ESP_LOGI(TAG, "music event: idle");
        s_music_playing = false;
        s_music_paused = false;
        break;
    default:
        ESP_LOGI(TAG, "music event: %d", static_cast<int>(ctx->audio_event));
        break;
    }
}

float note_frequency(int midi_note)
{
    return 440.0f * std::pow(2.0f, static_cast<float>(midi_note - 69) / 12.0f);
}

void test_audio_task(void *arg)
{
    const auto mode = static_cast<TestAudioMode>(reinterpret_cast<uintptr_t>(arg));
    if (mode == TestAudioMode::Music) {
        audio_play_test_music();
    } else {
        audio_play_test_tone();
    }
    s_test_audio_playing = false;
    s_test_audio_stop_requested = false;
    s_test_audio_task = nullptr;
    vTaskDelete(nullptr);
}

esp_err_t start_test_audio_task(TestAudioMode mode, const char *name)
{
    if (s_test_audio_playing) {
        s_last_err = ESP_ERR_INVALID_STATE;
        return s_last_err;
    }
    s_test_audio_playing = true;
    s_test_audio_stop_requested = false;
    s_last_err = ESP_OK;
    void *arg = reinterpret_cast<void *>(static_cast<uintptr_t>(mode));
    if (xTaskCreate(test_audio_task, name, 4096, arg, 4, &s_test_audio_task) != pdPASS) {
        s_test_audio_playing = false;
        s_test_audio_stop_requested = false;
        s_test_audio_task = nullptr;
        s_last_err = ESP_ERR_NO_MEM;
        return s_last_err;
    }
    return ESP_OK;
}

} // namespace

esp_err_t audio_init()
{
    if (s_speaker != nullptr) {
        return ESP_OK;
    }
    s_speaker = bsp_audio_codec_speaker_init();
    if (s_speaker == nullptr) {
        s_last_err = ESP_FAIL;
        ESP_LOGW(TAG, "speaker codec init failed");
        return s_last_err;
    }
    int ret = esp_codec_dev_set_out_vol(s_speaker, s_volume);
    if (ret != 0) {
        s_last_err = ESP_FAIL;
        ESP_LOGW(TAG, "set volume failed: %d", ret);
        return s_last_err;
    }
    s_last_err = ESP_OK;
    return ESP_OK;
}

esp_err_t audio_set_volume(int volume)
{
    if (volume < 0) {
        volume = 0;
    } else if (volume > 100) {
        volume = 100;
    }
    s_volume = volume;
    ESP_RETURN_ON_ERROR(audio_init(), TAG, "audio init failed");
    int ret = esp_codec_dev_set_out_vol(s_speaker, s_volume);
    s_last_err = (ret == 0) ? ESP_OK : ESP_FAIL;
    return s_last_err;
}

int audio_get_volume()
{
    return s_volume;
}

esp_err_t audio_play_test_tone()
{
    if (s_mic_open) {
        s_last_err = ESP_ERR_INVALID_STATE;
        ESP_LOGW(TAG, "speaker test skipped while microphone is active");
        return s_last_err;
    }
    ESP_RETURN_ON_ERROR(audio_init(), TAG, "audio init failed");
    if (s_music_playing || s_music_paused) {
        audio_music_stop();
    }
    if (s_speaker_open) {
        esp_codec_dev_close(s_speaker);
        s_speaker_open = false;
    }

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = 1,
        .channel_mask = 0,
        .sample_rate = SAMPLE_RATE,
        .mclk_multiple = 0,
    };
    int ret = esp_codec_dev_open(s_speaker, &fs);
    if (ret != 0) {
        s_last_err = ESP_FAIL;
        return s_last_err;
    }
    s_speaker_open = true;

    constexpr int chunk_samples = 256;
    int16_t samples[chunk_samples] = {};
    const int total_samples = SAMPLE_RATE * TONE_MS / 1000;
    int written = 0;
    while (written < total_samples) {
        int n = total_samples - written;
        if (n > chunk_samples) {
            n = chunk_samples;
        }
        for (int i = 0; i < n; ++i) {
            float t = static_cast<float>(written + i) / SAMPLE_RATE;
            samples[i] = static_cast<int16_t>(std::sin(2.0f * 3.1415926f * TONE_HZ * t) * 9000);
        }
        ret = esp_codec_dev_write(s_speaker, samples, n * sizeof(int16_t));
        if (ret != 0) {
            break;
        }
        written += n;
    }
    esp_codec_dev_close(s_speaker);
    s_speaker_open = false;
    s_last_err = (ret == 0) ? ESP_OK : ESP_FAIL;
    return s_last_err;
}

esp_err_t audio_play_test_tone_async()
{
    return start_test_audio_task(TestAudioMode::Tone, "audio_tone");
}

esp_err_t audio_play_test_music()
{
    if (s_mic_open) {
        s_last_err = ESP_ERR_INVALID_STATE;
        ESP_LOGW(TAG, "music test skipped while microphone is active");
        return s_last_err;
    }
    ESP_RETURN_ON_ERROR(audio_init(), TAG, "audio init failed");
    if (s_music_playing || s_music_paused) {
        audio_music_stop();
    }
    if (s_speaker_open) {
        esp_codec_dev_close(s_speaker);
        s_speaker_open = false;
    }

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = 1,
        .channel_mask = 0,
        .sample_rate = MUSIC_SAMPLE_RATE,
        .mclk_multiple = 0,
    };
    int ret = esp_codec_dev_open(s_speaker, &fs);
    if (ret != 0) {
        s_last_err = ESP_FAIL;
        return s_last_err;
    }
    s_speaker_open = true;
    ret = esp_codec_dev_set_out_vol(s_speaker, s_volume);
    if (ret != 0) {
        esp_codec_dev_close(s_speaker);
        s_speaker_open = false;
        s_last_err = ESP_FAIL;
        return s_last_err;
    }

    constexpr int melody[] = {72, 76, 79, 83, 81, 79, 76, 72, 74, 77, 81, 84, 83, 79, 76, 72};
    constexpr int chunk_samples = 256;
    constexpr int note_samples = MUSIC_SAMPLE_RATE / 2;
    const int total_samples = MUSIC_SAMPLE_RATE * MUSIC_MS / 1000;
    int16_t samples[chunk_samples] = {};
    int written = 0;
    while ((written < total_samples) && !s_test_audio_stop_requested) {
        int n = total_samples - written;
        if (n > chunk_samples) {
            n = chunk_samples;
        }
        for (int i = 0; i < n; ++i) {
            const int pos = written + i;
            const size_t note_index = (pos / note_samples) % (sizeof(melody) / sizeof(melody[0]));
            const int in_note = pos % note_samples;
            const float t = static_cast<float>(pos) / MUSIC_SAMPLE_RATE;
            const float phase_note = static_cast<float>(in_note) / note_samples;
            const float env_in = phase_note < 0.08f ? (phase_note / 0.08f) : 1.0f;
            const float env_out = phase_note > 0.82f ? ((1.0f - phase_note) / 0.18f) : 1.0f;
            const float env = env_in * env_out;
            const float f1 = note_frequency(melody[note_index]);
            const float f2 = note_frequency(melody[note_index] - 12);
            const float lead = std::sin(2.0f * 3.1415926f * f1 * t);
            const float bass = std::sin(2.0f * 3.1415926f * f2 * t);
            samples[i] = static_cast<int16_t>((lead * 7200.0f + bass * 2400.0f) * env);
        }
        ret = esp_codec_dev_write(s_speaker, samples, n * sizeof(int16_t));
        if (ret != 0) {
            break;
        }
        written += n;
    }
    esp_codec_dev_close(s_speaker);
    s_speaker_open = false;
    s_last_err = (ret == 0) ? ESP_OK : ESP_FAIL;
    return s_last_err;
}

esp_err_t audio_play_test_music_async()
{
    return start_test_audio_task(TestAudioMode::Music, "audio_music_test");
}

esp_err_t audio_test_stop()
{
    if (s_test_audio_playing) {
        s_test_audio_stop_requested = true;
    }
    return ESP_OK;
}

bool audio_test_is_playing()
{
    return s_test_audio_playing;
}

esp_err_t audio_mic_start(int sample_rate, int channels)
{
    s_mic_init_attempted = true;
    if (s_test_audio_playing) {
        s_mic_last_err = ESP_ERR_INVALID_STATE;
        s_last_err = s_mic_last_err;
        return s_mic_last_err;
    }
    if (s_music_playing || s_music_paused) {
        audio_music_stop();
    }

    if (sample_rate <= 0) {
        sample_rate = SAMPLE_RATE;
    }
    if (channels <= 0) {
        channels = 1;
    }
    if (channels > 2) {
        channels = 2;
    }

    if (s_microphone == nullptr) {
        s_microphone = bsp_audio_codec_microphone_init();
        if (s_microphone == nullptr) {
            s_mic_last_err = ESP_FAIL;
            s_last_err = s_mic_last_err;
            ESP_LOGW(TAG, "microphone codec init failed");
            return s_mic_last_err;
        }
    }

    if (s_mic_open) {
        esp_codec_dev_close(s_microphone);
        s_mic_open = false;
    }

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = static_cast<uint8_t>(channels),
        .channel_mask = 0,
        .sample_rate = static_cast<uint32_t>(sample_rate),
        .mclk_multiple = 0,
    };
    int ret = esp_codec_dev_open(s_microphone, &fs);
    if (ret != 0) {
        s_mic_last_err = ESP_FAIL;
        s_last_err = s_mic_last_err;
        ESP_LOGW(TAG, "microphone open failed: %d", ret);
        return s_mic_last_err;
    }
    int gain_ret = esp_codec_dev_set_in_gain(s_microphone, 24.0f);
    if (gain_ret != 0) {
        ESP_LOGW(TAG, "set microphone gain failed: %d", gain_ret);
    }
    s_mic_open = true;
    s_mic_last_err = ESP_OK;
    s_last_err = ESP_OK;
    return ESP_OK;
}

esp_err_t audio_mic_read(void *buffer, size_t byte_count, size_t *bytes_read)
{
    if (bytes_read != nullptr) {
        *bytes_read = 0;
    }
    if ((buffer == nullptr) || (byte_count == 0) || (s_microphone == nullptr) || !s_mic_open) {
        s_mic_last_err = ESP_ERR_INVALID_STATE;
        s_last_err = s_mic_last_err;
        return s_mic_last_err;
    }
    int ret = esp_codec_dev_read(s_microphone, buffer, byte_count);
    if (ret != 0) {
        s_mic_last_err = ESP_FAIL;
        s_last_err = s_mic_last_err;
        return s_mic_last_err;
    }
    if (bytes_read != nullptr) {
        *bytes_read = byte_count;
    }
    s_mic_last_err = ESP_OK;
    s_last_err = ESP_OK;
    return ESP_OK;
}

esp_err_t audio_mic_stop()
{
    if ((s_microphone != nullptr) && s_mic_open) {
        int ret = esp_codec_dev_close(s_microphone);
        s_mic_open = false;
        s_mic_last_err = (ret == 0) ? ESP_OK : ESP_FAIL;
        s_last_err = s_mic_last_err;
        return s_mic_last_err;
    }
    return ESP_OK;
}

esp_err_t audio_stream_start(int sample_rate)
{
    ESP_LOGI(TAG, "stream start requested: rate=%d vol=%d speaker=%p mic_open=%d music=%d/%d",
             sample_rate, s_volume, s_speaker, s_mic_open, s_music_playing, s_music_paused);
    if (sample_rate <= 0 || s_test_audio_playing) {
        s_last_err = ESP_ERR_INVALID_ARG;
        ESP_LOGW(TAG, "stream start rejected: invalid arg or test playing");
        return s_last_err;
    }
    ESP_RETURN_ON_ERROR(audio_init(), TAG, "audio init failed");
    audio_mic_stop();
    if (s_music_playing || s_music_paused) {
        audio_music_stop();
    }
    if (s_speaker_open) {
        esp_codec_dev_close(s_speaker);
        s_speaker_open = false;
    }
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = 1,
        .channel_mask = 0,
        .sample_rate = static_cast<uint32_t>(sample_rate),
        .mclk_multiple = 0,
    };
    const int ret = esp_codec_dev_open(s_speaker, &fs);
    if (ret != 0) {
        s_last_err = ESP_FAIL;
        ESP_LOGW(TAG, "stream speaker open failed: ret=%d", ret);
        return s_last_err;
    }
    s_speaker_open = true;
    s_stream_open = true;
    if (esp_codec_dev_set_out_vol(s_speaker, s_volume) != 0 ||
        esp_codec_dev_set_out_mute(s_speaker, false) != 0) {
        ESP_LOGW(TAG, "stream speaker volume/unmute failed");
        audio_stream_stop();
        s_last_err = ESP_FAIL;
        return s_last_err;
    }
    s_last_err = ESP_OK;
    ESP_LOGI(TAG, "stream started: rate=%d vol=%d", sample_rate, s_volume);
    return ESP_OK;
}

esp_err_t audio_stream_write(const int16_t *samples, size_t byte_count)
{
    if (!s_stream_open || !s_speaker_open || !samples || byte_count == 0 || (byte_count & 1U)) {
        s_last_err = ESP_ERR_INVALID_STATE;
        ESP_LOGW(TAG, "stream write rejected: open=%d speaker_open=%d samples=%p bytes=%u",
                 s_stream_open, s_speaker_open, samples, static_cast<unsigned>(byte_count));
        return s_last_err;
    }
    const int ret = esp_codec_dev_write(s_speaker, const_cast<int16_t *>(samples), byte_count);
    s_last_err = (ret == 0) ? ESP_OK : ESP_FAIL;
    if (ret != 0) {
        ESP_LOGW(TAG, "stream write failed: ret=%d bytes=%u", ret, static_cast<unsigned>(byte_count));
    }
    return s_last_err;
}

esp_err_t audio_stream_stop()
{
    if (s_stream_open && s_speaker != nullptr && s_speaker_open) {
        const int ret = esp_codec_dev_close(s_speaker);
        s_speaker_open = false;
        s_stream_open = false;
        s_last_err = (ret == 0) ? ESP_OK : ESP_FAIL;
        return s_last_err;
    }
    s_stream_open = false;
    return ESP_OK;
}

esp_err_t audio_play_pcm_blocking(const int16_t *samples, size_t byte_count, int sample_rate)
{
    if (sample_rate <= 0 || !samples || byte_count == 0 || (byte_count & 1U)) {
        s_last_err = ESP_ERR_INVALID_ARG;
        ESP_LOGW(TAG, "pcm play rejected: samples=%p bytes=%u rate=%d",
                 samples, static_cast<unsigned>(byte_count), sample_rate);
        return s_last_err;
    }
    if (s_mic_open) {
        audio_mic_stop();
    }
    ESP_RETURN_ON_ERROR(audio_init(), TAG, "audio init failed");
    if (s_music_playing || s_music_paused) {
        audio_music_stop();
    }
    if (s_speaker_open) {
        esp_codec_dev_close(s_speaker);
        s_speaker_open = false;
    }
    s_stream_open = false;

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = 1,
        .channel_mask = 0,
        .sample_rate = static_cast<uint32_t>(sample_rate),
        .mclk_multiple = 0,
    };
    int ret = esp_codec_dev_open(s_speaker, &fs);
    if (ret != 0) {
        s_last_err = ESP_FAIL;
        ESP_LOGW(TAG, "pcm play open failed: ret=%d rate=%d bytes=%u",
                 ret, sample_rate, static_cast<unsigned>(byte_count));
        return s_last_err;
    }
    s_speaker_open = true;
    ret = esp_codec_dev_set_out_vol(s_speaker, s_volume);
    if (ret == 0) {
        ret = esp_codec_dev_set_out_mute(s_speaker, false);
    }
    if (ret != 0) {
        ESP_LOGW(TAG, "pcm play volume/unmute failed: ret=%d", ret);
        esp_codec_dev_close(s_speaker);
        s_speaker_open = false;
        s_last_err = ESP_FAIL;
        return s_last_err;
    }

    ESP_LOGI(TAG, "pcm play start: bytes=%u rate=%d vol=%d",
             static_cast<unsigned>(byte_count), sample_rate, s_volume);
    // The live reply is accumulated in a std::vector.  That vector may be
    // backed by PSRAM, while the Waveshare I2S DMA path is safest with an
    // internal DMA-capable buffer.  Do not pass the network buffer directly
    // to esp_codec_dev_write: on some boots that produces only the codec
    // enable click and the first audio block is then rejected by DMA.
    constexpr size_t chunk_bytes = 1024;
    uint8_t *dma_buffer = static_cast<uint8_t *>(
        heap_caps_malloc(chunk_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
    if (dma_buffer == nullptr) {
        ESP_LOGW(TAG, "pcm play dma buffer allocation failed: bytes=%u free_internal=%u largest_internal=%u",
                 static_cast<unsigned>(chunk_bytes),
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                 static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
        esp_codec_dev_close(s_speaker);
        s_speaker_open = false;
        s_last_err = ESP_ERR_NO_MEM;
        return s_last_err;
    }
    constexpr int preroll_ms = 480;
    int16_t silence[1024] = {};
    size_t preroll_bytes = static_cast<size_t>(sample_rate) * preroll_ms / 1000 * sizeof(int16_t);
    while (preroll_bytes > 0) {
        size_t n = preroll_bytes;
        if (n > sizeof(silence)) n = sizeof(silence);
        n &= ~static_cast<size_t>(1);
        ret = esp_codec_dev_write(s_speaker, silence, n);
        if (ret != 0) {
            ESP_LOGW(TAG, "pcm play preroll failed: ret=%d bytes=%u",
                     ret, static_cast<unsigned>(n));
            break;
        }
        preroll_bytes -= n;
    }
    size_t written = 0;
    while (written < byte_count) {
        size_t n = byte_count - written;
        if (n > chunk_bytes) n = chunk_bytes;
        n &= ~static_cast<size_t>(1);
        std::memcpy(dma_buffer, reinterpret_cast<const uint8_t *>(samples) + written, n);
        ret = esp_codec_dev_write(s_speaker, dma_buffer, static_cast<int>(n));
        if (ret != 0) {
            ESP_LOGW(TAG, "pcm play write failed: ret=%d offset=%u bytes=%u",
                     ret, static_cast<unsigned>(written), static_cast<unsigned>(n));
            break;
        }
        written += n;
    }
    heap_caps_free(dma_buffer);
    esp_codec_dev_close(s_speaker);
    s_speaker_open = false;
    s_last_err = (ret == 0 && written == byte_count) ? ESP_OK : ESP_FAIL;
    ESP_LOGI(TAG, "pcm play done: ret=%d written=%u/%u",
             ret, static_cast<unsigned>(written), static_cast<unsigned>(byte_count));
    return s_last_err;
}

esp_err_t audio_music_init()
{
    if (s_music_player_init) {
        return ESP_OK;
    }
    ESP_LOGI(TAG, "audio_music_init begin free=%u internal=%u largest_internal=%u stack_hw=%u",
             static_cast<unsigned>(esp_get_free_heap_size()),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    ESP_RETURN_ON_ERROR(audio_init(), TAG, "audio init failed");
    audio_player_config_t config = {
        .mute_fn = player_mute_cb,
        .clk_set_fn = player_clk_set_cb,
        .write_fn = player_write_cb,
        .priority = 5,
        .coreID = tskNO_AFFINITY,
        .force_stereo = false,
        .write_fn2 = nullptr,
        .write_ctx = nullptr,
    };
    ESP_RETURN_ON_ERROR(audio_player_new(config), TAG, "audio player init failed");
    audio_player_callback_register(player_event_cb, nullptr);
    s_music_player_init = true;
    s_music_last_err = ESP_OK;
    ESP_LOGI(TAG, "audio_music_init ok free=%u internal=%u largest_internal=%u stack_hw=%u",
             static_cast<unsigned>(esp_get_free_heap_size()),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    return ESP_OK;
}

esp_err_t audio_music_scan(const char *dir, AudioMusicTrack *tracks, size_t max_tracks, size_t *track_count)
{
    if (track_count != nullptr) {
        *track_count = 0;
    }
    if ((tracks == nullptr) || (max_tracks == 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (dir == nullptr) {
        dir = AUDIO_MUSIC_DEFAULT_DIR;
    }

    DIR *dp = opendir(dir);
    if (dp == nullptr) {
        if (mkdir(dir, 0775) == 0) {
            ESP_LOGI(TAG, "created music dir: %s", dir);
            dp = opendir(dir);
        } else {
            ESP_LOGW(TAG, "create music dir failed: %s errno=%d", dir, errno);
        }
    }
    if (dp == nullptr) {
        ESP_LOGW(TAG, "open music dir failed: %s errno=%d", dir, errno);
        return ESP_FAIL;
    }

    size_t count = 0;
    while (dirent *entry = readdir(dp)) {
        if (count >= max_tracks) {
            break;
        }
        if (!is_audio_file(entry->d_name)) {
            continue;
        }
        char full_path[sizeof(AudioMusicTrack::path)] = {};
        if (!join_path(full_path, sizeof(full_path), dir, entry->d_name)) {
            continue;
        }
        struct stat st = {};
        if ((stat(full_path, &st) != 0) || !S_ISREG(st.st_mode)) {
            continue;
        }
        std::memcpy(tracks[count].path, full_path, std::strlen(full_path) + 1);
        make_title_from_name(entry->d_name, tracks[count].title, sizeof(tracks[count].title));
        tracks[count].size = static_cast<size_t>(st.st_size);
        ++count;
    }
    closedir(dp);

    if (track_count != nullptr) {
        *track_count = count;
    }
    return ESP_OK;
}

esp_err_t audio_music_play_file(const char *path)
{
    if ((path == nullptr) || (path[0] == '\0')) {
        s_music_last_err = ESP_ERR_INVALID_ARG;
        return s_music_last_err;
    }
    ESP_LOGI(TAG, "audio_music_play_file begin path=%s free=%u internal=%u largest_internal=%u stack_hw=%u",
             path,
             static_cast<unsigned>(esp_get_free_heap_size()),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    ESP_RETURN_ON_ERROR(audio_music_init(), TAG, "music init failed");
    ESP_LOGI(TAG, "audio_music_play_file after init path=%s", path);
    if (s_test_audio_playing) {
        s_music_last_err = ESP_ERR_INVALID_STATE;
        return s_music_last_err;
    }
    audio_mic_stop();
    if (s_music_playing || s_music_paused) {
        audio_player_stop();
        s_music_playing = false;
        s_music_paused = false;
    }

    FILE *fp = std::fopen(path, "rb");
    if (fp == nullptr) {
        s_music_last_err = ESP_FAIL;
        ESP_LOGW(TAG, "open music file failed: %s", path);
        return s_music_last_err;
    }
    ESP_LOGI(TAG, "audio_music_play_file fopen ok path=%s", path);

    esp_err_t ret = audio_player_play(fp);
    if (ret != ESP_OK) {
        std::fclose(fp);
        s_music_last_err = ret;
        ESP_LOGW(TAG, "audio_player_play failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "audio_player_play queued path=%s", path);
    size_t copy_len = 0;
    while ((path[copy_len] != '\0') && (copy_len + 1 < sizeof(s_music_path))) {
        ++copy_len;
    }
    std::memcpy(s_music_path, path, copy_len);
    s_music_path[copy_len] = '\0';
    s_music_playing = true;
    s_music_paused = false;
    s_music_last_err = ESP_OK;
    return ESP_OK;
}

esp_err_t audio_music_pause()
{
    ESP_RETURN_ON_ERROR(audio_music_init(), TAG, "music init failed");
    esp_err_t ret = audio_player_pause();
    if (ret == ESP_OK) {
        s_music_paused = true;
    }
    s_music_last_err = ret;
    return ret;
}

esp_err_t audio_music_resume()
{
    ESP_RETURN_ON_ERROR(audio_music_init(), TAG, "music init failed");
    esp_err_t ret = audio_player_resume();
    if (ret == ESP_OK) {
        s_music_playing = true;
        s_music_paused = false;
    }
    s_music_last_err = ret;
    return ret;
}

esp_err_t audio_music_stop()
{
    if (!s_music_player_init) {
        return ESP_OK;
    }
    esp_err_t ret = audio_player_stop();
    s_music_playing = false;
    s_music_paused = false;
    s_music_last_err = ret;
    return ret;
}

bool audio_music_is_playing()
{
    return s_music_playing;
}

bool audio_music_is_paused()
{
    return s_music_paused;
}

void audio_status_text(char *buffer, size_t buffer_size)
{
    if ((buffer == nullptr) || (buffer_size == 0)) {
        return;
    }
    const char *speaker_state = (s_speaker != nullptr) ? "ready" : "not initialized";
    const char *mic_state = "not in use";
    char mic_detail[64] = {};
    if (s_mic_open) {
        mic_state = "active";
    } else if (s_test_audio_playing) {
        mic_state = "blocked: test playing";
    } else if (s_mic_init_attempted && (s_microphone == nullptr)) {
        mic_state = "unavailable";
        std::snprintf(mic_detail, sizeof(mic_detail), " (%s)", esp_err_to_name(s_mic_last_err));
    } else if (s_mic_init_attempted && (s_mic_last_err != ESP_OK)) {
        mic_state = "unavailable";
        std::snprintf(mic_detail, sizeof(mic_detail), " (%s)", esp_err_to_name(s_mic_last_err));
    }
    std::snprintf(
        buffer,
        buffer_size,
        "Speaker: %s\nCodec: ES8311 speaker / ES7210 mic\nVolume: %d%%\nMic: %s%s\nLast: %s",
        speaker_state,
        s_volume,
        mic_state,
        mic_detail,
        esp_err_to_name(s_last_err)
    );
}

void audio_music_status_text(char *buffer, size_t buffer_size)
{
    if ((buffer == nullptr) || (buffer_size == 0)) {
        return;
    }
    const char *state = s_music_paused ? "paused" : (s_music_playing ? "playing" : "idle");
    std::snprintf(
        buffer,
        buffer_size,
        "Player: %s\nFile: %s\nVolume: %d%%\nLast: %s",
        state,
        s_music_path[0] ? s_music_path : "(none)",
        s_volume,
        esp_err_to_name(s_music_last_err)
    );
}

} // namespace watch
