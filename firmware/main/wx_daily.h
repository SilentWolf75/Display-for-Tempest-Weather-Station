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
