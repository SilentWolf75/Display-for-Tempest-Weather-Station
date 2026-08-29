# Roadmap

Ordered so that the riskiest unknown is settled before any effort is sunk into UI.

## Milestone 0 — Protocol proof, no hardware needed  ✅ DONE 2026-08-26

Verified against the live station from this machine. `tools/tempest_listen.py`
received hub `HB-00221923` (fw 343, uptime 53090 s, RSSI -45) and sensor
`ST-00221238` (battery 2.647 V, `sensor_status 0x0`, 1-minute report interval).
All 18 `obs_st` fields present and in spec order; `rapid_wind` every 3 s;
`hub_status` every 20 s. Sample reading: 72.1 F, 96.4 % RH, 28.95 inHg, wind
0.9 mph avg / 1.4 gust from NW, UV 0.

The hub broadcasts to the LAN with no token and no configuration, exactly as
documented. The data source is confirmed.

```bash
python tools/tempest_listen.py
```

Run it on a PC on the same LAN as the hub. If `obs_st` and `rapid_wind` frames appear,
the data source is confirmed and the field decoding in `tools/` becomes the reference
implementation the C parser is checked against.

`tools/tempest_sim.py` replays synthetic frames on 50222 so UI work can proceed without
the real hub (or without any network weather at all).

## Milestone 1 — Board bring-up

1. Activate ESP-IDF, `idf.py set-target esp32p4`, build and flash the stock
   `examples/peripherals/lcd/mipi_dsi` to confirm the panel lights up.
2. Pull the Elecrow V1.1/V1.2 example and read out the real pin map — backlight, panel
   reset, GT911 INT/RST. Fill in `firmware/main/board_pins.h` and delete the `#warning`s.
3. Confirm the C6 co-processor answers: `esp_wifi_remote` init succeeds and a scan
   returns APs. If the ESP-Hosted handshake fails, reflash the C6 slave firmware to the
   version matching the `espressif/esp_hosted` component this project pins.

## Milestone 2 — THE risk gate: UDP broadcast through ESP-Hosted  ✅ PASSED

Confirmed on hardware 2026-08-26. Broadcast traffic from the hub reaches the
P4 through the C6 over SDIO, unmodified:

    main: udp packets: 29 (+12)  wifi: up
    tempest_udp: obs_st  31.9C  44%RH  979.5mb  wind 0.6/1.0 m/s @296

No multicast workaround, no WebSocket fallback, no change to the ingest design.
The fallbacks documented below were never needed and are kept only in case a
future ESP-Hosted or C6 firmware regresses this.

Getting here required the SDIO bus to be configured correctly first -- see the
hard-won facts in CLAUDE.md. The link enumerates happily on the wrong pin map
and then silently carries no data, which cost most of a day.

## Milestone 3 — Data layer

- `tempest_udp.c` — socket task, cJSON parse, populate `wx_state` under mutex.
- `tempest_rest.c` — 10-minute `better_forecast` poll over HTTPS with the IDF cert bundle.
- SNTP for wall-clock time (needed for sunrise/sunset and "last updated" staleness).
- Staleness tracking: mark the display degraded if no `obs_st` for > 3 minutes.

## Milestone 4 — UI  ✅ running on hardware

Gauge-based layout, 1024x600 landscape. Three-page navigation: dashboard,
Night & Insights, and 24 h trend graphs.

Implemented on hardware:

- Live wind dial driven by `rapid_wind` at 3 s
- Lightning alert banner on the header strip (NWS warnings take priority)
- Smart weather insight pill on the conditions card
- EPA AQI badge in the header when AirNow data is available
- Heat index / wind chill smart-pill on the temperature card
- All LVGL calls wrapped in `display_lock()` / `display_unlock()`

## Milestone 5b — Trend graphs  ✅ done

Third screen, 2x2: temperature, pressure, wind (average + gust), humidity.
Reached by swiping or the page button in the header.

- **History is local**, in `history.c`: 288 five-minute buckets covering 24 h,
  ~13 KB in PSRAM, fed from every `obs_st`. Same rule as the rest of the
  outdoor half -- it keeps working with the internet down.
- Rain is **summed** within a bucket, gust is a **maximum**, everything else is
  a mean. Averaging a gust would defeat the point of recording it.
- Empty buckets become `LV_CHART_POINT_NONE`, so an offline period shows as a
  **gap** rather than a straight line drawn through missing time.
- Redraws every 30 s, not every tick: the buckets are five minutes wide.

- **Backfilled at boot.** `tempest_rest_backfill_history()` discovers the
  Tempest `device_id` via `GET /stations` (the observations endpoint is keyed
  on device, not station, and the list also contains the hub), then pulls 24 h
  of `obs_st` in four 6-hour windows so no single response has to be held in
  memory. Oldest window first, because history.c tracks only a head bucket and
  rejects anything older than it -- which is also what makes the backfill safe
  to race against the live UDP feed.
  Needs the API token; without one the graphs fill from live data only.

## Milestone 5 — Polish  ✅ largely done

- Backlight schedule / dim at night (PWM) — **done**
- Runtime settings in NVS — **done**
- 24 h trend graphs — **done** (see 5b)
- OTA with automatic rollback — **done** (`ota.c`, LAN upload)
- Local web dashboard — **done** (`http://tempest.local:8080`)
- Home Assistant MQTT auto-discovery — **done** (toggle + broker in settings)
- Audio: alerts, chimes, keyboard clicks — **done**
- microSD CSV logging — **done**
- Printed case with Grove port for indoor sensor — **done** (`ALL_PORTS = true`)

## Open questions

- Wall-mounted or desk? The case in `case/` is a desk stand; wall mount is not designed yet.
