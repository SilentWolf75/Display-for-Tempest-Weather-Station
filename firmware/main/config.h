#pragma once
/*
 * Runtime settings, persisted in NVS.
 *
 * Units used to be a compile-time Kconfig choice. They are runtime now because
 * a settings screen that requires a reflash is not a settings screen. The
 * conversion helpers below keep the same names the UI already used, so call
 * sites did not change -- only what the macros expand to.
 *
 * wx_state still stores SI exclusively. These convert at render time.
 */

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    CFG_UNITS_IMPERIAL = 0,
    CFG_UNITS_METRIC   = 1,
} cfg_units_t;

typedef struct {
    cfg_units_t units;
    uint8_t     brightness_day;      /* 5..100 */
    uint8_t     brightness_night;    /* 0..100, 0 = backlight off */
    uint8_t     night_start_hour;    /* local hour dimming begins */
    uint8_t     night_end_hour;      /* local hour full brightness resumes */
    bool        night_dim_enabled;
    bool        animate_forecast;    /* animate all 7 icons, not just header */
    uint8_t     wind_scale_max_ms;   /* full-scale on the wind ring */
} cfg_t;

esp_err_t cfg_init(void);

/* Snapshot. Cheap; copies a small struct under a lock. */
void cfg_get(cfg_t *out);

/* Applies and persists. Only fields that actually changed are written to NVS,
 * so dragging a slider does not burn flash on every pixel. */
esp_err_t cfg_set(const cfg_t *in);

/* Restores defaults and persists them. */
esp_err_t cfg_reset(void);

/* True when the local hour falls inside the configured night window.
 * Handles windows that wrap past midnight, which is the normal case. */
bool cfg_is_night(int local_hour);

/* ---- unit conversion, reading the live setting ----
 * The macro names match what ui.c already used, so switching from the old
 * compile-time Kconfig choice was a one-block change. */
float       cfg_temp(float celsius);
const char *cfg_temp_suffix(void);
float       cfg_wind(float ms);
const char *cfg_wind_suffix(void);
float       cfg_pressure(float mb);
const char *cfg_pressure_suffix(void);
const char *cfg_pressure_fmt(void);
float       cfg_rain(float mm);
const char *cfg_rain_suffix(void);
const char *cfg_rain_fmt(void);
float       cfg_distance(float km);
const char *cfg_distance_suffix(void);
