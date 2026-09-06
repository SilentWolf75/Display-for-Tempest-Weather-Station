# Display for Tempest Weather Station — working notes

## What this is

Standalone ESP-IDF firmware for an Elecrow CrowPanel Advance 10.1" (ESP32-P4)
that renders a WeatherFlow Tempest station on a 1024x600 panel. No Home
Assistant. Live data from the hub's local UDP broadcast; forecast from the
Tempest REST API.

## Decisions already made — do not relitigate

- **ESP-IDF + LVGL in C**, not ESPHome and not Arduino. Chosen because Elecrow's
  own docs and the Mjrovai lessons are ESP-IDF, so the gotchas are documented.
- **Local UDP is the primary data source.** REST is forecast-only. The
  WebSocket API is the designated fallback if broadcast fails, not a default.
- **No Home Assistant dependency**, even though one could exist on the network.
- **The partition table is OTA-capable and lives at 0xA000**, not the 0x8000
  default: the OTA bootloader grew to within 256 bytes of the default budget.
  Two 3 MB app slots, which must always be resized together. Rollback is armed,
  and `ota_mark_valid()` is called late in `app_main` on purpose -- an image
  that cannot boot far enough to render reverts itself.
- **Settings are runtime, in NVS** (`config.c`), not Kconfig. The old
  `TEMPEST_UNITS` choice was deleted -- a settings screen that needs a reflash
  is not a settings screen. `U_TEMP` and friends in `ui.c` kept their spelling
  but now expand to `cfg_*()` calls, so the `*_SUF` macros are function calls
  and can no longer be string-concatenated into a format literal. Use `%s`.
- **Anything cumulative is accumulated on-device** in `wx_state.c`: rain since
  local midnight, observed daily hi/lo, a 3-hour pressure-sample ring buffer,
  and a windowed lightning count. The UDP feed carries instantaneous readings
  only, so none of this can be read straight off the wire.
- `wx_state` holds **SI units only**. Convert in the UI at render time. This is
  load-bearing: it keeps the units toggle trivial and keeps ingest swappable.

## Hard-won facts

- The **ESP32-P4 has no radio.** Wi-Fi is proxied to an ESP32-C6 over SDIO via
  ESP-Hosted / `esp_wifi_remote`. If `esp_wifi_init()` fails, it is a C6
  firmware/component version mismatch, not application code.
- UDP broadcast through ESP-Hosted was previously verified on this board.
  Recheck it after transport or board-revision changes. The health loop logs
  silence while Wi-Fi is up; WebSocket fallback tracks UDP separately.
- **Every LVGL call from outside an LVGL callback must be wrapped** in
  `display_lock()` / `display_unlock()`. Unlocked calls hang the panel rather
  than crashing, so the symptom is confusing.
- Panel is **EK79007 over MIPI-DSI**; touch is **GT911** on I2C GPIO 7/8. The
  I2C bus is shared, so do not assume exclusive access.
- Power the board from a **5 V/2 A external supply** when flashing. PC USB ports
  brown out and it looks like a flashing failure.
- **`esp_lvgl_port` is pinned to `~2.7.2` on purpose.** Its manifest claims
  `lvgl >=8,<10` and `idf >=5.2`, both of which are wrong for newer releases:
  2.8.0 uses `LV_COLOR_FORMAT_RGB565_SWAPPED` (needs LVGL >= 9.3) and 2.9.0 uses
  the renamed DPI callback `on_frame_buf_complete` (needs IDF >= 5.6; IDF 5.5.3
  only has `on_color_trans_done` / `on_refresh_done`). The dependency solver
  will happily pick 2.9.0 and then fail to compile inside the component itself,
  which looks like a broken toolchain rather than a version conflict. Only
  unpin when moving IDF and LVGL forward together.
- The EK79007 config macro is `EK79007_1024_600_PANEL_60HZ_CONFIG(px_format)`
  for IDF < 6.0 (there is a separate `..._CONFIG_CF` variant for IDF 6). It sets
  the DSI lane rate to 900 Mbps and the DPI clock to 52 MHz.
- Credentials are generated into a build header by CMake. Creating, changing,
  or removing secrets.h is tracked; a normal build is sufficient. An absent
  file builds with an empty token and on-device Wi-Fi setup.
- **Weather icons are Meteocons Lottie files rendered by ThorVG**, not a font.
  LVGL fonts are single-colour alpha masks and cannot be "realistic". See
  [docs/icons.md](docs/icons.md). Assets live on the SPIFFS `storage`
  partition, built by `python tools/build_icons.py`.
- **Enabling `CONFIG_LV_USE_LOTTIE` looks almost free until you use it.**
  `--gc-sections` strips ThorVG while nothing references it, so the binary grows
  only ~92 KB. Creating one widget pulls in the real 423 KB. Never judge the
  cost of an optional LVGL feature from a build that does not call it.
- **SPIFFS object names default to 32 chars**; the icon paths need
  `CONFIG_SPIFFS_OBJ_NAME_LEN=64`. Keep the icon download cache OUT of
  `firmware/spiffs/` -- that whole directory is baked into a 2 MB partition and
  the tarball alone is 3 MB.
- LVGL fonts are opt-in per size. Referencing `lv_font_montserrat_NN` in `ui.c`
  without a matching `CONFIG_LV_FONT_MONTSERRAT_NN=y` in `sdkconfig.defaults`
  fails as "undeclared identifier", which reads like a typo but is not.

