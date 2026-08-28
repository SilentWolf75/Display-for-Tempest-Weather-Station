#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool     active;
    char     id[64];
    char     event[64];       /* e.g. "Tornado Warning" */
    char     headline[128];
    char     severity[16];    /* "Extreme", "Severe", "Moderate" */
    int64_t  expires_epoch;
    bool     sound_played;
} nws_alert_t;

/**
 * Start the background NWS weather alerts poller.
 */
esp_err_t nws_alerts_start(void);

/**
 * Trigger an immediate refresh of weather alerts.
 */
void nws_alerts_refresh(void);

/**
 * Get current active emergency alert info (thread-safe).
 */
bool nws_alerts_get_active(nws_alert_t *out_alert);

#ifdef __cplusplus
}
#endif
