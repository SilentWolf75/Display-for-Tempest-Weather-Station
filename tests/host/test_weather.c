#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_platform.h"
#include "wx_state.h"
#include "wx_daily.h"
#include "history.h"
#include "alert_policy.h"
static void near(float a,float b) { assert(fabsf(a-b)<0.001f); }
static wx_state_t reading(int64_t epoch,float rain,float temp) {
    wx_state_t p={0}; p.obs_epoch=epoch; p.air_temp_c=temp;
    p.humidity_pct=50; p.pressure_mb=1000; p.wind_avg_ms=2;
    p.wind_gust_ms=4; p.rain_last_min_mm=rain; return p;
}
static void test_dates(void) {
    assert(nws_parse_iso("2026-09-04T12:00:00-05:00")==nws_parse_iso("2026-09-04T17:00:00Z"));
    assert(nws_parse_iso("2026-09-04T12:00:00+05:30")==nws_parse_iso("2026-09-04T06:30:00Z"));
    assert(!nws_parse_iso("2026-02-29T12:00:00Z"));
    assert(nws_parse_iso("2024-03-01T00:00:00Z")-nws_parse_iso("2024-02-28T00:00:00Z")==172800);
    nws_alert_t advisory={.active=true,.expires_epoch=100};
    strcpy(advisory.event,"Heat Advisory"); strcpy(advisory.severity,"Moderate");
    nws_alert_t tornado=advisory; strcpy(tornado.event,"Tornado Warning");
    strcpy(tornado.severity,"Severe");
    assert(nws_alert_rank(&tornado)>nws_alert_rank(&advisory));
    assert(nws_alert_live(&advisory,99)); assert(!nws_alert_live(&advisory,100));
    puts("PASS alert priority, expiry, timezone offsets, leap days");
}
static void test_daily(void) {
    wx_daily_t d={0}; wx_state_t out={0};
    int64_t start=nws_parse_iso("2025-12-28T12:00:00Z");
    for(int i=0;i<9;i++) {
        wx_state_t p=reading(start+i*86400,1,10+i);
        wx_daily_add(&d,&p); wx_daily_add(&d,&p);
    }
    wx_daily_project(&d,start+8*86400,&out);
    near(out.rain_7d_mm,7); near(out.rain_today_mm,1);
    near(out.rain_month_mm,5); near(out.rain_ytd_mm,5);
    assert(out.daily_valid && out.rain_totals_partial);
    wx_daily_project(&d,nws_parse_iso("2026-02-01T00:00:00Z"),&out);
    near(out.rain_month_mm,0); near(out.rain_today_mm,0);
    near(out.rain_7d_mm,0); near(out.rain_ytd_mm,5); assert(!out.daily_valid);
    puts("PASS daily deduplication, 7-day expiry, month/year rollover and zeros");
}
static void test_history(void) {
    assert(history_init()==ESP_OK);
    test_now=(time_t)nws_parse_iso("2026-09-04T12:04:00Z");
    int64_t bucket=(int64_t)test_now/300*300;
    history_add(bucket,20,50,1000,2,4,1,0);
    history_add(bucket,20,50,1000,2,4,1,0);
    float out[HIST_BUCKETS];
    assert(history_get(HIST_RAIN,out,HIST_BUCKETS,NULL,NULL)==1);
    near(out[287],1); assert(isnan(out[286]));
    assert(history_begin_backfill()==ESP_OK);
    history_add_backfill(bucket-600,10,50,1000,2,4,2,0);
    history_add(bucket+60,22,50,1000,2,4,3,0);
    history_add_backfill(bucket+60,99,50,1000,2,4,99,0);
    history_add_backfill(bucket,99,50,1000,2,4,99,0);
    history_end_backfill();
    assert(history_get(HIST_RAIN,out,HIST_BUCKETS,NULL,NULL)==2);
    near(out[285],2); assert(isnan(out[286])); near(out[287],4);
    history_get(HIST_TEMP,out,HIST_BUCKETS,NULL,NULL); near(out[287],21);
    test_fail_alloc=1; assert(history_begin_backfill()==ESP_ERR_NO_MEM);
    history_get(HIST_RAIN,out,HIST_BUCKETS,NULL,NULL); near(out[287],4);
    assert(history_begin_backfill()==ESP_OK); history_end_backfill();
    history_get(HIST_RAIN,out,HIST_BUCKETS,NULL,NULL); near(out[287],4);
    test_now+=86400;
    assert(history_get(HIST_RAIN,out,HIST_BUCKETS,NULL,NULL)==0);
    for(int i=0;i<HIST_BUCKETS;i++) assert(isnan(out[i]));
    assert(!history_is_plottable());
    puts("PASS history gaps, interleaving, duplicates, allocation failure, expiry");
}
static void test_state(void) {
    test_now=(time_t)nws_parse_iso("2026-09-05T12:00:00Z");
    assert(wx_state_init()==ESP_OK);
    wx_state_t p=reading(test_now,2,23), out;
    wx_update_obs_st(&p); wx_update_obs_st(&p); wx_snapshot(&out);
    near(out.rain_today_mm,2);
    p.obs_epoch++; p.air_temp_c=NAN;
    assert(!wx_obs_values_valid(&p)); wx_update_obs_st(&p); wx_snapshot(&out);
    near(out.air_temp_c,23); near(out.temp_low_today_c,23);
    p.air_temp_c=23; p.pressure_mb=0; assert(!wx_obs_values_valid(&p));
    wx_daily_checkpoint(); assert(wx_state_init()==ESP_OK); wx_snapshot(&out);
    near(out.rain_today_mm,2); near(out.temp_low_today_c,23);
    p=reading(test_now,2,23); wx_update_obs_st(&p); wx_snapshot(&out);
    near(out.rain_today_mm,2);
    wx_update_aqi(0,"Good",0); wx_snapshot(&out); assert(!wx_aqi_is_stale(&out));
    test_now+=7201; wx_snapshot(&out); assert(wx_aqi_is_stale(&out));
    assert(wx_obs_is_stale(&out));
    wx_note_udp_packet(); wx_snapshot(&out); assert(!wx_udp_is_stale(&out));
    test_now+=181; wx_snapshot(&out); assert(wx_udp_is_stale(&out));
    puts("PASS invalid readings, deduplication, persistence restore and freshness");
}
void test_runtime(void);
int main(void) {
#ifdef _WIN32
    _putenv_s("TZ","UTC0"); _tzset();
#else
    setenv("TZ","UTC0",1); tzset();
#endif
    test_dates();test_daily();test_history();test_state();test_runtime();
    puts("All weather regressions passed"); return 0;
}
