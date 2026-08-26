#include "wifi_setup.h"
#include "config.h"
#include "net.h"
#include "display.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "wifi_ui";

#define COL_BG        lv_color_hex(0x0B0E13)
#define COL_CARD      lv_color_hex(0x151A22)
#define COL_TEXT      lv_color_hex(0xE8EDF2)
#define COL_DIM       lv_color_hex(0x7E8B99)
#define COL_ACCENT    lv_color_hex(0x4FC3F7)
#define COL_OK        lv_color_hex(0x66BB6A)
#define COL_ALERT     lv_color_hex(0xEF5350)

#define MAX_APS       24
/* Generous on purpose: this task builds LVGL widgets, and widget creation
 * with layout and text rendering is not cheap in stack. At 4 KB it faulted
 * partway through populating a 14-row list. */
#define SCAN_STACK    8192

static lv_obj_t *s_screen;
static lv_obj_t *s_prev;
static lv_obj_t *s_list;
static lv_obj_t *s_status;
static lv_obj_t *s_scan_btn;
static lv_obj_t *s_pass_ta;
static lv_obj_t *s_keyboard;
static lv_obj_t *s_connect_btn;
static lv_obj_t *s_chosen_lbl;

static char       s_chosen_ssid[CFG_SSID_LEN];
static net_ap_t   s_aps[MAX_APS];
static int        s_ap_count;
static volatile bool s_scanning;    /* task in flight */
static volatile bool s_scan_ready;  /* results awaiting draw */
static volatile int  s_scan_result;

/* ------------------------------------------------------------------------ */

static void on_connect(lv_event_t *e);
static void on_kb_ready(lv_event_t *e);

static void set_status(const char *text, lv_color_t colour)
{
    lv_label_set_text(s_status, text);
    lv_obj_set_style_text_color(s_status, colour, 0);
}

/* Signal strength as something a person can act on, rather than a dBm number
 * that means nothing to most people. */
static const char *signal_words(int8_t rssi)
{
    if (rssi >= -55) return "excellent";
    if (rssi >= -67) return "good";
    if (rssi >= -75) return "fair";
    return "weak";
}

static void on_ap_clicked(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    const char *ssid = (const char *)lv_event_get_user_data(e);

    strncpy(s_chosen_ssid, ssid, sizeof(s_chosen_ssid) - 1);
    s_chosen_ssid[sizeof(s_chosen_ssid) - 1] = '\0';
    (void)btn;

    ESP_LOGI(TAG, "network selected: %s", s_chosen_ssid);

    lv_label_set_text_fmt(s_chosen_lbl, "Password for %s", s_chosen_ssid);
    lv_textarea_set_text(s_pass_ta, "");
    /* The keyboard is always on screen, so there is nothing to reveal -- just
     * point it at the field and put the cursor there. Hiding it and relying on
     * a row tap to unhide meant one missed hit-test left no way to type. */
    lv_keyboard_set_textarea(s_keyboard, s_pass_ta);
    set_status("enter the password, then Connect", COL_DIM);
}

/* The scan task does NOT touch LVGL.
 *
 * It used to build the network list itself while holding the port lock, and
 * that crashed reliably inside lv_obj_invalidate() with a NULL dereference
 * (MTVAL 0x8). Creating widgets from a foreign task is not worth debugging
 * when the fix is simple: fetch the data here, set a flag, and let the LVGL
 * task draw it from its own context in wifi_setup_tick().
 *
 * The cost is up to one tick (1 s) of latency between the scan finishing and
 * the list appearing, which is invisible next to the ~3 s scan itself. */
static void scan_task(void *arg)
{
    (void)arg;
    s_scan_result = net_scan(s_aps, MAX_APS);
    s_scan_ready  = true;
    s_scanning    = false;
    vTaskDelete(NULL);
}

