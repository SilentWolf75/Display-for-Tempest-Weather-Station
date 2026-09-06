# Current weather icon repair

## Findings
The app deleted the Lottie animation when leaving Live. LVGL 9.2 retains its animation pointer for the lifetime of the widget, and lv_lottie_set_src_data writes through that pointer when artwork changes. Returning to Live reloaded the JSON without recreating the timer. This is a confirmed use-after-free and explains a stopped animation; it can also leave stale artwork or corrupt other state. Its relationship to earlier blank-screen incidents is not established.

The current conditions card was refreshed only when observations or forecasts changed, so a sunrise/sunset transition without new readings did not necessarily update it. Opening Settings before animation startup also unconditionally marked the hero animation setup complete.

## Repair
Preserve LVGL's animation object and use visibility to pause drawing. LVGL already skips rendering hidden Lottie widgets. Static icons retain the timer with its render callback disabled, and retain the callback separately so later artwork changes still render the representative frame. Refresh the conditions card on clock-minute changes as well as data changes. Forecast settings no longer cancel pending hero animation setup.

## Validation
Firmware build passed. The new tests/test_icon_lifecycle.py exercises production functions with a checked LVGL timer test double: 20 pause/resume cycles, source changes while hidden, day/night variants, unchanged-source no-op, and repeated static artwork changes. It passed and is included in CI.

An application-only update was installed on COM12 with hash verification, preserving bootloader and settings. The device logged partly-cloudy-day, sun altitude 53.7 degrees, local time 14:28, day; the hero icon was promoted to Lottie. After being asked to check the sun icon, animation and a Sky-to-Live round trip, the user confirmed: It is good now.
