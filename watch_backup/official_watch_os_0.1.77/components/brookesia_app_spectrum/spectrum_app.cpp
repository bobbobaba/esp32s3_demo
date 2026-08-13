#include "spectrum_app.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "esp_brookesia.hpp"
#include "esp_dsp.h"
#include "esp_lib_utils.h"
#include "esp_log.h"
#include "lvgl.h"
#include "watch_app_icons.hpp"
#include "watch_audio.hpp"

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "SpectrumApp"

namespace esp_brookesia::apps {
namespace {
constexpr const char *APP_NAME = "Mic Spectrum";
constexpr int SAFE_TOP = 24;
constexpr int SAFE_SIDE = 22;
constexpr int N_SAMPLES = 1024;
constexpr int SAMPLE_RATE = 16000;
constexpr int CHANNELS = 2;
constexpr int BAR_COUNT = 40;

__attribute__((aligned(16))) int16_t s_raw_data[N_SAMPLES * CHANNELS] = {};
__attribute__((aligned(16))) float s_audio_buffer[N_SAMPLES] = {};
__attribute__((aligned(16))) float s_window[N_SAMPLES] = {};
__attribute__((aligned(16))) float s_fft_buffer[N_SAMPLES * 2] = {};
float s_display_spectrum[BAR_COUNT] = {};
float s_peak[BAR_COUNT] = {};
portMUX_TYPE s_spectrum_mux = portMUX_INITIALIZER_UNLOCKED;

lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
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

uint32_t bar_color_for_index(int index)
{
    if (index < BAR_COUNT / 3) {
        return 0x33D6FF;
    }
    if (index < (BAR_COUNT * 2) / 3) {
        return 0x7CFF6B;
    }
    return 0xFFB020;
}
} // namespace

SpectrumApp *SpectrumApp::_instance = nullptr;

SpectrumApp *SpectrumApp::requestInstance()
{
    if (_instance == nullptr) {
        _instance = new SpectrumApp();
    }
    return _instance;
}

SpectrumApp::SpectrumApp():
    systems::phone::App(APP_NAME, watch_app_icon_spectrum_48(), true, true, true)
{
}

bool SpectrumApp::run(void)
{
    _mic_unavailable = false;
    _mic_error = 0;
    lv_obj_t *screen = lv_scr_act();
    ESP_UTILS_CHECK_NULL_RETURN(screen, false, "Invalid active screen");
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x05070B), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t *root = lv_obj_create(screen);
    ESP_UTILS_CHECK_NULL_RETURN(root, false, "Create root failed");
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root, lv_color_hex(0x05070B), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(root, SAFE_SIDE, 0);
    lv_obj_set_style_pad_right(root, SAFE_SIDE, 0);
    lv_obj_set_style_pad_top(root, SAFE_TOP, 0);
    lv_obj_set_style_pad_bottom(root, 24, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(root, 14, 0);

    make_label(root, "Mic Spectrum", &lv_font_montserrat_30, 0xFFFFFF);
    _status_label = make_label(root, "Starting microphone...", &lv_font_montserrat_16, 0xAEB7C6);
    lv_obj_set_width(_status_label, LV_PCT(100));
    lv_label_set_long_mode(_status_label, LV_LABEL_LONG_WRAP);

    lv_obj_t *panel = lv_obj_create(root);
    ESP_UTILS_CHECK_NULL_RETURN(panel, false, "Create spectrum panel failed");
    lv_obj_remove_style_all(panel);
    lv_obj_set_width(panel, LV_PCT(100));
    lv_obj_set_height(panel, 278);
    lv_obj_set_style_radius(panel, 28, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x10131A), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x253044), 0);
    lv_obj_set_style_pad_left(panel, 14, 0);
    lv_obj_set_style_pad_right(panel, 14, 0);
    lv_obj_set_style_pad_top(panel, 18, 0);
    lv_obj_set_style_pad_bottom(panel, 18, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);

    for (int i = 0; i < BAR_COUNT; ++i) {
        lv_obj_t *bar = lv_obj_create(panel);
        lv_obj_remove_style_all(bar);
        lv_obj_set_width(bar, 5);
        lv_obj_set_height(bar, 8);
        lv_obj_set_style_radius(bar, 4, 0);
        lv_obj_set_style_bg_color(bar, lv_color_hex(bar_color_for_index(i)), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        _bars[i] = bar;
    }

    lv_obj_t *back = lv_button_create(root);
    lv_obj_set_width(back, LV_PCT(100));
    lv_obj_set_height(back, 50);
    style_button(back, 0x232833);
    lv_obj_add_event_cb(back, onBackClicked, LV_EVENT_CLICKED, this);
    lv_obj_center(make_label(back, "Back", &lv_font_montserrat_18, 0xFFFFFF));

    if (!startAudioTask()) {
        setStatus("Microphone unavailable. Check ES7210/I2S init logs.", 0xFFB020);
    }
    _timer = lv_timer_create(onTimer, 33, this);
    return true;
}

