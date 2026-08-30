#pragma once
/*
 * Over-the-air updates over the LAN.
 *
 * Handlers mount on the web dashboard server at /ota (form) and /ota/update
 * (raw .bin POST). On success the new slot becomes boot and the device
 * restarts.
 *
 * Rollback: CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE is on, so a freshly written
 * image boots as PENDING_VERIFY and is reverted on the next reset unless
 * ota_mark_valid() is called. main.c calls it once the display and the data
 * feeds have come up.
 *
 * ############################################################################
 * #  SECURITY                                                                #
 * #  Uploads accept firmware from anything on the LAN. Set                   #
 * #  CONFIG_OTA_PASSWORD to require X-OTA-Password; empty means anyone on    #
 * #  your network can reflash. There is no TLS.                              #
 * ############################################################################
 */

#include <stdbool.h>
#include "esp_err.h"
#include "esp_http_server.h"

/* Mount OTA routes on an existing HTTP server (web dashboard, port 8080). */
esp_err_t ota_register(httpd_handle_t server);

/* Standalone server on port 80 when the web dashboard is disabled. */
esp_err_t ota_start(void);

/* Stop a standalone OTA server we own. No-op if mounted on the dashboard. */
void ota_stop(void);

/* Dashboard httpd was stopped; drop our handle so ota_start() can take over. */
void ota_detach(void);

/* True when no password is configured, or the request carries it
 * (X-OTA-Password header or ?p= query). Shared with /api/chime and /api/logs. */
bool ota_password_ok(httpd_req_t *req);

void ota_mark_valid(void);

bool ota_in_progress(void);
