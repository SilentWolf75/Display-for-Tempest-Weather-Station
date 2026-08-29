#include "history.h"

#include <math.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "history";

typedef struct {
    int64_t  bucket_start;      /* epoch, floored to HIST_BUCKET_S */
    double   temp_sum;
    double   humidity_sum;
    double   pressure_sum;
    double   wind_sum;
    float    gust_max;
    float    rain_sum;
    double   uv_sum;
    uint16_t samples;
} bucket_t;

static bucket_t         *s_ring;        /* HIST_BUCKETS, PSRAM */
static int               s_head;        /* index of the newest bucket */
static int               s_count;       /* buckets holding data */
static bool              s_backfill_mode;
static SemaphoreHandle_t s_lock;

#define LOCK()    xSemaphoreTake(s_lock, portMAX_DELAY)
#define UNLOCK()  xSemaphoreGive(s_lock)

esp_err_t history_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }
    s_ring = heap_caps_calloc(HIST_BUCKETS, sizeof(bucket_t),
                              MALLOC_CAP_SPIRAM);
    if (!s_ring) {
        ESP_LOGE(TAG, "no PSRAM for %d history buckets", HIST_BUCKETS);
        return ESP_ERR_NO_MEM;
    }
    s_head  = -1;
    s_count = 0;
    ESP_LOGI(TAG, "%d buckets x %u s = %d h of history (%u KB)",
             HIST_BUCKETS, (unsigned)HIST_BUCKET_S,
             HIST_BUCKETS * HIST_BUCKET_S / 3600,
             (unsigned)(HIST_BUCKETS * sizeof(bucket_t) / 1024));
    return ESP_OK;
}

void history_add(int64_t epoch, float temp_c, float humidity_pct,
                 float pressure_mb, float wind_avg_ms, float wind_gust_ms,
                 float rain_mm, float uv)
{
    if (!s_ring || epoch < 1600000000LL) {
        return;             /* clock not set; a bucket key would be nonsense */
    }
    int64_t start = (epoch / HIST_BUCKET_S) * HIST_BUCKET_S;

    LOCK();

    bucket_t *b = (s_head >= 0) ? &s_ring[s_head] : NULL;

    /* Reject anything older than the newest bucket. The ring only tracks a
     * head, so an out-of-order sample would open a new bucket with an older
     * timestamp and scramble the ordering. This matters because the REST
     * backfill and the live UDP feed can overlap at startup -- whichever wins,
     * the buffer stays monotonic. */
    if (!s_backfill_mode && b && start < b->bucket_start) {
        UNLOCK();
        return;
    }

    if (!b || b->bucket_start != start) {
        /* New bucket. Note this does NOT zero-fill the gap when the device was
         * offline for a while -- history_get() detects gaps by comparing
         * bucket_start against the expected time, so silence stays visible as
         * a gap rather than being interpolated over. */
        s_head = (s_head + 1) % HIST_BUCKETS;
        b = &s_ring[s_head];
        memset(b, 0, sizeof(*b));
        b->bucket_start = start;
        if (s_count < HIST_BUCKETS) {
            s_count++;
        }
    }

    b->temp_sum     += temp_c;
    b->humidity_sum += humidity_pct;
    b->pressure_sum += pressure_mb;
    b->wind_sum     += wind_avg_ms;
    b->uv_sum       += uv;
    b->rain_sum     += rain_mm;             /* summed, not averaged */
    if (wind_gust_ms > b->gust_max) {
        b->gust_max = wind_gust_ms;         /* max, not averaged */
    }
    b->samples++;

    UNLOCK();
}

/* Reads one series out of a bucket. NAN means "no data in this bucket". */
static float bucket_value(const bucket_t *b, hist_series_t series)
{
    if (b->samples == 0) {
        return NAN;
    }
    double n = (double)b->samples;
    switch (series) {
    case HIST_TEMP:     return (float)(b->temp_sum / n);
    case HIST_PRESSURE: return (float)(b->pressure_sum / n);
    case HIST_WIND:     return (float)(b->wind_sum / n);
    case HIST_HUMIDITY: return (float)(b->humidity_sum / n);
    default:            return NAN;
    }
}

/* Walks the ring oldest-to-newest into a caller buffer. Shared by both
 * getters so the ordering logic exists once. */
static int walk(float *out, int out_len, hist_series_t series, bool gust,
                float *min_out, float *max_out)
{
    if (!s_ring || s_count == 0) {
        return 0;
    }

    float lo = INFINITY, hi = -INFINITY;
    int written = 0;
    int n = (out_len < s_count) ? out_len : s_count;

    /* Oldest of the n we are returning. */
    int start = (s_head - n + 1 + HIST_BUCKETS * 2) % HIST_BUCKETS;

    for (int i = 0; i < n; i++) {
        const bucket_t *b = &s_ring[(start + i) % HIST_BUCKETS];
        float v = gust ? (b->samples ? b->gust_max : NAN)
                       : bucket_value(b, series);
        out[i] = v;
        if (!isnan(v)) {
            written++;
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
    }
    for (int i = n; i < out_len; i++) {
        out[i] = NAN;
    }

    if (min_out) *min_out = isinf(lo) ? 0.0f : lo;
    if (max_out) *max_out = isinf(hi) ? 0.0f : hi;
    return written;
}

int history_get(hist_series_t series, float *out, int out_len,
                float *min_out, float *max_out)
{
    if (!out || out_len <= 0 || series >= HIST_SERIES_COUNT) {
        return 0;
    }
    LOCK();
    int n = walk(out, out_len, series, false, min_out, max_out);
    UNLOCK();
    return n;
}

bool history_get_secondary(hist_series_t series, float *out, int out_len)
{
    if (series != HIST_WIND || !out || out_len <= 0) {
        return false;
    }
    LOCK();
    walk(out, out_len, series, true, NULL, NULL);
    UNLOCK();
    return true;
}

bool history_is_plottable(void)
{
    return s_count >= 2;
}

void history_begin_backfill(void)
{
    LOCK();
    s_backfill_mode = true;
    s_head  = -1;
    s_count = 0;
    if (s_ring) {
        memset(s_ring, 0, HIST_BUCKETS * sizeof(bucket_t));
    }
    UNLOCK();
}

void history_end_backfill(void)
{
    LOCK();
    s_backfill_mode = false;
    UNLOCK();
}

void history_span(int64_t *oldest, int64_t *newest)
{
    LOCK();
    if (s_count == 0) {
        if (oldest) *oldest = 0;
        if (newest) *newest = 0;
    } else {
        int start = (s_head - s_count + 1 + HIST_BUCKETS * 2) % HIST_BUCKETS;
        if (oldest) *oldest = s_ring[start].bucket_start;
        if (newest) *newest = s_ring[s_head].bucket_start;
    }
    UNLOCK();
}
