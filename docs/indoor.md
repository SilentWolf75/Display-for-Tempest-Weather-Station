# Indoor temperature

The Tempest is an outdoor station with no indoor channel, so indoor readings
need their own source. That source is a **$3 I2C sensor** on the board's
Grove/Crowtail connector.

## The part

An **AHT20 / DHT20** (address 0x38) or an **SHT4x** (0x44). Both are probed at
boot and whichever answers is used, so either works without a rebuild.

The DHT20 is the one Elecrow sells for this connector, and it is the same part
Mjrovai's lesson 10 uses on this exact board at GPIO 7/8 -- so the wiring is
documented rather than hopeful. It plugs in; there is no soldering.

- No account, no tokens, nothing that expires
- Works with the internet down, like the local UDP feed
- About $3

## Implementation notes

`indoor.c` shares the I2C bus the touch controller already owns, via
`display_get_i2c_bus()`, rather than creating a second master on the same two
pins. It therefore requires `display_init()` to have run first.

Readings outside -20..70 C or 0..100 %RH are discarded. A sensor that has come
loose returns garbage rather than failing cleanly, and a wall display showing
-50 C indoors is worse than one showing nothing.

Polling is every `CONFIG_INDOOR_POLL_INTERVAL_S` (default 30 s). Indoor
temperature moves slowly; this is already generous.

If no sensor answers, `indoor_start()` logs once and returns
`ESP_ERR_NOT_FOUND`. The indoor gauge reads "no sensor" and the outdoor half is
completely unaffected.

## Staleness

`wx_indoor_is_stale()` is deliberately separate from `wx_obs_is_stale()`: the
two feeds fail independently, and a missing sensor must not imply a missing
weather station. Past `CONFIG_INDOOR_STALE_S` (default 30 min) the indoor group
dims to 40% and the label says how old the reading is, so an unplugged sensor
looks obviously wrong rather than quietly frozen at its last value.

## What was removed

A Nest / Smart Device Management integration was built first and then deleted.
Modern Nest hardware exposes no local API; reaching it needs a paid Google
Device Access project, a Cloud project, an OAuth client, and a consent screen
that Google's own console currently refuses to publish -- after which an
unpublished app's refresh tokens expire every 7 days.

It would have added setpoint and HVAC state, which drive nothing the display
actually needs. `git log` has the implementation if it is ever wanted back.
