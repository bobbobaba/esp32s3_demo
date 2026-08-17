#include "watch_app_icons.hpp"

#include <cstdint>
#include <cstring>

#include "esp_heap_caps.h"

namespace {

/*
 * Launcher icons are generated at boot instead of loading PNGs from LittleFS.
 * They stay available before storage mounts and use one shared visual system:
 * rounded tile, subtle accent halo, consistent glyph weight, and fixed padding.
 */
constexpr int W = 48;
constexpr int H = 48;
constexpr uint8_t OPAQUE = 0xff;

struct Color {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

constexpr Color WHITE = {246, 249, 255};
constexpr Color SOFT_WHITE = {219, 231, 248};

struct Icon {
    uint8_t *pixels = nullptr;
    lv_image_dsc_t dsc = {
        .header = {
            .magic = LV_IMAGE_HEADER_MAGIC,
            .cf = LV_COLOR_FORMAT_ARGB8888,
            .flags = 0,
            .w = W,
            .h = H,
            .stride = W * 4,
            .reserved_2 = 0,
        },
        .data_size = W * H * 4,
        .data = nullptr,
        .reserved = nullptr,
        .reserved_2 = nullptr,
    };
    bool ready = false;
};

Icon s_home;
Icon s_settings;
Icon s_files;
Icon s_gallery;
Icon s_ota;
Icon s_sensors;
Icon s_level;
Icon s_spectrum;
Icon s_quick;
Icon s_audio;
Icon s_music;
Icon s_alarm;
Icon s_calendar;
Icon s_anniversary;
Icon s_weather;
Icon s_timer;
Icon s_power;
Icon s_cloud;
Icon s_quota;
Icon s_ai_chat;
Icon s_codex;
Icon s_robots;
Icon s_game;
Icon s_counter;
Icon s_squareline;

uint8_t blend_channel(uint8_t from, uint8_t to, int amount)
{
    if (amount <= 0) {
        return from;
    }
    if (amount >= 100) {
        return to;
    }
    return static_cast<uint8_t>((static_cast<int>(from) * (100 - amount) + static_cast<int>(to) * amount) / 100);
}

Color blend(Color from, Color to, int amount)
{
    return {
        .r = blend_channel(from.r, to.r, amount),
        .g = blend_channel(from.g, to.g, amount),
        .b = blend_channel(from.b, to.b, amount),
    };
}

void put(Icon &icon, int x, int y, Color color = WHITE, uint8_t alpha = OPAQUE)
{
    if ((icon.pixels == nullptr) || (x < 0) || (x >= W) || (y < 0) || (y >= H)) {
        return;
    }
    uint8_t *p = &icon.pixels[(y * W + x) * 4];
    p[0] = alpha;
    p[1] = color.r;
    p[2] = color.g;
    p[3] = color.b;
}

void hline(Icon &icon, int x0, int x1, int y, int t = 1, Color color = WHITE)
{
    if (x0 > x1) {
        int tmp = x0;
        x0 = x1;
        x1 = tmp;
    }
    for (int yy = y; yy < y + t; ++yy) {
        for (int x = x0; x <= x1; ++x) {
            put(icon, x, yy, color);
        }
    }
}

void vline(Icon &icon, int x, int y0, int y1, int t = 1, Color color = WHITE)
{
    if (y0 > y1) {
        int tmp = y0;
        y0 = y1;
        y1 = tmp;
    }
    for (int xx = x; xx < x + t; ++xx) {
        for (int y = y0; y <= y1; ++y) {
            put(icon, xx, y, color);
        }
    }
}

void rect(Icon &icon, int x, int y, int w, int h, int t = 2, Color color = WHITE)
{
    hline(icon, x, x + w - 1, y, t, color);
    hline(icon, x, x + w - 1, y + h - t, t, color);
    vline(icon, x, y, y + h - 1, t, color);
    vline(icon, x + w - t, y, y + h - 1, t, color);
}

void fill_rect(Icon &icon, int x, int y, int w, int h, Color color = WHITE)
{
    for (int yy = y; yy < y + h; ++yy) {
        hline(icon, x, x + w - 1, yy, 1, color);
    }
}

void line(Icon &icon, int x0, int y0, int x1, int y1, int t = 1, Color color = WHITE)
{
    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = (y1 > y0) ? (y0 - y1) : (y1 - y0);
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;
    while (true) {
        for (int oy = 0; oy < t; ++oy) {
            for (int ox = 0; ox < t; ++ox) {
                put(icon, x0 + ox, y0 + oy, color);
            }
        }
        if ((x0 == x1) && (y0 == y1)) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void circle(Icon &icon, int cx, int cy, int r, int t = 2, Color color = WHITE)
{
    for (int y = cy - r - t; y <= cy + r + t; ++y) {
        for (int x = cx - r - t; x <= cx + r + t; ++x) {
            int dx = x - cx;
            int dy = y - cy;
            int d2 = dx * dx + dy * dy;
            if ((d2 >= (r - t) * (r - t)) && (d2 <= (r + t) * (r + t))) {
                put(icon, x, y, color);
            }
        }
    }
}

void fill_circle(Icon &icon, int cx, int cy, int r, Color color = WHITE)
{
    for (int y = cy - r; y <= cy + r; ++y) {
        for (int x = cx - r; x <= cx + r; ++x) {
            int dx = x - cx;
            int dy = y - cy;
            if ((dx * dx + dy * dy) <= r * r) {
                put(icon, x, y, color);
            }
        }
    }
}

bool inside_round_rect(int x, int y, int left, int top, int right, int bottom, int radius)
{
    if ((x < left) || (x > right) || (y < top) || (y > bottom)) {
        return false;
    }

    int cx = x;
    int cy = y;
    if (x < left + radius) {
        cx = left + radius;
    } else if (x > right - radius) {
        cx = right - radius;
    }
    if (y < top + radius) {
        cy = top + radius;
    } else if (y > bottom - radius) {
        cy = bottom - radius;
    }

    int dx = x - cx;
    int dy = y - cy;
    return (dx * dx + dy * dy) <= radius * radius;
}

void draw_tile(Icon &icon, Color base, Color accent)
{
    constexpr int left = 2;
    constexpr int top = 2;
    constexpr int right = W - 3;
    constexpr int bottom = H - 3;
    constexpr int radius = 12;
    constexpr int halo_x = 36;
    constexpr int halo_y = 11;
    constexpr int halo_r = 20;

    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            if (!inside_round_rect(x, y, left, top, right, bottom, radius)) {
                continue;
            }

            int vertical = 84 + ((y - top) * 22) / (bottom - top);
            Color color = {
                .r = static_cast<uint8_t>((static_cast<int>(base.r) * vertical) / 100),
                .g = static_cast<uint8_t>((static_cast<int>(base.g) * vertical) / 100),
                .b = static_cast<uint8_t>((static_cast<int>(base.b) * vertical) / 100),
            };

            int dx = x - halo_x;
            int dy = y - halo_y;
            int d2 = dx * dx + dy * dy;
            if (d2 < halo_r * halo_r) {
                color = blend(color, accent, ((halo_r * halo_r - d2) * 34) / (halo_r * halo_r));
            }

            bool edge = !inside_round_rect(x, y, left + 1, top + 1, right - 1, bottom - 1, radius - 1);
            if (edge) {
                color = blend(color, WHITE, 18);
            }
            put(icon, x, y, color);
        }
    }
}

void clear(Icon &icon)
{
    if (icon.pixels != nullptr) {
        std::memset(icon.pixels, 0, W * H * 4);
    }
}

void draw_home(Icon &i)
{
    line(i, 10, 25, 24, 12, 3);
    line(i, 24, 12, 38, 25, 3);
    rect(i, 15, 24, 18, 14, 2);
    vline(i, 24, 29, 38, 2);
}

void draw_settings(Icon &i)
{
    circle(i, 24, 24, 11, 3);
    fill_circle(i, 24, 24, 3);
    hline(i, 22, 26, 8, 3);
    hline(i, 22, 26, 37, 3);
    vline(i, 8, 22, 26, 3);
    vline(i, 37, 22, 26, 3);
    line(i, 13, 13, 17, 17, 3);
    line(i, 31, 31, 35, 35, 3);
    line(i, 13, 35, 17, 31, 3);
    line(i, 31, 17, 35, 13, 3);
}

void draw_files(Icon &i)
{
    fill_rect(i, 10, 16, 12, 5);
    rect(i, 9, 20, 30, 19, 2);
    hline(i, 12, 35, 25, 2);
}

void draw_gallery(Icon &i)
{
    rect(i, 9, 11, 30, 27, 3);
    fill_circle(i, 31, 19, 3);
    line(i, 12, 34, 20, 25, 3);
    line(i, 20, 25, 26, 31, 3);
    line(i, 26, 31, 33, 23, 3);
}

void draw_ota(Icon &i)
{
    circle(i, 18, 29, 8, 2);
    circle(i, 29, 27, 10, 2);
    hline(i, 13, 37, 36, 2);
    line(i, 24, 32, 24, 14, 3);
    line(i, 24, 14, 17, 21, 3);
    line(i, 24, 14, 31, 21, 3);
}

void draw_sensors(Icon &i)
{
    circle(i, 24, 24, 15, 2);
    line(i, 24, 24, 24, 10, 2);
    line(i, 24, 24, 37, 31, 2);
    line(i, 24, 24, 12, 33, 2);
    fill_circle(i, 24, 24, 3);
    fill_circle(i, 24, 10, 2);
    fill_circle(i, 37, 31, 2);
    fill_circle(i, 12, 33, 2);
}

void draw_spectrum(Icon &i)
{
    int xs[] = {10, 16, 22, 28, 34};
    int hs[] = {11, 22, 16, 27, 20};
    for (int n = 0; n < 5; ++n) {
        fill_rect(i, xs[n], 38 - hs[n], 4, hs[n]);
    }
}

void draw_quick(Icon &i)
{
    line(i, 27, 8, 15, 26, 3);
    line(i, 15, 26, 25, 26, 3);
    line(i, 25, 26, 20, 40, 3);
    line(i, 20, 40, 34, 21, 3);
    line(i, 34, 21, 24, 21, 3);
}

void draw_audio(Icon &i)
{
    fill_rect(i, 9, 21, 7, 9);
    line(i, 16, 21, 26, 14, 2);
    line(i, 16, 29, 26, 36, 2);
    vline(i, 26, 14, 36, 2);
    circle(i, 31, 25, 6, 1);
    circle(i, 31, 25, 11, 1);
}

void draw_music(Icon &i)
{
    vline(i, 28, 12, 31, 3);
    hline(i, 28, 38, 12, 3);
    vline(i, 38, 12, 27, 3);
    fill_circle(i, 23, 34, 5);
    fill_circle(i, 34, 30, 5);
}

void draw_alarm(Icon &i)
{
    circle(i, 24, 27, 13, 2);
    line(i, 24, 27, 24, 18, 2);
    line(i, 24, 27, 31, 31, 2);
    line(i, 15, 13, 8, 8, 2);
    line(i, 33, 13, 40, 8, 2);
    hline(i, 20, 28, 42, 2);
}

void draw_calendar(Icon &i)
{
    rect(i, 10, 12, 28, 28, 2);
    hline(i, 11, 37, 20, 2);
    vline(i, 17, 10, 16, 2);
    vline(i, 31, 10, 16, 2);
    fill_rect(i, 16, 25, 4, 4);
    fill_rect(i, 23, 25, 4, 4);
    fill_rect(i, 30, 25, 4, 4);
    fill_rect(i, 16, 32, 4, 4);
    fill_rect(i, 23, 32, 4, 4);
}

void draw_anniversary(Icon &i)
{
    fill_circle(i, 18, 18, 7);
    fill_circle(i, 30, 18, 7);
    line(i, 11, 21, 24, 38, 4);
    line(i, 37, 21, 24, 38, 4);
    fill_rect(i, 16, 17, 17, 11);
    line(i, 24, 11, 24, 18, 2);
    line(i, 21, 14, 27, 14, 2);
}

void draw_weather(Icon &i)
{
    fill_circle(i, 18, 18, 7);
    line(i, 18, 6, 18, 10, 2);
    line(i, 7, 18, 11, 18, 2);
    line(i, 25, 11, 29, 7, 2);
    line(i, 9, 9, 12, 12, 2);
    fill_circle(i, 26, 29, 10);
    fill_circle(i, 16, 31, 7);
    fill_circle(i, 34, 33, 6);
    hline(i, 13, 39, 38, 3);
}

void draw_timer(Icon &i)
{
    circle(i, 24, 26, 15, 2);
    hline(i, 19, 29, 8, 3);
    vline(i, 23, 9, 13, 2);
    line(i, 24, 26, 24, 16, 2);
    line(i, 24, 26, 32, 30, 2);
    line(i, 13, 14, 9, 10, 2);
    line(i, 35, 14, 39, 10, 2);
}

void draw_power(Icon &i)
{
    rect(i, 9, 13, 28, 22, 3);
    fill_rect(i, 37, 19, 4, 10);
    hline(i, 13, 20, 17, 2, SOFT_WHITE);
    line(i, 27, 15, 18, 27, 3);
    line(i, 18, 27, 25, 27, 3);
    line(i, 25, 27, 20, 39, 3);
    line(i, 20, 39, 31, 24, 3);
    line(i, 31, 24, 24, 24, 3);
}

void draw_cloud(Icon &i)
{
    circle(i, 18, 29, 8, 2);
    circle(i, 29, 27, 10, 2);
    hline(i, 13, 38, 36, 3);
    line(i, 15, 16, 33, 16, 2);
    line(i, 15, 16, 10, 21, 2);
    line(i, 33, 16, 38, 21, 2);
    fill_circle(i, 24, 16, 3);
    fill_circle(i, 10, 21, 3);
    fill_circle(i, 38, 21, 3);
}

void draw_quota(Icon &i)
{
    rect(i, 10, 11, 28, 26, 2);
    hline(i, 13, 35, 18, 2);
    vline(i, 18, 25, 34, 3);
    vline(i, 24, 21, 34, 3);
    vline(i, 30, 28, 34, 3);
    line(i, 16, 40, 32, 40, 2);
    line(i, 24, 8, 24, 14, 2);
    line(i, 21, 11, 27, 11, 2);
}

void draw_ai_chat(Icon &i)
{
    rect(i, 9, 12, 30, 20, 3);
    line(i, 15, 32, 12, 39, 3);
    hline(i, 16, 33, 37, 3);
    fill_circle(i, 17, 22, 2);
    fill_circle(i, 24, 22, 2);
    fill_circle(i, 31, 22, 2);
    line(i, 35, 9, 35, 14, 2, SOFT_WHITE);
    line(i, 32, 12, 38, 12, 2, SOFT_WHITE);
}

void draw_codex(Icon &i)
{
    rect(i, 8, 12, 32, 24, 3);
    line(i, 15, 20, 21, 25, 3);
    line(i, 21, 25, 15, 30, 3);
    hline(i, 26, 33, 30, 3);
}

void draw_robots(Icon &i)
{
    rect(i, 10, 16, 28, 19, 4);
    vline(i, 23, 10, 16, 2);
    fill_circle(i, 24, 9, 2);
    fill_circle(i, 18, 25, 3);
    fill_circle(i, 30, 25, 3);
    hline(i, 19, 29, 32, 2);
    line(i, 10, 23, 6, 20, 2);
    line(i, 38, 23, 42, 20, 2);
}

void draw_game(Icon &i)
{
    rect(i, 9, 9, 30, 30, 3);
    hline(i, 10, 37, 19, 2);
    hline(i, 10, 37, 29, 2);
    vline(i, 19, 10, 37, 2);
    vline(i, 29, 10, 37, 2);
    fill_rect(i, 12, 12, 5, 5);
    fill_rect(i, 22, 22, 5, 5);
    fill_rect(i, 32, 32, 4, 4);
}

void draw_counter(Icon &i)
{
    circle(i, 17, 24, 9, 2);
    circle(i, 31, 24, 9, 2);
    hline(i, 12, 22, 24, 2);
    vline(i, 16, 19, 29, 2);
    hline(i, 26, 36, 24, 2);
}

void draw_level(Icon &i)
{
    circle(i, 24, 24, 15, 2, SOFT_WHITE);
    hline(i, 12, 36, 23, 2, SOFT_WHITE);
    vline(i, 23, 12, 36, 2, SOFT_WHITE);
    circle(i, 24, 24, 6, 2);
    fill_circle(i, 24, 24, 3);
}

void draw_squareline(Icon &i)
{
    rect(i, 10, 10, 28, 28, 3);
    fill_rect(i, 14, 14, 7, 7);
    fill_rect(i, 27, 14, 7, 7);
    fill_rect(i, 14, 27, 7, 7);
    fill_rect(i, 27, 27, 7, 7, SOFT_WHITE);
}

const lv_image_dsc_t *build(Icon &icon, void (*draw)(Icon &), Color base, Color accent)
{
    if (!icon.ready) {
        /*
         * The board has external PSRAM enabled. Keeping launcher bitmaps there
         * leaves scarce internal DRAM available for WiFi, audio, and LVGL
         * transient buffers. These descriptors remain valid for the lifetime
         * of the firmware.
         */
        icon.pixels = static_cast<uint8_t *>(heap_caps_calloc(1, W * H * 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (icon.pixels == nullptr) {
            return nullptr;
        }
        icon.dsc.data = icon.pixels;
        clear(icon);
        draw_tile(icon, base, accent);
        draw(icon);
        icon.ready = true;
    }
    return &icon.dsc;
}

} // namespace

const lv_image_dsc_t *watch_app_icon_home_48() { return build(s_home, draw_home, {17, 105, 157}, {69, 205, 229}); }
const lv_image_dsc_t *watch_app_icon_settings_48() { return build(s_settings, draw_settings, {91, 67, 177}, {185, 128, 234}); }
const lv_image_dsc_t *watch_app_icon_files_48() { return build(s_files, draw_files, {28, 116, 133}, {81, 204, 197}); }
const lv_image_dsc_t *watch_app_icon_gallery_48() { return build(s_gallery, draw_gallery, {111, 73, 142}, {232, 136, 205}); }
const lv_image_dsc_t *watch_app_icon_ota_48() { return build(s_ota, draw_ota, {21, 122, 94}, {101, 220, 164}); }
const lv_image_dsc_t *watch_app_icon_sensors_48() { return build(s_sensors, draw_sensors, {159, 85, 29}, {240, 170, 76}); }
const lv_image_dsc_t *watch_app_icon_level_48() { return build(s_level, draw_level, {31, 104, 91}, {98, 222, 177}); }
const lv_image_dsc_t *watch_app_icon_spectrum_48() { return build(s_spectrum, draw_spectrum, {157, 43, 97}, {241, 118, 174}); }
const lv_image_dsc_t *watch_app_icon_quick_48() { return build(s_quick, draw_quick, {187, 112, 22}, {251, 201, 92}); }
const lv_image_dsc_t *watch_app_icon_audio_48() { return build(s_audio, draw_audio, {102, 71, 177}, {169, 143, 244}); }
const lv_image_dsc_t *watch_app_icon_music_48() { return build(s_music, draw_music, {188, 45, 112}, {244, 127, 182}); }
const lv_image_dsc_t *watch_app_icon_alarm_48() { return build(s_alarm, draw_alarm, {185, 62, 73}, {247, 129, 137}); }
const lv_image_dsc_t *watch_app_icon_calendar_48() { return build(s_calendar, draw_calendar, {49, 101, 170}, {111, 183, 244}); }
const lv_image_dsc_t *watch_app_icon_anniversary_48() { return build(s_anniversary, draw_anniversary, {160, 47, 102}, {241, 128, 177}); }
const lv_image_dsc_t *watch_app_icon_weather_48() { return build(s_weather, draw_weather, {17, 115, 156}, {103, 205, 234}); }
const lv_image_dsc_t *watch_app_icon_timer_48() { return build(s_timer, draw_timer, {67, 78, 170}, {126, 159, 245}); }
const lv_image_dsc_t *watch_app_icon_power_48() { return build(s_power, draw_power, {20, 112, 109}, {75, 210, 186}); }
const lv_image_dsc_t *watch_app_icon_cloud_48() { return build(s_cloud, draw_cloud, {51, 79, 128}, {123, 165, 229}); }
const lv_image_dsc_t *watch_app_icon_quota_48() { return build(s_quota, draw_quota, {55, 116, 75}, {123, 204, 141}); }
const lv_image_dsc_t *watch_app_icon_ai_chat_48() { return build(s_ai_chat, draw_ai_chat, {21, 98, 160}, {100, 193, 235}); }
const lv_image_dsc_t *watch_app_icon_codex_48() { return build(s_codex, draw_codex, {15, 101, 98}, {84, 212, 187}); }
const lv_image_dsc_t *watch_app_icon_robots_48() { return build(s_robots, draw_robots, {77, 61, 170}, {152, 139, 241}); }
const lv_image_dsc_t *watch_app_icon_game_48() { return build(s_game, draw_game, {180, 77, 32}, {244, 156, 80}); }
const lv_image_dsc_t *watch_app_icon_counter_48() { return build(s_counter, draw_counter, {38, 112, 85}, {104, 204, 147}); }
const lv_image_dsc_t *watch_app_icon_squareline_48() { return build(s_squareline, draw_squareline, {47, 73, 149}, {133, 161, 242}); }
