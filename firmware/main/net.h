#pragma once
/*
 * Wi-Fi and time.
 *
 * On the ESP32-P4 there is no radio. These are the ordinary esp_wifi_* APIs,
 * but every call is marshalled over SDIO to the ESP32-C6 by esp_wifi_remote /
 * ESP-Hosted. If net_start() fails at the very first esp_wifi_init(), the
 * problem is almost always a version mismatch between the esp_hosted component
 * and the slave firmware on the C6 -- not this code.
 */

#include <stdbool.h>
#include "esp_err.h"

/* Brings up netif, event loop, Wi-Fi, and starts SNTP once an IP is acquired.
 * Returns once the station has associated, or ESP_ERR_TIMEOUT. */
esp_err_t net_start(void);

bool net_is_connected(void);

/* True once SNTP has set a plausible wall clock. Sunrise/sunset rendering and
 * staleness checks are meaningless before this. */
bool net_time_is_valid(void);
