#pragma once

/* microSD card on SDMMC slot 0.
 *
 * The previous version of this file was not an SD driver at all: it reported a
 * hardcoded "mounted", read its free space off the SPIFFS icon partition, and
 * its logging function was never called from anywhere. This one talks to the
 * card.
 *
 * Pins are Elecrow's, from the V1.2 board notes: slot 0, 1-bit, 10 MHz,
 * internal pull-ups. There is no card-detect line on this board, so removal is
 * only noticed the next time an operation fails.
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "wx_state.h"

#define SDCARD_MOUNT      "/sdcard"
/* Logs go in /sdcard/weather when the filesystem will make the directory,
 * and fall back to the card root with a "wx_" prefix when it will not.
 *
 * That is not belt-and-braces for its own sake: on the exFAT card this was
 * developed against, f_mkdir failed with FR_DISK_ERR while writing a file in
 * the root worked. FAT32 handles both, so it gets the tidy layout. */
#define SDCARD_LOG_DIR    SDCARD_MOUNT "/weather"

/* Prefix used only by the root fallback.
 *
 * On the 128 GB exFAT card this was developed against, f_mkdir and
 * f_getfree both fail with FR_DISK_ERR, while creating, writing and
 * closing a file in the root works perfectly. Rather than fight it, the
 * files carry a prefix so they are still easy to find and count. */
#define SDCARD_LOG_PREFIX "wx_"

typedef struct {
    bool     mounted;
    char     name[16];          /* CID product name, e.g. "SD32G" */
    uint64_t total_bytes;
    uint64_t free_bytes;
    uint32_t log_files;         /* CSV months on the card */
} sdcard_info_t;

/* Mounts the card if one is present. Never formats automatically -- a card
 * with data on it is the user's, not ours. Returns ESP_OK when mounted,
 * ESP_ERR_NOT_FOUND when the slot is empty, and passes anything else through. */
esp_err_t sdcard_start(void);

bool sdcard_is_mounted(void);
esp_err_t sdcard_get_info(sdcard_info_t *out);

/* Unmount, then try to mount again -- for a card swapped while running. */
esp_err_t sdcard_remount(void);
void sdcard_unmount(void);

/* Formatting takes seconds and must never run on the LVGL task, so it happens
 * on its own worker. Kick it off, then poll sdcard_format_state(). */
typedef enum {
    SDCARD_FMT_IDLE = 0,
    SDCARD_FMT_BUSY,
    SDCARD_FMT_DONE,
    SDCARD_FMT_FAILED,
} sdcard_fmt_state_t;

esp_err_t          sdcard_format_async(void);
sdcard_fmt_state_t sdcard_format_state(void);

/* Appends one row to /sdcard/weather/YYYY_MM.csv, at most once a minute.
 * Safe to call often; it rate-limits itself and no-ops when unmounted. */
esp_err_t sdcard_log_weather(const wx_state_t *s, int64_t now_epoch);
