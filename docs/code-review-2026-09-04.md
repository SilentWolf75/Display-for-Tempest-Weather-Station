# Project review — September 4, 2026

Original findings are preserved below for context. The implementation follow-up is recorded at the end.

Reviewed the current working tree, including existing uncommitted changes. Findings below are based on source inspection unless a validation result explicitly says otherwise. No firmware was flashed and no hardware behavior was verified in this review.

## Fix first

### 1. API token is sent over plain HTTP — high priority

Location: `firmware/main/tempest_rest.c:40–45`.

The station-discovery and historical-observation URLs start with `http://` and include the personal API token in their query strings. The certificate bundle configured in `rest_get()` does not encrypt an HTTP request. Even if the server redirects to HTTPS, the first request has already transmitted the token without encryption.

Use HTTPS for both endpoints and verify certificate validation. The forecast endpoint already uses HTTPS.

### 2. WebSocket fallback disconnects because of its own traffic — high priority

Locations: `firmware/main/tempest_ws.c:75–79` and `:147–153`.

Every incoming WebSocket text event calls `wx_note_udp_packet()`. The fallback polling function subsequently sees a fresh UDP timestamp and closes the WebSocket, logging that UDP resumed. This can repeatedly alternate between connection and stale-data periods when UDP is actually unavailable.

Keep separate timestamps for UDP reception and usable observations. Only the real UDP listener should update UDP health; acknowledgements and other WebSocket control messages should not count as weather observations.

### 3. Browser firmware upload uses a format the server rejects — high priority

Locations: `firmware/main/ota.c:77` and `:101–107`.

The provided form uploads `multipart/form-data`, but the update handler writes the entire request body directly into the firmware partition. Consequently, a normal browser upload sends a multipart boundary instead of the firmware header and fails validation. The raw-binary command-line upload takes a different path and can work.

Submit the selected File as a raw request body from JavaScript, with progress and error feedback, or implement a proper multipart parser. Test through the actual `/ota` page.

### 4. A broken display can still pass the OTA rollback gate — high priority

Locations: `firmware/main/main.c:170`, `:214–219`, and `:318`.

Display initialization errors are nonfatal, and the UI initialization task signals completion even if it failed to acquire the display lock. Startup nevertheless calls `ota_mark_valid()` unconditionally. An updated image that cannot bring up the screen can therefore cancel automatic rollback and leave the device headless.

Track successful display initialization and UI readiness separately from task completion. For a display product, cancel rollback only after those required checks succeed. Do not require internet availability to validate an otherwise healthy image.

### 5. Alerts can be missed or remain active after expiry — high priority

Locations: `firmware/main/nws_alerts.c:594`, `:635`, and `:737–744`.

The alert parser examines only the first entry of the returned `features` array. A severe alert in a later entry is never evaluated for display or siren playback. The code comment calls this the highest-priority alert, but the application performs no ranking itself.

Separately, the stored expiry time is only formatted for display. `nws_alerts_get_active()` returns the stored active flag without checking expiry. If polling fails after an alert expires, the old warning remains active indefinitely.

Evaluate all returned alerts, rank them explicitly, and process notification IDs individually. Expire cached alerts locally and show that alert updates are unavailable when fetching fails; distinguish that state from a confirmed all-clear.

## Other functional bugs

### 6. Turning off the web dashboard does not reliably keep it off

Locations: `firmware/main/ui/settings.c:563–577` and `firmware/main/main.c:325–326`.

The settings callback stops the server and saves the new value. However, the health loop uses `cfg_boot.web_server_enabled`, captured at startup. If the dashboard was enabled at boot, the next connected health-loop iteration restarts it, within roughly 30 seconds.

Read the current configuration in the health loop or let one service-management task own server start/stop decisions.

### 7. Historical backfill can scramble or discard live history

Locations: `firmware/main/history.c:73–88`, `:187–203`, and `firmware/main/tempest_rest.c:796–837`.

Beginning backfill clears the live ring and globally disables the rejection of older buckets. The UDP task remains active. A current observation can therefore appear between historical observations, causing the ring to jump forward and backward in time. Per-call locking prevents simultaneous memory writes but does not make the whole backfill operation atomic.

An allocation failure after `history_begin_backfill()` also returns without calling `history_end_backfill()`, leaving that mode enabled. Repeated unsuccessful fetches can repeatedly erase accumulated live history.

Build historical data in a separate buffer, merge by timestamp with live observations, and publish the result atomically. Allocate before changing shared state and restore state on every exit path.

### 8. The 24-hour graphs compress missing time

Locations: `firmware/main/history.c:130–153`, `firmware/main/ui/graphs.c:271–285`, and `firmware/main/web_server.c:197–214`.

The history reader copies stored buckets in sequence without comparing their timestamps to expected five-minute positions. Thus a long offline period appears as adjacent readings, despite comments promising gaps. Old buckets also remain until replaced by enough new buckets, so the data can span longer than 24 hours. The browser chart additionally skips missing entries and connects the remaining points.

Map readings onto a fixed time axis, expire buckets outside the requested window, and break lines across missing samples in both displays.

### 9. Rain totals do not consistently represent their labels

