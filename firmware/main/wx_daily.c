#include "wx_daily.h"
#include <string.h>
#include <time.h>

static int day_key(const struct tm *t)
{
    int y = t->tm_year + 1900;
    int prev = y - 1;
    return 365 * y + prev / 4 - prev / 100 + prev / 400 + t->tm_yday;
}

void wx_daily_add(wx_daily_t *d, const wx_state_t *p)
{
    if (p->obs_epoch <= d->last_epoch) return;
    time_t epoch = (time_t)p->obs_epoch;
    struct tm t;
    localtime_r(&epoch, &t);
    int day = day_key(&t);
    if (d->year != t.tm_year) d->year_rain = 0;
    if (d->year != t.tm_year || d->month != t.tm_mon) d->month_rain = 0;
    d->year = t.tm_year;
    d->month = t.tm_mon;
    d->year_rain += p->rain_last_min_mm;
    d->month_rain += p->rain_last_min_mm;
    wx_day_t *v = &d->days[day % 8];
    if (!v->valid || v->day != day) {
        *v = (wx_day_t){ .day = day, .high = p->air_temp_c,
                        .low = p->air_temp_c, .valid = true };
    }
    v->rain += p->rain_last_min_mm;
    if (p->air_temp_c > v->high) v->high = p->air_temp_c;
    if (p->air_temp_c < v->low) v->low = p->air_temp_c;
    d->last_epoch = p->obs_epoch;
    d->version = 1;
}

void wx_daily_project(const wx_daily_t *d, int64_t now, wx_state_t *out)
{
    if (now < 1600000000LL) now = d->last_epoch;
    time_t epoch = (time_t)now;
    struct tm t;
    localtime_r(&epoch, &t);
    int day = day_key(&t);
    out->rain_today_mm = out->rain_7d_mm = 0;
    out->daily_valid = false;
    out->rain_totals_partial = true; /* Missed LAN packets cannot be inferred. */
    out->rain_ytd_mm = d->year == t.tm_year ? d->year_rain : 0;
    out->rain_month_mm = d->year == t.tm_year && d->month == t.tm_mon ? d->month_rain : 0;
    for (int i = 0; i < 8; i++) {
        const wx_day_t *v = &d->days[i];
        if (!v->valid || v->day > day || v->day < day - 6) continue;
        out->rain_7d_mm += v->rain;
        if (v->day == day) {
            out->rain_today_mm = v->rain;
            out->temp_high_today_c = v->high;
            out->temp_low_today_c = v->low;
            out->daily_valid = true;
        }
    }
}
