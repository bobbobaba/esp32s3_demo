#include <string.h>

#include "screens.h"
#include "images.h"
#include "fonts.h"
#include "actions.h"
#include "vars.h"
#include "styles.h"
#include "ui.h"

objects_t objects;

static const uint32_t k_screen_bg = 0x242832;

lv_obj_t *tick_value_change_obj;

static lv_obj_t *screen_base(uint32_t bg) {
    lv_obj_t *screen = lv_obj_create(0);
    (void)bg;
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(screen, 128, 128);
    lv_obj_set_style_bg_color(screen, lv_color_hex(k_screen_bg), 0);
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
    lv_obj_set_style_radius(obj, 5, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(border), 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    return obj;
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y, int w, const lv_font_t *font, uint32_t color) {
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_width(obj, w);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_text_letter_space(obj, 0, 0);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
    lv_label_set_text(obj, text);
    return obj;
}

static const lv_font_t *font_for_label(const char *text) {
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (*p >= 0x80) return &lv_font_simsun_16_cjk;
    }
    if (strcmp(text, "12:48") == 0) return &lv_font_montserrat_24;
    if (strcmp(text, "LISTEN") == 0) return &lv_font_montserrat_12;
    return &lv_font_montserrat_10;
}

static uint32_t color_for_label(const char *text) {
    if (strstr(text, "TEMP")) return 0xFFD75A;
    if (strstr(text, "CPU")) return 0x7DFF7A;
    if (strstr(text, "MEM")) return 0x5FE8FF;
    if (strstr(text, "OTA")) return 0x19D3FF;
    if (strstr(text, "WIFI") || strstr(text, "NET")) return 0x7DFF7A;
    if (strstr(text, "RAINBOW") || strstr(text, "LED")) return 0x19D3FF;
    if (strstr(text, "P4") || strstr(text, "P5") || strstr(text, "P6") || strstr(text, "P7")) return 0x91A0B6;
    return 0xF5F7FA;
}

void create_screen_home() {
    lv_obj_t *s = screen_base(0x05070B);
    objects.home = s;
    panel(s, 8, 24, 112, 22, 0x101722, 0x263142);
    panel(s, 20, 52, 88, 31, 0x0D151C, 0x19D3FF);
    panel(s, 8, 91, 52, 18, 0x30343D, 0x626A78);
    panel(s, 68, 91, 52, 18, 0x30343D, 0x626A78);
    label(s, "ESP32-S3", 18, 4, 92, font_for_label("ESP32-S3"), 0x000000 | color_for_label("ESP32-S3"));
    objects.home_temp = label(s, "TEMP 26C", 30, 28, 78, font_for_label("TEMP 26C"), 0x000000 | color_for_label("TEMP 26C"));
    objects.home_time = label(s, "12:48", 27, 58, 74, font_for_label("12:48"), 0x000000 | color_for_label("12:48"));
    objects.home_cpu = label(s, "CPU 18%", 13, 95, 44, font_for_label("CPU 18%"), 0x000000 | color_for_label("CPU 18%"));
    objects.home_mem = label(s, "MEM 42%", 73, 95, 44, font_for_label("MEM 42%"), 0x000000 | color_for_label("MEM 42%"));
    label(s, "P4 MENU", 4, 114, 60, font_for_label("P4 MENU"), 0x000000 | color_for_label("P4 MENU"));
    label(s, "P7 AI", 88, 114, 38, font_for_label("P7 AI"), 0x000000 | color_for_label("P7 AI"));
    tick_screen_home();
}

void tick_screen_home() {}

void create_screen_menu() {
    lv_obj_t *s = screen_base(0x05070B);
    objects.menu = s;
    panel(s, 8, 24, 112, 17, 0x101722, 0x263142);
    panel(s, 8, 45, 112, 17, 0x0D151C, 0x19D3FF);
    panel(s, 8, 66, 112, 17, 0x17140C, 0xFFC928);
    panel(s, 8, 87, 112, 17, 0x0D1710, 0x7DFF7A);
    label(s, "MENU", 47, 4, 42, font_for_label("MENU"), 0x000000 | color_for_label("MENU"));
    objects.menu_rows[0] = label(s, "> SET", 18, 27, 92, font_for_label("> SET"), 0x000000 | color_for_label("> SET"));
    objects.menu_rows[1] = label(s, "  服务器", 18, 48, 92, font_for_label("  服务器"), 0x000000 | color_for_label("  服务器"));
    objects.menu_rows[2] = label(s, "  LED", 18, 69, 92, font_for_label("  LED"), 0x000000 | color_for_label("  LED"));
    objects.menu_rows[3] = label(s, "  语音", 18, 90, 92, font_for_label("  语音"), 0x000000 | color_for_label("  语音"));
    label(s, "P5 UP", 4, 114, 38, font_for_label("P5 UP"), 0x000000 | color_for_label("P5 UP"));
    label(s, "P6 DN", 47, 114, 38, font_for_label("P6 DN"), 0x000000 | color_for_label("P6 DN"));
    label(s, "P7 OK", 88, 114, 38, font_for_label("P7 OK"), 0x000000 | color_for_label("P7 OK"));
    tick_screen_menu();
}

