#pragma once
/*
 * Over-the-air updates over the LAN.
 *
 * Serves a small page at http://<device-ip>/ with a firmware upload form, and
 * accepts a POST of a raw .bin at /update. On success it sets the new slot as
 * the boot partition and restarts.
 *
 * Rollback: CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE is on, so a freshly written
 * image boots as PENDING_VERIFY and is reverted on the next reset unless
 * ota_mark_valid() is called. main.c calls it once the display and the data
 * feeds have come up, which means a firmware that cannot render or cannot talk
 * to the station undoes itself rather than stranding a wall-mounted panel.
 *
 * ############################################################################
 * #  SECURITY                                                                #
 * #  This accepts firmware from anything that can reach the device. Set      #
 * #  CONFIG_OTA_PASSWORD to require a shared secret; leaving it empty means  #
 * #  anyone on your LAN can reflash the panel. It is logged loudly at boot   #
 * #  when unset. There is no TLS -- the upload crosses the LAN in the clear. #
 * ############################################################################
 */

#include <stdbool.h>
#include "esp_err.h"

/* Starts the HTTP server. Call once the network is up. */
esp_err_t ota_start(void);

/* Marks the running image good so rollback does not revert it. Safe to call
 * repeatedly; only the first call after an update does anything. */
void ota_mark_valid(void);

/* True while an upload is in progress -- the UI dims and stops animating so
 * the write is not competing with the vector renderer. */
bool ota_in_progress(void);
