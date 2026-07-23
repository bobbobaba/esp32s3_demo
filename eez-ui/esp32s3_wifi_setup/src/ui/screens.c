#include <string.h>
#include "screens.h"
#include "images.h"
#include "fonts.h"
#include "actions.h"
#include "vars.h"
#include "styles.h"
#include "ui.h"

objects_t objects;

static lv_obj_t *make_screen() {
    lv_obj_t *screen = lv_obj_create(0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(screen, 128, 128);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x05070B), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    return screen;
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y, const lv_font_t *font, uint32_t color) {
    lv_obj_t *obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_text_letter_space(obj, 0, 0);
    return obj;
}

static lv_obj_t *plate(lv_obj_t *parent, int x, int y, int w, int h, uint32_t border) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, 4, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x101723), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(border), 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    return obj;
}

static void title(lv_obj_t *parent, const char *text, uint32_t color) {
    label(parent, text, 4, 4, &lv_font_montserrat_10, color);
    lv_obj_t *line = lv_obj_create(parent);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(line, 0, 18);
    lv_obj_set_size(line, 128, 2);
    lv_obj_set_style_bg_color(line, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(line, 0, 0);
}

void create_screen_home() {
    lv_obj_t *s = make_screen();
    objects.home = s;
    title(s, "ESP32-S3", 0x19D3FF);
    objects.home_time = label(s, "12:48", 20, 28, &lv_font_montserrat_32, 0xF8FBFF);
    plate(s, 6, 72, 116, 34, 0x2D3442);
    objects.home_temp = label(s, "TEMP 26C", 12, 77, &lv_font_montserrat_10, 0xFFD75A);
    objects.home_cpu = label(s, "CPU 18%", 12, 91, &lv_font_montserrat_8, 0x7DFF7A);
    objects.home_mem = label(s, "MEM 42%", 70, 91, &lv_font_montserrat_8, 0x5FE8FF);
    label(s, "P4 MENU   P7 AI", 10, 114, &lv_font_montserrat_8, 0x8E9BB1);
    tick_screen_home();
}

void create_screen_menu() {
    lv_obj_t *s = make_screen();
    objects.menu = s;
    title(s, "MENU", 0xFFC928);
    const char *rows[] = {"> AI CALL", "  SERVER", "  LED", "  SETTINGS"};
    for (int i = 0; i < 4; ++i) {
        plate(s, 6, 24 + i * 21, 116, 18, i == 0 ? 0xFFC928 : 0x2D3442);
        objects.menu_rows[i] = label(s, rows[i], 14, 28 + i * 21, &lv_font_montserrat_10, i == 0 ? 0xFFFFFF : 0xD6DBE6);
    }
    label(s, "P5/P6 SEL  P7 OK", 5, 114, &lv_font_montserrat_8, 0x8E9BB1);
    tick_screen_menu();
}

void create_screen_ai_call() {
    lv_obj_t *s = make_screen();
    objects.ai_call = s;
    title(s, "AI CALL", 0xFF4FA3);
    objects.ai_status = label(s, "LISTEN", 43, 27, &lv_font_montserrat_12, 0x7DFF7A);
    plate(s, 34, 45, 60, 24, 0xFF4FA3);
    label(s, "MIC", 53, 52, &lv_font_montserrat_12, 0xFFFFFF);
    objects.ai_you = label(s, "YOU: ...", 8, 78, &lv_font_montserrat_8, 0xD6DBE6);
    objects.ai_reply = label(s, "AI: ...", 8, 92, &lv_font_montserrat_8, 0xFFFFFF);
    label(s, "P4 EXIT P5- P6+", 5, 114, &lv_font_montserrat_8, 0x8E9BB1);
    tick_screen_ai_call();
}

void create_screen_settings() {
    lv_obj_t *s = make_screen();
    objects.settings = s;
    title(s, "SETTINGS", 0xFFC928);
    plate(s, 6, 26, 116, 20, 0xFFC928);
    objects.settings_volume = label(s, "VOL 500%", 12, 31, &lv_font_montserrat_10, 0xFFFFFF);
    plate(s, 6, 52, 116, 20, 0x19D3FF);
    objects.settings_ota = label(s, "OTA OFF", 12, 57, &lv_font_montserrat_10, 0xFFFFFF);
    plate(s, 6, 78, 116, 20, 0x7DFF7A);
    objects.settings_wifi = label(s, "WIFI ON", 12, 83, &lv_font_montserrat_10, 0xFFFFFF);
    label(s, "P7 WIFI", 43, 114, &lv_font_montserrat_8, 0x8E9BB1);
    tick_screen_settings();
}

void create_screen_server() {
    lv_obj_t *s = make_screen();
    objects.server = s;
    title(s, "SERVER", 0x7DFF7A);
    objects.server_uptime = label(s, "UP 0D 00:00", 8, 29, &lv_font_montserrat_10, 0xFFFFFF);
    objects.server_load = label(s, "LOAD 0.00", 8, 51, &lv_font_montserrat_10, 0x19D3FF);
    objects.server_mem = label(s, "MEM --%", 8, 73, &lv_font_montserrat_10, 0xFF4FA3);
    objects.server_disk = label(s, "DISK --%", 8, 95, &lv_font_montserrat_10, 0xFFC928);
    label(s, "P4 BACK", 43, 114, &lv_font_montserrat_8, 0x8E9BB1);
    tick_screen_server();
}

void create_screen_led() {
    lv_obj_t *s = make_screen();
    objects.led = s;
    title(s, "LED", 0x19D3FF);
    plate(s, 38, 32, 52, 36, 0x19D3FF);
    objects.led_mode = label(s, "RAINBOW", 39, 77, &lv_font_montserrat_10, 0x19D3FF);
    label(s, "P4 OFF  P5 RAIN", 7, 98, &lv_font_montserrat_8, 0x8E9BB1);
    label(s, "P6 BRTH  P7 FLASH", 1, 114, &lv_font_montserrat_8, 0x8E9BB1);
    tick_screen_led();
}

void create_screen_wifi_setup() {
    lv_obj_t *s = make_screen();
    objects.wifi_setup = s;
    title(s, "WIFI SETUP", 0x19D3FF);
    plate(s, 31, 29, 66, 52, 0xFFFFFF);
    label(s, "QR", 56, 49, &lv_font_montserrat_14, 0xFFFFFF);
    objects.wifi_status = label(s, "AP ESP32S3-Setup", 9, 88, &lv_font_montserrat_8, 0xD6DBE6);
    label(s, "192.168.4.1", 27, 102, &lv_font_montserrat_8, 0xFFC928);
    label(s, "P4 BACK", 43, 116, &lv_font_montserrat_8, 0x8E9BB1);
    tick_screen_wifi_setup();
}

void create_screen_mpu_data() {
    lv_obj_t *s = make_screen();
    objects.mpu_data = s;
    title(s, "MPU DATA", 0x19D3FF);
    plate(s, 6, 28, 116, 34, 0x19D3FF);
    objects.mpu_acc = label(s, "ACC X -- Y --\nZ --", 12, 34, &lv_font_montserrat_8, 0xFFFFFF);
    plate(s, 6, 70, 116, 34, 0xFFC928);
    objects.mpu_gyro = label(s, "GYR X -- Y --\nZ --", 12, 76, &lv_font_montserrat_8, 0xFFFFFF);
    label(s, "P7 CAL P4 BACK", 11, 114, &lv_font_montserrat_8, 0x8E9BB1);
    tick_screen_mpu_data();
}

void tick_screen_home() {}
void tick_screen_menu() {}
void tick_screen_ai_call() {}
void tick_screen_settings() {}
void tick_screen_server() {}
void tick_screen_led() {}
void tick_screen_wifi_setup() {}
void tick_screen_mpu_data() {}

typedef void (*tick_screen_func_t)();
static tick_screen_func_t tick_screen_funcs[] = {
    tick_screen_home,
    tick_screen_menu,
    tick_screen_ai_call,
    tick_screen_settings,
    tick_screen_server,
    tick_screen_led,
    tick_screen_wifi_setup,
    tick_screen_mpu_data,
};

void tick_screen(int screen_index) {
    if (screen_index >= 0 && screen_index < 8) tick_screen_funcs[screen_index]();
}

void tick_screen_by_id(enum ScreensEnum screenId) {
    tick_screen(screenId - 1);
}

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
    create_screen_wifi_setup();
    create_screen_mpu_data();
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
#if LV_FONT_MONTSERRAT_14
    { "MONTSERRAT_14", &lv_font_montserrat_14 },
#endif
#if LV_FONT_MONTSERRAT_32
    { "MONTSERRAT_32", &lv_font_montserrat_32 },
#endif
};

uint32_t active_theme_index = 0;
