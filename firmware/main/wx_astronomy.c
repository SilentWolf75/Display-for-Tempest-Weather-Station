#include "wx_astronomy.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Gardner / 66030 — same default the moon helpers already use. */
#define DEFAULT_LAT  38.8075f
#define DEFAULT_LON  (-94.9157f)

#define SYNODIC_DAYS  29.530588853

static double julian_day(int64_t epoch)
{
    return (double)epoch / 86400.0 + 2440587.5;
}

void wx_moon_compute(int64_t epoch, wx_moon_info_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));

    double jd = julian_day(epoch);
    /* Reference new moon: 2000-01-06 18:14 UTC (JD 2451550.26) */
    double days = jd - 2451550.26;
    double age  = fmod(days, SYNODIC_DAYS);
    if (age < 0.0) {
        age += SYNODIC_DAYS;
    }

    out->age_days = (float)age;
    /* Illumination peaks at full moon (age ~14.765 days). */
    out->illumination = (float)((1.0 - cos(2.0 * M_PI * age / SYNODIC_DAYS)) * 0.5);

    const char *name = "Full";
    const char *slug = "moon-full";

    if (age < 1.85) {
        name = "New"; slug = "moon-new";
    } else if (age < 5.53) {
        name = "Waxing Crescent"; slug = "moon-waxing-crescent";
    } else if (age < 9.22) {
        name = "First Quarter"; slug = "moon-first-quarter";
    } else if (age < 12.91) {
        name = "Waxing Gibbous"; slug = "moon-waxing-gibbous";
    } else if (age < 16.61) {
        name = "Full"; slug = "moon-full";
    } else if (age < 20.30) {
        name = "Waning Gibbous"; slug = "moon-waning-gibbous";
    } else if (age < 23.99) {
        name = "Last Quarter"; slug = "moon-last-quarter";
    } else {
        name = "Waning Crescent"; slug = "moon-waning-crescent";
    }

    strncpy(out->phase_name, name, sizeof(out->phase_name) - 1);
    strncpy(out->icon_slug, slug, sizeof(out->icon_slug) - 1);
}

bool wx_is_night_sky(int64_t now, int64_t sunset_epoch,
                     int64_t moonrise_epoch, int64_t moonset_epoch)
{
    if (sunset_epoch > 0 && now >= sunset_epoch) {
        if (moonrise_epoch > 0 && moonset_epoch > 0) {
            if (moonrise_epoch < moonset_epoch) {
                return now >= moonset_epoch || now < moonrise_epoch;
            }
            return now >= moonset_epoch && now < moonrise_epoch;
        }
        return true;
    }
    if (sunset_epoch > 0 && now < sunset_epoch) {
        struct tm lt;
        time_t t = (time_t)now;
        localtime_r(&t, &lt);
        return lt.tm_hour >= 20 || lt.tm_hour < 6;
    }
    struct tm lt;
    time_t t = (time_t)now;
    localtime_r(&t, &lt);
    return lt.tm_hour >= 20 || lt.tm_hour < 6;
}

static void get_lunar_coords(int64_t epoch, float lon_deg, double *out_dec, double *out_ha)
{
    double d = ((double)epoch / 86400.0 + 2440587.5) - 2451545.0;

    double L = fmod(218.316 + 13.176396 * d, 360.0) * M_PI / 180.0;
    double M = fmod(134.963 + 13.064993 * d, 360.0) * M_PI / 180.0;
    double F = fmod(93.272 + 13.229350 * d, 360.0) * M_PI / 180.0;

    double l = L + (6.289 * sin(M)) * M_PI / 180.0;
    double b = (5.128 * sin(F)) * M_PI / 180.0;

    double eps = (23.439 - 0.00000036 * d) * M_PI / 180.0;

    double ra = atan2(sin(l) * cos(eps) - tan(b) * sin(eps), cos(l));
    double dec = asin(sin(b) * cos(eps) + cos(b) * sin(eps) * sin(l));

    double gmst = fmod(280.46061837 + 360.98564736629 * d, 360.0) * M_PI / 180.0;
    double lmst = gmst + (double)lon_deg * M_PI / 180.0;
    double ha = lmst - ra;

    /* Normalize HA to [-PI, +PI] */
    while (ha > M_PI)  ha -= 2.0 * M_PI;
    while (ha < -M_PI) ha += 2.0 * M_PI;

    *out_dec = dec;
    *out_ha  = ha;
}

