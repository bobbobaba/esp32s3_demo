#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Screens

enum ScreensEnum {
    _SCREEN_ID_FIRST = 1,
    SCREEN_ID_HOME = 1,
    SCREEN_ID_MENU = 2,
    SCREEN_ID_AI_CALL = 3,
    SCREEN_ID_SETTINGS = 4,
    SCREEN_ID_SERVER = 5,
    SCREEN_ID_LED = 6,
    SCREEN_ID_WI_FI_SETUP = 7,
    SCREEN_ID_MPU_DATA = 8,
    _SCREEN_ID_LAST = 8
};

typedef struct _objects_t {
    lv_obj_t *home;
    lv_obj_t *menu;
    lv_obj_t *ai_call;
    lv_obj_t *settings;
    lv_obj_t *server;
    lv_obj_t *led;
    lv_obj_t *wi_fi_setup;
    lv_obj_t *mpu_data;
    lv_obj_t *home_time;
    lv_obj_t *home_temp;
    lv_obj_t *home_cpu;
    lv_obj_t *home_mem;
    lv_obj_t *menu_rows[4];
    lv_obj_t *ai_status;
    lv_obj_t *ai_you;
    lv_obj_t *ai_reply;
    lv_obj_t *settings_volume;
    lv_obj_t *settings_ota;
    lv_obj_t *settings_wifi;
    lv_obj_t *server_uptime;
    lv_obj_t *server_load;
    lv_obj_t *server_mem;
    lv_obj_t *server_disk;
    lv_obj_t *led_mode;
    lv_obj_t *wifi_status;
    lv_obj_t *mpu_acc;
    lv_obj_t *mpu_gyro;
} objects_t;

extern objects_t objects;

void create_screen_home();
void tick_screen_home();

void create_screen_menu();
void tick_screen_menu();

void create_screen_ai_call();
void tick_screen_ai_call();

void create_screen_settings();
void tick_screen_settings();

void create_screen_server();
void tick_screen_server();

void create_screen_led();
void tick_screen_led();

void create_screen_wi_fi_setup();
void tick_screen_wi_fi_setup();

void create_screen_mpu_data();
void tick_screen_mpu_data();

void tick_screen_by_id(enum ScreensEnum screenId);
void tick_screen(int screen_index);

void create_screens();

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/
