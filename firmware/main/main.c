/*
 * Tempest Weather Display
 * Elecrow CrowPanel Advance 10.1" (ESP32-P4 + ESP32-C6)
 *
 * Boot order matters:
 *   1. NVS + shared state, before any producer exists.
 *   2. Display, so a failure anywhere later is visible on the panel rather
 *      than only on a serial console nobody is watching.
 *   3. Network. Non-fatal if it fails -- the UI says "no network" and the
 *      retry loop keeps running in the background.
 *   4. UDP ingest (live data) and REST poll (forecast).
 *   5. A 1 Hz repaint timer.
 */

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"

#include "wx_state.h"
#include "config.h"
#include "history.h"
#include "diag.h"
#include "ota.h"
#include "display.h"
#include "net.h"
#include "tempest_udp.h"
#include "tempest_rest.h"
#include "indoor.h"
#include "ui/ui.h"
#include "ui/wx_icons.h"

static const char *TAG = "main";

#define UI_TICK_PERIOD_MS   1000
#define UI_LOCK_TIMEOUT_MS  100

static void ui_timer_cb(lv_timer_t *timer)
{
    /* Runs inside the LVGL task, so the lock is already held. */
    (void)timer;
    ui_tick();
}

static void log_boot_banner(void)
{
    ESP_LOGI(TAG, "----------------------------------------");
    ESP_LOGI(TAG, "Tempest Weather Display");
    ESP_LOGI(TAG, "station %d, UDP port %d",
             CONFIG_TEMPEST_STATION_ID, CONFIG_TEMPEST_UDP_PORT);
    ESP_LOGI(TAG, "internal free: %u B",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "PSRAM free:    %u B",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    ESP_LOGI(TAG, "----------------------------------------");
}

void app_main(void)
{
    log_boot_banner();

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(wx_state_init());
    ESP_ERROR_CHECK(cfg_init());   /* before the UI reads units */
    ESP_ERROR_CHECK(history_init());  /* before the first obs_st */

#if CONFIG_DIAG_ON_BOOT
    /* Before the display, so this still reaches the console when the panel
     * refuses to initialise -- the most likely first-boot outcome while
     * board_pins.h is unverified. */
    diag_run_all();
#endif

    /* Icons must be mounted before ui_init() builds the widgets that load
     * them. A failure here is not fatal -- the panel renders without artwork
     * rather than not at all. */
    if (wx_icons_init() != ESP_OK) {
        ESP_LOGW(TAG, "weather icons unavailable; continuing without artwork");
    }

    /* --- display first, so later failures are visible --- */
    err = display_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "display init failed: %s", esp_err_to_name(err));
        ESP_LOGE(TAG, "check board_pins.h against the Elecrow schematic");
        /* Keep going headless -- the serial log is still useful for the
         * Milestone 2 UDP test, which does not need a working panel. */
    } else {
        if (display_lock(-1)) {
            ui_init();
            lv_timer_create(ui_timer_cb, UI_TICK_PERIOD_MS, NULL);
            display_unlock();
        }
    }

    /* --- network: not fatal --- */
    err = net_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "network not up yet (%s); retrying in background",
                 esp_err_to_name(err));
    }

#if CONFIG_DIAG_ON_BOOT
    diag_report_network();
#endif

#if CONFIG_OTA_ENABLED
    ota_start();
#endif

    /* --- live data --- */
    ESP_ERROR_CHECK(tempest_udp_start());

    /* --- forecast: the only cloud feed ---
     * Backs off and retries on its own, so it does not wait on the network
     * being up right now. It fails independently by design: losing the
     * internet must not take the local UDP readings down with it. */
    tempest_rest_start();

    /* After display_init(), which owns the shared I2C bus. Not fatal:
     * a missing sensor just leaves the indoor gauge empty. */
    indoor_start();

    /* --- health log, and the Milestone 2 evidence trail ---
     * If packet_count stays at 0 while Wi-Fi is connected, the C6 is not
     * forwarding broadcast frames. See docs/roadmap.md Milestone 2. */
    /* Rollback gate. Reaching here means NVS, the state layer, the display and
     * the UDP listener all came up, so a freshly flashed image has proved
     * itself enough to keep. An image that crashes before this point reverts
     * on the next reset instead of stranding a wall-mounted panel. */
    ota_mark_valid();

    uint32_t last_count = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000));

        uint32_t count = tempest_udp_packet_count();
        ESP_LOGI(TAG, "udp packets: %lu (+%lu)  wifi: %s  heap: %u/%u",
                 (unsigned long)count,
                 (unsigned long)(count - last_count),
                 net_is_connected() ? "up" : "down",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

        if (count == last_count && net_is_connected()) {
            ESP_LOGW(TAG, "no UDP traffic in 30s despite an active network.");
            ESP_LOGW(TAG, "the ESP32-C6 may be dropping broadcast frames --");
            ESP_LOGW(TAG, "see docs/roadmap.md Milestone 2 for the fallbacks.");
        }
        last_count = count;
    }
}
