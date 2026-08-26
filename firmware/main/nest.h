#pragma once
/*
 * Google Nest thermostat via the Smart Device Management (SDM) API.
 *
 * Supplies the indoor half of the display: ambient temperature, humidity,
 * setpoint, and live HVAC state. The Tempest is an outdoor station and has no
 * indoor channel, so this is the only source for those tiles.
 *
 * IMPORTANT: unlike the Tempest, this is NOT local. Modern Nest hardware
 * exposes no local API, so every reading is a cloud round-trip over TLS. When
 * the internet is down the indoor tiles go stale while the outdoor half keeps
 * working from UDP. The UI must show that difference rather than hiding it.
 *
 * Auth is OAuth 2.0 refresh-token flow:
 *   - You authorise ONCE in a browser on a PC and keep the refresh token.
 *   - The device exchanges that refresh token for a 1-hour access token
 *     whenever the current one is close to expiring.
 *   - The refresh token itself does not expire unless revoked (password
 *     change, access withdrawn in the Google account, or project deletion).
 *
 * See docs/nest-api.md for the one-time setup, including the $5 Device Access
 * registration fee.
 */

#include <stdbool.h>
#include "esp_err.h"

/* Starts the poll task. Safe to call before the network is up -- it backs off
 * and retries. */
esp_err_t nest_start(void);

/* One synchronous poll. Refreshes the access token first if needed. */
esp_err_t nest_fetch_now(void);

/* True once at least one poll has succeeded. */
bool nest_is_configured(void);
