#include "diag.h"
#include "board_pins.h"

#include <inttypes.h>
#include <string.h>

#include "esp_log.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "esp_mac.h"
#include "driver/i2c_master.h"
#if CONFIG_TEMPEST_NETWORK_ENABLED
#include "esp_wifi.h"
#endif

static const char *TAG = "diag";

/* Parts we expect to find, so the scan output reads as a verdict rather than
 * a list of numbers to go and look up. */
typedef struct {
    uint8_t     addr;
    const char *name;
} known_device_t;

static const known_device_t KNOWN[] = {
    { 0x14, "GT911 touch (alternate address)" },
    { 0x5D, "GT911 touch (default address)"   },
    { 0x38, "DHT20 / AHT20 temp-humidity"     },
    { 0x40, "HTU21 / Si7021 temp-humidity"    },
    { 0x44, "SHT3x/SHT4x temp-humidity"       },
    { 0x76, "BMP/BME280 pressure"             },
    { 0x77, "BMP/BME280 pressure (alt)"       },
};

static const char *identify(uint8_t addr)
{
    for (size_t i = 0; i < sizeof(KNOWN) / sizeof(KNOWN[0]); i++) {
        if (KNOWN[i].addr == addr) {
            return KNOWN[i].name;
        }
    }
    return NULL;
}

int diag_i2c_scan(void)
{
    ESP_LOGI(TAG, "--- I2C scan: SDA=GPIO%d SCL=GPIO%d @ %d Hz ---",
             BOARD_I2C_SDA_GPIO, BOARD_I2C_SCL_GPIO, BOARD_I2C_FREQ_HZ);

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port          = BOARD_I2C_PORT,
        .sda_io_num        = BOARD_I2C_SDA_GPIO,
        .scl_io_num        = BOARD_I2C_SCL_GPIO,
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    i2c_master_bus_handle_t bus = NULL;
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cannot create I2C bus: %s", esp_err_to_name(err));
        ESP_LOGE(TAG, "the SDA/SCL pins in board_pins.h are probably wrong");
        return -1;
    }

    int found = 0;
    /* 0x08..0x77 is the usable 7-bit range; the rest is reserved. */
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        if (i2c_master_probe(bus, addr, 50) != ESP_OK) {
            continue;
        }
        found++;
        const char *name = identify(addr);
        if (name) {
            ESP_LOGI(TAG, "  0x%02X  %s", addr, name);
        } else {
            ESP_LOGI(TAG, "  0x%02X  (unrecognised)", addr);
        }
    }

    i2c_del_master_bus(bus);

    if (found == 0) {
        ESP_LOGW(TAG, "  nothing responded.");
        ESP_LOGW(TAG, "  Either the pins are wrong, or the bus has no pull-ups");
        ESP_LOGW(TAG, "  on this board. Check the Elecrow schematic before");
        ESP_LOGW(TAG, "  assuming the touch controller is dead.");
    } else {
        ESP_LOGI(TAG, "  %d device(s) found", found);
    }
    return found;
}

void diag_report_hardware(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);

    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    ESP_LOGI(TAG, "--- hardware ---");
    ESP_LOGI(TAG, "  chip      : %s, %d core(s), silicon rev %d.%d",
             CONFIG_IDF_TARGET, chip.cores,
             chip.revision / 100, chip.revision % 100);
    ESP_LOGI(TAG, "  flash     : %" PRIu32 " MB", flash_size / (1024 * 1024));
    ESP_LOGI(TAG, "  psram     : %u KB free of %u KB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
             (unsigned)(heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024));
    ESP_LOGI(TAG, "  internal  : %u KB free",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
    ESP_LOGI(TAG, "  sta mac   : %02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    const char *reason;
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  reason = "power on";            break;
    case ESP_RST_SW:       reason = "software restart";    break;
    case ESP_RST_PANIC:    reason = "PANIC (check the backtrace above)"; break;
    case ESP_RST_INT_WDT:  reason = "interrupt watchdog";  break;
    case ESP_RST_TASK_WDT: reason = "task watchdog";       break;
    case ESP_RST_WDT:      reason = "other watchdog";      break;
    case ESP_RST_BROWNOUT: reason = "BROWNOUT -- use a 5V/2A supply, not a PC port";
                           break;
    default:               reason = "other";               break;
    }
    ESP_LOGI(TAG, "  last reset: %s", reason);

    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running) {
        ESP_LOGI(TAG, "  running   : %s at 0x%06" PRIx32 " (%" PRIu32 " KB)",
                 running->label, running->address, running->size / 1024);
    }

    esp_ota_img_states_t state;
    if (running && esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGW(TAG, "  image is PENDING_VERIFY -- it will roll back unless");
        ESP_LOGW(TAG, "  it is marked valid. See ota.c.");
    }

    const esp_partition_t *storage = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "storage");
    ESP_LOGI(TAG, "  icons     : %s",
             storage ? "storage partition present" : "MISSING -- reflash");
}

void diag_report_network(void)
{
#if !CONFIG_TEMPEST_NETWORK_ENABLED
    ESP_LOGI(TAG, "--- network: compiled out ---");
#else
    ESP_LOGI(TAG, "--- network (ESP32-C6 over SDIO) ---");

    wifi_ap_record_t ap;
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "  associated: %s", (const char *)ap.ssid);
        ESP_LOGI(TAG, "  rssi      : %d dBm, channel %d", ap.rssi, ap.primary);
    } else {
        ESP_LOGW(TAG, "  not associated (%s)", esp_err_to_name(err));
        ESP_LOGW(TAG, "  If esp_wifi_init() itself failed earlier, the");
        ESP_LOGW(TAG, "  ESP-Hosted link to the C6 is not up -- that is a");
        ESP_LOGW(TAG, "  firmware version mismatch, not a Wi-Fi problem.");
    }
#endif
}

void diag_run_all(void)
{
    diag_report_hardware();
    diag_i2c_scan();
}
