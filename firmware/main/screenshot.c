#include "screenshot.h"
#include "display.h"

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "esp_vfs_dev.h"
#include "lvgl.h"
#include "others/snapshot/lv_snapshot.h"

static const char *TAG = "screenshot";

void screenshot_dump(void)
{
    ESP_LOGI(TAG, "capturing screen snapshot...");

    if (!display_lock(2000)) {
        ESP_LOGE(TAG, "failed to acquire display lock for screenshot");
        return;
    }

    lv_obj_t *scr = lv_screen_active();
    if (!scr) {
        ESP_LOGE(TAG, "no active screen found");
        display_unlock();
        return;
    }

    lv_draw_buf_t *draw_buf = lv_snapshot_take(scr, LV_COLOR_FORMAT_RGB565);
    display_unlock();

    if (!draw_buf || !draw_buf->data) {
        ESP_LOGE(TAG, "lv_snapshot_take failed");
        return;
    }

    uint32_t w = draw_buf->header.w;
    uint32_t h = draw_buf->header.h;
    uint32_t num_pixels = w * h;
    const uint16_t *pixels = (const uint16_t *)draw_buf->data;

    ESP_LOGI(TAG, "compressing snapshot with ASCII-RLE (%lux%lu)...", (unsigned long)w, (unsigned long)h);

    /* Output header */
    printf("\n===SCREENSHOT_HEX_START:%lu:%lu===\n", (unsigned long)w, (unsigned long)h);
    fflush(stdout);

    /* RLE encode: count (uint16_t hex), color (uint16_t hex) */
    uint32_t i = 0;
    while (i < num_pixels) {
        uint16_t color = pixels[i];
        uint16_t run_len = 1;
        while ((i + run_len < num_pixels) && (pixels[i + run_len] == color) && (run_len < 65535)) {
            run_len++;
        }
        printf("%04X%04X\n", (unsigned int)run_len, (unsigned int)color);
        i += run_len;
    }
    fflush(stdout);

    printf("===SCREENSHOT_HEX_END===\n");
    fflush(stdout);

    lv_draw_buf_destroy(draw_buf);
    ESP_LOGI(TAG, "screenshot ASCII-RLE transfer complete");
}

static void screenshot_task(void *pvParameters)
{
    (void)pvParameters;
    uint8_t rx_buf[64];
    int idx = 0;

    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_param_config(UART_NUM_0, &uart_config);
    uart_driver_install(UART_NUM_0, 1024, 1024, 0, NULL, 0);
    esp_vfs_dev_uart_use_driver(UART_NUM_0);

    while (1) {
        uint8_t ch = 0;
        int len = uart_read_bytes(UART_NUM_0, &ch, 1, pdMS_TO_TICKS(100));
        if (len > 0) {
            if (ch == '\n' || ch == '\r') {
                rx_buf[idx] = '\0';
                if (idx > 0) {
                    if (strcasecmp((char *)rx_buf, "screenshot") == 0 ||
                        strcasecmp((char *)rx_buf, "capture") == 0) {
                        screenshot_dump();
                    }
                    idx = 0;
                }
            } else if (idx < (int)(sizeof(rx_buf) - 1)) {
                rx_buf[idx++] = ch;
            }
        }
    }
}

esp_err_t screenshot_init(void)
{
    BaseType_t ret = xTaskCreate(screenshot_task, "screenshot", 4096, NULL, 3, NULL);
    return (ret == pdPASS) ? ESP_OK : ESP_FAIL;
}
