# Tempest Weather Station — data interfaces

Station: **230728** (https://tempestwx.com/station/230728/)

This project uses two of the three available interfaces:

- **Local UDP** — primary source for everything live. No internet, no token, no limits.
- **REST** — forecast only, plus derived values the UDP feed does not carry.

The WebSocket API is documented here as the designated fallback if UDP broadcast through
ESP-Hosted proves unreliable (see `docs/hardware.md`).

---

## 1. Local UDP (primary)

The Tempest hub blasts unauthenticated JSON to the broadcast address on **UDP port
50222**. No pairing, no token, no configuration. The firmware just has to bind
`0.0.0.0:50222` and enable `SO_BROADCAST` / join the broadcast group.

### Message types

| `type` | Cadence | Carries |
|---|---|---|
| `rapid_wind` | every 3 s | instantaneous wind speed + direction |
| `obs_st` | every 60 s | the full 18-field observation |
| `evt_precip` | on event | rain started |
| `evt_strike` | on event | lightning strike: distance + energy |
| `device_status` | every 60 s | sensor uptime, voltage, RSSI, sensor fault flags |
| `hub_status` | every 10 s | hub uptime, firmware, RSSI |

### `obs_st` — `obs[0]` array, 18 fields, exact order

| Idx | Field | Unit |
|---|---|---|
| 0 | Time epoch | seconds |
| 1 | Wind lull (min 3 s sample) | m/s |
| 2 | Wind avg | m/s |
| 3 | Wind gust (max 3 s sample) | m/s |
| 4 | Wind direction | degrees |
| 5 | Wind sample interval | seconds |
| 6 | Station pressure | mb |
| 7 | Air temperature | C |
| 8 | Relative humidity | % |
| 9 | Illuminance | lux |
| 10 | UV | index |
| 11 | Solar radiation | W/m^2 |
| 12 | Rain over previous minute | mm |
| 13 | Precipitation type | 0=none, 1=rain, 2=hail, 3=rain+hail |
| 14 | Lightning strike avg distance | km |
| 15 | Lightning strike count | count |
| 16 | Battery | volts |
| 17 | Report interval | minutes |

Example:
```json
{"serial_number":"ST-00000512","type":"obs_st","hub_sn":"HB-00013030",
 "obs":[[1588948614,0.18,0.22,0.27,144,6,1017.57,22.37,50.26,328,0.03,3,0.000000,0,0,0,2.410,1]],
 "firmware_revision":129}
```

### `rapid_wind` — `ob` array, 3 fields

| Idx | Field | Unit |
|---|---|---|
| 0 | Time epoch | seconds |
| 1 | Wind speed | m/s |
| 2 | Wind direction | degrees |

```json
{"serial_number":"SK-00008453","type":"rapid_wind","hub_sn":"HB-00000001","ob":[1493322445,2.3,128]}
```

### What UDP does NOT give you

Everything derived. No feels-like, no heat index, no wind chill, no dew point, no
sea-level pressure, no pressure trend, and **no forecast**. Anything in that list is
either computed on-device from the raw fields or fetched from REST.

---

## 2. REST (forecast + derived)

Base: `https://swd.weatherflow.com/swd/rest/`
Auth: `?token=<personal access token>` on every request.

Get a **personal access token** at tempestwx.com -> Settings -> Data Authorizations ->
Create Token. OAuth 2.0 also exists but is meant for multi-user apps; a personal token is
correct for a single private device.

| Endpoint | Use |
|---|---|
| `better_forecast?station_id=230728&units_temp=f&units_wind=mph&units_pressure=inhg&units_precip=in&units_distance=mi` | Current conditions **plus** hourly and daily forecast. This is the one that matters. |
| `observations/station/230728` | Latest station-level obs, already unit-converted and derived |
| `observations/?device_id=<id>&type=obs_st` | Raw device obs, supports time ranges for graphs |
| `stations` | Station + device metadata, including the `device_id` needed above |

`better_forecast` returns `current_conditions` (with `feels_like`, `dew_point`,
`pressure_trend`, `conditions` text and an `icon` slug) and `forecast.daily[]` /
`forecast.hourly[]`.

### Rate limiting

Not published. Treat it as a courtesy budget: poll `better_forecast` **every 10 minutes**.
Live numbers come from UDP, so there is no reason to poll faster. Back off to 30 min on
any non-200.

### TLS on the P4

REST is HTTPS, which means a cert bundle. Use `esp_crt_bundle_attach` from
`esp-tls` rather than pinning a certificate — WeatherFlow rotates theirs.

---

## 3. WebSocket (fallback, not currently used)

`wss://ws.weatherflow.com/swd/data?token=<token>`

After connect, send `{"type":"listen_start","device_id":<id>,"id":"<random>"}` and
`{"type":"listen_rapid_start",...}`. Yields an observation roughly every minute plus
rapid wind. Same payload shapes as UDP.

Adopt this only if Milestone 2 shows UDP broadcast does not survive the SDIO link.

---

## Unit handling

The UDP feed is **metric, always**. Display units are a presentation-layer concern —
conversion happens in the UI layer, never in the ingest layer, so the state struct always
holds SI values. See `firmware/main/wx_state.h`.

| From | To | Formula |
|---|---|---|
| m/s | mph | `* 2.236936` |
| C | F | `* 9/5 + 32` |
| mb | inHg | `* 0.0295299830714` |
| mm | in | `/ 25.4` |
| km | mi | `* 0.621371` |
