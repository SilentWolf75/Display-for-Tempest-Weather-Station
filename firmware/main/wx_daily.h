#pragma once
#include "wx_state.h"
typedef struct {
    int32_t day;
    float rain, high, low;
    bool valid;
} wx_day_t;
typedef struct {
    uint32_t version;
    int64_t last_epoch;
    int32_t year, month;
    float month_rain, year_rain;
    wx_day_t days[8];
} wx_daily_t;
/* Called while the wx_state mutex is held. */
void wx_daily_add(wx_daily_t *d, const wx_state_t *p);
void wx_daily_project(const wx_daily_t *d, int64_t now, wx_state_t *out);

/* Raise today (and yesterday) to at least the station's official local-day
 * totals. UDP 1-minute tips are missed whenever the panel is off or a LAN
 * packet drops; WeatherFlow's precip_accum_local_day is the number the
 * official apps show. Returns true if any bucket changed.
 * station_epoch, when newer than last_epoch, becomes the dedup cursor so
 * the next UDP minute is not added on top of rain the station already
 * counted. */
bool wx_daily_raise_station(wx_daily_t *d, int64_t now,
                            float today_mm, float yesterday_mm,
                            int64_t station_epoch);
