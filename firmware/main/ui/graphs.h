#pragma once
/*
 * 24-hour trend graphs.
 *
 * A third LVGL screen, alongside the main panel and settings. Four charts in a
 * 2x2 grid: temperature, pressure, wind (average plus gust), humidity.
 *
 * Data comes from history.c, which is fed by the local UDP feed -- so these
 * graphs keep working with the internet down, and they fill in over the first
 * 24 hours from a cold boot rather than being backfilled from the cloud. See
 * the note in docs/roadmap.md about REST backfill.
 *
 * All entry points assume the LVGL lock is already held.
 */

#include <stdbool.h>
#include "esp_err.h"
#include "lvgl.h"

esp_err_t graphs_init(void);
void      graphs_show(void);
void      graphs_hide(void);

/* Redraws from the history buffer. Returns immediately when not visible.
 * Called from the 1 Hz tick, but only actually redraws every 30 s -- the
 * underlying buckets are 5 minutes wide, so faster is wasted work. */
void graphs_tick(void);

bool graphs_is_visible(void);

/* Force a redraw on the next tick (e.g. after REST history backfill). */
void graphs_request_redraw(void);