## Indoor data comes from a local sensor

The Tempest is outdoor-only. Indoor temperature and humidity come from an
AHT20/DHT20 (0x38) or SHT4x (0x44) on the Grove/I2C header -- see
[docs/indoor.md](docs/indoor.md).

A Nest/SDM integration was built and then removed. Recorded so it is not
rebuilt by accident: modern Nest hardware has no local API, access needs a paid
Google Device Access project plus an OAuth consent screen that Google's own
console currently cannot publish, and an unpublished app's refresh tokens
expire every 7 days. For "what is the temperature in here", a $3 part that
never expires wins outright. `git log` has the implementation if it is ever
wanted back.

`indoor.c` shares the I2C bus the touch controller owns via
`display_get_i2c_bus()` rather than creating a second master on the same pins,
and discards readings outside -20..70 C -- a sensor that comes loose returns
garbage rather than an error.

## Verified live data (2026-08-26)

`tools/tempest_listen.py` captured the real station from this machine:
hub `HB-00221923` fw 343, sensor `ST-00221238`, `sensor_status 0x0`,
battery 2.647 V, 1-minute report interval, all 18 `obs_st` fields present and in
spec order. The Python decoder is the reference the C parser mirrors — if they
ever disagree, the Python one was validated against real traffic.

## The STEP model is the authoritative source for mechanical dimensions

`ESP32-P4-10_1-inch-20251230.stp` (Creo assembly, ~19.5 MB, from Elecrow) is a
full 3D model of the board with **named parts**, and it beats both the Eagle
PCB file and any hand measurement. It settles:

- **The stack**, referenced to the PCB's front face: glass front +2.00, active
  area -0.20 (222.7 x 125.3), LCD module footprint -4.80 (235.5 x 143.5,
  centred), PCB front -4.90, PCB back -6.50 (so the PCB is 1.60), rear-most
  extent -19.62. Board rep is #5343, 247.00 x 147.00.
- **The board is 247.00 x 147.00** and the LCD module (235.5 x 143.5) is
  centred on it, while the active area (222.7 x 125.3) is not.

**The .brd coordinates are a BACK view.** Eagle's top view is the component
side, which on a display board faces backwards. Confirmed against a render of
the STEP model: the Grove pair sits left and the GPIO pair right, matching the
.brd unmirrored, with BOOT/RESET and the power switch visible. `case/` assembles
with the screen facing +z, so its own x,y is a FRONT view and every .brd
position must be mirrored (`mx()` in `tempest_stand.scad`). Getting this wrong
puts every opening on the wrong side, which is exactly what happened.

**BOOT and RESET face out of the back**, near the .brd x=0 edge at roughly
y = 16..34; the power switch is SW1 on the .brd x=247 edge at y = 100.9. They
need back-plate openings, not edge cutouts.

**Do not trust a hand-rolled STEP assembly traversal for part positions.** The
attempt here produced two clusters of parts in frames 90 degrees apart and
placed the power switch 59 mm from where it actually is; a "correction" to the
.brd designators based on it was wrong and had to be reverted. There are no
`MAPPED_ITEM` entities in this file at all, so any code handling them is dead.
Layer heights off the board representation (#5343) were reliable; in-plane
part positions were not. Cross-check against the .brd or a screenshot.

Parsing notes, because this cost real time: part geometry is in LOCAL
coordinates and placed by `MAPPED_ITEM` + `REPRESENTATION_MAP`, not only by
`NEXT_ASSEMBLY_USAGE_OCCURRENCE` — a traversal that ignores mapped items
returns empty boxes for the biggest parts. Instances of one product must be
labelled separately or they merge into one meaningless bbox. And the
sub-assembly frames are not all aligned: one cluster of parts comes out rotated
90 degrees from another, so **cross-check any position against a known one**
(the two USB-C ports agree with the PCB file to 0.6 mm and make a good anchor).
The in-plane axes still cannot be tied to a left/right/up/down, which is why
the bezel window is sized to be safe under either sign of the active area's
3 mm off-centre offset.

The speakers are accessories and are **not** in the model, so their body size
is still the one dimension in `case/` waiting on a caliper.

## Unverified — do not trust

`firmware/main/board_pins.h`. Only I2C GPIO 7/8 is sourced. Backlight pin, panel
reset, touch INT/RST and all MIPI-DSI timings are placeholders. `display.c`
deliberately uses the `esp_lcd_ek79007` component's own config macros instead of
hand-entered timings. Verify against the Elecrow schematic and their V1.1/V1.2
example before driving pins, then define `BOARD_PINS_VERIFIED`.

The README and bring-up log record prior hardware validation. The September 4
reliability fixes were compiled and host-tested but have not been flashed or
validated on the panel.

## Build

ESP-IDF v5.5.3 at `C:\esp\v5.5.3\esp-idf`, toolchain riscv32-esp-elf 14.2.0.
Activate with `C:\Espressif\tools\Microsoft.v5.5.3.PowerShell_profile.ps1`,
then `idf.py set-target esp32p4 && idf.py build` from `firmware/`.

`main/secrets.h` is gitignored and holds the Tempest API token. Copy it from
`main/secrets.h.example`.

## Style

Match the existing code: 4-space indent, Allman-free K&R braces, `s_` prefix for
file-static state, comments that explain *why* rather than restating the call.
Log messages are lowercase and specific.
