/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

std::expected<void, std::string> SettingsApp::refresh_header(system::core::AppContext &context)
{
    std::vector<gui::BindingValueUpdate> updates;
    add_binding_update(
        updates,
        HEADER_BACK_PATH,
        "commonProps.hidden",
        current_page_ == PAGE_HOME ? "true" : "false"
    );
    add_binding_update(
        updates,
        HEADER_TITLE_PATH,
        "labelProps.text",
        localized_text(current_locale_, current_page_)
    );
    return context.gui().set_binding_values(updates);
}

std::expected<void, std::string> SettingsApp::refresh_theme_state(system::core::AppContext &context)
{
    const bool dark = current_theme_id_ == THEME_DARK;
    auto tokens = load_selectable_theme_tokens(context, current_theme_id_);
    if (!tokens) {
        return std::unexpected(tokens.error());
    }

    std::vector<gui::BindingValueUpdate> updates;
    auto light_label = localized_text(current_locale_, "light");
    if (pending_theme_id_ == THEME_LIGHT) {
        light_label += " (" + localized_text(current_locale_, "restart_required") + ")";
    }
    auto dark_label = localized_text(current_locale_, "dark");
    if (pending_theme_id_ == THEME_DARK) {
        dark_label += " (" + localized_text(current_locale_, "restart_required") + ")";
    }
    add_theme_mode_updates(
        updates, "/display/page/theme_modes/light_mode", !dark, *tokens, "#f3f3f3", std::move(light_label)
    );
    add_theme_mode_updates(
        updates, "/display/page/theme_modes/dark_mode", dark, *tokens, "#38393a", std::move(dark_label)
    );
    add_binding_update(
        updates,
        "/settings_home/page/main_list/display/value_box/value",
        "labelProps.text",
        localized_text(current_locale_, dark ? "dark" : "light")
    );
    return context.gui().set_binding_values(updates);
}

std::expected<void, std::string> SettingsApp::refresh_language_state(system::core::AppContext &context)
{
    std::vector<gui::BindingValueUpdate> updates;
    add_binding_update(
        updates,
        "/more/page/language_card/language/value_box/value",
        "labelProps.text",
        get_language_summary_name(current_locale_)
    );
    for (const auto &path : dynamic_language_paths_) {
        const auto instance_id = path.substr(std::string_view(LANGUAGE_LIST_PARENT).size() + 1);
        auto it = language_instance_to_locale_.find(instance_id);
        if (it == language_instance_to_locale_.end()) {
            continue;
        }
        add_binding_update(
            updates,
            path + "/title_box/title",
            "labelProps.text",
            get_language_display_name(current_locale_, it->second)
        );
        add_binding_update(
            updates,
            path + "/value_box/value",
            "labelProps.text",
            it->second == pending_language_locale_ ?
            localized_text(current_locale_, "restart_required") :
            it->second == current_locale_ ? localized_text(current_locale_, "current") : ""
        );
    }
    return context.gui().set_binding_values(updates);
}

std::expected<void, std::string> SettingsApp::refresh_diagnostic_state(system::core::AppContext &context)
{
    auto format_kb = [](size_t bytes) {
        return std::to_string(bytes / 1024) + " KiB";
    };

    std::string system_name = context.system_service().get_system_info().name;
    if (system_name.empty()) {
        system_name = localized_text(current_locale_, "text.unknown");
    }

    std::string firmware = context.system_service().get_system_info().version;
    if (firmware.empty()) {
        firmware = localized_text(current_locale_, "text.unknown");
    }

    std::string reset_reason = "unknown";
#if defined(ESP_PLATFORM)
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON: reset_reason = "poweron"; break;
    case ESP_RST_EXT: reset_reason = "external"; break;
    case ESP_RST_SW: reset_reason = "software"; break;
    case ESP_RST_PANIC: reset_reason = "panic"; break;
    case ESP_RST_INT_WDT: reset_reason = "interrupt_wdt"; break;
    case ESP_RST_TASK_WDT: reset_reason = "task_wdt"; break;
    case ESP_RST_WDT: reset_reason = "watchdog"; break;
    case ESP_RST_DEEPSLEEP: reset_reason = "deepsleep"; break;
    case ESP_RST_BROWNOUT: reset_reason = "brownout"; break;
    case ESP_RST_SDIO: reset_reason = "sdio"; break;
    default: break;
    }
#endif

    size_t heap_free = 0;
    size_t psram_free = 0;
    size_t psram_largest = 0;
#if defined(ESP_PLATFORM)
    heap_free = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
#endif

    std::string wifi_state = wifi_ui_state_.service_ready ? (wifi_ui_state_.enabled ? "ready" : "disabled") :
                             "service_unavailable";
    if (!wifi_ui_state_.general_state.empty()) {
        wifi_state += " / " + wifi_ui_state_.general_state;
    }

    std::string ota_state = "idle";
    if (current_page_ == PAGE_HOME) {
        ota_state = "available";
    }

    std::string battery_state = "unavailable";
    if (device_ui_state_.audio_service_ready || device_ui_state_.display_service_ready) {
        battery_state = std::to_string(device_ui_state_.brightness) + "% / " +
                        std::to_string(device_ui_state_.volume) + "%";
    }

    std::string current_app = localized_text(current_locale_, "text.unknown");
    if (auto active_app = context.system_service().get_active_app(); active_app.has_value()) {
        const auto app = active_app.value();
        current_app = !app.manifest.name.empty() ? app.manifest.name :
                      !app.manifest.id.empty() ? app.manifest.id : current_app;
        if (app.state != system::core::AppState::Running) {
            current_app += " (" + std::string(BROOKESIA_DESCRIBE_ENUM_TO_STR(app.state)) + ")";
        }
    }

    std::vector<gui::BindingValueUpdate> updates;
    add_binding_update(updates, DIAGNOSTIC_SYSTEM_NAME_PATH, "labelProps.text", system_name);
    add_binding_update(updates, DIAGNOSTIC_FIRMWARE_PATH, "labelProps.text", firmware);
    add_binding_update(updates, DIAGNOSTIC_RESET_REASON_PATH, "labelProps.text", reset_reason);
    add_binding_update(updates, DIAGNOSTIC_HEAP_PATH, "labelProps.text", format_kb(heap_free));
    add_binding_update(updates, DIAGNOSTIC_PSRAM_PATH, "labelProps.text", format_kb(psram_free));
    add_binding_update(updates, DIAGNOSTIC_WIFI_PATH, "labelProps.text", wifi_state);
    add_binding_update(updates, DIAGNOSTIC_OTA_PATH, "labelProps.text", ota_state);
    add_binding_update(updates, DIAGNOSTIC_BATTERY_PATH, "labelProps.text", battery_state);
    add_binding_update(updates, DIAGNOSTIC_POWER_PATH, "labelProps.text", "battery");
    add_binding_update(updates, DIAGNOSTIC_APP_PATH, "labelProps.text", current_app);
    add_binding_update(updates, DIAGNOSTIC_TASK_PATH, "labelProps.text", "runtime");
    return context.gui().set_binding_values(updates);
}
