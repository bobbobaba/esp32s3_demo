#include <string.h>

#include "screens.h"
#include "images.h"
#include "fonts.h"
#include "actions.h"
#include "vars.h"
#include "styles.h"
#include "ui.h"

#include <string.h>

objects_t objects;

//
// Event handlers
//

lv_obj_t *tick_value_change_obj;

//
// Screens
//

void create_screen_home() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.home = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 128, 128);
    {
        lv_obj_t *parent_obj = obj;
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            lv_obj_set_pos(obj, 4, 4);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "ESP32-S3");
        }
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.home_time = obj;
            lv_obj_set_pos(obj, 20, 28);
            lv_obj_set_size(obj, 88, 30);
            lv_label_set_text_static(obj, "12:48");
        }
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.home_temp = obj;
            lv_obj_set_pos(obj, 8, 66);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "TEMP 26C");
        }
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.home_cpu = obj;
            lv_obj_set_pos(obj, 8, 82);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "CPU 18%");
        }
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.home_mem = obj;
            lv_obj_set_pos(obj, 70, 82);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "MEM 42%");
        }
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            lv_obj_set_pos(obj, 8, 112);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "P4 MENU  P7 AI");
        }
    }
    
    tick_screen_home();
}

void tick_screen_home() {
}

void create_screen_menu() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.menu = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 128, 128);
    {
        lv_obj_t *parent_obj = obj;
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            lv_obj_set_pos(obj, 48, 4);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "MENU");
        }
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.menu_rows[0] = obj;
            lv_obj_set_pos(obj, 10, 24);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "> AI CALL");
        }
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.menu_rows[1] = obj;
            lv_obj_set_pos(obj, 10, 42);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "  SERVER");
        }
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.menu_rows[2] = obj;
            lv_obj_set_pos(obj, 10, 60);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "  LED");
        }
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.menu_rows[3] = obj;
            lv_obj_set_pos(obj, 10, 78);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "  SETTINGS");
        }
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            lv_obj_set_pos(obj, 6, 112);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "P5/P6 SEL P7 OK");
        }
    }
    
    tick_screen_menu();
}

void tick_screen_menu() {
}

void create_screen_ai_call() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.ai_call = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 128, 128);
    {
        lv_obj_t *parent_obj = obj;
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            lv_obj_set_pos(obj, 38, 4);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "AI CALL");
        }
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.ai_status = obj;
            lv_obj_set_pos(obj, 42, 25);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "LISTEN");
        }
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            lv_obj_set_pos(obj, 42, 46);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "[ MIC ]");
        }
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.ai_you = obj;
            lv_obj_set_pos(obj, 8, 72);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "YOU: ...");
        }
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.ai_reply = obj;
            lv_obj_set_pos(obj, 8, 88);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "AI: ...");
        }
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            lv_obj_set_pos(obj, 4, 112);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "P4 EXIT  P5- P6+");
        }
    }
    
    tick_screen_ai_call();
}

void tick_screen_ai_call() {
}

void create_screen_settings() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.settings = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 128, 128);
    {
        lv_obj_t *parent_obj = obj;
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            lv_obj_set_pos(obj, 34, 4);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "SETTINGS");
        }
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.settings_volume = obj;
            lv_obj_set_pos(obj, 10, 28);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "VOL 500%");
        }
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.settings_ota = obj;
            lv_obj_set_pos(obj, 10, 50);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "OTA OFF");
        }
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.settings_wifi = obj;
            lv_obj_set_pos(obj, 10, 72);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "WIFI ON");
        }
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            lv_obj_set_pos(obj, 10, 94);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "FW 1.0.0");
        }
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            lv_obj_set_pos(obj, 42, 112);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "P7 WIFI");
        }
    }
    
    tick_screen_settings();
}

void tick_screen_settings() {
}

void create_screen_server() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.server = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 128, 128);
    {
        lv_obj_t *parent_obj = obj;
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            lv_obj_set_pos(obj, 42, 4);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "SERVER");
        }
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.server_uptime = obj;
            lv_obj_set_pos(obj, 8, 28);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "UP 0D 00:00");
        }
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.server_load = obj;
            lv_obj_set_pos(obj, 8, 50);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "LOAD 0.00");
        }
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.server_mem = obj;
            lv_obj_set_pos(obj, 8, 72);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "MEM  --%");
        }
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.server_disk = obj;
            lv_obj_set_pos(obj, 8, 94);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "DISK --%");
        }
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            lv_obj_set_pos(obj, 42, 112);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "P4 BACK");
        }
    }
    
    tick_screen_server();
}

void tick_screen_server() {
}

