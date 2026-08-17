/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "systems/phone/widgets/app_launcher/esp_brookesia_app_launcher.hpp"

namespace esp_brookesia::systems::phone {

constexpr AppLauncherIcon::Data STYLESHEET_410_502_DARK_APP_LAUNCHER_ICON_DATA = {
    .main = {
        // A four-column grid leaves a dependable touch target without scaling
        // the watch's native 48px icon artwork into a soft-looking bitmap.
        .size = gui::StyleSize::SQUARE(88),
        .layout_row_pad = 4,
    },
    .image = {
        .default_size = gui::StyleSize::SQUARE(52),
        .press_size = gui::StyleSize::SQUARE(48),
    },
    .label = {
        .text_font = gui::StyleFont::SIZE(12),
        .text_color = gui::StyleColor::COLOR(0xD6DCE7),
    },
};

constexpr AppLauncherData STYLESHEET_410_502_DARK_APP_LAUNCHER_DATA = {
    .main = {
        .y_start = 0,
        .size = gui::StyleSize::RECT_PERCENT(100, 100),
    },
    .table = {
        .default_num = 1,
        .size = gui::StyleSize::RECT_PERCENT(100, 88),
    },
    .indicator = {
        .main_size = gui::StyleSize::RECT_W_PERCENT(100, 12),
        .main_layout_column_pad = 8,
        .main_layout_bottom_offset = 14,
        .spot_inactive_size = gui::StyleSize::SQUARE(6),
        .spot_active_size = gui::StyleSize::RECT(24, 6),
        .spot_inactive_background_color = gui::StyleColor::COLOR(0x303947),
        .spot_active_background_color = gui::StyleColor::COLOR(0xAEB7C6),
    },
    .icon = STYLESHEET_410_502_DARK_APP_LAUNCHER_ICON_DATA,
    .flags = {
        .enable_table_scroll_anim = 0,
    },
};

} // namespace esp_brookesia::systems::phone