/* Runs in the LVGL task. */
static void publish_scan_results(void)
{
    int n = s_scan_result;

    lv_obj_clean(s_list);
    s_ap_count = (n > 0) ? n : 0;

    if (n < 0) {
        set_status("scan failed -- is the radio up?", COL_ALERT);
    } else if (n == 0) {
        set_status("no networks found", COL_ALERT);
    } else {
        for (int i = 0; i < n; i++) {
            char row[96];
            snprintf(row, sizeof(row), "%s  %s   (%s%s)",
                     s_aps[i].secure ? LV_SYMBOL_WIFI : LV_SYMBOL_EYE_OPEN,
                     s_aps[i].ssid, signal_words(s_aps[i].rssi),
                     s_aps[i].secure ? "" : ", open");
            /* Plain button rather than lv_list_add_button(): that helper
             * builds an lv_image for the icon and sets its label to
             * LV_LABEL_LONG_SCROLL_CIRCULAR, so a 20-network list would leave
             * 20 infinite scroll animations running forever on a core that
             * also has a weather panel to draw. */
            lv_obj_t *btn = lv_button_create(s_list);
            /* LVGL returns NULL when it cannot allocate, and dereferencing
             * that faults deep inside lv_obj_invalidate() where nothing hints
             * at memory. Stop cleanly and say so instead. */
            if (!btn) {
                ESP_LOGE(TAG, "out of LVGL memory after %d rows", i);
                set_status("too many networks to list", COL_ALERT);
                break;
            }
            lv_obj_set_width(btn, LV_PCT(100));
            lv_obj_set_height(btn, 46);
            lv_obj_set_style_bg_color(btn, COL_BG, 0);
            lv_obj_set_style_radius(btn, 8, 0);

            lv_obj_t *lbl = lv_label_create(btn);
            if (lbl) {
                lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
                lv_label_set_text(lbl, row);
                lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
                lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
                lv_obj_set_width(lbl, LV_PCT(100));
                lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);
            }
            /* s_aps is static and outlives the buttons. */
            lv_obj_add_event_cb(btn, on_ap_clicked, LV_EVENT_CLICKED,
                                s_aps[i].ssid);
        }
        set_status("pick a network", COL_DIM);
    }
    lv_obj_clear_state(s_scan_btn, LV_STATE_DISABLED);
}

static void on_scan(lv_event_t *e)
{
    (void)e;
    if (s_scanning) {
        return;
    }
    s_scanning   = true;
    s_scan_ready = false;
    lv_obj_add_state(s_scan_btn, LV_STATE_DISABLED);
    set_status("scanning...", COL_ACCENT);

    if (xTaskCreate(scan_task, "wifi_scan", SCAN_STACK, NULL, 4, NULL)
            != pdPASS) {
        s_scanning = false;
        lv_obj_clear_state(s_scan_btn, LV_STATE_DISABLED);
        set_status("could not start scan", COL_ALERT);
    }
}

static void on_connect(lv_event_t *e)
{
    (void)e;
    if (s_chosen_ssid[0] == '\0') {
        set_status("pick a network first", COL_ALERT);
        return;
    }
    const char *pass = lv_textarea_get_text(s_pass_ta);

    esp_err_t err = net_apply_credentials(s_chosen_ssid, pass);
    if (err == ESP_OK) {
        lv_label_set_text_fmt(s_status, "connecting to %s...", s_chosen_ssid);
        lv_obj_set_style_text_color(s_status, COL_ACCENT, 0);
    } else {
        set_status("could not apply credentials", COL_ALERT);
        ESP_LOGE(TAG, "net_apply_credentials: %s", esp_err_to_name(err));
    }
}

static void on_kb_ready(lv_event_t *e)
{
    on_connect(e);
}

static void on_back(lv_event_t *e)
{
    (void)e;
    wifi_setup_hide();
}

/* ------------------------------------------------------------------------ */

static lv_obj_t *make_button(lv_obj_t *parent, int x, int y, int w, int h,
                             const char *text, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, COL_CARD, 0);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_obj_center(l);
    return b;
}

