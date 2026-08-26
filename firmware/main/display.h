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
#include "lvgl.h"

esp_err_t display_init(void);

/* Take the LVGL mutex. timeout_ms < 0 waits forever. */
bool display_lock(int timeout_ms);
void display_unlock(void);

/* 0-100. No-op until the backlight pin in board_pins.h is verified. */
void display_set_brightness(int percent);
