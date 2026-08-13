/*
 * SPDX-FileCopyrightText: 2023-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "bsp/esp-bsp.h"
#include "esp_brookesia.hpp"
#include "boost/thread.hpp"
#include "watch_connectivity.hpp"
#include "watch_alarm.hpp"
#include "watch_power.hpp"
#include "watch_time.hpp"
#include "watch_display.hpp"
#include "watch_storage.hpp"
#include "watch_home_app.hpp"
#include "ota_app.hpp"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "sdkconfig.h"
#if CONFIG_PM_ENABLE
#include "esp_pm.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>
#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "Main"
#include "esp_lib_utils.h"
#include "./dark/stylesheet.hpp"

using namespace esp_brookesia;
using namespace esp_brookesia::gui;
using namespace esp_brookesia::systems::phone;

#define LVGL_PORT_INIT_CONFIG() \
    {                               \
        .task_priority = 4,       \
        .task_stack = 10 * 1024,       \
        .task_affinity = -1,      \
        .task_max_sleep_ms = 500, \
        .timer_period_ms = 5,     \
    }

constexpr bool EXAMPLE_SHOW_MEM_INFO = false;
// 0.1.62+ introduced several early boot/background services. On the
// ESP32-S3-Touch-AMOLED-2.06 these made some builds reach a white-screen state
// before the watch home could become visible. Keep the apps compiled, but start
// from the verified official-demo boot path first; re-enable services one by one
// after the display is stable on hardware.
constexpr bool WATCH_SAFE_STARTUP = true;
constexpr bool WATCH_ENABLE_POWER_MANAGEMENT = false;
constexpr bool WATCH_ENABLE_STORAGE_EARLY_MOUNT = true;
constexpr bool WATCH_ENABLE_ALARM_BACKGROUND = true;
constexpr bool WATCH_ENABLE_DISPLAY_SETTINGS_INIT = true;
constexpr bool WATCH_ENABLE_DISPLAY_TICK = true;
constexpr bool WATCH_ENABLE_OTA_STARTUP_REPORT = false;
constexpr const char *WATCH_HOME_APP_NAME = "WatchHome";
constexpr const char *ALARM_APP_NAME = "Alarm";

namespace {

struct HomeButtonContext {
    Phone *phone = nullptr;
    int watch_home_app_id = -1;
    bool initialized = false;
    bool pending_watch_home_start = false;
    uint32_t pending_watch_home_tick = 0;
};

HomeButtonContext s_home_button_context;
struct AlarmContext {
    Phone *phone = nullptr;
    int alarm_app_id = -1;
};

AlarmContext s_alarm_context;
volatile bool s_power_button_pending = false;
bool s_power_button_task_started = false;
constexpr TickType_t POWER_BUTTON_POLL_INTERVAL = pdMS_TO_TICKS(150);

void configure_power_management()
{
#if CONFIG_PM_ENABLE
    const esp_pm_config_t pm_config = {
        .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = 40,
        .light_sleep_enable = false,
    };
    esp_err_t err = esp_pm_configure(&pm_config);
    if (err == ESP_OK) {
        ESP_UTILS_LOGI(
            "Power management enabled: max=%dMHz min=%dMHz light_sleep=off",
            pm_config.max_freq_mhz,
            pm_config.min_freq_mhz
        );
    } else {
        ESP_UTILS_LOGW("Power management configure failed: %s", esp_err_to_name(err));
    }
#else
    ESP_UTILS_LOGI("Power management disabled by sdkconfig");
#endif
}

bool start_watch_home(Phone *phone, int watch_home_app_id)
{
    if ((phone == nullptr) || (watch_home_app_id < 0)) {
        return false;
    }
    systems::base::Context::AppEventData app_event_data = {
        .id = watch_home_app_id,
        .type = systems::base::Context::AppEventType::START,
        .data = nullptr,
    };
    watch::display_wake();
    ESP_UTILS_LOGI("PWR short click: start watch home app (id: %d)", watch_home_app_id);
    const bool ok = phone->sendAppEvent(&app_event_data);
    if (ok) {
        esp_brookesia::apps::watch_home_set_active(true);
    }
    return ok;
}

bool start_alarm_app(Phone *phone, int alarm_app_id)
{
    if ((phone == nullptr) || (alarm_app_id < 0)) {
        return false;
    }
    systems::base::Context::AppEventData app_event_data = {
        .id = alarm_app_id,
        .type = systems::base::Context::AppEventType::START,
        .data = nullptr,
    };
    watch::display_wake();
    ESP_UTILS_LOGI("Alarm fired: start alarm app (id: %d)", alarm_app_id);
    return phone->sendAppEvent(&app_event_data);
}

bool send_system_home(Phone *phone)
{
    if (phone == nullptr) {
        return false;
    }
    watch::display_wake();
    ESP_UTILS_LOGI("PWR short click: send Brookesia HOME navigation");
    return phone->sendNavigateEvent(systems::base::Manager::NavigateType::HOME);
}

void poll_power_button(HomeButtonContext *context)
{
    if ((context == nullptr) || (context->phone == nullptr) || (context->watch_home_app_id < 0)) {
        return;
    }

    if (!context->initialized) {
        context->initialized = true;
        ESP_UTILS_LOGI("PWR home shortcut ready: background AXP2101 IRQ polling");
    }

    if (context->pending_watch_home_start &&
        (lv_tick_elaps(context->pending_watch_home_tick) >= 100)) {
        context->pending_watch_home_start = false;
        if (!start_watch_home(context->phone, context->watch_home_app_id)) {
            ESP_UTILS_LOGW("PWR short click: delayed start watch home app failed");
        }
    }

    if (s_power_button_pending) {
        s_power_button_pending = false;
        watch::display_wake();
        watch::display_notify_activity();
        if (esp_brookesia::apps::watch_home_is_active()) {
            ESP_UTILS_LOGI("PWR short click on watch home: open app launcher");
            esp_brookesia::apps::watch_home_set_active(false);
            if (!send_system_home(context->phone)) {
                ESP_UTILS_LOGW("PWR short click: failed to open app launcher from watch home");
            }
            return;
        }
        if (!send_system_home(context->phone)) {
            ESP_UTILS_LOGW("PWR short click: failed to send Brookesia HOME navigation");
        }
        context->pending_watch_home_start = true;
        context->pending_watch_home_tick = lv_tick_get();
    }
}

void power_button_task(void *)
{
    while (true) {
        if (watch::power_poll_pkey_short_press()) {
            s_power_button_pending = true;
        }
        vTaskDelay(POWER_BUTTON_POLL_INTERVAL);
    }
}

void poll_alarm(AlarmContext *context)
{
    if ((context == nullptr) || (context->phone == nullptr) || (context->alarm_app_id < 0)) {
        watch::alarm_poll();
        return;
    }
    if (watch::alarm_poll() && !start_alarm_app(context->phone, context->alarm_app_id)) {
        ESP_UTILS_LOGW("Alarm fired: failed to start alarm app");
    }
}

} // namespace

extern "C" void app_main(void)
{
    ESP_UTILS_LOGI("Display ESP-Brookesia phone demo");
    if constexpr (WATCH_ENABLE_POWER_MANAGEMENT) {
        configure_power_management();
    } else {
        ESP_UTILS_LOGW("Safe startup: defer power management");
    }

    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = LVGL_PORT_INIT_CONFIG(),
    };
    ESP_UTILS_CHECK_NULL_EXIT(bsp_display_start_with_config(&cfg), "Start display failed");
    ESP_UTILS_CHECK_ERROR_EXIT(bsp_display_backlight_on(), "Turn on display backlight failed");

    esp_err_t power_err = watch::power_init();
    if (power_err != ESP_OK) {
        ESP_UTILS_LOGW("Power init failed: %s", esp_err_to_name(power_err));
    }

    if constexpr (WATCH_ENABLE_STORAGE_EARLY_MOUNT) {
        esp_err_t littlefs_err = watch::storage_littlefs_mount();
        if (littlefs_err != ESP_OK) {
            ESP_UTILS_LOGW("LittleFS early mount failed: %s", esp_err_to_name(littlefs_err));
        }
        esp_err_t sd_err = watch::storage_sd_mount();
        if (sd_err != ESP_OK) {
            ESP_UTILS_LOGW("SD early mount skipped/failed: %s", esp_err_to_name(sd_err));
        }
    }

    esp_err_t connectivity_err = watch::connectivity_init();
    if (connectivity_err != ESP_OK) {
        ESP_UTILS_LOGW("Connectivity init failed: %s", esp_err_to_name(connectivity_err));
    }
    esp_err_t time_err = watch::time_service_init();
    if (time_err != ESP_OK) {
        ESP_UTILS_LOGW("Time service init failed: %s", esp_err_to_name(time_err));
    }
    if constexpr (WATCH_ENABLE_ALARM_BACKGROUND) {
        esp_err_t alarm_err = watch::alarm_init();
        if (alarm_err != ESP_OK) {
            ESP_UTILS_LOGW("Alarm init failed: %s", esp_err_to_name(alarm_err));
        }
    }
    if constexpr (WATCH_ENABLE_DISPLAY_SETTINGS_INIT) {
        esp_err_t display_err = watch::display_settings_init();
        if (display_err != ESP_OK) {
            ESP_UTILS_LOGW("Display settings init failed: %s", esp_err_to_name(display_err));
        }
    }
    if constexpr (WATCH_ENABLE_OTA_STARTUP_REPORT) {
        esp_brookesia::apps::ota_report_startup_async();
    }
    /* Configure GUI lock */
    LvLock::registerCallbacks([](int timeout_ms) {
        if (timeout_ms < 0) {
            timeout_ms = 0;
        } else if (timeout_ms == 0) {
            timeout_ms = 1;
        }
        ESP_UTILS_CHECK_FALSE_RETURN(bsp_display_lock(timeout_ms), false, "Lock failed");

        return true;
    }, []() {
        bsp_display_unlock();

        return true;
    });

    /* Create a phone object */
    Phone *phone = new (std::nothrow) Phone();
    ESP_UTILS_CHECK_NULL_EXIT(phone, "Create phone failed");

    /* Try using a stylesheet that corresponds to the resolution */
    if ((BSP_LCD_H_RES == 410) && (BSP_LCD_V_RES == 502)) {
        Stylesheet *stylesheet = new (std::nothrow) Stylesheet(STYLESHEET_410_502_DARK);
        ESP_UTILS_CHECK_NULL_EXIT(stylesheet, "Create stylesheet failed");

        ESP_UTILS_LOGI("Using stylesheet (%s)", stylesheet->core.name);
        ESP_UTILS_CHECK_FALSE_EXIT(phone->addStylesheet(stylesheet), "Add stylesheet failed");
        ESP_UTILS_CHECK_FALSE_EXIT(phone->activateStylesheet(stylesheet), "Activate stylesheet failed");
        delete stylesheet;
    }

    {
        // When operating on non-GUI tasks, should acquire a lock before operating on LVGL
        LvLockGuard gui_guard;

        /* Begin the phone */
        ESP_UTILS_CHECK_FALSE_EXIT(phone->begin(), "Begin failed");
        // assert(phone->getDisplay().showContainerBorder() && "Show container border failed");

        /* Init and install apps from registry */
        std::vector<systems::base::Manager::RegistryAppInfo> inited_apps;
        ESP_UTILS_CHECK_FALSE_EXIT(phone->initAppFromRegistry(inited_apps), "Init app registry failed");
        ESP_UTILS_CHECK_FALSE_EXIT(phone->installAppFromRegistry(inited_apps), "Install app registry failed");

        int watch_home_app_id = -1;
        int alarm_app_id = -1;

        /* Open the watch UI by default while keeping the official app launcher available via navigation. */
        for (const auto &[name, app] : inited_apps) {
            if ((app != nullptr) && (std::strcmp(name.c_str(), WATCH_HOME_APP_NAME) == 0)) {
                watch_home_app_id = app->getId();
            } else if ((app != nullptr) && (std::strcmp(name.c_str(), ALARM_APP_NAME) == 0)) {
                alarm_app_id = app->getId();
            }
        }
        if (watch_home_app_id >= 0) {
            systems::base::Context::AppEventData app_event_data = {
                .id = watch_home_app_id,
                .type = systems::base::Context::AppEventType::START,
                .data = nullptr,
            };
            ESP_UTILS_LOGI("Start default watch home app: %s (id: %d)", WATCH_HOME_APP_NAME, watch_home_app_id);
            ESP_UTILS_CHECK_FALSE_EXIT(phone->sendAppEvent(&app_event_data), "Start default watch home app failed");
        }
        if (watch_home_app_id < 0) {
            ESP_UTILS_LOGW("Watch home app not found; PWR home shortcut disabled");
        } else {
            s_home_button_context.phone = phone;
            s_home_button_context.watch_home_app_id = watch_home_app_id;
            if (!s_power_button_task_started) {
                s_power_button_task_started = xTaskCreateWithCaps(
                    power_button_task,
                    "pwr_home_poll",
                    3072,
                    nullptr,
                    3,
                    nullptr,
                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
                ) == pdPASS;
                if (!s_power_button_task_started) {
                    ESP_UTILS_LOGW("Failed to start PWR home background polling task");
                }
            }
            lv_timer_create([](lv_timer_t *t) {
                poll_power_button(static_cast<HomeButtonContext *>(lv_timer_get_user_data(t)));
            }, 50, &s_home_button_context);
        }
        if constexpr (WATCH_ENABLE_ALARM_BACKGROUND) {
            if (alarm_app_id < 0) {
                ESP_UTILS_LOGW("Alarm app not found; alarm popup disabled");
            } else {
                s_alarm_context.phone = phone;
                s_alarm_context.alarm_app_id = alarm_app_id;
                lv_timer_create([](lv_timer_t *t) {
                    poll_alarm(static_cast<AlarmContext *>(lv_timer_get_user_data(t)));
                }, 1000, &s_alarm_context);
            }
        }

        /* Create a timer to update the clock */
        lv_timer_create([](lv_timer_t *t) {
            time_t now;
            struct tm timeinfo;
            Phone *phone = (Phone *)t->user_data;

            ESP_UTILS_CHECK_NULL_EXIT(phone, "Invalid phone");

            if constexpr (WATCH_ENABLE_DISPLAY_TICK) {
                if (!watch::display_is_on()) {
                    static uint32_t last_background_tick = 0;
                    if ((last_background_tick == 0) || (lv_tick_elaps(last_background_tick) >= 60000)) {
                        last_background_tick = lv_tick_get();
                        watch::time_poll();
                    }
                    return;
                }
            }

            watch::time_poll();

            time(&now);
            localtime_r(&now, &timeinfo);

            ESP_UTILS_CHECK_FALSE_EXIT(
                phone->getDisplay().getStatusBar()->setClock(timeinfo.tm_hour, timeinfo.tm_min),
                "Refresh status bar failed"
            );
        }, WATCH_ENABLE_DISPLAY_TICK ? 5000 : 1000, phone);

        lv_timer_create([](lv_timer_t *t) {
            Phone *phone = (Phone *)t->user_data;
            ESP_UTILS_CHECK_NULL_EXIT(phone, "Invalid phone");

            if constexpr (WATCH_ENABLE_DISPLAY_TICK) {
                if (!watch::display_is_on()) {
                    return;
                }
            }

            auto *status_bar = phone->getDisplay().getStatusBar();
            ESP_UTILS_CHECK_NULL_EXIT(status_bar, "Invalid status bar");

            if (watch::wifi_is_connected()) {
                int rssi = watch::wifi_rssi_dbm();
                auto wifi_state =
                    (rssi >= -50) ? esp_brookesia::systems::phone::StatusBar::WifiState::SIGNAL_3 :
                    (rssi >= -70) ? esp_brookesia::systems::phone::StatusBar::WifiState::SIGNAL_2 :
                                    esp_brookesia::systems::phone::StatusBar::WifiState::SIGNAL_1;
                ESP_UTILS_CHECK_FALSE_EXIT(status_bar->setWifiIconState(wifi_state), "Refresh wifi status failed");
            } else {
                ESP_UTILS_CHECK_FALSE_EXIT(
                    status_bar->setWifiIconState(esp_brookesia::systems::phone::StatusBar::WifiState::DISCONNECTED),
                    "Refresh wifi status failed"
                );
            }
        }, WATCH_ENABLE_DISPLAY_TICK ? 5000 : 1000, phone);

        lv_timer_create([](lv_timer_t *t) {
            Phone *phone = (Phone *)t->user_data;
            ESP_UTILS_CHECK_NULL_EXIT(phone, "Invalid phone");

            if constexpr (WATCH_ENABLE_DISPLAY_TICK) {
                if (!watch::display_is_on()) {
                    return;
                }
            }

            auto *status_bar = phone->getDisplay().getStatusBar();
            ESP_UTILS_CHECK_NULL_EXIT(status_bar, "Invalid status bar");

            int percent = watch::power_battery_percent();
            if (percent >= 0) {
                ESP_UTILS_CHECK_FALSE_EXIT(
                    status_bar->setBatteryPercent(watch::power_is_charging(), percent),
                    "Refresh battery status failed"
                );
            }
        }, 5000, phone);

        if constexpr (WATCH_ENABLE_DISPLAY_TICK) {
            lv_timer_create([](lv_timer_t *) {
                watch::display_tick(bsp_display_get_input_dev());
            }, 50, nullptr);
        }
    }

    if constexpr (EXAMPLE_SHOW_MEM_INFO) {
        esp_utils::thread_config_guard thread_config({
            .name = "mem_info",
            .stack_size = 4096,
        });
        boost::thread([ = ]() {
            char buffer[128];    /* Make sure buffer is enough for `sprintf` */
            size_t internal_free = 0;
            size_t internal_total = 0;
            size_t external_free = 0;
            size_t external_total = 0;

            while (1) {
                internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
                internal_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
                external_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
                external_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
                sprintf(buffer,
                        "\t           Biggest /     Free /    Total\n"
                        "\t  SRAM : [%8d / %8d / %8d]\n"
                        "\t PSRAM : [%8d / %8d / %8d]",
                        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), internal_free, internal_total,
                        heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM), external_free, external_total);
                ESP_UTILS_LOGI("\n%s", buffer);

                {
                    LvLockGuard gui_guard;
                    ESP_UTILS_CHECK_FALSE_EXIT(
                        phone->getDisplay().getRecentsScreen()->setMemoryLabel(
                            internal_free / 1024, internal_total / 1024, external_free / 1024, external_total / 1024
                        ), "Set memory label failed"
                    );
                }

                boost::this_thread::sleep_for(boost::chrono::seconds(5));
            }
        }).detach();
    }
}
