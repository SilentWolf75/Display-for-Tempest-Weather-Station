# Tempest Weather Display

A standalone 1024x600 wall display for a WeatherFlow Tempest weather station,
running on an Elecrow CrowPanel Advance 10.1" (ESP32-P4).

No Home Assistant, no broker, no cloud dependency for live data — the panel
listens directly to the Tempest hub's UDP broadcasts on the LAN and only reaches
out to the internet for the forecast.

Indoor temperature comes from a **$3 I2C sensor on the Grove header** (AHT20 /
DHT20 or SHT4x), since the Tempest is an outdoor-only station. A Nest/SDM path
also exists but is not the default — see [docs/indoor.md](docs/indoor.md) for
why.

- **Station:** 230728 (hub `HB-00221923`, sensor `ST-00221238`)
- **Hardware:** [docs/hardware.md](docs/hardware.md)
- **Outdoor data:** [docs/tempest-api.md](docs/tempest-api.md)
- **Indoor data:** [docs/indoor.md](docs/indoor.md) (Nest alternative: [docs/nest-api.md](docs/nest-api.md))
- **Weather icons:** [docs/icons.md](docs/icons.md)
- **Bring-up checklist:** [docs/bringup.md](docs/bringup.md) — start here when the panel arrives
- **Plan and status:** [docs/roadmap.md](docs/roadmap.md)

## Architecture

```
  Tempest hub          swd.weatherflow.com     Google SDM API
   (your LAN)               (cloud)                (cloud)
       |                       |                      |
       | UDP 50222             | HTTPS / 10 min       | HTTPS / 5 min
       | broadcast             | better_forecast      | OAuth + traits
       | obs_st 60s            |                      |
       | rapid_wind 3s         |                      |
       v                       v                      v
  tempest_udp.c ------> wx_state.c (mutex) <---- nest.c
                             ^
                             |
                       tempest_rest.c
                             |
                             | wx_snapshot()
                             v
                          ui.c  (LVGL 9.2, 1 Hz repaint)
                             |
                          display.c
                             |
            EK79007 / MIPI-DSI  +  GT911 / I2C
```

The three feeds fail independently on purpose. Only the leftmost one is local:
if the internet drops, the outdoor readings keep updating while the forecast and
indoor tiles age out and are marked stale.

`wx_state` holds SI units exclusively. Unit conversion happens in `ui.c` at
render time, which is what keeps the imperial/metric switch a one-line change.
It is also the seam that lets the UDP ingest be swapped for the WebSocket API
without the UI noticing.

## Status

| Milestone | State |
|---|---|
| 0 — Tempest protocol verified against the live station | **done** |
| 1 — Board bring-up | tooling ready ([checklist](docs/bringup.md)), blocked on hardware |
| 2 — UDP broadcast through ESP-Hosted (the risk gate) | blocked on hardware |
| 3 — Data layer | written, **compiles clean**, untested on device |
| 4 — UI | gauge layout written, **compiles clean**, untested on device |
| Animated weather icons | Meteocons Lottie + ThorVG, **built and compiling** |
| 5 — Polish | settings screen + night dimming done; OTA + graphs pending |
| Correctness pass | **done** — rain/pressure/hi-lo/UV bugs fixed |
| Hardware readiness | **done** — I2C scanner, boot diagnostics, OTA + rollback |
| 24-hour trend graphs | **done** — local history + REST backfill at boot |
| Nest indoor feed | written, **compiles clean**, needs $5 Device Access setup |

`firmware/` builds clean for `esp32p4` on ESP-IDF v5.5.3 — **2.04 MB app**
(ThorVG's vector engine is 423 KB of that) plus **416 KB of icon artwork** on
the 2 MB storage partition. The layout is now OTA-capable: two 3 MB app slots
with automatic rollback, 35 % free in the active slot. It has **never been run on real
hardware**. Treat pin assignments in `firmware/main/board_pins.h` as unverified
— see the warning at the top of that file.

## Try it now, without hardware

The Python tools work today and need nothing but a PC on the same LAN.

Watch your actual station:

```bash
python tools/tempest_listen.py
```

Replay synthetic frames so UI work can proceed with the hub unplugged:

```bash
python tools/tempest_sim.py --fast --storm
```

## Building the firmware

Requires ESP-IDF v5.3+ (v5.5.3 is installed at `C:\esp\v5.5.3\esp-idf`).

Activate the toolchain in PowerShell:

```bash
. C:\Espressif\tools\Microsoft.v5.5.3.PowerShell_profile.ps1
```

Then configure credentials and build:

```bash
cd firmware && cp main/secrets.h.example main/secrets.h
```

Put your Tempest token and, optionally, your Nest OAuth credentials in
`main/secrets.h` — see [docs/nest-api.md](docs/nest-api.md) for the Nest setup.
Set the Wi-Fi SSID and password under `idf.py menuconfig` -> "Tempest Weather
Display", then:

Note: creating `secrets.h` for the first time does **not** invalidate the build
cache. Run `idf.py fullclean` after adding it, or the credentials will be
silently compiled out.

```bash
idf.py set-target esp32p4 && idf.py build
```

Flash with the board on a 5 V/2 A supply, not a PC USB port:

```bash
idf.py -p COM7 flash monitor
```

## Layout

```
docs/          hardware notes, API reference, roadmap
firmware/      ESP-IDF project
  main/
    main.c            boot order and the UDP health watchdog
    wx_state.[ch]     mutex-protected shared state, SI units + accumulators
    config.[ch]       runtime settings persisted in NVS
    diag.[ch]         boot diagnostics and I2C bus scanner
    history.[ch]      24h ring buffer, 5-minute buckets, ~13 KB PSRAM
    ui/graphs.c       trend graphs screen (chart button, bottom right)
    ota.[ch]          LAN firmware upload with automatic rollback
    ui/settings.c     settings screen (gear button, bottom right)
    tempest_udp.[ch]  hub broadcast listener + JSON decode
    tempest_rest.[ch] better_forecast poller over TLS
    net.[ch]          Wi-Fi via esp_wifi_remote, SNTP
    indoor.[ch]       local I2C temp/humidity sensor (the default)
    nest.[ch]         Nest over OAuth + SDM (optional alternative)
    display.[ch]      EK79007 + GT911 + LVGL bring-up
    board_pins.h      pin map — UNVERIFIED, read the header
    ui/ui.c           the screen: rings, cards, forecast strip
    ui/wx_icons.c     Meteocons Lottie icons via ThorVG, loaded from SPIFFS
  spiffs/icons/     generated artwork (python tools/build_icons.py)
tools/         Python listener, simulator, and icon builder
```
