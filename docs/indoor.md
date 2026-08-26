# Indoor temperature

The Tempest is an outdoor station with no indoor channel, so indoor readings
need a second source. There are two, and the default is the boring one.

## Default: a local I2C sensor

An **AHT20 / DHT20** (address 0x38) or **SHT4x** (0x44) on the board's
Grove/Crowtail connector. Both are probed at boot and whichever answers is
used, so either part works without a rebuild.

The DHT20 is the one Elecrow sells for this connector, and it is the same part
Mjrovai's lesson 10 uses on this exact board at GPIO 7/8 — so the wiring is
documented rather than hopeful. It plugs in; there is no soldering.

- No account, no tokens, nothing that expires
- Works with the internet down, like the local UDP feed
- Costs about $3

`indoor.c` shares the I2C bus the touch controller already owns via
`display_get_i2c_bus()`, rather than creating a second master on the same two
pins.

Readings outside -20..70 C or 0..100 %RH are discarded. A sensor that has come
loose returns garbage rather than failing cleanly, and a wall display showing
-50 C indoors is worse than one showing nothing.

## Alternative: a Nest thermostat

`nest.c`, selectable via `CONFIG_INDOOR_SOURCE_NEST`. It additionally reports
setpoint, HVAC state and Eco mode, which drive the indoor ring's colour.

**It is not the default, for reasons worth recording:**

- Modern Nest hardware has **no local API**. Every reading is a cloud
  round-trip, so it stops working when the internet does.
- Access requires a **paid ($5) Google Device Access project**, a Google Cloud
  project, an OAuth client, and a configured consent screen.
- Publishing that consent screen to production is **currently blocked by a
  Google console bug** — see the note in [nest-api.md](nest-api.md).
- Unpublished apps get refresh tokens that **Google expires after 7 days**, so
  the indoor tiles stop weekly until reauthorised in a browser.

For the actual requirement — *what is the temperature in here* — a $3 part that
never expires is the better trade by a wide margin. The Nest path stays in the
tree in case Google ever fixes publishing; switching is a `menuconfig` change,
not a code change.

## What the UI does with each

`ui.c` keys off whether `thermostat_mode` is populated:

| | I2C sensor | Nest |
|---|---|---|
| Temperature | yes | yes |
| Humidity | yes | yes |
| Setpoint line | hidden | "set to 70F" |
| HVAC state + ring colour | hidden, ring is neutral | HEATING / COOLING / IDLE |
| Staleness | after `CONFIG_NEST_STALE_S` | same, plus REAUTHORIZE on a dead token |

No invented values: the bare sensor hides the rows it cannot fill rather than
showing zeros.
