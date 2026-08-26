#pragma once
/*
 * Hand-written LVGL layout for 1024x600 landscape.
 *
 * Kept in code rather than SquareLine while the layout is still moving --
 * a diff of a C file is reviewable, a diff of exported SquareLine output is
 * not. If the layout stops being comfortable to express here, switch: create a
 * generic 1024x600 16-bit SquareLine project, export into this directory, and
 * call ui_init() the same way. See docs/hardware.md.
 *
 * The caller owns locking: ui_init() and ui_tick() both assume the LVGL mutex
 * is already held.
 */

#include "esp_err.h"

/* Builds the screen. Call with the LVGL lock held. */
esp_err_t ui_init(void);

/* Repaints from the current wx_state snapshot. Call with the LVGL lock held,
 * roughly once a second -- rapid_wind only lands every 3 s, so faster gains
 * nothing but the clock wants a per-second tick. */
void ui_tick(void);
