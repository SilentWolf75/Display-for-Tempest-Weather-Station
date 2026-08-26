#include "wx_icons.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_spiffs.h"

static const char *TAG = "wx_icons";

#define ICON_PARTITION  "storage"
#define ICON_MOUNT      "/icons"
#define ICON_PATH_FMT   ICON_MOUNT "/icons/%s.json"

/* Largest Lottie in the set is possibly-sleet-day at ~57 KB. 96 KB leaves
 * room for a richer icon set without another code change. */
#define ICON_JSON_MAX   (96 * 1024)

static bool s_mounted;

/* Per-widget state. LVGL user_data owns it; freed on LV_EVENT_DELETE. */
typedef struct {
    void   *pixels;             /* ARGB8888 render target, PSRAM */
    char   *json;               /* Lottie source, must outlive the widget */
    int     size;
    bool    animate;
    char    slug[40];
} icon_ctx_t;

/* Slugs that mean precipitation. Kept here rather than in the UI so the icon
 * vocabulary lives in exactly one place. */
static const char *WET_SLUGS[] = {
    "possibly-rainy-day", "possibly-rainy-night",
    "possibly-sleet-day", "possibly-sleet-night",
    "possibly-snow-day", "possibly-snow-night",
    "possibly-thunderstorm-day", "possibly-thunderstorm-night",
    "rainy", "sleet", "snow", "thunderstorm",
};

bool wx_icon_is_wet(const char *slug)
{
    if (!slug) {
        return false;
    }
    for (size_t i = 0; i < sizeof(WET_SLUGS) / sizeof(WET_SLUGS[0]); i++) {
        if (strcmp(WET_SLUGS[i], slug) == 0) {
            return true;
        }
    }
    return false;
}

esp_err_t wx_icons_init(void)
{
    if (s_mounted) {
        return ESP_OK;
    }

    esp_vfs_spiffs_conf_t conf = {
        .base_path              = ICON_MOUNT,
        .partition_label        = ICON_PARTITION,
        .max_files              = 4,
        .format_if_mount_failed = false,   /* never reformat: the image is
                                            * build output, not user data */
    };

    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(err));
        ESP_LOGE(TAG, "run 'python tools/build_icons.py' then reflash --");
        ESP_LOGE(TAG, "the storage partition is probably empty.");
        return err;
    }

    size_t total = 0, used = 0;
    if (esp_spiffs_info(ICON_PARTITION, &total, &used) == ESP_OK) {
        ESP_LOGI(TAG, "icons mounted: %u/%u bytes used",
                 (unsigned)used, (unsigned)total);
    }
    s_mounted = true;
    return ESP_OK;
}

static char *load_json(const char *slug, size_t *out_len)
{
    char path[96];
    snprintf(path, sizeof(path), ICON_PATH_FMT, slug);

    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len <= 0 || len > ICON_JSON_MAX) {
        ESP_LOGW(TAG, "%s has implausible size %ld", slug, len);
        fclose(f);
        return NULL;
    }

    /* PSRAM: these are tens of KB and never DMA'd. */
    char *buf = heap_caps_malloc((size_t)len + 1, MALLOC_CAP_SPIRAM);
    if (!buf) {
        ESP_LOGE(TAG, "no PSRAM for %s (%ld bytes)", slug, len);
        fclose(f);
        return NULL;
    }

    size_t got = fread(buf, 1, (size_t)len, f);
    fclose(f);
    if (got != (size_t)len) {
        ESP_LOGE(TAG, "short read on %s: %u of %ld", slug, (unsigned)got, len);
        free(buf);
        return NULL;
    }
    buf[len] = '\0';
    *out_len = (size_t)len;
    return buf;
}

static void icon_delete_cb(lv_event_t *e)
{
    icon_ctx_t *ctx = lv_event_get_user_data(e);
    if (!ctx) {
        return;
    }
    free(ctx->pixels);
    free(ctx->json);
    free(ctx);
}

lv_obj_t *wx_icon_create(lv_obj_t *parent, int size, bool animate)
{
    icon_ctx_t *ctx = calloc(1, sizeof(icon_ctx_t));
    if (!ctx) {
        return NULL;
    }
    ctx->size    = size;
    ctx->animate = animate;

    /* ThorVG renders into ARGB8888 regardless of the display colour format. */
    size_t px_bytes = (size_t)size * (size_t)size * 4u;
    ctx->pixels = heap_caps_malloc(px_bytes, MALLOC_CAP_SPIRAM);
    if (!ctx->pixels) {
        ESP_LOGE(TAG, "no PSRAM for a %dx%d icon buffer (%u B)",
                 size, size, (unsigned)px_bytes);
        free(ctx);
        return NULL;
    }
    memset(ctx->pixels, 0, px_bytes);

    lv_obj_t *obj = lv_lottie_create(parent);
    lv_lottie_set_buffer(obj, size, size, ctx->pixels);
    lv_obj_set_size(obj, size, size);
    lv_obj_add_event_cb(obj, icon_delete_cb, LV_EVENT_DELETE, ctx);
    lv_obj_set_user_data(obj, ctx);
    return obj;
}

void wx_icon_set(lv_obj_t *icon, const char *slug)
{
    if (!icon || !slug || slug[0] == '\0') {
        return;
    }
    icon_ctx_t *ctx = lv_obj_get_user_data(icon);
    if (!ctx) {
        return;
    }
    /* Called from the 1 Hz repaint, so make a no-op cheap. */
    if (strcmp(ctx->slug, slug) == 0) {
        return;
    }

    size_t len = 0;
    char *json = load_json(slug, &len);
    if (!json) {
        /* An unrecognised or missing slug falls back rather than blanking a
         * hole in the layout. Only try the fallback once. */
        if (strcmp(slug, "unknown") == 0) {
            ESP_LOGE(TAG, "fallback icon missing; icons partition broken?");
            return;
        }
        ESP_LOGW(TAG, "no icon for '%s', using fallback", slug);
        wx_icon_set(icon, "unknown");
        return;
    }

    /* ThorVG keeps referencing the source buffer, so the previous one can only
     * be released after the new source is installed. */
    char *old = ctx->json;
    ctx->json = json;
    lv_lottie_set_src_data(icon, json, len);
    free(old);

    strncpy(ctx->slug, slug, sizeof(ctx->slug) - 1);
    ctx->slug[sizeof(ctx->slug) - 1] = '\0';

    if (!ctx->animate) {
        /* Static use: drop the frame-driving animation so the widget renders
         * one representative frame and then costs nothing. Seven simultaneous
         * vector animations in the forecast strip would burn far more CPU than
         * they are worth on a 400 MHz core. */
        lv_anim_delete(icon, NULL);
    }

    ESP_LOGD(TAG, "icon '%s' loaded (%u B, %s)", slug, (unsigned)len,
             ctx->animate ? "animated" : "static");
}
