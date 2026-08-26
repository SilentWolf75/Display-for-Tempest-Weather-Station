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

## Milestone 4 — UI  (first pass written, compiles clean)

Gauge-based layout, 1024x600 landscape. Three rings across the top -- outdoor
cards, then a seven-day forecast strip.

Implementation notes worth keeping:

- **Graded rings are N solid arcs, not a gradient.** LVGL 9.2 cannot draw a
  gradient along an arc. `make_graded_scale()` stacks five `lv_arc`s sharing a
  centre, each covering one 54-degree slice, with a position knob on top. The
  commercial consoles do the same thing.
- **Stale indoor dims to 40% AND states its age.** Dimming alone hides whether
  a reading is six minutes or six hours old, and this feed is cloud-dependent
  so it fails while the outdoor half keeps running.
- **Forecast icons need a font that does not exist yet.** `ui/wx_icons.c` maps
  WeatherFlow slugs to Weather Icons codepoints and currently returns short
  text placeholders. Flip `WX_ICON_FONT_AVAILABLE` after running the
  `lv_font_conv` command in `ui/wx_icons.h`. ~25 KB, versus ~400 KB for the
  equivalent PNG set which also could not be recoloured.

### Superseded first draft



1024x600 landscape. Hand-written LVGL first; SquareLine only if the layout stops being
expressible in code comfortably.

Original flat-tile sketch, kept for reference:

```
+----------------------------------------------------------+
|  72F          Partly Cloudy          Sat 3:42 PM   [wifi] |
|  feels 75F                                                |
+---------------------+------------------------------------+
|   WIND              |  humidity  pressure  uv   rain today|
|   [compass dial]    |    52%     29.94"    3.1    0.02"   |
|   4.2 mph  NNW      |                                     |
|   gust 9.1                                                |
+---------------------+------------------------------------+
|  [ 7-day forecast strip: icon / hi / lo / precip% ]       |
+----------------------------------------------------------+
```

- Live wind dial driven by `rapid_wind` at 3 s — the one thing that makes a Tempest
  display feel alive rather than like a webpage.
- Lightning banner that takes over the top strip on `evt_strike`.
- All LVGL calls wrapped in `lvgl_port_lock()` / `lvgl_port_unlock()`. Non-negotiable —
  this is the documented failure mode on this board.

## Milestone 5b — Trend graphs  (written, compiles clean)

Third screen, 2x2: temperature, pressure, wind (average + gust), humidity.
Reached by the chart button next to the gear, bottom-right.

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

## Milestone 5 — Polish

- Backlight schedule / dim at night (PWM on the backlight pin, once known).
- Touch: tap a tile to swap units, swipe for a history graph page.
- 24 h trend sparklines from `observations/?device_id=...` with a time range.
- Custom partition table — the stock ones do not leave room for UI image assets, per the
  Elecrow wiki.
- OTA, so the panel does not need to come off the wall.

## Open questions

- Has the hardware arrived? Milestones 1+ need it; Milestone 0 does not.
- Wall-mounted or desk? Decides whether backlight scheduling and viewing angle matter.
- Preferred units — the code carries SI and converts at the edge, so this is a one-line
  default plus a touch toggle.