bool SpectrumApp::back(void)
{
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

bool SpectrumApp::close(void)
{
    if (_timer != nullptr) {
        lv_timer_delete(_timer);
        _timer = nullptr;
    }
    requestStopAudioTask();
    _status_label = nullptr;
    std::fill(std::begin(_bars), std::end(_bars), nullptr);
    return true;
}

void SpectrumApp::onBackClicked(lv_event_t *event)
{
    auto *app = static_cast<SpectrumApp *>(lv_event_get_user_data(event));
    if (app != nullptr) {
        app->back();
    }
}

void SpectrumApp::onTimer(lv_timer_t *timer)
{
    auto *app = static_cast<SpectrumApp *>(lv_timer_get_user_data(timer));
    if (app != nullptr) {
        app->refreshBars();
    }
}

bool SpectrumApp::startAudioTask()
{
    _stop_requested = false;
    if (_task != nullptr) {
        setStatus("Microphone analyzer running");
        return true;
    }

    esp_err_t ret = dsps_fft2r_init_fc32(nullptr, CONFIG_DSP_MAX_FFT_SIZE);
    if (ret != ESP_OK) {
        ESP_LOGE(ESP_UTILS_LOG_TAG, "FFT init failed: %s", esp_err_to_name(ret));
        return false;
    }
    dsps_wind_hann_f32(s_window, N_SAMPLES);

    BaseType_t ok = xTaskCreate(audioTask, "mic_spectrum", 7 * 1024, this, 5, &_task);
    if (ok != pdPASS) {
        _task = nullptr;
        ESP_LOGE(ESP_UTILS_LOG_TAG, "Create audio task failed");
        return false;
    }
    setStatus("Listening through ES7210 microphone");
    return true;
}

void SpectrumApp::requestStopAudioTask()
{
    _stop_requested = true;
}

void SpectrumApp::audioTask(void *arg)
{
    auto *app = static_cast<SpectrumApp *>(arg);
    if (app == nullptr) {
        vTaskDelete(nullptr);
        return;
    }

    esp_err_t ret = watch::audio_mic_start(SAMPLE_RATE, CHANNELS);
    if (ret != ESP_OK) {
        ESP_LOGE(ESP_UTILS_LOG_TAG, "microphone start failed: %s", esp_err_to_name(ret));
        app->_mic_error = static_cast<int>(ret);
        app->_mic_unavailable = true;
        app->_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    while (!app->_stop_requested) {
        size_t bytes_read = 0;
        ret = watch::audio_mic_read(s_raw_data, sizeof(s_raw_data), &bytes_read);
        if ((ret != ESP_OK) || (bytes_read != sizeof(s_raw_data))) {
            ESP_LOGW(ESP_UTILS_LOG_TAG, "microphone read failed: %s bytes=%u", esp_err_to_name(ret), static_cast<unsigned>(bytes_read));
            app->_mic_error = static_cast<int>(ret);
            vTaskDelay(pdMS_TO_TICKS(80));
            continue;
        }

        for (int i = 0; i < N_SAMPLES; ++i) {
            const int32_t left = s_raw_data[i * CHANNELS];
            const int32_t right = s_raw_data[i * CHANNELS + 1];
            s_audio_buffer[i] = static_cast<float>(left + right) / (2.0f * 32768.0f);
        }

        dsps_mul_f32(s_audio_buffer, s_window, s_audio_buffer, N_SAMPLES, 1, 1, 1);
        for (int i = 0; i < N_SAMPLES; ++i) {
            s_fft_buffer[2 * i] = s_audio_buffer[i];
            s_fft_buffer[2 * i + 1] = 0.0f;
        }

        dsps_fft2r_fc32(s_fft_buffer, N_SAMPLES);
        dsps_bit_rev_fc32(s_fft_buffer, N_SAMPLES);

        float next_spectrum[BAR_COUNT] = {};
        for (int i = 0; i < BAR_COUNT; ++i) {
            const int fft_idx = 2 + i * ((N_SAMPLES / 2) - 2) / BAR_COUNT;
            const float real = s_fft_buffer[2 * fft_idx];
            const float imag = s_fft_buffer[2 * fft_idx + 1];
            const float magnitude = std::sqrt(real * real + imag * imag);
            const float db = 20.0f * std::log10((magnitude / (N_SAMPLES / 2)) + 1e-9f);
            next_spectrum[i] = std::max(-90.0f, std::min(0.0f, db));
        }

        portENTER_CRITICAL(&s_spectrum_mux);
        std::memcpy(s_display_spectrum, next_spectrum, sizeof(s_display_spectrum));
        portEXIT_CRITICAL(&s_spectrum_mux);
    }

    watch::audio_mic_stop();
    app->_task = nullptr;
    vTaskDelete(nullptr);
}

void SpectrumApp::refreshBars()
{
    if (_mic_unavailable) {
        char text[112] = {};
        std::snprintf(
            text,
            sizeof(text),
            "Microphone unavailable. ES7210/I2S init failed: %s",
            esp_err_to_name(static_cast<esp_err_t>(_mic_error))
        );
        setStatus(text, 0xFFB020);
        return;
    }

    float bars[BAR_COUNT] = {};
    portENTER_CRITICAL(&s_spectrum_mux);
    std::memcpy(bars, s_display_spectrum, sizeof(bars));
    portEXIT_CRITICAL(&s_spectrum_mux);

    for (int i = 0; i < BAR_COUNT; ++i) {
        if (_bars[i] == nullptr) {
            continue;
        }
        const float normalized = std::sqrt(std::max(0.0f, std::min(1.0f, (bars[i] + 90.0f) / 90.0f)));
        int height = static_cast<int>(8.0f + normalized * 226.0f);
        if (s_peak[i] < height) {
            s_peak[i] = height;
        } else {
            s_peak[i] = std::max(8.0f, s_peak[i] - 4.0f);
        }
        lv_obj_set_height(_bars[i], static_cast<int>(s_peak[i]));
    }
}

void SpectrumApp::setStatus(const char *text, uint32_t color)
{
    if (_status_label != nullptr) {
        lv_label_set_text(_status_label, text);
        lv_obj_set_style_text_color(_status_label, lv_color_hex(color), 0);
    }
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, SpectrumApp, APP_NAME, []()
{
    return std::shared_ptr<SpectrumApp>(SpectrumApp::requestInstance(), [](SpectrumApp *) {});
})

} // namespace esp_brookesia::apps
