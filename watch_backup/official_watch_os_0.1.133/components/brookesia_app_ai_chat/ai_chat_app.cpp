#include "ai_chat_app.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <sys/stat.h>
#include <string>
#include <vector>

#include "cJSON.h"
#include "esp_brookesia.hpp"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "esp_lib_utils.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_transport.h"
#include "esp_transport_tcp.h"
#include "esp_transport_ws.h"
#include "freertos/idf_additions.h"
#include "freertos/stream_buffer.h"
#include "lvgl.h"
#include "mbedtls/base64.h"
#include "widgets/ime/lv_ime_pinyin.h"
#include "watch_app_icons.hpp"
#include "watch_connectivity.hpp"
#include "watch_audio.hpp"
#include "watch_fonts.hpp"
#include "watch_private_config.hpp"
#include "watch_storage.hpp"

namespace esp_brookesia::apps {
namespace {
constexpr const char *APP_NAME = "AI Chat";
constexpr const char *DEVICE_ID = "esp32-s3-watch";
constexpr size_t MAX_HTTP_BODY = 24 * 1024;
constexpr size_t AUDIO_SAMPLE_RATE = 16000;
constexpr size_t VOICE_SECONDS = 4;
// Keep enough room for a natural sentence. The microphone path is stable;
// response latency is reduced on the server after ASR completion instead of
// truncating the user's speech on the watch.
constexpr size_t LIVE_VOICE_MS = 4000;
constexpr size_t LIVE_FRAME_BYTES = 1920; // 60 ms of 16 kHz mono PCM16.
// The Waveshare BSP initializes the I2S channel at 22050 Hz.  The codec open
// call alone does not reconfigure that clock, so live TTS PCM must use the BSP
// rate or it plays too fast.
constexpr int LIVE_REPLY_PLAYBACK_RATE = 22050;
constexpr int LIVE_MIC_WARMUP_FRAMES = 4;
constexpr size_t LIVE_AUDIO_STREAM_CAPACITY = 384 * 1024;
// A 32 KiB prebuffer is only about 740 ms at 22.05 kHz PCM16. Wi-Fi delivery
// of an audio frame can occasionally pause longer than that, which produces
// audible word-by-word gaps. Start after roughly 1.5 seconds instead.
constexpr size_t LIVE_AUDIO_PREBUFFER_BYTES = 64 * 1024;
constexpr size_t LIVE_AUDIO_IO_BYTES = 2048;
constexpr int LIVE_AUDIO_PREROLL_MS = 160;
constexpr size_t TTS_AUDIO_STREAM_CAPACITY = 384 * 1024;
constexpr size_t TTS_AUDIO_PREBUFFER_BYTES = 64 * 1024;
constexpr size_t TTS_AUDIO_IO_BYTES = 4096;
constexpr int TTS_AUDIO_PREROLL_MS = 120;
constexpr const char *DUPLEX_PATH = "/DUDUSERVER/api/v1/ai-call/duplex?access_token=";
constexpr size_t VOICE_BYTES = AUDIO_SAMPLE_RATE * VOICE_SECONDS * sizeof(int16_t);
constexpr size_t MAX_TTS_BYTES = 240 * 1024;
constexpr const char *TTS_FILE = "/littlefs/ai_reply.wav";
constexpr int SAFE_TOP = 24;
constexpr int LIVE_SAFE_TOP = 38;
constexpr ws_transport_opcodes_t WS_TEXT_FIN =
    static_cast<ws_transport_opcodes_t>(WS_TRANSPORT_OPCODES_TEXT | WS_TRANSPORT_OPCODES_FIN);
constexpr ws_transport_opcodes_t WS_BINARY_FIN =
    static_cast<ws_transport_opcodes_t>(WS_TRANSPORT_OPCODES_BINARY | WS_TRANSPORT_OPCODES_FIN);

struct HttpBody { std::string data; int status = 0; };

struct PcmStats {
    uint32_t samples = 0;
    uint32_t peak = 0;
    uint32_t avg_abs = 0;
};

struct LivePcmPlayer {
    StreamBufferHandle_t stream = nullptr;
    TaskHandle_t task = nullptr;
    std::atomic<bool> done{false};
    std::atomic<bool> cancel{false};
    std::atomic<bool> finished{false};
    std::atomic<esp_err_t> result{ESP_FAIL};
    size_t bytes_received = 0;
};

struct TtsWavInfo {
    int sample_rate = 0;
};

struct TtsPcmPlayer {
    StreamBufferHandle_t stream = nullptr;
    TaskHandle_t task = nullptr;
    std::atomic<bool> done{false};
    std::atomic<bool> cancel{false};
    std::atomic<bool> finished{false};
    std::atomic<esp_err_t> result{ESP_FAIL};
    size_t bytes_received = 0;
    int sample_rate = 0;
};

enum class TtsStreamResult {
    Unsupported,
    Played,
    FailedAfterStart,
};

bool parse_tts_wav_header(const uint8_t *data, size_t size, TtsWavInfo *info)
{
    if (data == nullptr || info == nullptr || size < 44) {
        return false;
    }
    if (std::memcmp(data, "RIFF", 4) != 0 ||
        std::memcmp(data + 8, "WAVE", 4) != 0 ||
        std::memcmp(data + 12, "fmt ", 4) != 0 ||
        std::memcmp(data + 36, "data", 4) != 0) {
        return false;
    }

    int16_t audio_format = 0;
    int16_t channels = 0;
    int32_t sample_rate = 0;
    int16_t bits_per_sample = 0;
    std::memcpy(&audio_format, data + 20, sizeof(audio_format));
    std::memcpy(&channels, data + 22, sizeof(channels));
    std::memcpy(&sample_rate, data + 24, sizeof(sample_rate));
    std::memcpy(&bits_per_sample, data + 34, sizeof(bits_per_sample));
    if (audio_format != 1 || channels != 1 || bits_per_sample != 16 || sample_rate <= 0) {
        return false;
    }

    info->sample_rate = sample_rate;
    return true;
}

void tts_pcm_player_task(void *arg)
{
    auto *player = static_cast<TtsPcmPlayer *>(arg);
    if (player == nullptr || player->stream == nullptr) {
        if (player != nullptr) {
            player->result.store(ESP_ERR_INVALID_ARG);
            player->finished.store(true);
        }
        vTaskDeleteWithCaps(nullptr);
        return;
    }

    esp_err_t result = watch::audio_stream_start(player->sample_rate);
    if (result == ESP_OK) {
        auto *dma_buffer = static_cast<uint8_t *>(
            heap_caps_malloc(TTS_AUDIO_IO_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
        if (dma_buffer == nullptr) {
            result = ESP_ERR_NO_MEM;
        } else {
            int16_t silence[TTS_AUDIO_IO_BYTES / sizeof(int16_t)] = {};
            size_t preroll_bytes =
                static_cast<size_t>(player->sample_rate) * TTS_AUDIO_PREROLL_MS / 1000 * sizeof(int16_t);
            while (preroll_bytes > 0 && !player->cancel.load()) {
                size_t count = std::min(preroll_bytes, sizeof(silence));
                count &= ~static_cast<size_t>(1);
                if (watch::audio_stream_write(silence, count) != ESP_OK) {
                    result = ESP_FAIL;
                    break;
                }
                preroll_bytes -= count;
            }

            while (result == ESP_OK && !player->cancel.load()) {
                const size_t received = xStreamBufferReceive(
                    player->stream,
                    dma_buffer,
                    TTS_AUDIO_IO_BYTES,
                    pdMS_TO_TICKS(120));
                if (received > 0) {
                    if (watch::audio_stream_write(
                            reinterpret_cast<const int16_t *>(dma_buffer), received) != ESP_OK) {
                        result = ESP_FAIL;
                        break;
                    }
                    continue;
                }
                if (player->done.load() && xStreamBufferIsEmpty(player->stream)) {
                    break;
                }
            }
            heap_caps_free(dma_buffer);
        }
        const esp_err_t stop_result = watch::audio_stream_stop();
        if (result == ESP_OK && stop_result != ESP_OK) {
            result = stop_result;
        }
    }
    player->result.store(result);
    player->finished.store(true);
    vTaskDeleteWithCaps(nullptr);
}

void live_pcm_player_task(void *arg)
{
    auto *player = static_cast<LivePcmPlayer *>(arg);
    if (player == nullptr || player->stream == nullptr) {
        if (player != nullptr) {
            player->result.store(ESP_ERR_INVALID_ARG);
            player->finished.store(true);
        }
        vTaskDeleteWithCaps(nullptr);
        return;
    }

    esp_err_t result = watch::audio_stream_start(LIVE_REPLY_PLAYBACK_RATE);
    if (result == ESP_OK) {
        auto *dma_buffer = static_cast<uint8_t *>(
            heap_caps_malloc(LIVE_AUDIO_IO_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
        if (dma_buffer == nullptr) {
            result = ESP_ERR_NO_MEM;
        } else {
            int16_t silence[LIVE_AUDIO_IO_BYTES / sizeof(int16_t)] = {};
            size_t preroll_bytes =
                static_cast<size_t>(LIVE_REPLY_PLAYBACK_RATE) * LIVE_AUDIO_PREROLL_MS / 1000 * sizeof(int16_t);
            while (preroll_bytes > 0 && !player->cancel.load()) {
                size_t count = std::min(preroll_bytes, sizeof(silence));
                count &= ~static_cast<size_t>(1);
                if (watch::audio_stream_write(silence, count) != ESP_OK) {
                    result = ESP_FAIL;
                    break;
                }
                preroll_bytes -= count;
            }

            while (result == ESP_OK && !player->cancel.load()) {
                const size_t received = xStreamBufferReceive(
                    player->stream,
                    dma_buffer,
                    LIVE_AUDIO_IO_BYTES,
                    pdMS_TO_TICKS(120));
                if (received > 0) {
                    if (watch::audio_stream_write(
                            reinterpret_cast<const int16_t *>(dma_buffer), received) != ESP_OK) {
                        result = ESP_FAIL;
                        break;
                    }
                    continue;
                }
                if (player->done.load() && xStreamBufferIsEmpty(player->stream)) {
                    break;
                }
            }
            heap_caps_free(dma_buffer);
        }
        const esp_err_t stop_result = watch::audio_stream_stop();
        if (result == ESP_OK && stop_result != ESP_OK) {
            result = stop_result;
        }
    }
    player->result.store(result);
    player->finished.store(true);
    vTaskDeleteWithCaps(nullptr);
}

void update_pcm_stats(const uint8_t *data, size_t size, PcmStats &stats)
{
    if ((data == nullptr) || (size < sizeof(int16_t))) return;
    const auto *samples = reinterpret_cast<const int16_t *>(data);
    const size_t count = size / sizeof(int16_t);
    uint64_t sum_abs = static_cast<uint64_t>(stats.avg_abs) * stats.samples;
    for (size_t i = 0; i < count; ++i) {
        const int value = samples[i];
        const uint32_t abs_value = static_cast<uint32_t>(value < 0 ? -value : value);
        if (abs_value > stats.peak) stats.peak = abs_value;
        sum_abs += abs_value;
    }
    stats.samples += static_cast<uint32_t>(count);
    stats.avg_abs = stats.samples > 0 ? static_cast<uint32_t>(sum_abs / stats.samples) : 0;
}

const char *friendly_voice_error(const char *detail)
{
    if (detail == nullptr) return "Live voice service error";
    if ((std::strcmp(detail, "no speech recognized") == 0) ||
        (std::strcmp(detail, "no speech detected") == 0)) {
        return "No speech recognized. Hold close and speak after prompt";
    }
    if (std::strcmp(detail, "audio too quiet") == 0) {
        return "Voice too quiet. Speak closer";
    }
    if (std::strcmp(detail, "audio too short") == 0) {
        return "Recording too short. Retry";
    }
    return detail[0] ? detail : "Live voice service error";
}

bool internal_voice_text(const char *value)
{
    if (!value || !value[0]) return true;
    std::string text(value);
    text.erase(0, text.find_first_not_of(" \t\r\n"));
    const size_t end = text.find_last_not_of(" \t\r\n");
    if (end != std::string::npos) text.resize(end + 1);
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (text == ":" || text == "：" || text == "," || text == "，" ||
        text == ";" || text == "；" || text == "." || text == "。") {
        return true;
    }
    return text == "reply" || text == "replay" || text == "response" ||
           text.find("reply_text") != std::string::npos ||
           text.find("replay_text") != std::string::npos ||
           text.find("output_audio") != std::string::npos;
}

esp_err_t http_event(esp_http_client_event_t *event)
{
    auto *body = static_cast<HttpBody *>(event->user_data);
    if (event->event_id == HTTP_EVENT_ON_DATA && body && event->data &&
        body->data.size() + static_cast<size_t>(event->data_len) <= MAX_HTTP_BODY) {
        body->data.append(static_cast<const char *>(event->data), event->data_len);
    }
    return ESP_OK;
}

bool request(const char *url, const char *token, HttpBody &body, const char *post_body = nullptr)
{
    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = 18000;
    config.event_handler = http_event;
    config.user_data = &body;
    config.keep_alive_enable = false;
    auto *client = esp_http_client_init(&config);
    if (!client) return false;
    if (post_body) {
        esp_http_client_set_method(client, HTTP_METHOD_POST);
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, post_body, std::strlen(post_body));
    }
    if (token && token[0]) {
        std::string auth = "Bearer ";
        auth += token;
        esp_http_client_set_header(client, "Authorization", auth.c_str());
    }
    const esp_err_t err = esp_http_client_perform(client);
    body.status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    return err == ESP_OK && body.status >= 200 && body.status < 300;
}

bool request_audio(const char *token, const uint8_t *audio, size_t audio_size, HttpBody &body)
{
    if (!token || !token[0] || !audio || audio_size == 0) return false;
    constexpr const char *boundary = "----watchAiAudioBoundary";
    const std::string prefix = std::string("--") + boundary + "\r\n"
        "Content-Disposition: form-data; name=\"device_id\"\r\n\r\n" + DEVICE_ID + "\r\n--" + boundary + "\r\n"
        "Content-Disposition: form-data; name=\"source\"\r\n\r\n" "ai_chat\r\n--" + boundary + "\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"watch.wav\"\r\n"
        "Content-Type: audio/wav\r\n\r\n";
    const std::string suffix = std::string("\r\n--") + boundary + "--\r\n";
    esp_http_client_config_t config = {};
    config.url = WATCH_PRIVATE_DUDUSERVER_API_BASE_URL "/ai-call/audio";
    config.timeout_ms = 30000;
    config.event_handler = http_event;
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

TtsStreamResult play_tts_wav_stream(const char *url, const char *token)
{
    if (!url || !url[0]) {
        return TtsStreamResult::Unsupported;
    }
    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = 30000;
    config.keep_alive_enable = false;
    auto *client = esp_http_client_init(&config);
    if (!client) {
        return TtsStreamResult::Unsupported;
    }
    if (token && token[0]) {
        std::string auth = "Bearer ";
        auth += token;
        esp_http_client_set_header(client, "Authorization", auth.c_str());
    }

    bool ok = esp_http_client_open(client, 0) == ESP_OK;
    int64_t total = ok ? esp_http_client_fetch_headers(client) : -1;
    const int code = esp_http_client_get_status_code(client);
    if (!ok || code < 200 || code >= 300 || (total > 0 && total > static_cast<int64_t>(MAX_TTS_BYTES))) {
        esp_http_client_cleanup(client);
        return TtsStreamResult::Unsupported;
    }

    constexpr size_t read_bytes = TTS_AUDIO_IO_BYTES;
    uint8_t *buffer = static_cast<uint8_t *>(
        heap_caps_malloc(read_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!buffer) {
        buffer = static_cast<uint8_t *>(heap_caps_malloc(read_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    if (!buffer) {
        esp_http_client_cleanup(client);
        return TtsStreamResult::Unsupported;
    }

    TtsPcmPlayer player;
    player.stream = xStreamBufferCreateWithCaps(
        TTS_AUDIO_STREAM_CAPACITY,
        1,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const bool stream_available = player.stream != nullptr;
    std::vector<uint8_t> header;
    header.reserve(64);
    TtsWavInfo wav = {};
    bool playback_started = false;
    bool have_pending_byte = false;
    uint8_t pending_byte = 0;
    size_t received = 0;
    int idle_reads = 0;

    auto start_player = [&]() -> bool {
        if (!stream_available || player.task != nullptr || player.finished.load()) {
            return false;
        }
        player.sample_rate = wav.sample_rate;
        player.cancel.store(false);
        player.done.store(false);
        player.finished.store(false);
        player.result.store(ESP_FAIL);
        if (xTaskCreateWithCaps(
                tts_pcm_player_task,
                "watch_ai_tts_pcm",
                4096,
                &player,
                5,
                &player.task,
                MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) != pdPASS) {
            player.task = nullptr;
            player.result.store(ESP_ERR_NO_MEM);
            player.finished.store(true);
            return false;
        }
        playback_started = true;
        return true;
    };

    auto finish_player = [&](bool cancel) -> bool {
        if (player.stream == nullptr) {
            return true;
        }
        player.done.store(true);
        player.cancel.store(cancel);
        if (player.task == nullptr) {
            vStreamBufferDeleteWithCaps(player.stream);
            player.stream = nullptr;
            return player.finished.load() || player.bytes_received == 0;
        }
        const size_t audio_ms = player.bytes_received * 1000 /
                                (sizeof(int16_t) * static_cast<size_t>(player.sample_rate));
        const int timeout_ms = cancel
            ? 1500
            : std::clamp(static_cast<int>(audio_ms + 2000), 2000, 30000);
        for (int elapsed_ms = 0; elapsed_ms < timeout_ms && !player.finished.load(); elapsed_ms += 10) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (!player.finished.load()) {
            player.cancel.store(true);
            for (int elapsed_ms = 0; elapsed_ms < 1500 && !player.finished.load(); elapsed_ms += 10) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }
        if (!player.finished.load()) {
            ESP_LOGW(APP_NAME, "tts PCM player did not stop before worker cleanup");
            while (!player.finished.load()) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }
        player.task = nullptr;
        vStreamBufferDeleteWithCaps(player.stream);
        player.stream = nullptr;
        return true;
    };

    auto write_pcm = [&](const uint8_t *data, size_t size) -> bool {
        if (data == nullptr || size == 0 || player.stream == nullptr) {
            return true;
        }
        if (have_pending_byte) {
            if (size == 0) {
                return true;
            }
            uint8_t pair[2] = {pending_byte, data[0]};
            const size_t sent = xStreamBufferSend(
                player.stream, pair, sizeof(pair), pdMS_TO_TICKS(300));
            if (sent != sizeof(pair)) {
                ESP_LOGW(APP_NAME, "tts stream short write pending sent=%u expected=2",
                         static_cast<unsigned>(sent));
                return false;
            }
            player.bytes_received += sent;
            data += 1;
            size -= 1;
            have_pending_byte = false;
        }
        if (size == 0) {
            return true;
        }
        if ((size & 1U) != 0) {
            pending_byte = data[size - 1];
            have_pending_byte = true;
            --size;
        }
        while (size > 0) {
            size_t n = std::min(size, read_bytes);
            n &= ~static_cast<size_t>(1);
            if (n == 0) {
                break;
            }
            const size_t sent = xStreamBufferSend(
                player.stream, data, n, pdMS_TO_TICKS(300));
            if (sent != n) {
                ESP_LOGW(APP_NAME, "tts stream short write sent=%u expected=%u",
                         static_cast<unsigned>(sent),
                         static_cast<unsigned>(n));
                return false;
            }
            player.bytes_received += sent;
            data += n;
            size -= n;
            if (!playback_started && player.bytes_received >= TTS_AUDIO_PREBUFFER_BYTES) {
                if (!start_player()) {
                    return false;
                }
            }
        }
        return true;
    };

    while (idle_reads < 12) {
        const int read = esp_http_client_read(client, reinterpret_cast<char *>(buffer), read_bytes);
        if (read < 0) {
            ok = false;
            break;
        }
        if (read == 0) {
            if (total > 0 && received >= static_cast<size_t>(total)) {
                break;
            }
            if (esp_http_client_is_complete_data_received(client)) {
                break;
            }
            ++idle_reads;
            continue;
        }
        idle_reads = 0;
        received += static_cast<size_t>(read);
        if (received > MAX_TTS_BYTES) {
            ok = false;
            break;
        }

        if (header.size() < 44) {
            const size_t need = 44 - header.size();
            const size_t take = std::min(need, static_cast<size_t>(read));
            header.insert(header.end(), buffer, buffer + take);
            if (header.size() < 44) {
                continue;
            }
            if (!parse_tts_wav_header(header.data(), header.size(), &wav)) {
                ok = false;
                break;
            }
            if (wav.sample_rate <= 0) {
                ok = false;
                break;
            }
            if (take < static_cast<size_t>(read)) {
                if (!write_pcm(buffer + take, static_cast<size_t>(read) - take)) {
                    ok = false;
                    break;
                }
            }
            header.clear();
            continue;
        }

        if (!write_pcm(buffer, static_cast<size_t>(read))) {
            ok = false;
            break;
        }
    }

    if (have_pending_byte) {
        ESP_LOGW(APP_NAME, "tts stream dropped trailing odd byte");
    }
    if (!playback_started && player.bytes_received > 0) {
        if (!start_player()) {
            if (player.stream != nullptr) {
                vStreamBufferDeleteWithCaps(player.stream);
                player.stream = nullptr;
            }
            heap_caps_free(buffer);
            esp_http_client_cleanup(client);
            return TtsStreamResult::Unsupported;
        }
    }
    if (playback_started) {
        const bool playback_ok = finish_player(false);
        if (!playback_ok || player.result.load() != ESP_OK) {
            ok = false;
        }
    } else if (player.stream != nullptr) {
        vStreamBufferDeleteWithCaps(player.stream);
        player.stream = nullptr;
    }

    heap_caps_free(buffer);
    esp_http_client_cleanup(client);
    if (playback_started && ok) {
        return TtsStreamResult::Played;
    }
    if (playback_started) {
        return TtsStreamResult::FailedAfterStart;
    }
    return TtsStreamResult::Unsupported;
}

bool download_tts_and_play(const char *url, const char *token)
{
    const TtsStreamResult stream_result = play_tts_wav_stream(url, token);
    if (stream_result == TtsStreamResult::Played) {
        return true;
    }
    if (stream_result == TtsStreamResult::FailedAfterStart) {
        return false;
    }
    if (!url || !url[0] || watch::storage_littlefs_mount() != ESP_OK) return false;
    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = 30000;
    config.keep_alive_enable = false;
    auto *client = esp_http_client_init(&config);
    if (!client) return false;
    if (token && token[0]) {
        std::string auth = "Bearer "; auth += token;
        esp_http_client_set_header(client, "Authorization", auth.c_str());
    }
    bool ok = esp_http_client_open(client, 0) == ESP_OK;
    int64_t total = ok ? esp_http_client_fetch_headers(client) : -1;
    const int code = esp_http_client_get_status_code(client);
    if (!ok || code < 200 || code >= 300 || total <= 44 || total > static_cast<int64_t>(MAX_TTS_BYTES)) {
        esp_http_client_cleanup(client);
        return false;
    }
    const char *tmp = "/littlefs/ai_reply.tmp";
    FILE *file = std::fopen(tmp, "wb");
    if (!file) { esp_http_client_cleanup(client); return false; }
    uint8_t *buffer = static_cast<uint8_t *>(heap_caps_malloc(2048, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!buffer) buffer = static_cast<uint8_t *>(heap_caps_malloc(2048, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    size_t received = 0;
    if (buffer) {
        while (received < static_cast<size_t>(total)) {
            const int read = esp_http_client_read(client, reinterpret_cast<char *>(buffer), std::min(static_cast<size_t>(2048), static_cast<size_t>(total) - received));
            if (read <= 0 || std::fwrite(buffer, 1, read, file) != static_cast<size_t>(read)) { ok = false; break; }
            received += static_cast<size_t>(read);
        }
        heap_caps_free(buffer);
    } else {
        ok = false;
    }
    const int close_result = std::fclose(file);
    esp_http_client_cleanup(client);
    if (!ok || close_result != 0 || received != static_cast<size_t>(total)) { std::remove(tmp); return false; }
    std::remove(TTS_FILE);
    if (std::rename(tmp, TTS_FILE) != 0) { std::remove(tmp); return false; }
    return watch::audio_music_play_file(TTS_FILE) == ESP_OK;
}

void write_wav_header(uint8_t *data, size_t pcm_size)
{
    if (!data || pcm_size < 44) return;
    const uint32_t data_size = static_cast<uint32_t>(pcm_size - 44);
    const uint32_t rate = AUDIO_SAMPLE_RATE;
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

std::string login()
{
    HttpBody body;
    if (!request(WATCH_PRIVATE_DUDUSERVER_API_BASE_URL "/auth/login", nullptr, body, WATCH_PRIVATE_LOGIN_BODY)) return {};
    cJSON *root = cJSON_Parse(body.data.c_str());
    cJSON *token = root ? cJSON_GetObjectItemCaseSensitive(root, "access_token") : nullptr;
    std::string value = cJSON_IsString(token) && token->valuestring ? token->valuestring : "";
    cJSON_Delete(root);
    return value;
}

lv_obj_t *label(lv_obj_t *parent, const char *value, const lv_font_t *font, uint32_t color)
{
    auto *object = lv_label_create(parent);
    lv_label_set_text(object, value);
    watch_display::apply_text_font(object, value, font);
    lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
    return object;
}

void style_button(lv_obj_t *button, uint32_t color)
{
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
}

bool encode_base64(const uint8_t *source, size_t source_size, std::string &encoded)
{
    if (!source || source_size == 0) return false;
    const size_t encoded_size = ((source_size + 2) / 3) * 4;
    // mbedTLS requires room for a trailing NUL even though the JSON payload uses the written length.
    const size_t target_size = encoded_size + 1;
    encoded.resize(target_size);
    size_t written = 0;
    if (mbedtls_base64_encode(reinterpret_cast<unsigned char *>(encoded.data()), target_size, &written,
                              source, source_size) != 0) return false;
    encoded.resize(written);
    return true;
}

bool decode_base64(const char *source, std::vector<uint8_t> &decoded)
{
    if (!source || !source[0]) return false;
    const size_t source_size = std::strlen(source);
    decoded.resize((source_size / 4) * 3 + 4);
    size_t written = 0;
    if (mbedtls_base64_decode(decoded.data(), decoded.size(), &written,
                              reinterpret_cast<const unsigned char *>(source), source_size) != 0) return false;
    decoded.resize(written);
    return true;
}

bool wait_live_audio_ready(esp_transport_handle_t ws, char *status, size_t status_size)
{
    char buffer[512] = {};
    std::string message;
    for (int i = 0; i < 20; ++i) {
        const int received = esp_transport_read(ws, buffer, sizeof(buffer) - 1, 1000);
        if (received == 0) continue;
        if (received < 0) {
            std::snprintf(status, status_size, "%s", "Live voice disconnected");
            return false;
        }
        if (esp_transport_ws_get_read_opcode(ws) != WS_TRANSPORT_OPCODES_TEXT &&
            esp_transport_ws_get_read_opcode(ws) != WS_TRANSPORT_OPCODES_CONT) {
            continue;
        }
        message.append(buffer, received);
        if (!esp_transport_ws_get_fin_flag(ws)) continue;
        cJSON *root = cJSON_ParseWithLength(message.data(), message.size());
        message.clear();
        if (!root) continue;
        cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
        cJSON *codec = cJSON_GetObjectItemCaseSensitive(root, "codec");
        cJSON *detail = cJSON_GetObjectItemCaseSensitive(root, "message");
        const char *event_type = cJSON_IsString(type) && type->valuestring ? type->valuestring : "";
        const char *codec_value = cJSON_IsString(codec) && codec->valuestring ? codec->valuestring : "";
        const char *detail_value = cJSON_IsString(detail) && detail->valuestring ? detail->valuestring : "";
        const bool ready = std::strcmp(event_type, "bridge.audio.ready") == 0 &&
                           std::strcmp(codec_value, "pcm") == 0;
        const bool error = std::strcmp(event_type, "error") == 0 ||
                           std::strcmp(event_type, "bridge.error") == 0;
        if (ready) {
            cJSON_Delete(root);
            return true;
        }
        if (error) {
            std::snprintf(status, status_size, "%s", detail_value[0] ? detail_value : "Live audio setup failed");
            cJSON_Delete(root);
            return false;
        }
        cJSON_Delete(root);
    }
    std::snprintf(status, status_size, "%s", "Voice service timeout");
    return false;
}

void append_text(char *target, size_t target_size, const char *suffix)
{
    if (!target || target_size == 0 || !suffix) return;
    size_t used = 0;
    while (used < target_size && target[used] != '\0') ++used;
    if (used + 1 < target_size) std::snprintf(target + used, target_size - used, "%s", suffix);
}

void replace_if_more_complete(char *target, size_t target_size, const char *candidate)
{
    if (!target || target_size == 0 || !candidate || !candidate[0]) return;
    const size_t current_len = std::strlen(target);
    const size_t candidate_len = std::strlen(candidate);
    if (current_len == 0 || candidate_len >= current_len) {
        std::snprintf(target, target_size, "%s", candidate);
    }
}
} // namespace

AiChatApp *AiChatApp::_instance = nullptr;
bool AiChatApp::_open_live_on_next_run = false;

AiChatApp *AiChatApp::requestInstance()
{
    if (!_instance) _instance = new AiChatApp();
    return _instance;
}

void AiChatApp::requestOpenLiveOnNextRun()
{
    _open_live_on_next_run = true;
}

AiChatApp::AiChatApp() : systems::phone::App(APP_NAME, watch_app_icon_ai_chat_48(), true, true, true)
{
    _mutex = xSemaphoreCreateMutex();
}

bool AiChatApp::run(void)
{
    auto *screen = lv_scr_act();
    ESP_UTILS_CHECK_NULL_RETURN(screen, false, "Invalid screen");
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x07080B), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    auto *root = lv_obj_create(screen);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_left(root, 14, 0);
    lv_obj_set_style_pad_right(root, 14, 0);
    // The phone shell owns the top status bar. Keep the chat header below it.
    lv_obj_set_style_pad_top(root, SAFE_TOP, 0);
    lv_obj_set_style_pad_bottom(root, 12, 0);
    lv_obj_set_style_pad_row(root, 7, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);

    auto *header = lv_obj_create(root);
    lv_obj_remove_style_all(header);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, 32);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    label(header, "AI Chat", &lv_font_montserrat_20, 0xFFFFFF);
    auto *back = lv_button_create(header);
    lv_obj_set_size(back, 46, 28);
    style_button(back, 0x29303A);
    lv_obj_add_event_cb(back, onBack, LV_EVENT_CLICKED, this);
    lv_obj_center(label(back, "Back", &lv_font_montserrat_12, 0xFFFFFF));

    _status_label = label(root, _status, &lv_font_montserrat_12, 0x9FAABA);
    lv_obj_set_width(_status_label, LV_PCT(100));
    lv_obj_set_height(_status_label, 18);
    lv_label_set_long_mode(_status_label, LV_LABEL_LONG_DOT);

    _messages_page = lv_obj_create(root);
    lv_obj_remove_style_all(_messages_page);
    lv_obj_set_width(_messages_page, LV_PCT(100));
    lv_obj_set_flex_grow(_messages_page, 1);
    lv_obj_set_flex_flow(_messages_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(_messages_page, 7, 0);
    lv_obj_set_scroll_dir(_messages_page, LV_DIR_VER);

    auto *actions = lv_obj_create(root);
    lv_obj_remove_style_all(actions); lv_obj_set_width(actions, LV_PCT(100)); lv_obj_set_height(actions, 42);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_column(actions, 7, 0);
    auto *input = lv_button_create(actions);
    lv_obj_set_width(input, LV_PCT(48)); lv_obj_set_height(input, 42);
    style_button(input, 0x315C9E);
    lv_obj_add_event_cb(input, onInput, LV_EVENT_CLICKED, this);
    lv_obj_center(label(input, "Input", &lv_font_montserrat_16, 0xFFFFFF));
    auto *voice = lv_button_create(actions);
    lv_obj_set_width(voice, LV_PCT(48)); lv_obj_set_height(voice, 42); style_button(voice, 0x2E7D5A);
    lv_obj_add_event_cb(voice, onVoice, LV_EVENT_CLICKED, this);
    lv_obj_center(label(voice, "Voice 4s", &lv_font_montserrat_14, 0xFFFFFF));

    auto *live = lv_button_create(root);
    lv_obj_set_size(live, LV_PCT(100), 34); style_button(live, 0x5A397F);
    lv_obj_add_event_cb(live, onLive, LV_EVENT_CLICKED, this);
    lv_obj_center(label(live, "Live Talk", &lv_font_montserrat_14, 0xFFFFFF));

    _timer = lv_timer_create(onTimer, 250, this);
    addMessage(false, "你好，我是云端 AI。输入问题开始对话。");
    refreshUi();
    if (_open_live_on_next_run) {
        _open_live_on_next_run = false;
        showLive();
    }
    return true;
}

bool AiChatApp::back(void)
{
    ++_generation;
    closeLive();
    closeInput();
    if (_timer) { lv_timer_delete(_timer); _timer = nullptr; }
    watch::audio_mic_stop();
    watch::audio_stream_stop();
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        _running = false;
        _voice_request = false;
        _live_request = false;
        _dirty = true;
        xSemaphoreGive(_mutex);
    }
    _status_label = _messages_page = nullptr;
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Close failed");
    return true;
}

void AiChatApp::onBack(lv_event_t *event) { if (auto *app = static_cast<AiChatApp *>(lv_event_get_user_data(event))) app->back(); }
void AiChatApp::onInput(lv_event_t *event) { if (auto *app = static_cast<AiChatApp *>(lv_event_get_user_data(event))) app->showInput(); }
void AiChatApp::onVoice(lv_event_t *event) { if (auto *app = static_cast<AiChatApp *>(lv_event_get_user_data(event))) app->startVoice(); }
void AiChatApp::onLive(lv_event_t *event) { if (auto *app = static_cast<AiChatApp *>(lv_event_get_user_data(event))) app->showLive(); }
void AiChatApp::onLiveTalk(lv_event_t *event)
{
    auto *app = static_cast<AiChatApp *>(lv_event_get_user_data(event));
    if (!app) return;
    if (app->_live_request && app->_running) {
        app->_live_stop_requested.store(true);
        if (app->_mutex && xSemaphoreTake(app->_mutex, pdMS_TO_TICKS(30)) == pdTRUE) {
            std::snprintf(app->_status, sizeof(app->_status), "%s", "Stopping recording...");
            app->_dirty = true;
            xSemaphoreGive(app->_mutex);
        }
        return;
    }
    if (!app->_running) app->startLiveVoice();
}
void AiChatApp::onLiveClose(lv_event_t *event) { if (auto *app = static_cast<AiChatApp *>(lv_event_get_user_data(event))) app->closeLive(); }
void AiChatApp::onSend(lv_event_t *event) { if (auto *app = static_cast<AiChatApp *>(lv_event_get_user_data(event))) app->submitText(); }
void AiChatApp::onCancel(lv_event_t *event) { if (auto *app = static_cast<AiChatApp *>(lv_event_get_user_data(event))) app->closeInput(); }
void AiChatApp::onKeyboardReady(lv_event_t *event) { if (auto *app = static_cast<AiChatApp *>(lv_event_get_user_data(event))) app->submitText(); }

void AiChatApp::showInput()
{
    if (_input_panel || _running) return;
    _input_panel = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(_input_panel);
    lv_obj_set_size(_input_panel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(_input_panel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(_input_panel, LV_OPA_50, 0);

    auto *sheet = lv_obj_create(_input_panel);
    lv_obj_set_size(sheet, LV_PCT(100), 230);
    lv_obj_align(sheet, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(sheet, lv_color_hex(0x111723), 0);
    lv_obj_set_style_bg_opa(sheet, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(sheet, lv_color_hex(0x3A4C69), 0);
    lv_obj_set_style_radius(sheet, 10, 0);
    lv_obj_set_style_pad_all(sheet, 8, 0);
    lv_obj_set_style_pad_row(sheet, 6, 0);
    lv_obj_set_flex_flow(sheet, LV_FLEX_FLOW_COLUMN);

    _input_textarea = lv_textarea_create(sheet);
    lv_obj_set_width(_input_textarea, LV_PCT(100));
    lv_obj_set_height(_input_textarea, 38);
    lv_textarea_set_one_line(_input_textarea, true);
    lv_textarea_set_max_length(_input_textarea, 220);
    lv_textarea_set_placeholder_text(_input_textarea, "Message");
    lv_obj_set_style_text_font(_input_textarea, &lv_font_source_han_sans_sc_14_cjk, 0);

    auto *actions = lv_obj_create(sheet);
    lv_obj_remove_style_all(actions);
    lv_obj_set_width(actions, LV_PCT(100));
    lv_obj_set_height(actions, 34);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(actions, 7, 0);
    auto *cancel = lv_button_create(actions);
    lv_obj_set_size(cancel, LV_PCT(48), 32); style_button(cancel, 0x29303A);
    lv_obj_add_event_cb(cancel, onCancel, LV_EVENT_CLICKED, this); lv_obj_center(label(cancel, "Cancel", &lv_font_montserrat_12, 0xFFFFFF));
    auto *send = lv_button_create(actions);
    lv_obj_set_size(send, LV_PCT(48), 32); style_button(send, 0x315C9E);
    lv_obj_add_event_cb(send, onSend, LV_EVENT_CLICKED, this); lv_obj_center(label(send, "Send", &lv_font_montserrat_12, 0xFFFFFF));

#if LV_USE_KEYBOARD
    _input_keyboard = lv_keyboard_create(sheet);
    lv_obj_set_width(_input_keyboard, LV_PCT(100));
    lv_obj_set_height(_input_keyboard, 142);
    lv_obj_set_style_text_font(_input_keyboard, &lv_font_source_han_sans_sc_14_cjk, LV_PART_ITEMS);
    lv_keyboard_set_textarea(_input_keyboard, _input_textarea);
    lv_obj_add_event_cb(_input_keyboard, onKeyboardReady, LV_EVENT_READY, this);
#if LV_USE_IME_PINYIN
    _input_ime = lv_ime_pinyin_create(sheet);
    lv_ime_pinyin_set_keyboard(_input_ime, _input_keyboard);
    lv_ime_pinyin_set_mode(_input_ime, LV_IME_PINYIN_MODE_K9);
    _input_candidates = lv_ime_pinyin_get_cand_panel(_input_ime);
    lv_obj_set_size(_input_candidates, LV_PCT(100), 30);
    lv_obj_add_flag(_input_candidates, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_style_bg_color(_input_candidates, lv_color_hex(0x182235), 0);
    lv_obj_set_style_bg_opa(_input_candidates, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(_input_candidates, &lv_font_source_han_sans_sc_14_cjk, 0);
    lv_obj_set_style_text_color(_input_candidates, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_color(_input_candidates, lv_color_hex(0xFFFFFF), LV_PART_ITEMS);
#endif
#endif
    lv_obj_add_state(_input_textarea, LV_STATE_FOCUSED);
}

void AiChatApp::closeInput()
{
    if (_input_panel && lv_obj_is_valid(_input_panel)) lv_obj_delete(_input_panel);
    _input_panel = _input_textarea = _input_keyboard = _input_ime = _input_candidates = nullptr;
}

void AiChatApp::submitText()
{
    if (!_input_textarea || _running) return;
    const char *value = lv_textarea_get_text(_input_textarea);
    if (!value || !value[0]) return;
    ++_generation;
    std::snprintf(_pending_message, sizeof(_pending_message), "%s", value);
    addMessage(true, _pending_message);
    closeInput();
    _running = true;
    _voice_request = false;
    _live_request = false;
    std::snprintf(_status, sizeof(_status), "%s", "AI is thinking...");
    _dirty = true;
    if (xTaskCreateWithCaps(chatTask, "watch_ai_chat", 10240, this, 2, &_task, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        _running = false;
        std::snprintf(_status, sizeof(_status), "%s", "Could not start chat task");
        _dirty = true;
    }
}

void AiChatApp::startVoice()
{
    if (_running) return;
    ++_generation;
    _voice_request = true;
    _live_request = false;
    _running = true;
    // Login can take several seconds. Do not tell the user to speak until the microphone is open.
    std::snprintf(_status, sizeof(_status), "%s", "Preparing voice...");
    _dirty = true;
    if (xTaskCreateWithCaps(chatTask, "watch_ai_voice", 12288, this, 2, &_task, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        _running = false; _voice_request = false;
        std::snprintf(_status, sizeof(_status), "%s", "Could not start voice task"); _dirty = true;
    }
}

void AiChatApp::showLive()
{
    if (_live_panel || _running) return;
    _live_panel = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(_live_panel);
    lv_obj_set_size(_live_panel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(_live_panel, lv_color_hex(0x070B12), 0);
    lv_obj_set_style_bg_opa(_live_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(_live_panel, 14, 0);
    lv_obj_set_style_pad_right(_live_panel, 14, 0);
    lv_obj_set_style_pad_top(_live_panel, LIVE_SAFE_TOP, 0);
    lv_obj_set_style_pad_bottom(_live_panel, 14, 0);
    lv_obj_set_style_pad_row(_live_panel, 7, 0);
    lv_obj_set_flex_flow(_live_panel, LV_FLEX_FLOW_COLUMN);

    auto *header = lv_obj_create(_live_panel);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), 30);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    label(header, "Live Talk", &lv_font_montserrat_20, 0xFFFFFF);
    auto *close = lv_button_create(header);
    lv_obj_set_size(close, 48, 28); style_button(close, 0x29303A);
    lv_obj_add_event_cb(close, onLiveClose, LV_EVENT_CLICKED, this);
    lv_obj_center(label(close, "Back", &lv_font_montserrat_12, 0xFFFFFF));

    _live_status_label = label(_live_panel, "Ready. Tap Talk, then speak.", &lv_font_montserrat_14, 0x8CB8E8);
    lv_obj_set_width(_live_status_label, LV_PCT(100));
    lv_label_set_long_mode(_live_status_label, LV_LABEL_LONG_WRAP);

    auto *transcript_box = lv_obj_create(_live_panel);
    lv_obj_set_width(transcript_box, LV_PCT(100)); lv_obj_set_height(transcript_box, 70);
    lv_obj_set_style_radius(transcript_box, 8, 0); lv_obj_set_style_bg_color(transcript_box, lv_color_hex(0x132238), 0);
    lv_obj_set_style_border_width(transcript_box, 0, 0); lv_obj_set_style_pad_all(transcript_box, 8, 0);
    lv_obj_set_style_pad_row(transcript_box, 4, 0);
    lv_obj_set_flex_flow(transcript_box, LV_FLEX_FLOW_COLUMN);
    label(transcript_box, "You", &lv_font_montserrat_12, 0x91C8FF);
    _live_transcript_label = label(transcript_box, _live_transcript[0] ? _live_transcript : "...", &lv_font_watch_zh_14, 0xFFFFFF);
    lv_obj_set_width(_live_transcript_label, LV_PCT(100)); lv_label_set_long_mode(_live_transcript_label, LV_LABEL_LONG_WRAP);

    auto *reply_box = lv_obj_create(_live_panel);
    lv_obj_set_width(reply_box, LV_PCT(100)); lv_obj_set_flex_grow(reply_box, 1);
    lv_obj_set_style_radius(reply_box, 8, 0); lv_obj_set_style_bg_color(reply_box, lv_color_hex(0x17251F), 0);
    lv_obj_set_style_border_width(reply_box, 0, 0); lv_obj_set_style_pad_all(reply_box, 8, 0);
    lv_obj_set_style_pad_row(reply_box, 4, 0);
    lv_obj_set_flex_flow(reply_box, LV_FLEX_FLOW_COLUMN);
    label(reply_box, "AI", &lv_font_montserrat_12, 0x76E0B3);
    _live_reply_label = label(reply_box, _live_reply[0] ? _live_reply : "Short voice turns for faster replies.", &lv_font_watch_zh_14, 0xFFFFFF);
    lv_obj_set_width(_live_reply_label, LV_PCT(100)); lv_label_set_long_mode(_live_reply_label, LV_LABEL_LONG_WRAP);

    auto *talk = lv_button_create(_live_panel);
    lv_obj_set_size(talk, LV_PCT(100), 52); style_button(talk, 0x2E7D5A);
    lv_obj_add_event_cb(talk, onLiveTalk, LV_EVENT_CLICKED, this);
    lv_obj_center(label(talk, "Start / Stop", &lv_font_montserrat_16, 0xFFFFFF));
}

void AiChatApp::closeLive()
{
    ++_generation;
    _live_stop_requested.store(true);
    watch::audio_mic_stop();
    // The live PCM playback task owns the speaker stream.  Closing it from
    // the LVGL event handler races a DMA write and causes the click/garble
    // seen on interrupted replies; the worker observes the generation change
    // and stops its stream itself.
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        _running = false;
        _voice_request = false;
        _live_request = false;
        std::snprintf(_status, sizeof(_status), "%s", "Live talk cancelled");
        _dirty = true;
        xSemaphoreGive(_mutex);
    }
    if (_live_panel && lv_obj_is_valid(_live_panel)) lv_obj_delete(_live_panel);
    _live_panel = _live_status_label = _live_transcript_label = _live_reply_label = nullptr;
}

void AiChatApp::startLiveVoice()
{
    if (_running) return;
    _live_stop_requested.store(false);
    _live_capturing.store(false);
    _live_request = true;
    _running = true;
    std::snprintf(_status, sizeof(_status), "%s", "Preparing live talk...");
    _dirty = true;
    _live_transcript[0] = '\0';
    _live_reply[0] = '\0';
    if (xTaskCreateWithCaps(realtimeTask, "watch_ai_live", 14336, this, 2, &_task, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        _live_request = false; _running = false;
        std::snprintf(_status, sizeof(_status), "%s", "Could not start live talk"); _dirty = true;
    }
}

void AiChatApp::realtimeTask(void *arg)
{
    auto *app = static_cast<AiChatApp *>(arg);
    if (!app) { vTaskDeleteWithCaps(nullptr); return; }
    const uint32_t generation = app->_generation;
    char final_status[96] = "Live talk failed";
    bool have_reply = false;
    int live_audio_chunks = 0;
    size_t live_audio_bytes = 0;
    std::vector<uint8_t> live_audio_pcm;
    LivePcmPlayer live_player;
    live_player.stream = xStreamBufferCreateWithCaps(
        LIVE_AUDIO_STREAM_CAPACITY,
        1,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const bool live_streaming_available = live_player.stream != nullptr;
    esp_transport_handle_t tcp = nullptr;
    esp_transport_handle_t ws = nullptr;

    auto start_live_player = [&]() {
        if (!live_streaming_available || live_player.task != nullptr || live_player.finished.load()) {
            return false;
        }
        live_player.cancel.store(false);
        live_player.done.store(false);
        live_player.finished.store(false);
        live_player.result.store(ESP_FAIL);
        if (xTaskCreateWithCaps(
                live_pcm_player_task,
                "watch_ai_pcm",
                4096,
                &live_player,
                5,
                &live_player.task,
                MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) != pdPASS) {
            live_player.task = nullptr;
            live_player.result.store(ESP_ERR_NO_MEM);
            live_player.finished.store(true);
            return false;
        }
        return true;
    };

    auto finish_live_player = [&](bool cancel) {
        if (live_player.stream == nullptr) return true;
        live_player.done.store(true);
        live_player.cancel.store(cancel);
        if (live_player.task == nullptr) {
            vStreamBufferDeleteWithCaps(live_player.stream);
            live_player.stream = nullptr;
            return live_player.finished.load() || live_player.bytes_received == 0;
        }
        const size_t audio_ms = live_audio_bytes * 1000 /
                                (sizeof(int16_t) * static_cast<size_t>(LIVE_REPLY_PLAYBACK_RATE));
        const int timeout_ms = cancel
            ? 1500
            : std::clamp(static_cast<int>(audio_ms + 2500), 2500, 30000);
        for (int elapsed_ms = 0; elapsed_ms < timeout_ms && !live_player.finished.load(); elapsed_ms += 10) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (!live_player.finished.load()) {
            // Do not free the stream buffer beneath an active task. Ask the
            // worker to stop, then give the codec/I2S write a bounded time to
            // return. This also avoids concurrent audio_stream_stop() calls.
            live_player.cancel.store(true);
            for (int elapsed_ms = 0; elapsed_ms < 1500 && !live_player.finished.load(); elapsed_ms += 10) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }
        if (!live_player.finished.load()) {
            ESP_LOGW(APP_NAME, "live PCM player did not stop before worker cleanup");
            // The player has the stream buffer on the realtime task's stack.
            // Keep that storage alive until the player leaves its final I2S
            // write; returning here would turn a slow codec shutdown into a
            // use-after-free crash.
            while (!live_player.finished.load()) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }
        live_player.task = nullptr;
        vStreamBufferDeleteWithCaps(live_player.stream);
        live_player.stream = nullptr;
        return true;
    };

    auto update = [app, generation](const char *status) {
        if (app->_mutex && xSemaphoreTake(app->_mutex, pdMS_TO_TICKS(30)) == pdTRUE) {
            if (generation == app->_generation) {
                std::snprintf(app->_status, sizeof(app->_status), "%s", status);
                app->_dirty = true;
            }
            xSemaphoreGive(app->_mutex);
        }
    };
    uint8_t pending_pcm_byte = 0;
    bool has_pending_pcm_byte = false;
    auto cache_live_audio = [&](const uint8_t *data, size_t size) {
        if (data == nullptr || size == 0) return;
        uint8_t pair[2] = {};
        if (has_pending_pcm_byte) {
            pair[0] = pending_pcm_byte;
            pair[1] = data[0];
            data += 1;
            size -= 1;
            has_pending_pcm_byte = false;
            if (live_streaming_available) {
                const size_t sent = xStreamBufferSend(
                    live_player.stream, pair, sizeof(pair), pdMS_TO_TICKS(300));
                live_player.bytes_received += sent;
                live_audio_bytes += sent;
            } else if (live_audio_pcm.size() + sizeof(pair) <= 384000) {
                live_audio_pcm.insert(live_audio_pcm.end(), pair, pair + sizeof(pair));
                live_audio_bytes += sizeof(pair);
            }
        }
        if (size == 0) return;
        if ((size & 1U) != 0) {
            pending_pcm_byte = data[size - 1];
            has_pending_pcm_byte = true;
            --size;
        }
        if (size == 0) return;
        if (live_streaming_available) {
            const size_t sent = xStreamBufferSend(
                live_player.stream,
                data,
                size,
                pdMS_TO_TICKS(600));
            if (sent != size) {
                ESP_LOGW(APP_NAME,
                         "live voice audio stream buffer short sent=%u expected=%u",
                         static_cast<unsigned>(sent),
                         static_cast<unsigned>(size));
            }
            live_player.bytes_received += sent;
            live_audio_bytes += sent;
            ++live_audio_chunks;
            if (live_audio_chunks <= 3) {
                ESP_LOGI(APP_NAME, "live voice audio cached chunk=%d bytes=%u total=%u",
                         live_audio_chunks,
                         static_cast<unsigned>(sent),
                         static_cast<unsigned>(live_audio_bytes));
            }
            if (live_player.task == nullptr &&
                live_player.bytes_received >= LIVE_AUDIO_PREBUFFER_BYTES) {
                if (start_live_player()) {
                    update("Playing reply...");
                    ESP_LOGI(APP_NAME,
                             "live voice playback started after prebuffer bytes=%u",
                             static_cast<unsigned>(live_player.bytes_received));
                }
            } else if ((live_audio_chunks % 8) == 0) {
                char speaking_status[48] = {};
                std::snprintf(speaking_status, sizeof(speaking_status), "Buffering audio... %d", live_audio_chunks);
                update(speaking_status);
            }
        } else {
            constexpr size_t max_live_audio_bytes = 384000;
            if (live_audio_pcm.size() + size <= max_live_audio_bytes) {
                live_audio_pcm.insert(live_audio_pcm.end(), data, data + size);
                ++live_audio_chunks;
                live_audio_bytes += size;
            }
        }
    };
    auto cleanup = [&]() {
        watch::audio_mic_stop();
        if (live_player.stream != nullptr) {
            live_player.cancel.store(true);
            live_player.done.store(true);
            finish_live_player(true);
        }
        if (ws) { esp_transport_close(ws); esp_transport_destroy(ws); ws = nullptr; }
        if (tcp) { esp_transport_destroy(tcp); tcp = nullptr; }
    };

    if (!watch::wifi_is_connected()) {
        std::snprintf(final_status, sizeof(final_status), "%s", "WiFi not connected");
    } else {
        update("Connecting live voice...");
        const std::string token = login();
        if (token.empty()) {
            std::snprintf(final_status, sizeof(final_status), "%s", "Cloud login failed");
        } else {
            tcp = esp_transport_tcp_init();
            ws = tcp ? esp_transport_ws_init(tcp) : nullptr;
            const std::string path = std::string(DUPLEX_PATH) + token;
            if (!ws) {
                std::snprintf(final_status, sizeof(final_status), "%s", "Not enough memory for live voice");
            } else {
                esp_transport_ws_set_path(ws, path.c_str());
                esp_transport_ws_set_user_agent(ws, "esp32-watch-live-voice");
                if (esp_transport_connect(ws, WATCH_PRIVATE_SERVER_HOST, 80, 12000) < 0 ||
                    esp_transport_ws_get_upgrade_request_status(ws) != 101) {
                    std::snprintf(final_status, sizeof(final_status), "%s", "Live voice connection failed");
                } else {
                    constexpr const char *pcm_start_event = "{\"type\":\"audio.start\",\"codec\":\"pcm\",\"rate\":16000,\"channels\":1,\"frame_ms\":60}";
                    if (esp_transport_ws_send_raw(ws, WS_TEXT_FIN, pcm_start_event,
                                                  std::strlen(pcm_start_event), 3000) != static_cast<int>(std::strlen(pcm_start_event))) {
                    std::snprintf(final_status, sizeof(final_status), "%s", "Live audio setup failed");
                } else if (update("Connecting voice service..."), !wait_live_audio_ready(ws, final_status, sizeof(final_status))) {
                    ESP_LOGW(APP_NAME, "live voice setup handshake failed: %s", final_status);
                } else if (watch::audio_mic_start(AUDIO_SAMPLE_RATE, 1) != ESP_OK) {
                    std::snprintf(final_status, sizeof(final_status), "%s", "Microphone unavailable");
                } else {
                    update("Get ready...");
                    uint8_t frame[LIVE_FRAME_BYTES] = {};
                    for (int warmup = 0; warmup < LIVE_MIC_WARMUP_FRAMES && generation == app->_generation; ++warmup) {
                        size_t warmup_read = 0;
                        if (watch::audio_mic_read(frame, sizeof(frame), &warmup_read) != ESP_OK ||
                            warmup_read != sizeof(frame)) {
                            ESP_LOGW(APP_NAME, "live voice mic warmup failed: frame=%d bytes=%u",
                                     warmup,
                                     static_cast<unsigned>(warmup_read));
                            break;
                        }
                    }
                    update("Recording... tap Talk to stop (max 4s)");
                    bool sent_audio = false;
                    const char *capture_failure = nullptr;
                    const int frames = static_cast<int>((AUDIO_SAMPLE_RATE * LIVE_VOICE_MS / 1000 * sizeof(int16_t)) / LIVE_FRAME_BYTES);
                    constexpr int min_capture_frames = 4; // 240 ms minimum
                    PcmStats capture_stats = {};
                    app->_live_capturing.store(true);
                    ESP_LOGI(APP_NAME, "live voice PCM capture start: frames=%d frame_bytes=%u free_heap=%u largest_internal=%u stack_watermark=%u",
                             frames,
                             static_cast<unsigned>(sizeof(frame)),
                             static_cast<unsigned>(esp_get_free_heap_size()),
                             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
                    for (int index = 0; index < frames && generation == app->_generation; ++index) {
                        if (app->_live_stop_requested.load() && index >= min_capture_frames) break;
                        size_t read = 0;
                        if (watch::audio_mic_read(frame, sizeof(frame), &read) != ESP_OK || read != sizeof(frame)) {
                            capture_failure = "Microphone read failed";
                            ESP_LOGW(APP_NAME, "live voice mic frame failed: frame=%d bytes=%u", index, static_cast<unsigned>(read));
                            break;
                        }
                        update_pcm_stats(frame, read, capture_stats);
                        const int sent = esp_transport_ws_send_raw(ws, WS_BINARY_FIN,
                                                                   reinterpret_cast<const char *>(frame), read, 3000);
                        if (sent != static_cast<int>(read)) {
                            capture_failure = "Live audio send failed";
                            ESP_LOGW(APP_NAME, "live voice PCM send failed: frame=%d sent=%d expected=%u", index, sent, static_cast<unsigned>(read));
                            break;
                        }
                        sent_audio = true;
                    }
                    app->_live_capturing.store(false);
                    watch::audio_mic_stop();
                    ESP_LOGI(APP_NAME, "live voice PCM capture done: samples=%u peak=%u avg_abs=%u sent=%d",
                             static_cast<unsigned>(capture_stats.samples),
                             static_cast<unsigned>(capture_stats.peak),
                             static_cast<unsigned>(capture_stats.avg_abs),
                             sent_audio ? 1 : 0);
                    if (!sent_audio || generation != app->_generation) {
                        std::snprintf(final_status, sizeof(final_status), "%s", capture_failure ? capture_failure : "Live voice cancelled");
                    } else if (esp_transport_ws_send_raw(ws, WS_TEXT_FIN, "{\"type\":\"commit\"}", 17, 3000) != 17) {
                        std::snprintf(final_status, sizeof(final_status), "%s", "Could not send live voice");
                    } else {
                        update("Listening for reply...");
                        std::string message;
                        constexpr size_t ws_read_buffer_bytes = 12 * 1024;
                        char *buffer = static_cast<char *>(
                            heap_caps_malloc(ws_read_buffer_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
                        int idle_count = 0;
                        bool complete = false;
                        // esp_transport_read() is allowed to return a
                        // partial payload even when the WebSocket FIN bit is
                        // already set. Keep the current frame payload
                        // accumulated until all declared bytes have arrived;
                        // otherwise a large JSON audio.delta is parsed as two
                        // invalid JSON fragments and silently discarded.
                        int ws_frame_payload_len = -1;
                        size_t ws_frame_payload_read = 0;
                        live_audio_pcm.reserve(256 * 1024);
                        if (buffer == nullptr) {
                            std::snprintf(final_status, sizeof(final_status), "%s", "Not enough memory for voice");
                            complete = true;
                        }
                        while (buffer != nullptr && !complete && generation == app->_generation && idle_count < 30) {
                            const int received = esp_transport_read(ws, buffer, static_cast<int>(ws_read_buffer_bytes - 1), 1000);
                            if (received == 0) { ++idle_count; continue; }
                            if (received < 0) { std::snprintf(final_status, sizeof(final_status), "%s", "Live voice disconnected"); break; }
                            idle_count = 0;
                            const ws_transport_opcodes_t read_opcode = esp_transport_ws_get_read_opcode(ws);
                            if (read_opcode == WS_TRANSPORT_OPCODES_BINARY) {
                                have_reply = true;
                                cache_live_audio(reinterpret_cast<const uint8_t *>(buffer),
                                                 static_cast<size_t>(received));
                                continue;
                            }
                            if (read_opcode != WS_TRANSPORT_OPCODES_TEXT &&
                                read_opcode != WS_TRANSPORT_OPCODES_CONT) continue;
                            const int declared_payload_len = esp_transport_ws_get_read_payload_len(ws);
                            if (ws_frame_payload_len < 0 ||
                                declared_payload_len != ws_frame_payload_len) {
                                ws_frame_payload_len = declared_payload_len;
                                ws_frame_payload_read = 0;
                            }
                            message.append(buffer, received);
                            ws_frame_payload_read += static_cast<size_t>(received);
                            const bool frame_payload_complete =
                                (declared_payload_len <= 0) ||
                                (ws_frame_payload_read >= static_cast<size_t>(declared_payload_len));
                            if (!frame_payload_complete) {
                                if (ws_frame_payload_read == static_cast<size_t>(received)) {
                                    ESP_LOGD(APP_NAME,
                                             "live voice ws partial frame bytes=%u/%u opcode=%d fin=%d",
                                             static_cast<unsigned>(ws_frame_payload_read),
                                             static_cast<unsigned>(declared_payload_len),
                                             static_cast<int>(read_opcode),
                                             esp_transport_ws_get_fin_flag(ws) ? 1 : 0);
                                }
                                continue;
                            }
                            if (!esp_transport_ws_get_fin_flag(ws)) {
                                ws_frame_payload_len = -1;
                                ws_frame_payload_read = 0;
                                continue;
                            }
                            cJSON *root = cJSON_ParseWithLength(message.data(), message.size());
                            message.clear();
                            ws_frame_payload_len = -1;
                            ws_frame_payload_read = 0;
                            if (!root) continue;
                            cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
                            const char *event_type = cJSON_IsString(type) && type->valuestring ? type->valuestring : "";
                            cJSON *delta = cJSON_GetObjectItemCaseSensitive(root, "delta");
                            cJSON *text = cJSON_GetObjectItemCaseSensitive(root, "text");
                            cJSON *transcript = cJSON_GetObjectItemCaseSensitive(root, "transcript");
                            cJSON *detail = cJSON_GetObjectItemCaseSensitive(root, "message");
                            const char *value = cJSON_IsString(delta) && delta->valuestring ? delta->valuestring :
                                                (cJSON_IsString(text) && text->valuestring ? text->valuestring : "");
                            const char *completed_value = cJSON_IsString(text) && text->valuestring ? text->valuestring :
                                                          (cJSON_IsString(transcript) && transcript->valuestring ? transcript->valuestring : value);
                            if (std::strcmp(event_type, "conversation.item.input_audio_transcription.delta") == 0) {
                                if (app->_mutex && xSemaphoreTake(app->_mutex, pdMS_TO_TICKS(30)) == pdTRUE) {
                                    if (generation == app->_generation) { append_text(app->_live_transcript, sizeof(app->_live_transcript), value); app->_dirty = true; }
                                    xSemaphoreGive(app->_mutex);
                                }
                            } else if (std::strcmp(event_type, "conversation.item.input_audio_transcription.completed") == 0) {
                                if (app->_mutex && xSemaphoreTake(app->_mutex, pdMS_TO_TICKS(30)) == pdTRUE) {
                                    if (generation == app->_generation && completed_value[0]) {
                                        replace_if_more_complete(app->_live_transcript, sizeof(app->_live_transcript), completed_value);
                                        app->_dirty = true;
                                    }
                                    xSemaphoreGive(app->_mutex);
                                }
                            } else if (std::strcmp(event_type, "response.output_text.delta") == 0) {
                                if (internal_voice_text(value)) {
                                    cJSON_Delete(root);
                                    continue;
                                }
                                if (app->_mutex && xSemaphoreTake(app->_mutex, pdMS_TO_TICKS(30)) == pdTRUE) {
                                    if (generation == app->_generation) { append_text(app->_live_reply, sizeof(app->_live_reply), value); app->_dirty = true; }
                                    xSemaphoreGive(app->_mutex);
                                }
                                have_reply = true;
                            } else if (std::strcmp(event_type, "response.output_text.done") == 0) {
                                have_reply = true;
                            } else if (std::strcmp(event_type, "response.output_audio.delta") == 0) {
                                have_reply = true;
                                std::vector<uint8_t> pcm;
                                if (decode_base64(value, pcm)) {
                                    const size_t bytes = pcm.size() & ~static_cast<size_t>(1);
                                    if (bytes > 0) cache_live_audio(pcm.data(), bytes);
                                } else {
                                    ESP_LOGW(APP_NAME,
                                             "live voice audio base64 decode failed chars=%u",
                                             static_cast<unsigned>(std::strlen(value)));
                                }
                            } else if (std::strcmp(event_type, "response.output_audio.done") == 0) {
                                complete = true;
                                if (has_pending_pcm_byte) {
                                    ESP_LOGW(APP_NAME, "live voice audio dropped trailing odd byte");
                                    has_pending_pcm_byte = false;
                                }
                                ESP_LOGI(APP_NAME, "live voice audio done chunks=%d bytes=%u",
                                         live_audio_chunks,
                                         static_cast<unsigned>(live_audio_bytes));
                                if (live_streaming_available && generation == app->_generation) {
                                    if (live_player.task == nullptr && live_player.bytes_received > 0) {
                                        if (start_live_player()) {
                                            update("Playing reply...");
                                        }
                                    }
                                    const bool playback_finished = finish_live_player(false);
                                    if (!playback_finished || live_player.result.load() != ESP_OK) {
                                        std::snprintf(final_status, sizeof(final_status), "%s", "Voice playback failed");
                                    }
                                } else if (!live_audio_pcm.empty() && generation == app->_generation) {
                                    update("Playing reply...");
                                    ESP_LOGI(APP_NAME, "live voice playback rate=%d pcm_bytes=%u",
                                             LIVE_REPLY_PLAYBACK_RATE,
                                             static_cast<unsigned>(live_audio_pcm.size() & ~static_cast<size_t>(1)));
                                    const esp_err_t play_ret = watch::audio_play_pcm_blocking(
                                        reinterpret_cast<const int16_t *>(live_audio_pcm.data()),
                                        live_audio_pcm.size() & ~static_cast<size_t>(1),
                                        LIVE_REPLY_PLAYBACK_RATE);
                                    ESP_LOGI(APP_NAME, "live voice buffered playback ret=%d bytes=%u",
                                             static_cast<int>(play_ret),
                                             static_cast<unsigned>(live_audio_pcm.size() & ~static_cast<size_t>(1)));
                                    if (play_ret != ESP_OK) {
                                        std::snprintf(final_status, sizeof(final_status), "%s", "Voice playback failed");
                                    }
                                }
                                std::snprintf(final_status, sizeof(final_status), "%s", "Reply ready");
                            } else if (std::strcmp(event_type, "error") == 0 || std::strcmp(event_type, "bridge.error") == 0) {
                                const char *detail_text = cJSON_IsString(detail) && detail->valuestring ? detail->valuestring : "";
                                std::snprintf(final_status, sizeof(final_status), "%s", friendly_voice_error(detail_text));
                                complete = true;
                            }
                            cJSON_Delete(root);
                        }
                        heap_caps_free(buffer);
                        if (!have_reply && std::strcmp(final_status, "Reply ready") == 0) {
                            std::snprintf(final_status, sizeof(final_status), "%s", "Voice reply was empty");
                        }
                    }
                }
                }
            }
        }
    }
    cleanup();
    if (app->_mutex && xSemaphoreTake(app->_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (generation == app->_generation) {
            if (app->_live_transcript[0]) app->addMessage(true, app->_live_transcript);
            if (app->_live_reply[0]) app->addMessage(false, app->_live_reply);
            std::snprintf(app->_status, sizeof(app->_status), "%s", final_status);
            app->_running = false;
            app->_live_request = false;
            app->_dirty = true;
        }
        xSemaphoreGive(app->_mutex);
    }
    app->_task = nullptr;
    vTaskDeleteWithCaps(nullptr);
}

void AiChatApp::addMessage(bool user, const char *text)
{
    if (!text || !text[0]) return;
    if (_message_count == MAX_MESSAGES) {
        std::memmove(&_messages[0], &_messages[1], sizeof(Message) * (MAX_MESSAGES - 1));
        --_message_count;
    }
    auto &message = _messages[_message_count++];
    message.user = user;
    std::snprintf(message.text, sizeof(message.text), "%s", text);
    _dirty = true;
}

void AiChatApp::chatTask(void *arg)
{
    auto *app = static_cast<AiChatApp *>(arg);
    if (!app) { vTaskDeleteWithCaps(nullptr); return; }
    const uint32_t generation = app->_generation;
    char prompt[sizeof(app->_pending_message)] = {};
    std::snprintf(prompt, sizeof(prompt), "%s", app->_pending_message);
    std::string reply;
    std::string tts_url;
    char status[96] = {};
    std::string token;
    if (!watch::wifi_is_connected()) {
        std::snprintf(status, sizeof(status), "%s", "WiFi not connected");
    } else {
        token = login();
        if (token.empty()) {
            std::snprintf(status, sizeof(status), "%s", "Cloud login failed");
        } else if (app->_voice_request || app->_live_request) {
            const size_t capture_bytes = app->_live_request
                ? (AUDIO_SAMPLE_RATE * LIVE_VOICE_MS / 1000 * sizeof(int16_t))
                : VOICE_BYTES;
            uint8_t *audio = static_cast<uint8_t *>(heap_caps_malloc(capture_bytes + 44, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            size_t recorded = 0;
            if (!audio) {
                std::snprintf(status, sizeof(status), "%s", "Not enough memory for recording");
            } else if (watch::audio_mic_start(AUDIO_SAMPLE_RATE, 1) != ESP_OK) {
                std::snprintf(status, sizeof(status), "%s", "Microphone unavailable");
                heap_caps_free(audio);
            } else {
                if (app->_mutex && xSemaphoreTake(app->_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                    std::snprintf(app->_status, sizeof(app->_status), "%s", app->_live_request ? "Speak now..." : "Speak now (4 seconds)...");
                    app->_dirty = true;
                    xSemaphoreGive(app->_mutex);
                }
                // Let the status repaint before the capture window begins.
                vTaskDelay(pdMS_TO_TICKS(350));
                PcmStats capture_stats = {};
                while (recorded < capture_bytes) {
                    size_t read = 0;
                    const size_t want = std::min(static_cast<size_t>(1024), capture_bytes - recorded);
                    if (watch::audio_mic_read(audio + 44 + recorded, want, &read) != ESP_OK || read == 0) break;
                    update_pcm_stats(audio + 44 + recorded, read, capture_stats);
                    recorded += read;
                }
                watch::audio_mic_stop();
                ESP_LOGI(APP_NAME, "voice PCM capture done: bytes=%u samples=%u peak=%u avg_abs=%u live=%d",
                         static_cast<unsigned>(recorded),
                         static_cast<unsigned>(capture_stats.samples),
                         static_cast<unsigned>(capture_stats.peak),
                         static_cast<unsigned>(capture_stats.avg_abs),
                         app->_live_request ? 1 : 0);
                if (recorded < 16000) {
                    std::snprintf(status, sizeof(status), "%s", "Recording too short");
                } else {
                    write_wav_header(audio, recorded + 44);
                    if (app->_mutex && xSemaphoreTake(app->_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                        std::snprintf(app->_status, sizeof(app->_status), "%s", "Recognizing voice..."); app->_dirty = true; xSemaphoreGive(app->_mutex);
                    }
                    HttpBody body;
                    if (!request_audio(token.c_str(), audio, recorded + 44, body)) {
                        describe_audio_error(body, status, sizeof(status));
                    } else {
                        cJSON *response = cJSON_Parse(body.data.c_str());
                        cJSON *transcript = response ? cJSON_GetObjectItemCaseSensitive(response, "transcript_text") : nullptr;
                        cJSON *text = response ? cJSON_GetObjectItemCaseSensitive(response, "reply_text") : nullptr;
                        cJSON *audio_url = response ? cJSON_GetObjectItemCaseSensitive(response, "audio_url") : nullptr;
                        if (cJSON_IsString(transcript) && transcript->valuestring) app->addMessage(true, transcript->valuestring);
                        if (app->_live_request && cJSON_IsString(transcript) && transcript->valuestring) {
                            std::snprintf(app->_live_transcript, sizeof(app->_live_transcript), "%s", transcript->valuestring);
                        }
                        if (cJSON_IsString(text) && text->valuestring) reply = text->valuestring;
                        if (cJSON_IsString(audio_url) && audio_url->valuestring) tts_url = audio_url->valuestring;
                        cJSON_Delete(response);
                        if (reply.empty()) std::snprintf(status, sizeof(status), "%s", "Voice reply was empty");
                    }
                }
                heap_caps_free(audio);
            }
        } else {
            cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "device_id", DEVICE_ID);
            cJSON_AddStringToObject(root, "message", prompt);
            cJSON_AddStringToObject(root, "input_type", "text");
            char *payload = cJSON_PrintUnformatted(root);
            HttpBody body;
            const bool ok = payload && request(WATCH_PRIVATE_DUDUSERVER_API_BASE_URL "/ai-call", token.c_str(), body, payload);
            if (payload) cJSON_free(payload);
            cJSON_Delete(root);
            if (!ok) {
                std::snprintf(status, sizeof(status), "AI request failed (%d)", body.status);
            } else {
                cJSON *response = cJSON_Parse(body.data.c_str());
                cJSON *text = response ? cJSON_GetObjectItemCaseSensitive(response, "reply_text") : nullptr;
                if (cJSON_IsString(text) && text->valuestring && text->valuestring[0]) reply = text->valuestring;
                cJSON_Delete(response);
                if (reply.empty()) std::snprintf(status, sizeof(status), "%s", "AI returned an empty reply");
            }
        }
    }
    bool reply_committed = false;
    if (!reply.empty() && generation == app->_generation) {
        if (app->_mutex && xSemaphoreTake(app->_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            app->addMessage(false, reply.c_str());
            xSemaphoreGive(app->_mutex);
        } else {
            app->addMessage(false, reply.c_str());
        }
        reply_committed = true;
    }
    if (!tts_url.empty() && generation == app->_generation) {
        if (app->_mutex && xSemaphoreTake(app->_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            std::snprintf(app->_status, sizeof(app->_status), "%s", "Playing voice reply..."); app->_dirty = true; xSemaphoreGive(app->_mutex);
        }
        if (!download_tts_and_play(tts_url.c_str(), token.c_str())) {
            std::snprintf(status, sizeof(status), "%s", "Reply ready (voice unavailable)");
        }
    }
    if (app->_mutex && xSemaphoreTake(app->_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (generation == app->_generation) {
            if (!reply.empty()) {
                if (app->_live_request) std::snprintf(app->_live_reply, sizeof(app->_live_reply), "%s", reply.c_str());
            }
            if (!reply.empty() && !reply_committed) {
                app->addMessage(false, reply.c_str());
            } else {
                std::snprintf(app->_status, sizeof(app->_status), "%s", status[0] ? status : "AI request failed");
            }
            if (!reply.empty()) {
                std::snprintf(app->_status, sizeof(app->_status), "%s", "Reply ready");
            }
            app->_running = false;
            app->_voice_request = false;
            app->_live_request = false;
            app->_dirty = true;
        }
        xSemaphoreGive(app->_mutex);
    }
    app->_task = nullptr;
    vTaskDeleteWithCaps(nullptr);
}

void AiChatApp::onTimer(lv_timer_t *timer)
{
    auto *app = static_cast<AiChatApp *>(lv_timer_get_user_data(timer));
    if (app) app->refreshUi();
}

void AiChatApp::refreshUi()
{
    bool dirty = false;
    if (_mutex && xSemaphoreTake(_mutex, 0) == pdTRUE) {
        dirty = _dirty;
        _dirty = false;
        xSemaphoreGive(_mutex);
    }
    if (!dirty) return;
    if (_status_label && lv_obj_is_valid(_status_label)) watch_display::set_label_text(_status_label, _status, &lv_font_montserrat_12);
    if (_live_status_label && lv_obj_is_valid(_live_status_label)) watch_display::set_label_text(_live_status_label, _status, &lv_font_montserrat_14);
    if (_live_transcript_label && lv_obj_is_valid(_live_transcript_label)) watch_display::set_label_text(_live_transcript_label, _live_transcript[0] ? _live_transcript : "...", &lv_font_watch_zh_14);
    if (_live_reply_label && lv_obj_is_valid(_live_reply_label)) watch_display::set_label_text(_live_reply_label, _live_reply[0] ? _live_reply : "...", &lv_font_watch_zh_14);
    rebuildMessages();
}

void AiChatApp::rebuildMessages()
{
    if (!_messages_page || !lv_obj_is_valid(_messages_page)) return;
    lv_obj_clean(_messages_page);
    for (uint8_t i = 0; i < _message_count; ++i) {
        const auto &message = _messages[i];
        auto *bubble = lv_obj_create(_messages_page);
        lv_obj_set_width(bubble, LV_PCT(100));
        lv_obj_set_height(bubble, LV_SIZE_CONTENT);
        lv_obj_set_style_min_height(bubble, 42, 0);
        lv_obj_set_style_radius(bubble, 8, 0);
        lv_obj_set_style_bg_color(bubble, lv_color_hex(message.user ? 0x214B82 : 0x1A202A), 0);
        lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(bubble, 0, 0);
        lv_obj_set_style_pad_all(bubble, 8, 0);
        lv_obj_set_style_pad_row(bubble, 3, 0);
        lv_obj_set_flex_flow(bubble, LV_FLEX_FLOW_COLUMN);
        auto *role = label(bubble, message.user ? "You" : "AI", &lv_font_montserrat_12, message.user ? 0x91C8FF : 0x76E0B3);
        auto *body = label(bubble, message.text, &lv_font_watch_zh_14, 0xFFFFFF);
        lv_obj_set_width(body, LV_PCT(100));
        lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    }
    lv_obj_update_layout(_messages_page);
    lv_obj_scroll_to_y(_messages_page, LV_COORD_MAX, LV_ANIM_OFF);
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, AiChatApp, APP_NAME, []()
{
    return std::shared_ptr<AiChatApp>(AiChatApp::requestInstance(), [](AiChatApp *) {});
});

} // namespace esp_brookesia::apps
