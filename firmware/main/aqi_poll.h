#pragma once

#include "esp_err.h"

esp_err_t aqi_poll_start(void);
void aqi_poll_refresh(void);
