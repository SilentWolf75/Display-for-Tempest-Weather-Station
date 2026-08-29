#!/usr/bin/env python3
"""Convert CC0 UI sounds into SPIFFS-ready 24 kHz mono 16-bit WAV files.

Sources (all CC0, no attribution required):
  Kenney Interface Sounds — https://kenney.nl/assets/interface-sounds
  Pack mirror: https://github.com/Calinou/kenney-interface-sounds

    python tools/prepare_audio.py
"""

from __future__ import annotations

import array
import io
import struct
import sys
import urllib.request
import wave
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
OUT_DIR = REPO / "firmware" / "spiffs" / "audio"
TARGET_SR = 24000
KENNEY_ZIP_URL = (
    "https://github.com/Calinou/kenney-interface-sounds/archive/refs/heads/master.zip"
)
CACHE = REPO / ".cache" / "kenney-interface-sounds.zip"

# Kenney source -> SPIFFS filename
MAPPING = {
    "click_001.wav": "keyclick.wav",       # soft keyboard tap (~115 ms)
    "bong_001.wav": "chime.wav",           # short clock-like ding (~130 ms)
    "confirmation_001.wav": "notify.wav",  # UI notification (~295 ms)
}


def fetch_kenney_zip() -> bytes:
    CACHE.parent.mkdir(parents=True, exist_ok=True)
    if not CACHE.exists() or CACHE.stat().st_size < 10000:
        print(f"downloading Kenney Interface Sounds (CC0)...")
        with urllib.request.urlopen(KENNEY_ZIP_URL, timeout=120) as resp:
            CACHE.write_bytes(resp.read())
    return CACHE.read_bytes()


def read_wav_mono(path: str, data: bytes) -> tuple[int, array.array]:
    with wave.open(io.BytesIO(data), "rb") as wf:
        sr = wf.getframerate()
        ch = wf.getnchannels()
        sw = wf.getsampwidth()
        if sw != 2:
            raise ValueError(f"{path}: expected 16-bit PCM, got {sw * 8}-bit")
        raw = wf.readframes(wf.getnframes())
    samples = array.array("h")
    samples.frombytes(raw)
    if ch == 2:
        stereo = samples
        samples = array.array("h", (stereo[i] for i in range(0, len(stereo), 2)))
    elif ch != 1:
        raise ValueError(f"{path}: expected mono or stereo, got {ch} channels")
    return sr, samples


def resample_linear(src_sr: int, samples: array.array, dst_sr: int) -> array.array:
    if src_sr == dst_sr or not samples:
        return samples
    src_len = len(samples)
    dst_len = max(1, int(round(src_len * dst_sr / src_sr)))
    out = array.array("h", [0] * dst_len)
    for i in range(dst_len):
        src_pos = i * (src_len - 1) / max(dst_len - 1, 1)
        idx = int(src_pos)
        frac = src_pos - idx
        s0 = samples[idx]
        s1 = samples[min(idx + 1, src_len - 1)]
        out[i] = int(round(s0 + (s1 - s0) * frac))
    return out


def normalize(samples: array.array, target_peak: int = 28000) -> array.array:
    peak = max(abs(s) for s in samples) if samples else 0
    if peak <= 0:
        return samples
    scale = target_peak / peak
    return array.array("h", (max(-32767, min(32767, int(round(s * scale)))) for s in samples))


def write_wav(path: Path, samples: array.array, sr: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sr)
        wf.writeframes(samples.tobytes())


def main() -> int:
    zbytes = fetch_kenney_zip()
    zf = zipfile.ZipFile(io.BytesIO(zbytes))
    prefix = "kenney-interface-sounds-master/addons/kenney_interface_sounds/"

    for src_name, dst_name in MAPPING.items():
        src_path = prefix + src_name
        if src_path not in zf.namelist():
            print(f"error: {src_name} not found in Kenney zip", file=sys.stderr)
            return 1
        src_sr, samples = read_wav_mono(src_name, zf.read(src_path))
        samples = resample_linear(src_sr, samples, TARGET_SR)
        samples = normalize(samples)
        dst = OUT_DIR / dst_name
        write_wav(dst, samples, TARGET_SR)
        ms = len(samples) * 1000 / TARGET_SR
        print(f"{src_name} -> {dst.relative_to(REPO)} ({ms:.0f} ms, {dst.stat().st_size} bytes)")

    print("done — rebuild firmware so SPIFFS picks up the new WAV files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
