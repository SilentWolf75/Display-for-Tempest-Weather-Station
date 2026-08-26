# Nest thermostat — indoor data via the SDM API

Device: **Nest Learning Thermostat, 4th generation (2024)**

The Tempest is an outdoor station with no indoor channel. Every commercial
weather console splits indoor/outdoor; this is how we get the indoor half.

## Read this before spending anything

- **There is no local API.** Modern Nest hardware does not expose one. Every
  reading is a cloud round-trip to Google over TLS. When your internet is down,
  the indoor tiles go stale while the outdoor half keeps working from the
  Tempest's local UDP broadcast. `wx_indoor_is_stale()` is deliberately
  separate from `wx_obs_is_stale()` for exactly this reason.
- **Device Access registration costs $5, one time, non-refundable.**
- **Model support:** Google's docs state "All Google Nest thermostat models are
  supported in the Device Access program and the API functionality is the same
  for all models." They do not enumerate models, so the 4th gen is covered by
  that blanket statement rather than by name. **Verify with the curl in step 5
  before writing any more firmware against it** — it costs nothing but the $5
  already spent, and it is the one assumption in this integration that has not
  been tested on real hardware.
- **Matter is the road not taken.** The 4th gen speaks Matter, which would be
  local and would avoid all of this. But the ESP32-C6 on this board is occupied
  being the Wi-Fi radio for the P4 via ESP-Hosted, so it cannot also be a Thread
  radio. Revisit only if the board's replaceable wireless module is ever swapped.

## One-time setup

### 1. Register a Device Access project — $5

Go to <https://console.nest.google.com/device-access>, accept the terms, pay the
one-time fee, and create a project. Keep the **Project ID** (a UUID) — that is
`NEST_PROJECT_ID`.

### 2. Create an OAuth client

In Google Cloud Console (<https://console.cloud.google.com>), same or a new
project:

- APIs & Services -> Credentials -> Create credentials -> **OAuth client ID**
- Application type: **Web application**
- Authorised redirect URI: `https://www.google.com`
  (a throwaway — you only need the `code` parameter it lands with)

Keep the **Client ID** and **Client secret** — `NEST_CLIENT_ID` and
`NEST_CLIENT_SECRET`.

### 3. Enable the API

In the same Cloud project: APIs & Services -> Library -> enable
**Smart Device Management API**. Skipping this produces a 403 that reads like an
auth problem but is not.

### 4. Authorise once, in a browser

Open this URL, substituting your project ID and client ID:

```
https://nestservices.google.com/partnerconnections/PROJECT_ID/auth?redirect_uri=https://www.google.com&access_type=offline&prompt=consent&client_id=CLIENT_ID&response_type=code&scope=https://www.googleapis.com/auth/sdm.service
```

Grant access. You land on google.com with `?code=...` in the address bar. Copy
that code — it is single-use and expires in minutes.

`access_type=offline` and `prompt=consent` are both required. Without them the
response carries no refresh token and you have to start over.

Exchange it:

```bash
curl -L -X POST 'https://www.googleapis.com/oauth2/v4/token?client_id=CLIENT_ID&client_secret=CLIENT_SECRET&code=CODE&grant_type=authorization_code&redirect_uri=https://www.google.com'
```

The response contains `refresh_token`, starting `1//`. That is
`NEST_REFRESH_TOKEN`. **Keep the slashes** — the firmware percent-encodes them
before putting the token in a form body, which is why `url_encode()` exists in
`nest.c`.

### 5. Find the device id, and confirm the 4th gen works

```bash
curl -X GET 'https://smartdevicemanagement.googleapis.com/v1/enterprises/PROJECT_ID/devices' -H 'Authorization: Bearer ACCESS_TOKEN'
```

Look for `sdm.devices.types.THERMOSTAT`. The `name` field looks like
`enterprises/PROJECT_ID/devices/DEVICE_ID`; the part after `devices/` is
`NEST_DEVICE_ID`.

**This response is the model-support test.** If the thermostat appears with
populated `sdm.devices.traits.Temperature` and `sdm.devices.traits.Humidity`
traits, the integration works. If it is missing or the traits are absent, stop —
the firmware cannot fix that, and the indoor tiles should be dropped from the
design rather than left permanently blank.

### 6. Fill in `secrets.h`

Copy `firmware/main/secrets.h.example` to `firmware/main/secrets.h` and paste
all five values. Leaving `NEST_REFRESH_TOKEN` empty disables the indoor tiles
cleanly — the outdoor display works without any of it.

## Traits used

| Trait | Field | Maps to |
|---|---|---|
| `sdm.devices.traits.Temperature` | `ambientTemperatureCelsius` | `indoor_temp_c` |
| `sdm.devices.traits.Humidity` | `ambientHumidityPercent` | `indoor_humidity_pct` |
| `sdm.devices.traits.ThermostatHvac` | `status` | `hvac_status` — OFF / HEATING / COOLING |
| `sdm.devices.traits.ThermostatMode` | `mode` | `thermostat_mode` — HEAT / COOL / HEATCOOL / OFF |
| `sdm.devices.traits.ThermostatTemperatureSetpoint` | `heatCelsius`, `coolCelsius` | `setpoint_heat_c`, `setpoint_cool_c` |
| `sdm.devices.traits.ThermostatEco` | `mode` | `eco_mode` (`MANUAL_ECO`) |

Only the setpoint matching the active mode is present: HEAT returns
`heatCelsius`, COOL returns `coolCelsius`, HEATCOOL returns both. The parser
defaults the absent one to 0, so the UI must key off `thermostat_mode` rather
than testing a setpoint for non-zero.

Everything is stored in **Celsius**, matching the SI-only rule for `wx_state`.

## Token lifecycle

- Access tokens last ~1 hour. `nest.c` refreshes when under 5 minutes remain,
  timed on `esp_timer_get_time()` — a monotonic clock, deliberately, so token
  handling does not depend on SNTP having succeeded.
- A 401 mid-poll discards the cached token so the next poll re-exchanges.
- The refresh token has no expiry, but **is revoked by a Google password change
  or by removing access in the account's security settings**. That surfaces as
  HTTP 400/401 on refresh, which `nest.c` logs explicitly because it never
  recovers on its own — you have to redo step 4.

## Rate limits

Google publishes a quota for the SDM API and returns 429 when exceeded.
`CONFIG_NEST_POLL_INTERVAL_S` defaults to **300 s**, which is far inside any
published limit and entirely adequate — indoor temperature does not move fast.
A 429 is logged and backs the task off to 5 minutes. Do not drop below 60 s.

## Failure behaviour

The indoor feed is designed to fail alone:

| Condition | Outdoor tiles | Indoor tiles |
|---|---|---|
| Normal | live | live |
| Internet down, LAN up | **live** (UDP) | stale after 30 min |
| Refresh token revoked | **live** | stale, error in log |
| No Nest credentials | **live** | hidden |
| Tempest hub off | stale | live |
