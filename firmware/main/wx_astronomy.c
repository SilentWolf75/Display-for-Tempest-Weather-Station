#include "wx_astronomy.h"

#include <math.h>
#include <string.h>
#include <time.h>

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
