#include "ota_health.h"
#include "esp_ota_ops.h"
#include "esp_log.h"
static const char *TAG = "ota";
void ota_mark_valid(void) {
    const esp_partition_t *p = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (p && esp_ota_get_state_partition(p, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        if (esp_ota_mark_app_valid_cancel_rollback() != ESP_OK)
            ESP_LOGE(TAG, "could not accept image; rollback remains armed");
    }
}
void ota_reject_pending(void) {
    const esp_partition_t *p = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (p && esp_ota_get_state_partition(p, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY)
        esp_ota_mark_app_invalid_rollback_and_reboot();
}
void ota_validate_boot(bool display_ready, bool ui_ready, bool tick_ready) {
    if (display_ready && ui_ready && tick_ready) ota_mark_valid();
    else ota_reject_pending();
}
