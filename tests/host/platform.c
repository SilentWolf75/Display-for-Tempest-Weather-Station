#include "test_platform.h"
#include <stdlib.h>
#include <string.h>
#include "nvs.h"
#include "wx_astronomy.h"
time_t test_now;
int test_fail_alloc;
static unsigned char saved[4096];
static size_t saved_len;
time_t test_time(time_t *out) { if (out) *out = test_now; return test_now; }
int64_t esp_timer_get_time(void) { return (int64_t)test_now * 1000000; }
void *test_calloc(size_t n,size_t s) {
    if (test_fail_alloc) { test_fail_alloc = 0; return NULL; }
    return calloc(n,s);
}
esp_err_t nvs_open(const char *ns,int mode,nvs_handle_t *h) { *h=1; return ESP_OK; }
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *n) {
    if (!saved_len || *n < saved_len) return ESP_FAIL;
    memcpy(out,saved,saved_len); *n=saved_len; return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *in,size_t n) {
    if (n > sizeof(saved)) return ESP_FAIL;
    memcpy(saved,in,n); saved_len=n; return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) { return ESP_OK; }
void nvs_close(nvs_handle_t h) {}
void ui_notify_forecast_updated(void) {}
void wx_moon_compute(int64_t epoch,wx_moon_info_t *out) { memset(out,0,sizeof(*out)); }