void tick_screen_menu() {}

void create_screen_ai_call() {
    lv_obj_t *s = screen_base(0x05070B);
    objects.ai_call = s;
    panel(s, 42, 34, 44, 40, 0x101722, 0x263142);
    panel(s, 6, 82, 116, 13, 0x0D151C, 0x19D3FF);
    panel(s, 6, 98, 116, 13, 0x17140C, 0xFFC928);
    label(s, "AI CALL", 36, 4, 58, font_for_label("AI CALL"), 0x000000 | color_for_label("AI CALL"));
    objects.ai_status = label(s, "LISTEN", 42, 23, 56, font_for_label("LISTEN"), 0x000000 | color_for_label("LISTEN"));
    label(s, "MIC", 52, 47, 30, font_for_label("MIC"), 0x000000 | color_for_label("MIC"));
    objects.ai_you = label(s, "YOU: ...", 10, 83, 108, font_for_label("YOU: ..."), 0x000000 | color_for_label("YOU: ..."));
    objects.ai_reply = label(s, "AI: ...", 10, 99, 108, font_for_label("AI: ..."), 0x000000 | color_for_label("AI: ..."));
    label(s, "P4 EXIT", 3, 114, 52, font_for_label("P4 EXIT"), 0x000000 | color_for_label("P4 EXIT"));
    label(s, "P5-", 65, 114, 26, font_for_label("P5-"), 0x000000 | color_for_label("P5-"));
    label(s, "P6+", 96, 114, 28, font_for_label("P6+"), 0x000000 | color_for_label("P6+"));
    tick_screen_ai_call();
}

void tick_screen_ai_call() {}

void create_screen_settings() {
    lv_obj_t *s = screen_base(0x05070B);
    objects.settings = s;
    panel(s, 7, 24, 114, 22, 0x101722, 0x263142);
    panel(s, 7, 52, 114, 22, 0x0D151C, 0x19D3FF);
    panel(s, 7, 80, 114, 22, 0x17140C, 0xFFC928);
    label(s, "SETTINGS", 31, 4, 70, font_for_label("SETTINGS"), 0x000000 | color_for_label("SETTINGS"));
    label(s, "AUDIO", 12, 29, 38, font_for_label("AUDIO"), 0x000000 | color_for_label("AUDIO"));
    objects.settings_volume = label(s, "VOL 500%", 58, 29, 58, font_for_label("VOL 500%"), 0x000000 | color_for_label("VOL 500%"));
    label(s, "OTA", 12, 57, 38, font_for_label("OTA"), 0x000000 | color_for_label("OTA"));
    objects.settings_ota = label(s, "OTA OFF", 58, 57, 58, font_for_label("OTA OFF"), 0x000000 | color_for_label("OTA OFF"));
    label(s, "NET", 12, 85, 38, font_for_label("NET"), 0x000000 | color_for_label("NET"));
    objects.settings_wifi = label(s, "WIFI ON", 58, 85, 58, font_for_label("WIFI ON"), 0x000000 | color_for_label("WIFI ON"));
    label(s, "P4 BACK", 2, 114, 48, font_for_label("P4 BACK"), 0x000000 | color_for_label("P4 BACK"));
    label(s, "P5-", 54, 114, 26, font_for_label("P5-"), 0x000000 | color_for_label("P5-"));
    label(s, "P6+", 82, 114, 26, font_for_label("P6+"), 0x000000 | color_for_label("P6+"));
    label(s, "P7 WIFI", 106, 114, 22, font_for_label("P7 WIFI"), 0x000000 | color_for_label("P7 WIFI"));
    tick_screen_settings();
}

void tick_screen_settings() {}

