#include "graphs.h"
#include "history.h"
#include "config.h"
#include "wx_state.h"
#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "graphs";

#define COL_BG      lv_color_hex(0x05080D)
#define COL_CARD    lv_color_hex(0x0C1118)
#define COL_BORDER  lv_color_hex(0x243044)
#define COL_GRID    lv_color_hex(0x1B2432)
#define COL_TEXT    lv_color_hex(0xF8FAFC)
#define COL_DIM     lv_color_hex(0x94A3B8)

#define COL_TEMP    lv_color_hex(0xFF7043)
#define COL_PRESS   lv_color_hex(0x26C6DA)
#define COL_WIND    lv_color_hex(0x4FC3F7)
#define COL_GUST    lv_color_hex(0xFFB300)
#define COL_HUMID   lv_color_hex(0x9C8CFF)
#define COL_RAIN    lv_color_hex(0x38BDF8)

/* 2x2 grid inside a 1024x600 screen with a 64px header. */
#define CARD_W      492
#define CARD_H      228
#define CARD_X0     14
#define CARD_Y0     66
#define GAP         12

/* One point per bucket. 288 points across ~460 px is under two pixels each,
 * which is as fine as this panel can show anyway. */
#define POINTS      HIST_BUCKETS

#define REDRAW_EVERY_S  30

typedef struct {
    lv_obj_t          *chart;
    lv_chart_series_t *primary;
    lv_chart_series_t *secondary;
    lv_obj_t          *title;
    lv_obj_t          *readout;
    lv_obj_t          *range;
    lv_obj_t          *axis;
    hist_series_t      series;
} panel_t;

static lv_obj_t *s_screen;
static lv_obj_t *s_clock;
static lv_obj_t *s_span_label;
#define GRAPH_PANELS 4
static panel_t   s_panels[GRAPH_PANELS];
static float    *s_scratch;         /* POINTS floats, PSRAM */
static int64_t   s_last_redraw;

/* ------------------------------------------------------------------------ */