void create_screen_led() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.led = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 128, 128);
    {
        lv_obj_t *parent_obj = obj;
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            lv_obj_set_pos(obj, 54, 4);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "LED");
        }
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            lv_obj_set_pos(obj, 34, 36);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "( COLOR )");
        }
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.led_mode = obj;
            lv_obj_set_pos(obj, 40, 68);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "RAINBOW");
        }
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            lv_obj_set_pos(obj, 7, 96);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "P4 OFF P5 RAIN");
        }
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            lv_obj_set_pos(obj, 0, 112);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "P6 BREATHE P7 FLASH");
        }
    }
    
    tick_screen_led();
}

void tick_screen_led() {
}

void create_screen_wi_fi_setup() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.wi_fi_setup = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 128, 128);
    {
        lv_obj_t *parent_obj = obj;
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            lv_obj_set_pos(obj, 30, 4);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "WIFI SETUP");
        }
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            lv_obj_set_pos(obj, 32, 34);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "[ QR CODE ]");
        }
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.wifi_status = obj;
            lv_obj_set_pos(obj, 10, 80);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "AP ESP32S3-Setup");
        }
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            lv_obj_set_pos(obj, 26, 96);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "192.168.4.1");
        }
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            lv_obj_set_pos(obj, 42, 112);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "P4 BACK");
        }
    }
    
    tick_screen_wi_fi_setup();
}

void tick_screen_wi_fi_setup() {
}

void create_screen_mpu_data() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.mpu_data = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 128, 128);
    {
        lv_obj_t *parent_obj = obj;
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            lv_obj_set_pos(obj, 38, 4);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "MPU DATA");
        }
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.mpu_acc = obj;
            lv_obj_set_pos(obj, 8, 28);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "ACC X --  Y --");
        }
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            lv_obj_set_pos(obj, 8, 44);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "ACC Z --");
        }
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.mpu_gyro = obj;
            lv_obj_set_pos(obj, 8, 66);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "GYR X --  Y --");
        }
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            lv_obj_set_pos(obj, 8, 82);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "GYR Z --");
        }
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            lv_obj_set_pos(obj, 8, 112);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "P7 CAL  P4 BACK");
        }
    }
    
    tick_screen_mpu_data();
}

void tick_screen_mpu_data() {
}

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

//
// Fonts
//

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
#if LV_FONT_MONTSERRAT_16
    { "MONTSERRAT_16", &lv_font_montserrat_16 },
#endif
#if LV_FONT_MONTSERRAT_18
    { "MONTSERRAT_18", &lv_font_montserrat_18 },
#endif
#if LV_FONT_MONTSERRAT_20
    { "MONTSERRAT_20", &lv_font_montserrat_20 },
#endif
#if LV_FONT_MONTSERRAT_22
    { "MONTSERRAT_22", &lv_font_montserrat_22 },
#endif
#if LV_FONT_MONTSERRAT_24
    { "MONTSERRAT_24", &lv_font_montserrat_24 },
#endif
#if LV_FONT_MONTSERRAT_26
    { "MONTSERRAT_26", &lv_font_montserrat_26 },
#endif
#if LV_FONT_MONTSERRAT_28
    { "MONTSERRAT_28", &lv_font_montserrat_28 },
#endif
#if LV_FONT_MONTSERRAT_30
    { "MONTSERRAT_30", &lv_font_montserrat_30 },
#endif
#if LV_FONT_MONTSERRAT_32
    { "MONTSERRAT_32", &lv_font_montserrat_32 },
#endif
#if LV_FONT_MONTSERRAT_34
    { "MONTSERRAT_34", &lv_font_montserrat_34 },
#endif
#if LV_FONT_MONTSERRAT_36
    { "MONTSERRAT_36", &lv_font_montserrat_36 },
#endif
#if LV_FONT_MONTSERRAT_38
    { "MONTSERRAT_38", &lv_font_montserrat_38 },
#endif
#if LV_FONT_MONTSERRAT_40
    { "MONTSERRAT_40", &lv_font_montserrat_40 },
#endif
#if LV_FONT_MONTSERRAT_42
    { "MONTSERRAT_42", &lv_font_montserrat_42 },
#endif
#if LV_FONT_MONTSERRAT_44
    { "MONTSERRAT_44", &lv_font_montserrat_44 },
#endif
#if LV_FONT_MONTSERRAT_46
    { "MONTSERRAT_46", &lv_font_montserrat_46 },
#endif
#if LV_FONT_MONTSERRAT_48
    { "MONTSERRAT_48", &lv_font_montserrat_48 },
#endif
};

//
// Color themes
//

uint32_t active_theme_index = 0;

//
//
//

void create_screens() {

// Set default LVGL theme
    lv_disp_t *dispp = lv_disp_get_default();
    lv_theme_t *theme = lv_theme_default_init(dispp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), false, LV_FONT_DEFAULT);
    lv_disp_set_theme(dispp, theme);
    
    // Initialize screens
    // Create screens
    create_screen_home();
    create_screen_menu();
    create_screen_ai_call();
    create_screen_settings();
    create_screen_server();
    create_screen_led();
    create_screen_wi_fi_setup();
    create_screen_mpu_data();
}
