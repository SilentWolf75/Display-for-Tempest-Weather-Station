#pragma once
/*
 * Week page: a readable 7-day outlook. Text only -- no Lottie at init.
 * Overlay on the one LVGL screen, never lv_screen_load().
 */

#include "esp_err.h"
#include <stdbool.h>

esp_err_t week_init(void);
void      week_show(void);
void      week_hide(void);
void      week_tick(void);
bool      week_is_visible(void);
