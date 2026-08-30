# Bring-up checklist

Work top to bottom the day the panel arrives. Each step has an explicit pass
condition, because the failure modes on this board look alike from the outside:
a blank screen is equally consistent with wrong pins, a brownout, and a
co-processor that never handshook.

## Before you plug anything in

**Use a 5 V / 2 A external supply, not a PC USB port.** The panel plus
peripherals will brown out on a typical 500 mA host port, and the symptom looks
exactly like a flashing failure. `diag_report_hardware()` prints the reset
reason at boot and calls out `ESP_RST_BROWNOUT` by name for this reason.

## 1. Flash and watch the console

```bash
cd firmware && idf.py -p COM7 flash monitor
```

Manual bootloader entry if it will not connect: hold BOOT, plug USB, keep
holding 1–2 s, release.

Diagnostics run **before** the display initialises, so this output arrives even
if the panel stays dark:

```
diag: --- hardware ---
diag:   chip      : esp32p4, 2 core(s), silicon rev X.Y
diag:   flash     : 16 MB
diag:   psram     : ~32000 KB free
diag:   last reset: power on
diag:   running   : ota_0 at 0x020000 (3072 KB)
diag:   icons     : storage partition present
```

**Pass:** flash reads 16 MB, PSRAM ~32 MB, storage partition present.
**If PSRAM is 0** the panel will never render — check `CONFIG_SPIRAM` survived.

## 2. I²C scan — the pin-verification gate

Immediately after, the scan runs:

```
diag: --- I2C scan: SDA=GPIO7 SCL=GPIO8 @ 400000 Hz ---
diag:   0x5D  GT911 touch (default address)
diag:   1 device(s) found
```

**Pass:** something answers at **0x5D or 0x14** — that is the GT911, and it
confirms GPIO 7/8 are correct.

**If nothing answers**, do not assume the touch controller is dead. In order:

1. Try the other address — the GT911 straps to 0x5D or 0x14 at power-up
   depending on the INT pin, and `BOARD_TOUCH_INT_GPIO` is currently `-1`.
2. Check the Elecrow schematic for the real SDA/SCL. GPIO 7/8 comes from the
   Mjrovai lessons, which is good evidence but not the schematic.
3. Check for external pull-ups. The code enables internal ones, which are weak.

Everything else in `board_pins.h` is still a placeholder. **This scan only
proves the I²C pins.**

## 3. Panel

If the screen stays dark but the console is healthy, the panel config is wrong,
not the board. In order of likelihood:

1. `BOARD_LCD_BACKLIGHT_GPIO` is 48, taken from an ambiguous wiki line about
   "LED control via the UART1 interface". It may be a status LED. Try driving
   other candidates, or check whether the backlight is even software-controlled.
2. MIPI-DSI timings come from the `esp_lcd_ek79007` component's own macros, not
   hand-entered numbers, so they are probably right — but Elecrow fitted a
   specific panel and the component ships one default.
3. `BOARD_LCD_RESET_GPIO` is `-1`. If the panel needs a hardware reset it will
   never initialise.

Compare against Elecrow's official V1.1/V1.2 example before changing anything.
Once verified, define `BOARD_PINS_VERIFIED` to silence the `#warning`.

## 4. Wi-Fi — the ESP-Hosted handshake

```
diag: --- network (ESP32-C6 over SDIO) ---
diag:   associated: <your ssid>
diag:   rssi      : -45 dBm, channel 6
```

**If `esp_wifi_init()` itself failed earlier in the log**, this is not a Wi-Fi
problem. The P4 has no radio; the ESP32-C6 must be running ESP-Hosted slave
firmware matching the `espressif/esp_hosted` component version this project
pins (3.0.6). Reflash the C6 rather than debugging the Wi-Fi config.

## 5. THE risk gate — UDP broadcast through ESP-Hosted

This is the assumption the whole design rests on, and the only one that cannot
be checked without hardware.

Watch for the 30-second health line:

```
main: udp packets: 47 (+21)  wifi: up  heap: ...
```

**Pass:** the count climbs. Your hub broadcasts `rapid_wind` every 3 s, so
expect roughly +21 per 30 s window once `obs_st` and `hub_status` are included.

**If it stays at 0 while Wi-Fi is up**, `main.c` says so explicitly every 30
seconds. The C6 is not forwarding broadcast frames. Options in order:

1. Join the multicast group / `IP_ADD_MEMBERSHIP` instead of relying on
   broadcast.
2. Check `esp_hosted` config for a broadcast/promiscuous filter.
3. Fall back to the Tempest **WebSocket** API — unicast TLS, guaranteed to
   traverse. The `wx_state` abstraction means this does not touch the UI.

Cross-check against a PC on the same LAN, which is known to work:

```bash
python tools/tempest_listen.py
```

If the PC sees traffic and the panel does not, the problem is the SDIO link,
not the network.

## 6. Touch

The settings gear is bottom-right. If touch is dead the gear is unreachable and
you are locked out of the settings screen — which is why step 2 matters before
you rely on it. `display.c` deliberately continues without touch rather than
failing to boot, so a dead GT911 still gives you a working weather display.

## 7. OTA

Once on the network, browse to `http://tempest.local:8080/ota` (or
`http://<device-ip>:8080/ota`). Upload **`tempest_display.bin`** from a
[GitHub release](https://github.com/SilentWolf75/tempest-weather-display/releases/latest)
— the app image only, about 2.6 MB. The merged factory image is for the
USB web flasher (`install.html` in the same release) and must not be posted
to `/ota`.

Scripted:

```bash
curl -X POST --data-binary @tempest_display.bin -H "X-OTA-Password: yourpassword" http://tempest.local:8080/ota/update
```

**Set `CONFIG_OTA_PASSWORD` in menuconfig.** Empty means anyone on your network
can reflash the panel; it is logged loudly at boot. There is no TLS either way —
this is a LAN convenience, not a security boundary.

**Rollback is armed.** A new image boots as `PENDING_VERIFY` and reverts on the
next reset unless `ota_mark_valid()` runs, which happens only after NVS, the
state layer, the display and the UDP listener have all come up. An image that
crashes before that point undoes itself instead of stranding a wall-mounted
panel.

## Flash layout

| Partition | Offset | Size |
|---|---|---|
| bootloader | 0x2000 | ~21 KB (34 % headroom) |
| partition table | 0xA000 | — |
| nvs | 0xB000 | 24 K |
| otadata | 0x11000 | 8 K |
| phy_init | 0x13000 | 4 K |
| ota_0 | 0x20000 | 3 M |
| ota_1 | 0x320000 | 3 M |
| storage (icons) | 0x620000 | 6 M |

The table sits at 0xA000 rather than the 0x8000 default: the OTA-capable
bootloader grew to within 256 bytes of the default budget, and overflowing it
is a hard build failure with an unhelpful message.

App is ~2.04 MB of the 3 MB slot. If it ever approaches 3 MB, resize both slots
together — they must match.
