#pragma once
/*
 * Second screen: moon/night mode, lightning proximity, rain periods,
 * hourly timeline, and AQI detail.
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
