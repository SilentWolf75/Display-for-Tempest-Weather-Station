#pragma once

#include "esp_err.h"

esp_err_t mqtt_app_start(void);
void mqtt_app_stop(void);
void mqtt_app_reconnect(void);
