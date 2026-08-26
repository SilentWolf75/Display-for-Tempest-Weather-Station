#pragma once
/*
 * Indoor temperature and humidity from a local I2C sensor.
 *
 * The Tempest is an outdoor station, so indoor readings need their own source.
 * A $3 part on the Grove header:
 *
 *   - no account, no tokens, nothing to expire
 *   - works with the internet down, like the local UDP feed
 *   - one wire, no soldering
 *
 * Supports AHT20/DHT20 (address 0x38) and SHT4x (0x44). Both are probed at
 * startup and whichever answers is used, so either part works without a
 * rebuild. The DHT20 is the one Elecrow sells for this board's Grove
 * connector.
 */

#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    INDOOR_SENSOR_NONE = 0,
    INDOOR_SENSOR_AHT20,     /* also DHT20 -- same silicon, same protocol */
    INDOOR_SENSOR_SHT4X,
} indoor_sensor_t;

/* Probes the bus and starts the poll task. Safe to call when no sensor is
 * fitted: it logs once and the indoor gauge shows "no sensor". */
esp_err_t indoor_start(void);

/* Which part was found, for the diagnostics panel. */
indoor_sensor_t indoor_sensor_type(void);
const char     *indoor_sensor_name(void);
