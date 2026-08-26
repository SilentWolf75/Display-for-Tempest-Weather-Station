#pragma once
/*
 * Mutex-protected snapshot of everything the display knows about the weather.
 *
 * Invariant: this struct holds SI units, exactly as the Tempest UDP feed
 * delivers them. No conversion happens on ingest. The UI converts at the point
 * of rendering, which is what makes a units toggle a one-line change instead of
 * a refactor.
 *
 * Producers: tempest_udp.c (live), tempest_rest.c (forecast).
 * Consumer:  ui.c, via wx_snapshot().
 */

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define WX_FORECAST_DAYS   7
#define WX_COND_STR_LEN    32

/* Barometric tendency over 3 hours, by the conventional mb thresholds. */
typedef enum {
    WX_TREND_FALLING_FAST = -2,
    WX_TREND_FALLING      = -1,
    WX_TREND_STEADY       =  0,
    WX_TREND_RISING       =  1,
    WX_TREND_RISING_FAST  =  2,
} wx_trend_t;

typedef enum {
    WX_PRECIP_NONE = 0,
    WX_PRECIP_RAIN = 1,
    WX_PRECIP_HAIL = 2,
    WX_PRECIP_RAIN_HAIL = 3,
} wx_precip_type_t;

/* One day of forecast, from the REST better_forecast endpoint. */
typedef struct {
    int64_t day_start_local;
    float   air_temp_high_c;
    float   air_temp_low_c;
    int     precip_probability;         /* percent */
    char    conditions[WX_COND_STR_LEN];
    char    icon[WX_COND_STR_LEN];      /* WeatherFlow icon slug */
} wx_forecast_day_t;

typedef struct {
    /* ---- from obs_st, every 60 s ---- */
    int64_t  obs_epoch;
    float    wind_lull_ms;
    float    wind_avg_ms;
    float    wind_gust_ms;
    int      wind_dir_deg;
    int      wind_sample_interval_s;
    float    pressure_mb;               /* station pressure, not sea level */
    float    air_temp_c;
    float    humidity_pct;
    uint32_t illuminance_lux;
    float    uv_index;
    float    solar_radiation_wm2;
    float    rain_last_min_mm;
    wx_precip_type_t precip_type;
    float    lightning_avg_dist_km;
    int      lightning_count;
    float    battery_v;
    int      report_interval_min;
    bool     obs_valid;

    /* ---- from rapid_wind, every 3 s ---- */
    int64_t  rapid_epoch;
    float    rapid_wind_ms;
    int      rapid_wind_dir_deg;
    bool     rapid_valid;

    /* ---- events ---- */
    int64_t  last_strike_epoch;
    float    last_strike_dist_km;
    uint32_t last_strike_energy;
    int64_t  last_precip_epoch;

    /* ---- hub / device health ---- */
    int      hub_rssi;
    int      device_rssi;
    uint32_t hub_uptime_s;
    uint32_t sensor_status;             /* 0 == all sensors healthy */

    /* ---- derived on device from the raw fields above ----
     * These exist because the UDP feed carries instantaneous sensor readings
     * only. Anything cumulative or historical has to be accumulated here. */
    float    dew_point_c;
    float    feels_like_c;

    /* Rain since local midnight, accumulated from the per-minute field.
     * rain_last_min_mm alone is useless on a display: it reads 0.00 through
     * most of a steady drizzle. */
    float    rain_today_mm;
    float    rain_rate_mm_hr;

    /* Observed extremes since local midnight -- what actually happened, as
     * opposed to what the forecast predicted. */
    float    temp_high_today_c;
    float    temp_low_today_c;
    bool     daily_valid;

    /* Real 3-hour barometric tendency from a sample history. NOT the
     * sea-level-minus-station difference, which is a fixed altitude offset. */
    float    pressure_trend_mb_3h;
    wx_trend_t pressure_trend;

    /* Strikes within the last 3 hours, from a timestamp ring buffer. The
     * obs_st lightning_count field is per-report-interval only. */
    int      strikes_3h;

    /* ---- indoor, from the Nest thermostat via the SDM API ----
     * Cloud-sourced, unlike everything above it. When the internet drops these
     * go stale while the outdoor values keep updating from UDP, so the UI must
     * age them independently. */
    float    indoor_temp_c;
    float    indoor_humidity_pct;
    char     hvac_status[16];        /* OFF / HEATING / COOLING */
    char     thermostat_mode[16];    /* HEAT / COOL / HEATCOOL / OFF */
    float    setpoint_heat_c;
    float    setpoint_cool_c;
    bool     eco_mode;
    int64_t  indoor_fetched_epoch;
    bool     indoor_valid;
    /* Set when Google rejects the refresh token outright (HTTP 400/401).
     * Distinct from merely stale: this never recovers on its own, so the
     * UI must say 'reauthorize' rather than show an age that keeps
     * climbing. The usual cause is an app left in Testing publishing
     * status, whose refresh tokens Google expires after 7 days. */
    bool     indoor_auth_failed;

    /* ---- from REST better_forecast ---- */
    char     current_conditions[WX_COND_STR_LEN];
    char     current_icon[WX_COND_STR_LEN];
    int64_t  sunrise_epoch;
    int64_t  sunset_epoch;
    wx_forecast_day_t forecast[WX_FORECAST_DAYS];
    int      forecast_days;
    int64_t  forecast_fetched_epoch;
    bool     forecast_valid;

    /* ---- link health ---- */
    bool     wifi_connected;
    int64_t  last_udp_epoch;            /* any datagram, for liveness */
    uint32_t udp_packets_total;
} wx_state_t;

