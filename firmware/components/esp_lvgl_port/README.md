# Vendored `esp_lvgl_port` (2.7.x) — touch read must not abort

Copy of the managed component `espressif/esp_lvgl_port`, present for one
change in `src/lvgl9/esp_lvgl_port_touch.c`.

## Why

Upstream reads the touch controller like this:

```c
ESP_ERROR_CHECK(esp_lcd_touch_read_data(touch_ctx->handle));
ESP_ERROR_CHECK(esp_lcd_touch_get_data(...));
```

`ESP_ERROR_CHECK` calls `abort()` on any non-`ESP_OK`. On this board the GT911
shares its I2C bus with the indoor AHT20/DHT20, and a transient read error there
therefore kills the whole firmware:

```
GT911: esp_lcd_touch_gt911_read_data(232): I2C read error!
ESP_ERROR_CHECK failed: esp_err_t 0x103 (ESP_ERR_INVALID_STATE)
  at esp_lvgl_port_touch.c line 127
abort() was called on core 1
```

A peripheral that misses a poll should drop that frame of input, not take the
panel down. Both calls now log a warning, report "released", and return; the
next poll tries again. At the LVGL refresh rate a skipped sample is invisible.

## What was changed

Only the two `ESP_ERROR_CHECK` calls in `lvgl_port_touchpad_read()`. Everything
else is identical to the managed copy in `managed_components/`.

## Cost

- Pins this component locally. The project already pins it to `~2.7.2` on
  purpose (see CLAUDE.md: 2.8.0 needs LVGL >= 9.3, 2.9.0 needs IDF >= 5.6), so
  the version was never going to float anyway.
- On upgrade: re-copy from `managed_components/` and re-apply this one change.
  `diff -r` against that directory shows the drift.

This does not paper over the I2C glitch itself — it stops the glitch being
fatal. If touch errors become frequent rather than occasional, the bus is worth
investigating on its own.
