# Hardware: Elecrow CrowPanel Advance 10.1"

Board part number seen in the wild: **DIS04310H / DHE04310D** (revisions V1.1 and V1.2).
Purchased via Amazon (ASIN B0GCJVLP7B) — same unit as the Elecrow store listing.

## Verified specifications

| Item | Value | Source |
|---|---|---|
| Main SoC | ESP32-P4, dual-core RISC-V @ 400 MHz + LP core | Elecrow store page |
| Memory | 16 MB flash, 32 MB PSRAM, 768 KB L2MEM | Elecrow store page |
| Radio | **ESP32-C6-MINI-1 co-processor** over SDIO — Wi-Fi 6 (2.4 GHz), BT 5.3 | Elecrow store page |
| Panel | 10.1" IPS, 1024x600, 16.7M colour, 400 cd/m² | Elecrow store page |
| Panel driver | **EK79007** over **MIPI-DSI** | SquareLine forum thread #6441 |
| Touch | **GT911** capacitive, 5-point, I2C | SquareLine forum thread #6441 |
| I2C bus | GPIO 7 (SDA) / GPIO 8 (SCL) — shared by GT911 and Grove header | Mjrovai/CrowPanel-10.1inch |
| Audio | NS4168 codec, dual speaker (2.0 mm PH), 1x mic | Elecrow store page |
| Storage | microSD slot | Elecrow store page |
| USB | 2x USB-C (one UART debug, one USB 2.0 device) | Elecrow store page |
| Expansion | 11-pin GPIO header, Crowtail/Grove I2C + UART, MIPI-CSI camera header | Elecrow store page |
| Power | 5 V / 2 A, plus lithium battery socket with charger | Elecrow store page |
| Dimensions | 248 x 147 mm | Elecrow store page |

## THE critical architectural fact

**The ESP32-P4 has no radio.** All networking is proxied to the ESP32-C6 over SDIO using
Espressif's **ESP-Hosted-MCU** transport, surfaced to application code through the
`esp_wifi_remote` component. From the app's point of view the normal `esp_wifi_*` and
LwIP socket APIs work — but:

- The C6 must be running matching **ESP-Hosted slave firmware**. Elecrow ships it
  pre-flashed. If the C6 firmware version and the `esp_hosted` component version
  disagree, Wi-Fi init fails at boot with a handshake error.
- **UDP broadcast reception is the least-proven path on this board.** Broadcast frames
  have to be accepted by the C6 and forwarded up the SDIO link. This is the single
  largest technical risk in this project — see `docs/roadmap.md`, Milestone 2, which
  exists purely to de-risk it before any UI work happens.
- Fallback if broadcast turns out to be unreliable: Tempest WebSocket API over TLS
  (unicast, ordinary TCP, definitely works through ESP-Hosted).

## Pin assignments — UNVERIFIED, confirm against hardware

Only two pin facts are documented by a source I trust:

- I2C SDA/SCL on **GPIO 7 / GPIO 8** (Mjrovai lesson 10 — bus shared with the DHT20).
- An LED / backlight-adjacent control on **GPIO 48** (Elecrow wiki, described as "LED
  control via the UART1 interface" — ambiguous, do not assume this is the backlight).

Everything else — backlight PWM pin, panel reset, GT911 INT/RST, SD card pins, the
MIPI-DSI lane configuration and panel timing — must be read off the Elecrow schematic
and their official V1.1/V1.2 example project once the board is in hand.

`firmware/main/board_pins.h` holds these as named constants with `#warning`-guarded
placeholders. **Nothing in this project invents a pin number.**

## Toolchain (verified present on this machine)

- ESP-IDF **v5.5.3** at `C:\esp\v5.5.3\esp-idf` (>= 5.3 required for P4 + MIPI-DSI)
- riscv32-esp-elf **14.2.0_20251107**
- Activate in PowerShell: `C:\Espressif\tools\Microsoft.v5.5.3.PowerShell_profile.ps1`

## Managed components this project pulls

| Component | Purpose |
|---|---|
| `espressif/esp_wifi_remote` | Wi-Fi API shim for the radio-less P4 |
| `espressif/esp_hosted` | SDIO transport to the C6 |
| `espressif/esp_lcd_ek79007` | MIPI-DSI panel driver |
| `espressif/esp_lcd_touch_gt911` | Touch controller |
| `espressif/esp_lvgl_port` | LVGL <-> esp_lcd glue, tick + task + locking |
| `lvgl/lvgl ~9.2.2` | UI toolkit (version pinned by Elecrow's SquareLine workflow) |

## Flashing notes

- Power the board from a **5 V/2 A external supply**, not a PC USB port. The panel plus
  peripherals will brown out on a typical 500 mA host port and the symptom looks like a
  flashing failure.
- Manual bootloader entry: hold BOOT, plug USB, keep holding 1-2 s, release.

## SquareLine Studio

There is no stock SquareLine profile for this board. The working recipe (confirmed by a
SquareLine developer on their forum) is: create a **generic 1024x600, 16-bit** project,
export the C files into `firmware/main/ui/`, and call `ui_init()` after display init.
SquareLine 1.5.1 pairs with LVGL 9.2.2.
