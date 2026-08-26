#pragma once
/*
 * Tempest REST forecast poller.
 *
 * The only reason this exists: the local UDP feed carries raw sensor readings
 * and nothing else. Forecast, condition text, and sunrise/sunset come from
 * better_forecast. Everything live still comes from UDP.
 *
 * Requires a personal access token in secrets.h.
 */

#include "esp_err.h"

/* Starts a task that polls every CONFIG_TEMPEST_FORECAST_INTERVAL_S seconds
 * and backs off to 30 minutes on any non-200. */
esp_err_t tempest_rest_start(void);

/* One-shot: discovers the Tempest device_id, pulls the last 24 h of obs_st in
 * 6-hour windows, and seeds history.c so the trend graphs are populated at
 * boot instead of filling over the following day. Safe to fail -- the graphs
 * simply start empty, which is the old behaviour. */
esp_err_t tempest_rest_backfill_history(void);

/* Fetch once, synchronously. Useful at boot so the screen is not empty while
 * waiting out the first poll interval. */
esp_err_t tempest_rest_fetch_now(void);
