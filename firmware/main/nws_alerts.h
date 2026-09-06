#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool     active;
    char     id[64];
    char     event[64];       /* e.g. "Tornado Warning" */
    char     headline[256];
    char     instruction[192];
    char     severity[16];    /* "Extreme", "Severe", "Moderate" */
    int64_t  expires_epoch;   /* `ends`, else `expires` */
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
bool nws_alerts_is_current(void);

/**
 * Get current active emergency alert info (thread-safe).
 */
bool nws_alerts_get_active(nws_alert_t *out_alert);

/* Official NWS /points + /forecast for the zip. Not radar imagery —
 * api.weather.gov does not serve display tiles. */
typedef struct {
    bool     valid;
    char     office[8];      /* WFO, e.g. TOP */
    char     radar[8];       /* nearest NEXRAD, e.g. EAX */
    char     city[32];
    char     state[4];
    char     period[24];     /* "This Afternoon" */
    char     short_fc[96];
    char     detailed[240];
    int      temp_f;
    int      pop;
    int64_t  fetched_epoch;
} nws_forecast_t;

bool nws_forecast_get(nws_forecast_t *out);

/* "7 PM", "7 PM tomorrow", "7 PM Mon", or empty if unknown. */
void nws_format_until(int64_t ends_epoch, char *buf, size_t n);

/* "Heat Advisory until 7 PM" — event only if the end time is unknown. */
void nws_format_banner(const nws_alert_t *alert, char *buf, size_t n);

/* Event, until-time, and instruction, repeated for a circular crawl. */
void nws_format_ticker(const nws_alert_t *alert, char *buf, size_t n);

#ifdef __cplusplus
}
#endif
