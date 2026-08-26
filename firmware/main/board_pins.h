#pragma once
/*
 * Elecrow CrowPanel Advance 10.1" (ESP32-P4) pin map.
 *
 * ############################################################################
 * #  READ THIS BEFORE TRUSTING ANY NUMBER IN THIS FILE                       #
 * #                                                                          #
 * #  Only the CONFIRMED block below comes from a source worth believing.     #
 * #  Everything under UNCONFIRMED is a placeholder. Verify each one against  #
 * #  the Elecrow schematic and their official V1.1/V1.2 example project      #
 * #  before flashing anything that drives a pin, then delete the #warning    #
 * #  at the bottom of this file.                                             #
 * ############################################################################
 */

/* ---------------------------- CONFIRMED ---------------------------------- */

/* I2C bus. Shared by the GT911 touch controller and the Grove/Crowtail header.
 * Source: Mjrovai/CrowPanel-10.1inch lesson 10, which puts a DHT20 on the same
 * bus and warns about the sharing explicitly. */
#define BOARD_I2C_PORT          0
#define BOARD_I2C_SDA_GPIO      7
#define BOARD_I2C_SCL_GPIO      8
#define BOARD_I2C_FREQ_HZ       400000

/* GT911 default 7-bit address. The part answers on 0x5D or 0x14 depending on
 * the INT/RST strap at power-up; the driver probes both. */
#define BOARD_TOUCH_I2C_ADDR    0x5D

/* Panel geometry. Not a guess. */
#define BOARD_LCD_H_RES         1024
#define BOARD_LCD_V_RES         600

/* ---------------------------- UNCONFIRMED -------------------------------- */

/* The Elecrow wiki mentions "LED control on pin 48 via the UART1 interface".
 * That phrasing is ambiguous and may refer to a status LED rather than the
 * panel backlight. Do NOT assume this drives brightness. */
#define BOARD_LCD_BACKLIGHT_GPIO    48
#define BOARD_LCD_BACKLIGHT_ON      1

/* Not documented anywhere I could find. -1 means "not wired / skip". */
#define BOARD_LCD_RESET_GPIO       (-1)
#define BOARD_TOUCH_RST_GPIO       (-1)
#define BOARD_TOUCH_INT_GPIO       (-1)

/* MIPI-DSI. The EK79007 on this panel is a 2-lane part at 1024x600; the lane
 * rate and the DPI timings must be copied from the Elecrow example, not
 * invented. These values are the esp_lcd_ek79007 component defaults and are
 * very likely wrong for this specific panel. */
#define BOARD_MIPI_DSI_LANES        2
#define BOARD_MIPI_DSI_LANE_MBPS    900
#define BOARD_MIPI_DPI_CLK_MHZ      52

#define BOARD_LCD_HSYNC_PULSE       10
#define BOARD_LCD_HBP               160
#define BOARD_LCD_HFP               160
#define BOARD_LCD_VSYNC_PULSE       1
#define BOARD_LCD_VBP               23
#define BOARD_LCD_VFP               12

#if !defined(BOARD_PINS_VERIFIED)
#warning "board_pins.h contains UNVERIFIED pin assignments and panel timings. \
Confirm against the Elecrow schematic before driving hardware, then define \
BOARD_PINS_VERIFIED to silence this."
#endif
