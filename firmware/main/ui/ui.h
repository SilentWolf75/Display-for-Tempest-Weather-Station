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
#include "lvgl.h"

typedef enum {
    UI_PAGE_DASHBOARD = 0,
    UI_PAGE_INSIGHTS,
    UI_PAGE_GRAPHS,
    UI_PAGE_COUNT,
} ui_page_t;

/* Builds the screen. Call with the LVGL lock held. */
esp_err_t ui_init(void);

/* Cycle dashboard -> insights -> graphs -> dashboard. */
void ui_page_next(void);
void ui_page_prev(void);
void ui_page_goto(ui_page_t page);

/* Horizontal swipe left/right to change pages. */
void ui_attach_swipe_nav(lv_obj_t *screen);

/* Standard header page button; returns the label inside. */
lv_obj_t *ui_create_page_button(lv_obj_t *parent, lv_align_t align, int x_ofs, int y_ofs,
                                ui_page_t page);

/* Repaints from the current wx_state snapshot. Call with the LVGL lock held,
 * roughly once a second -- rapid_wind only lands every 3 s, so faster gains
 * nothing but the clock wants a per-second tick. */
void ui_tick(void);
