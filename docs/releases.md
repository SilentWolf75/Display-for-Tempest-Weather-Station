# Publishing firmware

Build releases separately from the device build, with TEMPEST_PUBLIC_RELEASE=ON. This leaves local secrets.h untouched and generates empty Wi-Fi and Tempest credential defaults. Never publish the normal device build, flash backups, NVS dumps or serial logs.

With ESP-IDF initialized, run from firmware:
    idf.py -B ../.cache/release-build -D SDKCONFIG=../.cache/release-sdkconfig -D TEMPEST_PUBLIC_RELEASE=ON build

Use an absolute SDKCONFIG path if the shell or CMake resolves relative paths differently. Seed release-sdkconfig from a reviewed target configuration before the first build. This project targets CrowPanel 10.1 ESP32-P4 revision 1.x; do not force-flash the image onto incompatible revision 3.x hardware.

Package tempest_display.bin for OTA, the merged image at offset zero for USB installation, and web/index.html as install.html. Include SHA256SUMS.txt. Verify generated credential defaults are empty and check binaries against the local secret values before uploading. Preserve saved credentials and the bootloader on device updates; a merged factory installation erases/replaces the partition layout and icon storage.

The v1.0.1 changes improve icon lifecycle, layout, weather history, alert handling and OTA validation. Intermittent LCD blanking has not been proven resolved; local investigation found USB-only power and a separate animation use-after-free. Use the documented external 5 V / 2 A supply.
