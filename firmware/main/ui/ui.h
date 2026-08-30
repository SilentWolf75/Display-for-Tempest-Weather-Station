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

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "lvgl.h"

typedef enum {
    UI_PAGE_DASHBOARD = 0,
    UI_PAGE_INSIGHTS,
    UI_PAGE_WEEK,
    UI_PAGE_GRAPHS,
    UI_PAGE_ALERTS,
    UI_PAGE_COUNT,
} ui_page_t;

/* Bottom news ticker lives on lv_layer_top so every page (and Settings)
 * can leave this strip free. Overlays should be UI_CONTENT_H tall. */
#define UI_SCR_W        1024
#define UI_SCR_H        600
#define UI_TICKER_H     36
#define UI_CONTENT_H    (UI_SCR_H - UI_TICKER_H)

/* Builds the screen. Call with the LVGL lock held. */
esp_err_t ui_init(void);

/* The one LVGL screen — page overlays are children of this, never separate screens. */
lv_obj_t *ui_main_screen(void);

/* Cycle LIVE -> SKY -> WEEK -> 24H -> ALERTS -> LIVE. */
void ui_page_next(void);
void ui_page_prev(void);
void ui_page_goto(ui_page_t page);

/* Horizontal swipe left/right to change pages. */
void ui_attach_swipe_nav(lv_obj_t *screen);

/* Standard header page button; returns the label inside. */
lv_obj_t *ui_create_page_button(lv_obj_t *parent, lv_align_t align, int x_ofs, int y_ofs,
                                ui_page_t page);

/* Settings gear. Same look as the LIVE header control. */
void ui_create_gear_button(lv_obj_t *parent, lv_align_t align, int x_ofs, int y_ofs);

/* Keep the ticker above Settings / Wi-Fi after those overlays foreground. */
void ui_ticker_raise(void);

/* One pill per page. `current` is the wide lit one. */
void ui_create_page_dots(lv_obj_t *parent, int x, int y, ui_page_t current);

/* "LIVE" / "SKY" / "WEEK" / "24H" / "ALERTS" */
const char *ui_page_name(ui_page_t page);

/* Skip the LVGL invalidate when the string has not changed. Most ticks
 * rewrite the same clock / battery / forecast labels; that was the
 * biggest source of the panel feeling slower than it used to. */
void ui_label_set(lv_obj_t *lbl, const char *text);
void ui_label_setf(lv_obj_t *lbl, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/* Compact age: "12s", "3m", "2h". Negative or unknown → "--". */
void ui_fmt_age(char *buf, size_t n, int64_t age_s);

/* "2:41 PM" or "2:41:08 PM". Leading zero stripped. */
void ui_fmt_clock(char *buf, size_t n, int64_t epoch, bool seconds);

/* Repaints from the current wx_state snapshot. Call with the LVGL lock held,
 * roughly once a second -- rapid_wind only lands every 3 s, so faster gains
 * nothing but the clock wants a per-second tick. */
void ui_tick(void);

/* Call on touch or button press so the screensaver backs off. */
void ui_note_user_activity(void);

/* Rebuild forecast-strip icons after animate_forecast changes in settings. */
void ui_forecast_mode_changed(void);

/* Call after the post-SDIO flush. Icon promotion and the brightness grace
 * period start when the dashboard is actually on glass, not during ui_init. */
void ui_mark_panel_visible(void);

/* Called when REST/Open-Meteo publishes a new forecast snapshot. */
void ui_notify_forecast_updated(void);
