# Blank / light-blue display investigation

## Evidence
The pre-update live capture on COM12 showed stable free heap (294039 bytes internal, about 31.6 MB PSRAM), continuing UDP reception, indoor readings and SD-card writes while the user reported a blank display. A running LVGL clock did not establish renderer progress. This does not prove a specific cause for that incident.

The installed IDF 5.5.3 DPI driver explicitly documents persistent blue output after a PSRAM scanout underrun. Its EK79007/DPI implementation has no display on/off operation: the previous recovery calls could not restart it. An older August 28 log separately contains persistent GT911 failures; those were not present in the current live capture.

## Changes
- Give DW-GDMA scanout reads priority 15 on both master ports, leaving writes at default 0. Keep the validated 52 MHz panel timing and 1 Gbps DSI link unchanged.
- Stop full-screen recovery redraws after routine HTTPS completion. Preserve a boot repaint and I2C-only recovery for actual sensor failures.
- Atomically merge and consume recovery requests so a network task cannot discard a pending bus reset.
- Remove unsupported display on/off recovery and preserve selected brightness.
- Release an LVGL flush when the driver rejects the transfer, since no completion callback will arrive for that rejected submission.
- Log completed render cycles, responsiveness of the LVGL lock, minimum internal free heap and largest internal free block.

## Validation
ESP-IDF firmware build passed. Existing host weather and JavaScript dashboard/OTA tests passed. A focused host harness compiled the production recovery functions and passed network no-op, mixed boot/sensor requests, touch failure recovery, brightness preservation and balanced critical-section checks.

Hardware installation and observation results will be recorded below. These changes address identified failure paths; an extended on-device run is still needed to establish whether the intermittent symptom is eliminated.

## Installation
A full 16 MB backup was saved locally before modification (SHA-256 74db1d2ec5cd2addfa8caacf36450867ed7e12027ab35f5cf24e775aec1b177d). Bootloader, partition table and SPIFFS matched the build byte-for-byte, so only ota_0 and boot selection were written. Serial flashing verified the application hash. NVS and the second application slot were not written.

The device restarted and completed Tempest backfill, forecast, NWS and AQI requests. At the first health sample it reported 485 completed render cycles, a responsive LVGL lock, minimum internal heap 286823 bytes and largest free internal block 176128 bytes.

The user confirmed the weather screen and touch work after installation. During the 100-second serial capture, render counts advanced 485 -> 1041 -> 1611, internal free heap remained 287275 bytes, and all three LVGL lock checks succeeded. No underrun, pixel transfer, touch or watchdog errors appeared in that capture. Selected brightness returned to 85%. Long-duration recurrence remains unverified.


## Recurrence and bandwidth configuration
The first repair was insufficient. After the Sky header update, the user reported blank output without touching the screen, with the backlight still on. Renders continued 1026 -> 1598 and internal free heap stayed at 288931 bytes. No underrun was captured, so insufficient bandwidth remains a hypothesis.

The follow-up enables performance optimization, PSRAM XIP, 256 KB L2 cache and 128-byte cache lines, following Espressif's LCD FAQ:
https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/peripherals/lcd.html
The original 200 MHz PSRAM, RGB565 format and panel timing are retained. Optimized compilation exposed string-copy warnings; flagged copies were replaced with explicitly bounded, terminated formatting and SSID display was limited to 32 bytes. Compiler checks remain enabled. Firmware build and rebuilt host weather regressions passed.

Automatic approval review rejected a combined bootloader/application update. Inspection of SDK application startup (cpu_start.c image_process under CONFIG_SPIRAM_FLASH_LOAD_TO_PSRAM) verified that relocation is application-owned. An application-only update succeeded with flash hash verification; bootloader, NVS, partition table and assets were left intact.

At roughly two minutes the user confirmed that the screen remained on. The first four health samples showed renders 516 -> 1100 -> 1681 -> 2261, responsive LVGL locks, and internal free heap stable at 155007 bytes. The larger cache uses additional internal memory; minimum observed internal heap was 154027 bytes with a largest free block of 63488 bytes. Extended observation continues.

The 330-second post-update capture completed. Ten health samples showed render counts increasing 516 -> 5768, all LVGL lock checks succeeded, and internal free heap stayed 155007 bytes. The user confirmed the display stayed on and then confirmed Sky remained visible with warning and conditions on separate rows. No longer-duration or overnight stability claim is made. Initial HTTPS downloads completed; a subsequent HTTPS cycle also completed at uptime about 320-321 seconds, alongside continued UDP and SD-card activity.

## Power-source finding after Week layout update
The blank screen recurred again. Startup-to-100-second capture showed renders 502 -> 1085 -> 1664, all LVGL lock checks succeeding and stable internal heap. No DPI underrun was captured; one transient touch I2C write error occurred during startup. The user then confirmed the board is powered only from computer USB. The existing bring-up requirement is a regulated 5 V / 2 A external supply. Further firmware experimentation is paused pending a wall-powered test; insufficient power is a leading hypothesis, not a measured or confirmed root cause. The Week spacing patch is installed and builds successfully, but its visible layout remains unconfirmed because of the blank screen.
