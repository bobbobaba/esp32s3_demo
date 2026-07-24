#include <string.h>

#include "screens.h"
#include "images.h"
#include "catstand_img.h"
#include "pixel_watch_img.h"
#include "fonts.h"
#include "actions.h"
#include "vars.h"
#include "styles.h"
#include "ui.h"

objects_t objects;

lv_obj_t *tick_value_change_obj;

static lv_obj_t *screen_base(uint32_t bg) {
    lv_obj_t *screen = lv_obj_create(0);
    (void)bg;
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(screen, 128, 128);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xFFFFFF), 0);
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
    // unscii是8px等宽字体，-1字距压到7px/字符，与原Montserrat布局宽度兼容
    lv_obj_set_style_text_letter_space(obj, font == &lv_font_unscii_8 ? -1 : 0, 0);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
    lv_label_set_text(obj, text);
    return obj;
}

static lv_obj_t *shape(lv_obj_t *parent, int x, int y, int w, int h, int radius, uint32_t color) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    return obj;
}

// 冒险岛式描边文字：阴影副本挂在user_data上，lvglSetLabel会同步文本
static lv_obj_t *pixel_label(lv_obj_t *parent, const char *text, int x, int y, int w, uint32_t fill, uint32_t shadow_color) {
    lv_obj_t *shadow = label(parent, text, x + 1, y + 1, w, &lv_font_unscii_8, shadow_color);
    lv_obj_t *main = label(parent, text, x, y, w, &lv_font_unscii_8, fill);
    lv_obj_set_user_data(main, shadow);
    return main;
}

// 血条/蓝条底槽
static lv_obj_t *bar_track(lv_obj_t *parent, int x, int y, int w, int h) {
    lv_obj_t *obj = shape(parent, x, y, w, h, 2, 0x3A3A44);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x2B1B10), 0);
    return obj;
}

static const lv_font_t *font_for_label(const char *text) {
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (*p >= 0x80) return &lv_font_simsun_16_cjk;
    }
    // 8位像素点阵字体，匹配冒险岛像素风
    return &lv_font_unscii_8;
}

static uint32_t color_for_label(const char *text) {
    if (strstr(text, "26C") || strstr(text, "--C")) return 0x8A5A00;
    if (strstr(text, "CPU")) return 0x146C2E;
    if (strstr(text, "MEM")) return 0x0B5C7A;
    if (strstr(text, "OTA")) return 0x006C8F;
    if (strstr(text, "WIFI") || strstr(text, "NET")) return 0x146C2E;
    if (strstr(text, "RAINBOW") || strstr(text, "LED")) return 0x006C8F;
    if (strstr(text, "P4") || strstr(text, "P5") || strstr(text, "P6") || strstr(text, "P7")) return 0x344054;
    return 0x111827;
}

