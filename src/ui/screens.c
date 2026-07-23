#include <string.h>

#include "screens.h"
#include "images.h"
#include "fonts.h"
#include "actions.h"
#include "vars.h"
#include "styles.h"
#include "ui.h"

objects_t objects;

lv_obj_t *tick_value_change_obj;

static lv_obj_t *screen_base(uint32_t bg) {
    lv_obj_t *screen = lv_obj_create(0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(screen, 128, 128);
    lv_obj_set_style_bg_color(screen, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    return screen;
}

static lv_obj_t *panel(lv_obj_t *parent, int x, int y, int w, int h, uint32_t bg, uint32_t border) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, 6, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(border), 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    return obj;
}

static lv_obj_t *line(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, h / 2, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    return obj;
}

static lv_obj_t *dot(lv_obj_t *parent, int x, int y, int size, uint32_t color) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, size, size);
    lv_obj_set_style_radius(obj, size / 2, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    return obj;
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y, const lv_font_t *font, uint32_t color) {
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_text_letter_space(obj, 0, 0);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
    lv_label_set_text(obj, text);
    return obj;
}

static lv_obj_t *bar(lv_obj_t *parent, int x, int y, int w, uint32_t color, int value) {
    lv_obj_t *obj = lv_bar_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, 4);
    lv_bar_set_range(obj, 0, 100);
    lv_bar_set_value(obj, value, LV_ANIM_OFF);
    lv_obj_set_style_radius(obj, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 2, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x222A36), LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), LV_PART_INDICATOR);
    return obj;
}

static void title(lv_obj_t *parent, const char *text, uint32_t accent) {
    line(parent, 0, 0, 128, 20, 0x101722);
    line(parent, 0, 19, 128, 1, 0x263142);
    line(parent, 0, 0, 4, 20, accent);
    dot(parent, 111, 7, 3, 0x46556A);
    dot(parent, 117, 5, 3, 0x8B97A8);
    dot(parent, 123, 3, 3, accent);
    label(parent, text, 10, 4, &lv_font_montserrat_10, 0xF5F7FA);
}

static void footer(lv_obj_t *parent, const char *text) {
    line(parent, 0, 111, 128, 17, 0x0B1017);
    label(parent, text, 6, 115, &lv_font_montserrat_8, 0x91A0B6);
}

void create_screen_home() {
    lv_obj_t *s = screen_base(0x05070B);
    objects.home = s;
    dot(s, 10, 10, 108, 0x0D121A);
    dot(s, 16, 16, 96, 0x05070B);
    line(s, 23, 21, 82, 1, 0x253248);
    line(s, 23, 104, 82, 1, 0x253248);
    panel(s, 32, 35, 64, 54, 0x0B1017, 0x283349);
    objects.home_time = label(s, "12:48", 24, 44, &lv_font_montserrat_32, 0xF8FBFF);
    objects.home_temp = label(s, "TEMP 26C", 34, 26, &lv_font_montserrat_10, 0xFFD75A);
    panel(s, 7, 91, 54, 18, 0x101722, 0x263142);
    panel(s, 67, 91, 54, 18, 0x101722, 0x263142);
    objects.home_cpu = label(s, "CPU 18%", 13, 95, &lv_font_montserrat_8, 0x7DFF7A);
    objects.home_mem = label(s, "MEM 42%", 73, 95, &lv_font_montserrat_8, 0x5FE8FF);
    dot(s, 12, 22, 5, 0x19D3FF);
    dot(s, 111, 22, 5, 0xFF4FA3);
    footer(s, "P4 MENU                 P7 AI");
    tick_screen_home();
}

void tick_screen_home() {}

void create_screen_menu() {
    lv_obj_t *s = screen_base(0x070A0F);
    objects.menu = s;
    title(s, "MENU", 0xFFC928);
    const char *rows[] = {"> AI CALL", "  SERVER", "  LED", "  SETTINGS"};
    const uint32_t colors[] = {0xFF4FA3, 0x7DFF7A, 0x19D3FF, 0xFFC928};
    for (int i = 0; i < 4; ++i) {
        panel(s, 7, 24 + i * 21, 114, 18, i == 0 ? 0x1B1720 : 0x101722, i == 0 ? 0xFFC928 : 0x263142);
        dot(s, 13, 30 + i * 21, 6, colors[i]);
        objects.menu_rows[i] = label(s, rows[i], 27, 27 + i * 21, &lv_font_montserrat_10, i == 0 ? 0xFFFFFF : 0xD8DEE9);
        lv_obj_set_width(objects.menu_rows[i], 86);
    }
    footer(s, "P5 UP       P6 DOWN       P7 OK");
    tick_screen_menu();
}

