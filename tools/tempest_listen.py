#!/usr/bin/env python3
"""Listen to Tempest hub UDP broadcasts on port 50222 and pretty-print them.

Run this on any machine on the same LAN as the hub. No token, no config.
It is both a diagnostic ("is the hub actually broadcasting?") and the reference
decoder that the C parser in firmware/main/tempest_udp.c is checked against.

    python tools/tempest_listen.py
    python tools/tempest_listen.py --raw          # dump raw JSON too
    python tools/tempest_listen.py --units metric
"""

import argparse
import json
import socket
import sys
from datetime import datetime

PORT = 50222

# obs_st field names, in wire order. Index == position in the obs[0] array.
OBS_ST_FIELDS = [
    "time_epoch", "wind_lull", "wind_avg", "wind_gust", "wind_dir",
    "wind_sample_interval", "pressure", "air_temp", "humidity", "illuminance",
    "uv", "solar_radiation", "rain_last_min", "precip_type",
    "lightning_avg_dist", "lightning_count", "battery", "report_interval",
]

PRECIP_TYPE = {0: "none", 1: "rain", 2: "hail", 3: "rain+hail"}

COMPASS = ["N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
           "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"]


def bearing(deg):
    return COMPASS[int((deg % 360) / 22.5 + 0.5) % 16]


def clock(epoch):
    return datetime.fromtimestamp(epoch).strftime("%H:%M:%S")


class Units:
    def __init__(self, imperial=True):
        self.imperial = imperial

    def temp(self, c):
        return (f"{c * 9 / 5 + 32:.1f}F" if self.imperial else f"{c:.1f}C") if c is not None else "--"

    def speed(self, ms):
        return (f"{ms * 2.236936:.1f}mph" if self.imperial else f"{ms:.1f}m/s") if ms is not None else "--"

    def press(self, mb):
        return (f"{mb * 0.0295299830714:.2f}inHg" if self.imperial else f"{mb:.1f}mb") if mb is not None else "--"

    def rain(self, mm):
        return (f"{mm / 25.4:.3f}in" if self.imperial else f"{mm:.2f}mm") if mm is not None else "--"

    def dist(self, km):
        return (f"{km * 0.621371:.1f}mi" if self.imperial else f"{km:.1f}km") if km is not None else "--"


def show_obs_st(msg, u):
    arr = msg.get("obs", [[]])[0]
    if len(arr) < len(OBS_ST_FIELDS):
        print(f"  !! obs_st has {len(arr)} fields, spec says {len(OBS_ST_FIELDS)}")
    o = dict(zip(OBS_ST_FIELDS, arr))
    print(f"[{clock(o.get('time_epoch', 0))}] obs_st  {msg.get('serial_number', '?')}")
    print(f"    temp {u.temp(o.get('air_temp'))}   humidity {o.get('humidity', '--')}%"
          f"   pressure {u.press(o.get('pressure'))}")
    print(f"    wind avg {u.speed(o.get('wind_avg'))}  gust {u.speed(o.get('wind_gust'))}"
          f"  lull {u.speed(o.get('wind_lull'))}  dir {o.get('wind_dir', '--')}deg"
          f" {bearing(o.get('wind_dir', 0))}")
    print(f"    uv {o.get('uv', '--')}   solar {o.get('solar_radiation', '--')}W/m2"
          f"   lux {o.get('illuminance', '--')}")
    print(f"    rain(1min) {u.rain(o.get('rain_last_min'))}"
          f"   type {PRECIP_TYPE.get(o.get('precip_type'), '?')}"
          f"   lightning {o.get('lightning_count', 0)} @ {u.dist(o.get('lightning_avg_dist'))}")
    print(f"    battery {o.get('battery', '--')}V   report interval {o.get('report_interval', '--')}min")


def show_rapid_wind(msg, u):
    ob = msg.get("ob", [])
    if len(ob) < 3:
        return
    epoch, speed, direction = ob[0], ob[1], ob[2]
    print(f"[{clock(epoch)}] rapid_wind  {u.speed(speed)} from {direction}deg {bearing(direction)}")


def show_strike(msg, u):
    ev = msg.get("evt", [])
    if len(ev) < 3:
        return
    print(f"[{clock(ev[0])}] *** LIGHTNING *** {u.dist(ev[1])} away, energy {ev[2]}")


def show_precip(msg, u):
    ev = msg.get("evt", [])
    print(f"[{clock(ev[0] if ev else 0)}] *** RAIN STARTED ***")


def show_status(msg, u):
    t = msg["type"]
    if t == "hub_status":
        print(f"[{clock(msg.get('timestamp', 0))}] hub_status  {msg.get('serial_number')}"
              f"  fw {msg.get('firmware_revision')}  uptime {msg.get('uptime')}s"
              f"  rssi {msg.get('rssi')}")
    else:
        print(f"[{clock(msg.get('timestamp', 0))}] device_status  {msg.get('serial_number')}"
              f"  {msg.get('voltage')}V  rssi {msg.get('rssi')}"
              f"  sensor_status 0x{msg.get('sensor_status', 0):x}")


HANDLERS = {
    "obs_st": show_obs_st,
    "rapid_wind": show_rapid_wind,
    "evt_strike": show_strike,
    "evt_precip": show_precip,
    "hub_status": show_status,
    "device_status": show_status,
}


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--raw", action="store_true", help="also print the raw JSON")
    ap.add_argument("--units", choices=["imperial", "metric"], default="imperial")
    ap.add_argument("--only", help="comma-separated message types to show")
    ap.add_argument("--port", type=int, default=PORT)
    args = ap.parse_args()

    u = Units(args.units == "imperial")
    wanted = set(args.only.split(",")) if args.only else None

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    try:
        sock.bind(("0.0.0.0", args.port))
    except OSError as e:
        sys.exit(f"cannot bind UDP {args.port}: {e}\n"
                 f"(something else already listening? WeatherFlow's own app perhaps)")

    print(f"listening on UDP {args.port} ... (Ctrl-C to stop)")
    print("if nothing appears within ~10s, the hub is not on this broadcast domain\n")

    seen = {}
    try:
        while True:
            data, addr = sock.recvfrom(2048)
            try:
                msg = json.loads(data.decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError):
                print(f"[??] undecodable {len(data)}B from {addr[0]}")
                continue

            mtype = msg.get("type", "unknown")
            seen[mtype] = seen.get(mtype, 0) + 1
            if wanted and mtype not in wanted:
                continue

            handler = HANDLERS.get(mtype)
            if handler:
                handler(msg, u)
            else:
                print(f"[??] unhandled type '{mtype}' from {addr[0]}: {msg}")
            if args.raw:
                print(f"    raw: {json.dumps(msg)}")
    except KeyboardInterrupt:
        print("\n\nmessage counts:")
        for k, v in sorted(seen.items()):
            print(f"  {k:<16} {v}")


if __name__ == "__main__":
    main()
