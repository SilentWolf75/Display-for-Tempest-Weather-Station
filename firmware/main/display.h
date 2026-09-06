#pragma once
/*
 * Panel, touch, and LVGL bring-up for the CrowPanel Advance 10.1".
 *
 * EK79007 over MIPI-DSI + GT911 over I2C, both handed to esp_lvgl_port which
 * owns the LVGL task and tick.
 *
 * ###########################################################################
 * #  Every LVGL call made outside an LVGL callback MUST be wrapped in       #
 * #  display_lock() / display_unlock(). This is the documented failure mode #
 * #  on this board -- unlocked calls from a data task corrupt the display   #
 * #  list and the symptom is a hang, not a crash.                           #
 * ###########################################################################
 */

#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "lvgl.h"

esp_err_t display_init(void);

/* Take the LVGL mutex. timeout_ms < 0 waits forever. */
bool display_lock(int timeout_ms);
void display_unlock(void);

/* 0-100. No-op until the backlight pin in board_pins.h is verified. */
void display_set_brightness(int percent);

/* Last value passed to display_set_brightness() (0-100). */
uint8_t display_get_brightness(void);

/* Invalidate the active screen and poke the LVGL task to flush now. */
void display_refresh_now(void);

/* Thread-safe: queue a boot repaint or an indoor I2C reset. Ordinary
 * network completion does not require a display reset. reason is a literal. */
void display_recover_after_sdio(const char *reason);

/* LVGL-thread only (lock already held). Returns true only when a repaint
 * was requested; an I2C-only reset does not invalidate the screen. */
bool display_apply_recover_request(void);

/* One TLS session at a time (recursive on the same task). Indoor I2C
 * and Lottie stand down while busy. */
void display_https_begin(void);
void display_https_end(void);
bool display_https_busy(void);

/* GT911 shares I2C with the indoor sensor. After a long run the bus can
 * wedge; LVGL already skips the failed poll. This counts failures and
 * queues an I2C-only recover on the LVGL thread. */
void display_note_touch_io(esp_err_t err);

/* The I2C bus the touch controller sits on. Shared with the Grove header,
 * so the indoor sensor attaches to this rather than creating a second
 * master on the same two pins. NULL if display_init() has not run or the
 * bus could not be created. */
i2c_master_bus_handle_t display_get_i2c_bus(void);

/* Log render progress, LVGL lock responsiveness and internal heap margins. */
void display_log_health(void);
