#pragma once
/*
 * Wi-Fi setup screen: scan, pick a network, type the password, connect.
 *
 * Exists so the panel never has to come off the wall to change networks. The
 * credentials land in NVS via cfg_set_wifi(), so they survive a reflash --
 * secrets.h only ever seeds a blank config.
 *
 * Threading: scanning blocks for ~2 s, which would freeze the UI if it ran in
 * the LVGL task. It runs in its own task and takes the LVGL lock only to
 * publish results.
 *
 * All entry points other than the internal task assume the LVGL lock is held.
 */

#include <stdbool.h>
#include "esp_err.h"
#include "lvgl.h"

esp_err_t wifi_setup_init(void);
void      wifi_setup_show(void);
void      wifi_setup_hide(void);
bool      wifi_setup_is_visible(void);

/* Refreshes the connection status line. Call from the 1 Hz tick. */
void      wifi_setup_tick(void);
