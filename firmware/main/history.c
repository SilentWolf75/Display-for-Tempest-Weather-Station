#include "history.h"
#include <math.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"

/* Minute slots allow timestamp-based deduplication across UDP and REST.
 * Five-minute averages are assembled on read, preserving real gaps. */
#define MINUTES (HIST_BUCKETS * HIST_BUCKET_S / 60)
typedef struct {
    int64_t epoch;
    float temp, humidity, pressure, wind, gust, rain, uv;
} sample_t;
static sample_t *s_live, *s_seed;
static SemaphoreHandle_t s_lock;
#define LOCK() xSemaphoreTake(s_lock, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_lock)

esp_err_t history_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_live = heap_caps_calloc(MINUTES, sizeof(sample_t), MALLOC_CAP_SPIRAM);
    return s_lock && s_live ? ESP_OK : ESP_ERR_NO_MEM;
}

static void put(sample_t *ring, const sample_t *p)
{
    if (!ring || p->epoch < 1600000000LL) return;
    sample_t *old = &ring[(p->epoch / 60) % MINUTES];
    if (p->epoch > old->epoch) *old = *p;
}

void history_add(int64_t epoch, float temp, float humidity, float pressure,
                 float wind, float gust, float rain, float uv)
{
    sample_t p = {epoch, temp, humidity, pressure, wind, gust, rain, uv};
    if (!s_live) return;
    LOCK();
    put(s_live, &p);
    UNLOCK();
}

void history_add_backfill(int64_t epoch, float temp, float humidity, float pressure,
                          float wind, float gust, float rain, float uv)
{
    sample_t p = {epoch, temp, humidity, pressure, wind, gust, rain, uv};
    LOCK();
    put(s_seed, &p);
    UNLOCK();
}

esp_err_t history_begin_backfill(void)
{
    sample_t *seed = heap_caps_calloc(MINUTES, sizeof(sample_t), MALLOC_CAP_SPIRAM);
    if (!seed) return ESP_ERR_NO_MEM;
    LOCK();
    if (s_seed) {
        UNLOCK();
        free(seed);
        return ESP_ERR_INVALID_STATE;
    }
    s_seed = seed;
    UNLOCK();
    return ESP_OK;
}

void history_end_backfill(void)
{
    LOCK();
    sample_t *seed = s_seed;
    if (seed) {
        for (int i = 0; i < MINUTES; i++) {
            /* Live wins for the same minute, even if cloud rounding differs. */
            if (s_live[i].epoch / 60 < seed[i].epoch / 60) s_live[i] = seed[i];
        }
    }
    s_seed = NULL;
    UNLOCK();
    free(seed);
}

static int walk(float *out, int len, hist_series_t series, bool gust,
                float *min_out, float *max_out)
{
    for (int i = 0; i < len; i++) out[i] = NAN;
    if (min_out) *min_out = 0;
    if (max_out) *max_out = 0;
    int64_t now = (int64_t)time(NULL);
    if (!s_live || now < 1600000000LL) return 0;
    int n = len < HIST_BUCKETS ? len : HIST_BUCKETS;
    int64_t end = now / HIST_BUCKET_S * HIST_BUCKET_S;
    int64_t start = end - (int64_t)(n - 1) * HIST_BUCKET_S;
    int have = 0;
    float lo = INFINITY, hi = -INFINITY;
    for (int i = 0; i < n; i++) {
        double sum = 0;
        float peak = 0;
        int count = 0;
        int64_t bucket = start + (int64_t)i * HIST_BUCKET_S;
        for (int m = 0; m < HIST_BUCKET_S / 60; m++) {
            int64_t minute = bucket / 60 + m;
            const sample_t *p = &s_live[minute % MINUTES];
            if (p->epoch / 60 != minute || p->epoch > now) continue;
            float v = series == HIST_TEMP ? p->temp :
                      series == HIST_PRESSURE ? p->pressure :
                      series == HIST_WIND ? p->wind :
                      series == HIST_HUMIDITY ? p->humidity : p->rain;
            if (!isfinite(v)) continue;
            sum += v;
            if (p->gust > peak) peak = p->gust;
            count++;
        }
        if (!count) continue;
        float v = gust ? peak : (float)(series == HIST_RAIN ? sum : sum / count);
        out[i] = v;
        if (v < lo) lo = v;
        if (v > hi) hi = v;
        have++;
    }
    if (have) {
        if (min_out) *min_out = lo;
        if (max_out) *max_out = hi;
    }
    return have;
}

int history_get(hist_series_t series, float *out, int len, float *lo, float *hi)
{
    if (!out || len <= 0 || series < 0 || series >= HIST_SERIES_COUNT) return 0;
    LOCK();
    int n = walk(out, len, series, false, lo, hi);
    UNLOCK();
    return n;
}

bool history_get_secondary(hist_series_t series, float *out, int len)
{
    if (series != HIST_WIND || !out || len <= 0) return false;
    LOCK();
    walk(out, len, series, true, NULL, NULL);
    UNLOCK();
    return true;
}

void history_span(int64_t *oldest, int64_t *newest)
{
    int64_t now = (int64_t)time(NULL);
    int64_t first = now / HIST_BUCKET_S * HIST_BUCKET_S -
                    (HIST_BUCKETS - 1LL) * HIST_BUCKET_S;
    int64_t lo = 0, hi = 0;
    LOCK();
    if (s_live) for (int i = 0; i < MINUTES; i++) {
        int64_t t = s_live[i].epoch;
        if (t < first || t > now || t < 1600000000LL) continue;
        if (!lo || t < lo) lo = t;
        if (t > hi) hi = t;
    }
    UNLOCK();
    if (oldest) *oldest = lo;
    if (newest) *newest = hi;
}

bool history_is_plottable(void)
{
    int64_t lo, hi;
    history_span(&lo, &hi);
    return lo && lo / HIST_BUCKET_S != hi / HIST_BUCKET_S;
}
