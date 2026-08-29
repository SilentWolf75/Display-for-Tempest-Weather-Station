#pragma once

#include <stdbool.h>
#include "esp_err.h"

esp_err_t tempest_ws_start(void);

/* True while the WebSocket link is carrying observations. */
bool tempest_ws_is_active(void);
