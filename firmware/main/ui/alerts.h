#pragma once
/*
 * Alerts page: full NWS text, lightning, and rain context.
 * Overlay on the one LVGL screen, never lv_screen_load().
 */

#include "esp_err.h"
#include <stdbool.h>

esp_err_t alerts_init(void);
void      alerts_show(void);
void      alerts_hide(void);
void      alerts_tick(void);
bool      alerts_is_visible(void);
