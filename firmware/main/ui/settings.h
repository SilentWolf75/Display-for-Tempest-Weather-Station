#pragma once
/*
 * Settings screen.
 *
 * A second LVGL screen rather than an overlay, so the main panel keeps its
 * widget tree intact and returning is instant with no rebuild.
 *
 * Every control writes through cfg_set() immediately -- there is no OK/Cancel.
 * On a wall panel you want to see brightness change as you drag the slider,
 * and cfg_set() only touches NVS when a value actually differs, so this does
 * not thrash the flash.
 *
 * All entry points assume the LVGL lock is already held.
 */

#include "esp_err.h"
#include "lvgl.h"

/* Builds the screen once. Call after ui_init(). */
esp_err_t settings_init(void);

/* Switches to the settings screen / back to the main panel. */
void settings_show(void);
void settings_hide(void);

/* Refreshes the live diagnostics block. Call from the 1 Hz tick; returns
 * immediately when the screen is not visible. */
void settings_tick(void);

/* True while the settings screen is the active one. */
bool settings_is_visible(void);