float wx_sun_altitude_deg(int64_t epoch, float lat_deg, float lon_deg)
{
    if (lat_deg == 0.0f && lon_deg == 0.0f) {
        lat_deg = DEFAULT_LAT;
        lon_deg = DEFAULT_LON;
    }
    if (epoch < 1600000000LL) {
        return 45.0f;
    }

    /* Compact NOAA solar position: good to ~0.3° — enough for day/night. */
    double d = (double)epoch / 86400.0 + 2440587.5 - 2451545.0;
    double g = fmod(357.529 + 0.98560028 * d, 360.0) * M_PI / 180.0;
    double q = fmod(280.459 + 0.98564736 * d, 360.0);
    double L = (q + 1.915 * sin(g) + 0.020 * sin(2.0 * g)) * M_PI / 180.0;
    double e = (23.439 - 0.00000036 * d) * M_PI / 180.0;
    double ra = atan2(cos(e) * sin(L), cos(L));
    double dec = asin(sin(e) * sin(L));

    double gmst = fmod(280.46061837 + 360.98564736629 * d, 360.0) * M_PI / 180.0;
    double ha = gmst + (double)lon_deg * M_PI / 180.0 - ra;
    while (ha > M_PI) {
        ha -= 2.0 * M_PI;
    }
    while (ha < -M_PI) {
        ha += 2.0 * M_PI;
    }

    double lat = (double)lat_deg * M_PI / 180.0;
    double sin_alt = sin(lat) * sin(dec) + cos(lat) * cos(dec) * cos(ha);
    if (sin_alt > 1.0) {
        sin_alt = 1.0;
    }
    if (sin_alt < -1.0) {
        sin_alt = -1.0;
    }
    return (float)(asin(sin_alt) * 180.0 / M_PI);
}

bool wx_is_daylight(int64_t now, int64_t sunrise_epoch, int64_t sunset_epoch,
                    float lat_deg, float lon_deg)
{
    if (now < 1600000000LL) {
        return true;
    }

    /* Trust the forecast pair only while it still describes this solar day.
     * Yesterday's sunset would otherwise keep us "night" all morning. */
    if (sunrise_epoch > 0 && sunset_epoch > sunrise_epoch) {
        int64_t span = sunset_epoch - sunrise_epoch;
        if (span > 6 * 3600 && span < 20 * 3600 &&
            now >= sunrise_epoch - 6 * 3600 &&
            now <= sunset_epoch + 6 * 3600) {
            return now >= sunrise_epoch && now < sunset_epoch;
        }
    }

    /* −0.83° is the usual refraction correction at geometric sunrise. */
    return wx_sun_altitude_deg(now, lat_deg, lon_deg) > -0.83f;
}

float wx_moon_altitude_calc(int64_t epoch, float lat_deg, float lon_deg)
{
    if (lat_deg == 0.0f && lon_deg == 0.0f) {
        lat_deg = DEFAULT_LAT;
        lon_deg = DEFAULT_LON;
    }
    double dec = 0.0, ha = 0.0;
    get_lunar_coords(epoch, lon_deg, &dec, &ha);

    double lat = (double)lat_deg * M_PI / 180.0;
    double sin_alt = sin(lat) * sin(dec) + cos(lat) * cos(dec) * cos(ha);
    if (sin_alt > 1.0)  sin_alt = 1.0;
    if (sin_alt < -1.0) sin_alt = -1.0;

    return (float)(asin(sin_alt) * 180.0 / M_PI);
}

float wx_moon_sky_fraction(int64_t epoch, float lat_deg, float lon_deg)
{
    if (lat_deg == 0.0f && lon_deg == 0.0f) {
        lat_deg = DEFAULT_LAT;
        lon_deg = DEFAULT_LON;
    }
    double dec = 0.0, ha = 0.0;
    get_lunar_coords(epoch, lon_deg, &dec, &ha);

    /* ha = -PI/2 (Rising East) -> f = 0.0
     * ha = 0     (Meridian South) -> f = 0.5
     * ha = +PI/2 (Setting West)  -> f = 1.0
     */
    double semi_arc = M_PI / 2.0;
    double lat = (double)lat_deg * M_PI / 180.0;
    /* Accurate semi-diurnal arc for current declination: cos(H0) = -tan(lat)*tan(dec) */
    double cos_h0 = -tan(lat) * tan(dec);
    if (cos_h0 >= -1.0 && cos_h0 <= 1.0) {
        semi_arc = acos(cos_h0);
    }
    if (semi_arc < 0.5) semi_arc = M_PI / 2.0;

    double f = 0.5 + (ha / (2.0 * semi_arc));
    if (f < 0.0) f = 0.0;
    if (f > 1.0) f = 1.0;
    return (float)f;
}

float wx_moon_days_until_full(float age_days)
{
    float full = (float)SYNODIC_DAYS * 0.5f;
    float d = full - age_days;
    if (d < 0.0f) {
        d += (float)SYNODIC_DAYS;
    }
    return d;
}

float wx_moon_days_until_new(float age_days)
{
    if (age_days <= 0.0f) {
        return 0.0f;
    }
    return (float)SYNODIC_DAYS - age_days;
}
