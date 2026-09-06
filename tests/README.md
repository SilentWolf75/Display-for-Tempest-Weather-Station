# Regression checks

These tests compile the production history, daily totals, observation state,
alert policy, WebSocket fallback, and OTA health functions on a computer.
Clock, storage, socket, and RTOS adapters are simulated. They do not validate
ESP-Hosted transport, actual flash writes, display rendering, or task scheduling
on the board.

With CMake and a C compiler available:

    cmake -S tests/host -B build/host -DCMAKE_BUILD_TYPE=Debug
    cmake --build build/host
    ctest --test-dir build/host --output-on-failure
    node tests/ota.test.cjs
    node tests/dashboard.test.cjs

On Windows, run these from a Visual Studio Developer Command Prompt, or load
the Visual Studio C++ build environment first. Tests use fake credentials,
never firmware/main/secrets.h.

Covered cases: missing/invalid readings; duplicate observations; 7-day,
month and year boundaries; zero totals; saved-total restoration; missing
history buckets; live data interleaved with backfill; failed backfill allocation;
aged-out history; separate data freshness; valid zero AQI; alert ordering,
expiry and ISO timezone offsets; WebSocket self-traffic versus UDP recovery;
Wi-Fi loss; OTA display/UI/timer readiness; raw browser uploads, passwords,
progress, HTTP rejection, connection loss, and timeout.

Icon lifecycle regression: run python3 tests/test_icon_lifecycle.py with a C compiler available (on Windows, use a Visual Studio developer shell). It exercises production icon functions with a checked LVGL timer test double: repeated pause/resume, changing artwork while hidden, day/night variants and repeat static updates.