void tick_screen_menu() {}

void create_screen_ai_call() {
    lv_obj_t *s = screen_base(0x07080D);
    objects.ai_call = s;
    title(s, "AI CALL", 0xFF4FA3);
    dot(s, 42, 31, 44, 0x20101A);
    dot(s, 51, 40, 26, 0xFF4FA3);
    panel(s, 56, 36, 16, 30, 0x05070B, 0xFF8BC2);
    line(s, 48, 67, 32, 4, 0xFF4FA3);
    line(s, 62, 71, 4, 10, 0xFF4FA3);
    objects.ai_status = label(s, "LISTEN", 42, 24, &lv_font_montserrat_12, 0x7DFF7A);
    panel(s, 6, 84, 116, 12, 0x101722, 0x263142);
    panel(s, 6, 98, 116, 12, 0x101722, 0x263142);
    objects.ai_you = label(s, "YOU: ...", 10, 86, &lv_font_montserrat_8, 0xAEB8CA);
    objects.ai_reply = label(s, "AI: ...", 10, 100, &lv_font_montserrat_8, 0xF5F7FA);
    lv_obj_set_width(objects.ai_you, 108);
    lv_obj_set_width(objects.ai_reply, 108);
    footer(s, "P4 EXIT          P5 VOL-  P6 VOL+");
    tick_screen_ai_call();
}

void tick_screen_ai_call() {}

void create_screen_settings() {
    lv_obj_t *s = screen_base(0x070A0F);
    objects.settings = s;
    title(s, "SETTINGS", 0xFFC928);
    panel(s, 7, 25, 114, 22, 0x17140C, 0xFFC928);
    panel(s, 7, 53, 114, 22, 0x0D151C, 0x19D3FF);
    panel(s, 7, 81, 114, 22, 0x0D1710, 0x7DFF7A);
    label(s, "AUDIO", 12, 29, &lv_font_montserrat_8, 0xFFC928);
    label(s, "OTA", 12, 57, &lv_font_montserrat_8, 0x19D3FF);
    label(s, "NET", 12, 85, &lv_font_montserrat_8, 0x7DFF7A);
    objects.settings_volume = label(s, "VOL 500%", 55, 29, &lv_font_montserrat_10, 0xFFFFFF);
    objects.settings_ota = label(s, "OTA OFF", 55, 57, &lv_font_montserrat_10, 0xFFFFFF);
    objects.settings_wifi = label(s, "WIFI ON", 55, 85, &lv_font_montserrat_10, 0xFFFFFF);
    bar(s, 12, 47, 104, 0xFFC928, 42);
    footer(s, "P4 BACK   P5-   P6+   P7 WIFI");
    tick_screen_settings();
}

void tick_screen_settings() {}

void create_screen_server() {
    lv_obj_t *s = screen_base(0x05080B);
    objects.server = s;
    title(s, "SERVER", 0x7DFF7A);
    panel(s, 6, 25, 116, 18, 0x101722, 0x263142);
    panel(s, 6, 48, 116, 18, 0x0C1613, 0x7DFF7A);
    panel(s, 6, 71, 116, 18, 0x11101B, 0xFF4FA3);
    panel(s, 6, 94, 116, 18, 0x17140C, 0xFFC928);
    dot(s, 12, 31, 6, 0xD8DEE9);
    dot(s, 12, 54, 6, 0x7DFF7A);
    dot(s, 12, 77, 6, 0xFF4FA3);
    dot(s, 12, 100, 6, 0xFFC928);
    objects.server_uptime = label(s, "UP 0D 00:00", 24, 28, &lv_font_montserrat_10, 0xFFFFFF);
    objects.server_load = label(s, "LOAD 0.00", 24, 51, &lv_font_montserrat_10, 0xFFFFFF);
    objects.server_mem = label(s, "MEM --%", 24, 74, &lv_font_montserrat_10, 0xFFFFFF);
    objects.server_disk = label(s, "DISK --%", 24, 97, &lv_font_montserrat_10, 0xFFFFFF);
    footer(s, "P4 BACK");
    tick_screen_server();
}

void tick_screen_server() {}