Locations: `firmware/main/wx_state.c:80–98`, `:233`, `:352–364`, and `firmware/main/tempest_rest.c:518–536`.

- The local seven-day total adds completed days but never removes the day that falls out of the window.
- The REST “month” total sums the entire requested past-31-days-plus-today window rather than filtering to the current calendar month.
- Zero-valued fetched totals are ignored, so a previous nonzero total cannot be corrected to zero.
- Rain today and observed daily high/low are initialized from scratch at boot; historical backfill only restores the graph ring, not these accumulators.
- Observation ingestion does not reject duplicate timestamps before adding rainfall, so a repeated observation adds the same rain again.

Use date-keyed daily buckets with explicit observed-versus-forecast provenance, accept legitimate zeros, deduplicate observations, and restore measured accumulators after reboot. Until complete data is available, label totals as partial.

### 10. Saving a ZIP code performs network work on the UI thread

Locations: `firmware/main/ui/settings.c:645` and `firmware/main/nws_alerts.c:732–735`.

The LVGL callback calls `nws_alerts_refresh()` synchronously, which resolves coordinates and fetches alerts. Network waits and the shared HTTPS mutex can block touch handling and repainting for seconds or longer during slow connections.

Signal the existing background task to refresh and return immediately from the UI callback. Present completion or failure asynchronously.

### 11. Missing sensor readings become valid zeros

Locations: `firmware/main/tempest_udp.c:64–82` and `firmware/main/wx_state.c:158–183`.

Null or absent temperature, humidity, and pressure fields default to zero, after which the observation is marked valid and fed into derived values, daily extrema, and graph history. A null temperature can therefore become a displayed 32°F reading and the day's low, rather than “unavailable.”

Validate mandatory fields and carry validity per measurement where partial observations are useful. Also filter UDP messages to the intended station/sensor when multiple hubs share the network.

## Improvements after those fixes

- Add regression tests for fallback transitions, duplicate/null observations, midnight/month rollover, backfill/live interleaving, alert ranking/expiry, runtime service toggles, and browser uploads. The existing workflow deploys the web flasher but does not run firmware regression checks.
- Standardize freshness and source labels across the panel, browser, MQTT, and CSV logs. For example, AQI has no fetched timestamp in its update path, and the web indoor display checks validity without checking age.
- Debounce persistent settings writes. `cfg_set()` avoids identical writes but still commits every changed slider value; its comment overstates the flash-write protection.
- Make station credentials/configuration and build prerequisites consistent. `secrets.h` is now included unconditionally, while documentation still describes optional `__has_include` behavior.
- Reconcile documentation with the implementation: hardware validation claims conflict between README and CLAUDE.md, and standalone OTA uses port 80 whereas the documented dashboard OTA URL uses port 8080.
- Keep enclosure dimensions and exported manufacturing files tied to a documented hardware revision. Automated geometry probes cannot establish physical fit, sensor thermal behavior, or speaker clearance on their own.

## Validation

- ESP-IDF 5.5.3 incremental build completed successfully. Existing compiled objects were reused; this was not a clean rebuild.
- App image: `0x2984d0` bytes; smallest app slot: `0x300000` bytes, with approximately 14% free.
- Existing case/check_geometry.py completed successfully: all probes passed. This checks modeled geometry, not physical board fit or printed-part tolerances.
- No on-device runtime, touch, network-failover, firmware-upload, or rollback test was performed.
- Existing user changes were preserved. This report is the only intentional source-tree addition from the review.

## Implementation follow-up

All eleven findings have been addressed in source. The changes include HTTPS
for token-bearing requests; separate fallback/UDP health; raw browser uploads
with bounded failure handling; an explicit display/UI/timer rollback gate;
ranked cached alerts with local expiry and independent notification keys;
background ZIP refresh; main-owned dashboard service changes; timestamped
history merging and gaps; measured, date-based rain totals with NVS checkpoints;
and validation/deduplication of core observations.

Additional work: freshness indicators, legitimate zero AQI, debounced settings
writes, explicit credential build dependencies, optional serial filters,
regression tests, and CI running the host and browser checks.

Coverage limitations: rainfall is measured partial coverage, saved every five
minutes (a power cut can lose the unsaved tail); retained alert and notification
lists are bounded to 16 entries; runtime service changes apply within 30 seconds.
Host tests simulate clock/storage/socket/RTOS behavior and cannot establish
physical display, flash, or transport behavior. No firmware was flashed.

Validation completed:
- Production-code C regression suite passed (MSVC host build).
- Browser raw-upload/error tests and embedded dashboard syntax/gap tests passed.
- Isolated credential tracking passed for absent, newly created, changed, and removed fake credentials.
- ESP-IDF 5.5.3 rebuild and final incremental build passed. Final app image is
  0x29a390 bytes with 0x65c70 bytes (about 13%) free in the 3 MB app slot.
- Patch whitespace check passed. Existing unused-function warnings remain.
- Enclosure files were unchanged by this implementation; the earlier geometry result still applies.
- No on-device flash, touch, network-failover, or rollback test was performed.
See tests/README.md and docs/bringup.md for remaining on-device checks.
