#pragma once
/*
 * Bring-up diagnostics.
 *
 * Exists because `board_pins.h` is guesswork until the hardware is in hand.
 * These run before the display driver, so they still produce useful output on
 * the serial console when the panel itself refuses to initialise -- which is
 * the most likely first-boot outcome.
 *
 * See docs/bringup.md for the checklist these feed.
 */

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* Probes every address on the configured I2C bus and logs what answers,
 * naming the parts we expect. Safe to call before display_init(); it creates
 * and destroys its own bus handle so it does not collide with the touch
 * driver.
 *
 * Returns the number of devices found, or -1 if the bus could not be created
 * at all (which points at the SDA/SCL pins being wrong). */
int diag_i2c_scan(void);

/* Logs chip revision, flash and PSRAM size, partition layout, reset reason and
 * the running OTA slot. First thing worth seeing in a serial log. */
void diag_report_hardware(void);

/* Confirms the ESP32-C6 co-processor is reachable and reports the ESP-Hosted
 * link state. Call after net_start(). */
void diag_report_network(void);

/* Runs everything above in order. */
void diag_run_all(void);