esp_err_t wifi_setup_init(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, COL_BG, 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(s_screen);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, COL_TEXT, 0);
    lv_label_set_text(title, "Wi-Fi");
    lv_obj_set_pos(title, 24, 18);

    make_button(s_screen, 1024 - 150 - 24, 14, 150, 50,
                LV_SYMBOL_LEFT "  Done", on_back);
    s_scan_btn = make_button(s_screen, 150, 16, 130, 46, "Scan", on_scan);

    s_status = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_status, COL_DIM, 0);
    lv_label_set_text(s_status, "");
    lv_obj_set_pos(s_status, 300, 30);

    /* Left: the network list. Right: password entry. */
    s_list = lv_list_create(s_screen);
    lv_obj_set_pos(s_list, 24, 78);
    lv_obj_set_size(s_list, 470, 480);
    lv_obj_set_style_bg_color(s_list, COL_CARD, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_radius(s_list, 12, 0);

    s_chosen_lbl = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_chosen_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_chosen_lbl, COL_TEXT, 0);
    lv_label_set_text(s_chosen_lbl, "Tap a network, then type its password");
    lv_obj_set_pos(s_chosen_lbl, 516, 84);

    s_pass_ta = lv_textarea_create(s_screen);
    lv_obj_set_pos(s_pass_ta, 516, 116);
    lv_obj_set_size(s_pass_ta, 484, 52);
    lv_textarea_set_one_line(s_pass_ta, true);
    lv_textarea_set_password_mode(s_pass_ta, true);
    lv_textarea_set_placeholder_text(s_pass_ta,
                                    "password (leave blank if open)");
    lv_obj_set_style_text_font(s_pass_ta, &lv_font_montserrat_18, 0);

    s_connect_btn = make_button(s_screen, 516, 180, 160, 48,
                                "Connect", on_connect);
    lv_obj_set_style_bg_color(s_connect_btn, COL_ACCENT, 0);

    s_keyboard = lv_keyboard_create(s_screen);
    lv_obj_set_size(s_keyboard, 484, 318);
    /* lv_keyboard_create() aligns itself LV_ALIGN_BOTTOM_MID by default, and
     * LVGL treats lv_obj_set_pos() on an ALIGNED object as an offset from that
     * alignment rather than an absolute position. Setting (516,240) therefore
     * put it at (786,522) -- almost entirely off a 1024x600 panel, which looks
     * exactly like the keyboard never being created at all.
     *
     * lv_obj_align() with an explicit TOP_LEFT sets the anchor and the offset
     * together, so the numbers mean what they say. */
    lv_obj_align(s_keyboard, LV_ALIGN_TOP_LEFT, 516, 240);
    lv_keyboard_set_textarea(s_keyboard, s_pass_ta);
    /* The tick on the keyboard connects, so the whole flow can be done from
     * the keyboard without reaching for the button. */
    lv_obj_add_event_cb(s_keyboard, on_kb_ready, LV_EVENT_READY, NULL);

    /* Report what actually got built. A widget that is NULL, zero-sized or
     * positioned off-panel all look identical from the front. */
    lv_obj_update_layout(s_screen);
    ESP_LOGI(TAG, "widgets: kb=%p ta=%p connect=%p list=%p",
             s_keyboard, s_pass_ta, s_connect_btn, s_list);
    if (s_keyboard) {
        ESP_LOGI(TAG, "keyboard: x=%d y=%d w=%d h=%d hidden=%d",
                 (int)lv_obj_get_x(s_keyboard), (int)lv_obj_get_y(s_keyboard),
                 (int)lv_obj_get_width(s_keyboard),
                 (int)lv_obj_get_height(s_keyboard),
                 lv_obj_has_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN) ? 1 : 0);
    }
    if (s_pass_ta) {
        ESP_LOGI(TAG, "textarea: x=%d y=%d w=%d h=%d hidden=%d",
                 (int)lv_obj_get_x(s_pass_ta), (int)lv_obj_get_y(s_pass_ta),
                 (int)lv_obj_get_width(s_pass_ta),
                 (int)lv_obj_get_height(s_pass_ta),
                 lv_obj_has_flag(s_pass_ta, LV_OBJ_FLAG_HIDDEN) ? 1 : 0);
    }

    ESP_LOGI(TAG, "wi-fi setup screen built");
    return ESP_OK;
}

void wifi_setup_tick(void)
{
    if (!wifi_setup_is_visible()) {
        return;
    }
    /* Draw any pending scan results here, in the LVGL task. */
    if (s_scan_ready) {
        s_scan_ready = false;
        publish_scan_results();
        return;
    }
    if (s_scanning) {
        return;
    }
    /* Only overwrite the status line once things have settled, so it does not
     * stamp on "scanning..." or a validation message. */
    if (net_is_connected()) {
        char buf[64];
        snprintf(buf, sizeof(buf), LV_SYMBOL_OK "  connected to %s",
                 net_current_ssid());
        set_status(buf, COL_OK);
    }
}

void wifi_setup_show(void)
{
    if (!s_screen) {
        return;
    }
    s_prev = lv_screen_active();

    if (net_is_connected()) {
        char buf[64];
        snprintf(buf, sizeof(buf), LV_SYMBOL_OK "  connected to %s",
                 net_current_ssid());
        set_status(buf, COL_OK);
    } else if (net_current_ssid()[0]) {
        char buf[64];
        snprintf(buf, sizeof(buf), "not connected (configured: %s)",
                 net_current_ssid());
        set_status(buf, COL_DIM);
    } else {
        set_status("no network configured -- tap Scan", COL_DIM);
    }

    lv_screen_load(s_screen);
}

void wifi_setup_hide(void)
{
    if (s_prev) {
        lv_screen_load(s_prev);
    }
}

bool wifi_setup_is_visible(void)
{
    return s_screen && lv_screen_active() == s_screen;
}
