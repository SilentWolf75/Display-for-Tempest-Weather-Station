#pragma once
/*
 * 24-hour observation history, for the trend graphs.
 *
 * Held on-device in PSRAM rather than fetched per-repaint: the source is the
 * local UDP feed, so the graphs keep working when the internet does not, which
 * is the same rule the rest of the outdoor half follows.
 *
 * Samples are bucketed to 5 minutes (288 buckets for 24 h). obs_st arrives
 * once a minute, so each bucket averages five readings -- which is also what
 * makes the lines readable at 480 px wide instead of a noisy 1440-point mess.
 *
 * Rain is summed within a bucket rather than averaged; everything else is a
 * mean, and gust is a maximum. Averaging a gust would defeat the point of it.
 *
 * Cost: 288 buckets x ~44 bytes = ~13 KB.
 */

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "wx_state.h"

#define HIST_BUCKETS    288
#define HIST_BUCKET_S   300

typedef enum {
    HIST_TEMP = 0,
    HIST_PRESSURE,
    HIST_WIND,
    HIST_HUMIDITY,
    HIST_RAIN,
    HIST_SERIES_COUNT,
} hist_series_t;

esp_err_t history_init(void);

/* Folds one observation into the current bucket. Called from wx_state.c on
 * every obs_st, so it must be cheap and must not take the state lock. */
void history_add(int64_t epoch, float temp_c, float humidity_pct,
                 float pressure_mb, float wind_avg_ms, float wind_gust_ms,
                 float rain_mm, float uv);

/* Copies a series into `out`, oldest first, in SI units. Buckets with no data
 * are filled with LV_CHART_POINT_NONE's float equivalent (NAN) so the chart can
 * show gaps rather than drawing a line through missing time.
 *
 * Returns the number of buckets that actually hold data. `min_out`/`max_out`
 * receive the observed range, ignoring gaps -- pass NULL to skip. */
int history_get(hist_series_t series, float *out, int out_len,
                float *min_out, float *max_out);

/* Secondary line for a series: gust for HIST_WIND, nothing for the others.
 * Returns false when the series has no secondary. */
bool history_get_secondary(hist_series_t series, float *out, int out_len);

/* True once at least two buckets hold data -- below that a graph is a dot. */
bool history_is_plottable(void);

/* Wipe the ring and accept ascending backfill samples (REST history seed).
 * Must be paired with history_end_backfill(). */
void history_begin_backfill(void);
void history_end_backfill(void);

/* Oldest and newest bucket timestamps, for axis labels. */
void history_span(int64_t *oldest, int64_t *newest);
