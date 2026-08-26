#include "display.h"
#include "board_pins.h"

#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_ek79007.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lvgl_port.h"

static const char *TAG = "display";

/* 16 bpp: 1024 * 600 * 2 = 1.2 MB per full framebuffer. With 32 MB of PSRAM
 * two full buffers are affordable and double buffering is worth it for the
 * wind dial animation. */
#define LCD_BIT_PER_PIXEL       16
#define LVGL_BUF_HEIGHT         (BOARD_LCD_V_RES / 4)
#define LVGL_TASK_STACK         8192
#define LVGL_TASK_PRIORITY      2

#define BACKLIGHT_LEDC_TIMER    LEDC_TIMER_0
#define BACKLIGHT_LEDC_CHANNEL  LEDC_CHANNEL_0
#define BACKLIGHT_DUTY_RES      LEDC_TIMER_10_BIT
#define BACKLIGHT_FREQ_HZ       5000

static esp_lcd_panel_handle_t     s_panel;
static esp_lcd_touch_handle_t     s_touch;
static i2c_master_bus_handle_t    s_i2c;
static lv_display_t              *s_disp;
static bool                       s_backlight_ready;

/* ------------------------------------------------------------------------ */

static esp_err_t init_backlight(void)
{
    if (BOARD_LCD_BACKLIGHT_GPIO < 0) {
        ESP_LOGW(TAG, "no backlight pin configured; brightness control disabled");
        return ESP_OK;
    }

    ledc_timer_config_t timer = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = BACKLIGHT_LEDC_TIMER,
        .duty_resolution = BACKLIGHT_DUTY_RES,
        .freq_hz         = BACKLIGHT_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "ledc timer");

    ledc_channel_config_t ch = {
        .gpio_num   = BOARD_LCD_BACKLIGHT_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = BACKLIGHT_LEDC_CHANNEL,
        .timer_sel  = BACKLIGHT_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&ch), TAG, "ledc channel");

    s_backlight_ready = true;
    return ESP_OK;
}

void display_set_brightness(int percent)
{
    if (!s_backlight_ready) {
        return;
    }
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;

    uint32_t max_duty = (1u << BACKLIGHT_DUTY_RES) - 1;
    uint32_t duty = (max_duty * percent) / 100;
#if BOARD_LCD_BACKLIGHT_ON == 0
    duty = max_duty - duty;     /* active-low backlight */
#endif
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BACKLIGHT_LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BACKLIGHT_LEDC_CHANNEL);
}

/* ------------------------------------------------------------------------ */

static esp_err_t init_panel(void)
{
    /* Bus, IO and DPI timing all come from the esp_lcd_ek79007 component's own
     * macros rather than hand-entered numbers. The component ships the correct
     * 1024x600 configuration for this panel; the values in board_pins.h are a
     * reference for cross-checking against the Elecrow schematic only.
     *
     * If the panel comes up scrambled or shifted, the macro names below are the
     * first place to look -- open the component header and match them to the
     * variant Elecrow actually fitted. */
    esp_lcd_dsi_bus_handle_t dsi_bus = NULL;
    esp_lcd_dsi_bus_config_t bus_config = EK79007_PANEL_BUS_DSI_2CH_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_config, &dsi_bus),
                        TAG, "dsi bus");

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_dbi_io_config_t dbi_config = EK79007_PANEL_IO_DBI_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_config, &io),
                        TAG, "dbi io");

    esp_lcd_dpi_panel_config_t dpi_config =
        EK79007_1024_600_PANEL_60HZ_CONFIG(LCD_COLOR_PIXEL_FORMAT_RGB565);

    ek79007_vendor_config_t vendor_config = {
        .mipi_config = {
            .dsi_bus    = dsi_bus,
            .dpi_config = &dpi_config,
        },
    };

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = BOARD_LCD_RESET_GPIO,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BIT_PER_PIXEL,
        .vendor_config  = &vendor_config,
    };

    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ek79007(io, &panel_config, &s_panel),
                        TAG, "ek79007 panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel init");

    ESP_LOGI(TAG, "EK79007 up at %dx%d", BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    return ESP_OK;
}

static esp_err_t init_touch(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port          = BOARD_I2C_PORT,
        .sda_io_num        = BOARD_I2C_SDA_GPIO,
        .scl_io_num        = BOARD_I2C_SCL_GPIO,
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_i2c), TAG, "i2c bus");

    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    tp_io_cfg.scl_speed_hz = BOARD_I2C_FREQ_HZ;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(s_i2c, &tp_io_cfg, &tp_io),
                        TAG, "touch io");

    esp_lcd_touch_config_t tp_cfg = {
        .x_max        = BOARD_LCD_H_RES,
        .y_max        = BOARD_LCD_V_RES,
        .rst_gpio_num = BOARD_TOUCH_RST_GPIO,
        .int_gpio_num = BOARD_TOUCH_INT_GPIO,
        .levels = {
            .reset     = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy  = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };

    esp_err_t err = esp_lcd_touch_new_i2c_gt911(tp_io, &tp_cfg, &s_touch);
    if (err != ESP_OK) {
        /* A dead touch controller should not stop the weather from showing.
         * Log it and carry on read-only. */
        ESP_LOGW(TAG, "GT911 init failed (%s); continuing without touch",
                 esp_err_to_name(err));
        s_touch = NULL;
    }
    return ESP_OK;
}

static esp_err_t init_lvgl(void)
{
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = LVGL_TASK_PRIORITY;
    port_cfg.task_stack    = LVGL_TASK_STACK;
    port_cfg.timer_period_ms = 5;
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port_cfg), TAG, "lvgl port");

    lvgl_port_display_cfg_t disp_cfg = {
        .panel_handle = s_panel,
        .buffer_size  = BOARD_LCD_H_RES * LVGL_BUF_HEIGHT,
        .double_buffer = true,
        .hres         = BOARD_LCD_H_RES,
        .vres         = BOARD_LCD_V_RES,
        .monochrome   = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy  = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma   = false,
            .buff_spiram = true,
        },
    };

    lvgl_port_display_dsi_cfg_t dsi_cfg = {
        .flags = {
            .avoid_tearing = true,
        },
    };

    s_disp = lvgl_port_add_disp_dsi(&disp_cfg, &dsi_cfg);
    ESP_RETURN_ON_FALSE(s_disp, ESP_FAIL, TAG, "lvgl display");

    if (s_touch) {
        lvgl_port_touch_cfg_t touch_cfg = {
            .disp   = s_disp,
            .handle = s_touch,
        };
        ESP_RETURN_ON_FALSE(lvgl_port_add_touch(&touch_cfg), ESP_FAIL,
                            TAG, "lvgl touch");
    }
    return ESP_OK;
}

esp_err_t display_init(void)
{
    ESP_RETURN_ON_ERROR(init_backlight(), TAG, "backlight");
    ESP_RETURN_ON_ERROR(init_panel(),     TAG, "panel");
    ESP_RETURN_ON_ERROR(init_touch(),     TAG, "touch");
    ESP_RETURN_ON_ERROR(init_lvgl(),      TAG, "lvgl");

    /* Turn the light on only after there is something to look at. */
    display_set_brightness(100);
    return ESP_OK;
}

bool display_lock(int timeout_ms)
{
    return lvgl_port_lock(timeout_ms);
}

void display_unlock(void)
{
    lvgl_port_unlock();
}
