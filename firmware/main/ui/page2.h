#pragma once
/*
 * Sky page: insight chips, lunar arc / moon phase, sun times, and 24-hour hourly.
 * Night standby overlay is still owned here.
 */

#include "esp_err.h"
#include <stdbool.h>

esp_err_t page2_init(void);
void      page2_show(void);
void      page2_hide(void);
void      page2_tick(void);
bool      page2_is_visible(void);

/* True when night standby clock is covering the panel. */
bool      page2_night_standby_active(void);
