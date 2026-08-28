#!/usr/bin/env python3
"""Capture a live screenshot of the ESP32-P4 display over serial UART.

Usage:
    python tools/screenshot.py [port] [output.png]
"""

import sys
import time
import serial
from pathlib import Path
from PIL import Image

def capture_screenshot(port='COM18', baud=115200, out_path='screenshot.png'):
    print(f"Connecting to {port} @ {baud}...")
    s = serial.Serial()
    s.port = port
    s.baudrate = baud
    s.timeout = 3.0
    s.dtr = False
    s.rts = False
    s.open()
    
    time.sleep(1.0)
    s.reset_input_buffer()
    
    print("Requesting screenshot from device...")
    s.write(b"\r\nscreenshot\r\n")
    s.flush()
    
    # Wait for header marker
    t0 = time.time()
    header = ""
    while time.time() - t0 < 10.0:
        raw_line = s.readline()
        line = raw_line.decode('latin1', errors='replace').strip()
        if "===SCREENSHOT_HEX_START:" in line:
            header = line
            break
            
    if not header:
        print("Error: Did not receive SCREENSHOT_HEX_START marker.")
        s.close()
        return False
        
    print(f"Received header: {header}")
    parts = header.replace("=", "").split(":")
    w = int(parts[1])
    h = int(parts[2])
    total_pixels = w * h
    
    print(f"Receiving Hex-RLE stream for {w}x{h} ({total_pixels} pixels)...")
    pixel_list = []
    t_start = time.time()
    
    while len(pixel_list) < total_pixels and (time.time() - t_start < 45.0):
        raw_line = s.readline()
        if not raw_line:
            continue
        line = raw_line.decode('latin1', errors='replace').strip()
        if "===SCREENSHOT_HEX_END===" in line:
            break
        if len(line) == 8:
            try:
                run_len = int(line[:4], 16)
                color = int(line[4:8], 16)
                r = ((color >> 11) & 0x1F) * 255 // 31
                g = ((color >> 5) & 0x3F) * 255 // 63
                b = (color & 0x1F) * 255 // 31
                pixel_list.extend([(r, g, b)] * run_len)
            except ValueError:
                continue
                
    s.close()
    
    if len(pixel_list) < total_pixels:
        print(f"Warning: partial frame ({len(pixel_list)}/{total_pixels} pixels). Filling remainder...")
        pixel_list.extend([(0, 0, 0)] * (total_pixels - len(pixel_list)))
        
    print(f"Rendering {w}x{h} PNG...")
    img = Image.new("RGB", (w, h))
    img.putdata(pixel_list[:total_pixels])
    
    out = Path(out_path).resolve()
    out.parent.mkdir(parents=True, exist_ok=True)
    img.save(str(out), "PNG")
    print(f"Screenshot successfully saved to {out}")
    return True

if __name__ == "__main__":
    port = sys.argv[1] if len(sys.argv) > 1 else 'COM18'
    out_file = sys.argv[2] if len(sys.argv) > 2 else 'screenshot.png'
    success = capture_screenshot(port=port, out_path=out_file)
    sys.exit(0 if success else 1)