static panel_t *build_panel(lv_obj_t *parent, int col, int row,
                            hist_series_t series, const char *title,
                            lv_color_t colour, bool with_secondary)
{
    panel_t *p = &s_panels[row * 2 + col];
    p->series = series;

    int x = CARD_X0 + col * (CARD_W + GAP);
    int y = CARD_Y0 + row * (CARD_H + GAP);

    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, CARD_W, CARD_H);
    lv_obj_set_style_bg_color(card, COL_CARD, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, COL_BORDER, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *accent = lv_obj_create(card);
    lv_obj_remove_style_all(accent);
    lv_obj_set_pos(accent, -12, -12);
    lv_obj_set_size(accent, CARD_W, 2);
    lv_obj_set_style_bg_color(accent, colour, 0);
    lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(accent, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    p->title = lv_label_create(card);
    lv_obj_set_style_text_font(p->title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(p->title, colour, 0);
    lv_obj_set_style_text_letter_space(p->title, 1, 0);
    lv_label_set_text(p->title, title);
    lv_obj_set_pos(p->title, 0, 0);

    if (with_secondary) {
        lv_obj_update_layout(p->title);
        lv_obj_t *slash = lv_label_create(card);
        lv_obj_set_style_text_font(slash, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(slash, COL_DIM, 0);
        lv_obj_set_style_text_letter_space(slash, 1, 0);
        lv_label_set_text(slash, "/");
        lv_obj_align_to(slash, p->title, LV_ALIGN_OUT_RIGHT_MID, 6, 0);

        lv_obj_t *gust = lv_label_create(card);
        lv_obj_set_style_text_font(gust, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(gust, COL_GUST, 0);
        lv_obj_set_style_text_letter_space(gust, 1, 0);
        lv_label_set_text(gust, "GUST");
        lv_obj_align_to(gust, slash, LV_ALIGN_OUT_RIGHT_MID, 6, 0);
    }

    p->readout = lv_label_create(card);
    lv_obj_set_style_text_font(p->readout, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(p->readout, COL_TEXT, 0);
    lv_label_set_text(p->readout, "--");
    lv_obj_align(p->readout, LV_ALIGN_TOP_RIGHT, 0, -2);

    p->range = lv_label_create(card);
    lv_obj_set_style_text_font(p->range, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(p->range, COL_DIM, 0);
    lv_label_set_text(p->range, "");
    lv_obj_set_pos(p->range, 0, 20);

    p->axis = lv_label_create(card);
    lv_obj_set_style_text_font(p->axis, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(p->axis, COL_DIM, 0);
    lv_label_set_text(p->axis, "-24h                         now");
    lv_obj_set_pos(p->axis, 0, CARD_H - 32);
    lv_obj_set_width(p->axis, CARD_W - 24);

    p->chart = lv_chart_create(card);
    lv_obj_set_size(p->chart, CARD_W - 24, CARD_H - 88);
    lv_obj_set_pos(p->chart, 0, 44);
    lv_chart_set_type(p->chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(p->chart, POINTS);
    lv_chart_set_update_mode(p->chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_div_line_count(p->chart, 4, 6);

    lv_obj_set_style_bg_opa(p->chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(p->chart, 0, 0);
    lv_obj_set_style_pad_all(p->chart, 0, 0);
    lv_obj_set_style_line_color(p->chart, COL_GRID, LV_PART_MAIN);
    lv_obj_set_style_line_width(p->chart, 1, LV_PART_MAIN);
    lv_obj_set_style_line_width(p->chart, 2, LV_PART_ITEMS);
    /* No dot per point: at 288 points they merge into a smear. */
    lv_obj_set_style_width(p->chart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_height(p->chart, 0, LV_PART_INDICATOR);
    lv_obj_clear_flag(p->chart, LV_OBJ_FLAG_SCROLLABLE);

    if (with_secondary) {
        /* Gust behind average, so the average stays legible on top. */
        p->secondary = lv_chart_add_series(p->chart, COL_GUST,
                                           LV_CHART_AXIS_PRIMARY_Y);
    }
    p->primary = lv_chart_add_series(p->chart, colour,
                                     LV_CHART_AXIS_PRIMARY_Y);
    return p;
}

esp_err_t graphs_init(void)
{
    s_scratch = heap_caps_malloc(POINTS * sizeof(float), MALLOC_CAP_SPIRAM);
    if (!s_scratch) {
        ESP_LOGE(TAG, "no PSRAM for the plotting scratch buffer");
        return ESP_ERR_NO_MEM;
    }

    /* Full-screen overlay on the dashboard — never a second LVGL screen. */
    s_screen = lv_obj_create(ui_main_screen());
    lv_obj_set_size(s_screen, UI_SCR_W, UI_CONTENT_H);
    lv_obj_set_pos(s_screen, 0, 0);
    lv_obj_set_style_bg_color(s_screen, COL_BG, 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *title = lv_label_create(s_screen);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, COL_TEXT, 0);
    lv_label_set_text(title, "24H");
    lv_obj_set_pos(title, 20, 8);

    s_span_label = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_span_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_span_label, COL_DIM, 0);
    lv_label_set_text(s_span_label, "last 24 hours");
    lv_obj_set_pos(s_span_label, 90, 16);

    s_clock = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_clock, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_clock, COL_TEXT, 0);
    lv_label_set_text(s_clock, "--:--");
    lv_obj_align(s_clock, LV_ALIGN_TOP_RIGHT, -168, 10);

    ui_create_page_dots(s_screen, 20, 40, UI_PAGE_GRAPHS);
    ui_create_page_button(s_screen, LV_ALIGN_TOP_RIGHT, -68, 8, UI_PAGE_GRAPHS);
    ui_create_gear_button(s_screen, LV_ALIGN_TOP_RIGHT, -14, 8);
    ui_attach_swipe_nav(s_screen);

    build_panel(s_screen, 0, 0, HIST_TEMP,     "TEMPERATURE", COL_TEMP,  false);
    build_panel(s_screen, 1, 0, HIST_PRESSURE, "PRESSURE",    COL_PRESS, false);
    build_panel(s_screen, 0, 1, HIST_WIND,     "WIND",        COL_WIND,  true);
    build_panel(s_screen, 1, 1, HIST_RAIN,     "RAIN",        COL_RAIN,  false);

    ESP_LOGI(TAG, "graphs screen built");
    return ESP_OK;
}

/* ------------------------------------------------------------------------ */

static float to_display(hist_series_t series, float si)
{
    switch (series) {
    case HIST_TEMP:     return cfg_temp(si);
    case HIST_PRESSURE: return cfg_pressure(si);
    case HIST_WIND:     return cfg_wind(si);
    case HIST_RAIN:     return cfg_rain(si);
    default:            return si;      /* humidity is unitless */
    }
}

static const char *series_suffix(hist_series_t series)
{
    switch (series) {
    case HIST_TEMP:     return cfg_temp_suffix();
    case HIST_PRESSURE: return cfg_pressure_suffix();
    case HIST_WIND:     return cfg_wind_suffix();
    case HIST_RAIN:     return cfg_rain_suffix();
    default:            return "%";
    }
}

static void redraw_panel(panel_t *p)
{
    float lo_si = 0.0f, hi_si = 0.0f;
    int have = history_get(p->series, s_scratch, POINTS, &lo_si, &hi_si);

    if (have < 2) {
        ui_label_set(p->readout, "--");
        ui_label_set(p->range, "collecting...");
        return;
    }

    float lo = to_display(p->series, lo_si);
    float hi = to_display(p->series, hi_si);
    float yscale = (p->series == HIST_RAIN) ? 100.0f : 1.0f;

    /* Pad the range so a flat line does not sit welded to the axis, and so a
     * one-degree wiggle does not look like a cliff. Rain is hundredths so
     * the line survives lrintf. */
    float pad = (hi - lo) * 0.15f;
    if (pad < 0.5f) {
        pad = 0.5f;
    }
    if (p->series == HIST_RAIN && pad < 0.02f) {
        pad = 0.02f;
    }
    lv_chart_set_range(p->chart, LV_CHART_AXIS_PRIMARY_Y,
                       (int32_t)((lo - pad) * yscale),
                       (int32_t)((hi + pad) * yscale + 0.999f));

    /* Push the primary series. NAN buckets become POINT_NONE, so an offline
     * gap shows as a break rather than a straight line through missing time. */
    int32_t *y = lv_chart_get_y_array(p->chart, p->primary);
    float newest = NAN;
    float oldest = NAN;
    for (int i = 0; i < POINTS; i++) {
        float v = s_scratch[i];
        if (isnan(v)) {
            y[i] = LV_CHART_POINT_NONE;
        } else {
            y[i] = (int32_t)lrintf(to_display(p->series, v) * yscale);
            newest = v;
            if (isnan(oldest)) {
                oldest = v;
            }
        }
    }

    if (p->secondary && history_get_secondary(p->series, s_scratch, POINTS)) {
        int32_t *g = lv_chart_get_y_array(p->chart, p->secondary);
        for (int i = 0; i < POINTS; i++) {
            float v = s_scratch[i];
            g[i] = isnan(v) ? LV_CHART_POINT_NONE
                            : (int32_t)lrintf(to_display(p->series, v));
        }
    }

    lv_chart_refresh(p->chart);

    if (!isnan(newest)) {
        if (p->series == HIST_PRESSURE) {
            wx_state_t wx;
            wx_snapshot(&wx);
            ui_label_setf(p->readout, "%.2f%s  %s",
                          (double)to_display(p->series, newest),
                          series_suffix(p->series),
                          wx_trend_description(wx.pressure_trend));
        } else if (!isnan(oldest)) {
            float d = to_display(p->series, newest) - to_display(p->series, oldest);
            float thresh = (p->series == HIST_RAIN) ? 0.02f : 0.15f;
            const char *dir = (d > thresh) ? "up" : (d < -thresh) ? "down" : "flat";
            const char *fmt = (p->series == HIST_RAIN) ? "%.2f%s  %s %.2f" : "%.1f%s  %s %.1f";
            ui_label_setf(p->readout, fmt,
                          (double)to_display(p->series, newest),
                          series_suffix(p->series),
                          dir, (double)((d < 0.0f) ? -d : d));
        } else {
            ui_label_setf(p->readout, (p->series == HIST_RAIN) ? "%.2f%s" : "%.1f%s",
                          (double)to_display(p->series, newest),
                          series_suffix(p->series));
        }
    }
    if (p->series == HIST_RAIN) {
        ui_label_setf(p->range, "%.2f to %.2f%s   %d h",
                      (double)lo, (double)hi, series_suffix(p->series),
                      have * HIST_BUCKET_S / 3600);
    } else {
        ui_label_setf(p->range, "%.0f to %.0f%s   %d h",
                      (double)lo, (double)hi, series_suffix(p->series),
                      have * HIST_BUCKET_S / 3600);
    }
}

void graphs_tick(void)
{
    if (!graphs_is_visible()) {
        return;
    }
    int64_t now = (int64_t)time(NULL);
    struct tm lt;
    time_t tnow = (time_t)now;
    localtime_r(&tnow, &lt);
    char clk[16];
    strftime(clk, sizeof(clk), "%I:%M %p", &lt);
    if (clk[0] == '0') {
        memmove(clk, clk + 1, strlen(clk));
    }
    ui_label_set(s_clock, clk);

    if (s_last_redraw != 0 && (now - s_last_redraw) < REDRAW_EVERY_S) {
        return;         /* buckets are 5 min wide; 1 Hz redraw is pointless */
    }
    s_last_redraw = now;

    if (!history_is_plottable()) {
        ui_label_set(s_span_label,
                     "history fills over the first 24 hours after boot");
        for (int i = 0; i < GRAPH_PANELS; i++) {
            ui_label_set(s_panels[i].range, "collecting...");
        }
        return;
    }

    int64_t oldest = 0, newest = 0;
    history_span(&oldest, &newest);
    wx_state_t wx;
    wx_snapshot(&wx);
    if (oldest && newest) {
        char a[16], b[16];
        time_t ta = (time_t)oldest, tb = (time_t)newest;
        struct tm sa, sb;
        localtime_r(&ta, &sa);
        localtime_r(&tb, &sb);
        strftime(a, sizeof(a), "%H:%M", &sa);
        strftime(b, sizeof(b), "%H:%M", &sb);
        float rain24 = 0.0f;
        history_get(HIST_RAIN, s_scratch, POINTS, NULL, NULL);
        for (int i = 0; i < POINTS; i++) {
            if (!isnan(s_scratch[i])) {
                rain24 += s_scratch[i];
            }
        }
        char rain_s[16];
        snprintf(rain_s, sizeof(rain_s), cfg_rain_fmt(), (double)cfg_rain(rain24));
        if (wx.obs_valid) {
            ui_label_setf(s_span_label, "%s  to  %s   now %.0f%s   rain %s%s",
                          a, b, (double)cfg_temp(wx.air_temp_c),
                          cfg_temp_suffix(), rain_s, cfg_rain_suffix());
        } else {
            ui_label_setf(s_span_label, "%s  to  %s   rain %s%s",
                          a, b, rain_s, cfg_rain_suffix());
        }
    }

    for (int i = 0; i < GRAPH_PANELS; i++) {
        redraw_panel(&s_panels[i]);
    }
}

void graphs_show(void)
{
    if (!s_screen) {
        return;
    }
    s_last_redraw = 0;          /* force an immediate redraw on entry */
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_screen);
    lv_obj_invalidate(s_screen);
    graphs_tick();
}

void graphs_hide(void)
{
    if (!s_screen) {
        return;
    }
    lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
}

bool graphs_is_visible(void)
{
    return s_screen && !lv_obj_has_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
}

void graphs_request_redraw(void)
{
    /* Called from tempest_rest after history backfill — flag only; graphs_tick()
     * runs on the LVGL thread. */
    s_last_redraw = 0;
}