void create_screen_led() {
    lv_obj_t *s = screen_base(0x05070B);
    objects.led = s;
    title(s, "LED FX", 0x19D3FF);
    dot(s, 37, 30, 54, 0x071923);
    dot(s, 47, 40, 34, 0x19D3FF);
    dot(s, 56, 49, 16, 0xFFFFFF);
    line(s, 24, 84, 80, 4, 0x263142);
    dot(s, 23, 79, 14, 0x111820);
    dot(s, 57, 79, 14, 0x111820);
    dot(s, 91, 79, 14, 0x111820);
    dot(s, 27, 83, 6, 0x000000);
    dot(s, 61, 83, 6, 0x19D3FF);
    dot(s, 95, 83, 6, 0xFF4FA3);
    objects.led_mode = label(s, "RAINBOW", 39, 96, &lv_font_montserrat_10, 0x19D3FF);
    footer(s, "P4 OFF  P5 RAIN  P6 BRTH  P7 FLSH");
    tick_screen_led();
}

void tick_screen_led() {}

void create_screen_wi_fi_setup() {
    lv_obj_t *s = screen_base(0x05070B);
    objects.wi_fi_setup = s;
    title(s, "WIFI SETUP", 0x19D3FF);
    panel(s, 35, 28, 58, 58, 0xFFFFFF, 0xFFFFFF);
    panel(s, 40, 33, 12, 12, 0x05070B, 0x05070B);
    panel(s, 76, 33, 12, 12, 0x05070B, 0x05070B);
    panel(s, 40, 69, 12, 12, 0x05070B, 0x05070B);
    line(s, 56, 50, 5, 5, 0x05070B);
    line(s, 66, 50, 14, 5, 0x05070B);
    line(s, 56, 62, 22, 5, 0x05070B);
    objects.wifi_status = label(s, "AP ESP32S3-Setup", 8, 91, &lv_font_montserrat_8, 0xD8DEE9);
    label(s, "192.168.4.1", 27, 102, &lv_font_montserrat_8, 0xFFC928);
    footer(s, "P4 BACK");
    tick_screen_wi_fi_setup();
}

void tick_screen_wi_fi_setup() {}

void create_screen_mpu_data() {
    lv_obj_t *s = screen_base(0x05080B);
    objects.mpu_data = s;
    title(s, "MPU DATA", 0x19D3FF);
    panel(s, 6, 26, 116, 36, 0x0D151C, 0x19D3FF);
    panel(s, 6, 68, 116, 36, 0x17140C, 0xFFC928);
    dot(s, 13, 34, 8, 0x19D3FF);
    dot(s, 13, 76, 8, 0xFFC928);
    label(s, "ACC", 25, 31, &lv_font_montserrat_8, 0x19D3FF);
    label(s, "GYR", 25, 73, &lv_font_montserrat_8, 0xFFC928);
    objects.mpu_acc = label(s, "X --  Y --\nZ --", 25, 42, &lv_font_montserrat_8, 0xFFFFFF);
    objects.mpu_gyro = label(s, "X --  Y --\nZ --", 25, 84, &lv_font_montserrat_8, 0xFFFFFF);
    lv_obj_set_width(objects.mpu_acc, 90);
    lv_obj_set_width(objects.mpu_gyro, 90);
    footer(s, "P4 BACK                 P7 CAL");
    tick_screen_mpu_data();
}

void tick_screen_mpu_data() {}

typedef void (*tick_screen_func_t)();
tick_screen_func_t tick_screen_funcs[] = {
    tick_screen_home,
    tick_screen_menu,
    tick_screen_ai_call,
    tick_screen_settings,
    tick_screen_server,
    tick_screen_led,
    tick_screen_wi_fi_setup,
    tick_screen_mpu_data,
};

void tick_screen(int screen_index) {
    if (screen_index >= 0 && screen_index < 8) {
        tick_screen_funcs[screen_index]();
    }
}

void tick_screen_by_id(enum ScreensEnum screenId) {
    tick_screen(screenId - 1);
}

ext_font_desc_t fonts[] = {
#if LV_FONT_MONTSERRAT_8
    { "MONTSERRAT_8", &lv_font_montserrat_8 },
#endif
#if LV_FONT_MONTSERRAT_10
    { "MONTSERRAT_10", &lv_font_montserrat_10 },
#endif
#if LV_FONT_MONTSERRAT_12
    { "MONTSERRAT_12", &lv_font_montserrat_12 },
#endif
#if LV_FONT_MONTSERRAT_32
    { "MONTSERRAT_32", &lv_font_montserrat_32 },
#endif
};

uint32_t active_theme_index = 0;

void create_screens() {
    lv_disp_t *dispp = lv_disp_get_default();
    lv_theme_t *theme = lv_theme_default_init(dispp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), false, LV_FONT_DEFAULT);
    lv_disp_set_theme(dispp, theme);
    create_screen_home();
    create_screen_menu();
    create_screen_ai_call();
    create_screen_settings();
    create_screen_server();
    create_screen_led();
    create_screen_wi_fi_setup();
    create_screen_mpu_data();
}
