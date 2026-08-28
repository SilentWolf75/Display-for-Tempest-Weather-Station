#pragma once
/*
 * Elecrow CrowPanel Advance 10.1" (ESP32-P4) pin map.
 *
 * VERIFIED against Elecrow's own board configuration:
 *   Elecrow-RD/CrowPanel-Advanced-10.1inch-ESP32-P4-HMI-AI-Display-1024x600-
 *   IPS-Touch-Screen, branch master,
 *   example/V1.0/Arduino_Code/Lesson05-Touchscreen/esp_panel_board_custom_conf.h
 *
 * Confirmed on real hardware 2026-08-26 (chip rev v1.3, MAC e8:f6:0a:e3:f5:e2).
 *
 * The earlier placeholders in this file were wrong in three ways that all
 * produced silent failures rather than errors, so they are recorded here as a
 * warning against guessing:
 *
 *   - I2C was assumed GPIO 7/8 (from the Mjrovai lessons). It is 45/46. The
 *     bus scan simply found nothing.
 *   - The backlight was assumed to be GPIO 48. There is no software backlight
 *     control on this board at all.
 *   - The DSI lane rate was assumed to be the esp_lcd_ek79007 component
 *     default of 900 Mbps. Elecrow drives it at 1000, and the mismatch hung
 *     esp_lcd_panel_init() hard enough to trip the task watchdog forever.
 */

#define BOARD_PINS_VERIFIED 1

/* ---------------------------- Panel -------------------------------------- */

#define BOARD_LCD_H_RES             1024
#define BOARD_LCD_V_RES             600

/* EK79007 over MIPI-DSI. Two lanes at 1000 Mbps -- NOT the component macro's
 * 900. Overridden explicitly in display.c. */
#define BOARD_MIPI_DSI_LANES        2
#define BOARD_MIPI_DSI_LANE_MBPS    1000
#define BOARD_MIPI_DPI_CLK_MHZ      52

/* Matches EK79007_1024_600_PANEL_60HZ_CONFIG() exactly, which is why the
 * component macro is still used for the DPI config. */
#define BOARD_LCD_HSYNC_PULSE       10
#define BOARD_LCD_HBP               160
#define BOARD_LCD_HFP               160
#define BOARD_LCD_VSYNC_PULSE       1
#define BOARD_LCD_VBP               23
#define BOARD_LCD_VFP               12

/* No panel reset line is wired. */
#define BOARD_LCD_RESET_GPIO        (-1)

/* Backlight: GPIO 31, PWM. From Elecrow's own bsp_illuminate.c
 * (LCD_GPIO_BLIGHT 31, BLIGHT_PWM_Hz 30000, 11-bit duty).
 *
 * Their Arduino-side esp_panel_board_custom_conf.h says USE_BACKLIGHT(0) with
 * BACKLIGHT_IO(38), which is misleading: that library simply does not manage
 * the backlight, and 38 is a leftover template default. The ESP-IDF BSP is the
 * one that tells the truth.
 *
 * With this wrong, the panel initialises perfectly and renders into a dark
 * screen -- no error, nothing in the log. */
#define BOARD_LCD_BACKLIGHT_GPIO    31
#define BOARD_LCD_BACKLIGHT_ON      1
#define BOARD_LCD_BACKLIGHT_FREQ_HZ 30000

/* ---------------------------- Touch -------------------------------------- */

/* GT911. This bus is shared with the Grove/Crowtail header, which is where the
 * indoor temperature sensor lives -- do not assume exclusive access. */
#define BOARD_I2C_PORT              0
/* microSD (TF) socket, SDMMC slot 0, 1-bit.
 *
 * From Elecrow's V1.2 board notes: "GPIO43=CLK, GPIO44=CMD, and GPIO39=D0;
 * SDMMC_HOST slot 0, 1-bit, 10 MHz, with internal pull-ups." D1/D2/D3 are not
 * wired in 1-bit mode, and there is NO card-detect line -- removal is only
 * discovered when a file operation fails.
 *
 * These do not collide with the C6's SDIO link (GPIO 15-19 + 32) or the shared
 * I2C bus below.
 */
#define BOARD_SD_CLK_GPIO           43
#define BOARD_SD_CMD_GPIO           44
#define BOARD_SD_D0_GPIO            39
#define BOARD_SD_FREQ_KHZ           10000

#define BOARD_I2C_SDA_GPIO          45
#define BOARD_I2C_SCL_GPIO          46
#define BOARD_I2C_FREQ_HZ           100000

#define BOARD_TOUCH_RST_GPIO        40
#define BOARD_TOUCH_RST_LEVEL       0       /* active low */
#define BOARD_TOUCH_INT_GPIO        42
#define BOARD_TOUCH_INT_LEVEL       0       /* active low */

/* GT911 straps to 0x5D or 0x14 depending on the INT pin at power-up; the
 * driver probes both, and Elecrow's config leaves the address at "default". */
#define BOARD_TOUCH_I2C_ADDR        0x14

/* ---------------------------- Audio / Speaker ---------------------------- */

/* NS4168 Class-D I2S Audio Amplifier & Stereo Speakers */
#define BOARD_AUDIO_PA_GPIO         30      /* PA_EN / Amplifier power enable */
#define BOARD_AUDIO_I2S_PORT        I2S_NUM_1
#define BOARD_AUDIO_I2S_WS          21      /* Word Select / LRCLK */
#define BOARD_AUDIO_I2S_BCLK        22      /* Bit Clock */
#define BOARD_AUDIO_I2S_DOUT        23      /* Data Out */
#define BOARD_AUDIO_SAMPLE_RATE     44100
