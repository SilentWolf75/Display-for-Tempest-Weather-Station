#!/usr/bin/env python3
"""Build the animated weather-icon set for the SPIFFS partition.

Downloads Meteocons (MIT licensed) and copies the icons WeatherFlow can
actually return into firmware/spiffs/icons/, named by WeatherFlow's own slug so
the firmware needs no translation table at runtime.

    python tools/build_icons.py
    python tools/build_icons.py --variant line     # outline instead of filled
    python tools/build_icons.py --list             # show the mapping, no download

Why Lottie and not PNG frames: these are vector animations of a few KB each
that ThorVG renders on the ESP32-P4 at whatever size we ask for. The equivalent
pre-rendered frame sequences would be hundreds of KB per icon and locked to one
size. See docs/icons.md.

Licence: Meteocons is MIT (basmilius/meteocons). The LICENSE file is copied
into the icon directory alongside the assets.
"""

import argparse
import io
import json
import shutil
import sys
import tarfile
import urllib.request
from pathlib import Path

PKG_URL = "https://registry.npmjs.org/@meteocons/lottie/-/lottie-0.1.0.tgz"

REPO = Path(__file__).resolve().parent.parent
OUT_DIR = REPO / "firmware" / "spiffs" / "icons"
# Deliberately NOT under firmware/spiffs/ -- everything in that directory is
# baked into the 2 MB flash partition, and this tarball is 3 MB on its own.
CACHE = REPO / ".cache" / "meteocons.tgz"

# WeatherFlow's documented `icon` slugs -> Meteocons asset names.
# Left column is what better_forecast returns; right is the file we ship.
MAPPING = {
    "clear-day":                 "clear-day",
    "clear-night":               "starry-night",
    "starry-night":              "starry-night",
    "cloudy":                    "cloudy",
    "foggy":                     "fog",
    "partly-cloudy-day":         "partly-cloudy-day",
    "partly-cloudy-night":       "partly-cloudy-night",
    "possibly-rainy-day":        "partly-cloudy-day-rain",
    "possibly-rainy-night":      "partly-cloudy-night-rain",
    "possibly-sleet-day":        "partly-cloudy-day-sleet",
    "possibly-sleet-night":      "partly-cloudy-night-sleet",
    "possibly-snow-day":         "partly-cloudy-day-snow",
    "possibly-snow-night":       "partly-cloudy-night-snow",
    "possibly-thunderstorm-day": "thunderstorms-day",
    "possibly-thunderstorm-night": "thunderstorms-night",
    "rainy":                     "rain",
    "sleet":                     "sleet",
    "snow":                      "snow",
    "thunderstorm":              "thunderstorms",
    "windy":                     "wind",
    "moon-full":                 "moon-full",
    "moon-waxing-gibbous":       "moon-waxing-gibbous",
    "moon-first-quarter":        "moon-first-quarter",
    "moon-waning-gibbous":       "moon-waning-gibbous",
    "moon-last-quarter":         "moon-last-quarter",
    "moon-waxing-crescent":      "moon-waxing-crescent",
    "moon-waning-crescent":      "moon-waning-crescent",
    "moon-new":                  "moon-new",
}

# Rendered when the API hands us a slug we have never seen. Better a neutral
# cloud than a blank hole in the layout.
FALLBACK = ("unknown", "cloudy")


def fetch(force: bool) -> bytes:
    if CACHE.exists() and not force:
        print(f"using cached {CACHE.name} ({CACHE.stat().st_size // 1024} KB)")
        return CACHE.read_bytes()

    print(f"downloading {PKG_URL}")
    CACHE.parent.mkdir(parents=True, exist_ok=True)
    with urllib.request.urlopen(PKG_URL, timeout=120) as r:
        data = r.read()
    CACHE.write_bytes(data)
    print(f"  {len(data) // 1024} KB cached")
    return data


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--variant", default="fill",
                    choices=["fill", "line", "flat", "monochrome"],
                    help="Meteocons style (default: fill, the colourful one)")
    ap.add_argument("--force", action="store_true", help="re-download")
    ap.add_argument("--list", action="store_true",
                    help="print the slug mapping and exit")
    args = ap.parse_args()

    if args.list:
        for wf, mc in sorted(MAPPING.items()):
            print(f"  {wf:<30} -> {mc}")
        print(f"  {FALLBACK[0]:<30} -> {FALLBACK[1]}   (fallback)")
        return 0

    raw = fetch(args.force)
    tf = tarfile.open(fileobj=io.BytesIO(raw), mode="r:gz")

    members = {m.name: m for m in tf.getmembers() if m.isfile()}

    if OUT_DIR.exists():
        shutil.rmtree(OUT_DIR)
    OUT_DIR.mkdir(parents=True)

    wanted = dict(MAPPING)
    wanted[FALLBACK[0]] = FALLBACK[1]

    total = 0
    missing = []
    written = 0

    for wf_slug, mc_name in sorted(wanted.items()):
        member = f"package/{args.variant}/{mc_name}.json"
        if member not in members:
            missing.append((wf_slug, mc_name))
            continue

        data = tf.extractfile(members[member]).read()

        # Sanity check: a Lottie file must be JSON with the layer keys ThorVG
        # expects. A silently truncated download would otherwise only fail on
        # the device, which is a miserable place to debug it.
        try:
            doc = json.loads(data)
        except json.JSONDecodeError as e:
            print(f"  !! {mc_name}: not valid JSON ({e})")
            missing.append((wf_slug, mc_name))
            continue
        if "layers" not in doc or "fr" not in doc:
            print(f"  !! {mc_name}: missing Lottie keys, skipping")
            missing.append((wf_slug, mc_name))
            continue

        # Re-serialise minified: strips the pretty-printing, which is pure
        # payload on a device that parses this at boot.
        minified = json.dumps(doc, separators=(",", ":")).encode()

        out = OUT_DIR / f"{wf_slug}.json"
        out.write_bytes(minified)
        total += len(minified)
        written += 1
        saved = len(data) - len(minified)
        print(f"  {wf_slug:<30} {len(minified):>7,} B  "
              f"(-{saved:,} minified)  fr={doc.get('fr')} "
              f"frames={int(doc.get('op', 0))}")

    # Ship the licence next to the assets. It is MIT, which requires the
    # copyright notice to travel with the copies.
    lic = members.get("package/LICENSE")
    if lic:
        (OUT_DIR / "LICENSE").write_bytes(tf.extractfile(lic).read())
        print("  LICENSE copied (Meteocons is MIT)")

    print(f"\n{written} icons -> {OUT_DIR}")
    print(f"total {total:,} bytes ({total / 1024:.0f} KB) "
          f"of the 2 MB storage partition")

    if missing:
        print(f"\n!! {len(missing)} unmapped -- these slugs would render the "
              f"fallback:")
        for wf, mc in missing:
            print(f"     {wf} (wanted meteocons '{mc}')")
        print("   fix the MAPPING table in this script")
        return 1

    print("\nnext: idf.py build   (the SPIFFS image is regenerated "
          "automatically)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