void create_screen_server() {
    lv_obj_t *s = screen_base(0x05070B);
    objects.server = s;
    panel(s, 7, 24, 114, 18, 0x101722, 0x263142);
    panel(s, 7, 47, 114, 18, 0x0D151C, 0x19D3FF);
    panel(s, 7, 70, 114, 18, 0x17140C, 0xFFC928);
    panel(s, 7, 93, 114, 18, 0x0D1710, 0x7DFF7A);
    label(s, "SERVER", 39, 4, 58, font_for_label("SERVER"), 0x000000 | color_for_label("SERVER"));
    objects.server_uptime = label(s, "UP 0D 00:00", 14, 27, 100, font_for_label("UP 0D 00:00"), 0x000000 | color_for_label("UP 0D 00:00"));
    objects.server_load = label(s, "LOAD 0.00", 14, 50, 100, font_for_label("LOAD 0.00"), 0x000000 | color_for_label("LOAD 0.00"));
    objects.server_mem = label(s, "MEM --%", 14, 73, 100, font_for_label("MEM --%"), 0x000000 | color_for_label("MEM --%"));
    objects.server_disk = label(s, "DISK --%", 14, 96, 100, font_for_label("DISK --%"), 0x000000 | color_for_label("DISK --%"));
    label(s, "P4 BACK", 43, 114, 50, font_for_label("P4 BACK"), 0x000000 | color_for_label("P4 BACK"));
    tick_screen_server();
}

void tick_screen_server() {}

void create_screen_led() {
    lv_obj_t *s = screen_base(0x05070B);
    objects.led = s;
    panel(s, 37, 29, 54, 43, 0x101722, 0x263142);
    panel(s, 19, 78, 20, 18, 0x0D151C, 0x19D3FF);
    panel(s, 54, 78, 20, 18, 0x17140C, 0xFFC928);
    panel(s, 89, 78, 20, 18, 0x0D1710, 0x7DFF7A);
    label(s, "LED FX", 42, 4, 50, font_for_label("LED FX"), 0x000000 | color_for_label("LED FX"));
    label(s, "COLOR", 43, 44, 44, font_for_label("COLOR"), 0x000000 | color_for_label("COLOR"));
    objects.led_mode = label(s, "RAINBOW", 38, 98, 64, font_for_label("RAINBOW"), 0x000000 | color_for_label("RAINBOW"));
    label(s, "P4 OFF", 2, 114, 38, font_for_label("P4 OFF"), 0x000000 | color_for_label("P4 OFF"));
    label(s, "P5 RAIN", 41, 114, 46, font_for_label("P5 RAIN"), 0x000000 | color_for_label("P5 RAIN"));
    label(s, "P6/P7", 91, 114, 36, font_for_label("P6/P7"), 0x000000 | color_for_label("P6/P7"));
    tick_screen_led();
}

void tick_screen_led() {}

void create_screen_wi_fi_setup() {
    lv_obj_t *s = screen_base(0x05070B);
    objects.wi_fi_setup = s;
    panel(s, 36, 28, 56, 54, 0x101722, 0x263142);
    label(s, "WIFI SETUP", 27, 4, 78, font_for_label("WIFI SETUP"), 0x000000 | color_for_label("WIFI SETUP"));
    label(s, "QR", 55, 48, 22, font_for_label("QR"), 0x000000 | color_for_label("QR"));
    objects.wifi_status = label(s, "AP ESP32S3", 15, 87, 98, font_for_label("AP ESP32S3"), 0x000000 | color_for_label("AP ESP32S3"));
    label(s, "192.168.4.1", 26, 101, 78, font_for_label("192.168.4.1"), 0x000000 | color_for_label("192.168.4.1"));
    label(s, "P4 BACK", 43, 114, 50, font_for_label("P4 BACK"), 0x000000 | color_for_label("P4 BACK"));
    tick_screen_wi_fi_setup();
}

void tick_screen_wi_fi_setup() {}

void create_screen_mpu_data() {
    lv_obj_t *s = screen_base(0x05070B);
    objects.mpu_data = s;
    panel(s, 7, 25, 114, 34, 0x101722, 0x263142);
    panel(s, 7, 68, 114, 34, 0x0D151C, 0x19D3FF);
    label(s, "MPU DATA", 35, 4, 68, font_for_label("MPU DATA"), 0x000000 | color_for_label("MPU DATA"));
    objects.mpu_acc = label(s, "ACC X --  Y --", 14, 31, 100, font_for_label("ACC X --  Y --"), 0x000000 | color_for_label("ACC X --  Y --"));
    label(s, "Z --", 14, 45, 50, font_for_label("Z --"), 0x000000 | color_for_label("Z --"));
    objects.mpu_gyro = label(s, "GYR X --  Y --", 14, 74, 100, font_for_label("GYR X --  Y --"), 0x000000 | color_for_label("GYR X --  Y --"));
    label(s, "Z --", 14, 88, 50, font_for_label("Z --"), 0x000000 | color_for_label("Z --"));
    label(s, "P4 BACK", 4, 114, 56, font_for_label("P4 BACK"), 0x000000 | color_for_label("P4 BACK"));
    label(s, "P7 CAL", 82, 114, 46, font_for_label("P7 CAL"), 0x000000 | color_for_label("P7 CAL"));
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
#if LV_FONT_MONTSERRAT_24
    { "MONTSERRAT_24", &lv_font_montserrat_24 },
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
