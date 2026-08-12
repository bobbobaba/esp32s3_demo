#include "watch_app_icons.hpp"

#include <cstdint>
#include <cstring>

namespace {

constexpr int W = 48;
constexpr int H = 48;
constexpr uint8_t OPAQUE = 0xff;

struct Icon {
    uint8_t pixels[W * H * 4] = {};
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
        .data_size = sizeof(pixels),
        .data = pixels,
        .reserved = nullptr,
        .reserved_2 = nullptr,
    };
    bool ready = false;
};

Icon s_home;
Icon s_files;
Icon s_ota;
Icon s_sensors;
Icon s_spectrum;
Icon s_quick;
Icon s_audio;
Icon s_music;
Icon s_alarm;
Icon s_calendar;
Icon s_anniversary;
Icon s_weather;
Icon s_timer;
Icon s_cloud;
Icon s_quota;

void put(Icon &icon, int x, int y, uint8_t alpha = OPAQUE)
{
    if ((x < 0) || (x >= W) || (y < 0) || (y >= H)) {
        return;
    }
    uint8_t *p = &icon.pixels[(y * W + x) * 4];
    p[0] = 0xff;
    p[1] = 0xff;
    p[2] = 0xff;
    p[3] = alpha;
}

void hline(Icon &icon, int x0, int x1, int y, int t = 1)
{
    if (x0 > x1) {
        int tmp = x0;
        x0 = x1;
        x1 = tmp;
    }
    for (int yy = y; yy < y + t; ++yy) {
        for (int x = x0; x <= x1; ++x) {
            put(icon, x, yy);
        }
    }
}

void vline(Icon &icon, int x, int y0, int y1, int t = 1)
{
    if (y0 > y1) {
        int tmp = y0;
        y0 = y1;
        y1 = tmp;
    }
    for (int xx = x; xx < x + t; ++xx) {
        for (int y = y0; y <= y1; ++y) {
            put(icon, xx, y);
        }
    }
}

void rect(Icon &icon, int x, int y, int w, int h, int t = 2)
{
    hline(icon, x, x + w - 1, y, t);
    hline(icon, x, x + w - 1, y + h - t, t);
    vline(icon, x, y, y + h - 1, t);
    vline(icon, x + w - t, y, y + h - 1, t);
}

void fill_rect(Icon &icon, int x, int y, int w, int h)
{
    for (int yy = y; yy < y + h; ++yy) {
        hline(icon, x, x + w - 1, yy);
    }
}

void line(Icon &icon, int x0, int y0, int x1, int y1, int t = 1)
{
    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = (y1 > y0) ? (y0 - y1) : (y1 - y0);
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;
    while (true) {
        for (int oy = 0; oy < t; ++oy) {
            for (int ox = 0; ox < t; ++ox) {
                put(icon, x0 + ox, y0 + oy);
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

void circle(Icon &icon, int cx, int cy, int r, int t = 2)
{
    for (int y = cy - r - t; y <= cy + r + t; ++y) {
        for (int x = cx - r - t; x <= cx + r + t; ++x) {
            int dx = x - cx;
            int dy = y - cy;
            int d2 = dx * dx + dy * dy;
            if ((d2 >= (r - t) * (r - t)) && (d2 <= (r + t) * (r + t))) {
                put(icon, x, y);
            }
        }
    }
}

void fill_circle(Icon &icon, int cx, int cy, int r)
{
    for (int y = cy - r; y <= cy + r; ++y) {
        for (int x = cx - r; x <= cx + r; ++x) {
            int dx = x - cx;
            int dy = y - cy;
            if ((dx * dx + dy * dy) <= r * r) {
                put(icon, x, y);
            }
        }
    }
}

void clear(Icon &icon)
{
    std::memset(icon.pixels, 0, sizeof(icon.pixels));
}

const lv_image_dsc_t *finish(Icon &icon)
{
    icon.ready = true;
    return &icon.dsc;
}

void draw_home(Icon &i)
{
    line(i, 10, 25, 24, 12, 3);
    line(i, 24, 12, 38, 25, 3);
    rect(i, 15, 24, 18, 14, 2);
    vline(i, 24, 29, 38, 2);
}

void draw_files(Icon &i)
{
    fill_rect(i, 10, 16, 12, 5);
    rect(i, 9, 20, 30, 19, 2);
    hline(i, 12, 35, 25, 2);
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

const lv_image_dsc_t *build(Icon &icon, void (*draw)(Icon &))
{
    if (!icon.ready) {
        clear(icon);
        draw(icon);
        finish(icon);
    }
    return &icon.dsc;
}

} // namespace

const lv_image_dsc_t *watch_app_icon_home_48() { return build(s_home, draw_home); }
const lv_image_dsc_t *watch_app_icon_files_48() { return build(s_files, draw_files); }
const lv_image_dsc_t *watch_app_icon_ota_48() { return build(s_ota, draw_ota); }
const lv_image_dsc_t *watch_app_icon_sensors_48() { return build(s_sensors, draw_sensors); }
const lv_image_dsc_t *watch_app_icon_spectrum_48() { return build(s_spectrum, draw_spectrum); }
const lv_image_dsc_t *watch_app_icon_quick_48() { return build(s_quick, draw_quick); }
const lv_image_dsc_t *watch_app_icon_audio_48() { return build(s_audio, draw_audio); }
const lv_image_dsc_t *watch_app_icon_music_48() { return build(s_music, draw_music); }
const lv_image_dsc_t *watch_app_icon_alarm_48() { return build(s_alarm, draw_alarm); }
const lv_image_dsc_t *watch_app_icon_calendar_48() { return build(s_calendar, draw_calendar); }
const lv_image_dsc_t *watch_app_icon_anniversary_48() { return build(s_anniversary, draw_anniversary); }
const lv_image_dsc_t *watch_app_icon_weather_48() { return build(s_weather, draw_weather); }
const lv_image_dsc_t *watch_app_icon_timer_48() { return build(s_timer, draw_timer); }
const lv_image_dsc_t *watch_app_icon_cloud_48() { return build(s_cloud, draw_cloud); }
const lv_image_dsc_t *watch_app_icon_quota_48() { return build(s_quota, draw_quota); }
