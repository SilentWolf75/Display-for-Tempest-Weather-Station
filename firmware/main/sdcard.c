#include "sdcard.h"
#include "board_pins.h"

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <time.h>
#include <errno.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/gpio.h"
#include "sdmmc_cmd.h"
#include "esp_heap_caps.h"
#include <stdlib.h>

static const char *TAG = "sdcard";

static sdmmc_card_t *s_card;
static bool          s_mounted;
static int64_t       s_last_log_epoch;
static bool          s_subdir;   /* /sdcard/weather usable? */
static volatile sdcard_fmt_state_t s_fmt = SDCARD_FMT_IDLE;

static esp_err_t mount_card(void)
{
    /* Ensure strong internal pullups on CMD and D0 */
    gpio_set_pull_mode(BOARD_SD_CMD_GPIO, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(BOARD_SD_D0_GPIO, GPIO_PULLUP_ONLY);

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot         = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = 4000;
    host.command_timeout_ms = 4000;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.clk   = BOARD_SD_CLK_GPIO;
    slot.cmd   = BOARD_SD_CMD_GPIO;
    slot.d0    = BOARD_SD_D0_GPIO;
    slot.d1    = GPIO_NUM_NC;
    slot.d2    = GPIO_NUM_NC;
    slot.d3    = GPIO_NUM_NC;
    slot.d4    = GPIO_NUM_NC;
    slot.d5    = GPIO_NUM_NC;
    slot.d6    = GPIO_NUM_NC;
    slot.d7    = GPIO_NUM_NC;
    slot.cd    = GPIO_NUM_NC;
    slot.wp    = GPIO_NUM_NC;
    slot.width = 1;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t cfg = {
        .format_if_mount_failed = true,
        .max_files              = 5,
        .allocation_unit_size   = 0, /* 0 = auto-select based on card size */
    };

    esp_err_t err = esp_vfs_fat_sdmmc_mount(SDCARD_MOUNT, &host, &slot,
                                            &cfg, &s_card);
    if (err == ESP_OK) {
        s_mounted = true;
        ESP_LOGI(TAG, "mounted %s: %s, %llu MB",
                 s_card->cid.name,
                 (s_card->ocr & (1 << 30)) ? "SDHC/SDXC" : "SDSC",
                 ((uint64_t)s_card->csd.capacity * s_card->csd.sector_size)
                     / (1024 * 1024));
        /* One directory create, which doubles as the only write probe worth
         * doing: if the filesystem cannot make a directory it cannot allocate
         * a cluster, and the logs go in the root instead. No other writes
         * happen at mount. */
        s_subdir = (mkdir(SDCARD_LOG_DIR, 0777) == 0 || errno == EEXIST);
        ESP_LOGI(TAG, "history goes to %s",
                 s_subdir ? SDCARD_LOG_DIR : SDCARD_MOUNT " (root)");
        if (!s_subdir) {
            ESP_LOGW(TAG, "could not create %s (errno %d); using the root",
                     SDCARD_LOG_DIR, errno);
        }
    }
    return err;
}

esp_err_t sdcard_start(void)
{
    if (s_mounted) {
        return ESP_OK;
    }

    esp_err_t err = mount_card();
    if (err == ESP_OK) {
        return ESP_OK;
    }

    if (err == ESP_ERR_NOT_FOUND || err == ESP_ERR_TIMEOUT) {
        ESP_LOGW(TAG, "no card in the slot; history logging is off");
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGE(TAG, "mount failed: %s", esp_err_to_name(err));
    return err;
}

bool sdcard_is_mounted(void)
{
    return s_mounted;
}

void sdcard_unmount(void)
{
    if (!s_mounted) {
        return;
    }
    esp_vfs_fat_sdcard_unmount(SDCARD_MOUNT, s_card);
    s_card    = NULL;
    s_mounted = false;
}

esp_err_t sdcard_remount(void)
{
    sdcard_unmount();
    return sdcard_start();
}

static uint32_t count_log_files(void)
{
    DIR *d = opendir(s_subdir ? SDCARD_LOG_DIR : SDCARD_MOUNT);
    if (!d) {
        return 0;
    }
    uint32_t n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        const char *dot = strrchr(e->d_name, '.');
        if (!dot || strcasecmp(dot, ".csv") != 0) continue;
        /* In the root the prefix is what separates our files from the user's;
         * inside /weather everything there is ours. */
        if (s_subdir || strncmp(e->d_name, SDCARD_LOG_PREFIX,
                                strlen(SDCARD_LOG_PREFIX)) == 0) {
            n++;
        }
    }
    closedir(d);
    return n;
}

esp_err_t sdcard_get_info(sdcard_info_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    if (!s_mounted || !s_card) {
        return ESP_ERR_INVALID_STATE;
    }

    out->mounted = true;

    /* The CID product name is a fixed-width field that is not required to be
     * printable, and plenty of cards leave it blank or full of zeros -- this
     * one reports "00000", which is worse than useless on screen. Keep only
     * printable characters, trim the padding, and fall back to something
     * honest rather than showing the raw field. */
    size_t w = 0;
    for (size_t i = 0; i < sizeof(s_card->cid.name)
                       && w < sizeof(out->name) - 1; i++) {
        char ch = s_card->cid.name[i];
        if (ch >= 0x20 && ch < 0x7F) {
            out->name[w++] = ch;
        }
    }
    while (w > 0 && (out->name[w - 1] == ' ' || out->name[w - 1] == '0')) {
        w--;
    }
    out->name[w] = '\0';
    if (w == 0) {
        snprintf(out->name, sizeof(out->name), "microSD");
    }

    uint64_t total = 0, freeb = 0;
    if (esp_vfs_fat_info(SDCARD_MOUNT, &total, &freeb) == ESP_OK) {
        out->total_bytes = total;
        out->free_bytes  = freeb;
    } else {
        /* Fall back to the card's own capacity if FATFS will not answer. */
        out->total_bytes = (uint64_t)s_card->csd.capacity
                         * s_card->csd.sector_size;
    }
    out->log_files = count_log_files();
    return ESP_OK;
}

/* --- formatting ---------------------------------------------------------- */

static void format_task(void *arg)
{
    (void)arg;

    esp_err_t err;
    if (s_mounted && s_card) {
        err = esp_vfs_fat_sdcard_format(SDCARD_MOUNT, s_card);
    } else {
        /* Nothing mounted -- an unformatted card. Mount with formatting
         * allowed this once, because the user explicitly asked for it. */
        sdmmc_host_t host = SDMMC_HOST_DEFAULT();
        host.slot         = SDMMC_HOST_SLOT_0;
        host.max_freq_khz = BOARD_SD_FREQ_KHZ;

        sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
        slot.clk   = BOARD_SD_CLK_GPIO;
        slot.cmd   = BOARD_SD_CMD_GPIO;
        slot.d0    = BOARD_SD_D0_GPIO;
        slot.width = 1;
        slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

        esp_vfs_fat_sdmmc_mount_config_t cfg = {
            .format_if_mount_failed = true,
            .max_files              = 5,
            .allocation_unit_size   = 16 * 1024,
        };
        err = esp_vfs_fat_sdmmc_mount(SDCARD_MOUNT, &host, &slot, &cfg, &s_card);
        if (err == ESP_OK) {
            s_mounted = true;
        }
    }

    if (err == ESP_OK) {
        s_subdir = (mkdir(SDCARD_LOG_DIR, 0777) == 0 || errno == EEXIST);
        s_last_log_epoch = 0;          /* header gets rewritten */
        ESP_LOGI(TAG, "card formatted");
        s_fmt = SDCARD_FMT_DONE;
    } else {
        ESP_LOGE(TAG, "format failed: %s", esp_err_to_name(err));
        s_fmt = SDCARD_FMT_FAILED;
    }
    vTaskDelete(NULL);
}

esp_err_t sdcard_format_async(void)
{
    if (s_fmt == SDCARD_FMT_BUSY) {
        return ESP_ERR_INVALID_STATE;
    }
    s_fmt = SDCARD_FMT_BUSY;
    if (xTaskCreate(format_task, "sd_format", 4096, NULL, 4, NULL) != pdPASS) {
        s_fmt = SDCARD_FMT_FAILED;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

sdcard_fmt_state_t sdcard_format_state(void)
{
    return s_fmt;
}

/* --- history logging ----------------------------------------------------- */

esp_err_t sdcard_log_weather(const wx_state_t *s, int64_t now_epoch)
{
    if (!s_mounted)            return ESP_ERR_INVALID_STATE;
    if (!s || !s->obs_valid)   return ESP_ERR_INVALID_STATE;
    if (s_fmt == SDCARD_FMT_BUSY) return ESP_ERR_INVALID_STATE;
    /* Needs a real clock, or every row lands in 1970. */
    if (now_epoch < 1700000000LL) return ESP_ERR_INVALID_STATE;
    if (now_epoch - s_last_log_epoch < 60) return ESP_OK;

    time_t t = (time_t)now_epoch;
    struct tm lt;
    localtime_r(&t, &lt);

    char path[64];
    if (s_subdir) {
        snprintf(path, sizeof(path), SDCARD_LOG_DIR "/%04d_%02d.csv",
                 lt.tm_year + 1900, lt.tm_mon + 1);
    } else {
        snprintf(path, sizeof(path),
                 SDCARD_MOUNT "/" SDCARD_LOG_PREFIX "%04d_%02d.csv",
                 lt.tm_year + 1900, lt.tm_mon + 1);
    }

    struct stat st;
    bool write_header = (stat(path, &st) != 0 || st.st_size == 0);

    FILE *f = fopen(path, "a");
    if (!f) {
        /* fopen("a") creates the file but NOT its parent, so the usual cause
         * is a missing directory, not a missing card. Make it and retry once
         * before concluding anything about the hardware -- unmounting on the
         * first failed write reports a phantom "card removed" for what is
         * really a one-line fix. */
        ESP_LOGW(TAG, "cannot open %s: errno %d", path, errno);
        sdcard_unmount();
        return ESP_FAIL;
    }

    if (write_header) {
        fprintf(f, "epoch,datetime,temp_f,feels_f,dew_f,humidity_pct,"
                   "pressure_inhg,wind_avg_mph,wind_gust_mph,wind_dir_deg,"
                   "rain_today_in,rain_rate_in_hr,solar_wm2,uv,battery_v,aqi,"
                   "indoor_f,indoor_rh\n");
    }

    char when[32];
    strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", &lt);

    fprintf(f,
            "%lld,%s,%.2f,%.2f,%.2f,%.1f,%.3f,%.2f,%.2f,%d,"
            "%.3f,%.3f,%.1f,%.2f,%.2f,%d,%.2f,%.1f\n",
            (long long)now_epoch, when,
            (double)wx_c_to_f(s->air_temp_c),
            (double)wx_c_to_f(s->feels_like_c),
            (double)wx_c_to_f(s->dew_point_c),
            (double)s->humidity_pct,
            (double)wx_mb_to_inhg(s->pressure_mb),
            (double)wx_ms_to_mph(s->wind_avg_ms),
            (double)wx_ms_to_mph(s->wind_gust_ms),
            s->wind_dir_deg,
            (double)wx_mm_to_in(s->rain_today_mm),
            (double)wx_mm_to_in(s->rain_rate_mm_hr),
            (double)s->solar_radiation_wm2,
            (double)s->uv_index,
            (double)s->battery_v,
            s->aqi_valid ? s->aqi_val : 0,
            s->indoor_valid ? (double)wx_c_to_f(s->indoor_temp_c) : 0.0,
            s->indoor_valid ? (double)s->indoor_humidity_pct : 0.0);

    fclose(f);
    s_last_log_epoch = now_epoch;
    ESP_LOGI(TAG, "logged weather row -> %s (%s)", path, when);
    return ESP_OK;
}

esp_err_t sdcard_current_log_path(char *path, size_t path_len)
{
    if (!path || path_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }

    time_t t = time(NULL);
    if (t < 1700000000LL) {
        return ESP_ERR_INVALID_STATE;
    }
    struct tm lt;
    localtime_r(&t, &lt);

    if (s_subdir) {
        snprintf(path, path_len, SDCARD_LOG_DIR "/%04d_%02d.csv",
                 lt.tm_year + 1900, lt.tm_mon + 1);
    } else {
        snprintf(path, path_len,
                 SDCARD_MOUNT "/" SDCARD_LOG_PREFIX "%04d_%02d.csv",
                 lt.tm_year + 1900, lt.tm_mon + 1);
    }
    return ESP_OK;
}
