#include "ota.h"
#include "config.h"

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_system.h"
#include "esp_partition.h"
#include "esp_heap_caps.h"

static const char *TAG = "ota";

#define CHUNK_SIZE   4096

static httpd_handle_t s_server;
static volatile bool  s_in_progress;

bool ota_in_progress(void)
{
    return s_in_progress;
}

/* Embedded separately so the browser upload can be regression-tested. */
extern const unsigned char ota_html_start[] asm("_binary_ota_html_start");
extern const unsigned char ota_html_end[] asm("_binary_ota_html_end");
extern const unsigned char ota_js_start[] asm("_binary_ota_js_start");
extern const unsigned char ota_js_end[] asm("_binary_ota_js_end");

static esp_err_t root_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, (const char *)ota_html_start,
                           ota_html_end - ota_html_start - 1);
}

static esp_err_t script_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/javascript");
    return httpd_resp_send(req, (const char *)ota_js_start,
                           ota_js_end - ota_js_start - 1);
}

bool ota_password_ok(httpd_req_t *req)
{
    if (CONFIG_OTA_PASSWORD[0] == '\0') return true;
    char buf[64] = {0};
    return httpd_req_get_hdr_value_str(req, "X-OTA-Password", buf,
                                       sizeof(buf)) == ESP_OK &&
           strcmp(buf, CONFIG_OTA_PASSWORD) == 0;
}

static esp_err_t update_post(httpd_req_t *req)
{
    if (!ota_password_ok(req)) {
        ESP_LOGW(TAG, "upload rejected: bad or missing X-OTA-Password");
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "bad password");
        return ESP_FAIL;
    }
    if (req->content_len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty body");
        return ESP_FAIL;
    }

    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (!target) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "no OTA partition -- is the partition table "
                            "OTA-capable?");
        return ESP_FAIL;
    }
    if (req->content_len > (int)target->size) {
        ESP_LOGE(TAG, "image is %d bytes, slot holds %" PRIu32,
                 req->content_len, target->size);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "image too large");
        return ESP_FAIL;
    }

    ESP_LOGW(TAG, "OTA starting: %d bytes -> %s",
             req->content_len, target->label);
    s_in_progress = true;

    esp_ota_handle_t handle = 0;
    esp_err_t err = esp_ota_begin(target, req->content_len, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        s_in_progress = false;
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "ota_begin failed");
        return ESP_FAIL;
    }

    char *buf = malloc(CHUNK_SIZE);
    if (!buf) {
        esp_ota_abort(handle);
        s_in_progress = false;
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
        return ESP_FAIL;
    }

    int remaining = req->content_len;
    int last_pct = -10;
    int timeouts = 0;

    while (remaining > 0) {
        int want = remaining < CHUNK_SIZE ? remaining : CHUNK_SIZE;
        int got = httpd_req_recv(req, buf, want);
        if (got <= 0) {
            if (got == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 3) {
                continue;
            }
            ESP_LOGE(TAG, "receive failed with %d bytes left", remaining);
            free(buf);
            esp_ota_abort(handle);
            s_in_progress = false;
            return ESP_FAIL;
        }

        timeouts = 0;
        err = esp_ota_write(handle, buf, got);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            free(buf);
            esp_ota_abort(handle);
            s_in_progress = false;
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                "write failed -- not a valid image?");
            return ESP_FAIL;
        }
        remaining -= got;

        int pct = 100 - (remaining * 100 / req->content_len);
        if (pct >= last_pct + 10) {
            ESP_LOGI(TAG, "  %d%%", pct);
            last_pct = pct;
        }
    }
    free(buf);

    err = esp_ota_end(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        s_in_progress = false;
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "image failed validation");
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(target);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_boot_partition failed: %s", esp_err_to_name(err));
        s_in_progress = false;
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "could not set boot partition");
        return ESP_FAIL;
    }

    ESP_LOGW(TAG, "OTA complete; restarting into %s", target->label);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req,
        "<!doctype html><meta http-equiv=refresh content=15>"
        "<body style=\"font:16px system-ui;background:#0b0e13;color:#e8edf2;"
        "text-align:center;padding-top:4rem\">"
        "<p>Update written. Restarting.</p>"
        "<p style=color:#7e8b99>This page reloads in 15 seconds.</p>");

    /* Let the response actually leave before the reset. */
    vTaskDelay(pdMS_TO_TICKS(1200));
    cfg_flush();
    esp_restart();
    return ESP_OK;     /* not reached */
}

static bool s_registered;
static bool s_owns_server;

esp_err_t ota_register(httpd_handle_t server)
{
    if (!server) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_registered) {
        return ESP_OK;
    }

    httpd_uri_t page = { .uri = "/ota", .method = HTTP_GET,
                         .handler = root_get };
    httpd_uri_t upd  = { .uri = "/ota/update", .method = HTTP_POST,
                         .handler = update_post };
    esp_err_t err = httpd_register_uri_handler(server, &page);
    if (err != ESP_OK) {
        return err;
    }
    err = httpd_register_uri_handler(server, &upd);
    if (err != ESP_OK) {
        return err;
    }

    httpd_uri_t script = { .uri = "/ota.js", .method = HTTP_GET,
                           .handler = script_get };
    err = httpd_register_uri_handler(server, &script);
    if (err != ESP_OK) return err;
    s_registered = true;
    s_server = server;

    if (CONFIG_OTA_PASSWORD[0] == '\0') {
        ESP_LOGW(TAG, "OTA upload has NO password — anyone on the LAN can reflash");
    } else {
        ESP_LOGI(TAG, "OTA upload at /ota/update (password required)");
    }
    return ESP_OK;
}

void ota_detach(void)
{
    if (!s_owns_server) {
        s_server = NULL;
        s_registered = false;
    }
}

void ota_stop(void)
{
    if (s_owns_server && s_server) {
        httpd_stop(s_server);
    }
    s_server = NULL;
    s_registered = false;
    s_owns_server = false;
}

esp_err_t ota_start(void)
{
    if (s_owns_server && s_server) {
        return ESP_OK;
    }
    if (s_server && !s_owns_server) {
        return ESP_ERR_INVALID_STATE;
    }

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    cfg.stack_size       = 8192;
    cfg.recv_wait_timeout = 30;
    cfg.send_wait_timeout = 30;

    esp_err_t err = httpd_start(&s_server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        s_server = NULL;
        return err;
    }
    s_owns_server = true;

    err = ota_register(s_server);
    if (err != ESP_OK) {
        httpd_stop(s_server);
        s_server = NULL;
        s_owns_server = false;
        return err;
    }

    ESP_LOGI(TAG, "standalone OTA server on port 80");
    return ESP_OK;
}
