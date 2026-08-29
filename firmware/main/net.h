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
int8_t net_get_rssi(void);

/* One scanned access point. */
typedef struct {
    char    ssid[33];
    int8_t  rssi;
    bool    secure;
} net_ap_t;

/* Blocking active scan, roughly 2 seconds. Returns how many entries were
 * written, or negative on error. Must NOT be called from the LVGL task -- it
 * blocks far longer than a frame. */
int net_scan(net_ap_t *out, int max_aps);

/* Stores the credentials and reconnects with them. Safe to call while already
 * connected; the existing association is dropped first. */
esp_err_t net_apply_credentials(const char *ssid, const char *password);

/* SSID currently configured, or "" if none. Never returns NULL. */
const char *net_current_ssid(void);

/* True once SNTP has set a plausible wall clock. Sunrise/sunset rendering and
 * staleness checks are meaningless before this. */
bool net_time_is_valid(void);
void net_mark_time_valid(void);
void net_set_timezone(int tz_idx);

/* Writes the STA IPv4 address as dotted decimal. Returns false when Wi-Fi
 * is down or DHCP has not assigned an address yet. */
bool net_get_ip(char *buf, size_t len);
