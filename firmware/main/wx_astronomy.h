#pragma once
/*
 * Moon phase and lunar schedule helpers.
 *
 * Phase fraction and name are computed locally from the clock. Moonrise,
 * moonset, and hourly forecast slots are filled by tempest_rest from
 * Open-Meteo using the station zip code.
 */

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    float    illumination;     /* 0..1 lit fraction */
    float    age_days;           /* days since new moon */
    char     phase_name[32];     /* e.g. "Waxing Gibbous" */
    char     icon_slug[32];      /* Meteocons slug for wx_icons */
    int64_t  moonrise_epoch;
    int64_t  moonset_epoch;
    bool     schedule_valid;
} wx_moon_info_t;

/* Compute phase from Unix epoch (local moon schedule unchanged). */
void wx_moon_compute(int64_t epoch, wx_moon_info_t *out);

/* True between today's moonset and moonrise (or after sunset if no schedule). */
bool wx_is_night_sky(int64_t now, int64_t sunset_epoch,
                     int64_t moonrise_epoch, int64_t moonset_epoch);

/* Sun altitude in degrees. Positive = above the horizon. Uses Kansas
 * defaults when lat/lon are unset so icons still flip before geocode. */
float wx_sun_altitude_deg(int64_t epoch, float lat_deg, float lon_deg);

/* True when the sun is up. Prefers a fresh sunrise/sunset pair from the
 * forecast; otherwise uses solar altitude. Unknown clock → day. */
bool wx_is_daylight(int64_t now, int64_t sunrise_epoch, int64_t sunset_epoch,
                    float lat_deg, float lon_deg);

/* Astronomical real-time lunar altitude in degrees above horizon (-90 to +90) */
float wx_moon_altitude_calc(int64_t epoch, float lat_deg, float lon_deg);

/* Astronomical position fraction along the visible sky arc (0 = rising East, 0.5 = meridian South, 1 = setting West) */
float wx_moon_sky_fraction(int64_t epoch, float lat_deg, float lon_deg);

/* Days until the next full / new moon from a wx_moon_compute age. */
float wx_moon_days_until_full(float age_days);
float wx_moon_days_until_new(float age_days);
