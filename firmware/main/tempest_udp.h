#pragma once
/*
 * Tempest hub local UDP listener.
 *
 * Binds 0.0.0.0:CONFIG_TEMPEST_UDP_PORT and decodes the hub's unauthenticated
 * broadcast JSON into wx_state. No token, no pairing, no cloud.
 *
 * Field order for obs_st / rapid_wind is fixed by WeatherFlow's spec and is
 * mirrored one-for-one from tools/tempest_listen.py, which was verified against
 * the live station (hub HB-00221923, device ST-00221238).
 */

#include <stdint.h>
#include "esp_err.h"

esp_err_t tempest_udp_start(void);
void      tempest_udp_stop(void);

/* Datagrams received since boot -- a cheap health signal for the UI, and the
 * thing to watch during the Milestone 2 broadcast-through-ESP-Hosted test. */
uint32_t  tempest_udp_packet_count(void);