void create_screen_home() {
    lv_obj_t *s = screen_base(0x05070B);
    objects.home = s;
    objects.home_bg = lv_img_create(s);
    lv_img_set_src(objects.home_bg, &pixel_bg_day);
    lv_obj_set_pos(objects.home_bg, 0, 0);
    // 方案A：左侧小公告板 + 场景冒险元素 + 底部泥地操作栏
    pixel_label(s, "LV1", 3, 3, 24, 0xFFDC50, 0x2B1B10);
    pixel_label(s, "HP", 80, 2, 16, 0xFFB4B4, 0x2B1B10);
    bar_track(s, 28, 3, 50, 7);
    objects.home_cpu_bar = shape(s, 29, 4, 2, 5, 1, 0xE84545);
    pixel_label(s, "MP", 80, 11, 16, 0xB4C8FF, 0x2B1B10);
    bar_track(s, 28, 12, 50, 7);
    objects.home_mem_bar = shape(s, 29, 13, 2, 5, 1, 0x3D7BFF);
    objects.home_wifi_icon = lv_img_create(s);
    lv_img_set_src(objects.home_wifi_icon, &pixel_wifi_0);
    lv_obj_set_pos(objects.home_wifi_icon, 102, 2);

    lv_obj_t *board = lv_img_create(s);
    lv_img_set_src(board, &pixel_board);
    lv_obj_set_pos(board, 3, 22);
    // 板内缩小金色数字时钟：13x17数字 + 6x17冒号
    static const int kDigitX[4] = {8, 22, 40, 54};
    for (int i = 0; i < 4; ++i) {
        objects.home_time_digits[i] = lv_img_create(s);
        lv_img_set_src(objects.home_time_digits[i], &pixel_digit_0);
        lv_obj_set_pos(objects.home_time_digits[i], kDigitX[i], 36);
    }
    lv_obj_t *colon = lv_img_create(s);
    lv_img_set_src(colon, &pixel_digit_colon);
    lv_obj_set_pos(colon, 34, 36);
    objects.home_date = pixel_label(s, "--/--", 9, 62, 40, 0x8C3A12, 0xFFE2A8);
    objects.home_weather_icon = lv_img_create(s);
    lv_img_set_src(objects.home_weather_icon, &pixel_icon_sunny);
    lv_obj_set_pos(objects.home_weather_icon, 9, 70);
    objects.home_temp = pixel_label(s, "26C", 23, 72, 20, 0xA03A0E, 0xFFE2A8);

    // 板上/旁装饰
    lv_obj_t *leaf = lv_img_create(s);
    lv_img_set_src(leaf, &pixel_adv_leaf);
    lv_obj_set_pos(leaf, 5, 32);
    lv_obj_t *star1 = lv_img_create(s);
    lv_img_set_src(star1, &pixel_adv_star);
    lv_obj_set_pos(star1, 66, 26);
    lv_obj_t *star2 = lv_img_create(s);
    lv_img_set_src(star2, &pixel_adv_star);
    lv_obj_set_pos(star2, 72, 40);

    objects.home_cat = lv_img_create(s);
    lv_img_set_src(objects.home_cat, &pixel_cat_idle0);
    lv_obj_set_pos(objects.home_cat, 82, 58);

    // 地面上方冒险元素
    lv_obj_t *slime = lv_img_create(s);
    lv_img_set_src(slime, &pixel_adv_slime);
    lv_obj_set_pos(slime, 48, 82);
    lv_obj_t *chest = lv_img_create(s);
    lv_img_set_src(chest, &pixel_adv_chest);
    lv_obj_set_pos(chest, 64, 84);
    lv_obj_t *coin1 = lv_img_create(s);
    lv_img_set_src(coin1, &pixel_adv_coin);
    lv_obj_set_pos(coin1, 78, 88);
    lv_obj_t *coin2 = lv_img_create(s);
    lv_img_set_src(coin2, &pixel_adv_coin);
    lv_obj_set_pos(coin2, 55, 92);
    lv_obj_t *orb = lv_img_create(s);
    lv_img_set_src(orb, &pixel_adv_orb);
    lv_obj_set_pos(orb, 112, 68);
    lv_obj_t *potion = lv_img_create(s);
    lv_img_set_src(potion, &pixel_adv_potion);
    lv_obj_set_pos(potion, 70, 76);
    lv_obj_t *scroll = lv_img_create(s);
    lv_img_set_src(scroll, &pixel_adv_scroll);
    lv_obj_set_pos(scroll, 95, 80);

    // 底部泥地操作栏：对话窗 + EXP条 + 四个圆形菜单按钮
    lv_obj_t *ground = lv_img_create(s);
    lv_img_set_src(ground, &pixel_ground);
    lv_obj_set_pos(ground, 0, 100);
    lv_obj_t *chat = lv_img_create(s);
    lv_img_set_src(chat, &pixel_hud_chat);
    lv_obj_set_pos(chat, 3, 106);
    pixel_label(s, "AI", 6, 108, 16, 0x78C8FF, 0x000000);
    bar_track(s, 46, 108, 32, 7);
    objects.home_exp_bar = shape(s, 47, 109, 2, 5, 1, 0xFFC832);
    pixel_label(s, "EXP", 46, 117, 24, 0xFFDC78, 0x2B1B10);
    lv_obj_t *btn_menu = lv_img_create(s);
    lv_img_set_src(btn_menu, &pixel_hud_menu);
    lv_obj_set_pos(btn_menu, 84, 108);
    lv_obj_t *btn_chat = lv_img_create(s);
    lv_img_set_src(btn_chat, &pixel_hud_chat_btn);
    lv_obj_set_pos(btn_chat, 95, 108);
    lv_obj_t *btn_bag = lv_img_create(s);
    lv_img_set_src(btn_bag, &pixel_hud_bag);
    lv_obj_set_pos(btn_bag, 106, 108);
    lv_obj_t *btn_quest = lv_img_create(s);
    lv_img_set_src(btn_quest, &pixel_hud_quest);
    lv_obj_set_pos(btn_quest, 117, 108);
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
    label(s, "P4 BACK", 2, 114, 50, font_for_label("P4 BACK"), 0x000000 | color_for_label("P4 BACK"));
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
    label(s, "P4 OFF", 2, 114, 42, font_for_label("P4 OFF"), 0x000000 | color_for_label("P4 OFF"));
    label(s, "P5 RAIN", 44, 114, 49, font_for_label("P5 RAIN"), 0x000000 | color_for_label("P5 RAIN"));
    label(s, "P6/P7", 93, 114, 35, font_for_label("P6/P7"), 0x000000 | color_for_label("P6/P7"));
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
#if LV_FONT_MONTSERRAT_48
    { "MONTSERRAT_48", &lv_font_montserrat_48 },
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
