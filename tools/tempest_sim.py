#!/usr/bin/env python3
"""Broadcast synthetic Tempest frames on UDP 50222 so the panel can be developed
without the real hub (or without the real hub being reachable).

Emits the same cadence as a real station: rapid_wind every 3s, obs_st every 60s,
hub_status every 10s, plus occasional rain and lightning events.

    python tools/tempest_sim.py
    python tools/tempest_sim.py --fast          # 1s/10s, for impatient UI work
    python tools/tempest_sim.py --storm         # frequent strikes and rain
    python tools/tempest_sim.py --host 192.168.1.42   # unicast at one device
"""

import argparse
import json
import math
import random
import socket
import time

PORT = 50222
HUB_SN = "HB-00013030"
DEVICE_SN = "ST-00000512"


class Weather:
    """A slowly-wandering plausible weather state."""

    def __init__(self):
        self.t = 0.0
        self.temp = 21.0            # C
        self.humidity = 55.0        # %
        self.pressure = 1013.2      # mb
        self.wind_base = 3.0        # m/s
        self.wind_dir = 200.0       # deg
        self.rain_accum = 0.0       # mm this minute
        self.strikes = 0
        self.strike_dist = 0.0

    def step(self, dt):
        self.t += dt
        # diurnal-ish drift plus noise
        self.temp += (math.sin(self.t / 900.0) * 0.05 + random.uniform(-0.03, 0.03))
        self.temp = max(-25.0, min(45.0, self.temp))
        self.humidity += random.uniform(-0.4, 0.4)
        self.humidity = max(5.0, min(100.0, self.humidity))
        self.pressure += random.uniform(-0.05, 0.05)
        self.pressure = max(960.0, min(1050.0, self.pressure))
        self.wind_base += random.uniform(-0.15, 0.15)
        self.wind_base = max(0.0, min(25.0, self.wind_base))
        self.wind_dir = (self.wind_dir + random.uniform(-4, 4)) % 360

    def gust(self):
        return max(0.0, self.wind_base + abs(random.gauss(0, 1.2)))

    def lull(self):
        return max(0.0, self.wind_base - abs(random.gauss(0, 0.6)))

    def instant(self):
        return max(0.0, random.gauss(self.wind_base, 0.8))

    def uv(self):
        # crude day/night curve on a 20-minute simulated "day" so it visibly moves
        phase = math.sin(self.t / 1200.0 * math.pi)
        return round(max(0.0, phase * 9.0), 1)

    def solar(self):
        return round(self.uv() * 110.0, 1)

    def lux(self):
        return round(self.uv() * 12000.0)


def obs_st(w):
    return {
        "serial_number": DEVICE_SN,
        "type": "obs_st",
        "hub_sn": HUB_SN,
        "obs": [[
            int(time.time()),
            round(w.lull(), 2),
            round(w.wind_base, 2),
            round(w.gust(), 2),
            int(w.wind_dir),
            3,
            round(w.pressure, 2),
            round(w.temp, 2),
            round(w.humidity, 2),
            w.lux(),
            w.uv(),
            w.solar(),
            round(w.rain_accum, 6),
            1 if w.rain_accum > 0 else 0,
            round(w.strike_dist, 1),
            w.strikes,
            round(random.uniform(2.35, 2.80), 3),
            1,
        ]],
        "firmware_revision": 129,
    }


def rapid_wind(w):
    return {
        "serial_number": DEVICE_SN,
        "type": "rapid_wind",
        "hub_sn": HUB_SN,
        "ob": [int(time.time()), round(w.instant(), 2), int(w.wind_dir)],
    }


def hub_status(uptime):
    return {
        "serial_number": HUB_SN,
        "type": "hub_status",
        "firmware_revision": "194",
        "uptime": uptime,
        "rssi": random.randint(-70, -40),
        "timestamp": int(time.time()),
        "reset_flags": "BOR,PIN,POR",
        "seq": uptime // 10,
        "radio_stats": [25, 5, 0, 3, 2400],
    }


def device_status(uptime):
    return {
        "serial_number": DEVICE_SN,
        "type": "device_status",
        "hub_sn": HUB_SN,
        "timestamp": int(time.time()),
        "uptime": uptime,
        "voltage": round(random.uniform(2.35, 2.80), 2),
        "firmware_revision": 129,
        "rssi": random.randint(-70, -40),
        "hub_rssi": random.randint(-70, -40),
        "sensor_status": 0,
        "debug": 0,
    }


def evt_strike(dist_km, energy):
    return {
        "serial_number": DEVICE_SN,
        "type": "evt_strike",
        "hub_sn": HUB_SN,
        "evt": [int(time.time()), round(dist_km, 1), energy],
    }


def evt_precip():
    return {
        "serial_number": DEVICE_SN,
        "type": "evt_precip",
        "hub_sn": HUB_SN,
        "evt": [int(time.time())],
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="255.255.255.255",
                    help="destination; default broadcast, or a specific IP to unicast")
    ap.add_argument("--port", type=int, default=PORT)
    ap.add_argument("--fast", action="store_true",
                    help="rapid_wind every 1s, obs_st every 10s")
    ap.add_argument("--storm", action="store_true",
                    help="frequent lightning and rain events")
    ap.add_argument("--quiet", action="store_true", help="do not echo what is sent")
    args = ap.parse_args()

    rapid_every = 1.0 if args.fast else 3.0
    obs_every = 10.0 if args.fast else 60.0
    status_every = 10.0

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

    w = Weather()
    start = time.time()
    next_rapid = next_obs = next_status = start
    strike_chance = 0.08 if args.storm else 0.004
    rain_chance = 0.10 if args.storm else 0.005

    def send(msg):
        sock.sendto(json.dumps(msg).encode("utf-8"), (args.host, args.port))
        if not args.quiet:
            print(f"-> {msg['type']}")

    print(f"simulating a Tempest station -> {args.host}:{args.port}")
    print(f"   rapid_wind every {rapid_every}s, obs_st every {obs_every}s"
          f"{'  [STORM]' if args.storm else ''}")
    print("   Ctrl-C to stop\n")

    try:
        while True:
            now = time.time()
            uptime = int(now - start)
            w.step(0.2)

            if now >= next_rapid:
                send(rapid_wind(w))
                next_rapid = now + rapid_every
                if random.random() < strike_chance:
                    w.strikes += 1
                    w.strike_dist = random.uniform(1, 40)
                    send(evt_strike(w.strike_dist, random.randint(1000, 300000)))
                if random.random() < rain_chance:
                    w.rain_accum += random.uniform(0.01, 0.6)
                    send(evt_precip())

            if now >= next_obs:
                send(obs_st(w))
                w.rain_accum = 0.0     # obs_st reports the previous minute, then resets
                w.strikes = 0
                next_obs = now + obs_every

            if now >= next_status:
                send(hub_status(uptime))
                send(device_status(uptime))
                next_status = now + status_every

            time.sleep(0.2)
    except KeyboardInterrupt:
        print("\nstopped")


if __name__ == "__main__":
    main()
