# Animated weather icons

Meteocons Lottie animations, rendered on-device by ThorVG through LVGL's
`lv_lottie` widget.

## Why Lottie

Three approaches were considered:

| Approach | Colour | Animated | Size for 20 icons | Verdict |
|---|---|---|---|---|
| LVGL icon font | single colour only | no | ~25 KB | rejected — cannot be "realistic" |
| Pre-rendered frame sequences | full | yes | ~660 KB **per icon** at 96px | rejected — 13 MB, and locked to one size |
| **Lottie + ThorVG** | full, with gradients | yes | **373 KB total** | chosen |

LVGL fonts are alpha masks — one colour per glyph — which is why the first pass
used text placeholders. Lottie is vector, so the same 7 KB file renders at 46 px
in the forecast strip and 52 px in the header with no extra assets, and ThorVG
already ships inside LVGL 9.2 so there is no new dependency.

## Cost, measured

| | |
|---|---|
| App binary, gauge UI before ThorVG | 1472 KB |
| ThorVG enabled but unreferenced | 1564 KB |
| `lv_lottie` actually wired up | **1895 KB** |
| **Real ThorVG + Lottie cost** | **423 KB** of flash |

The middle row is a trap worth knowing: enabling `CONFIG_LV_USE_LOTTIE` alone
looked like it cost only 92 KB, because `--gc-sections` strips the whole engine
while nothing references it. The true cost only appears once a widget is created.

Icon data is another 416 KB, on the SPIFFS `storage` partition (2 MB, so ~20%
used). Note `build/storage.bin` is always 2 MB — SPIFFS images are preallocated
to the partition size; that is not the payload.

RAM per widget is one ARGB8888 render target in PSRAM: 52x52 header = 10.5 KB,
seven 46x46 forecast icons = 8.5 KB each. About 70 KB of 32 MB.

## Only one icon animates

The header renders its animation; the seven forecast icons load, draw one
representative frame, then have their frame-driving animation deleted
(`lv_anim_delete`). Eight simultaneous vector animations would be a poor trade
on a 400 MHz core for artwork nobody watches. If the P4 turns out to have
headroom, deleting that one call in `wx_icon_set()` animates everything.

**This is unmeasured.** ThorVG's frame cost on the ESP32-P4 has not been
observed on real hardware — only that it links and fits. If the header icon
proves too expensive, the fallbacks in order are: drop the render size, drop the
frame rate below the Lottie's native 60 fps, or fall back to a single static
frame.

## Regenerating the set

```bash
python tools/build_icons.py
```

Downloads `@meteocons/lottie` (3 MB, cached in `.cache/`, gitignored), maps
WeatherFlow's icon slugs onto Meteocons asset names, minifies the JSON, and
writes `firmware/spiffs/icons/<weatherflow-slug>.json`. Naming by WeatherFlow's
own slug means the firmware needs no translation table — it opens
`/icons/icons/<slug>.json` directly.

Then `idf.py build` regenerates the SPIFFS image automatically, and
`idf.py flash` writes it (`FLASH_IN_PROJECT`).

Options: `--variant line|flat|monochrome` for other Meteocons styles,
`--list` to print the slug mapping, `--force` to re-download.

The script fails with a non-zero exit if any slug is unmapped, rather than
silently shipping an incomplete set.

## Gotchas

- **SPIFFS object names default to 32 characters.** `/icons/possibly-thunderstorm-night.json`
  is 39, so `CONFIG_SPIFFS_OBJ_NAME_LEN=64` is required. `spiffsgen.py` enforces
  this at build time, which is the good outcome — it fails the build rather than
  the device.
- **Keep the download cache out of `firmware/spiffs/`.** Everything in that
  directory is baked into the flash image, and the tarball is 3 MB against a
  2 MB partition.
- **The Lottie source buffer must outlive the widget.** ThorVG keeps referencing
  it, so `wx_icons.c` holds the JSON in PSRAM and only frees the previous one
  after the new source is installed.

## Licensing

Meteocons is **MIT** (github.com/basmilius/meteocons). `build_icons.py` copies
the LICENSE file into `firmware/spiffs/icons/` alongside the assets, since MIT
requires the notice to travel with copies.

Of the other sets considered:

- **Makin-Things/weather-icons** — animated SVG, licence not verified here.
- **bignutty/google-weather-icons** — Google's proprietary Weather icons
  redistributed without a licence. Not used.
- **Freepik / Magnific stock sets** — require attribution or a premium licence.
  Not used.
