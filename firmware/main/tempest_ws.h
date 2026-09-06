#pragma once

#include <stdbool.h>
#include "esp_err.h"

esp_err_t tempest_ws_start(void);

/* One check: open the WeatherFlow WebSocket if UDP has gone stale,
 * close it once broadcast returns. Called from the 30 s health loop
 * so we do not keep an 8 KB task around for a fallback that is idle. */
void tempest_ws_poll(void);

/* True while the WebSocket link is carrying observations. */
bool tempest_ws_is_active(void);