/* Call once, before any producer or consumer task starts. */
esp_err_t wx_state_init(void);

/* Copy the whole state out under the lock. The UI must use this rather than
 * holding a pointer — a snapshot cannot tear halfway through a repaint. */
void wx_snapshot(wx_state_t *out);

/* Producer-side mutation. Each takes the lock internally. */
void wx_update_obs_st(const wx_state_t *partial);
void wx_update_rapid_wind(int64_t epoch, float speed_ms, int dir_deg);
void wx_update_strike(int64_t epoch, float dist_km, uint32_t energy);
void wx_update_precip_start(int64_t epoch);
void wx_update_hub_status(int rssi, uint32_t uptime_s);
void wx_update_device_status(int rssi, float voltage, uint32_t sensor_status);
void wx_update_forecast(const wx_state_t *partial);
void wx_update_indoor(const wx_state_t *partial);
void wx_set_indoor_auth_failed(bool failed);
void wx_set_wifi_connected(bool connected);
void wx_note_udp_packet(void);

/* True if no obs_st has landed within CONFIG_TEMPEST_OBS_STALE_S. */
bool wx_obs_is_stale(const wx_state_t *s);

/* True if the Nest reading is older than CONFIG_NEST_STALE_S. Separate from
 * wx_obs_is_stale because the two feeds fail independently. */
bool wx_indoor_is_stale(const wx_state_t *s);

/* ---- pure helpers, no locking, safe anywhere ---- */
float wx_dew_point_c(float temp_c, float humidity_pct);
float wx_feels_like_c(float temp_c, float humidity_pct, float wind_ms);
const char *wx_compass_point(int degrees);

/* "Low" / "Moderate" / "High" / "Very High" / "Extreme", per the WHO scale. */
const char *wx_uv_description(float uv_index);

/* "rising rapidly", "steady", ... for the pressure card. */
const char *wx_trend_description(wx_trend_t trend);

/* Unit conversion — presentation layer only. */
static inline float wx_c_to_f(float c)       { return c * 9.0f / 5.0f + 32.0f; }
static inline float wx_ms_to_mph(float ms)   { return ms * 2.236936f; }
static inline float wx_ms_to_kmh(float ms)   { return ms * 3.6f; }
static inline float wx_mb_to_inhg(float mb)  { return mb * 0.0295299830714f; }
static inline float wx_mm_to_in(float mm)    { return mm / 25.4f; }
static inline float wx_km_to_mi(float km)    { return km * 0.621371f; }
